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

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <libgen.h>

#define SU_LOG_DOMAIN "source"
#include <confdb.h>
#include "source.h"

#ifdef _SU_SINGLE_PRECISION
#  define sf_read sf_read_float
#  define SUSCAN_SOAPY_SAMPFMT SOAPY_SDR_CF32
#else
#  define sf_read sf_read_double
#  define SUSCAN_SOAPY_SAMPFMT SOAPY_SDR_CF64
#endif

#ifdef bool /* Someone is using this for whatever reason */
#  undef bool
#endif /* bool */

/* Private config list */
PTR_LIST(SUPRIVATE suscan_source_config_t, config);

/* Private device list */
PTR_LIST(SUPRIVATE suscan_source_device_t, device);

/* Hidden gain list */
PTR_LIST(SUPRIVATE struct suscan_source_gain_desc, hidden_gain);

/* Null device */
SUPRIVATE suscan_source_device_t *null_device;

/******************************* Source devices ******************************/
SUPRIVATE void
suscan_source_gain_desc_destroy(struct suscan_source_gain_desc *desc)
{
  if (desc->name != NULL)
    free(desc->name);

  free(desc);
}

SUPRIVATE struct suscan_source_gain_desc *
suscan_source_gain_desc_new(const char *name, SUFLOAT min, SUFLOAT max)
{
  struct suscan_source_gain_desc *new = NULL;

  SU_TRYCATCH(min <= max, return NULL);

  SU_TRYCATCH(
      new = calloc(1, sizeof(struct suscan_source_gain_desc)),
      goto fail);

  SU_TRYCATCH(new->name = strdup(name), goto fail);

  new->min = min;
  new->max = max;

  return new;

fail:
  if (new != NULL)
    suscan_source_gain_desc_destroy(new);

  return NULL;
}

/* Create ad-hoc hidden gains. */
SUPRIVATE const struct suscan_source_gain_desc *
suscan_source_gain_desc_new_hidden(const char *name, SUFLOAT value)
{
  struct suscan_source_gain_desc *new = NULL;

  SU_TRYCATCH(new = suscan_source_gain_desc_new(name, value, value), goto fail);

  SU_TRYCATCH(PTR_LIST_APPEND_CHECK(hidden_gain, new) != -1, goto fail);

  return new;

fail:
  if (new != NULL)
    suscan_source_gain_desc_destroy(new);

  return NULL;
}

SUPRIVATE const struct suscan_source_gain_desc *
suscan_source_device_lookup_gain_desc(
    const suscan_source_device_t *dev,
    const char *name)
{
  unsigned int i;

  printf("Looking up gain %s in %d gains\n", name, dev->gain_desc_count);
  for (i = 0; i < dev->gain_desc_count; ++i)
    if (strcmp(dev->gain_desc_list[i]->name, name) == 0)
      return dev->gain_desc_list[i];

  return NULL;
}

SUPRIVATE void
suscan_source_device_destroy(suscan_source_device_t *dev)
{
  unsigned int i;

  for (i = 0; i < dev->antenna_count; ++i)
    if (dev->antenna_list[i] != NULL)
      free(dev->antenna_list[i]);

  if (dev->antenna_list != NULL)
    free(dev->antenna_list);

  for (i = 0; i < dev->gain_desc_count; ++i)
    if (dev->gain_desc_list[i] != NULL)
      free(dev->gain_desc_list[i]);

  if (dev->gain_desc_list != NULL)
    free(dev->gain_desc_list);

  if (dev->desc != NULL)
    free(dev->desc);

  if (dev->args != NULL) {
    SoapySDRKwargs_clear(dev->args);
    free(dev->args);
  }

  free(dev);
}

void
suscan_source_device_info_finalize(struct suscan_source_device_info *info)
{
  /* NO-OP. Keeping method in case we add dynamic members in the future */
}

SUPRIVATE char *
suscan_source_device_build_desc(const char *driver, const char *label)
{
  if (label == NULL)
    label = "Unlabeled device";

  if (strcmp(driver, "audio") == 0)
    return strbuild("Audio input (%s)", label);
  else if (strcmp(driver, "hackrf") == 0)
    return strbuild("HackRF One (%s)", label);
  else if(strcmp(driver, "null") == 0)
    return strdup("Dummy device");
  return strbuild("%s (%s)", driver, label);
}

SUPRIVATE SUBOOL
suscan_source_device_populate_info(suscan_source_device_t *dev)
{
  SoapySDRDevice *sdev = NULL;
  struct suscan_source_gain_desc *desc = NULL;
  SoapySDRRange range;
  char **antenna_list = NULL;
  char **gain_list = NULL;
  char *dup = NULL;
  size_t antenna_count = 0;
  size_t gain_count = 0;
  unsigned int i;

  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(sdev = SoapySDRDevice_make(dev->args), goto done);

  /* Duplicate antenna list */
  if ((antenna_list = SoapySDRDevice_listAntennas(
          sdev,
          SOAPY_SDR_RX,
          0,
          &antenna_count)) != NULL) {
    for (i = 0; i < antenna_count; ++i) {
      SU_TRYCATCH(dup = strdup(antenna_list[i]), goto done);
      SU_TRYCATCH(PTR_LIST_APPEND_CHECK(dev->antenna, dup) != -1, goto done);
      dup = NULL;
    }
  }

  /* Duplicate gain list */
  if ((gain_list = SoapySDRDevice_listGains(
          sdev,
          SOAPY_SDR_RX,
          0,
          &gain_count)) != NULL) {
    for (i = 0; i < gain_count; ++i) {
      range = SoapySDRDevice_getGainElementRange(
          sdev,
          SOAPY_SDR_RX,
          0,
          gain_list[i]);
      SU_TRYCATCH(
          desc = suscan_source_gain_desc_new(
              gain_list[i],
              range.minimum,
              range.maximum),
          goto done);

      /* This may change in the future */
      desc->step = 1;
      desc->def = SoapySDRDevice_getGainElement(
          sdev,
          SOAPY_SDR_RX,
          0,
          gain_list[i]);

      SU_TRYCATCH(PTR_LIST_APPEND_CHECK(dev->gain_desc, desc) != -1, goto done);
      desc = NULL;
    }
  }

  ok = SU_TRUE;

done:
  if (dup != NULL)
    free(dup);

  SoapySDRStrings_clear(&antenna_list, antenna_count);
  SoapySDRStrings_clear(&gain_list, gain_count);

  if (desc != NULL)
    suscan_source_gain_desc_destroy(desc);

  if (sdev != NULL)
    SoapySDRDevice_unmake(sdev);

  return ok;
}

/* FIXME: This is awful. Plase change constness of 1st arg ASAP */
SUBOOL
suscan_source_device_get_info(
    const suscan_source_device_t *dev,
    unsigned int channel,
    struct suscan_source_device_info *info)
{
  if (!suscan_source_device_is_populated(dev)) {
    SU_TRYCATCH(
        suscan_source_device_populate_info((suscan_source_device_t *) dev),
        return SU_FALSE);

  }

  info->gain_desc_list = (const struct suscan_source_gain_desc **) dev->gain_desc_list;
  info->gain_desc_count = dev->gain_desc_count;

  info->antenna_list = (const char **) dev->antenna_list;
  info->antenna_count = dev->antenna_count;

  return SU_TRUE;
}

SUPRIVATE suscan_source_device_t *
suscan_source_device_new(const SoapySDRKwargs *args)
{
  suscan_source_device_t *new = NULL;
  const char *driver;
  unsigned int i;

  /* Not necessarily an error */
  if ((driver = SoapySDRKwargs_get((SoapySDRKwargs *) args, "driver")) == NULL)
    return NULL;

  SU_TRYCATCH(new = calloc(1, sizeof (suscan_source_device_t)), goto fail);
  SU_TRYCATCH(
      new->desc = suscan_source_device_build_desc(
          driver,
          SoapySDRKwargs_get((SoapySDRKwargs *) args, "label")),
      goto fail);

  SU_TRYCATCH(new->args = calloc(1, sizeof (SoapySDRKwargs)), goto fail);
  for (i = 0; i < args->size; ++i) {
    /* DANGER DANGER DANGER */
    SoapySDRKwargs_set(new->args, args->keys[i], args->vals[i]);
    /* DANGER DANGER DANGER */
  }

  new->driver = driver;
  new->index = -1;

  return new;

fail:
  if (new != NULL)
    suscan_source_device_destroy(new);

  return NULL;
}

SUBOOL
suscan_source_device_walk(
    SUBOOL (*function) (
        const suscan_source_device_t *dev,
        unsigned int index,
        void *private),
    void *private)
{
  unsigned int i;

  for (i = 0; i < device_count; ++i)
    if (device_list[i] != NULL)
      if (!(function)(device_list[i], i, private))
        return SU_FALSE;

  return SU_TRUE;
}

const suscan_source_device_t *
suscan_source_device_get_by_index(unsigned int index)
{
  if (index >= device_count)
    return NULL;

  return device_list[index];
}

unsigned int
suscan_source_device_get_count(void)
{
  return device_count;
}

#ifdef SUSCAN_DEBUG_KWARGS
SUPRIVATE void
debug_kwargs(const SoapySDRKwargs *a)
{
  unsigned int i;

  for (i = 0; i < a->size; ++i)
    printf("%s=%s,", a->keys[i], a->vals[i]);

  putchar(10);
}
#endif /* SUSCAN_DEBUG_KWARGS */

SUPRIVATE SUBOOL
suscan_source_device_soapy_args_are_equal(
    const SoapySDRKwargs *a,
    const SoapySDRKwargs *b)
{
  const char *val;
  unsigned int i;

#ifdef SUSCAN_DEBUG_KWARGS
  printf("Compare: ");
  debug_kwargs(a);
  debug_kwargs(b);
#endif /* SUSCAN_DEBUG_KWARGS */

  if (a->size == b->size) {
    for (i = 0; i < a->size; ++i) {
      val = SoapySDRKwargs_get((SoapySDRKwargs *) b, a->keys[i]);
      if (val == NULL) {
#ifdef SUSCAN_DEBUG_KWARGS
        printf("Value %s not present!\n", a->keys[i]);
#endif /* SUSCAN_DEBUG_KWARGS */
        return SU_FALSE;
      }
      if (strcmp(a->vals[i], val) != 0) {
#ifdef SUSCAN_DEBUG_KWARGS
        printf("Value %s is different (%s and %s)!\n", a->keys[i], a->vals[i], val);
#endif /* SUSCAN_DEBUG_KWARGS */
        return SU_FALSE;
      }
    }

    /* All of them are equal, consider a the same as b */
    return SU_TRUE;
  }

  return SU_FALSE;
}

SUPRIVATE int
suscan_source_device_assert_index(const SoapySDRKwargs *args)
{
  int i;
  suscan_source_device_t *dev = NULL;

  if (args->size == 0)
    return null_device->index;

  for (i = 0; i < device_count; ++i)
    if (suscan_source_device_soapy_args_are_equal(device_list[i]->args, args))
      goto done;

  i = -1;

  if ((dev = suscan_source_device_new(args)) != NULL) {
    SU_TRYCATCH(
        (i = dev->index = PTR_LIST_APPEND_CHECK(device, dev)) != -1,
        goto done);
    dev = NULL;
  }

done:
  if (dev != NULL)
    suscan_source_device_destroy(dev);

  return i;
}

SUPRIVATE suscan_source_device_t *
suscan_source_device_assert(const SoapySDRKwargs *args)
{
  int index;

  if ((index = suscan_source_device_assert_index(args)) == -1)
    return NULL;

  return device_list[index];
}

SUPRIVATE SUBOOL
suscan_source_register_null_device(void)
{
  SoapySDRKwargs args;
  suscan_source_device_t *dev;

  char *keys[] = {"driver"};
  char *vals[] = {"null"};

  args.size = 1;
  args.keys = keys;
  args.vals = vals;

  SU_TRYCATCH(dev = suscan_source_device_assert(&args), return SU_FALSE);

  null_device = dev;

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_source_detect_devices(void)
{
  SoapySDRKwargs *soapy_dev_list = NULL;
  suscan_source_device_t *dev = NULL;
  size_t soapy_dev_len;
  unsigned int i;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(suscan_source_register_null_device(), goto done);

  SU_TRYCATCH(
      soapy_dev_list = SoapySDRDevice_enumerate(NULL, &soapy_dev_len),
      goto done);

  for (i = 0; i < soapy_dev_len; ++i) {
    SU_TRYCATCH(
        dev = suscan_source_device_assert(soapy_dev_list + i),
        goto done);

    /*
     * Populate device info. If this fails, don't pass exception:
     * there may be a problem with this device, but not with the rest of them.
     */
    if (!suscan_source_device_is_populated(dev)) {
      SU_TRYCATCH(suscan_source_device_populate_info(dev), continue);
    }
  }

  ok = SU_TRUE;

done:
  if (soapy_dev_list != NULL)
    SoapySDRKwargsList_clear(soapy_dev_list, soapy_dev_len);

  return ok;
}

const suscan_source_device_t *
suscan_source_get_null_device(void)
{
  return null_device;
}

/***************************** Source Config API *****************************/
SUBOOL
suscan_source_config_walk(
    SUBOOL (*function) (suscan_source_config_t *cfg, void *private),
    void *private)
{
  unsigned int i;

  for (i = 0; i < config_count; ++i)
    if (config_list[i] != NULL)
      if (!(function)(config_list[i], private))
        return SU_FALSE;

  return SU_TRUE;
}

suscan_source_config_t *
suscan_source_config_lookup(const char *label)
{
  unsigned int i;

  for (i = 0; i < config_count; ++i)
    if (config_list[i] != NULL && config_list[i]->label != NULL)
      if (strcmp(config_list[i]->label, label) == 0)
        return config_list[i];

  return NULL;
}

/* This is just an unregister function. Nothing is destroyed. */
SUBOOL
suscan_source_config_unregister(suscan_source_config_t *config)
{
  unsigned int i;

  for (i = 0; i < config_count; ++i)
    if (config_list[i] == config) {
      config_list[i] = NULL;
      return SU_TRUE;
    }

  return SU_FALSE;
}

SUPRIVATE void
suscan_source_gain_value_destroy(struct suscan_source_gain_value *kv)
{
  free(kv);
}

SUPRIVATE struct suscan_source_gain_value *
suscan_source_gain_value_new(
    const struct suscan_source_gain_desc *desc,
    SUFLOAT val)
{
  struct suscan_source_gain_value *new = NULL;

  SU_TRYCATCH(
      new = malloc(sizeof(struct suscan_source_gain_value)),
      goto fail);

  new->desc = desc;

  if (val < desc->min)
    val = desc->min;
  if (val > desc->max)
    val = desc->max;

  new->val = val;

  return new;

fail:
  if (new != NULL)
    suscan_source_gain_value_destroy(new);

  return NULL;
}

void
suscan_source_config_destroy(suscan_source_config_t *config)
{
  unsigned int i;

  if (config->label != NULL)
    free(config->label);

  if (config->path != NULL)
    free(config->path);

  if (config->soapy_args != NULL) {
    SoapySDRKwargs_clear(config->soapy_args);
    free(config->soapy_args);
  }

  if (config->antenna != NULL)
    free(config->antenna);

  for (i = 0; i < config->gain_count; ++i)
    if (config->gain_list[i] != NULL)
      suscan_source_gain_value_destroy(config->gain_list[i]);

  if (config->gain_list != NULL)
    free(config->gain_list);

  for (i = 0; i < config->hidden_gain_count; ++i)
    if (config->hidden_gain_list[i] != NULL)
      suscan_source_gain_value_destroy(config->hidden_gain_list[i]);

  if (config->hidden_gain_list != NULL)
    free(config->hidden_gain_list);

  free(config);
}

/* Getters & Setters */
SUBOOL
suscan_source_config_set_label(suscan_source_config_t *config, const char *label)
{
  char *dup = NULL;

  if (label != NULL)
    SU_TRYCATCH(dup = strdup(label), return SU_FALSE);

  if (config->label != NULL)
    free(config->label);

  config->label = dup;

  return SU_TRUE;
}

enum suscan_source_type
suscan_source_config_get_type(const suscan_source_config_t *config)
{
  return config->type;
}

enum suscan_source_format
suscan_source_config_get_format(const suscan_source_config_t *config)
{
  return config->format;
}

void
suscan_source_config_set_type_format(
    suscan_source_config_t *config,
    enum suscan_source_type type,
    enum suscan_source_format format)
{
  config->type = type;
  config->format = format;
}

const char *
suscan_source_config_get_label(const suscan_source_config_t *config)
{
  if (config->label != NULL)
    return config->label;
  else
    return "Unlabeled source";
}

SUFREQ
suscan_source_config_get_freq(const suscan_source_config_t *config)
{
  return config->freq;
}

void
suscan_source_config_set_freq(suscan_source_config_t *config, SUFREQ freq)
{
  config->freq = freq;
}

SUFLOAT
suscan_source_config_get_bandwidth(const suscan_source_config_t *config)
{
  return config->bandwidth;
}

void
suscan_source_config_set_bandwidth(
    suscan_source_config_t *config,
    SUFLOAT bandwidth)
{
  config->bandwidth = bandwidth;
}

SUBOOL
suscan_source_config_get_iq_balance(const suscan_source_config_t *config)
{
  return config->iq_balance;
}

void
suscan_source_config_set_iq_balance(
    suscan_source_config_t *config,
    SUBOOL iq_balance)
{
  config->iq_balance = iq_balance;
}

SUBOOL
suscan_source_config_get_dc_remove(const suscan_source_config_t *config)
{
  return config->dc_remove;
}

void
suscan_source_config_set_dc_remove(
    suscan_source_config_t *config,
    SUBOOL dc_remove)
{
  config->dc_remove = dc_remove;
}

SUBOOL
suscan_source_config_get_loop(const suscan_source_config_t *config)
{
  return config->loop;
}

void
suscan_source_config_set_loop(suscan_source_config_t *config, SUBOOL loop)
{
  config->loop = loop;
}

const char *
suscan_source_config_get_path(const suscan_source_config_t *config)
{
  return config->path;
}

SUBOOL
suscan_source_config_set_path(suscan_source_config_t *config, const char *path)
{
  char *dup = NULL;

  if (path != NULL)
    SU_TRYCATCH(dup = strdup(path), return SU_FALSE);

  if (config->path != NULL)
    free(config->path);

  config->path = dup;

  return SU_TRUE;
}

const char *
suscan_source_config_get_antenna(const suscan_source_config_t *config)
{
  return config->antenna;
}

SUBOOL
suscan_source_config_set_antenna(
    suscan_source_config_t *config,
    const char *antenna)
{
  char *dup = NULL;

  if (antenna != NULL)
    SU_TRYCATCH(dup = strdup(antenna), return SU_FALSE);

  if (config->antenna != NULL)
    free(config->antenna);

  config->antenna = dup;

  return SU_TRUE;
}

unsigned int
suscan_source_config_get_samp_rate(const suscan_source_config_t *config)
{
  return config->samp_rate;
}

void
suscan_source_config_set_samp_rate(
    suscan_source_config_t *config,
    unsigned int samp_rate)
{
  config->samp_rate = samp_rate;
}

unsigned int
suscan_source_config_get_average(const suscan_source_config_t *config)
{
  return config->average;
}

SUBOOL
suscan_source_config_set_average(
    suscan_source_config_t *config,
    unsigned int average)
{
  if (average < 1) {
    SU_ERROR("Cannot set average to less than 1\n");
    return SU_FALSE;
  }

  config->average = average;

  return SU_TRUE;
}

unsigned int
suscan_source_config_get_channel(const suscan_source_config_t *config)
{
  return config->channel;
}

void
suscan_source_config_set_channel(
    suscan_source_config_t *config,
    unsigned int channel)
{
  config->channel = channel;
}

struct suscan_source_gain_value *
suscan_source_config_lookup_gain(
    const suscan_source_config_t *config,
    const char *name)
{
  unsigned int i;

  for (i = 0; i < config->gain_count; ++i)
    if (strcmp(config->gain_list[i]->desc->name, name) == 0)
      return config->gain_list[i];

  for (i = 0; i < config->hidden_gain_count; ++i)
    if (strcmp(config->hidden_gain_list[i]->desc->name, name) == 0)
      return config->hidden_gain_list[i];

  return NULL;
}

SUBOOL
suscan_source_config_walk_gains(
    const suscan_source_config_t *config,
    SUBOOL (*gain_cb) (void *private, const char *name, SUFLOAT value),
    void *private)
{
  unsigned int i;

  for (i = 0; i < config->gain_count; ++i)
    if (!(gain_cb) (
        private,
        config->gain_list[i]->desc->name,
        config->gain_list[i]->val))
      return SU_FALSE;

  return SU_TRUE;
}

struct suscan_source_gain_value *
suscan_source_config_assert_gain(
    suscan_source_config_t *config,
    const char *name,
    SUFLOAT value)
{
  struct suscan_source_gain_value *gain;
  const struct suscan_source_gain_desc *desc;
  SUBOOL hidden = SU_FALSE;

  if ((gain = suscan_source_config_lookup_gain(config, name)) != NULL)
    return gain;

  SU_TRYCATCH(config->device != NULL, goto fail);

  if ((desc = suscan_source_device_lookup_gain_desc(
      config->device,
      name)) == NULL) {
    /*
     * Gain is not present in this device. However, it has been explicitly
     * asserted. We register it as a hidden gain, just to keep it when
     * configuration is serialized.
     */
    SU_TRYCATCH(
        desc = suscan_source_gain_desc_new_hidden(name, value),
        goto fail);

    hidden = SU_TRUE;
  }

  SU_TRYCATCH(gain = suscan_source_gain_value_new(desc, value), goto fail);

  if (hidden) {
    SU_TRYCATCH(
        PTR_LIST_APPEND_CHECK(config->hidden_gain, gain) != -1,
        goto fail);
  } else {
    SU_TRYCATCH(
        PTR_LIST_APPEND_CHECK(config->gain, gain) != -1,
        goto fail);
  }

  return gain;

fail:
  if (gain != NULL)
    suscan_source_gain_value_destroy(gain);

  return NULL;
}

SUFLOAT
suscan_source_config_get_gain(
    const suscan_source_config_t *config,
    const char *name)
{
  struct suscan_source_gain_value *gain;

  if ((gain = suscan_source_config_lookup_gain(config, name)) == NULL)
    return 0;

  return gain->val;
}

SUBOOL
suscan_source_config_set_gain(
    suscan_source_config_t *config,
    const char *name,
    SUFLOAT value)
{
  struct suscan_source_gain_value *gain;

  if ((gain = suscan_source_config_assert_gain(config, name, value)) == NULL)
    return SU_FALSE;

  gain->val = value;

  return SU_TRUE;
}

SUBOOL
suscan_source_config_set_device(
    suscan_source_config_t *config,
    const suscan_source_device_t *dev)
{
  unsigned int i;

  /*
   * TODO: Once this API is fixed, allocate new soapy_args and replace
   * the old ones.
   */
  SoapySDRKwargs_clear(config->soapy_args);

  for (i = 0; i < dev->args->size; ++i) {
    /* ----8<----------------- DANGER DANGER DANGER ----8<----------------- */
    SoapySDRKwargs_set(
        config->soapy_args,
        dev->args->keys[i],
        dev->args->vals[i]);
    /* ----8<----------------- DANGER DANGER DANGER ----8<----------------- */
  }

  config->device = dev;

  /* Fuck off */
  return SU_TRUE;
}

suscan_source_config_t *
suscan_source_config_new(
    enum suscan_source_type type,
    enum suscan_source_format format)
{
  suscan_source_config_t *new = NULL;

  SU_TRYCATCH(new = calloc(1, sizeof(suscan_source_config_t)), goto fail);

  new->type = type;
  new->format = format;
  new->average = 1;

  SU_TRYCATCH(new->soapy_args = calloc(1, sizeof(SoapySDRKwargs)), goto fail);

  SU_TRYCATCH(
      suscan_source_config_set_device(new, suscan_source_get_null_device()),
      goto fail);

  return new;

fail:
  if (new != NULL)
    suscan_source_config_destroy(new);

  return NULL;
}

suscan_source_config_t *
suscan_source_config_clone(const suscan_source_config_t *config)
{
  suscan_source_config_t *new = NULL;
  unsigned int i;

  SU_TRYCATCH(
      new = suscan_source_config_new(config->type, config->format),
      goto fail);

  SU_TRYCATCH(suscan_source_config_set_label(new, config->label), goto fail);
  SU_TRYCATCH(suscan_source_config_set_path(new, config->path), goto fail);
  SU_TRYCATCH(
        suscan_source_config_set_antenna(new, config->antenna),
        goto fail);

  new->device = config->device;

  for (i = 0; i < config->gain_count; ++i)
    SU_TRYCATCH(
        suscan_source_config_set_gain(
            new,
            config->gain_list[i]->desc->name,
            config->gain_list[i]->val),
        goto fail);

  /* Copy hidden gains too */
  for (i = 0; i < config->hidden_gain_count; ++i)
    SU_TRYCATCH(
        suscan_source_config_set_gain(
            new,
            config->hidden_gain_list[i]->desc->name,
            config->hidden_gain_list[i]->val),
        goto fail);

  if (suscan_source_config_get_type(config) == SUSCAN_SOURCE_TYPE_SDR) {
    for (i = 0; i < config->soapy_args->size; ++i) {
      /* ----8<----------------- DANGER DANGER DANGER ----8<----------------- */
      SoapySDRKwargs_set(
          new->soapy_args,
          config->soapy_args->keys[i],
          config->soapy_args->vals[i]);
      /* ----8<----------------- DANGER DANGER DANGER ----8<----------------- */
    }
  }

  new->freq = config->freq;
  new->bandwidth = config->bandwidth;
  new->iq_balance = config->iq_balance;
  new->dc_remove = config->dc_remove;
  new->samp_rate = config->samp_rate;
  new->average = config->average;
  new->channel = config->channel;
  new->loop = config->loop;

  return new;

fail:
  if (new != NULL)
    suscan_source_config_destroy(new);

  return NULL;
}

SUPRIVATE const char *
suscan_source_config_helper_type_to_str(enum suscan_source_type type)
{
  switch (type) {
    case SUSCAN_SOURCE_TYPE_FILE:
      return "FILE";

    case SUSCAN_SOURCE_TYPE_SDR:
      return "SDR";
  }

  return NULL;
}

SUPRIVATE enum suscan_source_type
suscan_source_type_config_helper_str_to_type(const char *type)
{
  if (type != NULL) {
    if (strcasecmp(type, "FILE") == 0)
      return SUSCAN_SOURCE_TYPE_FILE;
    else if (strcasecmp(type, "SDR") == 0)
      return SUSCAN_SOURCE_TYPE_SDR;
  }

  return SUSCAN_SOURCE_TYPE_SDR;
}

SUPRIVATE const char *
suscan_source_config_helper_format_to_str(enum suscan_source_format type)
{
  switch (type) {
    case SUSCAN_SOURCE_FORMAT_AUTO:
      return "AUTO";

    case SUSCAN_SOURCE_FORMAT_RAW:
      return "RAW";

    case SUSCAN_SOURCE_FORMAT_WAV:
      return "WAV";
  }

  return NULL;
}

SUPRIVATE enum suscan_source_format
suscan_source_type_config_helper_str_to_format(const char *format)
{
  if (format != NULL) {
    if (strcasecmp(format, "AUTO") == 0)
      return SUSCAN_SOURCE_FORMAT_AUTO;
    else if (strcasecmp(format, "RAW") == 0)
      return SUSCAN_SOURCE_FORMAT_RAW;
    else if (strcasecmp(format, "WAV") == 0)
      return SUSCAN_SOURCE_FORMAT_WAV;
  }

  return SUSCAN_SOURCE_FORMAT_AUTO;
}

suscan_object_t *
suscan_source_config_to_object(const suscan_source_config_t *cfg)
{
  suscan_object_t *new = NULL;
  suscan_object_t *obj = NULL;
  unsigned int i;

  const char *tmp;

#define SU_CFGSAVE(kind, field)                                 \
    SU_TRYCATCH(                                                \
        JOIN(suscan_object_set_field_, kind)(                   \
            new,                                                \
            STRINGIFY(field),                                   \
            cfg->field),                                        \
        goto fail)

  SU_TRYCATCH(new = suscan_object_new(SUSCAN_OBJECT_TYPE_OBJECT), goto fail);

  SU_TRYCATCH(suscan_object_set_class(new, "source_config"), goto fail);

  SU_TRYCATCH(
      tmp = suscan_source_config_helper_type_to_str(cfg->type),
      goto fail);

  SU_TRYCATCH(suscan_object_set_field_value(new, "type", tmp), goto fail);

  if (cfg->type == SUSCAN_SOURCE_TYPE_FILE) {
    SU_TRYCATCH(
          tmp = suscan_source_config_helper_format_to_str(cfg->format),
          goto fail);
    SU_TRYCATCH(suscan_object_set_field_value(new, "format", tmp), goto fail);
  }

  if (cfg->label != NULL)
    SU_CFGSAVE(value, label);

  if (cfg->path != NULL)
    SU_CFGSAVE(value, path);

  if (cfg->antenna != NULL)
    SU_CFGSAVE(value, antenna);

  SU_CFGSAVE(float, freq);
  SU_CFGSAVE(float, bandwidth);
  SU_CFGSAVE(bool,  iq_balance);
  SU_CFGSAVE(bool,  dc_remove);
  SU_CFGSAVE(bool,  loop);
  SU_CFGSAVE(uint,  samp_rate);
  SU_CFGSAVE(uint,  average);
  SU_CFGSAVE(uint,  channel);

  /* Save SoapySDR kwargs */
  SU_TRYCATCH(obj = suscan_object_new(SUSCAN_OBJECT_TYPE_OBJECT), goto fail);

  if (suscan_source_config_get_type(cfg) == SUSCAN_SOURCE_TYPE_SDR) {
    for (i = 0; i < cfg->soapy_args->size; ++i)
      SU_TRYCATCH(
          suscan_object_set_field_value(
              obj,
              cfg->soapy_args->keys[i],
              cfg->soapy_args->vals[i]),
          goto fail);
  }

  SU_TRYCATCH(suscan_object_set_field(new, "sdr_args", obj), goto fail);
  obj = NULL;

  /* Save gains */
  SU_TRYCATCH(obj = suscan_object_new(SUSCAN_OBJECT_TYPE_OBJECT), goto fail);

  if (suscan_source_config_get_type(cfg) == SUSCAN_SOURCE_TYPE_SDR) {
    /* Save visible gains */
    for (i = 0; i < cfg->gain_count; ++i)
      SU_TRYCATCH(
          suscan_object_set_field_float(
              obj,
              cfg->gain_list[i]->desc->name,
              cfg->gain_list[i]->val),
          goto fail);

    /* Save hidden gains */
    for (i = 0; i < cfg->hidden_gain_count; ++i)
      SU_TRYCATCH(
          suscan_object_set_field_float(
              obj,
              cfg->hidden_gain_list[i]->desc->name,
              cfg->hidden_gain_list[i]->val),
          goto fail);
  }

  SU_TRYCATCH(suscan_object_set_field(new, "gains", obj), goto fail);
  obj = NULL;

  return new;

fail:
  if (obj != NULL)
    suscan_object_destroy(obj);

  if (new != NULL)
    suscan_object_destroy(new);

  return NULL;
}

suscan_source_config_t *
suscan_source_config_from_object(const suscan_object_t *object)
{
  suscan_source_config_t *new = NULL;
  suscan_source_device_t *device = NULL;
  suscan_object_t *obj, *entry = NULL;
  unsigned int i, count;
  SUFLOAT val;

  const char *tmp;

#define SU_CFGLOAD(kind, field, dfl)                            \
        JOIN(suscan_source_config_set_, field)(                 \
            new,                                                \
            JOIN(suscan_object_get_field_, kind)(               \
            object,                                             \
            STRINGIFY(field),                                   \
            dfl))

  SU_TRYCATCH(
      new = suscan_source_config_new(
          suscan_source_type_config_helper_str_to_type(
              suscan_object_get_field_value(object, "type")),
          suscan_source_type_config_helper_str_to_format(
              suscan_object_get_field_value(object, "format"))),
      goto fail);

  if ((tmp = suscan_object_get_field_value(object, "label")) != NULL)
    SU_TRYCATCH(suscan_source_config_set_label(new, tmp), goto fail);

  if ((tmp = suscan_object_get_field_value(object, "path")) != NULL)
    SU_TRYCATCH(suscan_source_config_set_path(new, tmp), goto fail);

  if ((tmp = suscan_object_get_field_value(object, "antenna")) != NULL)
    SU_TRYCATCH(suscan_source_config_set_antenna(new, tmp), goto fail);

  SU_CFGLOAD(float, freq, 0);
  SU_CFGLOAD(float, bandwidth, 0);
  SU_CFGLOAD(bool, iq_balance, SU_FALSE);
  SU_CFGLOAD(bool, dc_remove, SU_FALSE);
  SU_CFGLOAD(bool, loop, SU_FALSE);
  SU_CFGLOAD(uint, samp_rate, 1.8e6);
  SU_CFGLOAD(uint, channel, 0);

  SU_TRYCATCH(SU_CFGLOAD(uint, average, 1), goto fail);

  /* Set SDR args and gains, ONLY if this is a SDR source */
  if (suscan_source_config_get_type(new) == SUSCAN_SOURCE_TYPE_SDR) {
    if ((obj = suscan_object_get_field(object, "sdr_args")) != NULL)
      if (suscan_object_get_type(obj) == SUSCAN_OBJECT_TYPE_OBJECT) {
        count = suscan_object_field_count(obj);
        for (i = 0; i < count; ++i) {
          if ((entry = suscan_object_get_field_by_index(obj, i)) != NULL
              && suscan_object_get_type(entry) == SUSCAN_OBJECT_TYPE_FIELD) {
            /* ------------------- DANGER DANGER DANGER ------------------- */
            SoapySDRKwargs_set(
                new->soapy_args,
                suscan_object_get_name(entry),
                suscan_object_get_value(entry));
            /* ----------- HOW DO I EVEN KNOW IF THIS WORKED? ------------- */
          }
        }

        /* New device added. Assert it. */
        SU_TRYCATCH(
            new->device = device = suscan_source_device_assert(new->soapy_args),
            goto fail);

        /* This step is not critical, but we must try it anyways */
        if (!suscan_source_device_is_populated(device))
          SU_TRYCATCH(
              suscan_source_device_populate_info(device),
              SU_WARNING("Failed to populate device info\n"));
      }

    /* Retrieve gains */
    if ((obj = suscan_object_get_field(object, "gains")) != NULL)
      if (suscan_object_get_type(obj) == SUSCAN_OBJECT_TYPE_OBJECT) {
        count = suscan_object_field_count(obj);
        for (i = 0; i < count; ++i) {
          if ((entry = suscan_object_get_field_by_index(obj, i)) != NULL
              && suscan_object_get_type(entry) == SUSCAN_OBJECT_TYPE_FIELD) {
            if (sscanf(suscan_object_get_value(entry), "%g", &val) == 1)
              SU_TRYCATCH(
                  suscan_source_config_set_gain(
                      new,
                      suscan_object_get_name(entry),
                      val),
                  SU_WARNING(
                      "Profile-declared gain `%s' invalid\n",
                      suscan_object_get_name(entry)));
          }
        }

        /* New device added. Assert it. */
        SU_TRYCATCH(
            new->device = device = suscan_source_device_assert(new->soapy_args),
            goto fail);

        /* This step is not critical, but we must try it anyways */
        if (!suscan_source_device_is_populated(device))
          SU_TRYCATCH(
              suscan_source_device_populate_info(device),
              SU_WARNING("Failed to populate device info\n"));
      }
  }

  return new;

fail:
  if (new != NULL)
    suscan_source_config_destroy(new);

  return NULL;
}

/****************************** Source API ***********************************/
void
suscan_source_destroy(suscan_source_t *source)
{
  if (source->sf != NULL)
    sf_close(source->sf);

  if (source->rx_stream != NULL)
    SoapySDRDevice_closeStream(source->sdr, source->rx_stream);

  if (source->sdr != NULL)
    SoapySDRDevice_unmake(source->sdr);

  if (source->config != NULL)
    suscan_source_config_destroy(source->config);

  free(source);
}

SUPRIVATE SUBOOL
suscan_source_open_file(suscan_source_t *source)
{
  if (source->config->path == NULL) {
    SU_ERROR("Cannot open file source: path not set\n");
    return SU_FALSE;
  }

  switch (source->config->format) {
    case SUSCAN_SOURCE_FORMAT_WAV:
    case SUSCAN_SOURCE_FORMAT_AUTO:
      /* Autodetect: open as wav and, if failed, attempt to open as raw */
      source->sf_info.format = 0;
      if ((source->sf = sf_open(
          source->config->path,
          SFM_READ,
          &source->sf_info)) != NULL) {
        source->config->samp_rate = source->sf_info.samplerate;
        SU_INFO(
            "Audio file source opened, sample rate = %d\n",
            source->config->samp_rate);
        break;
      } else if (source->config->format == SUSCAN_SOURCE_FORMAT_WAV) {
        SU_ERROR(
            "Failed to open %s as audio file: %s\n",
            source->config->path,
            sf_strerror(NULL));
        return SU_FALSE;
      } else {
        SU_INFO("Failed to open source as audio file, falling back to raw...\n");
      }

    case SUSCAN_SOURCE_FORMAT_RAW:
      source->sf_info.format = SF_FORMAT_RAW | SF_FORMAT_FLOAT | SF_ENDIAN_LITTLE;
      source->sf_info.channels = 2;
      source->sf_info.samplerate = source->config->samp_rate;
      if ((source->sf = sf_open(
          source->config->path,
          SFM_READ,
          &source->sf_info)) == NULL) {
        source->config->samp_rate = source->sf_info.samplerate;
        SU_ERROR(
            "Failed to open %s as raw file: %s\n",
            source->config->path,
            sf_strerror(NULL));
        return SU_FALSE;
      }

      break;
  }

  source->iq_file = source->sf_info.channels == 2;

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_source_open_sdr(suscan_source_t *source)
{
  unsigned int i;

  if ((source->sdr = SoapySDRDevice_make(source->config->soapy_args)) == NULL) {
    SU_ERROR("Failed to open SDR device: %s\n", SoapySDRDevice_lastError());
    return SU_FALSE;
  }

  if (source->config->antenna != NULL)
    if (SoapySDRDevice_setAntenna(
        source->sdr,
        SOAPY_SDR_RX,
        source->config->channel,
        source->config->antenna) != 0) {
      SU_ERROR("Failed to set SDR antenna: %s\n", SoapySDRDevice_lastError());
      return SU_FALSE;
    }

  for (i = 0; i < source->config->gain_count; ++i)
    if (SoapySDRDevice_setGainElement(
        source->sdr,
        SOAPY_SDR_RX,
        source->config->channel,
        source->config->gain_list[i]->desc->name,
        source->config->gain_list[i]->val) != 0)
      SU_WARNING(
          "Failed to set gain `%s' to %gdB, ignoring silently\n",
          source->config->gain_list[i]->desc->name,
          source->config->gain_list[i]->val);


  if (SoapySDRDevice_setFrequency(
      source->sdr,
      SOAPY_SDR_RX,
      source->config->channel,
      source->config->freq,
      NULL) != 0) {
    SU_ERROR(
        "Failed to set SDR frequency: %s\n",
        SoapySDRDevice_lastError());
    return SU_FALSE;
  }

  if (SoapySDRDevice_setBandwidth(
      source->sdr,
      SOAPY_SDR_RX,
      source->config->channel,
      source->config->bandwidth) != 0) {
    SU_ERROR(
        "Failed to set SDR IF bandwidth: %s\n",
        SoapySDRDevice_lastError());
    return SU_FALSE;
  }

  if (SoapySDRDevice_setSampleRate(
      source->sdr,
      SOAPY_SDR_RX,
      source->config->channel,
      source->config->samp_rate) != 0) {
    SU_ERROR(
        "Failed to set sample rate: %s\n",
        SoapySDRDevice_lastError());
    return SU_FALSE;
  }
  /*
   * IQ-balance should be performed automatically. SoapySDR does not support
   * that yet.
   */
  source->soft_iq_balance = source->config->iq_balance;

  if (SoapySDRDevice_hasDCOffsetMode(
      source->sdr,
      SOAPY_SDR_RX,
      source->config->channel)) {
    if (SoapySDRDevice_setDCOffsetMode(
        source->sdr,
        SOAPY_SDR_RX,
        source->config->channel,
        source->config->dc_remove) != 0) {
      SU_ERROR(
          "Failed to set DC offset correction: %s\n",
          SoapySDRDevice_lastError());
      return SU_FALSE;
    }
  } else {
    source->soft_dc_correction = source->config->dc_remove;
  }

  /* All set: open SoapySDR stream */
  source->chan_array[0] = source->config->channel;

  if (SoapySDRDevice_setupStream(
      source->sdr,
      &source->rx_stream,
      SOAPY_SDR_RX,
      SUSCAN_SOAPY_SAMPFMT,
      source->chan_array,
      1,
      NULL) != 0) {
    SU_ERROR(
        "Failed to open RX stream on SDR device: %s\n",
        SoapySDRDevice_lastError());
    return SU_FALSE;
  }

  return SU_TRUE;
}

SUPRIVATE SUSDIFF
suscan_source_read_file(suscan_source_t *source, SUCOMPLEX *buf, SUSCOUNT max)
{
  SUFLOAT *as_real;
  int got, i;
  unsigned int real_count;

  if (source->force_eos)
    return 0;

  if (max > SUSCAN_SOURCE_DEFAULT_BUFSIZ)
    max = SUSCAN_SOURCE_DEFAULT_BUFSIZ;

  real_count = max * (source->iq_file ? 2 : 1);

  as_real = (SUFLOAT *) buf;

  got = sf_read(source->sf, as_real, real_count);

  if (got == 0 && source->config->loop) {
    if (sf_seek(source->sf, 0, SEEK_SET) == -1) {
      SU_ERROR("Failed to seek to the beginning of the stream\n");
      return 0;
    }

    got = sf_read(source->sf, as_real, real_count);
  }

  if (got > 0) {
    /* Real data mode: iteratively cast to complex */
    if (source->sf_info.channels == 1) {
      for (i = got - 1; i >= 0; --i)
        buf[i] = as_real[i];
    } else {
      got >>= 1;
    }
  }

  return got;
}

SUPRIVATE SUSDIFF
suscan_source_read_sdr(suscan_source_t *source, SUCOMPLEX *buf, SUSCOUNT max)
{
  int result;
  int flags;
  long long timeNs;
  SUBOOL retry;

  do {
    retry = SU_FALSE;
    if (source->force_eos)
      result = 0;
    else
      result = SoapySDRDevice_readStream(
          source->sdr,
          source->rx_stream,
          (void * const*) &buf,
          max,
          &flags,
          &timeNs,
          0); /* TODO: set timeOut */

    if (result == SOAPY_SDR_TIMEOUT
        || result == SOAPY_SDR_OVERFLOW
        || result == SOAPY_SDR_UNDERFLOW) {
      /* We should use this statuses as quality indicators */
      retry = SU_TRUE;
    }
  } while (retry);

  if (result < 0) {
    SU_ERROR(
        "Failed to read samples from stream: %s (result %d)\n",
        SoapySDR_errToStr(result),
        result);
    return SU_BLOCK_PORT_READ_ERROR_ACQUIRE;
  }

  return result;
}

SUSDIFF
suscan_source_read(suscan_source_t *source, SUCOMPLEX *buffer, SUSCOUNT max)
{
  SU_TRYCATCH(source->capturing, return SU_FALSE);

  if (source->read == NULL) {
    SU_ERROR("Signal source has no read() operation\n");
    return -1;
  }

  return (source->read) (source, buffer, max);
}

SUBOOL
suscan_source_start_capture(suscan_source_t *source)
{
  if (source->capturing) {
    SU_WARNING("start_capture: called twice, already capturing!\n");
    return SU_TRUE;
  }

  if (source->config->type == SUSCAN_SOURCE_TYPE_SDR) {
    if (SoapySDRDevice_activateStream(
        source->sdr,
        source->rx_stream,
        0,
        0,
        0) != 0) {
      SU_ERROR("Failed to activate stream: %s\n", SoapySDRDevice_lastError());
      return SU_FALSE;
    }
  }

  source->capturing = SU_TRUE;

  return SU_TRUE;
}

SUBOOL
suscan_source_stop_capture(suscan_source_t *source)
{
  if (!source->capturing) {
    SU_WARNING("stop_capture: called twice, already capturing!\n");
    return SU_TRUE;
  }

  if (source->config->type == SUSCAN_SOURCE_TYPE_SDR) {
    if (SoapySDRDevice_deactivateStream(
        source->sdr,
        source->rx_stream,
        0,
        0) != 0) {
      SU_ERROR("Failed to deactivate stream: %s\n", SoapySDRDevice_lastError());
      return SU_FALSE;
    }
  }

  source->capturing = SU_FALSE;

  return SU_TRUE;
}

SUBOOL
suscan_source_set_freq(suscan_source_t *source, SUFREQ freq)
{
  if (!source->capturing)
    return SU_FALSE;

  if (source->config->type == SUSCAN_SOURCE_TYPE_FILE)
    return SU_FALSE;

  /* Update config */
  suscan_source_config_set_freq(source->config, freq);

  /* Set device frequency */
  if (SoapySDRDevice_setFrequency(
      source->sdr,
      SOAPY_SDR_RX,
      source->config->channel,
      source->config->freq,
      NULL) != 0) {
    SU_ERROR(
        "Failed to set SDR frequency: %s\n",
        SoapySDRDevice_lastError());
    return SU_FALSE;
  }

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_source_config_check(const suscan_source_config_t *config)
{
  if (config->average < 1) {
    SU_ERROR("Invalid averaging value. Should be at least 1 for no averaging\n");
    return SU_FALSE;
  }

  if (config->samp_rate < 1
      && !(config->type == SUSCAN_SOURCE_TYPE_FILE
          && config->format == SUSCAN_SOURCE_FORMAT_WAV)) {
    SU_ERROR("Sample rate cannot be zero!\n");
    return SU_FALSE;
  }

  return SU_TRUE;
}

suscan_source_t *
suscan_source_new(suscan_source_config_t *config)
{
  suscan_source_t *new = NULL;

  SU_TRYCATCH(suscan_source_config_check(config), goto fail);
  SU_TRYCATCH(new = calloc(1, sizeof(suscan_source_t)), goto fail);
  SU_TRYCATCH(new->config = suscan_source_config_clone(config), goto fail);

  switch (new->config->type) {
    case SUSCAN_SOURCE_TYPE_FILE:
      SU_TRYCATCH(suscan_source_open_file(new), goto fail);
      new->read = suscan_source_read_file;
      break;

    case SUSCAN_SOURCE_TYPE_SDR:
      SU_TRYCATCH(suscan_source_open_sdr(new), goto fail);
      new->read = suscan_source_read_sdr;
      break;

    default:
      SU_ERROR("Malformed config object\n");
      goto fail;
  }

  return new;

fail:
  if (new != NULL)
    suscan_source_destroy(new);

  return NULL;
}

/*************************** API initialization ******************************/
SUPRIVATE SUBOOL
suscan_source_assert_default(void)
{
  suscan_source_config_t *new = NULL;

  SU_TRYCATCH(
      new = suscan_source_config_new(
          SUSCAN_SOURCE_TYPE_FILE,
          SUSCAN_SOURCE_FORMAT_AUTO),
      goto fail);

  SU_TRYCATCH(suscan_source_config_set_label(new, "Default source"), goto fail);

  suscan_source_config_set_dc_remove(new, SU_TRUE);

  SU_TRYCATCH(PTR_LIST_APPEND_CHECK(config, new) != -1, goto fail);

  return SU_TRUE;

fail:
  if (new != NULL)
    suscan_source_config_destroy(new);

  return SU_FALSE;
}

/* Put everything back on config context */
SUPRIVATE SUBOOL
suscan_sources_on_save(suscan_config_context_t *ctx, void *private)
{
  unsigned int i;
  suscan_object_t *cfg = NULL;

  suscan_config_context_flush(ctx);

  for (i = 0; i < config_count; ++i) {
    if (config_list[i] != NULL) {
      SU_TRYCATCH(
          cfg = suscan_source_config_to_object(config_list[i]),
          goto fail);

      SU_TRYCATCH(suscan_config_context_put(ctx, cfg), goto fail);

      cfg = NULL;
    }
  }

  return SU_TRUE;

fail:
  if (cfg != NULL)
    suscan_object_destroy(cfg);

  return SU_FALSE;
}

SUBOOL
suscan_source_config_register(suscan_source_config_t *config)
{
  SU_TRYCATCH(PTR_LIST_APPEND_CHECK(config, config) != -1, return SU_FALSE);

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_load_sources(void)
{
  suscan_config_context_t *ctx = NULL;
  suscan_source_config_t *cfg = NULL;
  const suscan_object_t *list = NULL;
  const suscan_object_t *cfgobj = NULL;
  const SoapySDRKwargs *args = NULL;
  unsigned int i, count;
  const char *tmp;

  SU_TRYCATCH(
      ctx = suscan_config_context_assert("sources"),
      goto fail);

  suscan_config_context_set_on_save(ctx, suscan_sources_on_save, NULL);

  list = suscan_config_context_get_list(ctx);

  count = suscan_object_set_get_count(list);

  for (i = 0; i < count; ++i) {
    if ((cfgobj = suscan_object_set_get(list, i)) != NULL) {
      if ((tmp = suscan_object_get_class(cfgobj)) != NULL
          && strcmp(tmp, "source_config") == 0) {
        if ((cfg = suscan_source_config_from_object(cfgobj)) == NULL) {
          SU_WARNING("Could not parse configuration #%d from config\n", i);
        } else {
          SU_TRYCATCH(suscan_source_config_register(cfg), goto fail);
          args = cfg->soapy_args;
          cfg = NULL;
        }
      }
    }
  }

  if (config_count == 0)
    SU_TRYCATCH(suscan_source_assert_default(), goto fail);

  return SU_TRUE;

fail:
  if (cfg != NULL)
    suscan_source_config_destroy(cfg);

  return SU_FALSE;
}

SUBOOL
suscan_init_sources(void)
{
  SU_TRYCATCH(suscan_confdb_use("sources"), return SU_FALSE);
  SU_TRYCATCH(suscan_source_detect_devices(), return SU_FALSE);
  SU_TRYCATCH(suscan_load_sources(), return SU_FALSE);

  return SU_TRUE;
}
