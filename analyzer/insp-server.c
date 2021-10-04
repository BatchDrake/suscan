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

#include "inspector/inspector.h"
#include "realtime.h"
#include "mq.h"
#include "msg.h"

/*
 * This is the server application: the worker that processes messages and
 * forwards samples to the inspector
 */

SUBOOL
suscan_inspector_sampler_loop(
    suscan_inspector_t *insp,
    const SUCOMPLEX *samp_buf,
    SUSCOUNT samp_count,
    struct suscan_mq *mq_out)
{
  struct suscan_analyzer_sample_batch_msg *msg = NULL;
  SUSDIFF fed;

  while (samp_count > 0) {
    /* Ensure the current inspector parameters are up-to-date */
    suscan_inspector_assert_params(insp);

    SU_TRYCATCH(
        (fed = suscan_inspector_feed_bulk(insp, samp_buf, samp_count)) >= 0,
        goto fail);

    if (suscan_inspector_get_output_length(insp) > insp->sample_msg_watermark) {
      /* New samples produced by sampler: send to client */
      SU_TRYCATCH(
          msg = suscan_analyzer_sample_batch_msg_new(
              insp->inspector_id,
              suscan_inspector_get_output_buffer(insp),
              suscan_inspector_get_output_length(insp)),
          goto fail);

      /* Reset size */
      insp->sampler_ptr = 0;

      SU_TRYCATCH(
          suscan_mq_write(mq_out, SUSCAN_ANALYZER_MESSAGE_TYPE_SAMPLES, msg),
          goto fail);

      msg = NULL; /* We don't own this anymore */
    }

    samp_buf   += fed;
    samp_count -= fed;
  }

  return SU_TRUE;

fail:
  if (msg != NULL)
    suscan_analyzer_sample_batch_msg_destroy(msg);

  return SU_FALSE;
}


SUBOOL
suscan_inspector_spectrum_loop(
    suscan_inspector_t *insp,
    const SUCOMPLEX *samp_buf,
    SUSCOUNT samp_count,
    struct suscan_mq *mq_out)
{
  suscan_spectsrc_t *src = NULL;
  SUSDIFF fed;

  if (insp->spectsrc_index > 0) {
    src = insp->spectsrc_list[insp->spectsrc_index - 1];

    while (samp_count > 0) {
      fed = suscan_spectsrc_feed(src, samp_buf, samp_count);

      SU_TRYCATCH(fed >= 0, goto fail);

      samp_buf   += fed;
      samp_count -= fed;
    }
  }

  return SU_TRUE;

fail:
  return SU_FALSE;
}

SUBOOL
suscan_inspector_estimator_loop(
    suscan_inspector_t *insp,
    const SUCOMPLEX *samp_buf,
    SUSCOUNT samp_count,
    struct suscan_mq *mq_out)
{
  struct suscan_analyzer_inspector_msg *msg = NULL;
  unsigned int i;
  uint64_t now;
  SUFLOAT value;
  SUFLOAT seconds;

  /* Check esimator state and update clients */
  if (insp->interval_estimator > 0) {
    now = suscan_gettime();
    seconds = (now - insp->last_estimator) * 1e-9;
    if (seconds >= insp->interval_estimator) {
      insp->last_estimator = now;
      for (i = 0; i < insp->estimator_count; ++i)
        if (suscan_estimator_is_enabled(insp->estimator_list[i])) {
          if (suscan_estimator_is_enabled(insp->estimator_list[i]))
            SU_TRYCATCH(
                suscan_estimator_feed(
                    insp->estimator_list[i],
                    samp_buf,
                    samp_count),
                goto fail);

          if (suscan_estimator_read(insp->estimator_list[i], &value)) {
            SU_TRYCATCH(
                msg = suscan_analyzer_inspector_msg_new(
                    SUSCAN_ANALYZER_INSPECTOR_MSGKIND_ESTIMATOR,
                    rand()),
                goto fail);

            msg->enabled = SU_TRUE;
            msg->estimator_id = i;
            msg->value = value;
            msg->inspector_id = insp->inspector_id;

            SU_TRYCATCH(
                suscan_mq_write(
                    mq_out,
                    SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR,
                    msg),
                goto fail);
          }
        }
    }
  }

  return SU_TRUE;

fail:
  if (msg != NULL)
    suscan_analyzer_inspector_msg_destroy(msg);

  return SU_FALSE;
}

SUPRIVATE SUBOOL
suscan_local_analyzer_on_channel_data(
    const struct sigutils_specttuner_channel *channel,
    void *private,
    const SUCOMPLEX *data,
    SUSCOUNT size)
{
  struct suscan_inspector_task_info *task_info =
      (struct suscan_inspector_task_info *) private;
  struct timeval source_time;
  SUFREQ freq;
  SUFLOAT freq_correction;

  /* Channel is not bound yet. No processing is performed */
  if (task_info == NULL)
    return SU_TRUE;

  /*
   * It is safe to close channels here: we are already protected
   * by the sched mutex.
   */
  if (task_info->inspector->state != SUSCAN_ASYNC_STATE_RUNNING) {
    /* Close channel: no FFT filtering will be performed */
    SU_TRYCATCH(
        su_specttuner_close_channel(
            task_info->sched->analyzer->stuner,
            (su_specttuner_channel_t *) channel),
        return SU_FALSE);

    /* Remove from scheduler: no further processing will take place */
    SU_TRYCATCH(
        suscan_inspsched_remove_task_info(
            task_info->sched,
            task_info),
        return SU_FALSE);

    /* Ready to destroy */
    task_info->inspector->state = SUSCAN_ASYNC_STATE_HALTED;

    /* Task info has been removed, it is safe to destroy it now */
    suscan_inspector_task_info_destroy(task_info);

    return SU_TRUE;
  }

  task_info->data = data;
  task_info->size = size;

  /* Check whether we should get source time */
  suscan_inspsched_get_source_time(task_info->sched, &source_time);

  freq = suscan_inspector_task_info_get_abs_freq(task_info); 
  if (suscan_inspector_get_correction(
        task_info->inspector,
        &source_time,
        freq,
        &freq_correction)) {

    suscan_local_analyzer_set_channel_correction(
      suscan_inspsched_get_analyzer(task_info->sched),
      (su_specttuner_channel_t *) channel, /* TODO: Fix this!! */
      freq_correction);
  }

  /* Deliver pending report */
  (void) suscan_inspector_deliver_report(
    task_info->inspector,
    &source_time,
    freq);
  
  return suscan_inspsched_queue_task(task_info->sched, task_info);
}

suscan_inspector_t *
suscan_local_analyzer_get_inspector(
    const suscan_local_analyzer_t *self,
    SUHANDLE handle)
{
  suscan_inspector_t *brinsp;

  if (handle < 0 || handle >= self->inspector_count)
    return NULL;

  brinsp = self->inspector_list[handle];

  if (brinsp != NULL && brinsp->state != SUSCAN_ASYNC_STATE_RUNNING)
    return NULL;

  return brinsp;
}

SUPRIVATE SUBOOL
suscan_local_analyzer_mark_inspector_as_dead(
    suscan_local_analyzer_t *self,
    suscan_inspector_t *insp)
{
  struct suscan_inspector_overridable_request *req = NULL;

  SU_TRYCATCH(
      suscan_local_analyzer_lock_inspector_list(self),
      return SU_FALSE);

  if ((req = suscan_inspector_get_userdata(insp)) != NULL)
    req->dead = SU_TRUE;

  suscan_local_analyzer_unlock_inspector_list(self);

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_local_analyzer_dispose_inspector_handle(
    suscan_local_analyzer_t *self,
    SUHANDLE handle)
{
  if (handle < 0 || handle >= self->inspector_count)
    return SU_FALSE;

  if (self->inspector_list[handle] == NULL)
    return SU_FALSE;

  SU_TRYCATCH(
      suscan_local_analyzer_lock_inspector_list(self),
      return SU_FALSE);

  self->inspector_list[handle] = NULL;

  suscan_local_analyzer_unlock_inspector_list(self);

  return SU_TRUE;
}

SUPRIVATE void
suscan_local_analyzer_cleanup_inspector_list_unsafe(
    suscan_local_analyzer_t *self)
{
  unsigned int i;

  for (i = 0; i < self->inspector_count; ++i)
    if (self->inspector_list[i] != NULL
        && self->inspector_list[i]->state == SUSCAN_ASYNC_STATE_HALTED) {
      suscan_inspector_destroy(self->inspector_list[i]);
      self->inspector_list[i] = NULL;
    }
}

SUPRIVATE SUBOOL
suscan_local_analyzer_open_inspector(
    suscan_local_analyzer_t *self,
    const char *class,
    const struct sigutils_channel *channel,
    struct suscan_analyzer_inspector_msg *msg)
{
  SUHANDLE hnd = -1;
  unsigned int fs = suscan_analyzer_get_samp_rate(self->parent);
  su_specttuner_channel_t *schan = NULL;
  suscan_inspector_t *new = NULL;
  unsigned int i;

  /* Open a channel to feed this inspector */
  SU_TRYCATCH(
      schan = suscan_local_analyzer_open_channel_ex(
          self,
          channel,
          msg->precise,
          suscan_local_analyzer_on_channel_data,
          NULL),
      goto fail);

  /* Populate channel properties */
  msg->fs = fs;
  msg->equiv_fs = fs / su_specttuner_channel_get_decimation(schan);
  msg->bandwidth = SU_NORM2ABS_FREQ(
      fs,
      SU_ANG2NORM_FREQ(su_specttuner_channel_get_bw(schan)));
  msg->lo = SU_NORM2ABS_FREQ(
      fs,
      SU_ANG2NORM_FREQ(su_specttuner_channel_get_f0(schan)));

  if (msg->lo > .5 * fs)
    msg->lo -= fs;

  /*
   * Channel has been opened, and now we have all the required information
   * to create a new inspector for it.
   */
  SU_TRYCATCH(
      new = suscan_inspector_new(
          class,
          suscan_analyzer_get_samp_rate(self->parent),
          schan,
          self->parent->mq_out),
      goto fail);

  /************************* POPULATE MESSAGE ********************************/
  SU_TRYCATCH(msg->config = suscan_inspector_create_config(new), goto fail);
  SU_TRYCATCH(suscan_inspector_get_config(new, msg->config), goto fail);

  /* Add estimator list */
  for (i = 0; i < new->estimator_count; ++i)
    SU_TRYCATCH(
        PTR_LIST_APPEND_CHECK(
            msg->estimator,
            (void *) new->estimator_list[i]->classptr) != -1,
        goto fail);

  /* Add applicable spectrum sources */
  for (i = 0; i < new->spectsrc_count; ++i)
    SU_TRYCATCH(
        PTR_LIST_APPEND_CHECK(
            msg->spectsrc,
            (void *) new->spectsrc_list[i]->classptr) != -1,
        goto fail);

  /*
   * Append inspector to analyzer's internal list and get a handle.
   * TODO: Find inspectors in HALTED state, and free them
   */

  suscan_local_analyzer_lock_inspector_list(self);

  suscan_local_analyzer_cleanup_inspector_list_unsafe(self);

  if ((hnd = PTR_LIST_APPEND_CHECK(self->inspector, new)) == -1)
    goto fail;

  suscan_local_analyzer_unlock_inspector_list(self);

  msg->handle = hnd;

  /*
   * Data may arrive to the channel in this point. But since it is not
   * bound yet, it will be discarded.
   */
  SU_TRYCATCH(
      suscan_local_analyzer_bind_inspector_to_channel(self, schan, new),
      goto fail);

  /*
   * At this point: inspector's state is RUNNING, and it is being handled
   * concurrently by the inspector scheduler.
   */
  return SU_TRUE;

fail:
  if (schan != NULL)
    (void) suscan_local_analyzer_close_channel(self, schan);

  if (hnd != -1)
    (void) suscan_local_analyzer_dispose_inspector_handle(self, hnd);

  return SU_FALSE;
}


#ifdef SUSCAN_ANALYZER_INSPECTOR_BLOCKING_MESSAGES
SUPRIVATE SUBOOL
suscan_local_analyzer_set_inspector_freq(
    suscan_local_analyzer_t *analyzer,
    suscan_inspector_t *insp,
    SUFREQ freq)
{
  SUBOOL mutex_acquired = SU_FALSE;
  SUBOOL ok = SU_FALSE;
  SUFLOAT f0;

  f0 = SU_NORM2ANG_FREQ(
      SU_ABS2NORM_FREQ(suscan_analyzer_get_samp_rate(analyzer->parent), freq));

  if (f0 < 0)
    f0 += 2 * PI;

  SU_TRYCATCH(suscan_local_analyzer_lock_loop(analyzer), goto done);
  mutex_acquired = SU_TRUE;

  /* vvvvvvvvvvvvvvvvvvvvvv Set frequency start vvvvvvvvvvvvvvvvvvvvvv */
  su_specttuner_set_channel_freq(
      analyzer->stuner,
      suscan_inspector_get_channel(insp),
      f0);

  /* ^^^^^^^^^^^^^^^^^^^^^ Set frequency end ^^^^^^^^^^^^^^^^^^^^^^^^^ */
  ok = SU_TRUE;

done:
  if (mutex_acquired)
    suscan_local_analyzer_unlock_loop(analyzer);

  return ok;
}

SUPRIVATE SUBOOL
suscan_local_analyzer_set_inspector_bandwidth(
    suscan_local_analyzer_t *analyzer,
    suscan_inspector_t *insp,
    SUFREQ bandwidth)
{
  SUBOOL mutex_acquired = SU_FALSE;
  SUBOOL ok = SU_FALSE;
  SUFLOAT relbw;

  relbw = SU_NORM2ANG_FREQ(
      SU_ABS2NORM_FREQ(
          suscan_analyzer_get_samp_rate(analyzer->parent),
          bandwidth));

  if (relbw < 0)
    relbw += 2 * PI;

  SU_TRYCATCH(suscan_local_analyzer_lock_loop(analyzer), goto done);
  mutex_acquired = SU_TRUE;

  /* vvvvvvvvvvvvvvvvvvvvvv Set bandwidth start vvvvvvvvvvvvvvvvvvvvvv */
  su_specttuner_set_channel_bandwidth(
      analyzer->stuner,
      suscan_inspector_get_channel(insp),
      relbw);

  SU_TRYCATCH(suscan_inspector_notify_bandwidth(insp, bandwidth), goto done);
  /* ^^^^^^^^^^^^^^^^^^^^^ Set bandwidth end ^^^^^^^^^^^^^^^^^^^^^^^^^ */

  ok = SU_TRUE;

done:
  if (mutex_acquired)
    suscan_local_analyzer_unlock_loop(analyzer);

  return ok;
}
#endif /* SUSCAN_ANALYZER_INSPECTOR_BLOCKING_MESSAGES */


/*
 * We have ownership on msg, this messages are urgent: they are placed
 * in the beginning of the queue
 */

/*
 * TODO: !!!!!!!!! Protect access to inspector object !!!!!!!!!!!!!!!
 */

SUBOOL
suscan_local_analyzer_parse_inspector_msg(
    suscan_local_analyzer_t *analyzer,
    struct suscan_analyzer_inspector_msg *msg)
{
  suscan_inspector_t *insp = NULL;
  suscan_frequency_corrector_t *corrector = NULL;
  xyz_t qth;
  SUBOOL ok = SU_FALSE;
  SUBOOL mutex_acquired = SU_FALSE;

  switch (msg->kind) {
    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_OPEN:
      if (!suscan_local_analyzer_open_inspector(
              analyzer,
              msg->class_name,
              &msg->channel,
              msg)) {
        msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_INVALID_CHANNEL;
      } else {
        msg->channel.ft = suscan_source_get_freq(analyzer->source);
      }
      break;

    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SET_ID:
      if ((insp = suscan_local_analyzer_get_inspector(
          analyzer,
          msg->handle)) == NULL) {
        /* No such handle */
        msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_HANDLE;
      } else {
        /* TODO: PROTECT!!!!!!!! */
        insp->inspector_id = msg->inspector_id;
      }
      break;

    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_ESTIMATOR:
      if ((insp = suscan_local_analyzer_get_inspector(
          analyzer,
          msg->handle)) == NULL) {
        /* No such handle */
        msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_HANDLE;
      } else if (msg->estimator_id >= insp->estimator_count) {
        msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_OBJECT;
      } else {
        suscan_estimator_set_enabled(
            insp->estimator_list[msg->estimator_id],
            msg->enabled);
      }
      break;

    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SPECTRUM:
      if ((insp = suscan_local_analyzer_get_inspector(
          analyzer,
          msg->handle)) == NULL) {
        /* No such handle */
        msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_HANDLE;
      } else if (msg->spectsrc_id > insp->spectsrc_count) {
        msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_OBJECT;
      } else {
        insp->spectsrc_index = msg->spectsrc_id;
      }
      break;

    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_GET_CONFIG:
      if ((insp = suscan_local_analyzer_get_inspector(
          analyzer,
          msg->handle)) == NULL) {
        /* No such handle */
        msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_HANDLE;
      } else {
        /* Retrieve current inspector params */
        msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SET_CONFIG;

        SU_TRYCATCH(
            msg->config = suscan_inspector_create_config(insp),
            goto done);

        SU_TRYCATCH(suscan_inspector_get_config(insp, msg->config), goto done);
      }
      break;

    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SET_CONFIG:
      if ((insp = suscan_local_analyzer_get_inspector(
          analyzer,
          msg->handle)) == NULL) {
        /* No such handle */
        msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_HANDLE;
      } else {
        /* Configuration stored as a config request */
        SU_TRYCATCH(suscan_inspector_set_config(insp, msg->config), goto done);
      }
      break;

    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SET_TLE:
      if ((insp = suscan_local_analyzer_get_inspector(
          analyzer,
          msg->handle)) == NULL) {
        /* No such handle */
        msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_HANDLE;
      } else if (!msg->tle_enable) {
        suscan_inspector_set_corrector(insp, NULL);
      } else if (!suscan_get_qth(&qth)) {
          msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_INVALID_CORRECTION;
          SU_WARNING("TLE request rejected. No QTH configured.\n");
      } else {
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
      }
      break;

    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_RESET_EQUALIZER:
      if ((insp = suscan_local_analyzer_get_inspector(
          analyzer,
          msg->handle)) == NULL) {
        /* No such handle */
        msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_HANDLE;
      } else {
        /* Reset equalizer */
        suscan_inspector_reset_equalizer(insp);
      }
      break;

    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SET_WATERMARK:
      if ((insp = suscan_local_analyzer_get_inspector(
          analyzer,
          msg->handle)) == NULL) {
        /* No such handle */
        msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_HANDLE;
      } else {
        if (!suscan_inspector_set_msg_watermark(insp, msg->watermark))
          msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_INVALID_ARGUMENT;
      }
      break;

    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SET_FREQ:
#ifdef SUSCAN_ANALYZER_INSPECTOR_BLOCKING_MESSAGES
      if ((insp = suscan_local_analyzer_get_inspector(
          analyzer,
          msg->handle)) == NULL) {
        /* No such handle */
        msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_HANDLE;
      } else {
        SU_TRYCATCH(
            suscan_local_analyzer_set_inspector_freq(
                analyzer,
                insp,
                msg->channel.fc - msg->channel.ft),
            msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_INVALID_ARGUMENT);
      }
#else
      SU_TRYCATCH(
          suscan_local_analyzer_set_inspector_freq_overridable(
              analyzer,
              msg->handle,
              msg->channel.fc - msg->channel.ft),
          msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_INVALID_ARGUMENT);
#endif /* SUSCAN_ANALYZER_INSPECTOR_BLOCKING_MESSAGES */
      break;

    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SET_BANDWIDTH:
#ifdef SUSCAN_ANALYZER_INSPECTOR_BLOCKING_MESSAGES
      if ((insp = suscan_local_analyzer_get_inspector(
          analyzer,
          msg->handle)) == NULL) {
        /* No such handle */
        msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_HANDLE;
      } else if (msg->channel.bw >= insp->samp_info.equiv_fs){
        msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_INVALID_ARGUMENT;
      } else {
        SU_TRYCATCH(
            suscan_local_analyzer_set_inspector_bandwidth(
                analyzer,
                insp,
                msg->channel.bw),
            msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_INVALID_ARGUMENT);
      }
#else
      SU_TRYCATCH(
          suscan_local_analyzer_set_inspector_bandwidth_overridable(
              analyzer,
              msg->handle,
              msg->channel.bw),
          msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_INVALID_ARGUMENT);
#endif /* SUSCAN_ANALYZER_INSPECTOR_BLOCKING_MESSAGES */

      break;

    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_CLOSE:
      if ((insp = suscan_local_analyzer_get_inspector(
          analyzer,
          msg->handle)) == NULL) {
        msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_HANDLE;
      } else {
        msg->inspector_id = insp->inspector_id;

        if (insp->state == SUSCAN_ASYNC_STATE_HALTED) {
          /*
           * Inspector has been halted. It's safe to dispose the handle
           * and free the object.
           */
          (void) suscan_local_analyzer_dispose_inspector_handle(
              analyzer,
              msg->handle);
          suscan_inspector_destroy(insp);
        } else {
          /*
           * Inspector is still running. Mark it as halting, so it will not
           * come back to the worker queue. Also, mark overridable request
           * as dead, so we prevent dealing with the inspector while it is
           * being removed.
           */
          SU_TRYCATCH(
              suscan_local_analyzer_mark_inspector_as_dead(analyzer, insp),
              goto done);
          insp->state = SUSCAN_ASYNC_STATE_HALTING;
        }

        /* We can't trust the inspector contents from here on out */
        insp = NULL;
      }
      break;

    default:
      msg->status = msg->kind;
      msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_KIND;
  }

  /*
   * If request has referenced an existing inspector, we include the
   * inspector ID in the response.
   */
  if (insp != NULL)
    msg->inspector_id = insp->inspector_id;

  if (!suscan_mq_write(
      analyzer->parent->mq_out,
      SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR,
      msg))
    goto done;

  msg = NULL;

  ok = SU_TRUE;

done:
  if (mutex_acquired)
    suscan_local_analyzer_unlock_loop(analyzer);

  return ok;
}
