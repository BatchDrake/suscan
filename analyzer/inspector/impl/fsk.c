/*

  Copyright (C) 2018 Gonzalo José Carracedo Carballal

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

#include <string.h>

#define SU_LOG_DOMAIN "fsk-inspector"

#include <sigutils/sigutils.h>
#include <sigutils/agc.h>
#include <sigutils/pll.h>
#include <sigutils/clock.h>
#include <sigutils/equalizer.h>

#include "inspector/interface.h"
#include "inspector/params.h"

#include "inspector/inspector.h"

/* Some default FSK demodulator parameters */
#define SUSCAN_FSK_INSPECTOR_DEFAULT_ROLL_OFF  .35
#define SUSCAN_FSK_INSPECTOR_DEFAULT_EQ_MU     1e-3
#define SUSCAN_FSK_INSPECTOR_DEFAULT_EQ_LENGTH 20
#define SUSCAN_FSK_INSPECTOR_MAX_MF_SPAN       1024

/*
 * Spike durations measured in symbol times
 * SUSCAN_FSK_INSPECTOR_FAST_RISE_FRAC has been doubled to reduce phase noise
 * induced by the non-linearity of the AGC
 */
#define SUSCAN_FSK_INSPECTOR_FAST_RISE_FRAC   (2 * 3.9062e-1)
#define SUSCAN_FSK_INSPECTOR_FAST_FALL_FRAC   (2 * SUSCAN_FSK_INSPECTOR_FAST_RISE_FRAC)
#define SUSCAN_FSK_INSPECTOR_SLOW_RISE_FRAC   (10 * SUSCAN_FSK_INSPECTOR_FAST_RISE_FRAC)
#define SUSCAN_FSK_INSPECTOR_SLOW_FALL_FRAC   (10 * SUSCAN_FSK_INSPECTOR_FAST_FALL_FRAC)
#define SUSCAN_FSK_INSPECTOR_HANG_MAX_FRAC    (SUSCAN_FSK_INSPECTOR_FAST_RISE_FRAC * 5)
#define SUSCAN_FSK_INSPECTOR_DELAY_LINE_FRAC  (SUSCAN_FSK_INSPECTOR_FAST_RISE_FRAC * 10)
#define SUSCAN_FSK_INSPECTOR_MAG_HISTORY_FRAC (SUSCAN_FSK_INSPECTOR_FAST_RISE_FRAC * 10)

struct suscan_fsk_inspector_params {
  struct suscan_inspector_gc_params gc;
  struct suscan_inspector_mf_params mf;
  struct suscan_inspector_br_params br;
  struct suscan_inspector_fsk_params fsk;
};

struct suscan_fsk_inspector {
  struct suscan_inspector_sampling_info samp_info;
  struct suscan_fsk_inspector_params req_params;
  struct suscan_fsk_inspector_params cur_params;

  /* Blocks */
  su_agc_t            agc;        /* AGC, for sampler */
  su_iir_filt_t       mf;         /* Matched filter (Root Raised Cosine) */
  su_clock_detector_t cd;         /* Clock detector */
  su_sampler_t        sampler;    /* Sampler */
  su_ncqo_t           lo;         /* Oscillator for manual carrier offset */
  SUCOMPLEX           phase;      /* Local oscillator phase */
  SUCOMPLEX           last;       /* Last processed sample */
};

SUSCOUNT
suscan_fsk_inspector_mf_span(SUSCOUNT span)
{
  if (span > SUSCAN_FSK_INSPECTOR_MAX_MF_SPAN) {
    SU_WARNING(
        "Matched filter sample span too big (%d), truncating to %d\n",
        span, SUSCAN_FSK_INSPECTOR_MAX_MF_SPAN);
    span = SUSCAN_FSK_INSPECTOR_MAX_MF_SPAN;
  }

  return span;
}

SUPRIVATE void
suscan_fsk_inspector_params_initialize(
    struct suscan_fsk_inspector_params *params)
{
  memset(params, 0, sizeof (struct suscan_fsk_inspector_params));

  params->gc.gc_ctrl = SUSCAN_INSPECTOR_GAIN_CONTROL_AUTOMATIC;
  params->gc.gc_gain = 1;

  params->br.br_ctrl  = SUSCAN_INSPECTOR_BAUDRATE_CONTROL_MANUAL;
  params->br.br_alpha = SU_PREFERED_CLOCK_ALPHA;
  params->br.br_beta  = SU_PREFERED_CLOCK_BETA;

  params->mf.mf_conf  = SUSCAN_INSPECTOR_MATCHED_FILTER_BYPASS;
  params->mf.mf_rolloff = SUSCAN_FSK_INSPECTOR_DEFAULT_ROLL_OFF;

  params->fsk.bits_per_tone = 1;
  params->fsk.quad_demod    = SU_FALSE;
  params->fsk.phase         = PI;
}

SUPRIVATE void
suscan_fsk_inspector_destroy(struct suscan_fsk_inspector *insp)
{
  su_iir_filt_finalize(&insp->mf);

  su_agc_finalize(&insp->agc);

  su_clock_detector_finalize(&insp->cd);

  su_sampler_finalize(&insp->sampler);

  free(insp);
}

SUPRIVATE struct suscan_fsk_inspector *
suscan_fsk_inspector_new(const struct suscan_inspector_sampling_info *sinfo)
{
  struct suscan_fsk_inspector *new = NULL;
  struct sigutils_equalizer_params eq_params =
      sigutils_equalizer_params_INITIALIZER;
  struct su_agc_params agc_params = su_agc_params_INITIALIZER;
  SUFLOAT bw, tau;

  SU_TRYCATCH(new = calloc(1, sizeof(struct suscan_fsk_inspector)), goto fail);

  new->samp_info = *sinfo;

  suscan_fsk_inspector_params_initialize(&new->cur_params);

  bw = sinfo->bw;
  tau = 1. /  bw; /* Approximate samples per symbol */

  /* Create clock detector */
  SU_TRYCATCH(
      su_clock_detector_init(
          &new->cd,
          1., /* Loop gain */
          .5 * bw, /* Baudrate hint */
          32  /* Buffer size */),
      goto fail);

  /* Fixed baudrate sampler */
  SU_TRYCATCH(su_sampler_init(&new->sampler, tau), goto fail);

  /* Initialize local oscillator */
  su_ncqo_init(&new->lo, 0);
  new->phase = SU_C_EXP(I * new->cur_params.fsk.phase);

  /* Initialize AGC */
  tau = 1. / bw; /* Samples per symbol */

  agc_params.fast_rise_t = tau * SUSCAN_FSK_INSPECTOR_FAST_RISE_FRAC;
  agc_params.fast_fall_t = tau * SUSCAN_FSK_INSPECTOR_FAST_FALL_FRAC;
  agc_params.slow_rise_t = tau * SUSCAN_FSK_INSPECTOR_SLOW_RISE_FRAC;
  agc_params.slow_fall_t = tau * SUSCAN_FSK_INSPECTOR_SLOW_FALL_FRAC;
  agc_params.hang_max    = tau * SUSCAN_FSK_INSPECTOR_HANG_MAX_FRAC;

  /* TODO: Check whether these sizes are too big */
  agc_params.delay_line_size  = tau * SUSCAN_FSK_INSPECTOR_DELAY_LINE_FRAC;
  agc_params.mag_history_size = tau * SUSCAN_FSK_INSPECTOR_MAG_HISTORY_FRAC;

  SU_TRYCATCH(su_agc_init(&new->agc, &agc_params), goto fail);

  /* Initialize matched filter, with T = tau */
  SU_TRYCATCH(
      su_iir_rrc_init(
          &new->mf,
          suscan_fsk_inspector_mf_span(6 * tau),
          tau,
          new->cur_params.mf.mf_rolloff),
      goto fail);


  /* Initialize equalizer */
  eq_params.mu = SUSCAN_FSK_INSPECTOR_DEFAULT_EQ_MU;
  eq_params.length = SUSCAN_FSK_INSPECTOR_DEFAULT_EQ_LENGTH;

  return new;

fail:
  if (new != NULL)
    suscan_fsk_inspector_destroy(new);

  return NULL;
}


/************************** API implementation *******************************/
void *
suscan_fsk_inspector_open(const struct suscan_inspector_sampling_info *sinfo)
{
  return suscan_fsk_inspector_new(sinfo);
}

SUBOOL
suscan_fsk_inspector_get_config(void *private, suscan_config_t *config)
{
  struct suscan_fsk_inspector *insp = (struct suscan_fsk_inspector *) private;

  SU_TRYCATCH(
      suscan_inspector_gc_params_save(&insp->cur_params.gc, config),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_inspector_mf_params_save(&insp->cur_params.mf, config),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_inspector_br_params_save(&insp->cur_params.br, config),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_inspector_fsk_params_save(&insp->cur_params.fsk, config),
      return SU_FALSE);

  return SU_TRUE;
}

SUBOOL
suscan_fsk_inspector_parse_config(void *private, const suscan_config_t *config)
{
  struct suscan_fsk_inspector *insp = (struct suscan_fsk_inspector *) private;

  suscan_fsk_inspector_params_initialize(&insp->req_params);

  SU_TRYCATCH(
      suscan_inspector_gc_params_parse(&insp->req_params.gc, config),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_inspector_mf_params_parse(&insp->req_params.mf, config),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_inspector_br_params_parse(&insp->req_params.br, config),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_inspector_fsk_params_parse(&insp->req_params.fsk, config),
      return SU_FALSE);

  return SU_TRUE;

}

/* This method is called inside the inspector mutex */
void
suscan_fsk_inspector_commit_config(void *private)
{
  SUFLOAT fs;
  SUBOOL mf_changed;
  SUFLOAT actual_baud;
  SUFLOAT sym_period;
  su_iir_filt_t mf = su_iir_filt_INITIALIZER;
  struct suscan_fsk_inspector *insp = (struct suscan_fsk_inspector *) private;

  actual_baud = insp->req_params.br.br_running
      ? insp->req_params.br.baud
      : 0;

  mf_changed =
      (insp->cur_params.br.baud != actual_baud)
      || (insp->cur_params.mf.mf_rolloff != insp->req_params.mf.mf_rolloff);

  insp->cur_params = insp->req_params;

  fs = insp->samp_info.equiv_fs;

  /* Update baudrate */
  su_clock_detector_set_baud(&insp->cd, SU_ABS2NORM_BAUD(fs, actual_baud));
  su_sampler_set_rate(&insp->sampler, SU_ABS2NORM_BAUD(fs, actual_baud));
  su_sampler_set_phase_addend(&insp->sampler, insp->cur_params.br.sym_phase);
  sym_period = su_sampler_get_period(&insp->sampler);

  insp->cd.alpha = insp->cur_params.br.br_alpha;
  insp->cd.beta = insp->cur_params.br.br_beta;

  /* Update output phase */
  insp->phase = SU_C_EXP(I * insp->cur_params.fsk.phase);
  
  /* Update matched filter */
  if (mf_changed && sym_period > 0) {
    if (!su_iir_rrc_init(
        &mf,
        suscan_fsk_inspector_mf_span(6 * sym_period),
        sym_period,
        insp->cur_params.mf.mf_rolloff)) {
      SU_ERROR("No memory left to update matched filter!\n");
    } else {
      su_iir_filt_finalize(&insp->mf);
      insp->mf = mf;
    }
  }
}

SUSDIFF
suscan_fsk_inspector_feed(
    void *private,
    suscan_inspector_t *insp,
    const SUCOMPLEX *x,
    SUSCOUNT count)
{
  SUSCOUNT i;
  SUSCOUNT osize = 0;
  SUFLOAT alpha;
  SUCOMPLEX const_gain;
  SUCOMPLEX det_x;
  SUCOMPLEX output;
  SUBOOL new_sample = SU_FALSE;
  SUCOMPLEX last = 0;

  struct suscan_fsk_inspector *fsk_insp =
      (struct suscan_fsk_inspector *) private;

  last = fsk_insp->last;

  for (i = 0; i < count && suscan_inspector_sampler_buf_avail(insp) > 0; ++i) {
    /* Re-center carrier */
    det_x = x[i] * SU_C_CONJ(su_ncqo_read(&fsk_insp->lo));

    /* Perform gain control */
    switch (fsk_insp->cur_params.gc.gc_ctrl) {
      case SUSCAN_INSPECTOR_GAIN_CONTROL_MANUAL:
        const_gain = 2 * fsk_insp->cur_params.gc.gc_gain * det_x;
        break;

      case SUSCAN_INSPECTOR_GAIN_CONTROL_AUTOMATIC:
        const_gain = 2 * su_agc_feed(&fsk_insp->agc, det_x);
        break;
    }

    /*
     * We are actually encoding frequency information in the phase. This
     * is intentional, as the UI quantizes the argument of each sample.
     */
    if (fsk_insp->cur_params.fsk.quad_demod)
      det_x = const_gain * SU_C_CONJ(last);
    else
      det_x = (const_gain * SU_C_CONJ(last)) /
      (.5 * (const_gain * SU_C_CONJ(const_gain) + last * SU_C_CONJ(last)) + 1e-8);

    last = const_gain;

    /* Add matched filter, if enabled */
    if (fsk_insp->cur_params.mf.mf_conf == SUSCAN_INSPECTOR_MATCHED_FILTER_MANUAL)
      det_x = su_iir_filt_feed(&fsk_insp->mf, det_x);

    /* Check if channel sampler is enabled */
    if (fsk_insp->cur_params.br.br_ctrl == SUSCAN_INSPECTOR_BAUDRATE_CONTROL_MANUAL) {
      output = det_x;
      new_sample = su_sampler_feed(&fsk_insp->sampler, &output);
    } else {
      /* Automatic baudrate control enabled */
      su_clock_detector_feed(&fsk_insp->cd, det_x);
      new_sample = su_clock_detector_read(&fsk_insp->cd, &output, 1) == 1;
    }

    if (new_sample)
      suscan_inspector_push_sample(insp, output * .75 * fsk_insp->phase);
  }

  fsk_insp->last = last;

  return i;
}

void
suscan_fsk_inspector_close(void *private)
{
  suscan_fsk_inspector_destroy((struct suscan_fsk_inspector *) private);
}

SUPRIVATE struct suscan_inspector_interface iface = {
    .name = "fsk",
    .desc = "FSK inspector",
    .open = suscan_fsk_inspector_open,
    .get_config = suscan_fsk_inspector_get_config,
    .parse_config = suscan_fsk_inspector_parse_config,
    .commit_config = suscan_fsk_inspector_commit_config,
    .feed = suscan_fsk_inspector_feed,
    .close = suscan_fsk_inspector_close
};

SUBOOL
suscan_fsk_inspector_register(void)
{
  SU_TRYCATCH(
      iface.cfgdesc = suscan_config_desc_new(),
      return SU_FALSE);

  /* Add all configuration parameters */
  SU_TRYCATCH(suscan_config_desc_add_gc_params(iface.cfgdesc), return SU_FALSE);
  SU_TRYCATCH(suscan_config_desc_add_fsk_params(iface.cfgdesc), return SU_FALSE);
  SU_TRYCATCH(suscan_config_desc_add_mf_params(iface.cfgdesc), return SU_FALSE);
  SU_TRYCATCH(suscan_config_desc_add_br_params(iface.cfgdesc), return SU_FALSE);

  /* Add estimator */
  SU_TRYCATCH(
      suscan_inspector_interface_add_estimator(&iface, "baud-nonlinear"),
      return SU_FALSE);

  /* Add applicable spectrum sources */
  SU_TRYCATCH(
      suscan_inspector_interface_add_spectsrc(&iface, "psd"),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_inspector_interface_add_spectsrc(&iface, "cyclo"),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_inspector_interface_add_spectsrc(&iface, "fmcyclo"),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_inspector_interface_add_spectsrc(&iface, "fmspect"),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_inspector_interface_add_spectsrc(&iface, "timediff"),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_inspector_interface_add_spectsrc(&iface, "abstimediff"),
      return SU_FALSE);

  /* Register inspector interface */
  SU_TRYCATCH(suscan_inspector_interface_register(&iface), return SU_FALSE);

  return SU_TRUE;
}
