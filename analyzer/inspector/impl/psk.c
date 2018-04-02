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

#define SU_LOG_DOMAIN "psk-inspector"

#include <sigutils/sigutils.h>
#include <sigutils/agc.h>
#include <sigutils/pll.h>
#include <sigutils/clock.h>
#include <sigutils/equalizer.h>

#include "inspector/interface.h"
#include "inspector/params.h"

#include "inspector/inspector.h"

/* Some default PSK demodulator parameters */
#define SUSCAN_PSK_INSPECTOR_DEFAULT_ROLL_OFF  .35
#define SUSCAN_PSK_INSPECTOR_DEFAULT_EQ_MU     1e-3
#define SUSCAN_PSK_INSPECTOR_DEFAULT_EQ_LENGTH 20
#define SUSCAN_PSK_INSPECTOR_MAX_MF_SPAN       1024

/*
 * Spike durations measured in symbol times
 * SUSCAN_PSK_INSPECTOR_FAST_RISE_FRAC has been doubled to reduce phase noise
 * induced by the non-linearity of the AGC
 */
#define SUSCAN_PSK_INSPECTOR_FAST_RISE_FRAC   (2 * 3.9062e-1)
#define SUSCAN_PSK_INSPECTOR_FAST_FALL_FRAC   (2 * SUSCAN_PSK_INSPECTOR_FAST_RISE_FRAC)
#define SUSCAN_PSK_INSPECTOR_SLOW_RISE_FRAC   (10 * SUSCAN_PSK_INSPECTOR_FAST_RISE_FRAC)
#define SUSCAN_PSK_INSPECTOR_SLOW_FALL_FRAC   (10 * SUSCAN_PSK_INSPECTOR_FAST_FALL_FRAC)
#define SUSCAN_PSK_INSPECTOR_HANG_MAX_FRAC    (SUSCAN_PSK_INSPECTOR_FAST_RISE_FRAC * 5)
#define SUSCAN_PSK_INSPECTOR_DELAY_LINE_FRAC  (SUSCAN_PSK_INSPECTOR_FAST_RISE_FRAC * 10)
#define SUSCAN_PSK_INSPECTOR_MAG_HISTORY_FRAC (SUSCAN_PSK_INSPECTOR_FAST_RISE_FRAC * 10)

struct suscan_psk_inspector_params {
  struct suscan_inspector_gc_params gc;
  struct suscan_inspector_fc_params fc;
  struct suscan_inspector_mf_params mf;
  struct suscan_inspector_eq_params eq;
  struct suscan_inspector_br_params br;
};

struct suscan_psk_inspector {
  struct suscan_inspector_sampling_info samp_info;
  struct suscan_psk_inspector_params req_params;
  struct suscan_psk_inspector_params cur_params;

  /* Blocks */
  su_agc_t            agc;        /* AGC, for sampler */
  su_costas_t         costas_2;   /* 2nd order Costas loop */
  su_costas_t         costas_4;   /* 4th order Costas loop */
  su_costas_t         costas_8;   /* 8th order Costas loop */
  su_iir_filt_t       mf;         /* Matched filter (Root Raised Cosine) */
  su_clock_detector_t cd;         /* Clock detector */
  su_equalizer_t      eq;         /* Equalizer */
  su_ncqo_t           lo;         /* Oscillator for manual carrier offset */
  SUCOMPLEX           phase;      /* Local oscillator phase */

  SUFLOAT   sym_phase;  /* Current sampling phase, in samples */
  SUFLOAT   sym_period; /* Symbol period */
  SUCOMPLEX sampler_prev; /* Used for interpolation */
};

SUSCOUNT
suscan_psk_inspector_mf_span(SUSCOUNT span)
{
  if (span > SUSCAN_PSK_INSPECTOR_MAX_MF_SPAN) {
    SU_WARNING(
        "Matched filter sample span too big (%d), truncating to %d\n",
        span, SUSCAN_PSK_INSPECTOR_MAX_MF_SPAN);
    span = SUSCAN_PSK_INSPECTOR_MAX_MF_SPAN;
  }

  return span;
}

SUPRIVATE void
suscan_psk_inspector_params_initialize(
    struct suscan_psk_inspector_params *params,
    const struct suscan_inspector_sampling_info *sinfo)
{
  memset(params, 0, sizeof (struct suscan_psk_inspector_params));

  params->gc.gc_ctrl    = SUSCAN_INSPECTOR_GAIN_CONTROL_AUTOMATIC;
  params->gc.gc_gain    = 1;

  params->br.br_ctrl    = SUSCAN_INSPECTOR_BAUDRATE_CONTROL_MANUAL;
  params->br.br_alpha   = SU_PREFERED_CLOCK_ALPHA;
  params->br.br_beta    = SU_PREFERED_CLOCK_BETA;

  params->fc.fc_ctrl    = SUSCAN_INSPECTOR_CARRIER_CONTROL_MANUAL;
  params->fc.fc_loopbw  = SU_NORM2ABS_FREQ(sinfo->equiv_fs, 1e-2 * sinfo->bw);
  params->mf.mf_conf    = SUSCAN_INSPECTOR_MATCHED_FILTER_BYPASS;
  params->mf.mf_rolloff = SUSCAN_PSK_INSPECTOR_DEFAULT_ROLL_OFF;

  params->eq.eq_conf    = SUSCAN_INSPECTOR_EQUALIZER_BYPASS;
  params->eq.eq_mu      = SUSCAN_PSK_INSPECTOR_DEFAULT_EQ_MU;
}

SUPRIVATE void
suscan_psk_inspector_destroy(struct suscan_psk_inspector *insp)
{
  su_iir_filt_finalize(&insp->mf);

  su_agc_finalize(&insp->agc);

  su_costas_finalize(&insp->costas_2);
  su_costas_finalize(&insp->costas_4);
  su_costas_finalize(&insp->costas_8);

  su_clock_detector_finalize(&insp->cd);

  su_equalizer_finalize(&insp->eq);

  free(insp);
}

SUPRIVATE struct suscan_psk_inspector *
suscan_psk_inspector_new(const struct suscan_inspector_sampling_info *sinfo)
{
  struct suscan_psk_inspector *new = NULL;
  struct sigutils_equalizer_params eq_params =
      sigutils_equalizer_params_INITIALIZER;
  struct su_agc_params agc_params = su_agc_params_INITIALIZER;
  SUFLOAT bw, tau;

  SU_TRYCATCH(new = calloc(1, sizeof(struct suscan_psk_inspector)), goto fail);

  new->samp_info = *sinfo;

  suscan_psk_inspector_params_initialize(&new->cur_params, sinfo);

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

  /* Initialize local oscillator */
  su_ncqo_init(&new->lo, 0);
  new->phase = 1.;

  /* Initialize AGC */
  tau = 1. / bw; /* Samples per symbol */

  agc_params.fast_rise_t = tau * SUSCAN_PSK_INSPECTOR_FAST_RISE_FRAC;
  agc_params.fast_fall_t = tau * SUSCAN_PSK_INSPECTOR_FAST_FALL_FRAC;
  agc_params.slow_rise_t = tau * SUSCAN_PSK_INSPECTOR_SLOW_RISE_FRAC;
  agc_params.slow_fall_t = tau * SUSCAN_PSK_INSPECTOR_SLOW_FALL_FRAC;
  agc_params.hang_max    = tau * SUSCAN_PSK_INSPECTOR_HANG_MAX_FRAC;

  /* TODO: Check whether these sizes are too big */
  agc_params.delay_line_size  = tau * SUSCAN_PSK_INSPECTOR_DELAY_LINE_FRAC;
  agc_params.mag_history_size = tau * SUSCAN_PSK_INSPECTOR_MAG_HISTORY_FRAC;

  SU_TRYCATCH(su_agc_init(&new->agc, &agc_params), goto fail);

  /* Initialize matched filter, with T = tau */
  SU_TRYCATCH(
      su_iir_rrc_init(
          &new->mf,
          suscan_psk_inspector_mf_span(6 * tau),
          tau,
          new->cur_params.mf.mf_rolloff),
      goto fail);

  /* Initialize PLLs */
  SU_TRYCATCH(
      su_costas_init(
          &new->costas_2,
          SU_COSTAS_KIND_BPSK,
          0         /* Frequency hint */,
          bw        /* Arm bandwidth */,
          3         /* Order */,
          SU_ABS2NORM_FREQ(sinfo->equiv_fs, new->cur_params.fc.fc_loopbw)),
      goto fail);

  SU_TRYCATCH(
      su_costas_init(
          &new->costas_4,
          SU_COSTAS_KIND_QPSK,
          0         /* Frequency hint */,
          bw        /* Arm bandwidth */,
          3         /* Order */,
          SU_ABS2NORM_FREQ(sinfo->equiv_fs, new->cur_params.fc.fc_loopbw)),
      goto fail);

  SU_TRYCATCH(
      su_costas_init(
          &new->costas_8,
          SU_COSTAS_KIND_8PSK,
          0         /* Frequency hint */,
          bw        /* Arm bandwidth */,
          3         /* Order */,
          SU_ABS2NORM_FREQ(sinfo->equiv_fs, new->cur_params.fc.fc_loopbw)),
      goto fail);

  /* Initialize equalizer */
  eq_params.mu = SUSCAN_PSK_INSPECTOR_DEFAULT_EQ_MU;
  eq_params.length = SUSCAN_PSK_INSPECTOR_DEFAULT_EQ_LENGTH;

  SU_TRYCATCH(su_equalizer_init(&new->eq, &eq_params), goto fail);

  return new;

fail:
  if (new != NULL)
    suscan_psk_inspector_destroy(new);

  return NULL;
}


/************************** API implementation *******************************/
void *
suscan_psk_inspector_open(const struct suscan_inspector_sampling_info *sinfo)
{
  return suscan_psk_inspector_new(sinfo);
}

SUBOOL
suscan_psk_inspector_get_config(void *private, suscan_config_t *config)
{
  struct suscan_psk_inspector *insp = (struct suscan_psk_inspector *) private;

  SU_TRYCATCH(
      suscan_inspector_gc_params_save(&insp->cur_params.gc, config),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_inspector_fc_params_save(&insp->cur_params.fc, config),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_inspector_mf_params_save(&insp->cur_params.mf, config),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_inspector_eq_params_save(&insp->cur_params.eq, config),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_inspector_br_params_save(&insp->cur_params.br, config),
      return SU_FALSE);

  return SU_TRUE;
}

SUBOOL
suscan_psk_inspector_parse_config(void *private, const suscan_config_t *config)
{
  struct suscan_psk_inspector *insp = (struct suscan_psk_inspector *) private;

  suscan_psk_inspector_params_initialize(&insp->req_params, &insp->samp_info);

  SU_TRYCATCH(
      suscan_inspector_gc_params_parse(&insp->req_params.gc, config),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_inspector_fc_params_parse(&insp->req_params.fc, config),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_inspector_mf_params_parse(&insp->req_params.mf, config),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_inspector_eq_params_parse(&insp->req_params.eq, config),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_inspector_br_params_parse(&insp->req_params.br, config),
      return SU_FALSE);

  return SU_TRUE;

}

/* This method is called inside the inspector mutex */
void
suscan_psk_inspector_commit_config(void *private)
{
  SUFLOAT fs;
  SUBOOL mf_changed;
  SUBOOL costas_changed;
  SUFLOAT actual_baud;
  su_costas_t costas;

  su_iir_filt_t mf = su_iir_filt_INITIALIZER;
  struct suscan_psk_inspector *insp = (struct suscan_psk_inspector *) private;

  actual_baud = insp->req_params.br.br_running
      ? insp->req_params.br.baud
      : 0;

  mf_changed =
      (insp->cur_params.br.baud != actual_baud)
      || (insp->cur_params.mf.mf_rolloff != insp->req_params.mf.mf_rolloff);

  costas_changed =
      insp->cur_params.fc.fc_loopbw != insp->req_params.fc.fc_loopbw;

  insp->cur_params = insp->req_params;

  fs = insp->samp_info.equiv_fs;

  /* Update inspector according to params */
  if (actual_baud > 0)
    insp->sym_period = 1. / SU_ABS2NORM_BAUD(fs, actual_baud);
  else
    insp->sym_period = 0;

  /* Update local oscillator frequency and phase */
  su_ncqo_set_freq(
      &insp->lo,
      SU_ABS2NORM_FREQ(fs, insp->cur_params.fc.fc_off));
  insp->phase = SU_C_EXP(I * insp->cur_params.fc.fc_phi);

  /* Update baudrate */
  su_clock_detector_set_baud(&insp->cd, SU_ABS2NORM_BAUD(fs, actual_baud));

  insp->cd.alpha = insp->cur_params.br.br_alpha;
  insp->cd.beta = insp->cur_params.br.br_beta;

  /* Update equalizer */
  insp->eq.params.mu = insp->cur_params.eq.eq_locked
      ? 0
      : insp->cur_params.eq.eq_mu;

  /* Update matched filter */
  if (mf_changed && insp->sym_period > 0) {
    if (!su_iir_rrc_init(
        &mf,
        suscan_psk_inspector_mf_span(6 * insp->sym_period),
        insp->sym_period,
        insp->cur_params.mf.mf_rolloff)) {
      SU_ERROR("No memory left to update matched filter!\n");
    } else {
      su_iir_filt_finalize(&insp->mf);
      insp->mf = mf;
    }
  }

  /* Re-center costas loops */
  if (insp->cur_params.fc.fc_ctrl == SUSCAN_INSPECTOR_CARRIER_CONTROL_MANUAL) {
    su_ncqo_set_freq(&insp->costas_2.ncqo, 0);
    su_ncqo_set_freq(&insp->costas_4.ncqo, 0);
    su_ncqo_set_freq(&insp->costas_8.ncqo, 0);
  }

  /* Update Costas loops */
  if (costas_changed) {
    SU_TRYCATCH(
        su_costas_init(
            &costas,
            SU_COSTAS_KIND_BPSK,
            0 /* Frequency hint */,
            insp->samp_info.bw,
            3 /* Order */,
            SU_ABS2NORM_FREQ(
                insp->samp_info.equiv_fs,
                insp->cur_params.fc.fc_loopbw)),
        return);
    su_costas_finalize(&insp->costas_2);
    insp->costas_2 = costas;

    SU_TRYCATCH(
        su_costas_init(
            &costas,
            SU_COSTAS_KIND_QPSK,
            0 /* Frequency hint */,
            insp->samp_info.bw,
            3 /* Order */,
            SU_ABS2NORM_FREQ(
                insp->samp_info.equiv_fs,
                insp->cur_params.fc.fc_loopbw)),
        return);
    su_costas_finalize(&insp->costas_4);
    insp->costas_4 = costas;

    SU_TRYCATCH(
        su_costas_init(
            &costas,
            SU_COSTAS_KIND_8PSK,
            0 /* Frequency hint */,
            insp->samp_info.bw,
            3 /* Order */,
            SU_ABS2NORM_FREQ(
                insp->samp_info.equiv_fs,
                insp->cur_params.fc.fc_loopbw)),
        return);
    su_costas_finalize(&insp->costas_8);
    insp->costas_8 = costas;
  }
}

SUSDIFF
suscan_psk_inspector_feed(
    void *private,
    suscan_inspector_t *insp,
    const SUCOMPLEX *x,
    SUSCOUNT count)
{
  SUSCOUNT i;
  SUSCOUNT osize = 0;
  SUFLOAT alpha;
  SUCOMPLEX det_x;
  SUCOMPLEX output;
  SUBOOL new_sample = SU_FALSE;
  SUCOMPLEX samp_phase_samples;
  struct suscan_psk_inspector *psk_insp =
      (struct suscan_psk_inspector *) private;

  samp_phase_samples = psk_insp->cur_params.br.sym_phase * psk_insp->sym_period;

  for (i = 0; i < count && suscan_inspector_sampler_buf_avail(insp) > 0; ++i) {
    /* Re-center carrier */
    det_x = x[i] * SU_C_CONJ(su_ncqo_read(&psk_insp->lo)) * psk_insp->phase;

    /* Perform gain control */
    switch (psk_insp->cur_params.gc.gc_ctrl) {
      case SUSCAN_INSPECTOR_GAIN_CONTROL_MANUAL:
        det_x *= 2 * psk_insp->cur_params.gc.gc_gain;
        break;

      case SUSCAN_INSPECTOR_GAIN_CONTROL_AUTOMATIC:
        det_x  = 2 * su_agc_feed(&psk_insp->agc, det_x);
        break;
    }

    /* Perform frequency correction */
    switch (psk_insp->cur_params.fc.fc_ctrl) {
      case SUSCAN_INSPECTOR_CARRIER_CONTROL_MANUAL:
        /* No-op */
        break;

      case SUSCAN_INSPECTOR_CARRIER_CONTROL_COSTAS_2:
        su_costas_feed(&psk_insp->costas_2, det_x);
        det_x = psk_insp->costas_2.y;
        break;

      case SUSCAN_INSPECTOR_CARRIER_CONTROL_COSTAS_4:
        su_costas_feed(&psk_insp->costas_4, det_x);
        det_x = psk_insp->costas_4.y;
        break;

      case SUSCAN_INSPECTOR_CARRIER_CONTROL_COSTAS_8:
        su_costas_feed(&psk_insp->costas_8, det_x);
        det_x = psk_insp->costas_8.y;
        break;
    }

    /* Add matched filter, if enabled */
    if (psk_insp->cur_params.mf.mf_conf
        == SUSCAN_INSPECTOR_MATCHED_FILTER_MANUAL)
      det_x = su_iir_filt_feed(&psk_insp->mf, det_x);

    /* Check if channel sampler is enabled */
    if (psk_insp->cur_params.br.br_ctrl
        == SUSCAN_INSPECTOR_BAUDRATE_CONTROL_MANUAL) {
      if (psk_insp->sym_period >= 1.) {
        psk_insp->sym_phase += 1.;
        if (psk_insp->sym_phase >= psk_insp->sym_period)
          psk_insp->sym_phase -= psk_insp->sym_period;

        new_sample =
            (SUBOOL) (SU_FLOOR(psk_insp->sym_phase - samp_phase_samples) == 0);

        /* Interpolate with previos sample for improved accuracy */
        if (new_sample) {
          alpha = psk_insp->sym_phase - SU_FLOOR(psk_insp->sym_phase);
          output = ((1 - alpha) * psk_insp->sampler_prev + alpha * det_x);
        }
      }

      /* Keep last sample for interpolation */
      psk_insp->sampler_prev = det_x;
    } else {
      /* Automatic baudrate control enabled */
      su_clock_detector_feed(&psk_insp->cd, det_x);

      new_sample = su_clock_detector_read(&psk_insp->cd, &det_x, 1) == 1;
      if (new_sample)
        output = det_x;
    }

    /* Apply channel equalizer, if enabled */
    if (new_sample) {
      if (psk_insp->cur_params.eq.eq_conf == SUSCAN_INSPECTOR_EQUALIZER_CMA) {
        suscan_inspector_lock(insp);
        output = su_equalizer_feed(&psk_insp->eq, output);
        suscan_inspector_unlock(insp);
      }

      /* Reduce amplitude so it fits in the constellation window */
      suscan_inspector_push_sample(insp, output * .75);
      new_sample = SU_FALSE;
    }
  }

  return i;
}

void
suscan_psk_inspector_close(void *private)
{
  suscan_psk_inspector_destroy((struct suscan_psk_inspector *) private);
}

SUPRIVATE struct suscan_inspector_interface iface = {
    .name = "psk",
    .desc = "PSK inspector",
    .open = suscan_psk_inspector_open,
    .get_config = suscan_psk_inspector_get_config,
    .parse_config = suscan_psk_inspector_parse_config,
    .commit_config = suscan_psk_inspector_commit_config,
    .feed = suscan_psk_inspector_feed,
    .close = suscan_psk_inspector_close
};

SUBOOL
suscan_psk_inspector_register(void)
{
  SU_TRYCATCH(
      iface.cfgdesc = suscan_config_desc_new(),
      return SU_FALSE);

  /* Add all configuration parameters */
  SU_TRYCATCH(suscan_config_desc_add_gc_params(iface.cfgdesc), return SU_FALSE);
  SU_TRYCATCH(suscan_config_desc_add_fc_params(iface.cfgdesc), return SU_FALSE);
  SU_TRYCATCH(suscan_config_desc_add_mf_params(iface.cfgdesc), return SU_FALSE);
  SU_TRYCATCH(suscan_config_desc_add_eq_params(iface.cfgdesc), return SU_FALSE);
  SU_TRYCATCH(suscan_config_desc_add_br_params(iface.cfgdesc), return SU_FALSE);

  /* Add some estimators */
  SU_TRYCATCH(
      suscan_inspector_interface_add_estimator(&iface, "baud-fac"),
      return SU_FALSE);

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
      suscan_inspector_interface_add_spectsrc(&iface, "exp_2"),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_inspector_interface_add_spectsrc(&iface, "exp_4"),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_inspector_interface_add_spectsrc(&iface, "exp_8"),
      return SU_FALSE);

  /* Register inspector interface */
  SU_TRYCATCH(suscan_inspector_interface_register(&iface), return SU_FALSE);

  return SU_TRUE;
}
