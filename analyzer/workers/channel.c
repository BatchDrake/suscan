/*

  Copyright (C) 2020 Gonzalo José Carracedo Carballal

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

/*
 * This is the channel analyzer worker: receives data, channelizes and
 * feeds inspectors.
 */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <stdint.h>
#include <time.h>

#define SU_LOG_DOMAIN "channel-analyzer"

#include <sigutils/sigutils.h>
#include <sigutils/detect.h>

#include "analyzer.h"

#include "mq.h"
#include "msg.h"

/*********************** Performance measurement *****************************/
SUINLINE void
suscan_analyzer_read_start(suscan_analyzer_t *analyzer)
{
  clock_gettime(CLOCK_MONOTONIC_COARSE, &analyzer->read_start);
}

SUINLINE void
suscan_analyzer_process_start(suscan_analyzer_t *analyzer)
{
  clock_gettime(CLOCK_MONOTONIC_COARSE, &analyzer->process_start);
}

SUINLINE void
suscan_analyzer_process_end(suscan_analyzer_t *analyzer)
{
  struct timespec sub;
  uint64_t total, cpu;

  clock_gettime(CLOCK_MONOTONIC_COARSE, &analyzer->process_end);

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


/********************* Related channel analyzer funcs ************************/
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

/******************** Source worker for channel mode *************************/
SUPRIVATE SUBOOL
suscan_analyzer_parse_overridable(suscan_analyzer_t *self)
{
  struct suscan_inspector_overridable_request *this, *next;
  SUBOOL ok = SU_FALSE;
  SUFLOAT f0;
  SUFLOAT relbw;

  if (self->insp_overridable != NULL) {
    SU_TRYCATCH(suscan_analyzer_lock_inspector_list(self), goto done);

    while (self->insp_overridable != NULL) {
      this = self->insp_overridable;
      next = self->insp_overridable->next;

      if (!this->dead) {
        /* Acknowledged */
        suscan_inspector_set_userdata(this->insp, NULL);

        /* Parse this request */
        if (this->freq_request) {
          f0 = SU_NORM2ANG_FREQ(
                SU_ABS2NORM_FREQ(
                    suscan_analyzer_get_samp_rate(self),
                    this->new_freq));

          if (f0 < 0)
            f0 += 2 * PI;

          su_specttuner_set_channel_freq(
              self->stuner,
              suscan_inspector_get_channel(this->insp),
              f0);
        }

        /* Set bandwidth request */
        if (this->bandwidth_request) {
          relbw = SU_NORM2ANG_FREQ(
                SU_ABS2NORM_FREQ(
                    suscan_analyzer_get_samp_rate(self),
                    this->new_bandwidth));
          su_specttuner_set_channel_bandwidth(
              self->stuner,
              suscan_inspector_get_channel(this->insp),
              relbw);
          SU_TRYCATCH(
              suscan_inspector_notify_bandwidth(this->insp, this->new_bandwidth),
              goto done);
        }
      }

      self->insp_overridable = next;
    }
    suscan_analyzer_unlock_inspector_list(self);
  }

  ok = SU_TRUE;

done:
  return ok;
}

SUBOOL
suscan_source_channel_wk_cb(
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
  struct timespec sub;
  SUFLOAT seconds;

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

  SU_TRYCATCH(suscan_analyzer_parse_overridable(analyzer), goto done);

  /* Ready to read */
  suscan_analyzer_read_start(analyzer);

  if ((got = suscan_source_read(
      analyzer->source,
      analyzer->read_buf,
      read_size)) > 0) {
    suscan_analyzer_process_start(analyzer);

    if (analyzer->iq_rev)
      suscan_analyzer_do_iq_rev(analyzer->read_buf, got);

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

    if (analyzer->det_feed) {
      /* Feed channel detector! */
      SU_TRYCATCH(
          su_channel_detector_feed_bulk(
              analyzer->detector,
              analyzer->read_buf,
              got) == got,
          goto done);
      analyzer->det_count += got;
      if (analyzer->det_count > analyzer->params.detector_params.window_size) {
        SU_TRYCATCH(
            suscan_analyzer_send_psd(analyzer, analyzer->detector),
            goto done);
        analyzer->last_psd = analyzer->read_start;
        analyzer->det_count = 0;
        analyzer->det_feed = SU_FALSE;
      }
    }

    if (analyzer->interval_psd > 0 && !analyzer->det_feed) {
      timespecsub(&analyzer->read_start, &analyzer->last_psd, &sub);
      seconds = sub.tv_sec + sub.tv_nsec * 1e-9;

      if (seconds >= analyzer->interval_psd)
        analyzer->det_feed = SU_TRUE;
    }

    if (SUSCAN_ANALYZER_FS_MEASURE_INTERVAL > 0) {
      timespecsub(&analyzer->read_start, &analyzer->last_measure, &sub);
      seconds = sub.tv_sec + sub.tv_nsec * 1e-9;

      if (seconds >= SUSCAN_ANALYZER_FS_MEASURE_INTERVAL) {
        analyzer->measured_samp_rate =
            analyzer->measured_samp_count / seconds;
        analyzer->measured_samp_count = 0;
        analyzer->last_measure = analyzer->read_start;
#ifdef SUSCAN_DEBUG_THROTTLE
        printf("Read rate: %g\n", analyzer->measured_samp_rate);
#endif /* SUSCAN_DEBUG_THROTTLE */
      }

      analyzer->measured_samp_count += got;
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

  restart = SU_TRUE;

done:
  if (mutex_acquired)
    (void) suscan_analyzer_unlock_loop(analyzer);

  return restart;
}

