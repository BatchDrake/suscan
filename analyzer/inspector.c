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

suscan_config_desc_t *psk_inspector_desc;

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
  SUFLOAT actual_baud;
  su_iir_filt_t mf = su_iir_filt_INITIALIZER;

  if (insp->params_requested) {
    suscan_inspector_lock(insp);

    actual_baud = insp->params_request.br_running
        ? insp->params_request.baud
        : 0;

    mf_changed =
        (insp->params.baud != actual_baud)
        || (insp->params.mf_rolloff != insp->params_request.mf_rolloff);

    insp->params = insp->params_request;

    fs = insp->equiv_fs; /* Use equivalent sample rate after dectimation */

    /* Update inspector according to params */
    if (actual_baud > 0)
      insp->sym_period = 1. / SU_ABS2NORM_BAUD(fs, actual_baud);
    else
      insp->sym_period = 0;

    /* Update local oscillator frequency and phase */
    su_ncqo_set_freq(
        &insp->lo,
        SU_ABS2NORM_FREQ(fs, insp->params.fc_off));
    insp->phase = SU_C_EXP(I * insp->params.fc_phi);

    /* Update baudrate */
    su_clock_detector_set_baud(&insp->cd, SU_ABS2NORM_BAUD(fs, actual_baud));

    insp->cd.alpha = insp->params.br_alpha;
    insp->cd.beta = insp->params.br_beta;

    /* Update equalizer */
    insp->eq.params.mu = insp->params.eq_locked ? 0 : insp->params.eq_mu;

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

  su_softtuner_finalize(&insp->tuner);

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

SUBOOL
suscan_inspector_params_initialize_from_config(
    struct suscan_inspector_params *params,
    const suscan_config_t *config)
{
  struct suscan_field_value *value;

  suscan_inspector_params_initialize(params);

  /***************************** Gain control ******************************/
  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          "agc.gain"),
      return SU_FALSE);

  SU_TRYCATCH(value->field->type == SUSCAN_FIELD_TYPE_FLOAT, return SU_FALSE);

  params->gc_gain = SU_MAG_RAW(value->as_float);

  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          "agc.enabled"),
      return SU_FALSE);

  SU_TRYCATCH(value->field->type == SUSCAN_FIELD_TYPE_BOOLEAN, return SU_FALSE);

  params->gc_ctrl = value->as_bool
      ? SUSCAN_INSPECTOR_GAIN_CONTROL_AUTOMATIC
      : SUSCAN_INSPECTOR_GAIN_CONTROL_MANUAL;

  /***************************** Freq control ******************************/
  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          "afc.costas-order"),
      return SU_FALSE);

  SU_TRYCATCH(value->field->type == SUSCAN_FIELD_TYPE_INTEGER, return SU_FALSE);

  params->fc_ctrl = value->as_int;

  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          "afc.offset"),
      return SU_FALSE);

  SU_TRYCATCH(value->field->type == SUSCAN_FIELD_TYPE_FLOAT, return SU_FALSE);

  params->fc_off = value->as_float;

  /*************************** Matched filter ******************************/
  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          "mf.type"),
      return SU_FALSE);

  SU_TRYCATCH(value->field->type == SUSCAN_FIELD_TYPE_INTEGER, return SU_FALSE);

  params->mf_conf = value->as_int;

  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          "mf.roll-off"),
      return SU_FALSE);

  SU_TRYCATCH(value->field->type == SUSCAN_FIELD_TYPE_FLOAT, return SU_FALSE);

  params->mf_rolloff = value->as_float;

  /***************************** Equalization *****************************/
  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          "equalizer.type"),
      return SU_FALSE);

  SU_TRYCATCH(value->field->type == SUSCAN_FIELD_TYPE_INTEGER, return SU_FALSE);

  params->eq_conf = value->as_int;

  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          "equalizer.rate"),
      return SU_FALSE);

  SU_TRYCATCH(value->field->type == SUSCAN_FIELD_TYPE_FLOAT, return SU_FALSE);

  params->eq_mu = value->as_float;

  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          "equalizer.locked"),
      return SU_FALSE);

  SU_TRYCATCH(value->field->type == SUSCAN_FIELD_TYPE_BOOLEAN, return SU_FALSE);

  params->eq_locked = value->as_bool;

  /**************************** Clock recovery ****************************/
  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          "clock.type"),
      return SU_FALSE);

  SU_TRYCATCH(value->field->type == SUSCAN_FIELD_TYPE_INTEGER, return SU_FALSE);

  params->br_ctrl = value->as_int;

  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          "clock.gain"),
      return SU_FALSE);

  SU_TRYCATCH(value->field->type == SUSCAN_FIELD_TYPE_FLOAT, return SU_FALSE);

  params->br_alpha = SU_MAG_RAW(value->as_float);

  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          "clock.baud"),
      return SU_FALSE);

  SU_TRYCATCH(value->field->type == SUSCAN_FIELD_TYPE_FLOAT, return SU_FALSE);

  params->baud = value->as_float;

  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          "clock.phase"),
      return SU_FALSE);

  SU_TRYCATCH(value->field->type == SUSCAN_FIELD_TYPE_FLOAT, return SU_FALSE);

  params->sym_phase = value->as_float;

  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          "clock.running"),
      return SU_FALSE);

  SU_TRYCATCH(value->field->type == SUSCAN_FIELD_TYPE_BOOLEAN, return SU_FALSE);

  params->br_running = value->as_bool;

  return SU_TRUE;
}

SUBOOL
suscan_inspector_params_populate_config(
    const struct suscan_inspector_params *params,
    suscan_config_t *config)
{
  /***************************** Gain control ******************************/
  SU_TRYCATCH(
      suscan_config_set_float(
          config,
          "agc.gain",
          SU_DB_RAW(params->gc_gain)),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_set_bool(
          config,
          "agc.enabled",
          params->gc_ctrl == SUSCAN_INSPECTOR_GAIN_CONTROL_AUTOMATIC),
      return SU_FALSE);

  /***************************** Freq control ******************************/
  SU_TRYCATCH(
      suscan_config_set_integer(
          config,
          "afc.costas-order",
          params->fc_ctrl),
      return SU_FALSE);

  if (params->fc_ctrl != SUSCAN_INSPECTOR_CARRIER_CONTROL_MANUAL)
    SU_TRYCATCH(
        suscan_config_set_integer(
            config,
            "afc.bits-per-symbol",
            params->fc_ctrl),
        return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_set_float(
          config,
          "afc.offset",
          params->fc_off),
      return SU_FALSE);

  /*************************** Matched filter ******************************/
  SU_TRYCATCH(
      suscan_config_set_integer(
          config,
          "mf.type",
          params->mf_conf),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_set_float(
          config,
          "mf.roll-off",
          params->mf_rolloff),
      return SU_FALSE);

  /***************************** Equalization *****************************/
  SU_TRYCATCH(
      suscan_config_set_integer(
          config,
          "equalizer.type",
          params->eq_conf),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_set_float(
          config,
          "equalizer.rate",
          params->eq_mu),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_set_bool(
          config,
          "equalizer.locked",
          params->eq_locked),
      return SU_FALSE);

  /**************************** Clock recovery ****************************/
  SU_TRYCATCH(
      suscan_config_set_integer(
          config,
          "clock.type",
          params->br_ctrl),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_set_float(
          config,
          "clock.gain",
          SU_DB_RAW(params->br_alpha)),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_set_float(
          config,
          "clock.baud",
          params->baud),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_set_float(
          config,
          "clock.phase",
          params->sym_phase),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_set_bool(
          config,
          "clock.running",
          params->br_running),
      return SU_FALSE);

  return SU_TRUE;
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
  struct sigutils_softtuner_params tuner_params =
      sigutils_softtuner_params_INITIALIZER;

  SUFLOAT tau;

  SU_TRYCATCH(new = calloc(1, sizeof (suscan_inspector_t)), goto fail);
  new->state = SUSCAN_ASYNC_STATE_CREATED;
  SU_TRYCATCH(pthread_mutex_init(&new->mutex, NULL) != -1, goto fail);

  /* Initialize inspector parameters */
  suscan_inspector_params_initialize(&new->params);

  /* Initialize spectrum parameters */
  new->interval_psd = .1;

  /* Configure tuner from channel parameters */
  tuner_params.samp_rate = fs;
  su_softtuner_params_adjust_to_channel(&tuner_params, channel);
  SU_TRYCATCH(su_softtuner_init(&new->tuner, &tuner_params), goto fail);

  new->equiv_fs = (SUFLOAT) fs / tuner_params.decimation;

  /* Configure channel detectors */
  cd_params.samp_rate = new->equiv_fs;
  cd_params.window_size = SUSCAN_SOURCE_DEFAULT_BUFSIZ;
  cd_params.tune = SU_FALSE; /* Inspector already takes care of tuning */

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
          .5 * SU_ABS2NORM_BAUD(new->equiv_fs, tuner_params.bw),
          32),
      goto fail);

  /* Initialize local oscillator */
  su_ncqo_init(&new->lo, 0);
  new->phase = 1.;

  /* Initialize AGC */
  tau = new->equiv_fs / tuner_params.bw; /* Samples per symbol */

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
          SU_ABS2NORM_FREQ(new->equiv_fs, tuner_params.bw),
          3,
          1e-2 * SU_ABS2NORM_FREQ(new->equiv_fs, tuner_params.bw)),
      goto fail);

  SU_TRYCATCH(
      su_costas_init(
          &new->costas_4,
          SU_COSTAS_KIND_QPSK,
          0,
          SU_ABS2NORM_FREQ(new->equiv_fs, tuner_params.bw),
          3,
          1e-2 * SU_ABS2NORM_FREQ(new->equiv_fs, tuner_params.bw)),
      goto fail);


  SU_TRYCATCH(
      su_costas_init(
          &new->costas_8,
          SU_COSTAS_KIND_8PSK,
          0,
          SU_ABS2NORM_FREQ(new->equiv_fs, tuner_params.bw),
          3,
          1e-2 * SU_ABS2NORM_FREQ(new->equiv_fs, tuner_params.bw)),
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

SUPRIVATE SUSDIFF
suscan_inspector_feed_psk_bulk(
    suscan_inspector_t *insp,
    const SUCOMPLEX *x,
    SUSCOUNT count)
{
  SUSCOUNT i;
  SUSCOUNT osize = 0;
  SUFLOAT alpha;
  SUCOMPLEX det_x;
  SUCOMPLEX output;
  SUFLOAT new_sample = SU_FALSE;
  SUCOMPLEX samp_phase_samples = insp->params.sym_phase * insp->sym_period;

  insp->sampler_output_size = 0;

  for (i = 0; i < count && osize < SUSCAN_INSPECTOR_SAMPLER_BUF_SIZE; ++i) {
    /* Re-center carrier */
    det_x = x[i] * SU_C_CONJ(su_ncqo_read(&insp->lo)) * insp->phase;

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
        /* No-op */
        break;

      case SUSCAN_INSPECTOR_CARRIER_CONTROL_COSTAS_2:
        su_costas_feed(&insp->costas_2, det_x);
        det_x = insp->costas_2.y;
        break;

      case SUSCAN_INSPECTOR_CARRIER_CONTROL_COSTAS_4:
        su_costas_feed(&insp->costas_4, det_x);
        det_x = insp->costas_4.y;
        break;

      case SUSCAN_INSPECTOR_CARRIER_CONTROL_COSTAS_8:
        su_costas_feed(&insp->costas_8, det_x);
        det_x = insp->costas_8.y;
        break;
    }

    /* Add matched filter, if enabled */
    if (insp->params.mf_conf == SUSCAN_INSPECTOR_MATCHED_FILTER_MANUAL)
      det_x = su_iir_filt_feed(&insp->mf, det_x);

    /* Check if channel sampler is enabled */
    if (insp->params.br_ctrl == SUSCAN_INSPECTOR_BAUDRATE_CONTROL_MANUAL) {
      if (insp->sym_period >= 1.) {
        insp->sym_phase += 1.;
        if (insp->sym_phase >= insp->sym_period)
          insp->sym_phase -= insp->sym_period;

        new_sample = (int) SU_FLOOR(insp->sym_phase - samp_phase_samples) == 0;

        /* Interpolate with previos sample for improved accuracy */
        if (new_sample) {
          alpha = insp->sym_phase - SU_FLOOR(insp->sym_phase);
          output = ((1 - alpha) * insp->sampler_prev + alpha * det_x);
        }
      }

      /* Keep last sample for interpolation */
      insp->sampler_prev = det_x;
    } else {
      /* Automatic baudrate control enabled */
      su_clock_detector_feed(&insp->cd, det_x);

      new_sample = su_clock_detector_read(&insp->cd, &det_x, 1) == 1;
      if (new_sample)
        output = det_x;
    }

    /* Apply channel equalizer, if enabled */
    if (new_sample) {
      if (insp->params.eq_conf == SUSCAN_INSPECTOR_EQUALIZER_CMA) {
        suscan_inspector_lock(insp);
        output = su_equalizer_feed(&insp->eq, output);
        suscan_inspector_unlock(insp);
      }

      /* Reduce amplitude so it fits in the constellation window */
      insp->sampler_output[osize++] = output * .75;
      new_sample = SU_FALSE;
    }
  }

  insp->sampler_output_size = osize;

  return i;
}

int
suscan_inspector_feed_bulk(
    suscan_inspector_t *insp,
    const SUCOMPLEX *x,
    int count)
{
  return suscan_inspector_feed_psk_bulk(insp, x, count);

#if 0
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
#endif
}

SUBOOL
suscan_init_inspectors(void)
{
  SU_TRYCATCH(
      psk_inspector_desc = suscan_config_desc_new(),
      return SU_FALSE);

  /*********************** Gain control configuration *******************/
  SU_TRYCATCH(
      suscan_config_desc_add_field(
          psk_inspector_desc,
          SUSCAN_FIELD_TYPE_BOOLEAN,
          SU_TRUE,
          "agc.enabled",
          "Automatic Gain Control is enabled"),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_desc_add_field(
          psk_inspector_desc,
          SUSCAN_FIELD_TYPE_FLOAT,
          SU_TRUE,
          "agc.gain",
          "Manual gain (dB)"),
      return SU_FALSE);

  /******************** Frequency control configurations *****************/
  SU_TRYCATCH(
      suscan_config_desc_add_field(
          psk_inspector_desc,
          SUSCAN_FIELD_TYPE_INTEGER,
          SU_TRUE,
          "afc.costas-order",
          "Constellation order (Costas loop)"),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_desc_add_field(
          psk_inspector_desc,
          SUSCAN_FIELD_TYPE_INTEGER,
          SU_TRUE,
          "afc.bits-per-symbol",
          "Bits per symbol"),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_desc_add_field(
          psk_inspector_desc,
          SUSCAN_FIELD_TYPE_FLOAT,
          SU_TRUE,
          "afc.offset",
          "Carrier offset (Hz)"),
      return SU_FALSE);

  /********************** Matched filtering ******************************/
  SU_TRYCATCH(
      suscan_config_desc_add_field(
          psk_inspector_desc,
          SUSCAN_FIELD_TYPE_INTEGER,
          SU_TRUE,
          "mf.type",
          "Matched filter configuration"),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_desc_add_field(
          psk_inspector_desc,
          SUSCAN_FIELD_TYPE_FLOAT,
          SU_TRUE,
          "mf.roll-off",
          "Roll-off factor"),
      return SU_FALSE);

  /************************* Equalizer configuration *********************/
  SU_TRYCATCH(
      suscan_config_desc_add_field(
          psk_inspector_desc,
          SUSCAN_FIELD_TYPE_INTEGER,
          SU_TRUE,
          "equalizer.type",
          "Equalizer configuration"),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_desc_add_field(
          psk_inspector_desc,
          SUSCAN_FIELD_TYPE_FLOAT,
          SU_TRUE,
          "equalizer.rate",
          "Equalizer update rate"),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_desc_add_field(
          psk_inspector_desc,
          SUSCAN_FIELD_TYPE_BOOLEAN,
          SU_TRUE,
          "equalizer.locked",
          "Equalizer has corrected channel distortion"),
      return SU_FALSE);

  /***************************** Clock Recovery **************************/
  SU_TRYCATCH(
      suscan_config_desc_add_field(
          psk_inspector_desc,
          SUSCAN_FIELD_TYPE_INTEGER,
          SU_TRUE,
          "clock.type",
          "Clock recovery method"),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_desc_add_field(
          psk_inspector_desc,
          SUSCAN_FIELD_TYPE_FLOAT,
          SU_TRUE,
          "clock.baud",
          "Symbol rate (baud)"),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_desc_add_field(
          psk_inspector_desc,
          SUSCAN_FIELD_TYPE_FLOAT,
          SU_TRUE,
          "clock.gain",
          "Gardner's algorithm loop gain"),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_desc_add_field(
          psk_inspector_desc,
          SUSCAN_FIELD_TYPE_FLOAT,
          SU_TRUE,
          "clock.phase",
          "Symbol phase"),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_desc_add_field(
          psk_inspector_desc,
          SUSCAN_FIELD_TYPE_BOOLEAN,
          SU_TRUE,
          "clock.running",
          "Clock recovery is running"),
      return SU_FALSE);

  return SU_TRUE;
}

