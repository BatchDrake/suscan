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
};

struct suscan_drift_inspector {
  struct suscan_inspector_sampling_info samp_info;
  struct suscan_drift_inspector_params req_params;
  struct suscan_drift_inspector_params cur_params;

  /* Blocks */
  su_agc_t   agc; /* AGC, to make sure we have consistent lock readings */
  su_pll_t   pll; /* PLL, to track the carrier */
};

SUPRIVATE void
suscan_drift_inspector_params_initialize(
  struct suscan_drift_inspector_params *params,
  const struct suscan_inspector_sampling_info *sinfo)
{
  SUFLOAT true_bw = SU_NORM2ABS_FREQ(sinfo->equiv_fs, sinfo->bw);

  memset(params, 0, sizeof(struct suscan_drift_inspector_params));

  params->lock_threshold = .5;
  params->cutoff = true_bw / SUSCAN_DRIFT_INSPECTOR_AGC_SLOWNESS;
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

  SU_ALLOCATE_FAIL(new, struct suscan_drift_inspector);
  
  new->samp_info = *sinfo;
  norm_cutoff = SU_ABS2NORM_FREQ(sinfo->equiv_fs, new->cur_params.cutoff);

  suscan_drift_inspector_params_initialize(&new->cur_params, sinfo);

  /* Create PLL */
  SU_TRY_FAIL(
      su_pll_init(
          &new->pll,
          0,
          norm_cutoff));

  /* Initialize AGC */
  tau = SUSCAN_DRIFT_INSPECTOR_AGC_SLOWNESS / norm_cutoff;
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

  return SU_TRUE;
}

/* This method is called inside the inspector mutex */
void
suscan_drift_inspector_commit_config(void *private)
{
  SUFLOAT fs, tau;
  SUBOOL needs_update;
  SUFLOAT norm_cutoff;
  struct su_agc_params agc_params = su_agc_params_INITIALIZER;

  su_pll_t new_pll;
  su_agc_t new_agc;

  struct suscan_drift_inspector *self = (struct suscan_drift_inspector *) private;

  needs_update = self->cur_params.cutoff != self->req_params.cutoff;
  self->cur_params = self->req_params;
  fs = self->samp_info.equiv_fs;

  if (needs_update) {
    norm_cutoff = SU_ABS2NORM_FREQ(fs, self->cur_params.cutoff);

    if (su_pll_init(
      &new_pll,
      0,
      norm_cutoff)) {
      su_pll_finalize(&self->pll);
      self->pll = new_pll;
    }

    /* Initialize AGC */
    tau = SUSCAN_DRIFT_INSPECTOR_AGC_SLOWNESS / norm_cutoff;
    agc_params.fast_rise_t = tau * SUSCAN_DRIFT_INSPECTOR_FAST_RISE_FRAC;
    agc_params.fast_fall_t = tau * SUSCAN_DRIFT_INSPECTOR_FAST_FALL_FRAC;
    agc_params.slow_rise_t = tau * SUSCAN_DRIFT_INSPECTOR_SLOW_RISE_FRAC;
    agc_params.slow_fall_t = tau * SUSCAN_DRIFT_INSPECTOR_SLOW_FALL_FRAC;
    agc_params.hang_max    = tau * SUSCAN_DRIFT_INSPECTOR_HANG_MAX_FRAC;

    /* TODO: Check whether these sizes are too big */
    agc_params.delay_line_size  = tau * SUSCAN_DRIFT_INSPECTOR_DELAY_LINE_FRAC;
    agc_params.mag_history_size = tau * SUSCAN_DRIFT_INSPECTOR_MAG_HISTORY_FRAC;

    if (su_agc_init(&new_agc, &agc_params)) {
      su_agc_finalize(&self->agc);
      self->agc = new_agc;
    }
  }
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

  struct suscan_drift_inspector *self =
      (struct suscan_drift_inspector *) private;

  for (i = 0; i < count && suscan_inspector_sampler_buf_avail(insp) > 0; ++i) {
    y  = 2 * su_agc_feed(&self->agc, x[i]);
    y = su_pll_track(&self->pll, y);

    /* TODO: Test lock */
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
  SU_TRYCATCH(suscan_config_desc_add_gc_params(iface.cfgdesc), return SU_FALSE);
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
