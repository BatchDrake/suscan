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

#define SU_LOG_DOMAIN "suscan-inspector"

#include <sigutils/sigutils.h>
#include <sigutils/sampling.h>

#include "source.h"
#include "inspector.h"

#define SUSCAN_INSPECTOR_DEFAULT_ROLL_OFF  .35
#define SUSCAN_INSPECTOR_DEFAULT_EQ_MU     1e-3
#define SUSCAN_INSPECTOR_DEFAULT_EQ_LENGTH 20
#define SUSCAN_INSPECTOR_MAX_MF_SPAN       1024

SUPRIVATE SUSCOUNT
suscan_inspector_mf_span(SUSCOUNT span)
{
  if (span > SUSCAN_INSPECTOR_MAX_MF_SPAN) {
    SU_WARNING(
        "Matched filter sample span too big (%d), truncating to %d\n",
        span, SUSCAN_INSPECTOR_MAX_MF_SPAN);
    span = SUSCAN_INSPECTOR_MAX_MF_SPAN;
  }

  return span;
}

SUPRIVATE void
suscan_inspector_lock(suscan_inspector_t *insp)
{
  (void) pthread_mutex_lock(&insp->mutex);
}

SUPRIVATE void
suscan_inspector_unlock(suscan_inspector_t *insp)
{
  (void) pthread_mutex_unlock(&insp->mutex);
}

void
suscan_inspector_request_params(
    suscan_inspector_t *insp,
    struct suscan_inspector_params *params_request)
{
  suscan_inspector_lock(insp);

  insp->params_request = *params_request;

  insp->params_requested = SU_TRUE;

  suscan_inspector_unlock(insp);
}

void
suscan_inspector_reset_equalizer(suscan_inspector_t *insp)
{
  suscan_inspector_lock(insp);

  su_equalizer_reset(&insp->eq);

  suscan_inspector_unlock(insp);
}

void
suscan_inspector_assert_params(suscan_inspector_t *insp)
{
  SUFLOAT fs;
  SUBOOL mf_changed;
  su_iir_filt_t mf = su_iir_filt_INITIALIZER;

  if (insp->params_requested) {
    suscan_inspector_lock(insp);

    mf_changed =
        (insp->params.baud != insp->params_request.baud)
        || (insp->params.mf_rolloff != insp->params_request.mf_rolloff);
    insp->params = insp->params_request;

    fs = insp->equiv_fs; /* Use equivalent sample rate after dectimation */

    /* Update inspector according to params */
    if (insp->params.baud > 0)
      insp->sym_period = 1. / SU_ABS2NORM_BAUD(fs, insp->params.baud);
    else
      insp->sym_period = 0;

    /* Update local oscillator frequency and phase */
    su_ncqo_set_freq(
        &insp->lo,
        SU_ABS2NORM_FREQ(fs, insp->params.fc_off));
    insp->phase = SU_C_EXP(I * insp->params.fc_phi);

    /* Update baudrate */
    su_clock_detector_set_baud(
        &insp->cd,
        SU_ABS2NORM_BAUD(fs, insp->params.baud));

    insp->cd.alpha = insp->params.br_alpha;
    insp->cd.beta = insp->params.br_beta;

    /* Update equalizer */
    insp->eq.params.mu = insp->params.eq_mu;

    /* Update matched filter */
    if (mf_changed) {
      if (!su_iir_rrc_init(
          &mf,
          suscan_inspector_mf_span(6 * insp->sym_period),
          insp->sym_period,
          insp->params.mf_rolloff)) {
        SU_ERROR("No memory left to update matched filter!\n");
      } else {
        su_iir_filt_finalize(&insp->mf);
        insp->mf = mf;
      }
    }

    /* Re-center costas loops */
    if (insp->params.fc_ctrl == SUSCAN_INSPECTOR_CARRIER_CONTROL_MANUAL) {
      su_ncqo_set_freq(&insp->costas_2.ncqo, 0);
      su_ncqo_set_freq(&insp->costas_4.ncqo, 0);
      su_ncqo_set_freq(&insp->costas_8.ncqo, 0);
    }
    insp->params_requested = SU_FALSE;

    suscan_inspector_unlock(insp);
  }
}

void
suscan_inspector_destroy(suscan_inspector_t *insp)
{
  pthread_mutex_destroy(&insp->mutex);

  if (insp->fac_baud_det != NULL)
    su_channel_detector_destroy(insp->fac_baud_det);

  if (insp->nln_baud_det != NULL)
    su_channel_detector_destroy(insp->nln_baud_det);

  su_iir_filt_finalize(&insp->mf);

  su_agc_finalize(&insp->agc);

  su_costas_finalize(&insp->costas_2);

  su_costas_finalize(&insp->costas_4);

  su_costas_finalize(&insp->costas_8);

  su_clock_detector_finalize(&insp->cd);

  su_equalizer_finalize(&insp->eq);

  free(insp);
}

/*
 * Spike durations measured in symbol times
 * SUSCAN_INSPECTOR_FAST_RISE_FRAC has been doubled to reduce phase noise
 * induced by the non-linearity of the AGC
 */
#define SUSCAN_INSPECTOR_FAST_RISE_FRAC   (2 * 3.9062e-1)
#define SUSCAN_INSPECTOR_FAST_FALL_FRAC   (2 * SUSCAN_INSPECTOR_FAST_RISE_FRAC)
#define SUSCAN_INSPECTOR_SLOW_RISE_FRAC   (10 * SUSCAN_INSPECTOR_FAST_RISE_FRAC)
#define SUSCAN_INSPECTOR_SLOW_FALL_FRAC   (10 * SUSCAN_INSPECTOR_FAST_FALL_FRAC)
#define SUSCAN_INSPECTOR_HANG_MAX_FRAC    (SUSCAN_INSPECTOR_FAST_RISE_FRAC * 5)
#define SUSCAN_INSPECTOR_DELAY_LINE_FRAC  (SUSCAN_INSPECTOR_FAST_RISE_FRAC * 10)
#define SUSCAN_INSPECTOR_MAG_HISTORY_FRAC (SUSCAN_INSPECTOR_FAST_RISE_FRAC * 10)

void
suscan_inspector_params_initialize(struct suscan_inspector_params *params)
{
  memset(params, 0, sizeof (struct suscan_inspector_params));

  params->gc_ctrl = SUSCAN_INSPECTOR_GAIN_CONTROL_AUTOMATIC;
  params->gc_gain = 1;

  params->br_ctrl = SUSCAN_INSPECTOR_BAUDRATE_CONTROL_MANUAL;
  params->br_alpha = SU_PREFERED_CLOCK_ALPHA;
  params->br_beta  = SU_PREFERED_CLOCK_BETA;

  params->fc_ctrl = SUSCAN_INSPECTOR_CARRIER_CONTROL_MANUAL;

  params->mf_conf = SUSCAN_INSPECTOR_MATCHED_FILTER_BYPASS;
  params->mf_rolloff = SUSCAN_INSPECTOR_DEFAULT_ROLL_OFF;

  params->eq_conf = SUSCAN_INSPECTOR_EQUALIZER_BYPASS;
  params->eq_mu = SUSCAN_INSPECTOR_DEFAULT_EQ_MU;
}

suscan_inspector_t *
suscan_inspector_new(SUSCOUNT fs, const struct sigutils_channel *channel)
{
  suscan_inspector_t *new;
  struct sigutils_channel_detector_params cd_params =
      sigutils_channel_detector_params_INITIALIZER;
  struct sigutils_equalizer_params eq_params =
      sigutils_equalizer_params_INITIALIZER;
  struct su_agc_params agc_params = su_agc_params_INITIALIZER;
  SUFLOAT tau;

  SU_TRYCATCH(new = calloc(1, sizeof (suscan_inspector_t)), goto fail);

  new->state = SUSCAN_ASYNC_STATE_CREATED;

  /* Initialize inspector parameters */
  SU_TRYCATCH(pthread_mutex_init(&new->mutex, NULL) != -1, goto fail);

  suscan_inspector_params_initialize(&new->params);

  /*
   * Removed alpha setting. This is now automatically done by
   * adjust_to_channel
   */
  cd_params.samp_rate = fs;
  cd_params.window_size = SUSCAN_SOURCE_DEFAULT_BUFSIZ;
  su_channel_params_adjust_to_channel(&cd_params, channel);

  new->equiv_fs = (SUFLOAT) cd_params.samp_rate / cd_params.decimation;

  /* Initialize spectrum parameters */
  new->interval_psd = .1;

  /* Create generic autocorrelation-based detector */
  cd_params.mode = SU_CHANNEL_DETECTOR_MODE_AUTOCORRELATION;
  SU_TRYCATCH(new->fac_baud_det = su_channel_detector_new(&cd_params), goto fail);

  /* Create non-linear baud rate detector */
  cd_params.mode = SU_CHANNEL_DETECTOR_MODE_NONLINEAR_DIFF;
  SU_TRYCATCH(new->nln_baud_det = su_channel_detector_new(&cd_params), goto fail);

  /* Create clock detector */
  SU_TRYCATCH(
      su_clock_detector_init(
          &new->cd,
          1.,
          .5 * SU_ABS2NORM_BAUD(new->equiv_fs, cd_params.bw),
          32),
      goto fail);

  /* Initialize local oscillator */
  su_ncqo_init(&new->lo, 0);
  new->phase = 1.;

  /* Initialize AGC */
  tau = new->equiv_fs / cd_params.bw; /* Samples per symbol */

  agc_params.fast_rise_t = tau * SUSCAN_INSPECTOR_FAST_RISE_FRAC;
  agc_params.fast_fall_t = tau * SUSCAN_INSPECTOR_FAST_FALL_FRAC;
  agc_params.slow_rise_t = tau * SUSCAN_INSPECTOR_SLOW_RISE_FRAC;
  agc_params.slow_fall_t = tau * SUSCAN_INSPECTOR_SLOW_FALL_FRAC;
  agc_params.hang_max    = tau * SUSCAN_INSPECTOR_HANG_MAX_FRAC;

  /* TODO: Check whether these sizes are too big */
  agc_params.delay_line_size  = tau * SUSCAN_INSPECTOR_DELAY_LINE_FRAC;
  agc_params.mag_history_size = tau * SUSCAN_INSPECTOR_MAG_HISTORY_FRAC;

  SU_TRYCATCH(su_agc_init(&new->agc, &agc_params), goto fail);

  /* Initialize matched filter, with T = tau */
  SU_TRYCATCH(
      su_iir_rrc_init(
          &new->mf,
          suscan_inspector_mf_span(6 * tau),
          tau,
          new->params.mf_rolloff),
      goto fail);

  /* Initialize PLLs */
  SU_TRYCATCH(
      su_costas_init(
          &new->costas_2,
          SU_COSTAS_KIND_BPSK,
          0,
          SU_ABS2NORM_FREQ(new->equiv_fs, cd_params.bw),
          3,
          1e-2 * SU_ABS2NORM_FREQ(new->equiv_fs, cd_params.bw)),
      goto fail);

  SU_TRYCATCH(
      su_costas_init(
          &new->costas_4,
          SU_COSTAS_KIND_QPSK,
          0,
          SU_ABS2NORM_FREQ(new->equiv_fs, cd_params.bw),
          3,
          1e-2 * SU_ABS2NORM_FREQ(new->equiv_fs, cd_params.bw)),
      goto fail);


  SU_TRYCATCH(
      su_costas_init(
          &new->costas_8,
          SU_COSTAS_KIND_8PSK,
          0,
          SU_ABS2NORM_FREQ(new->equiv_fs, cd_params.bw),
          3,
          1e-2 * SU_ABS2NORM_FREQ(new->equiv_fs, cd_params.bw)),
      goto fail);

  /* Initialize equalizer */
  eq_params.mu = SUSCAN_INSPECTOR_DEFAULT_EQ_MU;
  eq_params.length = SUSCAN_INSPECTOR_DEFAULT_EQ_LENGTH;

  SU_TRYCATCH(su_equalizer_init(&new->eq, &eq_params), goto fail);

  return new;

fail:
  if (new != NULL)
    suscan_inspector_destroy(new);

  return NULL;
}

int
suscan_inspector_feed_bulk(
    suscan_inspector_t *insp,
    const SUCOMPLEX *x,
    int count)
{
  int i;
  SUFLOAT alpha;
  SUCOMPLEX det_x;
  SUCOMPLEX sample;
  SUCOMPLEX samp_phase_samples = insp->params.sym_phase * insp->sym_period;
  SUBOOL ok = SU_FALSE;

  insp->sym_new_sample = SU_FALSE;

  for (i = 0; i < count && !insp->sym_new_sample; ++i) {
    /*
     * Feed channel detectors. TODO: use su_channel_detector_get_last_sample
     * with nln_baud_det.
     */
    SU_TRYCATCH(
        su_channel_detector_feed(insp->fac_baud_det, x[i]),
        goto done);
    SU_TRYCATCH(
        su_channel_detector_feed(insp->nln_baud_det, x[i]),
        goto done);

    /*
     * Verify the detector signal. Skip sample if it was not consumed
     * due to decimator.
     */
    if (!su_channel_detector_sample_was_consumed(insp->fac_baud_det))
      continue;

    insp->pending =
           insp->pending
        || (su_channel_detector_get_window_ptr(insp->fac_baud_det) == 0);

    det_x = su_channel_detector_get_last_sample(insp->fac_baud_det);

    /* Re-center carrier */
    det_x *= SU_C_CONJ(su_ncqo_read(&insp->lo)) * insp->phase;

    /* Perform gain control */
    switch (insp->params.gc_ctrl) {
      case SUSCAN_INSPECTOR_GAIN_CONTROL_MANUAL:
        det_x *= 2 * insp->params.gc_gain;
        break;

      case SUSCAN_INSPECTOR_GAIN_CONTROL_AUTOMATIC:
        det_x  = 2 * su_agc_feed(&insp->agc, det_x);
        break;
    }

    /* Perform frequency correction */
    switch (insp->params.fc_ctrl) {
      case SUSCAN_INSPECTOR_CARRIER_CONTROL_MANUAL:
        sample = det_x;
        break;

      case SUSCAN_INSPECTOR_CARRIER_CONTROL_COSTAS_2:
        su_costas_feed(&insp->costas_2, det_x);
        sample = insp->costas_2.y;
        break;

      case SUSCAN_INSPECTOR_CARRIER_CONTROL_COSTAS_4:
        su_costas_feed(&insp->costas_4, det_x);
        sample = insp->costas_4.y;
        break;

      case SUSCAN_INSPECTOR_CARRIER_CONTROL_COSTAS_8:
        su_costas_feed(&insp->costas_8, det_x);
        sample = insp->costas_8.y;
        break;
    }

    /* Add matched filter, if enabled */
    if (insp->params.mf_conf == SUSCAN_INSPECTOR_MATCHED_FILTER_MANUAL)
      sample = su_iir_filt_feed(&insp->mf, sample);

    /* Check if channel sampler is enabled */
    if (insp->params.br_ctrl == SUSCAN_INSPECTOR_BAUDRATE_CONTROL_MANUAL) {
      if (insp->sym_period >= 1.) {
        insp->sym_phase += 1.;
        if (insp->sym_phase >= insp->sym_period)
          insp->sym_phase -= insp->sym_period;

        insp->sym_new_sample =
            (int) SU_FLOOR(insp->sym_phase - samp_phase_samples) == 0;

        if (insp->sym_new_sample) {
          alpha = insp->sym_phase - SU_FLOOR(insp->sym_phase);

          insp->sym_sampler_output =
              ((1 - alpha) * insp->sym_last_sample + alpha * sample);

        }
      }
      insp->sym_last_sample = sample;
    } else {
      /* Automatic baudrate control enabled */
      su_clock_detector_feed(&insp->cd, sample);

      insp->sym_new_sample = su_clock_detector_read(&insp->cd, &sample, 1) == 1;
      if (insp->sym_new_sample)
        insp->sym_sampler_output = sample;
    }

    /* Apply channel equalizer, if enabled */
    if (insp->sym_new_sample) {
      if (insp->params.eq_conf == SUSCAN_INSPECTOR_EQUALIZER_CMA) {
        suscan_inspector_lock(insp);
        insp->sym_sampler_output = su_equalizer_feed(
            &insp->eq,
            insp->sym_sampler_output);
        suscan_inspector_unlock(insp);
      }

      /* Reduce amplitude so it fits in the constellation window */
      insp->sym_sampler_output *= .75;
    }
  }

  ok = SU_TRUE;

done:
  return ok ? i : -1;
}

