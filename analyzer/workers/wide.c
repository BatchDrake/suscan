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
 * This is the wide spectrum analyzer: walks the whole spectrum randomly,
 * given two limits, and returns PSD messages.
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <stdint.h>
#include <time.h>

#define SU_LOG_DOMAIN "wide-analyzer"

#include <sigutils/sigutils.h>
#include <sigutils/detect.h>
#include <analyzer/impl/local.h>

#include "mq.h"
#include "msg.h"

static uint64_t micros() {
  struct timeval tv;
  gettimeofday(&tv,NULL);
  return tv.tv_sec * 1000000ull + tv.tv_usec;
}

/*
 * TODO: Add methods to define partition bandwidth
 */
SUINLINE SUBOOL
suscan_local_analyzer_hop(suscan_local_analyzer_t *self)
{
  SUFLOAT rnd = (SUFLOAT) rand() / (SUFLOAT) RAND_MAX;
  SUFREQ fs = suscan_analyzer_get_samp_rate(self->parent);
  SUFREQ part_bw = self->current_sweep_params.partitioning
      == SUSCAN_ANALYZER_SPECTRUM_PARTITIONING_DISCRETE
      ? fs * self->current_sweep_params.rel_bw
      : 1;
  SUFREQ bw =
        self->current_sweep_params.max_freq
        - self->current_sweep_params.min_freq;
  SUFREQ next = .5 * (
      self->current_sweep_params.max_freq
      + self->current_sweep_params.min_freq);
  uint64_t t0, hop_time;

  /*
   * For frequencies below the sample rate, we don't hop.
   * We simply stay in the same frequency until the user changes
   * the frequency range. Note that when maximum and minimum frequencies
   * are exactly the same, the hop bandwidth is actually the sample rate.
   */

  if (bw < 1) {
    if (sufeq(self->curr_freq, next, 1))
      return SU_TRUE;
  } else {
    switch (self->current_sweep_params.strategy) {
      /*
       * Stochastic strategy: traverse the spectrum stochastically.
       * This is the original Monte Carlo approach.
       */
      case SUSCAN_ANALYZER_SWEEP_STRATEGY_STOCHASTIC:
        next = part_bw * SU_FLOOR(rnd * bw / part_bw)
            + self->current_sweep_params.min_freq;
        break;

      case SUSCAN_ANALYZER_SWEEP_STRATEGY_PROGRESSIVE:
        /* Progressive strategy: traverse the spectrum monotonically */
        if (self->current_sweep_params.partitioning ==
            SUSCAN_ANALYZER_SPECTRUM_PARTITIONING_DISCRETE) {
          /* Discrete: advance in fixed steps of fs * rel_bw */
          next = part_bw * self->part_ndx++
            + self->current_sweep_params.min_freq;
          if (next > self->current_sweep_params.max_freq) {
            if (self->curr_freq < self->current_sweep_params.max_freq - part_bw * 0.5) {
              next = self->current_sweep_params.max_freq;
              self->part_ndx = 0;
            } else {
              next = self->current_sweep_params.min_freq;
              self->part_ndx = 1;
            }
          }
        } else {
          /* Continuous: advance monotonically in randomized steps */
          SUFLOAT step_size = fs * self->current_sweep_params.rel_bw;
          SUFLOAT freq_jiggle = SU_FLOOR(step_size * rnd * 0.2);
          next = self->curr_freq + step_size - freq_jiggle;
          if (next > self->current_sweep_params.max_freq) {
            if (self->curr_freq < self->current_sweep_params.max_freq - step_size * 0.5) {
              next = self->current_sweep_params.max_freq - freq_jiggle;
            } else {
              next = self->current_sweep_params.min_freq + step_size * 0.5
                  - freq_jiggle;
            }
          } else if (next < self->current_sweep_params.min_freq) {
            /* can happen on first run when self->curr_freq may be invalid */
            next = self->current_sweep_params.min_freq;
          }
        }
        break;
    }
  }

  /* All set. Go ahed and hop */
  t0 = micros();
  if (suscan_source_set_freq2(
      self->source,
      next,
      suscan_source_config_get_lnb_freq(
          suscan_source_get_config(self->source)))) {
    hop_time = micros() - t0;
    self->hop_samples = fs * hop_time / 1000000;
    self->curr_freq = suscan_source_get_freq(self->source);
    self->source_info.frequency = self->curr_freq;

    return SU_TRUE;
  }

  return SU_FALSE;
}

SUBOOL
suscan_source_wide_wk_cb(
    struct suscan_mq *mq_out,
    void *wk_private,
    void *cb_private)
{
  suscan_local_analyzer_t *self = (suscan_local_analyzer_t *) wk_private;
  SUSDIFF got;
  SUBOOL mutex_acquired = SU_FALSE;
  SUBOOL restart = SU_FALSE;

  SU_TRYCATCH(suscan_local_analyzer_lock_loop(self), goto done);
  mutex_acquired = SU_TRUE;

  /* Non real time sources are not allowed. */
  SU_TRYCATCH(suscan_analyzer_is_real_time(self->parent), goto done);

  if (self->sweep_params_requested) {
    self->current_sweep_params = self->pending_sweep_params;
    self->sweep_params_requested = SU_FALSE;
  }

  if ((got = suscan_source_read(
      self->source,
      self->read_buf,
      self->read_size)) > 0) {

    if (self->iq_rev)
      suscan_analyzer_do_iq_rev(self->read_buf, got);
    self->fft_samples += got;

    if (self->fft_samples > self->current_sweep_params.fft_min_samples +
        self->hop_samples) {
      /* Feed detector (works in spectrum mode only) */
      SU_TRYCATCH(
          su_channel_detector_feed_bulk(
              self->detector,
              self->read_buf,
              got) == got,
          goto done);

      /*
       * Reached threshold. Send message and hop. Note we do this right here,
       * in the source worker. This way we ensure synchronous arrival
       * of samples at the selected frequency.
       */

      if (su_channel_detector_get_iters(self->detector) > 0) {
        SU_TRYCATCH(
            suscan_analyzer_send_psd(self->parent, self->detector),
            goto done);

        self->fft_samples = 0;
        su_channel_detector_rewind(self->detector);
        if (!suscan_local_analyzer_hop(self))
          SU_ERROR("Hop failed!\n");
      }
    }
  } else {
    self->parent->eos = SU_TRUE; /* TODO: use force_eos? */
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

  restart = SU_TRUE;

done:
  if (mutex_acquired)
    (void) suscan_local_analyzer_unlock_loop(self);

  return restart;
}

SUPRIVATE void
suscan_local_analyzer_init_detector_params(
    suscan_local_analyzer_t *self,
    struct sigutils_channel_detector_params *params)
{
  /* Recover template */
  *params = self->parent->params.detector_params;

  /* Populate members with source information */
  params->mode = SU_CHANNEL_DETECTOR_MODE_SPECTRUM;
  params->samp_rate = self->source_info.source_samp_rate;

  /* Adjust parameters that depend on sample rate */
  su_channel_params_adjust(params);

#if 0
  /* Make alpha a little bigger, to provide a more dynamic spectrum */
  if (params->alpha <= .05)
    params->alpha *= 20;
#endif
}

SUBOOL
suscan_local_analyzer_init_wide_worker(suscan_local_analyzer_t *self)
{
  struct sigutils_channel_detector_params det_params;
  SUBOOL ok = SU_FALSE;

  det_params = self->parent->params.detector_params;
  suscan_local_analyzer_init_detector_params(self, &det_params);

  SU_MAKE(self->detector, su_channel_detector, &det_params);

  /*
    * In case the source rejected our initial sample rate configuration, we
    * update the detector accordingly.
    *
    * We do this here and not in the header thread because, although this
    * can be slower, we ensure this way we can provide an accurate value of the
    * sample rate right after the analyzer object is created.
    */
  if (self->source_info.source_samp_rate != self->detector->params.samp_rate) {
    det_params = self->detector->params;
    det_params.samp_rate = self->source_info.effective_samp_rate;
    SU_TRY(suscan_local_analyzer_readjust_detector(self, &det_params));
  }

  SU_TRY(self->parent->params.max_freq >= self->parent->params.min_freq);

  self->current_sweep_params.fft_min_samples =
          SUSCAN_ANALYZER_MIN_POST_HOP_FFTS * det_params.window_size;
  self->current_sweep_params.max_freq = self->parent->params.max_freq;
  self->current_sweep_params.min_freq = self->parent->params.min_freq;
  self->current_sweep_params.rel_bw = 0.5;
  self->sweep_params_requested = SU_FALSE;

  self->hop_samples = 0;

  ok = SU_TRUE;

done:
  return ok;
}

SUBOOL
suscan_local_analyzer_start_wide_worker(suscan_local_analyzer_t *self)
{
  SUBOOL ok = SU_FALSE;

  if (!suscan_worker_push(
    self->source_wk,
      suscan_source_wide_wk_cb,
      self->source)) {
    suscan_analyzer_send_status(
        self->parent,
        SUSCAN_ANALYZER_MESSAGE_TYPE_SOURCE_INIT,
        SUSCAN_ANALYZER_INIT_FAILURE,
        "Failed to push source callback to worker (wide spectrum mode)");
    goto done;
  }

  ok = SU_TRUE;

done:
  return ok;
}
