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

#include "tonegen.h"
#include <analyzer/source.h>
#include <sys/time.h>

#ifdef _SU_SINGLE_PRECISION
#  define SUSCAN_SOAPY_SAMPFMT SOAPY_SDR_CF32
#else
#  define SUSCAN_SOAPY_SAMPFMT SOAPY_SDR_CF64
#endif

/****************************** Implementation ********************************/
SUPRIVATE void
suscan_source_tonegen_close(void *ptr)
{
  struct suscan_source_tonegen *self = (struct suscan_source_tonegen *) ptr;
  
  free(self);
}

SUPRIVATE SUBOOL
suscan_source_tonegen_populate_source_info(
  struct suscan_source_tonegen *self,
  struct suscan_source_info *info,
  const suscan_source_config_t *config)
{
  SUBOOL ok = SU_FALSE;

  info->realtime    = SU_TRUE;

  /* Adjust permissions */
  info->permissions = SUSCAN_ANALYZER_ALL_SDR_PERMISSIONS;
  info->permissions &= ~SUSCAN_ANALYZER_PERM_SET_DC_REMOVE;

  /* Set sample rate */
  info->source_samp_rate    = self->samp_rate;
  info->effective_samp_rate = self->samp_rate;
  info->measured_samp_rate  = self->samp_rate;
  
  /* Adjust limits */
  info->freq_min = -300000000000;
  info->freq_max = +300000000000;

  /* Get current source time */
  gettimeofday(&info->source_time, NULL);
  gettimeofday(&info->source_start, NULL);

  ok = SU_TRUE;

  return ok;
}

SUPRIVATE void *
suscan_source_tonegen_open(
  suscan_source_t *source,
  suscan_source_config_t *config,
  struct suscan_source_info *info)
{
  struct suscan_source_tonegen *new = NULL;
  const char *signal, *noise;
  SUFLOAT val;

  SU_ALLOCATE_FAIL(new, struct suscan_source_tonegen);

  new->config    = config;
  new->source    = source;
  new->samp_rate = config->samp_rate;
  new->init_freq = config->freq;

  su_ncqo_init(&new->tone, 0);
  suscan_throttle_init(&new->throttle, new->samp_rate);

  new->noise_amplitude  = 5e-3;
  new->signal_amplitude = 5e-1;

  signal = suscan_source_config_get_param(config, "signal");
  noise  = suscan_source_config_get_param(config, "noise");

  if (signal != NULL && sscanf(signal, "%g", &val) == 1)
    new->signal_amplitude = SU_MAG_RAW(val);
  if (noise != NULL  && sscanf(noise, "%g", &val) == 1)
    new->noise_amplitude  = SU_MAG_RAW(val);

  new->noise_amplitude *= SU_SQRT(new->samp_rate);

  /* Initialize source info */
  suscan_source_tonegen_populate_source_info(new, info, config);

  return new;

fail:
  if (new != NULL)
    suscan_source_tonegen_close(new);

  return NULL;
}

SUPRIVATE SUBOOL
suscan_source_tonegen_start(void *userdata)
{
  struct suscan_source_tonegen *self = (struct suscan_source_tonegen *) userdata;
  self->force_eos = SU_FALSE;
  return SU_TRUE;
}

SUPRIVATE SUSDIFF
suscan_source_tonegen_read(
  void *userdata,
  SUCOMPLEX *buf,
  SUSCOUNT max)
{
  struct suscan_source_tonegen *self = (struct suscan_source_tonegen *) userdata;
  SUSCOUNT i;
  SUCOMPLEX noise;

  if (self->force_eos)
    return SU_FALSE;
  
  max = suscan_throttle_get_portion(&self->throttle, max);

  if (self->out_of_band) {
    /* Out of band. Only noise. */
    for (i = 0; i < max; ++i)
      buf[i] = self->noise_amplitude * su_c_awgn();
  } else {
    /* In band. Simulate signal. */
    for (i = 0; i < max; ++i) {
      noise  = self->noise_amplitude * su_c_awgn();
      buf[i] = self->signal_amplitude * su_ncqo_read(&self->tone) + noise;
    }
  }
  suscan_throttle_advance(&self->throttle, max);

  return max;
}

SUPRIVATE void
suscan_source_tonegen_get_time(void *userdata, struct timeval *tv)
{
  gettimeofday(tv, NULL);
}


SUPRIVATE SUBOOL
suscan_source_tonegen_cancel(void *userdata)
{
  struct suscan_source_tonegen *self = (struct suscan_source_tonegen *) userdata;

  self->force_eos = SU_TRUE;
  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_source_tonegen_set_frequency(void *userdata, SUFREQ freq)
{
  struct suscan_source_tonegen *self = (struct suscan_source_tonegen *) userdata;
  SUFREQ delta = freq - self->init_freq;

  self->curr_freq = freq;
  self->out_of_band = SU_ABS(delta) > .5 * self->samp_rate;

  if (!self->out_of_band)
    su_ncqo_set_freq(
      &self->tone,
      SU_ABS2NORM_FREQ(self->samp_rate, -delta));
  
  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_source_tonegen_set_gain(void *userdata, const char *name, SUFLOAT gain)
{
  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_source_tonegen_set_antenna(void *userdata, const char *name)
{
  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_source_tonegen_set_bandwidth(void *userdata, SUFLOAT bw)
{
  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_source_tonegen_set_ppm(void *userdata, SUFLOAT ppm)
{
  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_source_tonegen_set_dc_remove(void *userdata, SUBOOL remove)
{
  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_source_tonegen_set_agc(void *userdata, SUBOOL set)
{
  return SU_TRUE;
}

SUPRIVATE struct suscan_source_interface g_tonegen_source =
{
  .name            = "tonegen",
  .desc            = "Tone generator with AWGN",
  .realtime        = SU_TRUE,
  
  .open            = suscan_source_tonegen_open,
  .close           = suscan_source_tonegen_close,
  .start           = suscan_source_tonegen_start,
  .cancel          = suscan_source_tonegen_cancel,
  .read            = suscan_source_tonegen_read,
  .set_frequency   = suscan_source_tonegen_set_frequency,
  .set_gain        = suscan_source_tonegen_set_gain,
  .set_antenna     = suscan_source_tonegen_set_antenna,
  .set_bandwidth   = suscan_source_tonegen_set_bandwidth,
  .set_ppm         = suscan_source_tonegen_set_ppm,
  .set_dc_remove   = suscan_source_tonegen_set_dc_remove,
  .set_agc         = suscan_source_tonegen_set_agc,
  .get_time        = suscan_source_tonegen_get_time,

  /* Unser members */
  .seek            = NULL,
  .max_size        = NULL,
  .estimate_size   = NULL,
  .get_freq_limits = NULL,
  .is_real_time    = NULL,
  .guess_metadata  = NULL,
};

SUBOOL
suscan_source_register_tonegen(void)
{
  int ndx;
  SUBOOL ok = SU_FALSE;

  SU_TRYC(ndx = suscan_source_register(&g_tonegen_source));

  ok = SU_TRUE;

done:
  return ok;
}
