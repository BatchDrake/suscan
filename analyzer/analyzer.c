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
#include <time.h>

#define SU_LOG_DOMAIN "analyzer"

#include <sigutils/sigutils.h>
#include <sigutils/detect.h>

#include "analyzer.h"

#include "mq.h"
#include "msg.h"

/*********************** Performance measurement *****************************/
SUPRIVATE void
suscan_analyzer_read_start(suscan_analyzer_t *analyzer)
{
  clock_gettime(CLOCK_MONOTONIC_RAW, &analyzer->read_start);
}

SUPRIVATE void
suscan_analyzer_process_start(suscan_analyzer_t *analyzer)
{
  clock_gettime(CLOCK_MONOTONIC_RAW, &analyzer->process_start);
}

SUPRIVATE void
suscan_analyzer_process_end(suscan_analyzer_t *analyzer)
{
  struct timespec sub;
  uint64_t total, cpu;

  clock_gettime(CLOCK_MONOTONIC_RAW, &analyzer->process_end);

  if (analyzer->read_start.tv_sec > 0) {
    timespecsub(&analyzer->process_end, &analyzer->read_start, &sub);
    total = sub.tv_sec * 1000000000 + sub.tv_nsec;

    timespecsub(&analyzer->process_end, &analyzer->process_start, &sub);
    cpu = sub.tv_sec * 1000000000 + sub.tv_nsec;

    /* Update CPU usage */
    if (total == 0)
      analyzer->cpu_usage +=
          SUSCAN_ANALYZER_CPU_USAGE_UPDATE_ALPHA
          * (1. - analyzer->cpu_usage);
    else
      analyzer->cpu_usage +=
          SUSCAN_ANALYZER_CPU_USAGE_UPDATE_ALPHA
          * ((SUFLOAT) cpu / (SUFLOAT) total - analyzer->cpu_usage);
  }
}

/************************* Baseband filter API *******************************/
SUPRIVATE struct suscan_analyzer_baseband_filter *
suscan_analyzer_baseband_filter_new(
    suscan_analyzer_baseband_filter_func_t func,
    void *privdata)
{
  struct suscan_analyzer_baseband_filter *filter;

  SU_TRYCATCH(
      filter = malloc(sizeof (struct suscan_analyzer_baseband_filter)),
      return NULL);

  filter->func = func;
  filter->privdata = privdata;

  return filter;
}

SUPRIVATE void
suscan_analyzer_baseband_filter_destroy(
    struct suscan_analyzer_baseband_filter *filter)
{
  free(filter);
}

SUBOOL
suscan_analyzer_register_baseband_filter(
    suscan_analyzer_t *analyzer,
    suscan_analyzer_baseband_filter_func_t func,
    void *privdata)
{
  struct suscan_analyzer_baseband_filter *new;

  SU_TRYCATCH(
      new = suscan_analyzer_baseband_filter_new(func, privdata),
      goto fail);

  new->func = func;
  new->privdata = privdata;

  SU_TRYCATCH(
      PTR_LIST_APPEND_CHECK(analyzer->bbfilt, new) != -1,
      goto fail);

  return SU_TRUE;

fail:
  if (new != NULL)
    suscan_analyzer_baseband_filter_destroy(new);

  return SU_FALSE;
}

SUPRIVATE SUBOOL
suscan_analyzer_feed_baseband_filters(
    suscan_analyzer_t *analyzer,
    const SUCOMPLEX *samples,
    SUSCOUNT length)
{
  unsigned int i;

  for (i = 0; i < analyzer->bbfilt_count; ++i)
    if (analyzer->bbfilt_list[i] != NULL)
      if (!analyzer->bbfilt_list[i]->func(
          analyzer->bbfilt_list[i]->privdata,
          analyzer,
          samples,
          length))
        return SU_FALSE;

  return SU_TRUE;
}

/************************ Source worker callback *****************************/
#ifdef SUSCAN_DEBUG_THROTTLE
SUBOOL   dbg_rate_set;
struct timespec dbg_rate_source_start;
SUSCOUNT dbg_rate_counter;
SUFLOAT  dbg_rate_mean;
SUSCOUNT dbg_rate_last_second;
#endif

SUPRIVATE SUBOOL
suscan_analyzer_feed_inspectors(
    suscan_analyzer_t *analyzer,
    const SUCOMPLEX *data,
    SUSCOUNT size)
{
  SUSDIFF got;
  SUBOOL ok = SU_TRUE;

  /*
   * No opened channels. We can avoid doing extra work. However, we
   * should clean the tuner in this case to keep it from having
   * samples from previous calls to feed_bulk.
   */
  if (su_specttuner_get_channel_count(analyzer->stuner) == 0)
    return SU_TRUE;

  /* This must be performed in a serialized way */
  while (size > 0) {

    /*
     * Must be protected from access by the analyzer thread: right now,
     * only the source worker can access the tuner.
     */
    suscan_analyzer_enter_sched(analyzer);
    got = su_specttuner_feed_bulk_single(analyzer->stuner, data, size);

    if (su_specttuner_new_data(analyzer->stuner)) {
      /*
       * New data has been queued to the existing inspectors. We must
       * ensure that all of them are done by issuing a barrier at the end
       * of the worker queue.
       */

      suscan_inspsched_sync(analyzer->sched);

      su_specttuner_ack_data(analyzer->stuner);
    }

    suscan_analyzer_leave_sched(analyzer);

    if (got == -1)
      ok = SU_FALSE;

    data += got;
    size -= got;
  }

  return ok;
}

/* Hacky way to perform IQ inversion without depending on the FPU */
SUPRIVATE SUBOOL
suscan_analyzer_do_iq_rev(SUCOMPLEX *buf, SUSCOUNT size)
{
  SUSCOUNT i;
  size <<= 1;
#ifdef _SU_SINGLE_PRECISION
  uint32_t *as_ints = (uint32_t *) buf;
  for (i = 1; i < size; i += 2)
    as_ints[i] ^= 0x80000000;

#else
  uint64_t *as_ints = (uint64_t *) buf;
  for (i = 1; i < size; i += 2)
    as_ints[i] ^= 0x8000000000000000ull;

#endif
}

SUBOOL
suscan_analyzer_lock_loop(suscan_analyzer_t *analyzer)
{
  return pthread_mutex_lock(&analyzer->loop_mutex) != -1;
}

void
suscan_analyzer_unlock_loop(suscan_analyzer_t *analyzer)
{
  (void) pthread_mutex_unlock(&analyzer->loop_mutex);
}


SUPRIVATE SUBOOL
suscan_source_wk_cb(
    struct suscan_mq *mq_out,
    void *wk_private,
    void *cb_private)
{
  suscan_analyzer_t *analyzer = (suscan_analyzer_t *) wk_private;
  SUSDIFF got;
  SUSCOUNT read_size;
  SUBOOL mutex_acquired = SU_FALSE;
  SUBOOL restart = SU_FALSE;
  unsigned int i;
#ifdef SUSCAN_DEBUG_THROTTLE
  struct timespec sub;
#endif

  SU_TRYCATCH(suscan_analyzer_lock_loop(analyzer), goto done);
  mutex_acquired = SU_TRUE;

  /* With non-real time sources, use throttle to control CPU usage */
  if (suscan_analyzer_is_real_time(analyzer)) {
    read_size = analyzer->read_size;
  } else {
    SU_TRYCATCH(
        pthread_mutex_lock(&analyzer->throttle_mutex) != -1,
        goto done);
    read_size = suscan_throttle_get_portion(
        &analyzer->throttle,
        analyzer->read_size);
    SU_TRYCATCH(
        pthread_mutex_unlock(&analyzer->throttle_mutex) != -1,
        goto done);
  }

  /* Ready to read */
  suscan_analyzer_read_start(analyzer);

#ifdef SUSCAN_DEBUG_THROTTLE
  if (!dbg_rate_set) {
    dbg_rate_set = SU_TRUE;
    dbg_rate_source_start = analyzer->read_start;
    dbg_rate_counter = 0;
  }
#endif

  if ((got = suscan_source_read(
      analyzer->source,
      analyzer->read_buf,
      read_size)) > 0) {
    suscan_analyzer_process_start(analyzer);
#ifdef SUSCAN_DEBUG_THROTTLE
    dbg_rate_counter += got;
#endif

    if (analyzer->iq_rev)
      suscan_analyzer_do_iq_rev(analyzer->read_buf, read_size);

    if (!suscan_analyzer_is_real_time(analyzer)) {
      SU_TRYCATCH(
          pthread_mutex_lock(&analyzer->throttle_mutex) != -1,
          goto done);
      suscan_throttle_advance(&analyzer->throttle, got);
      SU_TRYCATCH(
          pthread_mutex_unlock(&analyzer->throttle_mutex) != -1,
          goto done);
    }

    SU_TRYCATCH(
        suscan_analyzer_feed_baseband_filters(
            analyzer,
            analyzer->read_buf,
            got),
        goto done);

    /* Feed channel detector! */
    SU_TRYCATCH(
        su_channel_detector_feed_bulk(
            analyzer->detector,
            analyzer->read_buf,
            got) == got,
        goto done);

    analyzer->per_cnt_channels += got;
    analyzer->per_cnt_psd += got;

    /* Check channel update */
    if (analyzer->interval_channels > 0) {
      if (analyzer->per_cnt_channels
          >= analyzer->interval_channels * analyzer->effective_samp_rate) {
        analyzer->per_cnt_channels = 0;

        SU_TRYCATCH(
            suscan_analyzer_send_detector_channels(
                analyzer,
                analyzer->detector),
            goto done);
      }
    }

    /* Check spectrum update */
    if (analyzer->interval_psd > 0) {
      if (analyzer->per_cnt_psd
          >= analyzer->interval_psd * analyzer->effective_samp_rate) {
        analyzer->per_cnt_psd = 0;

        SU_TRYCATCH(
            suscan_analyzer_send_psd(analyzer, analyzer->detector),
            goto done);
      }
    }

    /* Feed inspectors! */
    SU_TRYCATCH(
        suscan_analyzer_feed_inspectors(analyzer, analyzer->read_buf, got),
        goto done);

  } else {
    analyzer->eos = SU_TRUE;
    analyzer->cpu_usage = 0;

    switch (got) {
      case SU_BLOCK_PORT_READ_END_OF_STREAM:
        suscan_analyzer_send_status(
            analyzer,
            SUSCAN_ANALYZER_MESSAGE_TYPE_EOS,
            got,
            "End of stream reached");
        break;

      case SU_BLOCK_PORT_READ_ERROR_NOT_INITIALIZED:
        suscan_analyzer_send_status(
            analyzer,
            SUSCAN_ANALYZER_MESSAGE_TYPE_EOS,
            got,
            "Port not initialized");
        break;

      case SU_BLOCK_PORT_READ_ERROR_ACQUIRE:
        suscan_analyzer_send_status(
            analyzer,
            SUSCAN_ANALYZER_MESSAGE_TYPE_READ_ERROR,
            got,
            "Acquire failed (source I/O error)");
        break;

      case SU_BLOCK_PORT_READ_ERROR_PORT_DESYNC:
        suscan_analyzer_send_status(
            analyzer,
            SUSCAN_ANALYZER_MESSAGE_TYPE_EOS,
            got,
            "Port desync");
        break;

      default:
        suscan_analyzer_send_status(
            analyzer,
            SUSCAN_ANALYZER_MESSAGE_TYPE_EOS,
            got,
            "Unexpected read result %d", got);
    }

    goto done;
  }

  /* Finish processing */
  suscan_analyzer_process_end(analyzer);

#ifdef SUSCAN_DEBUG_THROTTLE
    timespecsub(&analyzer->process_end, &dbg_rate_source_start, &sub);

    if (sub.tv_sec != dbg_rate_last_second) {
      dbg_rate_mean = (SUFLOAT) dbg_rate_counter / (sub.tv_sec + 1e-9 * sub.tv_nsec);
      printf("Current read rate: %lg\n", dbg_rate_mean);
      dbg_rate_last_second = sub.tv_sec;
    }
#endif

  restart = SU_TRUE;

done:
  if (mutex_acquired)
    (void) suscan_analyzer_unlock_loop(analyzer);

  return restart;
}


/************************* Main analyzer thread *******************************/
void
suscan_analyzer_req_halt(suscan_analyzer_t *analyzer)
{
  analyzer->halt_requested = SU_TRUE;

  suscan_mq_write_urgent(
      &analyzer->mq_in,
      SUSCAN_WORKER_MSG_TYPE_HALT,
      NULL);
}

SUPRIVATE void
suscan_analyzer_ack_halt(suscan_analyzer_t *analyzer)
{
  suscan_mq_write_urgent(
      analyzer->mq_out,
      SUSCAN_WORKER_MSG_TYPE_HALT,
      NULL);
}

SUPRIVATE void
suscan_wait_for_halt(suscan_analyzer_t *analyzer)
{
  uint32_t type;
  void *private;

  for (;;) {
    private = suscan_mq_read(&analyzer->mq_in, &type);
    if (type == SUSCAN_WORKER_MSG_TYPE_HALT) {
      suscan_analyzer_ack_halt(analyzer);
      break;
    }

    suscan_analyzer_dispose_message(type, private);
  }
}

SUPRIVATE SUBOOL
suscan_analyzer_override_throttle(suscan_analyzer_t *analyzer, SUSCOUNT val)
{
  SUBOOL mutex_acquired = SU_FALSE;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(!suscan_analyzer_is_real_time(analyzer), goto done);

  SU_TRYCATCH(
      pthread_mutex_lock(&analyzer->throttle_mutex) != -1,
      goto done);
  mutex_acquired = SU_TRUE;

  suscan_throttle_init(&analyzer->throttle, val);

  analyzer->effective_samp_rate = val;

  ok = SU_TRUE;

done:

  if (mutex_acquired)
    pthread_mutex_unlock(&analyzer->throttle_mutex);

  return ok;
}

SUPRIVATE SUBOOL
suscan_analyzer_reset_throttle(suscan_analyzer_t *analyzer)
{
  return suscan_analyzer_override_throttle(
      analyzer,
      suscan_analyzer_get_samp_rate(analyzer));
}

SUPRIVATE void *
suscan_analyzer_thread(void *data)
{
  suscan_analyzer_t *analyzer = (suscan_analyzer_t *) data;
  su_channel_detector_t *new_detector = NULL;
  struct sigutils_channel_detector_params new_det_params;
  const struct suscan_analyzer_params *new_params;
  const struct suscan_analyzer_throttle_msg *throttle;
  void *private = NULL;
  uint32_t type;
  SUBOOL mutex_acquired = SU_FALSE;
  SUBOOL halt_acked = SU_FALSE;

  SU_TRYCATCH(suscan_source_start_capture(analyzer->source), goto done);

  if (!suscan_worker_push(
      analyzer->source_wk,
      suscan_source_wk_cb,
      analyzer->source)) {
    suscan_analyzer_send_status(
        analyzer,
        SUSCAN_ANALYZER_MESSAGE_TYPE_SOURCE_INIT,
        SUSCAN_ANALYZER_INIT_FAILURE,
        "Failed to push async callback to worker");
    goto done;
  }

  /* Signal initialization success */
  suscan_analyzer_send_status(
      analyzer,
      SUSCAN_ANALYZER_MESSAGE_TYPE_SOURCE_INIT,
      SUSCAN_ANALYZER_INIT_SUCCESS,
      NULL);

  /* Pop all messages from queue before reading from the source */
  for (;;) {
    /* First read: blocks */
    private = suscan_mq_read(&analyzer->mq_in, &type);

    do {
      switch (type) {
        case SUSCAN_WORKER_MSG_TYPE_HALT:
          suscan_analyzer_ack_halt(analyzer);
          halt_acked = SU_TRUE;
          /* Nothing to dispose, safe to break the loop */
          goto done;

        case SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR:
          /* Baudrate inspector command. Handle separately */
          SU_TRYCATCH(
              suscan_analyzer_parse_inspector_msg(analyzer, private),
              goto done);

          /*
           * We don't dispose this message: it has been processed
           * by the baud inspector API and forwarded to the output mq
           */
          private = NULL;

          break;

        /* Forward these messages to output */
        case SUSCAN_ANALYZER_MESSAGE_TYPE_EOS:
        case SUSCAN_ANALYZER_MESSAGE_TYPE_CHANNEL:
          SU_TRYCATCH(
              suscan_mq_write(analyzer->mq_out, type, private),
              goto done);

          /* Not belonging to us anymore */
          private = NULL;

          break;

        case SUSCAN_ANALYZER_MESSAGE_TYPE_THROTTLE:
          throttle = (const struct suscan_analyzer_throttle_msg *) private;
          if (throttle->samp_rate == 0) {
            SU_TRYCATCH(
                suscan_analyzer_reset_throttle(analyzer),
                goto done);
          } else {
            SU_TRYCATCH(
                suscan_analyzer_override_throttle(analyzer, throttle->samp_rate),
                goto done);
          }
          break;

        case SUSCAN_ANALYZER_MESSAGE_TYPE_PARAMS:
          /*
           * Parameter messages affect the source worker, that must get their
           * objects updated. In order to do that, we protect their access
           * through the source's params_mutex
           */

          SU_TRYCATCH(
              pthread_mutex_lock(&analyzer->loop_mutex) != -1,
              goto done);
          mutex_acquired = SU_TRUE;

          /* vvvvvvvvvvvvvvv Source parameters update start vvvvvvvvvvvvv */
          new_params = (const struct suscan_analyzer_params *) private;

          /* Attempt to update detector parameters */
          new_det_params = analyzer->detector->params; /* Not all parameters are allowed */

          new_det_params.window_size = new_params->detector_params.window_size;
          new_det_params.window = new_params->detector_params.window;
          new_det_params.fc = new_params->detector_params.fc;
          su_channel_params_adjust(&new_det_params);

          if (!su_channel_detector_set_params(
              analyzer->detector,
              &new_det_params)) {
            /* If not possibe, re-create detector object */
            SU_TRYCATCH(
                new_detector = su_channel_detector_new(&new_det_params),
                goto done);

            su_channel_detector_destroy(analyzer->detector);
            analyzer->detector = new_detector;
          }

          analyzer->per_cnt_channels  = 0;
          analyzer->per_cnt_psd       = 0;

          analyzer->interval_channels = new_params->channel_update_int;
          analyzer->interval_psd      = new_params->psd_update_int;
          /* ^^^^^^^^^^^^^ Source parameters update end ^^^^^^^^^^^^^^^^^  */

          SU_TRYCATCH(
              pthread_mutex_unlock(&analyzer->loop_mutex) != -1,
              goto done);
          mutex_acquired = SU_FALSE;

          break;
      }

      if (private != NULL) {
        suscan_analyzer_dispose_message(type, private);
        private = NULL;
      }

      /* Next reads: until message queue is empty */
    } while (suscan_mq_poll(&analyzer->mq_in, &type, &private));
  }

done:
  if (mutex_acquired)
    (void) pthread_mutex_unlock(&analyzer->loop_mutex);

  if (private != NULL)
    suscan_analyzer_dispose_message(type, private);

  if (suscan_source_is_capturing(analyzer->source))
    suscan_source_stop_capture(analyzer->source);

  if (!halt_acked)
    suscan_wait_for_halt(analyzer);

  analyzer->running = SU_FALSE;

  pthread_exit(NULL);

  return NULL;
}

void *
suscan_analyzer_read(suscan_analyzer_t *analyzer, uint32_t *type)
{
  return suscan_mq_read(analyzer->mq_out, type);
}

struct suscan_analyzer_inspector_msg *
suscan_analyzer_read_inspector_msg(suscan_analyzer_t *analyzer)
{
  /* TODO: use poll and wait to wait for EOS and inspector messages */
  return suscan_mq_read_w_type(
      analyzer->mq_out,
      SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR);
}

SUBOOL
suscan_analyzer_write(suscan_analyzer_t *analyzer, uint32_t type, void *priv)
{
  return suscan_mq_write(&analyzer->mq_in, type, priv);
}

void
suscan_analyzer_consume_mq(struct suscan_mq *mq)
{
  void *private;
  uint32_t type;

  while (suscan_mq_poll(mq, &type, &private))
    suscan_analyzer_dispose_message(type, private);
}

SUPRIVATE SUBOOL
suscan_analyzer_consume_mq_until_halt(struct suscan_mq *mq)
{
  void *private;
  uint32_t type;

  while (suscan_mq_poll(mq, &type, &private))
    if (type == SUSCAN_WORKER_MSG_TYPE_HALT)
      return SU_TRUE;
    else
      suscan_analyzer_dispose_message(type, private);

  return SU_FALSE;
}

SUBOOL
suscan_analyzer_halt_worker(suscan_worker_t *worker)
{
  while (worker->state == SUSCAN_WORKER_STATE_RUNNING) {
    suscan_worker_req_halt(worker);

    while (!suscan_analyzer_consume_mq_until_halt(worker->mq_out))
      suscan_mq_wait(worker->mq_out);
  }

  return suscan_worker_destroy(worker);
}


SUPRIVATE void
suscan_analyzer_init_detector_params(
    suscan_analyzer_t *analyzer,
    struct sigutils_channel_detector_params *params)
{
  /* Recover template */
  *params = analyzer->params.detector_params;

  /* Populate members with source information */
  params->mode = SU_CHANNEL_DETECTOR_MODE_DISCOVERY;
  params->samp_rate = suscan_analyzer_get_samp_rate(analyzer);

  /* Adjust parameters that depend on sample rate */
  su_channel_params_adjust(params);

#if 0
  /* Make alpha a little bigger, to provide a more dynamic spectrum */
  if (params->alpha <= .05)
    params->alpha *= 20;
#endif
}

/********************** Suscan analyzer public API ***************************/
void
suscan_analyzer_source_barrier(suscan_analyzer_t *analyzer)
{
  pthread_barrier_wait(&analyzer->barrier);
}

void
suscan_analyzer_enter_sched(suscan_analyzer_t *analyzer)
{
  pthread_mutex_lock(&analyzer->sched_lock);
}

void
suscan_analyzer_leave_sched(suscan_analyzer_t *analyzer)
{
  pthread_mutex_unlock(&analyzer->sched_lock);
}

su_specttuner_channel_t *
suscan_analyzer_open_channel_ex(
    suscan_analyzer_t *analyzer,
    const struct sigutils_channel *chan_info,
    SUBOOL precise,
    SUBOOL (*on_data) (
        const struct sigutils_specttuner_channel *channel,
        void *privdata,
        const SUCOMPLEX *data, /* This pointer remains valid until the next call to feed */
        SUSCOUNT size),
        void *privdata)
{
  su_specttuner_channel_t *channel = NULL;
  struct sigutils_specttuner_channel_params params =
      sigutils_specttuner_channel_params_INITIALIZER;

  params.f0 =
      SU_NORM2ANG_FREQ(
          SU_ABS2NORM_FREQ(
              suscan_analyzer_get_samp_rate(analyzer),
              chan_info->fc - chan_info->ft));

  if (params.f0 < 0)
    params.f0 += 2 * PI;

  params.bw =
      SU_NORM2ANG_FREQ(
          SU_ABS2NORM_FREQ(
              suscan_analyzer_get_samp_rate(analyzer),
              chan_info->f_hi - chan_info->f_lo));
  params.guard = SUSCAN_ANALYZER_GUARD_BAND_PROPORTION;
  params.on_data = on_data;
  params.privdata = privdata;
  params.precise = precise;

  suscan_analyzer_enter_sched(analyzer);

  SU_TRYCATCH(
      channel = su_specttuner_open_channel(analyzer->stuner, &params),
      goto done);

done:
  suscan_analyzer_leave_sched(analyzer);

  return channel;
}

su_specttuner_channel_t *
suscan_analyzer_open_channel(
    suscan_analyzer_t *analyzer,
    const struct sigutils_channel *chan_info,
    SUBOOL (*on_data) (
        const struct sigutils_specttuner_channel *channel,
        void *privdata,
        const SUCOMPLEX *data, /* This pointer remains valid until the next call to feed */
        SUSCOUNT size),
        void *privdata)
{
  return suscan_analyzer_open_channel_ex(
      analyzer,
      chan_info,
      SU_FALSE,
      on_data,
      privdata);
}

SUBOOL
suscan_analyzer_close_channel(
    suscan_analyzer_t *analyzer,
    su_specttuner_channel_t *channel)
{
  SUBOOL ok;

  suscan_analyzer_enter_sched(analyzer);

  ok = su_specttuner_close_channel(analyzer->stuner, channel);

  suscan_analyzer_leave_sched(analyzer);

  return ok;
}

/*
 * There is no explicit UNBIND. Unbind happens inside
 * suscan_analyzer_on_channel_data when the inspector state
 * is different from RUNNING.
 */

SUBOOL
suscan_analyzer_bind_inspector_to_channel(
    suscan_analyzer_t *analyzer,
    su_specttuner_channel_t *channel,
    suscan_inspector_t *insp)
{
  struct suscan_inspector_task_info *task_info = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      task_info = suscan_inspector_task_info_new(insp),
      return SU_FALSE);

  task_info->channel = channel;

  suscan_analyzer_enter_sched(analyzer);

  SU_TRYCATCH(
      suscan_inspsched_append_task_info(analyzer->sched, task_info),
      goto done);

  /*
   * Task info registered, binding it to the inspector. Time to bind
   * this task to the channel, so it knows that to do when new data
   * arrives to it.
   */
  channel->params.privdata = task_info;

  /* Now we can say that the inspector is actually running */
  insp->state = SUSCAN_ASYNC_STATE_RUNNING;

  task_info = NULL;

  ok = SU_TRUE;

done:
  suscan_analyzer_leave_sched(analyzer);

  if (task_info != NULL)
    suscan_inspector_task_info_destroy(task_info);

  return ok;
}

void
suscan_analyzer_destroy(suscan_analyzer_t *analyzer)
{
  uint32_t type;
  unsigned int i;

  void *private;

  /* Prevent source from entering in timeout loops */
  if (analyzer->source != NULL)
    suscan_source_force_eos(analyzer->source);

  if (analyzer->running) {
    if (!analyzer->halt_requested) {
      suscan_analyzer_req_halt(analyzer);

      /*
       * TODO: this cannot wait forever. Add suscan_mq_read_with_timeout
       */
      while (!suscan_analyzer_consume_mq_until_halt(analyzer->mq_out))
        suscan_mq_wait(analyzer->mq_out);
    }

    /* TODO: add a timeout here too */
    if (pthread_join(analyzer->thread, NULL) == -1) {
      SU_ERROR("Thread failed to join, memory leak ahead\n");
      return;
    }
  }

  /* TODO: Concurrently-force EOS in source object */

  if (analyzer->source_wk != NULL)
    if (!suscan_analyzer_halt_worker(analyzer->source_wk)) {
      SU_ERROR("Source worker destruction failed, memory leak ahead\n");
      return;
    }

  if (analyzer->slow_wk != NULL)
    if (!suscan_analyzer_halt_worker(analyzer->slow_wk)) {
      SU_ERROR("Slow worker destruction failed, memory leak ahead\n");
      return;
    }

  /* Halt all inspector scheduler workers */
  if (analyzer->sched != NULL) {
    if (!suscan_inspsched_destroy(analyzer->sched)) {
      SU_ERROR("Failed to shutdown inspector scheduler, memory leak ahead\n");
      return;
    }

    /* FIXME: Add flag to prevent initialization errors! */
    pthread_barrier_destroy(&analyzer->barrier);

    pthread_mutex_destroy(&analyzer->sched_lock);
  }

  /* Consume any pending messages */
  suscan_analyzer_consume_mq(&analyzer->mq_in);

  /* Free channel detector */
  if (analyzer->detector != NULL)
    su_channel_detector_destroy(analyzer->detector);

  if (analyzer->loop_init)
    pthread_mutex_destroy(&analyzer->loop_mutex);

  /* Free spectral tuner */
  if (analyzer->stuner != NULL)
    su_specttuner_destroy(analyzer->stuner);

  /* Free read buffer */
  if (analyzer->read_buf != NULL)
    free(analyzer->read_buf);

  /* Remove all channel analyzers */
  for (i = 0; i < analyzer->inspector_count; ++i)
    if (analyzer->inspector_list[i] != NULL)
      suscan_inspector_destroy(analyzer->inspector_list[i]);

  if (analyzer->inspector_list != NULL)
    free(analyzer->inspector_list);

  /* Delete source information */
  if (analyzer->source != NULL)
    suscan_source_destroy(analyzer->source);

  /* Release slow worker data */
  suscan_analyzer_destroy_slow_worker_data(analyzer);

  if (analyzer->throttle_mutex_init)
    pthread_mutex_destroy(&analyzer->throttle_mutex);

  /* Delete all baseband filters */
  for (i = 0; i < analyzer->bbfilt_count; ++i)
    if (analyzer->bbfilt_list[i] != NULL)
      suscan_analyzer_baseband_filter_destroy(analyzer->bbfilt_list[i]);

  if (analyzer->bbfilt_list != NULL)
    free(analyzer->bbfilt_list);

  suscan_mq_finalize(&analyzer->mq_in);

  free(analyzer);
}

SUBOOL
suscan_analyzer_source_init(
    suscan_analyzer_t *analyzer,
    suscan_source_config_t *config)
{
  SU_TRYCATCH(analyzer->source = suscan_source_new(config), goto fail);

  /* For non-realtime sources (i.e. file sources), enable throttling */
  if (!suscan_analyzer_is_real_time(analyzer)) {
    /* Create throttle mutex */
      (void) pthread_mutex_init(&analyzer->throttle_mutex, NULL); /* Always succeeds */
      analyzer->throttle_mutex_init = SU_TRUE;

    suscan_throttle_init(
        &analyzer->throttle,
        suscan_analyzer_get_samp_rate(analyzer));
  }

  analyzer->effective_samp_rate = suscan_analyzer_get_samp_rate(analyzer);

  return SU_TRUE;

fail:
  return SU_FALSE;
}

SUBOOL
suscan_analyzer_set_iq_reverse(suscan_analyzer_t *analyzer, SUBOOL rev)
{
  analyzer->iq_rev = rev;
  return SU_TRUE;
}


suscan_analyzer_t *
suscan_analyzer_new(
    const struct suscan_analyzer_params *params,
    suscan_source_config_t *config,
    struct suscan_mq *mq)
{
  suscan_analyzer_t *new = NULL;
  struct sigutils_specttuner_params st_params =
      sigutils_specttuner_params_INITIALIZER;
  struct sigutils_channel_detector_params det_params;
  unsigned int worker_count;
  unsigned int i;

#ifdef SUSCAN_DEBUG_THROTTLE
  dbg_rate_set = SU_FALSE;
#endif /* SUSCAN_DEBUG_THROTTLE */

  if ((new = calloc(1, sizeof (suscan_analyzer_t))) == NULL) {
    SU_ERROR("Cannot allocate analyzer\n");
    goto fail;
  }

  new->params = *params;

  /* Allocate read buffer */
  if ((new->read_buf = malloc(
      SUSCAN_SOURCE_DEFAULT_BUFSIZ * sizeof(SUCOMPLEX))) == NULL) {
    SU_ERROR("Failed to allocate read buffer\n");
    goto fail;
  }

  new->read_size = SUSCAN_SOURCE_DEFAULT_BUFSIZ;

  /* Create input message queue */
  if (!suscan_mq_init(&new->mq_in)) {
    SU_ERROR("Cannot allocate input MQ\n");
    goto fail;
  }

  /* Initialize source */
  if (!suscan_analyzer_source_init(new, config)) {
    SU_ERROR("Failed to initialize source\n");
    goto fail;
  }

  /* Periodic updates */
  new->interval_channels = params->channel_update_int;
  new->interval_psd      = params->psd_update_int;

  /* Create channel detector */
  (void) pthread_mutex_init(&new->loop_mutex, NULL); /* Always succeeds */
  new->loop_init = SU_TRUE;
  det_params = params->detector_params;
  suscan_analyzer_init_detector_params(new, &det_params);
  SU_TRYCATCH(
      new->detector = su_channel_detector_new(&det_params),
      goto fail);

  /* Create source worker */
  if ((new->source_wk = suscan_worker_new(&new->mq_in, new))
      == NULL) {
    SU_ERROR("Cannot create source worker thread\n");
    goto fail;
  }

  /* Create slow worker */
  if ((new->slow_wk = suscan_worker_new(&new->mq_in, new))
      == NULL) {
    SU_ERROR("Cannot create slow worker thread\n");
    goto fail;
  }

  /* Initialize gain request mutex */
  SU_TRYCATCH(pthread_mutex_init(&new->hotconf_mutex, NULL) != -1, goto fail);
  new->gain_req_mutex_init = SU_TRUE;

  /* Create spectral tuner, with matching read size */
  st_params.window_size = new->read_size * 4;
  SU_TRYCATCH(new->stuner = su_specttuner_new(&st_params), goto fail);

  /* Create inspector scheduler and barrier */
  SU_TRYCATCH(new->sched = suscan_inspsched_new(new), goto fail);

  /*
   * During barrier initialization, we take the number of scheduler workers
   * plus 1 (the source worker)
   */
  SU_TRYCATCH(
      pthread_barrier_init(
          &new->barrier,
          NULL,
          suscan_inspsched_get_num_workers(new->sched) + 1) == 0,
      goto fail);

  /*
   * This mutex will protect the spectral tuner from concurrent access by
   * consumer, analyzer and scheduler worker threads
   */
  SU_TRYCATCH(pthread_mutex_init(&new->sched_lock, NULL) == 0, goto fail);

  new->mq_out = mq;

  if (pthread_create(
      &new->thread,
      NULL,
      suscan_analyzer_thread,
      new) == -1) {
    SU_ERROR("Cannot create main thread\n");
    goto fail;
  }

  new->running = SU_TRUE;

  return new;

fail:
  if (new != NULL)
    suscan_analyzer_destroy(new);

  return NULL;
}
