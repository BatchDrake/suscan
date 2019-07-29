/*

  Copyright (C) 2017 Gonzalo José Carracedo Carballal

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

#ifndef _SOURCE_H
#define _SOURCE_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <sndfile.h>
#include <sigutils/sigutils.h>
#include <SoapySDR/Device.h>
#include <SoapySDR/Formats.h>
#include "../util/object.h"

#define SUSCAN_SOURCE_DEFAULT_BUFSIZ 1024

/************************** Source config API ********************************/
struct suscan_source_gain_desc {
  char *name;
  SUFLOAT min;
  SUFLOAT max;
  SUFLOAT step;
  SUFLOAT def;
};

struct suscan_source_device_info {
  PTR_LIST_CONST(struct suscan_source_gain_desc, gain_desc);
  PTR_LIST_CONST(char, antenna);
};

#define suscan_source_device_info_INITIALIZER   \
{                                               \
  NULL, /* gain_list */                         \
  0, /* gain_count */                           \
  NULL, /* antenna_list */                      \
  0, /* antenna_count */                        \
}

void suscan_source_device_info_finalize(struct suscan_source_device_info *info);

struct suscan_source_device {
  const char *driver;
  char *desc;
  SoapySDRKwargs *args;
  int index;

  PTR_LIST(struct suscan_source_gain_desc, gain_desc);
  PTR_LIST(char, antenna);
};

typedef struct suscan_source_device suscan_source_device_t;

SUINLINE const char *
suscan_source_device_get_driver(const suscan_source_device_t *dev)
{
  const char *driver;

  if ((driver = SoapySDRKwargs_get(dev->args, "driver")) == NULL)
    driver = dev->driver;

  return driver;
}

SUINLINE const char *
suscan_source_device_get_desc(const suscan_source_device_t *dev)
{
  return dev->desc;
}

SUINLINE int
suscan_source_device_get_index(const suscan_source_device_t *dev)
{
  return dev->index;
}

SUINLINE SUBOOL
suscan_source_device_is_populated(const suscan_source_device_t *dev)
{
  return dev->antenna_count != 0;
}

SUBOOL suscan_source_device_walk(
    SUBOOL (*function) (
        const suscan_source_device_t *dev,
        unsigned int index,
        void *privdata),
    void *privdata);

const suscan_source_device_t *suscan_source_device_get_by_index(
    unsigned int index);

const suscan_source_device_t *suscan_source_get_null_device(void);

unsigned int suscan_source_device_get_count(void);

SUBOOL suscan_source_device_get_info(
    const suscan_source_device_t *dev,
    unsigned int channel,
    struct suscan_source_device_info *info);

enum suscan_source_type {
  SUSCAN_SOURCE_TYPE_FILE,
  SUSCAN_SOURCE_TYPE_SDR
};

enum suscan_source_format {
  SUSCAN_SOURCE_FORMAT_AUTO,
  SUSCAN_SOURCE_FORMAT_RAW,
  SUSCAN_SOURCE_FORMAT_WAV
};

struct suscan_source_gain_value {
  const struct suscan_source_gain_desc *desc;
  SUFLOAT val;
};

struct suscan_source_config {
  enum suscan_source_type type;
  enum suscan_source_format format;
  char *label; /* Label for this configuration */

  /* Common for all source types */
  SUFREQ  freq;
  SUFLOAT bandwidth;
  SUBOOL  iq_balance;
  SUBOOL  dc_remove;
  unsigned int samp_rate;
  unsigned int average;

  /* For file sources */
  char *path;
  SUBOOL loop;

  /* For SDR sources */
  const suscan_source_device_t *device; /* Borrowed, optional */
  SoapySDRKwargs *soapy_args;
  char *antenna;
  unsigned int channel;
  PTR_LIST(struct suscan_source_gain_value, gain);
  PTR_LIST(struct suscan_source_gain_value, hidden_gain);
};

typedef struct suscan_source_config suscan_source_config_t;

SUBOOL suscan_source_config_walk(
    SUBOOL (*function) (suscan_source_config_t *cfg, void *privdata),
    void *privdata);

/* Serialization methods */
suscan_object_t *suscan_source_config_to_object(
    const suscan_source_config_t *source);

suscan_source_config_t *suscan_source_config_from_object(
    const suscan_object_t *object);

const char *suscan_source_config_get_label(const suscan_source_config_t *source);
SUBOOL suscan_source_config_set_label(
    suscan_source_config_t *config,
    const char *label);

enum suscan_source_type suscan_source_config_get_type(
    const suscan_source_config_t *config);

enum suscan_source_format suscan_source_config_get_format(
    const suscan_source_config_t *config);

void suscan_source_config_set_type_format(
    suscan_source_config_t *config,
    enum suscan_source_type type,
    enum suscan_source_format format);

SUFREQ suscan_source_config_get_freq(const suscan_source_config_t *config);
void suscan_source_config_set_freq(
    suscan_source_config_t *config,
    SUFREQ freq);

SUFLOAT suscan_source_config_get_bandwidth(
    const suscan_source_config_t *config);
void suscan_source_config_set_bandwidth(
    suscan_source_config_t *config,
    SUFLOAT bandwidth);

SUBOOL suscan_source_config_get_iq_balance(
    const suscan_source_config_t *config);
void suscan_source_config_set_iq_balance(
    suscan_source_config_t *config,
    SUBOOL iq_balance);

SUBOOL suscan_source_config_get_dc_remove(const suscan_source_config_t *config);
void suscan_source_config_set_dc_remove(
    suscan_source_config_t *config,
    SUBOOL dc_remove);

SUBOOL suscan_source_config_get_loop(const suscan_source_config_t *config);
void suscan_source_config_set_loop(suscan_source_config_t *config, SUBOOL loop);

const char *suscan_source_config_get_path(const suscan_source_config_t *config);
SUBOOL suscan_source_config_set_path(
    suscan_source_config_t *config,
    const char *path);

const char *suscan_source_config_get_antenna(
    const suscan_source_config_t *config);
SUBOOL suscan_source_config_set_antenna(
    suscan_source_config_t *config,
    const char *antenna);

unsigned int suscan_source_config_get_samp_rate(
    const suscan_source_config_t *config);
void suscan_source_config_set_samp_rate(
    suscan_source_config_t *config,
    unsigned int samp_rate);

unsigned int suscan_source_config_get_average(
    const suscan_source_config_t *config);
SUBOOL suscan_source_config_set_average(
    suscan_source_config_t *config,
    unsigned int average);

unsigned int suscan_source_config_get_channel(
    const suscan_source_config_t *config);
void suscan_source_config_set_channel(
    suscan_source_config_t *config,
    unsigned int channel);

struct suscan_source_gain_value *suscan_source_config_lookup_gain(
    const suscan_source_config_t *config,
    const char *name);

SUBOOL suscan_source_config_walk_gains(
    const suscan_source_config_t *config,
    SUBOOL (*gain_cb) (void *privdata, const char *name, SUFLOAT value),
    void *privdata);

struct suscan_source_gain_value *suscan_source_config_assert_gain(
    suscan_source_config_t *config,
    const char *name,
    SUFLOAT value);

SUFLOAT suscan_source_config_get_gain(
    const suscan_source_config_t *config,
    const char *name);

SUBOOL suscan_source_config_set_gain(
    suscan_source_config_t *config,
    const char *name,
    SUFLOAT value);

SUBOOL suscan_source_config_set_device(
    suscan_source_config_t *config,
    const suscan_source_device_t *dev);

SUINLINE const suscan_source_device_t *
suscan_source_config_get_device(suscan_source_config_t *config)
{
  return config->device;
}

char *suscan_source_config_get_sdr_args(const suscan_source_config_t *config);

SUBOOL suscan_source_config_set_sdr_args(
    const suscan_source_config_t *config,
    const char *args);

suscan_source_config_t *suscan_source_config_new(
    enum suscan_source_type type,
    enum suscan_source_format format);

suscan_source_config_t *suscan_source_config_clone(
    const suscan_source_config_t *config);

suscan_source_config_t *suscan_source_config_lookup(const char *label);

SUBOOL suscan_source_config_unregister(suscan_source_config_t *config);

void suscan_source_config_destroy(suscan_source_config_t *);

/****************************** Source API ***********************************/
struct suscan_source {
  suscan_source_config_t *config; /* Source may alter configuration! */
  SUBOOL capturing;
  SUBOOL soft_dc_correction;
  SUBOOL soft_iq_balance;
  SUSDIFF (*read) (
        struct suscan_source *source,
        SUCOMPLEX *buffer,
        SUSCOUNT max);

  /* File sources are accessed through a soundfile handle */
  SNDFILE *sf;
  SF_INFO sf_info;
  SUBOOL iq_file;

  /* SDR sources are accessed through SoapySDR */
  SoapySDRDevice *sdr;
  SoapySDRStream *rx_stream;
  size_t chan_array[1];

  /* To prevent source from looping forever */
  SUBOOL force_eos;
};

typedef struct suscan_source suscan_source_t;

SUBOOL suscan_source_stop_capture(suscan_source_t *source);
SUBOOL suscan_source_start_capture(suscan_source_t *source);

SUBOOL suscan_source_set_freq(suscan_source_t *source, SUFREQ freq);
SUBOOL suscan_source_set_gain(
    suscan_source_t *source,
    const char *name,
    SUFLOAT gain);

suscan_source_t *suscan_source_new(suscan_source_config_t *config);

SUSDIFF suscan_source_read(
    suscan_source_t *source,
    SUCOMPLEX *buffer,
    SUSCOUNT max);

SUINLINE void
suscan_source_force_eos(suscan_source_t *src)
{
  src->force_eos = SU_TRUE;
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

void suscan_source_destroy(suscan_source_t *config);

SUBOOL suscan_source_config_register(suscan_source_config_t *config);

SUBOOL suscan_init_sources(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _SOURCE_H */
