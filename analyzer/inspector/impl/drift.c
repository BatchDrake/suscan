/*

  Copyright (C) 2023 Gonzalo José Carracedo Carballal

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

#include <string.h>

#define SU_LOG_DOMAIN "drift-inspector"

#include <sigutils/sigutils.h>
#include <sigutils/agc.h>
#include <sigutils/pll.h>

#include <analyzer/version.h>

#include "inspector/interface.h"
#include "inspector/params.h"

#include "inspector/inspector.h"

#define SUSCAN_DRIFT_INSPECTOR_PLL_BW_FRAC      5e-2
#define SUSCAN_DRIFT_INSPECTOR_AGC_SLOWNESS     200
#define SUSCAN_DRIFT_INSPECTOR_FAST_RISE_FRAC   (2 * 3.9062e-1)
#define SUSCAN_DRIFT_INSPECTOR_FAST_FALL_FRAC   (2 * SUSCAN_DRIFT_INSPECTOR_FAST_RISE_FRAC)
#define SUSCAN_DRIFT_INSPECTOR_SLOW_RISE_FRAC   (10 * SUSCAN_DRIFT_INSPECTOR_FAST_RISE_FRAC)
#define SUSCAN_DRIFT_INSPECTOR_SLOW_FALL_FRAC   (10 * SUSCAN_DRIFT_INSPECTOR_FAST_FALL_FRAC)
#define SUSCAN_DRIFT_INSPECTOR_HANG_MAX_FRAC    (SUSCAN_DRIFT_INSPECTOR_FAST_RISE_FRAC * 5)
#define SUSCAN_DRIFT_INSPECTOR_DELAY_LINE_FRAC  (SUSCAN_DRIFT_INSPECTOR_FAST_RISE_FRAC * 10)
#define SUSCAN_DRIFT_INSPECTOR_MAG_HISTORY_FRAC (SUSCAN_DRIFT_INSPECTOR_FAST_RISE_FRAC * 10)

struct suscan_drift_inspector_params {
  SUFLOAT lock_threshold;
  SUFLOAT cutoff;
  SUFLOAT feedback_interval;
  SUBOOL  pll_reset;

  /* These parameters are read only by the user */
  SUSCOUNT feedback_samples;
};

struct suscan_drift_inspector {
  struct suscan_inspector_sampling_info samp_info;
  struct suscan_drift_inspector_params req_params;
  struct suscan_drift_inspector_params cur_params;

  /* Blocks */
  su_agc_t   agc; /* AGC, to make sure we have consistent lock readings */
  su_pll_t   pll; /* PLL, to track the carrier */

  /* State */
  SUBOOL     lock_state;
  SUBOOL     switching_freq;
  SUFREQ     omdelta;
  SUFREQ     old_freq;   /* Hz */
  SUFREQ     chan_freq;  /* Hz */
  SUFREQ     old_omega;
  SUFREQ     cur_omega;
  
  /* This is during a frequency switch */
  SUSCOUNT   pending_fkicks;
  SUSCOUNT   num_fkicks;
  SUFLOAT    fkick;

  SUFLOAT    fkick_A;
  SUFLOAT    fkick_K;

  SUSCOUNT   feedback_wait;
  SUSCOUNT   feedback_counter;
};

SUPRIVATE void
suscan_drift_inspector_params_initialize(
  struct suscan_drift_inspector_params *params,
  const struct suscan_inspector_sampling_info *sinfo)
{
  SUFLOAT true_bw = SU_NORM2ABS_FREQ(sinfo->equiv_fs, sinfo->bw);

  memset(params, 0, sizeof(struct suscan_drift_inspector_params));

  params->lock_threshold    = .25;
  params->cutoff            = true_bw * SUSCAN_DRIFT_INSPECTOR_PLL_BW_FRAC;
  params->feedback_interval = .1;
}

SUPRIVATE void
suscan_drift_inspector_destroy(struct suscan_drift_inspector *self)
{
  su_agc_finalize(&self->agc);
  su_pll_finalize(&self->pll);
  free(self);
}

SUPRIVATE struct suscan_drift_inspector *
suscan_drift_inspector_new(const struct suscan_inspector_sampling_info *sinfo)
{
  struct suscan_drift_inspector *new = NULL;
  struct su_agc_params agc_params = su_agc_params_INITIALIZER;
  SUFLOAT tau, norm_cutoff;
  SUFREQ  base_samp_rate = sinfo->equiv_fs * sinfo->decimation;
  SUFREQ f0;

  SU_ALLOCATE_FAIL(new, struct suscan_drift_inspector);
  
  f0 = sinfo->f0;
  if (f0 > 1)
    f0 -= 2;
  
  new->samp_info = *sinfo;
  new->cur_omega = SU_NORM2ANG_FREQ(f0) * sinfo->decimation;
  new->chan_freq = SU_NORM2ABS_FREQ(base_samp_rate, f0);

  suscan_drift_inspector_params_initialize(&new->cur_params, sinfo);
  
  new->feedback_wait = new->cur_params.feedback_interval * sinfo->equiv_fs;
  new->cur_params.feedback_samples  = new->feedback_wait;
  new->cur_params.feedback_interval = new->feedback_wait / sinfo->equiv_fs;
  norm_cutoff = SU_ABS2NORM_FREQ(sinfo->equiv_fs, new->cur_params.cutoff);

  /* Create PLL */
  SU_TRY_FAIL(
      su_pll_init(
          &new->pll,
          0,
          norm_cutoff));

  /* Initialize AGC */
  tau = SUSCAN_DRIFT_INSPECTOR_AGC_SLOWNESS / sinfo->equiv_fs;
  if (tau > 200)
    tau = 200;
  
  agc_params.fast_rise_t = tau * SUSCAN_DRIFT_INSPECTOR_FAST_RISE_FRAC;
  agc_params.fast_fall_t = tau * SUSCAN_DRIFT_INSPECTOR_FAST_FALL_FRAC;
  agc_params.slow_rise_t = tau * SUSCAN_DRIFT_INSPECTOR_SLOW_RISE_FRAC;
  agc_params.slow_fall_t = tau * SUSCAN_DRIFT_INSPECTOR_SLOW_FALL_FRAC;
  agc_params.hang_max    = tau * SUSCAN_DRIFT_INSPECTOR_HANG_MAX_FRAC;

  /* TODO: Check whether these sizes are too big */
  agc_params.delay_line_size  = tau * SUSCAN_DRIFT_INSPECTOR_DELAY_LINE_FRAC;
  agc_params.mag_history_size = tau * SUSCAN_DRIFT_INSPECTOR_MAG_HISTORY_FRAC;

  SU_TRY_FAIL(su_agc_init(&new->agc, &agc_params));

  return new;

fail:
  if (new != NULL)
    suscan_drift_inspector_destroy(new);

  return NULL;
}

/************************** API implementation *******************************/
void *
suscan_drift_inspector_open(const struct suscan_inspector_sampling_info *sinfo)
{
  return suscan_drift_inspector_new(sinfo);
}

SUBOOL
suscan_drift_inspector_get_config(void *private, suscan_config_t *config)
{
  struct suscan_drift_inspector *self = (struct suscan_drift_inspector *) private;

  SU_TRYCATCH(
    suscan_config_set_float(
      config,
      "drift.cutoff",
      self->cur_params.cutoff),
    return SU_FALSE);
  
  SU_TRYCATCH(
    suscan_config_set_float(
      config,
      "drift.lock-threshold",
      self->cur_params.lock_threshold),
    return SU_FALSE);

  SU_TRYCATCH(
    suscan_config_set_float(
      config,
      "drift.feedback-interval",
      self->cur_params.feedback_interval),
    return SU_FALSE);

  SU_TRYCATCH(
    suscan_config_set_bool(
      config,
      "drift.pll-reset",
      SU_FALSE),
    return SU_FALSE);

  SU_TRYCATCH(
    suscan_config_set_integer(
      config,
      "drift.feedback-samples",
      self->cur_params.feedback_samples),
    return SU_FALSE);

  return SU_TRUE;
}

SUBOOL
suscan_drift_inspector_parse_config(void *private, const suscan_config_t *config)
{
  struct suscan_field_value *value;
  struct suscan_drift_inspector *self = (struct suscan_drift_inspector *) private;

  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          "drift.cutoff"),
      return SU_FALSE);

  SU_TRYCATCH(value->field->type == SUSCAN_FIELD_TYPE_FLOAT, return SU_FALSE);
  self->req_params.cutoff = value->as_float;

  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          "drift.lock-threshold"),
      return SU_FALSE);

  SU_TRYCATCH(value->field->type == SUSCAN_FIELD_TYPE_FLOAT, return SU_FALSE);
  self->req_params.lock_threshold = value->as_float;

  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          "drift.feedback-interval"),
      return SU_FALSE);

  SU_TRYCATCH(value->field->type == SUSCAN_FIELD_TYPE_FLOAT, return SU_FALSE);
  self->req_params.feedback_interval = value->as_float;

  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          "drift.pll-reset"),
      return SU_FALSE);

  SU_TRYCATCH(value->field->type == SUSCAN_FIELD_TYPE_BOOLEAN, return SU_FALSE);
  self->req_params.pll_reset = value->as_bool;

  return SU_TRUE;
}

/* This method is called inside the inspector mutex */
void
suscan_drift_inspector_commit_config(void *private)
{
  SUFLOAT fs;
  SUBOOL needs_update;
  SUFLOAT norm_cutoff;
  const struct suscan_inspector_sampling_info *sinfo;

  struct suscan_drift_inspector *self = (struct suscan_drift_inspector *) private;
  sinfo = &self->samp_info;

  needs_update = self->cur_params.cutoff != self->req_params.cutoff;
  self->cur_params = self->req_params;
  fs = self->samp_info.equiv_fs;

  if (self->cur_params.pll_reset) {
    self->cur_params.pll_reset = 0;
    self->pll.lock = 0;
    su_pll_set_angfreq(&self->pll, 0);
  }

  if (needs_update) {
    norm_cutoff = SU_ABS2NORM_FREQ(fs, self->cur_params.cutoff);

    su_pll_set_cutoff(&self->pll, norm_cutoff);

    /* Change feedback interval */
    self->feedback_wait = self->cur_params.feedback_interval * sinfo->equiv_fs;
    self->cur_params.feedback_samples = self->feedback_wait;
    self->cur_params.feedback_interval = self->feedback_wait / sinfo->equiv_fs;
  }
}

void
suscan_drift_inspector_new_freq(
  void *private,
  suscan_inspector_t *insp,
  SUFLOAT prev,
  SUFLOAT next)
{
  struct suscan_drift_inspector *self =
      (struct suscan_drift_inspector *) private;
  SUFREQ cur_freq;
  SUFREQ cur_fnorm = SU_ANG2NORM_FREQ(next);
  
  if (cur_fnorm > self->samp_info.decimation)
    cur_fnorm -= 2 * self->samp_info.decimation;
  cur_freq = SU_NORM2ABS_FREQ(self->samp_info.equiv_fs, cur_fnorm);

  self->switching_freq = SU_TRUE;
  self->cur_omega      = next;
  self->old_omega      = prev;
  self->omdelta        = next - prev;

  if (self->omdelta < -PI || self->omdelta > PI)
    self->omdelta -= 2 * PI * SU_FLOOR(self->omdelta / (2 * PI));
  
  self->old_freq       = self->chan_freq;
  self->chan_freq      = cur_freq;
}

SUSDIFF
suscan_drift_inspector_feed(
    void *private,
    suscan_inspector_t *insp,
    const SUCOMPLEX *x,
    SUSCOUNT count)
{
  SUSCOUNT i;
  SUCOMPLEX y = 0;
  SUBOOL lock_state;
  SUSCOUNT kpending = 0;
  SUFLOAT  fA, fK;
  SUSCOUNT feedback_counter;
  SUSCOUNT feedback_max;
  SUFLOAT  carr_freq;
  SUFREQ   curr_freq;
  SUFLOAT  alpha;
  SUFLOAT  kick;

  struct suscan_drift_inspector *self =
      (struct suscan_drift_inspector *) private;

  if (self->switching_freq) {
    self->fkick          = self->omdelta / count;
    self->pending_fkicks = count;
    self->num_fkicks     = count;
    self->switching_freq = SU_FALSE;
    self->fkick_A        = self->fkick * M_PI / (2 * self->num_fkicks);
    self->fkick_K        = M_PI / self->num_fkicks;
  }

  kpending = self->pending_fkicks;
  if (kpending > 0) {
    fA = self->fkick_A;
    fK = self->fkick_K;
  }

  feedback_counter = self->feedback_counter;
  feedback_max     = self->feedback_wait;

  for (i = 0; i < count && suscan_inspector_sampler_buf_avail(insp) > 0; ++i) {
    y  = 2 * su_agc_feed(&self->agc, x[i]);
    y = su_pll_track(&self->pll, y);

    if (kpending > 0) {
      kick = fA * SU_SIN(fK * (kpending - self->num_fkicks));
      su_pll_inc_angfreq(&self->pll, kick);
      --kpending;
    }

    if (++feedback_counter == feedback_max) {
      if (kpending == 0) {
        curr_freq = self->chan_freq;
      } else {
        alpha = kpending / (SUFLOAT) self->num_fkicks;
        curr_freq = (1 - alpha) * self->chan_freq  + alpha * self->old_freq;
      }

      carr_freq = SU_NORM2ABS_FREQ(
          self->samp_info.equiv_fs,
          su_pll_get_freq(&self->pll));

      suscan_inspector_push_sample(insp, carr_freq + I * curr_freq);
      feedback_counter = 0;
    }
  }

  self->feedback_counter = feedback_counter;
  self->pending_fkicks   = kpending;

  lock_state = su_pll_locksig(&self->pll) > self->cur_params.lock_threshold;
  if (self->lock_state != lock_state) {
    self->lock_state = lock_state;
    suscan_inspector_send_signal(insp, "lock", lock_state ? 1 : -1);
  }

  return i;
}

void
suscan_drift_inspector_close(void *private)
{
  suscan_drift_inspector_destroy((struct suscan_drift_inspector *) private);
}

SUPRIVATE struct suscan_inspector_interface iface = {
    .name = "drift",
    .desc = "Frequency drift inspector",
    .open = suscan_drift_inspector_open,
    .get_config = suscan_drift_inspector_get_config,
    .parse_config = suscan_drift_inspector_parse_config,
    .commit_config = suscan_drift_inspector_commit_config,
    .feed = suscan_drift_inspector_feed,
    .freq_changed = suscan_drift_inspector_new_freq,
    .close = suscan_drift_inspector_close
};

SUBOOL
suscan_drift_inspector_register(void)
{
  suscan_config_desc_t *desc = NULL;

  SU_TRYCATCH(
      desc = suscan_config_desc_new_ex(
          "drift-params-desc-" SUSCAN_VERSION_STRING),
      return SU_FALSE);

  /* Add all configuration parameters */
  SU_TRY_FAIL(
    suscan_config_desc_add_field(
      desc,
      SUSCAN_FIELD_TYPE_FLOAT,
      SU_FALSE,
      "drift.cutoff",
      "PLL cutoff frequency"));

  SU_TRY_FAIL(
    suscan_config_desc_add_field(
      desc,
      SUSCAN_FIELD_TYPE_FLOAT,
      SU_FALSE,
      "drift.lock-threshold",
      "Lock signal threshold"));

  SU_TRY_FAIL(
    suscan_config_desc_add_field(
      desc,
      SUSCAN_FIELD_TYPE_FLOAT,
      SU_FALSE,
      "drift.feedback-interval",
      "Feedback interval"));

  SU_TRY_FAIL(
    suscan_config_desc_add_field(
      desc,
      SUSCAN_FIELD_TYPE_BOOLEAN,
      SU_FALSE,
      "drift.pll-reset",
      "PLL reset signal"));

  SU_TRY_FAIL(
    suscan_config_desc_add_field(
      desc,
      SUSCAN_FIELD_TYPE_INTEGER,
      SU_FALSE,
      "drift.feedback-samples",
      "Samples per Doppler update"));

  iface.cfgdesc = desc;
  desc = NULL;
  SU_TRY_FAIL(suscan_config_desc_register(iface.cfgdesc));

  (void) suscan_inspector_interface_add_spectsrc(&iface, "psd");
  
  /* Register inspector interface */
  SU_TRY_FAIL(suscan_inspector_interface_register(&iface));

  return SU_TRUE;

fail:
  if (desc != NULL)
    suscan_config_desc_destroy(desc);

  return SU_FALSE;
}
