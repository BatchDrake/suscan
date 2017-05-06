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

#include <sigutils/sigutils.h>
#include <sigutils/detect.h>

#include "mq.h"
#include "msg.h"

void
suscan_inspector_destroy(suscan_inspector_t *brinsp)
{
  if (brinsp->fac_baud_det != NULL)
    su_channel_detector_destroy(brinsp->fac_baud_det);

  if (brinsp->nln_baud_det != NULL)
    su_channel_detector_destroy(brinsp->nln_baud_det);

  free(brinsp);
}

suscan_inspector_t *
suscan_inspector_new(
    const suscan_analyzer_t *analyzer,
    const struct sigutils_channel *channel)
{
  suscan_inspector_t *new;
  struct sigutils_channel_detector_params params =
      sigutils_channel_detector_params_INITIALIZER;

  if ((new = calloc(1, sizeof (suscan_inspector_t))) == NULL)
    goto fail;

  new->state = SUSCAN_ASYNC_STATE_CREATED;

  /* Common channel parameters */
  su_channel_params_adjust_to_channel(&params, channel);

  params.samp_rate = analyzer->source.detector->params.samp_rate;
  params.window_size = SUSCAN_SOURCE_DEFAULT_BUFSIZ;
  params.alpha = 1e-4;

  /* Create generic autocorrelation-based detector */
  params.mode = SU_CHANNEL_DETECTOR_MODE_AUTOCORRELATION;
  if ((new->fac_baud_det = su_channel_detector_new(&params)) == NULL)
    goto fail;

  /* Create non-linear baud rate detector */
  params.mode = SU_CHANNEL_DETECTOR_MODE_NONLINEAR_DIFF;
  if ((new->nln_baud_det = su_channel_detector_new(&params)) == NULL)
    goto fail;


  return new;

fail:
  if (new != NULL)
    suscan_inspector_destroy(new);

  return NULL;
}

/*
 * TODO: Store *one port* only per worker. This port is read once all
 * consumers have finished with their buffer.
 */
SUPRIVATE SUBOOL
suscan_inspector_wk_cb(
    struct suscan_mq *mq_out,
    void *wk_private,
    void *cb_private)
{
  suscan_consumer_t *consumer = (suscan_consumer_t *) wk_private;
  suscan_inspector_t *brinsp =
      (suscan_inspector_t *) cb_private;
  unsigned int i;
  SUSCOUNT got;
  SUCOMPLEX *samp;
  SUBOOL restart = SU_FALSE;

  if (brinsp->task_state.consumer == NULL)
    suscan_consumer_task_state_init(&brinsp->task_state, consumer);

  if (brinsp->state == SUSCAN_ASYNC_STATE_HALTING)
    goto done;

  if (!suscan_consumer_task_state_assert_samples(
      &brinsp->task_state,
      &samp,
      &got))
    goto done;

  if (got > 0) {
    /* Got samples, forward them to baud detectors */
    if (su_channel_detector_feed_bulk(brinsp->fac_baud_det, samp, got) < got)
        goto done;

    if (su_channel_detector_feed_bulk(brinsp->nln_baud_det, samp, got) < got)
      goto done;
  }

  suscan_consumer_task_state_advance(&brinsp->task_state, got);

  restart = SU_TRUE;

done:
  if (!restart) {
    brinsp->state = SUSCAN_ASYNC_STATE_HALTED;
    suscan_consumer_remove_task(brinsp->task_state.consumer);
  }

  return restart;
}

SUINLINE suscan_inspector_t *
suscan_analyzer_get_inspector(
    const suscan_analyzer_t *analyzer,
    SUHANDLE handle)
{
  suscan_inspector_t *brinsp;

  if (handle < 0 || handle >= analyzer->inspector_count)
    return NULL;

  brinsp = analyzer->inspector_list[handle];

  if (brinsp != NULL && brinsp->state != SUSCAN_ASYNC_STATE_RUNNING)
    return NULL;

  return brinsp;
}

SUPRIVATE SUBOOL
suscan_analyzer_dispose_inspector_handle(
    suscan_analyzer_t *analyzer,
    SUHANDLE handle)
{
  if (handle < 0 || handle >= analyzer->inspector_count)
    return SU_FALSE;

  if (analyzer->inspector_list[handle] == NULL)
    return SU_FALSE;

  analyzer->inspector_list[handle] = NULL;

  return SU_TRUE;
}

SUPRIVATE SUHANDLE
suscan_analyzer_register_inspector(
    suscan_analyzer_t *analyzer,
    suscan_inspector_t *brinsp)
{
  SUHANDLE hnd;

  if (brinsp->state != SUSCAN_ASYNC_STATE_CREATED)
    return SU_FALSE;

  /* Plugged. Append handle to list */
  /* TODO: Find inspectors in HALTED state, and free them */
  if ((hnd = PTR_LIST_APPEND_CHECK(analyzer->inspector, brinsp)) == -1)
    return -1;

  /* Mark it as running and push to worker */
  brinsp->state = SUSCAN_ASYNC_STATE_RUNNING;

  if (!suscan_analyzer_push_task(
      analyzer,
      suscan_inspector_wk_cb,
      brinsp)) {
    suscan_analyzer_dispose_inspector_handle(analyzer, hnd);
    return -1;
  }

  return hnd;
}

/* We have ownership on msg */
SUBOOL
suscan_analyzer_parse_baud(
    suscan_analyzer_t *analyzer,
    struct suscan_analyzer_inspector_msg *msg)
{
  suscan_inspector_t *new = NULL;
  suscan_inspector_t *brinsp;
  SUHANDLE handle = -1;
  SUBOOL ok = SU_FALSE;

  switch (msg->kind) {
    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_OPEN:
      if ((new = suscan_inspector_new(
          analyzer,
          &msg->channel)) == NULL)
        goto done;

      handle = suscan_analyzer_register_inspector(analyzer, new);
      if (handle == -1)
        goto done;
      new = NULL;

      msg->handle = handle;

      break;

    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_GET_INFO:
      if ((brinsp = suscan_analyzer_get_inspector(
          analyzer,
          msg->handle)) == NULL) {
        /* No such handle */
        msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_HANDLE;
      } else {
        /* Retrieve current esimate for message kind */
        msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_INFO;
        msg->baudrate.fac = brinsp->fac_baud_det->baud;
        msg->baudrate.nln = brinsp->nln_baud_det->baud;
      }
      break;

    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_CLOSE:
      if ((brinsp = suscan_analyzer_get_inspector(
          analyzer,
          msg->handle)) == NULL) {
        msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_HANDLE;
      } else {
        if (brinsp->state == SUSCAN_ASYNC_STATE_HALTED) {
          /*
           * Inspector has been halted. It's safe to dispose the handle
           * and free the object.
           */
          (void) suscan_analyzer_dispose_inspector_handle(
              analyzer,
              msg->handle);
          suscan_inspector_destroy(brinsp);
        } else {
          /*
           * Inspector is still running. Mark it as halting, so it will not
           * come back to the worker queue.
           */
          brinsp->state = SUSCAN_ASYNC_STATE_HALTING;
        }
      }

      break;

    default:
      msg->status = msg->kind;
      msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_KIND;
  }

  if (!suscan_mq_write(
      analyzer->mq_out,
      SUSCAN_ANALYZER_MESSAGE_TYPE_BR_INSPECTOR,
      msg))
    goto done;

  ok = SU_TRUE;

done:
  if (new != NULL)
    suscan_inspector_destroy(new);

  return ok;
}

/************************* Baudrate inspector API ****************************/
SUHANDLE
suscan_inspector_open(
    suscan_analyzer_t *analyzer,
    const struct sigutils_channel *channel)
{
  struct suscan_analyzer_inspector_msg *req = NULL;
  struct suscan_analyzer_inspector_msg *resp = NULL;
  uint32_t type;
  uint32_t req_id = rand();

  SUHANDLE handle = -1;

  if ((req = suscan_analyzer_inspector_msg_new(
      SUSCAN_ANALYZER_INSPECTOR_MSGKIND_OPEN,
      req_id)) == NULL) {
    SU_ERROR("Failed to craft open message\n");
    goto done;
  }

  req->channel = *channel;

  if (!suscan_analyzer_write(
      analyzer,
      SUSCAN_ANALYZER_MESSAGE_TYPE_BR_INSPECTOR,
      req)) {
    SU_ERROR("Failed to send open command\n");
    goto done;
  }

  req = NULL;

  for (;;) {
    resp = suscan_analyzer_read(analyzer, &type);
    if (type == SUSCAN_ANALYZER_MESSAGE_TYPE_EOS) {
      SU_ERROR("Unexpected end of stream while opening baud inspector\n");
      goto done;
    }

    if (type == SUSCAN_ANALYZER_MESSAGE_TYPE_BR_INSPECTOR)
      break;

    /* Not the message we were looking for */
    suscan_analyzer_dispose_message(type, (void *) resp);
  };

  if (resp->req_id != req_id) {
    SU_ERROR("Unmatched response received\n");
    goto done;
  }

  if (resp->kind != SUSCAN_ANALYZER_INSPECTOR_MSGKIND_OPEN) {
    SU_ERROR("Unexpected message kind\n");
    goto done;
  }


  handle = resp->handle;

done:
  if (req != NULL)
    suscan_analyzer_inspector_msg_destroy(req);

  if (resp != NULL)
    suscan_analyzer_inspector_msg_destroy(resp);

  return handle;
}

SUBOOL
suscan_inspector_close(
    suscan_analyzer_t *analyzer,
    SUHANDLE handle)
{
  struct suscan_analyzer_inspector_msg *req = NULL;
  struct suscan_analyzer_inspector_msg *resp = NULL;
  uint32_t type;
  uint32_t req_id = rand();
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
      SUSCAN_ANALYZER_MESSAGE_TYPE_BR_INSPECTOR,
      req)) {
    SU_ERROR("Failed to send close command\n");
    goto done;
  }

  req = NULL;

  for (;;) {
    resp = suscan_analyzer_read(analyzer, &type);
    if (type == SUSCAN_ANALYZER_MESSAGE_TYPE_EOS) {
      SU_ERROR("Unexpected end of stream while closing baud inspector\n");
      goto done;
    }

    if (type == SUSCAN_ANALYZER_MESSAGE_TYPE_BR_INSPECTOR)
      break;

    /* Not the message we were looking for */
    suscan_analyzer_dispose_message(type, (void *) resp);
  }

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
  if (req != NULL)
    suscan_analyzer_inspector_msg_destroy(req);

  if (resp != NULL)
    suscan_analyzer_inspector_msg_destroy(resp);

  return ok;
}

SUBOOL
suscan_inspector_get_info(
    suscan_analyzer_t *analyzer,
    SUHANDLE handle,
    struct suscan_inspector_result *result)
{
  struct suscan_analyzer_inspector_msg *req = NULL;
  struct suscan_analyzer_inspector_msg *resp = NULL;
  uint32_t type;
  uint32_t req_id = rand();
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
      SUSCAN_ANALYZER_MESSAGE_TYPE_BR_INSPECTOR,
      req)) {
    SU_ERROR("Failed to send get_info command\n");
    goto done;
  }

  req = NULL;

  for (;;) {
    resp = suscan_analyzer_read(analyzer, &type);
    if (type == SUSCAN_ANALYZER_MESSAGE_TYPE_EOS) {
      SU_ERROR("Unexpected end of stream while asking for info\n");
      goto done;
    }

    if (type == SUSCAN_ANALYZER_MESSAGE_TYPE_BR_INSPECTOR)
      break;

    /* Not the message we were looking for */
    suscan_analyzer_dispose_message(type, (void *) resp);
  };

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

  *result = resp->baudrate;

  ok = SU_TRUE;

done:
  if (req != NULL)
    suscan_analyzer_inspector_msg_destroy(req);

  if (resp != NULL)
    suscan_analyzer_inspector_msg_destroy(resp);

  return ok;
}
