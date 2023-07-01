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

#ifndef _SOURCE_H
#define _SOURCE_H

#include <sndfile.h>
#include <string.h>
#include <sigutils/sigutils.h>
#include <SoapySDR/Device.h>
#include <SoapySDR/Formats.h>
#include <SoapySDR/Version.h>
#include <analyzer/serialize.h>
#include <analyzer/source/device.h>
#include <analyzer/source/config.h>
#include <analyzer/source/info.h>
#include <sigutils/util/util.h>
#include <sigutils/dc_corrector.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#if defined(_WIN32) && defined(interface)
#  undef interface
#endif /* interface */

#define SUSCAN_SOURCE_DEFAULT_BUFSIZ 1024

#define SUSCAN_SOURCE_SETTING_PREFIX    "setting:"
#define SUSCAN_SOURCE_SETTING_PFXLEN    (sizeof("setting:") - 1)

#define SUSCAN_SOURCE_DEFAULT_READ_TIMEOUT 100000 /* 100 ms */
#define SUSCAN_SOURCE_ANTIALIAS_REL_SIZE    5
#define SUSCAN_SOURCE_DECIMATOR_BUFFER_SIZE 512

#define SUSCAN_SOURCE_DC_AVERAGING_PERIOD   10
#define SUSCAN_SOURCE_DECIM_INNER_GUARD     5e-2


struct sigutils_specttuner;
struct sigutils_specttuner_channel;

/************** Source interface: to be implemented by all sources ************/
struct suscan_source;
struct suscan_source_interface {
  const char *name;
  const char *desc;

  void   *(*open) (
    struct suscan_source *source,
    suscan_source_config_t *config,
    struct suscan_source_info *info);
  void     (*close) (void *);

  SUSDIFF  (*estimate_size) (const suscan_source_config_t *); /* Without decimation */

  SUBOOL   (*start) (void *);
  SUBOOL   (*cancel) (void *);

  SUSDIFF  (*read) (void *, SUCOMPLEX *buffer, SUSCOUNT max);
  SUSDIFF  (*max_size) (void *);
  
  void     (*get_time) (void *, struct timeval *tv);
  SUBOOL   (*seek) (void *,  SUSCOUNT samples);

  SUBOOL   (*set_frequency) (void *, SUFREQ freq);
  SUBOOL   (*set_gain) (void *, const char *name, SUFLOAT value);
  SUBOOL   (*set_antenna) (void *, const char *);
  SUBOOL   (*set_bandwidth) (void *, SUFLOAT);
  SUBOOL   (*set_ppm) (void *, SUFLOAT);
  SUBOOL   (*set_dc_remove) (void *, SUBOOL);
  SUBOOL   (*set_agc) (void *, SUBOOL);

  unsigned (*get_samp_rate) (void *);
};

struct suscan_source {
  suscan_source_config_t *config; /* Source may alter configuration! */
  const struct suscan_source_interface *iface;
  struct suscan_source_info info;

  SUBOOL   capturing;
  void    *src_priv; /* Opaque source object */

  SUSCOUNT total_samples;
  SUBOOL   looped;

  SUBOOL   dc_correction_enabled;
  SUBOOL   soft_dc;

  su_dc_corrector_t dc_corrector;

  /* To prevent source from looping forever */
  SUBOOL force_eos;

  /* Downsampling members */
  struct sigutils_specttuner         *decimator;
  struct sigutils_specttuner_channel *main_channel;
  SUCOMPLEX *read_buf;
  SUCOMPLEX *curr_buf;
  SUSCOUNT   curr_size;
  SUSCOUNT   curr_ptr;

  SUCOMPLEX *decim_spillover;
  SUSCOUNT   decim_spillover_alloc;
  SUSCOUNT   decim_spillover_size;
  SUSCOUNT   decim_spillover_ptr;

  int decim;
};

typedef struct suscan_source suscan_source_t;

/* Construction and destruction */
suscan_source_t *suscan_source_new(suscan_source_config_t *config);
void suscan_source_destroy(suscan_source_t *config);

/* Interface methods */
SUBOOL suscan_source_start_capture(suscan_source_t *source);
SUBOOL suscan_source_stop_capture(suscan_source_t *source);

SUSDIFF suscan_source_read(
    suscan_source_t *source,
    SUCOMPLEX *buffer,
    SUSCOUNT max);
SUSDIFF  suscan_source_get_max_size(const suscan_source_t *self);

void   suscan_source_get_time(suscan_source_t *self, struct timeval *tv);
SUBOOL suscan_source_seek(suscan_source_t *self, SUSCOUNT);

SUFREQ suscan_source_get_freq(const suscan_source_t *source);
SUBOOL suscan_source_set_freq(suscan_source_t *source, SUFREQ freq);
SUBOOL suscan_source_set_lnb_freq(suscan_source_t *source, SUFREQ freq);
SUBOOL suscan_source_set_freq2(suscan_source_t *source, SUFREQ freq, SUFREQ lnb);
SUBOOL suscan_source_set_gain(
    suscan_source_t *source,
    const char *name,
    SUFLOAT gain);
SUBOOL suscan_source_set_antenna(suscan_source_t *source, const char *name);
SUBOOL suscan_source_set_bandwidth(suscan_source_t *source, SUFLOAT bw);
SUBOOL suscan_source_set_ppm(suscan_source_t *source, SUFLOAT ppm);
SUBOOL suscan_source_set_dc_remove(suscan_source_t *source, SUBOOL remove);
SUBOOL suscan_source_set_agc(suscan_source_t *source, SUBOOL set);

/* Other API methods */
SUSCOUNT suscan_source_get_dc_samples(const suscan_source_t *self);
SUSCOUNT suscan_source_get_consumed_samples(const suscan_source_t *self);
SUSCOUNT suscan_source_get_base_samp_rate(const suscan_source_t *self);
void     suscan_source_get_end_time(
  const suscan_source_t *self, 
  struct timeval *tv);

SUINLINE void 
suscan_source_get_start_time(
  const suscan_source_t *self,
  struct timeval *tv)
{
  suscan_source_config_get_start_time(self->config, tv);
}

SUINLINE void 
suscan_source_set_start_time(
  suscan_source_t *self,
  struct timeval tv)
{
  suscan_source_config_set_start_time(self->config, tv);
}

SUINLINE SUBOOL
suscan_source_has_looped(suscan_source_t *self)
{
  SUBOOL looped = self->looped;
  self->looped = SU_FALSE;

  return looped;
}

SUINLINE enum suscan_source_type
suscan_source_get_type(const suscan_source_t *src)
{
  return src->config->type;
}

SUINLINE SUFLOAT
suscan_source_get_samp_rate(const suscan_source_t *src)
{
  if (src->capturing)
    return (SUFLOAT) src->info.source_samp_rate;
  else
    return (SUFLOAT) src->config->samp_rate / src->config->average;
}

SUINLINE int
suscan_source_get_decimation(const suscan_source_t *self)
{
  return self->decim;
}

SUINLINE void
suscan_source_force_eos(suscan_source_t *src)
{
  src->force_eos = SU_TRUE;
}

SUINLINE const struct suscan_source_info *
suscan_source_get_info(const suscan_source_t *self)
{
  return &self->info;
}

SUINLINE const suscan_source_config_t *
suscan_source_get_config(const suscan_source_t *src)
{
  return src->config;
}

SUINLINE SUBOOL
suscan_source_is_capturing(const suscan_source_t *src)
{
  return src->capturing;
}


/******************************* Source interface *****************************/
SUBOOL suscan_source_config_register(suscan_source_config_t *config);

int suscan_source_register(const struct suscan_source_interface *iface);
const struct suscan_source_interface *suscan_source_interface_lookup_by_index(int);
const struct suscan_source_interface *suscan_source_interface_lookup_by_name(const char *);

SUBOOL suscan_source_detect_devices(void);

/* Internal */
SUBOOL suscan_source_register_file(void);
SUBOOL suscan_source_register_soapysdr(void);

SUBOOL suscan_source_init_source_types(void);
SUBOOL suscan_init_sources(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _SOURCE_H */
