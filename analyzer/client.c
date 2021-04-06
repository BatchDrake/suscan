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

/*
 * This is the client interface: provides helper functions to call
 * different methods in the analyzer thread
 */
#define SU_LOG_DOMAIN "analyzer-client"

#include <sigutils/sigutils.h>

#include "inspector/inspector.h"
#include "mq.h"
#include "msg.h"

/**************************** Configuration methods **************************/
SUBOOL
suscan_analyzer_set_params_async(
    suscan_analyzer_t *analyzer,
    const struct suscan_analyzer_params *params,
    uint32_t req_id)
{
  struct suscan_analyzer_params *dup = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(dup = malloc(sizeof(struct suscan_analyzer_params)), goto done);

  *dup = *params;

  if (!suscan_analyzer_write(
      analyzer,
      SUSCAN_ANALYZER_MESSAGE_TYPE_PARAMS,
      dup)) {
    SU_ERROR("Failed to send set_params command\n");
    goto done;
  }

  dup = NULL;

  ok = SU_TRUE;

done:
  if (dup != NULL)
    free(dup);

  return ok;
}

SUBOOL
suscan_analyzer_set_throttle_async(
    suscan_analyzer_t *analyzer,
    SUSCOUNT samp_rate,
    uint32_t req_id)
{
  struct suscan_analyzer_throttle_msg *throttle = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      throttle = malloc(sizeof(struct suscan_analyzer_throttle_msg)),
      goto done);

  throttle->samp_rate = samp_rate;

  if (!suscan_analyzer_write(
      analyzer,
      SUSCAN_ANALYZER_MESSAGE_TYPE_THROTTLE,
      throttle)) {
    SU_ERROR("Failed to send throttle command\n");
    goto done;
  }

  throttle = NULL;

  ok = SU_TRUE;

done:
  if (throttle != NULL)
    free(throttle);

  return ok;
}

/****************************** Inspector methods ****************************/
SUBOOL
suscan_analyzer_open_ex_async(
    suscan_analyzer_t *analyzer,
    const char *class,
    const struct sigutils_channel *channel,
    SUBOOL precise,
    uint32_t req_id)
{
  struct suscan_analyzer_inspector_msg *req = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      req = suscan_analyzer_inspector_msg_new(
          SUSCAN_ANALYZER_INSPECTOR_MSGKIND_OPEN,
          req_id),
      goto done);

  SU_TRYCATCH(req->class_name = strdup(class), goto done);

  req->channel = *channel;
  req->precise = precise;

  if (!suscan_analyzer_write(
      analyzer,
      SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR,
      req)) {
    SU_ERROR("Failed to send open command\n");
    goto done;
  }

  req = NULL; /* Now it belongs to the queue */

  ok = SU_TRUE;

done:
  if (req != NULL)
    suscan_analyzer_inspector_msg_destroy(req);

  return ok;
}

SUBOOL
suscan_analyzer_open_async(
    suscan_analyzer_t *analyzer,
    const char *class,
    const struct sigutils_channel *channel,
    uint32_t req_id)
{
  return suscan_analyzer_open_ex_async(
      analyzer,
      class,
      channel,
      SU_FALSE,
      req_id);
}

SUHANDLE
suscan_analyzer_open(
    suscan_analyzer_t *analyzer,
    const char *class,
    const struct sigutils_channel *channel)
{
  struct suscan_analyzer_inspector_msg *resp = NULL;
  uint32_t req_id = rand();
  SUHANDLE handle = -1;

  SU_TRYCATCH(
      suscan_analyzer_open_async(analyzer, class, channel, req_id),
      goto done);

  SU_TRYCATCH(
      resp = suscan_analyzer_read_inspector_msg(analyzer),
      goto done);

  if (resp->req_id != req_id) {
    SU_ERROR("Unmatched response received\n");
    goto done;
  } else if (resp->kind != SUSCAN_ANALYZER_INSPECTOR_MSGKIND_OPEN) {
    SU_ERROR("Unexpected message kind\n");
    goto done;
  }

  handle = resp->handle;

done:
  if (resp != NULL)
    suscan_analyzer_inspector_msg_destroy(resp);

  return handle;
}

SUBOOL
suscan_analyzer_close_async(
    suscan_analyzer_t *analyzer,
    SUHANDLE handle,
    uint32_t req_id)
{
  struct suscan_analyzer_inspector_msg *req = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      req = suscan_analyzer_inspector_msg_new(
          SUSCAN_ANALYZER_INSPECTOR_MSGKIND_CLOSE,
          req_id),
      goto done);

  req->handle = handle;

  if (!suscan_analyzer_write(
      analyzer,
      SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR,
      req)) {
    SU_ERROR("Failed to send close command\n");
    goto done;
  }

  req = NULL;

  ok = SU_TRUE;

done:
  if (req != NULL)
    suscan_analyzer_inspector_msg_destroy(req);

  return ok;
}

SUBOOL
suscan_analyzer_close(
    suscan_analyzer_t *analyzer,
    SUHANDLE handle)
{

  struct suscan_analyzer_inspector_msg *resp = NULL;
  uint32_t req_id = rand();
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      suscan_analyzer_close_async(analyzer, handle, req_id),
      goto done);

  SU_TRYCATCH(
      resp = suscan_analyzer_read_inspector_msg(analyzer),
      goto done);

  if (resp->req_id != req_id) {
    SU_ERROR("Unmatched response received\n");
    goto done;
  }

  if (resp->kind == SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_HANDLE) {
    SU_WARNING("Wrong handle passed to analyzer\n");
    goto done;
  } else if (resp->kind != SUSCAN_ANALYZER_INSPECTOR_MSGKIND_CLOSE) {
    SU_ERROR("Unexpected message kind\n");
    goto done;
  }

  ok = SU_TRUE;

done:
  if (resp != NULL)
    suscan_analyzer_inspector_msg_destroy(resp);

  return ok;
}

SUBOOL
suscan_analyzer_set_inspector_bandwidth_async(
    suscan_analyzer_t *analyzer,
    SUHANDLE handle,
    SUFREQ bw,
    uint32_t req_id)
{
  struct suscan_analyzer_inspector_msg *req = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      req = suscan_analyzer_inspector_msg_new(
          SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SET_BANDWIDTH,
          req_id),
      goto done);

  req->handle = handle;
  req->channel.bw = bw;

  if (!suscan_analyzer_write(
      analyzer,
      SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR,
      req)) {
    SU_ERROR("Failed to send set_bw command\n");
    goto done;
  }

  req = NULL; /* Now it belongs to the queue */

  ok = SU_TRUE;

done:
  if (req != NULL)
    suscan_analyzer_inspector_msg_destroy(req);

  return ok;
}

SUBOOL
suscan_analyzer_set_inspector_freq_async(
    suscan_analyzer_t *analyzer,
    SUHANDLE handle,
    SUFREQ freq,
    uint32_t req_id)
{
  struct suscan_analyzer_inspector_msg *req = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      req = suscan_analyzer_inspector_msg_new(
          SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SET_FREQ,
          req_id),
      goto done);

  req->handle = handle;
  req->channel.fc = freq;
  req->channel.ft = 0;

  if (!suscan_analyzer_write(
      analyzer,
      SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR,
      req)) {
    SU_ERROR("Failed to send set_freq command\n");
    goto done;
  }

  req = NULL; /* Now it belongs to the queue */

  ok = SU_TRUE;

done:
  if (req != NULL)
    suscan_analyzer_inspector_msg_destroy(req);

  return ok;
}

SUBOOL
suscan_analyzer_set_inspector_config_async(
    suscan_analyzer_t *analyzer,
    SUHANDLE handle,
    const suscan_config_t *config,
    uint32_t req_id)
{
  struct suscan_analyzer_inspector_msg *req = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      req = suscan_analyzer_inspector_msg_new(
          SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SET_CONFIG,
          req_id),
      goto done);

  req->handle = handle;

  SU_TRYCATCH(req->config = suscan_config_new(config->desc), goto done);

  SU_TRYCATCH(suscan_config_copy(req->config, config), goto done);

  if (!suscan_analyzer_write(
      analyzer,
      SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR,
      req)) {
    SU_ERROR("Failed to send set_inspector_config command\n");
    goto done;
  }

  req = NULL;

  ok = SU_TRUE;

done:
  if (req != NULL)
    suscan_analyzer_inspector_msg_destroy(req);

  return ok;
}

SUBOOL
suscan_analyzer_set_inspector_id_async(
    suscan_analyzer_t *analyzer,
    SUHANDLE handle,
    uint32_t inspector_id,
    uint32_t req_id)
{
  struct suscan_analyzer_inspector_msg *req = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      req = suscan_analyzer_inspector_msg_new(
          SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SET_ID,
          req_id),
      goto done);

  req->handle = handle;
  req->inspector_id = inspector_id;

  if (!suscan_analyzer_write(
      analyzer,
      SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR,
      req)) {
    SU_ERROR("Failed to send set_inspector_id command\n");
    goto done;
  }

  req = NULL;

  ok = SU_TRUE;

done:
  if (req != NULL)
    suscan_analyzer_inspector_msg_destroy(req);

  return ok;
}

SUBOOL
suscan_analyzer_inspector_estimator_cmd_async(
    suscan_analyzer_t *analyzer,
    SUHANDLE handle,
    uint32_t estimator_id,
    SUBOOL enabled,
    uint32_t req_id)
{
  struct suscan_analyzer_inspector_msg *req = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      req = suscan_analyzer_inspector_msg_new(
          SUSCAN_ANALYZER_INSPECTOR_MSGKIND_ESTIMATOR,
          req_id),
      goto done);

  req->handle = handle;
  req->estimator_id = estimator_id;
  req->enabled = enabled;

  if (!suscan_analyzer_write(
      analyzer,
      SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR,
      req)) {
    SU_ERROR("Failed to send estimator_cmd command\n");
    goto done;
  }

  req = NULL;

  ok = SU_TRUE;

done:
  if (req != NULL)
    suscan_analyzer_inspector_msg_destroy(req);

  return ok;
}

SUBOOL
suscan_analyzer_inspector_set_spectrum_async(
    suscan_analyzer_t *analyzer,
    SUHANDLE handle,
    uint32_t spectsrc_id,
    uint32_t req_id)
{
  struct suscan_analyzer_inspector_msg *req = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      req = suscan_analyzer_inspector_msg_new(
          SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SPECTRUM,
          req_id),
      goto done);

  req->handle = handle;
  req->spectsrc_id = spectsrc_id;

  if (!suscan_analyzer_write(
      analyzer,
      SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR,
      req)) {
    SU_ERROR("Failed to send set_spectrum command\n");
    goto done;
  }

  req = NULL;

  ok = SU_TRUE;

done:
  if (req != NULL)
    suscan_analyzer_inspector_msg_destroy(req);

  return ok;
}

SUBOOL
suscan_analyzer_reset_equalizer_async(
    suscan_analyzer_t *analyzer,
    SUHANDLE handle,
    uint32_t req_id)
{
  struct suscan_analyzer_inspector_msg *req = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      req = suscan_analyzer_inspector_msg_new(
          SUSCAN_ANALYZER_INSPECTOR_MSGKIND_RESET_EQUALIZER,
          req_id),
      goto done);

  req->handle = handle;

  if (!suscan_analyzer_write(
      analyzer,
      SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR,
      req)) {
    SU_ERROR("Failed to send reset_equalizer command\n");
    goto done;
  }

  req = NULL;

  ok = SU_TRUE;

done:
  if (req != NULL)
    suscan_analyzer_inspector_msg_destroy(req);

  return ok;
}

SUBOOL
suscan_analyzer_set_inspector_watermark_async(
    suscan_analyzer_t *analyzer,
    SUHANDLE handle,
    SUSCOUNT watermark,
    uint32_t req_id)
{
  struct suscan_analyzer_inspector_msg *req = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      req = suscan_analyzer_inspector_msg_new(
          SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SET_WATERMARK,
          req_id),
      goto done);

  req->handle = handle;
  req->watermark = watermark;

  if (!suscan_analyzer_write(
      analyzer,
      SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR,
      req)) {
    SU_ERROR("Failed to send set_watermark command\n");
    goto done;
  }

  req = NULL;

  ok = SU_TRUE;

done:
  if (req != NULL)
    suscan_analyzer_inspector_msg_destroy(req);

  return ok;
}


