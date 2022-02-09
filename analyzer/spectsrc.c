/*

  Copyright (C) 2017 Gonzalo José Carracedo Carballal

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

#define SU_LOG_DOMAIN "spectsrc"

#include "spectsrc.h"
#include <sigutils/taps.h>

PTR_LIST_CONST(struct suscan_spectsrc_class, spectsrc_class);

SUPRIVATE SUBOOL spectsrcs_init = SU_FALSE;

const struct suscan_spectsrc_class *
suscan_spectsrc_class_lookup(const char *name)
{
  unsigned int i;

  for (i = 0; i < spectsrc_class_count; ++i)
    if (strcmp(spectsrc_class_list[i]->name, name) == 0)
      return spectsrc_class_list[i];

  return NULL;
}

SUBOOL
suscan_spectsrc_class_register(const struct suscan_spectsrc_class *class)
{
  SU_TRYCATCH(class->name    != NULL, return SU_FALSE);
  SU_TRYCATCH(class->desc    != NULL, return SU_FALSE);
  SU_TRYCATCH(class->ctor    != NULL, return SU_FALSE);
  SU_TRYCATCH(class->dtor    != NULL, return SU_FALSE);

  SU_TRYCATCH(
      suscan_spectsrc_class_lookup(class->name) == NULL,
      return SU_FALSE);

  SU_TRYCATCH(
      PTR_LIST_APPEND_CHECK(spectsrc_class, (void *) class) != -1,
      return SU_FALSE);

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_spectsrc_on_psd_data(
    void *userdata,
    const SUFLOAT *data,
    unsigned int size)
{
  suscan_spectsrc_t *self = (suscan_spectsrc_t *) userdata;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      (self->on_spectrum) (self->userdata, data, size),
      goto done);

  ok = SU_TRUE;

done:
  return ok;
}

void
suscan_spectsrc_set_throttle_factor(
  suscan_spectsrc_t *self,
  SUFLOAT throttle_factor)
{
  if (!sufeq(throttle_factor, self->throttle_factor, 1e-6)) {
    self->throttle_factor = throttle_factor;
    self->smooth_psd_params.refresh_rate = self->refresh_rate / self->throttle_factor;
    (void) su_smoothpsd_set_params(self->smooth_psd, &self->smooth_psd_params);
  }
}

suscan_spectsrc_t *
suscan_spectsrc_new(
    const struct suscan_spectsrc_class *classdef,
    SUFLOAT  samp_rate,
    SUFLOAT  spectrum_rate,
    SUSCOUNT size,
    enum sigutils_channel_detector_window window_type,
    SUBOOL (*on_spectrum) (void *userdata, const SUFLOAT *data, SUSCOUNT size),
    void *userdata)
{
  suscan_spectsrc_t *new = NULL;
  struct sigutils_smoothpsd_params params =
      sigutils_smoothpsd_params_INITIALIZER;

  SU_TRYCATCH(new = calloc(1, sizeof(suscan_spectsrc_t)), goto fail);

  new->classptr = classdef;
  new->on_spectrum = on_spectrum;
  new->userdata = userdata;

  if (classdef->preproc != NULL) {
    SU_TRYCATCH(new->buffer = malloc(size * sizeof(SUCOMPLEX)), goto fail);
    new->buffer_size = size;
  }

  new->refresh_rate = spectrum_rate;
  new->throttle_factor = 1.;

  params.fft_size = size;
  params.samp_rate = samp_rate;
  params.refresh_rate = new->refresh_rate / new->throttle_factor;
  params.window = window_type;

  new->smooth_psd_params = params;
  
  SU_TRYCATCH(
      new->smooth_psd = su_smoothpsd_new(
          &params,
          suscan_spectsrc_on_psd_data,
          new),
      goto fail);

  SU_TRYCATCH(
      new->privdata = (classdef->ctor) (new),
      goto fail);

  return new;

fail:
  if (new != NULL)
    suscan_spectsrc_destroy(new);

  return NULL;
}


SUSCOUNT
suscan_spectsrc_feed(
    suscan_spectsrc_t *self,
    const SUCOMPLEX *data,
    SUSCOUNT size)
{
  if (self->classptr->preproc != NULL) {
    /* Spectrum source has a preprocessing routine. Apply data to it */
    if (size > self->buffer_size)
      size = self->buffer_size;

    memcpy(self->buffer, data, size * sizeof(SUCOMPLEX));
    SU_TRYCATCH(
        (self->classptr->preproc) (
            self,
            self->privdata,
            self->buffer,
            size),
        return SU_FALSE);

    SU_TRYCATCH(
        su_smoothpsd_feed(self->smooth_psd, self->buffer, size),
        return -1);
  } else {
    SU_TRYCATCH(
        su_smoothpsd_feed(self->smooth_psd, data, size),
        return -1);
  }

  return size;
}

void
suscan_spectsrc_destroy(suscan_spectsrc_t *self)
{
  if (self != NULL)
    (self->classptr->dtor) (self->privdata);

  if (self->buffer != NULL)
    free(self->buffer);

  if (self->smooth_psd != NULL)
    su_smoothpsd_destroy(self->smooth_psd);

  free(self);
}

SUBOOL
suscan_spectsrcs_initialized(void)
{
  return spectsrcs_init;
}

SUBOOL
suscan_init_spectsrcs(void)
{
  SU_TRYCATCH(suscan_spectsrc_psd_register(), return SU_FALSE);
  SU_TRYCATCH(suscan_spectsrc_cyclo_register(), return SU_FALSE);
  SU_TRYCATCH(suscan_spectsrc_fmcyclo_register(), return SU_FALSE);
  SU_TRYCATCH(suscan_spectsrc_fmspect_register(), return SU_FALSE);
  SU_TRYCATCH(suscan_spectsrc_pmspect_register(), return SU_FALSE);
  SU_TRYCATCH(suscan_spectsrc_timediff_register(), return SU_FALSE);
  SU_TRYCATCH(suscan_spectsrc_exp_2_register(), return SU_FALSE);
  SU_TRYCATCH(suscan_spectsrc_exp_4_register(), return SU_FALSE);
  SU_TRYCATCH(suscan_spectsrc_exp_8_register(), return SU_FALSE);

  spectsrcs_init = SU_TRUE;

  return SU_TRUE;
}
