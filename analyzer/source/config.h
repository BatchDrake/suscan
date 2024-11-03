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

#ifndef _ANALYZER_SOURCE_CONFIG_H
#define _ANALYZER_SOURCE_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <analyzer/device/spec.h>
#include <sigutils/util/compat-time.h>
#include <analyzer/serialize.h>
#include <object.h>

#define SUSCAN_SOURCE_DEFAULT_NAME      "Default source"
#define SUSCAN_SOURCE_DEFAULT_FREQ      433920000 /* 433 ISM */
#define SUSCAN_SOURCE_DEFAULT_SAMP_RATE 1000000
#define SUSCAN_SOURCE_DEFAULT_BANDWIDTH SUSCAN_SOURCE_DEFAULT_SAMP_RATE

#define SUSCAN_SOURCE_LOCAL_INTERFACE   "local"
#define SUSCAN_SOURCE_REMOTE_INTERFACE  "remote"

enum suscan_source_format {
  SUSCAN_SOURCE_FORMAT_AUTO,
  SUSCAN_SOURCE_FORMAT_RAW_FLOAT32,
  SUSCAN_SOURCE_FORMAT_WAV,
  SUSCAN_SOURCE_FORMAT_RAW_UNSIGNED8,
  SUSCAN_SOURCE_FORMAT_RAW_SIGNED16,
  SUSCAN_SOURCE_FORMAT_RAW_SIGNED8,
  SUSCAN_SOURCE_FORMAT_SIGMF
};

#define SUSCAN_SOURCE_CONFIG_GUESS_FREQ       (1 << 0)
#define SUSCAN_SOURCE_CONFIG_GUESS_SAMP_RATE  (1 << 1)
#define SUSCAN_SOURCE_CONFIG_GUESS_START_TIME (1 << 2)
#define SUSCAN_SOURCE_CONFIG_GUESS_IS_UTC     (1 << 3)
#define SUSCAN_SOURCE_CONFIG_GUESS_FORMAT     (1 << 4)
#define SUSCAN_SOURCE_CONFIG_GUESS_SIZE       (1 << 5)

#define SUSCAN_SOURCE_FORMAT_FALLBACK SUSCAN_SOURCE_FORMAT_RAW_FLOAT32

struct suscan_source_gain_value {
  char   *name;
  SUFLOAT val;
};

struct suscan_source_metadata {
  SUFREQ         frequency;
  unsigned       sample_rate;
  struct timeval start_time;
  SUBOOL         is_utc;
  enum suscan_source_format format;
  SUSDIFF        size;

  uint32_t       guessed;
};

SUSCAN_SERIALIZABLE(suscan_source_config) {
  char *type;
  enum suscan_source_format format;
  char *label; /* Label for this configuration */

  /* Common for all source types */
  SUFREQ  freq;
  SUFREQ  lnb_freq;
  SUFLOAT bandwidth;
  SUBOOL  iq_balance;
  SUBOOL  dc_remove;
  SUFLOAT ppm;
  struct timeval start_time;
  unsigned int samp_rate;
  unsigned int average;

  /* For file sources */
  char *path;
  SUBOOL loop;

  /* For real time sources */
  suscan_device_spec_t *device_spec;
  char *antenna;
  unsigned int channel;
  PTR_LIST(struct suscan_source_gain_value, gain);
  PTR_LIST(struct suscan_source_gain_value, hidden_gain);
};

typedef struct suscan_source_config suscan_source_config_t;

SUBOOL suscan_source_config_deserialize_ex(
    struct suscan_source_config *self,
    grow_buf_t *buffer,
    const char *force_host);

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

const char *suscan_source_config_get_type(
    const suscan_source_config_t *config);

enum suscan_source_format suscan_source_config_get_format(
    const suscan_source_config_t *config);

SUBOOL suscan_source_config_set_type_format(
    suscan_source_config_t *config,
    const char *type,
    enum suscan_source_format format);

SUFREQ suscan_source_config_get_freq(const suscan_source_config_t *config);
void suscan_source_config_set_freq(
    suscan_source_config_t *config,
    SUFREQ freq);

SUFREQ suscan_source_config_get_lnb_freq(const suscan_source_config_t *config);
void suscan_source_config_set_lnb_freq(
    suscan_source_config_t *config,
    SUFREQ lnb_freq);

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
SUBOOL suscan_source_config_file_is_valid(const suscan_source_config_t *self);
SUBOOL suscan_source_config_is_real_time(const suscan_source_config_t *self);
SUBOOL suscan_source_config_guess_metadata(
    const suscan_source_config_t *self,
    struct suscan_source_metadata *metadata);
SUBOOL suscan_source_config_is_seekable(const suscan_source_config_t *self);
SUBOOL suscan_source_config_get_end_time(
  const suscan_source_config_t *self,
  struct timeval *tv);
SUBOOL suscan_source_config_get_freq_limits(
    const suscan_source_config_t *self,
    SUFREQ *min,
    SUFREQ *max);
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

const char *suscan_source_config_get_interface(
    const suscan_source_config_t *self);

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

SUBOOL suscan_source_config_walk_gains_ex(
    const suscan_source_config_t *config,
    SUBOOL (*gain_cb) (void *privdata, struct suscan_source_gain_value *),
    void *privdata);

struct suscan_source_gain_value *suscan_source_config_upsert_gain(
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

SUFLOAT suscan_source_config_get_ppm(const suscan_source_config_t *config);
void suscan_source_config_set_ppm(
    suscan_source_config_t *config,
    SUFLOAT ppm);

void suscan_source_config_get_start_time(
  const suscan_source_config_t *config,
  struct timeval *tv);
void suscan_source_config_set_start_time(
    suscan_source_config_t *config,
    struct timeval tv);

SUBOOL suscan_source_config_set_device_spec(
    suscan_source_config_t *config,
    const suscan_device_spec_t *spec);

SUINLINE const suscan_device_spec_t *
suscan_source_config_get_device_spec(const suscan_source_config_t *config)
{
  return config->device_spec;
}

SUINLINE const char *
suscan_source_config_get_param(
    const suscan_source_config_t *self,
    const char *key)
{
  return suscan_device_spec_get(self->device_spec, key);
}

SUINLINE SUBOOL
suscan_source_config_set_param(
    const suscan_source_config_t *self,
    const char *key,
    const char *value)
{
  return suscan_device_spec_set(self->device_spec, key, value);
}

SUBOOL
suscan_source_config_walk_params(
  const suscan_source_config_t *self,
  SUBOOL (*callback) (
    const suscan_source_config_t *self,
    const char *key,
    const char *value,
    void *userdata),
  void *userdata);

SUINLINE void
suscan_source_config_clear_params(suscan_source_config_t *self)
{
  suscan_device_spec_reset(self->device_spec);
}

SUINLINE SUBOOL
suscan_source_config_is_remote(const suscan_source_config_t *self)
{
  return strcmp(
    suscan_device_spec_analyzer(self->device_spec),
    SUSCAN_SOURCE_REMOTE_INTERFACE) == 0;
}

suscan_source_config_t *suscan_source_config_new_default(void);
suscan_source_config_t *suscan_source_config_new(
    const char *type,
    enum suscan_source_format format);

suscan_source_config_t *suscan_source_config_clone(
    const suscan_source_config_t *config);

void suscan_source_config_swap(
    suscan_source_config_t *config1,
    suscan_source_config_t *config2);

suscan_source_config_t *suscan_source_config_lookup(const char *label);

SUBOOL suscan_source_config_unregister(suscan_source_config_t *config);

void suscan_source_config_destroy(suscan_source_config_t *);

SUBOOL suscan_load_sources(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _ANALYZER_SOURCE_CONFIG_H */
