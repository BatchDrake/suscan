/*

  Copyright (C) 2019 Gonzalo José Carracedo Carballal

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

#define SU_LOG_DOMAIN "audio-inspector"

#include <sigutils/sigutils.h>
#include <sigutils/agc.h>
#include <sigutils/pll.h>
#include <sigutils/sampling.h>
#include <sigutils/iir.h>

#include "inspector/interface.h"
#include "inspector/params.h"
#include "inspector/inspector.h"

#include <string.h>

#define SUSCAN_AUDIO_INSPECTOR_SAMPLE_RATE 44100

struct suscan_audio_inspector_params {
  struct suscan_inspector_gc_params gc;
  struct suscan_inspector_audio_params audio;
};

/*
 * Spike durations measured in symbol times
 * SUSCAN_AUDIO_INSPECTOR_FAST_RISE_FRAC has been doubled to reduce phase noise
 * induced by the non-linearity of the AGC
 */
#define SUSCAN_AUDIO_INSPECTOR_FAST_RISE_FRAC   (2 * 3.9062e-1)
#define SUSCAN_AUDIO_INSPECTOR_FAST_FALL_FRAC   (2 * SUSCAN_AUDIO_INSPECTOR_FAST_RISE_FRAC)
#define SUSCAN_AUDIO_INSPECTOR_SLOW_RISE_FRAC   (10 * SUSCAN_AUDIO_INSPECTOR_FAST_RISE_FRAC)
#define SUSCAN_AUDIO_INSPECTOR_SLOW_FALL_FRAC   (10 * SUSCAN_AUDIO_INSPECTOR_FAST_FALL_FRAC)
#define SUSCAN_AUDIO_INSPECTOR_HANG_MAX_FRAC    (SUSCAN_AUDIO_INSPECTOR_FAST_RISE_FRAC * 5)
#define SUSCAN_AUDIO_INSPECTOR_DELAY_LINE_FRAC  (SUSCAN_AUDIO_INSPECTOR_FAST_RISE_FRAC * 10)
#define SUSCAN_AUDIO_INSPECTOR_MAG_HISTORY_FRAC (SUSCAN_AUDIO_INSPECTOR_FAST_RISE_FRAC * 10)

struct suscan_audio_inspector {
  struct suscan_inspector_sampling_info samp_info;
  struct suscan_audio_inspector_params req_params;
  struct suscan_audio_inspector_params cur_params;

  /* Blocks */
  su_agc_t  agc;          /* AGC, for AM-like modulations */
  su_iir_filt_t filt;     /* Input filter */
  su_pll_t pll;           /* Carrier tracking PLL */
  SUCOMPLEX last;         /* Last processed sample (for quad demod) */
  SUFLOAT   sym_phase;    /* Current sampling phase, in samples */
  SUFLOAT   sym_period;   /* Symbol period */
  SUCOMPLEX sampler_prev; /* Used for interpolation */
  SUBOOL    filt_init;    /* Flag to save whether the filter was initialized */
};

SUPRIVATE void
suscan_audio_inspector_params_initialize(
    struct suscan_audio_inspector_params *params,
    const struct suscan_inspector_sampling_info *sinfo)
{
  memset(params, 0, sizeof(struct suscan_audio_inspector_params));

  params->gc.gc_ctrl = SUSCAN_INSPECTOR_GAIN_CONTROL_AUTOMATIC;
  params->gc.gc_gain = 1;

  params->audio.sample_rate = SUSCAN_AUDIO_INSPECTOR_SAMPLE_RATE;
  params->audio.demod       = SUSCAN_INSPECTOR_AUDIO_DEMOD_DISABLED;
  params->audio.cutoff      = SUSCAN_AUDIO_INSPECTOR_SAMPLE_RATE / 2;
}

SUPRIVATE void
suscan_audio_inspector_destroy(struct suscan_audio_inspector *insp)
{
  su_iir_filt_finalize(&insp->filt);

  su_agc_finalize(&insp->agc);
}

SUPRIVATE struct suscan_audio_inspector *
suscan_audio_inspector_new(const struct suscan_inspector_sampling_info *sinfo)
{
  struct suscan_audio_inspector *new = NULL;
  struct su_agc_params agc_params = su_agc_params_INITIALIZER;
  SUFLOAT tau, bw;

  SU_TRYCATCH(
      new = calloc(1, sizeof(struct suscan_audio_inspector)),
      goto fail);


  new->samp_info = *sinfo;

  suscan_audio_inspector_params_initialize(&new->cur_params, sinfo);

  bw = sinfo->bw;
  tau = 1. / bw;

  agc_params.fast_rise_t = tau * SUSCAN_AUDIO_INSPECTOR_FAST_RISE_FRAC;
  agc_params.fast_fall_t = tau * SUSCAN_AUDIO_INSPECTOR_FAST_FALL_FRAC;
  agc_params.slow_rise_t = tau * SUSCAN_AUDIO_INSPECTOR_SLOW_RISE_FRAC;
  agc_params.slow_fall_t = tau * SUSCAN_AUDIO_INSPECTOR_SLOW_FALL_FRAC;
  agc_params.hang_max    = tau * SUSCAN_AUDIO_INSPECTOR_HANG_MAX_FRAC;

  /* TODO: Check whether these sizes are too big */
  agc_params.delay_line_size  = tau * SUSCAN_AUDIO_INSPECTOR_DELAY_LINE_FRAC;
  agc_params.mag_history_size = tau * SUSCAN_AUDIO_INSPECTOR_MAG_HISTORY_FRAC;

  SU_TRYCATCH(su_agc_init(&new->agc, &agc_params), goto fail);

  /* PLL init, always one tenth of the bandwidth */
  su_pll_init(&new->pll, 0, .005f * bw);

  return new;

fail:
  if (new != NULL)
    suscan_audio_inspector_destroy(new);

  return new;
}

/************************** API implementation *******************************/
void *
suscan_audio_inspector_open(const struct suscan_inspector_sampling_info *s)
{
  return suscan_audio_inspector_new(s);
}

SUBOOL
suscan_audio_inspector_get_config(void *private, suscan_config_t *config)
{
  struct suscan_audio_inspector *insp =
      (struct suscan_audio_inspector *) private;

  SU_TRYCATCH(
      suscan_inspector_gc_params_save(&insp->cur_params.gc, config),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_inspector_audio_params_save(&insp->cur_params.audio, config),
      return SU_FALSE);

  return SU_TRUE;
}

SUBOOL
suscan_audio_inspector_parse_config(void *private, const suscan_config_t *config)
{
  struct suscan_audio_inspector *insp = (struct suscan_audio_inspector *) private;


  SU_TRYCATCH(
      suscan_inspector_gc_params_parse(&insp->req_params.gc, config),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_inspector_audio_params_parse(&insp->req_params.audio, config),
      return SU_FALSE);

  return SU_TRUE;
}

/* Called inside inspector mutex */
void
suscan_audio_inspector_commit_config(void *private)
{
  struct suscan_audio_inspector *insp =
      (struct suscan_audio_inspector *) private;
  su_iir_filt_t filt;
  SUBOOL filt_initialized;
  SUFLOAT fs = insp->samp_info.equiv_fs;

  filt_initialized = su_iir_bwlpf_init(
      &filt,
      5,
      SU_ABS2NORM_FREQ(fs, insp->req_params.audio.cutoff));
  if (!filt_initialized) {
    SU_ERROR("No memory left to initialize demodulator filter");
  } else {
    su_iir_filt_finalize(&insp->filt);
    insp->filt = filt;
    insp->filt_init = SU_TRUE;
  }

  /* Set sampling info */
  if (insp->req_params.audio.sample_rate > 0)
    insp->sym_period = 1. / SU_ABS2NORM_BAUD(
        fs,
        insp->req_params.audio.sample_rate);
  else
    insp->sym_period = 0.;

  insp->cur_params = insp->req_params;
}

SUSDIFF
suscan_audio_inspector_feed(
    void *private,
    suscan_inspector_t *insp,
    const SUCOMPLEX *x,
    SUSCOUNT count)
{
  SUCOMPLEX last, det_x;
  SUSCOUNT i;
  SUFLOAT alpha, output;
  struct suscan_audio_inspector *self =
      (struct suscan_audio_inspector *) private;

  if (self->cur_params.audio.demod == SUSCAN_INSPECTOR_AUDIO_DEMOD_DISABLED)
    return count;

  last = self->last;

  for (i = 0; i < count && suscan_inspector_sampler_buf_avail(insp) > 0; ++i) {
    det_x = x[i];

    /* Perform gain control */
    switch (self->cur_params.gc.gc_ctrl) {
      case SUSCAN_INSPECTOR_GAIN_CONTROL_MANUAL:
        det_x = 2 * self->cur_params.gc.gc_gain * det_x;
        break;

      case SUSCAN_INSPECTOR_GAIN_CONTROL_AUTOMATIC:
        det_x  = 2 * su_agc_feed(&self->agc, det_x);
        break;
    }

    switch (self->cur_params.audio.demod) {
      case SUSCAN_INSPECTOR_AUDIO_DEMOD_FM:
        output = SU_C_ARG(det_x * SU_C_CONJ(last)) / M_PI;
        last = det_x;
        break;

      case SUSCAN_INSPECTOR_AUDIO_DEMOD_AM:
        output = su_pll_track(&self->pll, det_x);
    }

    if (self->filt_init)
      output = su_iir_filt_feed(&self->filt, output);

    output *= self->cur_params.audio.volume;

    if (self->sym_period >= 1.) {
      self->sym_phase += 1.;
      if (self->sym_phase >= self->sym_period)
        self->sym_phase -= self->sym_period;

      /* Interpolate with previous sample for improved accuracy */
      if ((SUBOOL) (SU_FLOOR(self->sym_phase) == 0)) {
        alpha = self->sym_phase - SU_FLOOR(self->sym_phase);
        output = ((1 - alpha) * self->sampler_prev + alpha * output);
        suscan_inspector_push_sample(insp, output * .75);
      }
    }

    self->sampler_prev = output;
  }

  self->last = last;

  return i;
}

void
suscan_audio_inspector_close(void *private)
{
  suscan_audio_inspector_destroy((struct suscan_audio_inspector *) private);
}

SUPRIVATE struct suscan_inspector_interface iface = {
    .name = "audio",
    .desc = "Audio inspector",
    .open = suscan_audio_inspector_open,
    .get_config = suscan_audio_inspector_get_config,
    .parse_config = suscan_audio_inspector_parse_config,
    .commit_config = suscan_audio_inspector_commit_config,
    .feed = suscan_audio_inspector_feed,
    .close = suscan_audio_inspector_close
};

SUBOOL
suscan_audio_inspector_register(void)
{
  SU_TRYCATCH(
      iface.cfgdesc = suscan_config_desc_new(),
      return SU_FALSE);

  /* Add all configuration parameters */
  SU_TRYCATCH(suscan_config_desc_add_gc_params(iface.cfgdesc), return SU_FALSE);
  SU_TRYCATCH(suscan_config_desc_add_audio_params(iface.cfgdesc), return SU_FALSE);

  /* Register inspector interface */
  SU_TRYCATCH(suscan_inspector_interface_register(&iface), return SU_FALSE);

  return SU_TRUE;
}
