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
#include <sigutils/clock.h>

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
#define SUSCAN_AUDIO_INSPECTOR_FAST_RISE_FRAC   (100 * 3.9062e-1)
#define SUSCAN_AUDIO_INSPECTOR_FAST_FALL_FRAC   (2 * SUSCAN_AUDIO_INSPECTOR_FAST_RISE_FRAC)
#define SUSCAN_AUDIO_INSPECTOR_SLOW_RISE_FRAC   (10 * SUSCAN_AUDIO_INSPECTOR_FAST_RISE_FRAC)
#define SUSCAN_AUDIO_INSPECTOR_SLOW_FALL_FRAC   (10 * SUSCAN_AUDIO_INSPECTOR_FAST_FALL_FRAC)
#define SUSCAN_AUDIO_INSPECTOR_HANG_MAX_FRAC    (SUSCAN_AUDIO_INSPECTOR_FAST_RISE_FRAC * 5)
#define SUSCAN_AUDIO_INSPECTOR_DELAY_LINE_FRAC  (SUSCAN_AUDIO_INSPECTOR_FAST_RISE_FRAC * 10)
#define SUSCAN_AUDIO_INSPECTOR_MAG_HISTORY_FRAC (SUSCAN_AUDIO_INSPECTOR_FAST_RISE_FRAC * 10)

#define SUSCAN_AUDIO_INSPECTOR_BRICKWALL_LEN      200
#define SUSCAN_AUDIO_AM_LPF_SECONDS               .1
#define SUSCAN_AUDIO_AM_ATTENUATION               .25
#define SUSCAN_AUDIO_AM_CARRIER_AVERAGING_SECONDS .2

struct suscan_audio_inspector {
  struct suscan_inspector_sampling_info samp_info;
  struct suscan_audio_inspector_params req_params;
  struct suscan_audio_inspector_params cur_params;

  /* Blocks */
  su_agc_t  agc;          /* AGC, for AM-like modulations */
  su_iir_filt_t filt;     /* Input filter */
  su_pll_t pll;           /* Carrier tracking PLL */
  su_ncqo_t lo;           /* Oscillator */
  su_sampler_t sampler;   /* Fixed rate sampler */
  SUFLOAT beta;          /* Coefficient for single pole IIR filter */
  SUCOMPLEX last;         /* Last processed sample (for quad demod) */
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

  su_pll_finalize(&insp->pll);

  su_agc_finalize(&insp->agc);

  su_sampler_finalize(&insp->sampler);

  free(insp);
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

  /* PLL init, this is an experimental optimum that works rather well for AM */
  su_pll_init(&new->pll, 0, .005f * bw);

  /* Filter init */
  su_iir_bwlpf_init(
      &new->filt,
      5,
      SU_ABS2NORM_FREQ(sinfo->equiv_fs, new->cur_params.audio.cutoff));

  /* NCQO init, used to sideband adjustment */
  su_ncqo_init(&new->lo, .5 * bw);

  /* One second time constant, used to remove AM carrier */
  new->beta = 1 - SU_EXP(
      -1.f / (SUSCAN_AUDIO_AM_CARRIER_AVERAGING_SECONDS * sinfo->equiv_fs));

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
suscan_audio_inspector_new_bandwidth(void *private, SUFREQ bw)
{
  struct suscan_audio_inspector *insp =
        (struct suscan_audio_inspector *) private;
  SUFLOAT fs = insp->samp_info.equiv_fs;

  /* Initialize oscillator */
  su_ncqo_set_freq(&insp->lo, SU_ABS2NORM_FREQ(fs, .5 * bw));
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

  insp->last  = 0;

  if (insp->req_params.audio.demod != SUSCAN_INSPECTOR_AUDIO_DEMOD_DISABLED) {
    switch (insp->req_params.audio.demod)
    {
      case SUSCAN_INSPECTOR_AUDIO_DEMOD_FM:
        /*
         * FM transmissions are rather wide (up to 15 kHz), and pilot tones
         * are at around 19 kHz. We prefer to attenuate the pilot tone instead
         * of providing high stability at lower cutoff frequencies.
         */
        filt_initialized = su_iir_bwlpf_init(
            &filt,
            5,
            SU_ABS2NORM_FREQ(fs, insp->req_params.audio.cutoff));
        break;

      case SUSCAN_INSPECTOR_AUDIO_DEMOD_AM:
        /*
         * AM transmissions are around 12 kHz (6 per sideband). In this case,
         * it is okay to provide a filter with lower Q but stable at lower
         * cutoff frequencies.
         */
        filt_initialized = su_iir_bwlpf_init(
            &filt,
            3,
            SU_ABS2NORM_FREQ(fs, insp->req_params.audio.cutoff));
        break;

      case SUSCAN_INSPECTOR_AUDIO_DEMOD_LSB:
      case SUSCAN_INSPECTOR_AUDIO_DEMOD_USB:
        /*
         * SSB transmissions are usually very narrow, and require great
         * selectivity, even at low cutoffs. We sacrifice CPU in order
         * to attain this.
         */
        filt_initialized = su_iir_brickwall_lp_init(
            &filt,
            SUSCAN_AUDIO_INSPECTOR_BRICKWALL_LEN,
            SU_ABS2NORM_FREQ(fs, insp->req_params.audio.cutoff));
        break;

      default:
        filt_initialized = SU_FALSE;
        break;
    }

    if (!filt_initialized) {
      SU_ERROR("No memory left to initialize audio filter");
    } else {
      su_iir_filt_finalize(&insp->filt);
      insp->filt = filt;
    }
  }

  /* Set sampling info */
  if (insp->req_params.audio.sample_rate > 0)
    su_sampler_set_rate(
        &insp->sampler,
        SU_ABS2NORM_BAUD(fs, insp->req_params.audio.sample_rate));

  insp->cur_params = insp->req_params;
}

SUSDIFF
suscan_audio_inspector_feed(
    void *private,
    suscan_inspector_t *insp,
    const SUCOMPLEX *x,
    SUSCOUNT count)
{
  SUCOMPLEX last, det_x, output = 0;
  SUSCOUNT i;
  SUCOMPLEX lo;
  struct suscan_audio_inspector *self =
      (struct suscan_audio_inspector *) private;

  if (self->cur_params.audio.demod == SUSCAN_INSPECTOR_AUDIO_DEMOD_DISABLED)
    return count;

  last = self->last;

  for (i = 0; i < count && suscan_inspector_sampler_buf_avail(insp) > 0; ++i) {
    det_x = SU_VALID(x[i]) ? x[i] : 0;

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
        last   = det_x;
        break;

      case SUSCAN_INSPECTOR_AUDIO_DEMOD_AM:
        /* Synchronous detection */
        output  = su_pll_track(&self->pll, det_x);

        /* Carrier removal */
        last   += self->beta * (output - last);
        output -= last;

        /* Volume attenuation */
        output *= SUSCAN_AUDIO_AM_ATTENUATION;
        break;

      case SUSCAN_INSPECTOR_AUDIO_DEMOD_USB:
        lo = su_ncqo_read(&self->lo);
        output  = det_x * lo;
        break;

      case SUSCAN_INSPECTOR_AUDIO_DEMOD_LSB:
        lo = su_ncqo_read(&self->lo);
        output = det_x * SU_C_CONJ(lo);
        break;

      default:
        break;
    }

    output *= self->cur_params.audio.volume;

    output = su_iir_filt_feed(&self->filt, output);

    if (su_sampler_feed(&self->sampler, &output))
      suscan_inspector_push_sample(insp, output * .75);
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
    .new_bandwidth = suscan_audio_inspector_new_bandwidth,
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
