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
#include <analyzer/impl/local.h>

#include "realtime.h"

#include "mq.h"
#include "msg.h"

/*********************** Performance measurement *****************************/
SUINLINE void
suscan_local_analyzer_read_start(suscan_local_analyzer_t *analyzeryzer)
{
  analyzeryzer->read_start = suscan_gettime_coarse();
}

SUINLINE void
suscan_local_analyzer_process_start(suscan_local_analyzer_t *analyzer)
{
  analyzer->process_start = suscan_gettime_coarse();
}

SUINLINE void
suscan_local_analyzer_process_end(suscan_local_analyzer_t *analyzer)
{
  uint64_t total, cpu;

  analyzer->process_end = suscan_gettime_coarse();

  if (analyzer->read_start != 0) {
    total = analyzer->process_end - analyzer->read_start;
    cpu = analyzer->process_end - analyzer->process_start;

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
suscan_local_analyzer_feed_baseband_filters(
    suscan_local_analyzer_t *analyzer,
    const SUCOMPLEX *samples,
    SUSCOUNT length)
{
  unsigned int i;

  for (i = 0; i < analyzer->bbfilt_count; ++i)
    if (analyzer->bbfilt_list[i] != NULL)
      if (!analyzer->bbfilt_list[i]->func(
          analyzer->bbfilt_list[i]->privdata,
          analyzer->parent,
          samples,
          length))
        return SU_FALSE;

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_local_analyzer_feed_inspectors(
    suscan_local_analyzer_t *analyzer,
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
    suscan_local_analyzer_enter_sched(analyzer);
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

    suscan_local_analyzer_leave_sched(analyzer);

    if (got == -1)
      ok = SU_FALSE;

    data += got;
    size -= got;
  }

  return ok;
}

/******************** Source worker for channel mode *************************/
SUPRIVATE SUBOOL
suscan_local_analyzer_parse_overridable(suscan_local_analyzer_t *self)
{
  struct suscan_inspector_overridable_request *this, *next;
  SUBOOL ok = SU_FALSE;
  SUFLOAT f0;
  SUFLOAT relbw;

  if (self->insp_overridable != NULL) {
    SU_TRYCATCH(suscan_local_analyzer_lock_inspector_list(self), goto done);

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
                    suscan_analyzer_get_samp_rate(self->parent),
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
                    suscan_analyzer_get_samp_rate(self->parent),
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

    suscan_local_analyzer_unlock_inspector_list(self);
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
  suscan_local_analyzer_t *self = (suscan_local_analyzer_t *) wk_private;
  SUSDIFF got;
  SUSCOUNT read_size;
  SUSCOUNT psd_win_size =
      su_channel_detector_get_window_size(self->detector);
  SUBOOL mutex_acquired = SU_FALSE;
  SUBOOL restart = SU_FALSE;
  SUFLOAT seconds;

  SU_TRYCATCH(suscan_local_analyzer_lock_loop(self), goto done);
  mutex_acquired = SU_TRUE;

  /* With non-real time sources, use throttle to control CPU usage */
  if (suscan_analyzer_is_real_time(self->parent)) {
    read_size = self->read_size;
  } else {
    SU_TRYCATCH(
        pthread_mutex_lock(&self->throttle_mutex) != -1,
        goto done);
    read_size = suscan_throttle_get_portion(
        &self->throttle,
        self->read_size);
    SU_TRYCATCH(
        pthread_mutex_unlock(&self->throttle_mutex) != -1,
        goto done);
  }

  SU_TRYCATCH(suscan_local_analyzer_parse_overridable(self), goto done);

  /* Ready to read */
  suscan_local_analyzer_read_start(self);

  if ((got = suscan_source_read(
      self->source,
      self->read_buf,
      read_size)) > 0) {
    suscan_local_analyzer_process_start(self);

    if (self->iq_rev)
      suscan_analyzer_do_iq_rev(self->read_buf, got);

    if (!suscan_analyzer_is_real_time(self->parent)) {
      SU_TRYCATCH(
          pthread_mutex_lock(&self->throttle_mutex) != -1,
          goto done);
      suscan_throttle_advance(&self->throttle, got);
      SU_TRYCATCH(
          pthread_mutex_unlock(&self->throttle_mutex) != -1,
          goto done);
    }

    SU_TRYCATCH(
        suscan_local_analyzer_feed_baseband_filters(
            self,
            self->read_buf,
            got),
        goto done);

    if (self->det_num_psd > 0) {
      /* Feed channel detector! */
      SU_TRYCATCH(
          su_channel_detector_feed_bulk(
              self->detector,
              self->read_buf,
              got) == got,
          goto done);
      self->det_count += got;
      if (self->det_count >= psd_win_size) {
        SU_TRYCATCH(
            suscan_analyzer_send_psd(self->parent, self->detector),
            goto done);
        su_channel_detector_rewind(self->detector);
        self->last_psd = self->read_start;
        self->det_count = 0;
        --self->det_num_psd;
      }
    }

    if (self->interval_psd > 0 && self->det_num_psd == 0) {
      seconds = (self->read_start - self->last_psd) * 1e-9;

      if (seconds >= self->interval_psd)
        self->det_num_psd = SU_ROUND(seconds / self->interval_psd);
    }

    if (SUSCAN_ANALYZER_FS_MEASURE_INTERVAL > 0) {
      seconds = (self->read_start - self->last_measure) * 1e-9;

      if (seconds >= SUSCAN_ANALYZER_FS_MEASURE_INTERVAL) {
        self->measured_samp_rate =
            self->measured_samp_count / seconds;
        self->measured_samp_count = 0;
        self->last_measure = self->read_start;
#ifdef SUSCAN_DEBUG_THROTTLE
        printf("Read rate: %g\n", self->measured_samp_rate);
#endif /* SUSCAN_DEBUG_THROTTLE */
      }

      self->measured_samp_count += got;
    }

    /* Feed inspectors! */
    SU_TRYCATCH(
        suscan_local_analyzer_feed_inspectors(self, self->read_buf, got),
        goto done);

  } else {
    self->parent->eos = SU_TRUE; /* TODO: Use force_eos? */
    self->cpu_usage = 0;

    switch (got) {
      case SU_BLOCK_PORT_READ_END_OF_STREAM:
        suscan_analyzer_send_status(
            self->parent,
            SUSCAN_ANALYZER_MESSAGE_TYPE_EOS,
            got,
            "End of stream reached");
        break;

      case SU_BLOCK_PORT_READ_ERROR_NOT_INITIALIZED:
        suscan_analyzer_send_status(
            self->parent,
            SUSCAN_ANALYZER_MESSAGE_TYPE_EOS,
            got,
            "Port not initialized");
        break;

      case SU_BLOCK_PORT_READ_ERROR_ACQUIRE:
        suscan_analyzer_send_status(
            self->parent,
            SUSCAN_ANALYZER_MESSAGE_TYPE_READ_ERROR,
            got,
            "Acquire failed (source I/O error)");
        break;

      case SU_BLOCK_PORT_READ_ERROR_PORT_DESYNC:
        suscan_analyzer_send_status(
            self->parent,
            SUSCAN_ANALYZER_MESSAGE_TYPE_EOS,
            got,
            "Port desync");
        break;

      default:
        suscan_analyzer_send_status(
            self->parent,
            SUSCAN_ANALYZER_MESSAGE_TYPE_EOS,
            got,
            "Unexpected read result %d", got);
    }

    goto done;
  }

  /* Finish processing */
  suscan_local_analyzer_process_end(self);

  restart = SU_TRUE;

done:
  if (mutex_acquired)
    (void) suscan_local_analyzer_unlock_loop(self);

  return restart;
}

