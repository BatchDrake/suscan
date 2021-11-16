/*

  Copyright (C) 2017 Gonzalo José Carracedo Carballal

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation, version 3.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this program.  If not, see
  <http://www.gnu.org/licenses/>

*/

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <stdint.h>

#define SU_LOG_DOMAIN "suscan-inspector-server"

#include <sigutils/sigutils.h>
#include <analyzer/impl/local.h>
#include <correctors/tle.h>
#include <sgdp4/sgdp4.h>

#include "inspector/factory.h"
#include "inspector/overridable.h"
#include "realtime.h"
#include "mq.h"
#include "msg.h"
#include "src/suscan.h"

/*
 * This is the server application: the worker that processes messages and
 * forwards samples to the inspector
 */

typedef SUBOOL (*suscan_inspector_msg_callback_t) (
  suscan_local_analyzer_t *self, /* Context */
  struct suscan_analyzer_inspector_msg *msg /* In / Out */);

SUPRIVATE suscan_inspector_msg_callback_t g_insp_callbacks[SUSCAN_ANALYZER_INSPECTOR_MSGKIND_COUNT];

#define MSGCB_NAME(name) JOIN(suscan_insp_server_cb_, name)
#define DEF_MSGCB(name)                                         \
  SUPRIVATE SUBOOL MSGCB_NAME(name)(                            \
    suscan_local_analyzer_t *self, /* Context */                \
    struct suscan_analyzer_inspector_msg *msg /* In / Out */)

#define INIT_MSGCB(kind)                            \
  g_insp_callbacks[                                 \
    JOIN(SUSCAN_ANALYZER_INSPECTOR_MSGKIND_, kind)] \
  = MSGCB_NAME(kind)

/************************** Handle list API *************************/
SUHANDLE
suscan_local_analyzer_register_inspector(
  suscan_local_analyzer_t *self,
  suscan_inspector_t *insp)
{
  SUHANDLE handle = -1;
  SUHANDLE new_handle;
  SUBOOL mutex_acquired = SU_FALSE;

  SU_TRYCATCH(pthread_mutex_lock(&self->insp_mutex) == 0, goto done);
  mutex_acquired = SU_TRUE;

  /* Generate a new handle */
  do {
    new_handle = rand() ^ (rand() << 16);
  } while(
    new_handle != -1 
    && rbtree_search(self->insp_hash, new_handle, RB_EXACT) != 0);

  SU_TRYCATCH(
    rbtree_insert(
      self->insp_hash,
      new_handle,
      insp) == 0,
    goto done);
  
  SU_REF(insp, global_handle);

  handle = new_handle;

done:
  if (mutex_acquired)
    (void) pthread_mutex_unlock(&self->insp_mutex);

  return handle;
}

SUBOOL
suscan_local_analyzer_unregister_inspector(
  suscan_local_analyzer_t *self,
  SUHANDLE handle)
{
  suscan_inspector_t *insp = NULL;
  struct rbtree_node *node = NULL;
  SUBOOL mutex_acquired = SU_FALSE;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(pthread_mutex_lock(&self->insp_mutex) == 0, goto done);
  mutex_acquired = SU_TRUE;

  if ((node = rbtree_search(self->insp_hash, handle, RB_EXACT)) == NULL)
    goto done;
  
  insp = node->data;
  node->data = NULL;

  SU_DEREF(insp, global_handle);

  ok = SU_TRUE;

done:
  if (mutex_acquired)
    (void) pthread_mutex_unlock(&self->insp_mutex);

  return ok;
}

suscan_inspector_t *
suscan_local_analyzer_acquire_inspector(
  suscan_local_analyzer_t *self,
  SUHANDLE handle)
{
  suscan_inspector_t *insp = NULL;
  struct rbtree_node *node = NULL;
  SUBOOL mutex_acquired = SU_FALSE;

  SU_TRYCATCH(pthread_mutex_lock(&self->insp_mutex) == 0, goto done);
  mutex_acquired = SU_TRUE;

  if ((node = rbtree_search(self->insp_hash, handle, RB_EXACT)) == NULL)
    goto done;

  if ((insp = node->data) == NULL)
    goto done;

  SU_REF(insp, global_handle_user);

done:
  if (mutex_acquired)
    (void) pthread_mutex_unlock(&self->insp_mutex);

  return insp;
}

void
suscan_local_analyzer_return_inspector(
  suscan_local_analyzer_t *self,
  suscan_inspector_t *insp)
{
  SU_DEREF(insp, global_handle_user);
}

void
suscan_local_analyzer_destroy_global_handles_unsafe(
  suscan_local_analyzer_t *self)
{
  struct rbtree_node *node;
  suscan_inspector_t *insp = NULL;

  if (self->insp_hash != NULL) {
    node = rbtree_get_first(self->insp_hash);

    while (node != NULL) {
      insp = node->data;

      if (insp != NULL)
        SU_DEREF(insp, global_handle);

      node = rbtree_node_next(node);
    }
  }

  if (self->insp_init)
    (void) pthread_mutex_destroy(&self->insp_mutex);
}

/************************** Inspector Server *************************/
SUPRIVATE suscan_inspector_t *
suscan_local_analyzer_insp_from_msg(
  suscan_local_analyzer_t *self,
  struct suscan_analyzer_inspector_msg *msg)
{
  suscan_inspector_t *insp = NULL;
  
  insp = suscan_local_analyzer_acquire_inspector(self, msg->handle);
  if (insp == NULL)
    msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_HANDLE;
  else
    msg->inspector_id = insp->inspector_id;

  return insp;
}

DEF_MSGCB(OPEN)
{
  suscan_inspector_t *insp = NULL;
  unsigned int fs = suscan_analyzer_get_samp_rate(self->parent);
  struct suscan_inspector_sampling_info samp_info;
  unsigned int i;
  SUHANDLE handle;
  SUBOOL ok = SU_FALSE;

  /* 
   * TODO: in the future, conditionally select the inspector
   * factory to use. 
   */

  if ((insp = suscan_inspector_factory_open(
    self->insp_factory,
    msg->class_name,
    &msg->channel,
    msg->precise)) == NULL) {
    SU_ERROR("Failed to open inspector\n");
    msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_INVALID_CHANNEL;
    return SU_TRUE;
  }

  handle = suscan_local_analyzer_register_inspector(self, insp);
  
  if (handle == -1) {
    SU_ERROR("Could not register inspector globally\n");
    suscan_inspector_factory_halt_inspector(self->insp_factory, insp);

    msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_INVALID_ARGUMENT;
    return SU_TRUE;
  }
  
  /* All went well. Populate message and leave */
  suscan_inspector_get_sampling_info(insp, &samp_info);

  msg->handle    = handle;
  msg->fs        = fs;
  msg->equiv_fs  = samp_info.equiv_fs;
  msg->bandwidth = SU_NORM2ABS_FREQ(fs, samp_info.bw_bd);
  msg->lo        = SU_NORM2ABS_FREQ(fs, samp_info.f0);
  
  if (msg->lo > .5 * fs)
    msg->lo -= fs;
  
  msg->channel.ft = self->source_info.frequency;

  /* Add applicable estimators */
  for (i = 0; i < insp->estimator_count; ++i)
    SU_TRYCATCH(
        PTR_LIST_APPEND_CHECK(
            msg->estimator,
            (void *) insp->estimator_list[i]->classptr) != -1,
        goto done);

  /* Add applicable spectrum sources */
  for (i = 0; i < insp->spectsrc_count; ++i)
    SU_TRYCATCH(
        PTR_LIST_APPEND_CHECK(
            msg->spectsrc,
            (void *) insp->spectsrc_list[i]->classptr) != -1,
        goto done);

  SU_TRYCATCH(
    msg->config = suscan_inspector_create_config(insp),
    goto done);
  
  SU_TRYCATCH(suscan_inspector_get_config(insp, msg->config), goto done);

  ok = SU_TRUE;

done:
  return ok;
}

DEF_MSGCB(SET_ID)
{
  suscan_inspector_t *insp = NULL;
  
  insp = suscan_local_analyzer_acquire_inspector(self, msg->handle);
  if (insp == NULL) {
    msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_HANDLE;
    goto done;
  }
  
  insp->inspector_id = msg->inspector_id;

done:
  if (insp != NULL)
    suscan_local_analyzer_return_inspector(self, insp);
  
  return SU_TRUE;
}

DEF_MSGCB(ESTIMATOR)
{
  suscan_inspector_t *insp = NULL;
  
  if ((insp = suscan_local_analyzer_insp_from_msg(self, msg)) == NULL)
    goto done;
  
  if (msg->estimator_id < insp->estimator_count)
    suscan_estimator_set_enabled(
      insp->estimator_list[msg->estimator_id],
      msg->enabled);
  else
    msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_OBJECT;
  
done:
  if (insp != NULL)
    suscan_local_analyzer_return_inspector(self, insp);
  
  return SU_TRUE;
}

DEF_MSGCB(SPECTRUM)
{
  suscan_inspector_t *insp = NULL;
  
  if ((insp = suscan_local_analyzer_insp_from_msg(self, msg)) == NULL)
    goto done;
  
  if (msg->spectsrc_id <= insp->spectsrc_count)
    insp->spectsrc_index = msg->spectsrc_id;
  else
    msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_OBJECT;

done:
  if (insp != NULL)
    suscan_local_analyzer_return_inspector(self, insp);
  
  return SU_TRUE;
}

DEF_MSGCB(GET_CONFIG)
{
  suscan_inspector_t *insp = NULL;
  SUBOOL ok = SU_FALSE;
  
  if ((insp = suscan_local_analyzer_insp_from_msg(self, msg)) == NULL)
    goto done;
  
  msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SET_CONFIG;
  SU_TRYCATCH(
    msg->config = suscan_inspector_create_config(insp),
    goto done);
  
  SU_TRYCATCH(suscan_inspector_get_config(insp, msg->config), goto done);
  
  ok = SU_TRUE;
  
done:
  if (insp != NULL)
    suscan_local_analyzer_return_inspector(self, insp);
  
  return ok;
}

DEF_MSGCB(SET_CONFIG)
{
  suscan_inspector_t *insp = NULL;
  SUBOOL ok = SU_FALSE;
  
  if ((insp = suscan_local_analyzer_insp_from_msg(self, msg)) == NULL)
    goto done;
  
  SU_TRYCATCH(suscan_inspector_set_config(insp, msg->config), goto done);

  ok = SU_TRUE;
  
done:
  if (insp != NULL)
    suscan_local_analyzer_return_inspector(self, insp);
  
  return ok;
}

DEF_MSGCB(SET_TLE)
{
  suscan_inspector_t *insp = NULL;
  suscan_frequency_corrector_t *corrector = NULL;
  xyz_t qth;
  
  if ((insp = suscan_local_analyzer_insp_from_msg(self, msg)) == NULL)
    goto done;
  
  /* Disable TLE */
  if (!msg->tle_enable) {
    suscan_inspector_set_corrector(insp, NULL);
    goto done;
  }

  /* QTH config is mandatory */
  if (!suscan_get_qth(&qth)) {
    msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_INVALID_CORRECTION;
    SU_WARNING("TLE request rejected. No QTH configured.\n");
    goto done;
  }

  /* Lookup this corrector */
  corrector = suscan_frequency_corrector_new(
    "tle",
    SUSCAN_TLE_CORRECTOR_MODE_ORBIT,
    &qth,
    &msg->tle_orbit);
  
  if (corrector == NULL 
      || !suscan_inspector_set_corrector(insp, corrector))
    msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_INVALID_CORRECTION;
  else
    corrector = NULL;

done:
  if (insp != NULL)
    suscan_local_analyzer_return_inspector(self, insp);

  if (corrector != NULL)
    suscan_frequency_corrector_destroy(corrector);
  
  return SU_TRUE;
}

DEF_MSGCB(RESET_EQUALIZER)
{
  suscan_inspector_t *insp = NULL;
  
  if ((insp = suscan_local_analyzer_insp_from_msg(self, msg)) == NULL)
    goto done;
  
  suscan_inspector_reset_equalizer(insp);

done:
  if (insp != NULL)
    suscan_local_analyzer_return_inspector(self, insp);
  
  return SU_TRUE;
}

DEF_MSGCB(SET_WATERMARK)
{
  suscan_inspector_t *insp = NULL;
  
  if ((insp = suscan_local_analyzer_insp_from_msg(self, msg)) == NULL)
    goto done;
  
  if (!suscan_inspector_set_msg_watermark(insp, msg->watermark))
      msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_INVALID_ARGUMENT;

done:
  if (insp != NULL)
    suscan_local_analyzer_return_inspector(self, insp);
  
  return SU_TRUE;
}

DEF_MSGCB(SET_FREQ)
{
  struct suscan_inspector_overridable_request *req = NULL;
  suscan_inspector_t *insp = NULL;
  
  if ((insp = suscan_local_analyzer_insp_from_msg(self, msg)) == NULL)
    goto done;
  
  SU_TRYCATCH(
    req = suscan_inspector_request_manager_acquire_overridable(
      &self->insp_reqmgr,
      insp),
    goto done);

  /* Frequency is always relative to the center freq */
  req->freq_request = SU_TRUE;
  req->new_freq     = msg->channel.fc - msg->channel.ft;

done:
  if (req != NULL)
    suscan_inspector_request_manager_submit_overridable(
      &self->insp_reqmgr,
      req);

  if (insp != NULL)
    suscan_local_analyzer_return_inspector(self, insp);
  
  return SU_TRUE;
}

DEF_MSGCB(SET_BANDWIDTH)
{
  struct suscan_inspector_overridable_request *req = NULL;
  suscan_inspector_t *insp = NULL;
  
  if ((insp = suscan_local_analyzer_insp_from_msg(self, msg)) == NULL)
    goto done;
  
  SU_TRYCATCH(
    req = suscan_inspector_request_manager_acquire_overridable(
      &self->insp_reqmgr,
      insp),
    goto done);

  req->bandwidth_request = SU_TRUE;
  req->new_bandwidth     = msg->channel.bw;

done:
  if (req != NULL)
    suscan_inspector_request_manager_submit_overridable(
      &self->insp_reqmgr,
      req);

  if (insp != NULL)
    suscan_local_analyzer_return_inspector(self, insp);
  
  return SU_TRUE;
}

DEF_MSGCB(CLOSE)
{
  suscan_inspector_t *insp = NULL;
  
  if ((insp = suscan_local_analyzer_insp_from_msg(self, msg)) == NULL)
    goto done;
  
  SU_TRYCATCH(
    suscan_inspector_factory_halt_inspector(self->insp_factory, insp),
    goto done);

  SU_TRYCATCH(
    suscan_inspector_request_manager_clear_requests(&self->insp_reqmgr, insp),
    goto done);

  SU_TRYCATCH(
    suscan_local_analyzer_unregister_inspector(self, msg->handle),
    goto done);

done:
  if (insp != NULL)
    suscan_local_analyzer_return_inspector(self, insp);
  
  return SU_TRUE;
}

SUBOOL
suscan_local_analyzer_parse_inspector_msg(
  suscan_local_analyzer_t *self,
  struct suscan_analyzer_inspector_msg *msg)
{
  SUBOOL ok = SU_FALSE;
  
  if (msg->kind < 0
      || msg->kind >= SUSCAN_ANALYZER_INSPECTOR_MSGKIND_COUNT
      || g_insp_callbacks[msg->kind] == NULL) {
    msg->status = msg->kind;
    msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_KIND;
  } else {
    /* TODO: Report callback internal error? */
    SU_TRYCATCH(
      (g_insp_callbacks[msg->kind])(self, msg),
      goto done);
  }

  SU_TRYCATCH(
    suscan_mq_write(
      self->parent->mq_out,
      SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR,
      msg),
    goto done);
  msg = NULL;
    
  ok = SU_TRUE;
  
done:
  if (msg != NULL)
    suscan_analyzer_inspector_msg_destroy(msg);
  
  return ok;
}

SUBOOL
suscan_insp_server_init(void)
{
  INIT_MSGCB(OPEN);
  INIT_MSGCB(SET_ID);
  INIT_MSGCB(ESTIMATOR);
  INIT_MSGCB(SPECTRUM);
  INIT_MSGCB(GET_CONFIG);
  INIT_MSGCB(SET_CONFIG);
  INIT_MSGCB(SET_TLE);
  INIT_MSGCB(RESET_EQUALIZER);
  INIT_MSGCB(SET_WATERMARK);
  INIT_MSGCB(SET_FREQ);
  INIT_MSGCB(SET_BANDWIDTH);
  INIT_MSGCB(CLOSE);
  
  return SU_TRUE;
}
