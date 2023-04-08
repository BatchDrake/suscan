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

#define SU_LOG_DOMAIN "power-inspector"

#include <sigutils/sigutils.h>
#include <sigutils/agc.h>
#include <sigutils/pll.h>
#include <sigutils/sampling.h>
#include <sigutils/iir.h>
#include <sigutils/clock.h>

#include <analyzer/version.h>

#include "inspector/interface.h"
#include "inspector/params.h"
#include "inspector/inspector.h"

#include <string.h>

struct suscan_power_inspector_params {
  SUSCOUNT integrate_samples;
};

struct suscan_power_inspector {
  struct suscan_inspector_sampling_info samp_info;
  struct suscan_power_inspector_params req_params;
  struct suscan_power_inspector_params cur_params;

  SUFLOAT  pwr_kahan_acc;
  SUFLOAT  pwr_kahan_c;
  SUSCOUNT pwr_count;   
};

SUPRIVATE void
suscan_power_inspector_params_initialize(
    struct suscan_power_inspector_params *params,
    const struct suscan_inspector_sampling_info *sinfo)
{
  memset(params, 0, sizeof(struct suscan_power_inspector_params));
}

SUPRIVATE void
suscan_power_inspector_destroy(struct suscan_power_inspector *self)
{
  free(self);
}

SUPRIVATE struct suscan_power_inspector *
suscan_power_inspector_new(const struct suscan_inspector_sampling_info *sinfo)
{
  struct suscan_power_inspector *new = NULL;

  SU_ALLOCATE_FAIL(new, struct suscan_power_inspector);

  new->samp_info = *sinfo;

  suscan_power_inspector_params_initialize(&new->cur_params, sinfo);

  return new;

fail:
  if (new != NULL)
    suscan_power_inspector_destroy(new);

  return NULL;
}

/************************** API implementation *******************************/
void *
suscan_power_inspector_open(const struct suscan_inspector_sampling_info *s)
{
  return suscan_power_inspector_new(s);
}

SUBOOL
suscan_power_inspector_get_config(void *private, suscan_config_t *config)
{
  struct suscan_power_inspector *self = (struct suscan_power_inspector *) private;

  SU_TRYCATCH(
    suscan_config_set_integer(
      config,
      "power.integrate-samples",
      self->cur_params.integrate_samples),
    return SU_FALSE);
  
  return SU_TRUE;
}

SUBOOL
suscan_power_inspector_parse_config(void *private, const suscan_config_t *config)
{
  struct suscan_field_value *value;
  struct suscan_power_inspector *self = (struct suscan_power_inspector *) private;

  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          "power.integrate-samples"),
      return SU_FALSE);

  SU_TRYCATCH(value->field->type == SUSCAN_FIELD_TYPE_INTEGER, return SU_FALSE);

  self->req_params.integrate_samples = value->as_int;

  return SU_TRUE;
}

/* Called inside inspector mutex */
void
suscan_power_inspector_commit_config(void *private)
{
  struct suscan_power_inspector *self = 
    (struct suscan_power_inspector *) private;

  self->cur_params = self->req_params;
  self->pwr_count = 0;
  self->pwr_kahan_acc = 0;
  self->pwr_kahan_c = 0;
}

SUSDIFF
suscan_power_inspector_feed(
    void *private,
    suscan_inspector_t *insp,
    const SUCOMPLEX *x,
    SUSCOUNT count)
{
  struct suscan_power_inspector *self = 
    (struct suscan_power_inspector *) private;
  SUSCOUNT i, max = self->cur_params.integrate_samples;
  SUFLOAT acc, t, y, c, ptr, power;

  if (max == 0)
    return count;
  
  acc = self->pwr_kahan_acc;
  c   = self->pwr_kahan_c;
  ptr = self->pwr_count;

  for (i = 0; i < count && suscan_inspector_sampler_buf_avail(insp) > 0; ++i) {
    power = SU_C_REAL(x[i] * SU_C_CONJ(x[i]));
    y = power - c;
    t = acc + y;

    c = (t - acc) - y;
    acc = t;

    ++ptr;
    
    if (ptr >= max) {
      suscan_inspector_push_sample(insp, acc / max);
      ptr = 0;
      acc = 0;
      c   = 0;
    }
  }

  self->pwr_kahan_acc = acc;
  self->pwr_kahan_c   = c;
  self->pwr_count     = ptr;

  return i;
}

void
suscan_power_inspector_close(void *private)
{
  struct suscan_power_inspector *self = (struct suscan_power_inspector *) private;

  suscan_power_inspector_destroy(self);
}

SUPRIVATE struct suscan_inspector_interface iface = {
    .name = "power",
    .desc = "Power channel inspector",
    .open = suscan_power_inspector_open,
    .get_config = suscan_power_inspector_get_config,
    .parse_config = suscan_power_inspector_parse_config,
    .commit_config = suscan_power_inspector_commit_config,
    .feed = suscan_power_inspector_feed,
    .close = suscan_power_inspector_close
};

SUBOOL
suscan_power_inspector_register(void)
{
  suscan_config_desc_t *desc = NULL;

  SU_TRY_FAIL(
      desc = suscan_config_desc_new_ex(
          "power-params-desc-" SUSCAN_VERSION_STRING));

  SU_TRY_FAIL(
    suscan_config_desc_add_field(
      desc,
      SUSCAN_FIELD_TYPE_INTEGER,
      SU_FALSE,
      "power.integrate-samples",
      "Number of samples to integrate"));

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
