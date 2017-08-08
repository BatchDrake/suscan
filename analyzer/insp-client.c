/*

  Copyright (C) 2017 Gonzalo José Carracedo Carballal

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation, either version 3 of the
  License, or (at your option) any later version.

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
 * inspector methods in the analyzer thread
 */
#define SU_LOG_DOMAIN "suscan-inspector-client"

#include <sigutils/sigutils.h>

#include "inspector.h"
#include "mq.h"
#include "msg.h"

SUBOOL
suscan_inspector_open_async(
    suscan_analyzer_t *analyzer,
    const struct sigutils_channel *channel,
    uint32_t req_id)
{
  struct suscan_analyzer_inspector_msg *req = NULL;
  uint32_t type;
  SUBOOL ok = SU_FALSE;

  if ((req = suscan_analyzer_inspector_msg_new(
      SUSCAN_ANALYZER_INSPECTOR_MSGKIND_OPEN,
      req_id)) == NULL) {
    SU_ERROR("Failed to craft open message\n");
    goto done;
  }

  req->channel = *channel;

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

SUHANDLE
suscan_inspector_open(
    suscan_analyzer_t *analyzer,
    const struct sigutils_channel *channel)
{
  struct suscan_analyzer_inspector_msg *resp = NULL;
  uint32_t req_id = rand();
  SUHANDLE handle = -1;

  SU_TRYCATCH(
      suscan_inspector_open_async(analyzer, channel, req_id),
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
suscan_inspector_close_async(
    suscan_analyzer_t *analyzer,
    SUHANDLE handle,
    uint32_t req_id)
{
  struct suscan_analyzer_inspector_msg *req = NULL;
  uint32_t type;
  SUBOOL ok = SU_FALSE;

  if ((req = suscan_analyzer_inspector_msg_new(
      SUSCAN_ANALYZER_INSPECTOR_MSGKIND_CLOSE,
      req_id)) == NULL) {
    SU_ERROR("Failed to craft close message\n");
    goto done;
  }
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
suscan_inspector_close(
    suscan_analyzer_t *analyzer,
    SUHANDLE handle)
{

  struct suscan_analyzer_inspector_msg *resp = NULL;
  uint32_t req_id = rand();
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      suscan_inspector_close_async(analyzer, handle, req_id),
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
suscan_inspector_get_info_async(
    suscan_analyzer_t *analyzer,
    SUHANDLE handle,
    uint32_t req_id)
{
  struct suscan_analyzer_inspector_msg *req = NULL;
  uint32_t type;
  SUBOOL ok = SU_FALSE;

  if ((req = suscan_analyzer_inspector_msg_new(
      SUSCAN_ANALYZER_INSPECTOR_MSGKIND_GET_INFO,
      req_id)) == NULL) {
    SU_ERROR("Failed to craft get_info message\n");
    goto done;
  }

  req->handle = handle;

  if (!suscan_analyzer_write(
      analyzer,
      SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR,
      req)) {
    SU_ERROR("Failed to send get_info command\n");
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
suscan_inspector_get_info(
    suscan_analyzer_t *analyzer,
    SUHANDLE handle,
    struct suscan_baud_det_result *result)
{
  struct suscan_analyzer_inspector_msg *resp = NULL;
  uint32_t req_id = rand();
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      suscan_inspector_get_info_async(analyzer, handle, req_id),
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
  } else if (resp->kind != SUSCAN_ANALYZER_INSPECTOR_MSGKIND_INFO) {
    SU_ERROR("Unexpected message kind %d\n", resp->kind);
    goto done;
  }

  *result = resp->baud;

  ok = SU_TRUE;

done:
  if (resp != NULL)
    suscan_analyzer_inspector_msg_destroy(resp);

  return ok;
}

SUBOOL
suscan_inspector_set_inspector_params_async(
    suscan_analyzer_t *analyzer,
    SUHANDLE handle,
    const struct suscan_inspector_params *params,
    uint32_t req_id)
{
  struct suscan_analyzer_inspector_msg *req = NULL;
  uint32_t type;
  SUBOOL ok = SU_FALSE;

  if ((req = suscan_analyzer_inspector_msg_new(
      SUSCAN_ANALYZER_INSPECTOR_MSGKIND_PARAMS,
      req_id)) == NULL) {
    SU_ERROR("Failed to craft get_info message\n");
    goto done;
  }

  req->handle = handle;
  req->params = *params;

  if (!suscan_analyzer_write(
      analyzer,
      SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR,
      req)) {
    SU_ERROR("Failed to send set_params command\n");
    goto done;
  }

  req = NULL;

  ok = SU_TRUE;

done:
  if (req != NULL)
    suscan_analyzer_inspector_msg_destroy(req);

  return ok;
}
