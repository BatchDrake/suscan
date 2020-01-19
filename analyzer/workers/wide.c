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

#include "analyzer.h"

#include "mq.h"
#include "msg.h"

/*
 * TODO: Add methods to define partition bandwidth
 */
SUINLINE SUBOOL
suscan_analyzer_hop(suscan_analyzer_t *self)
{
  SUFLOAT rnd = (SUFLOAT) rand() / (SUFLOAT) RAND_MAX;
  SUFREQ fs = suscan_analyzer_get_samp_rate(self);
  SUFREQ part_bw = self->current_sweep_params.partitioning
      == SUSCAN_ANALYZER_SPECTRUM_PARTITIONING_DISCRETE
      ? fs / 2
      : 1;
  SUFREQ bw =
        self->current_sweep_params.max_freq
        - self->current_sweep_params.min_freq;
  SUFREQ next;

  /*
   * For frequencies below the sample rate, we don't hop.
   * We simply stay in the same frequency until the user changes
   * the frequency range. Note that when maximum and minimum frequencies
   * are exactly the same, the hop bandwidth is actually the sample rate.
   */
  if (bw < 1) {
    next = .5 * (
        self->current_sweep_params.max_freq
        + self->current_sweep_params.min_freq);

    if (fabs(self->curr_freq - next) < 1)
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
        /*
         * Progressive strategy: traverse the spectrum monotonically,
         * in fixed steps of fs / 2
         */
        next = (fs / 2) * self->part_ndx++
          + self->current_sweep_params.min_freq;
        if (next > self->current_sweep_params.max_freq) {
          next = self->current_sweep_params.min_freq;
          self->part_ndx = 1;
        }
        break;
    }
  }

  /* All set. Go ahed and hop */
  if (suscan_source_set_freq2(
      self->source,
      next,
      suscan_source_config_get_lnb_freq(
          suscan_source_get_config(self->source)))) {
    self->curr_freq = suscan_source_get_freq(self->source);
    return SU_TRUE;
  }

  return SU_FALSE;
}

SUBOOL
suscan_analyzer_set_sweep_stratrgy(
    suscan_analyzer_t *self,
    enum suscan_analyzer_sweep_strategy strategy)
{
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      self->params.mode == SUSCAN_ANALYZER_MODE_WIDE_SPECTRUM,
      goto done);


  self->pending_sweep_params =
        self->sweep_params_requested
        ? self->pending_sweep_params
        : self->current_sweep_params;
  self->pending_sweep_params.strategy = strategy;
  self->sweep_params_requested = SU_TRUE;

  ok = SU_TRUE;

done:
  return ok;
}

SUBOOL
suscan_analyzer_set_spectrum_partitioning(
    suscan_analyzer_t *self,
    enum suscan_analyzer_spectrum_partitioning partitioning)
{
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      self->params.mode == SUSCAN_ANALYZER_MODE_WIDE_SPECTRUM,
      goto done);

  self->pending_sweep_params =
      self->sweep_params_requested
      ? self->pending_sweep_params
      : self->current_sweep_params;
  self->pending_sweep_params.partitioning = partitioning;
  self->sweep_params_requested = SU_TRUE;

  ok = SU_TRUE;

done:
  return ok;
}

SUBOOL
suscan_analyzer_set_buffering_size(
    suscan_analyzer_t *self,
    SUSCOUNT size)
{
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      self->params.mode == SUSCAN_ANALYZER_MODE_WIDE_SPECTRUM,
      goto done);

  self->pending_sweep_params =
        self->sweep_params_requested
        ? self->pending_sweep_params
        : self->current_sweep_params;
  self->pending_sweep_params.fft_min_samples = size;
  self->sweep_params_requested = SU_TRUE;

  ok = SU_TRUE;

done:
  return ok;
}

SUBOOL
suscan_analyzer_set_hop_range(
    suscan_analyzer_t *self,
    SUFREQ min,
    SUFREQ max)
{
  SUBOOL mutex_acquired = SU_FALSE;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      self->params.mode == SUSCAN_ANALYZER_MODE_WIDE_SPECTRUM,
      goto done);

  SU_TRYCATCH(max - min >= 0, goto done);

  self->pending_sweep_params =
        self->sweep_params_requested
        ? self->pending_sweep_params
        : self->current_sweep_params;
  self->pending_sweep_params.min_freq = min;
  self->pending_sweep_params.max_freq = max;
  self->sweep_params_requested = SU_TRUE;

  ok = SU_TRUE;

done:
  return ok;
}

SUBOOL
suscan_source_wide_wk_cb(
    struct suscan_mq *mq_out,
    void *wk_private,
    void *cb_private)
{
  suscan_analyzer_t *self = (suscan_analyzer_t *) wk_private;
  SUSDIFF got;
  SUBOOL mutex_acquired = SU_FALSE;
  SUBOOL restart = SU_FALSE;
  struct timespec sub;

  SU_TRYCATCH(suscan_analyzer_lock_loop(self), goto done);
  mutex_acquired = SU_TRUE;

  /* Non real time sources are not allowed. */
  SU_TRYCATCH(suscan_analyzer_is_real_time(self), goto done);

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

    if (self->fft_samples > self->current_sweep_params.fft_min_samples) {
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
            suscan_analyzer_send_psd(self, self->detector),
            goto done);

        self->fft_samples = 0;
        su_channel_detector_rewind(self->detector);
        (void) suscan_analyzer_hop(self);
      }
    }
  } else {
    self->eos = SU_TRUE;
    self->cpu_usage = 0;

    switch (got) {
      case SU_BLOCK_PORT_READ_END_OF_STREAM:
        suscan_analyzer_send_status(
            self,
            SUSCAN_ANALYZER_MESSAGE_TYPE_EOS,
            got,
            "End of stream reached");
        break;

      case SU_BLOCK_PORT_READ_ERROR_NOT_INITIALIZED:
        suscan_analyzer_send_status(
            self,
            SUSCAN_ANALYZER_MESSAGE_TYPE_EOS,
            got,
            "Port not initialized");
        break;

      case SU_BLOCK_PORT_READ_ERROR_ACQUIRE:
        suscan_analyzer_send_status(
            self,
            SUSCAN_ANALYZER_MESSAGE_TYPE_READ_ERROR,
            got,
            "Acquire failed (source I/O error)");
        break;

      case SU_BLOCK_PORT_READ_ERROR_PORT_DESYNC:
        suscan_analyzer_send_status(
            self,
            SUSCAN_ANALYZER_MESSAGE_TYPE_EOS,
            got,
            "Port desync");
        break;

      default:
        suscan_analyzer_send_status(
            self,
            SUSCAN_ANALYZER_MESSAGE_TYPE_EOS,
            got,
            "Unexpected read result %d", got);
    }

    goto done;
  }

  restart = SU_TRUE;

done:
  if (mutex_acquired)
    (void) suscan_analyzer_unlock_loop(self);

  return restart;
}

