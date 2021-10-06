/*

  Copyright (C) 2020 Gonzalo José Carracedo Carballal

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
SUINLINE SUBOOL
suscan_local_analyzer_parse_seek_overridable(suscan_local_analyzer_t *self)
{
  SUSCOUNT pos;

  if (self->seek_req) {
    pos = self->seek_req_value;
    suscan_source_seek(self->source, pos);
    self->seek_req = self->seek_req_value != pos;
  }

  return SU_TRUE;
}

SUINLINE SUBOOL
suscan_local_analyzer_parse_insp_overridable(suscan_local_analyzer_t *self)
{
  struct suscan_inspector_overridable_request *this, *next;
  SUBOOL ok = SU_FALSE;
  SUFLOAT f0;
  SUFLOAT relbw;

  if (self->insp_overridable != NULL) {
    SU_TRYCATCH(suscan_local_analyzer_lock_inspector_list(self), goto done);

    while ((this = self->insp_overridable) != NULL) {
      next = this->next;

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

      suscan_inspector_overridable_request_destroy(this);
      self->insp_overridable = next;
    }

    suscan_local_analyzer_unlock_inspector_list(self);
  }

  ok = SU_TRUE;

done:
  return ok;
}

SUPRIVATE SUBOOL
suscan_local_analyzer_parse_overridable(suscan_local_analyzer_t *self)
{
  /* Parse pending overridable inspector requests */
  SU_TRYCATCH(
    suscan_local_analyzer_parse_insp_overridable(self),
    return SU_FALSE);

  /* Parse pending overridable seek requests */
  SU_TRYCATCH(
    suscan_local_analyzer_parse_seek_overridable(self),
    return SU_FALSE);

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_local_analyzer_on_psd(
    void *userdata,
    const SUFLOAT *psd,
    unsigned int size)
{
  suscan_local_analyzer_t *self = (suscan_local_analyzer_t *) userdata;

  SU_TRYCATCH(
      suscan_analyzer_send_psd_from_smoothpsd(
        self->parent, 
        self->smooth_psd,
        suscan_source_has_looped(self->source)),
      return SU_FALSE);

  return SU_TRUE;
}

SUBOOL
suscan_local_analyzer_init_channel_worker(suscan_local_analyzer_t *self)
{
  struct sigutils_smoothpsd_params sp_params =
      sigutils_smoothpsd_params_INITIALIZER;
  /* Create smooth PSD */
  sp_params.fft_size     = self->parent->params.detector_params.window_size;
  sp_params.samp_rate    = self->effective_samp_rate;
  sp_params.refresh_rate = 1. / self->interval_psd;

  self->sp_params = sp_params;

  SU_TRYCATCH(
      self->smooth_psd = su_smoothpsd_new(
          &sp_params,
          suscan_local_analyzer_on_psd,
          self),
      return SU_FALSE);

  return SU_TRUE;
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
  SUBOOL mutex_acquired = SU_FALSE;
  SUBOOL restart = SU_FALSE;
  SUFLOAT seconds;

  SU_TRYCATCH(suscan_local_analyzer_lock_loop(self), goto done);
  mutex_acquired = SU_TRUE;

  /* With non-real time sources, use throttle to control CPU usage */
  if (suscan_local_analyzer_is_real_time_ex(self)) {
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

    if (!suscan_local_analyzer_is_real_time_ex(self)) {
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

    SU_TRYCATCH(
        su_smoothpsd_feed(self->smooth_psd, self->read_buf, got),
        goto done);

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

