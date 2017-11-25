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
 * This is the server application: the worker that processes messages and
 * forwards samples to the inspector
 */

#define SU_LOG_DOMAIN "suscan-inspector-server"

#include <sigutils/sigutils.h>

#include "inspector.h"
#include "mq.h"
#include "msg.h"

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
  suscan_inspector_t *insp = (suscan_inspector_t *) cb_private;
  unsigned int i;
  int fed;
  SUSCOUNT samp_count;
  const SUCOMPLEX *samp_buf;
  struct suscan_analyzer_sample_batch_msg *batch_msg = NULL;
  SUBOOL restart = SU_FALSE;

  samp_buf   = suscan_consumer_get_buffer(consumer);
  samp_count = suscan_consumer_get_buffer_size(consumer);

  insp->per_cnt_psd += samp_count;

  while (samp_count > 0) {
    /* Ensure the current inspector parameters are up-to-date */
    suscan_inspector_assert_params(insp);

    SU_TRYCATCH(
        (fed = suscan_inspector_feed_bulk(insp, samp_buf, samp_count)) >= 0,
        goto done);

    if (insp->sym_new_sample) {
      /* Sampler was triggered */
      if (batch_msg == NULL)
        SU_TRYCATCH(
            batch_msg = suscan_analyzer_sample_batch_msg_new(
                insp->params.inspector_id),
            goto done);

      SU_TRYCATCH(
          suscan_analyzer_sample_batch_msg_append_sample(
              batch_msg,
              insp->sym_sampler_output),
          goto done);

    }

    samp_buf   += fed;
    samp_count -= fed;
  }

  /* Check spectrum update */
  if (insp->interval_psd > 0 && insp->pending)
    if (insp->per_cnt_psd
        >= insp->interval_psd
        * su_channel_detector_get_fs(insp->fac_baud_det)) {
      insp->per_cnt_psd = 0;

      switch (insp->params.psd_source) {
        case SUSCAN_INSPECTOR_PSD_SOURCE_FAC:
          if (!suscan_inspector_send_psd(insp, consumer, insp->fac_baud_det))
            goto done;
          break;

        case SUSCAN_INSPECTOR_PSD_SOURCE_NLN:
          if (!suscan_inspector_send_psd(insp, consumer, insp->nln_baud_det))
            goto done;
          break;

        default:
          /* Prevent warnings */
          break;
      }

      insp->pending = SU_FALSE;
    }

  /* Got samples, send message batch */
  if (batch_msg != NULL) {
    SU_TRYCATCH(
        suscan_mq_write(
            consumer->analyzer->mq_out,
            SUSCAN_ANALYZER_MESSAGE_TYPE_SAMPLES,
            batch_msg),
        goto done);
    batch_msg = NULL;
  }

  restart = insp->state == SUSCAN_ASYNC_STATE_RUNNING;

done:
  if (!restart) {
    insp->state = SUSCAN_ASYNC_STATE_HALTED;
    suscan_consumer_remove_task(consumer);
  }

  if (batch_msg != NULL)
    suscan_analyzer_sample_batch_msg_destroy(batch_msg);

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

/*
 * We have ownership on msg, this messages are urgent: they are placed
 * in the beginning of the queue
 */

/*
 * TODO: Protect access to inspector object!
 */

SUBOOL
suscan_analyzer_parse_inspector_msg(
    suscan_analyzer_t *analyzer,
    struct suscan_analyzer_inspector_msg *msg)
{
  suscan_inspector_t *new = NULL;
  suscan_inspector_t *insp = NULL;
  SUHANDLE handle = -1;
  SUBOOL ok = SU_FALSE;
  SUBOOL update_baud;

  switch (msg->kind) {
    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_OPEN:
      if ((new = suscan_inspector_new(
          su_channel_detector_get_fs(analyzer->source.detector),
          &msg->channel)) == NULL)
        goto done;

      handle = suscan_analyzer_register_inspector(analyzer, new);
      if (handle == -1)
        goto done;
      new = NULL;

      msg->handle = handle;
      break;

    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_GET_INFO:
      if ((insp = suscan_analyzer_get_inspector(
          analyzer,
          msg->handle)) == NULL) {
        /* No such handle */
        msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_HANDLE;
      } else {
        /* Retrieve current esimate for message kind */
        msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_INFO;
        msg->baud.fac = insp->fac_baud_det->baud;
        msg->baud.nln = insp->nln_baud_det->baud;
      }
      break;

    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_GET_INSP_PARAMS:
      if ((insp = suscan_analyzer_get_inspector(
          analyzer,
          msg->handle)) == NULL) {
        /* No such handle */
        msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_HANDLE;
      } else {
        /* Retrieve current inspector params */
        msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SET_INSP_PARAMS;
        msg->insp_params = insp->params;
      }
      break;

    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SET_INSP_PARAMS:
      if ((insp = suscan_analyzer_get_inspector(
          analyzer,
          msg->handle)) == NULL) {
        /* No such handle */
        msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_HANDLE;
      } else {
        /* Store the parameter update request */
        suscan_inspector_request_params(insp, &msg->insp_params);
      }
      break;

    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_RESET_EQUALIZER:
      if ((insp = suscan_analyzer_get_inspector(
          analyzer,
          msg->handle)) == NULL) {
        /* No such handle */
        msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_HANDLE;
      } else {
        /* Reset equalizer */
        suscan_inspector_reset_equalizer(insp);
      }
      break;

    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_CLOSE:
      if ((insp = suscan_analyzer_get_inspector(
          analyzer,
          msg->handle)) == NULL) {
        msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_HANDLE;
      } else {
        msg->inspector_id = insp->params.inspector_id;

        if (insp->state == SUSCAN_ASYNC_STATE_HALTED) {
          /*
           * Inspector has been halted. It's safe to dispose the handle
           * and free the object.
           */
          (void) suscan_analyzer_dispose_inspector_handle(
              analyzer,
              msg->handle);
          suscan_inspector_destroy(insp);
        } else {
          /*
           * Inspector is still running. Mark it as halting, so it will not
           * come back to the worker queue.
           */
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
    msg->inspector_id = insp->params.inspector_id;

  if (!suscan_mq_write(
      analyzer->mq_out,
      SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR,
      msg))
    goto done;

  ok = SU_TRUE;

done:
  if (new != NULL)
    suscan_inspector_destroy(new);

  return ok;
}
