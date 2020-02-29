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

#define SU_LOG_DOMAIN "suscan-inspector-server"

#include <sigutils/sigutils.h>

#include "inspector/inspector.h"
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
  struct suscan_analyzer_inspector_msg *msg = NULL;
  struct timespec now, sub;
  suscan_spectsrc_t *src = NULL;
  unsigned int i;
  SUFLOAT N0;
  SUSDIFF fed;
  SUFLOAT seconds;

  if (insp->spectsrc_index > 0) {
    src = insp->spectsrc_list[insp->spectsrc_index - 1];
    while (samp_count > 0) {
      fed = suscan_spectsrc_feed(src, samp_buf, samp_count);
      if (fed < samp_count) {
        clock_gettime(CLOCK_MONOTONIC_RAW, &now);
        timespecsub(&now, &insp->last_spectrum, &sub);
        seconds = sub.tv_sec + 1e-9 * sub.tv_nsec;
        if (seconds >= insp->interval_spectrum) {
          insp->last_spectrum = now;
          SU_TRYCATCH(
              msg = suscan_analyzer_inspector_msg_new(
                  SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SPECTRUM,
                  rand()),
              goto fail);

          msg->inspector_id = insp->inspector_id;
          msg->spectsrc_id = insp->spectsrc_index;
          msg->samp_rate = insp->samp_info.equiv_fs;
          msg->spectrum_size = SUSCAN_INSPECTOR_SPECTRUM_BUF_SIZE;

          SU_TRYCATCH(
              msg->spectrum_data = malloc(msg->spectrum_size * sizeof(SUFLOAT)),
              goto fail);

          SU_TRYCATCH(
              suscan_spectsrc_calculate(src, msg->spectrum_data),
              goto fail);

          /* Use signal floor as noise level */
          N0 = msg->spectrum_data[0];
          for (i = 1; i < msg->spectrum_size; ++i)
            if (N0 > msg->spectrum_data[i])
              N0 = msg->spectrum_data[i];

          msg->N0 = N0;

          SU_TRYCATCH(
              suscan_mq_write(
                  mq_out,
                  SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR,
                  msg),
              goto fail);

          msg = NULL; /* We don't own this anymore */
        } else {
          SU_TRYCATCH(suscan_spectsrc_drop(src), goto fail);
        }
      }

      samp_buf   += fed;
      samp_count -= fed;
    }
  }

  return SU_TRUE;

fail:
  if (msg != NULL)
    suscan_analyzer_inspector_msg_destroy(msg);

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
  struct timespec now, sub;
  unsigned int i;
  SUFLOAT value;
  SUFLOAT seconds;

  /* Check esimator state and update clients */
  if (insp->interval_estimator > 0) {
    clock_gettime(CLOCK_MONOTONIC_RAW, &now);
    timespecsub(&now, &insp->last_estimator, &sub);
    seconds = sub.tv_sec + 1e-9 * sub.tv_nsec;
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
suscan_analyzer_on_channel_data(
    const struct sigutils_specttuner_channel *channel,
    void *private,
    const SUCOMPLEX *data,
    SUSCOUNT size)
{
  struct suscan_inspector_task_info *task_info =
      (struct suscan_inspector_task_info *) private;

  /* Channel is not bound yet. No processing is performed */
  if (task_info == NULL)
    return SU_TRUE;

  /*
   * It is safe to close channels here: we are already protected
   * by the sched mutex.
   */
  if (task_info->inspector->state != SUSCAN_ASYNC_STATE_RUNNING) {
    SU_INFO(
        "Channel not in RUNNING state, setting to HALTED and removing inspector from scheduler\n");

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

  return suscan_inspsched_queue_task(task_info->sched, task_info);
}

suscan_inspector_t *
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
  struct suscan_inspector_overridable_request *req = NULL;

  if (handle < 0 || handle >= analyzer->inspector_count)
    return SU_FALSE;

  if (analyzer->inspector_list[handle] == NULL)
    return SU_FALSE;

  SU_TRYCATCH(suscan_analyzer_lock_inspector_list(analyzer), return SU_FALSE);
  if ((req = suscan_inspector_get_userdata(
      analyzer->inspector_list[handle])) != NULL)
    req->dead = SU_TRUE;

  analyzer->inspector_list[handle] = NULL;

  suscan_analyzer_unlock_inspector_list(analyzer);

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_analyzer_open_inspector(
    suscan_analyzer_t *analyzer,
    const char *class,
    const struct sigutils_channel *channel,
    struct suscan_analyzer_inspector_msg *msg)
{
  SUHANDLE hnd = -1;
  unsigned int fs = suscan_analyzer_get_samp_rate(analyzer);
  su_specttuner_channel_t *schan = NULL;
  suscan_inspector_t *new = NULL;
  unsigned int i;

  /* Open a channel to feed this inspector */
  SU_TRYCATCH(
      schan = suscan_analyzer_open_channel_ex(
          analyzer,
          channel,
          msg->precise,
          suscan_analyzer_on_channel_data,
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
          suscan_analyzer_get_samp_rate(analyzer),
          schan),
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

  suscan_analyzer_lock_inspector_list(analyzer);

  if ((hnd = PTR_LIST_APPEND_CHECK(analyzer->inspector, new)) == -1)
    goto fail;

  suscan_analyzer_unlock_inspector_list(analyzer);

  msg->handle = hnd;

  /*
   * Data may arrive to the channel in this point. But since it is not
   * bound yet, it will be discarded.
   */
  SU_TRYCATCH(
      suscan_analyzer_bind_inspector_to_channel(analyzer, schan, new),
      goto fail);

  /*
   * At this point: inspector's state is RUNNING, and it is being handled
   * concurrently by the inspector scheduler.
   */
  return SU_TRUE;

fail:
  if (schan != NULL)
    (void) suscan_analyzer_close_channel(analyzer, schan);

  if (hnd != -1)
    (void) suscan_analyzer_dispose_inspector_handle(analyzer, hnd);

  return SU_FALSE;
}

SUPRIVATE SUBOOL
suscan_analyzer_set_inspector_freq(
    suscan_analyzer_t *analyzer,
    suscan_inspector_t *insp,
    SUFREQ freq)
{
  SUBOOL mutex_acquired = SU_FALSE;
  SUBOOL ok = SU_FALSE;
  SUFLOAT f0;

  f0 = SU_NORM2ANG_FREQ(
      SU_ABS2NORM_FREQ(suscan_analyzer_get_samp_rate(analyzer), freq));

  if (f0 < 0)
    f0 += 2 * PI;

  SU_TRYCATCH(suscan_analyzer_lock_loop(analyzer), goto done);
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
    suscan_analyzer_unlock_loop(analyzer);

  return ok;
}

SUPRIVATE SUBOOL
suscan_analyzer_set_inspector_bandwidth(
    suscan_analyzer_t *analyzer,
    suscan_inspector_t *insp,
    SUFREQ bandwidth)
{
  SUBOOL mutex_acquired = SU_FALSE;
  SUBOOL ok = SU_FALSE;
  SUFLOAT relbw;

  relbw = SU_NORM2ANG_FREQ(
      SU_ABS2NORM_FREQ(suscan_analyzer_get_samp_rate(analyzer), bandwidth));

  if (relbw < 0)
    relbw += 2 * PI;

  SU_TRYCATCH(suscan_analyzer_lock_loop(analyzer), goto done);
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
    suscan_analyzer_unlock_loop(analyzer);

  return ok;
}


/*
 * We have ownership on msg, this messages are urgent: they are placed
 * in the beginning of the queue
 */

/*
 * TODO: !!!!!!!!! Protect access to inspector object !!!!!!!!!!!!!!!
 */

SUBOOL
suscan_analyzer_parse_inspector_msg(
    suscan_analyzer_t *analyzer,
    struct suscan_analyzer_inspector_msg *msg)
{
  suscan_inspector_t *insp = NULL;
  unsigned int i;
  SUHANDLE handle = -1;
  SUFLOAT f0, new_bw;
  SUBOOL ok = SU_FALSE;
  SUBOOL mutex_acquired = SU_FALSE;
  SUBOOL update_baud;

  switch (msg->kind) {
    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_OPEN:
      SU_TRYCATCH(
          suscan_analyzer_open_inspector(
              analyzer,
              msg->class_name,
              &msg->channel,
              msg),
          goto done);
      msg->channel.ft = suscan_source_get_freq(analyzer->source);
      break;

    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SET_ID:
      if ((insp = suscan_analyzer_get_inspector(
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
      if ((insp = suscan_analyzer_get_inspector(
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
      if ((insp = suscan_analyzer_get_inspector(
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
      if ((insp = suscan_analyzer_get_inspector(
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
      if ((insp = suscan_analyzer_get_inspector(
          analyzer,
          msg->handle)) == NULL) {
        /* No such handle */
        msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_HANDLE;
      } else {
        /* Configuration stored as a config request */
        SU_TRYCATCH(suscan_inspector_set_config(insp, msg->config), goto done);
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

    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SET_WATERMARK:
      if ((insp = suscan_analyzer_get_inspector(
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
      if ((insp = suscan_analyzer_get_inspector(
          analyzer,
          msg->handle)) == NULL) {
        /* No such handle */
        msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_HANDLE;
      } else {
        SU_TRYCATCH(
            suscan_analyzer_set_inspector_freq(
                analyzer,
                insp,
                msg->channel.fc - msg->channel.ft),
            msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_INVALID_ARGUMENT);
      }

      break;

    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SET_BANDWIDTH:
      if ((insp = suscan_analyzer_get_inspector(
          analyzer,
          msg->handle)) == NULL) {
        /* No such handle */
        msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_HANDLE;
      } else if (msg->channel.bw >= insp->samp_info.equiv_fs){
        msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_INVALID_ARGUMENT;
      } else {
        SU_TRYCATCH(
            suscan_analyzer_set_inspector_bandwidth(
                analyzer,
                insp,
                msg->channel.bw),
            msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_INVALID_ARGUMENT);
      }

      break;

    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_CLOSE:
      if ((insp = suscan_analyzer_get_inspector(
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
    msg->inspector_id = insp->inspector_id;

  if (!suscan_mq_write(
      analyzer->mq_out,
      SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR,
      msg))
    goto done;

  msg = NULL;

  ok = SU_TRUE;

done:
  if (mutex_acquired)
    suscan_analyzer_unlock_loop(analyzer);

  return ok;
}
