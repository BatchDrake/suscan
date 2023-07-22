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

#define SU_LOG_DOMAIN "source-config"

#include <analyzer/source.h>
#include <confdb.h>
#include <libgen.h>

/* Come on */
#ifdef bool
#  undef bool
#endif

/* Private config list */
PTR_LIST(SUPRIVATE suscan_source_config_t, config);

SUPRIVATE const char *suscan_source_config_helper_format_to_str(
  enum suscan_source_format type);

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

SUPRIVATE void
suscan_source_config_clear_gains(suscan_source_config_t *self)
{
  unsigned int i;

  for (i = 0; i < self->gain_count; ++i)
    if (self->gain_list[i] != NULL)
      suscan_source_gain_value_destroy(self->gain_list[i]);

  if (self->gain_list != NULL)
    free(self->gain_list);

  self->gain_count = 0;
  self->gain_list = NULL;

  for (i = 0; i < self->hidden_gain_count; ++i)
    if (self->hidden_gain_list[i] != NULL)
      suscan_source_gain_value_destroy(self->hidden_gain_list[i]);

  if (self->hidden_gain_list != NULL)
    free(self->hidden_gain_list);

  self->hidden_gain_count = 0;
  self->hidden_gain_list = NULL;
}

void
suscan_source_config_destroy(suscan_source_config_t *config)
{
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

  suscan_source_config_clear_gains(config);

  free(config);
}

/* Getters & Setters */
SUBOOL
suscan_source_config_set_label(
    suscan_source_config_t *config,
    const char *label)
{
  char *dup = NULL;

  if (label != NULL)
    SU_TRYCATCH(dup = strdup(label), return SU_FALSE);

  if (config->label != NULL)
    free(config->label);

  config->label = dup;

  return SU_TRUE;
}

const char *
suscan_source_config_get_type(const suscan_source_config_t *config)
{
  return config->type;
}

enum suscan_source_format
suscan_source_config_get_format(const suscan_source_config_t *config)
{
  return config->format;
}

SUBOOL
suscan_source_config_set_type_format(
    suscan_source_config_t *config,
    const char *type,
    enum suscan_source_format format)
{
  char *dup;
  
  SU_TRY_FAIL(dup = strdup(type));

  if (config->type != NULL)
    free(config->type);
  
  config->type   = dup;
  config->format = format;

  return SU_TRUE;

fail:
  return SU_FALSE;
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

SUFREQ
suscan_source_config_get_lnb_freq(const suscan_source_config_t *config)
{
  return config->lnb_freq;
}

void
suscan_source_config_set_lnb_freq(suscan_source_config_t *config, SUFREQ freq)
{
  config->lnb_freq = freq;
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

const char *
suscan_source_config_get_interface(const suscan_source_config_t *self)
{
  return self->interface;
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

  for (i = 0; i < config->hidden_gain_count; ++i)
    if (!(gain_cb) (
        private,
        config->hidden_gain_list[i]->desc->name,
        config->hidden_gain_list[i]->val))
      return SU_FALSE;

  return SU_TRUE;
}

SUBOOL
suscan_source_config_walk_gains_ex(
    const suscan_source_config_t *config,
    SUBOOL (*gain_cb) (void *private, struct suscan_source_gain_value *),
    void *private)
{
  unsigned int i;

  for (i = 0; i < config->gain_count; ++i)
    if (!(gain_cb) (private, config->gain_list[i]))
      return SU_FALSE;

  for (i = 0; i < config->hidden_gain_count; ++i)
    if (!(gain_cb) (private, config->hidden_gain_list[i]))
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

SUFLOAT
suscan_source_config_get_ppm(const suscan_source_config_t *config)
{
  return config->ppm;
}

void
suscan_source_config_set_ppm(suscan_source_config_t *config, SUFLOAT ppm)
{
  config->ppm = ppm;
}

SUBOOL
suscan_source_config_is_real_time(const suscan_source_config_t *self)
{
  const struct suscan_source_interface *iface;

  iface = suscan_source_interface_lookup_by_name(self->type);
  if (iface == NULL)
    return SU_FALSE;
  
  if (iface->is_real_time != NULL)
    return (iface->is_real_time) (self);

  return iface->realtime;
}

SUBOOL
suscan_source_config_is_seekable(const suscan_source_config_t *self)
{
  const struct suscan_source_interface *iface;

  iface = suscan_source_interface_lookup_by_name(self->type);
  if (iface == NULL)
    return SU_FALSE;
  
  return iface->seek != NULL;
}


SUBOOL
suscan_source_config_get_end_time(
  const suscan_source_config_t *self,
  struct timeval *tv)
{
  SUBOOL ok = SU_FALSE;
  struct timeval start, elapsed = {0, 0};
  SUSDIFF max_size;
  const struct suscan_source_interface *iface;

  iface = suscan_source_interface_lookup_by_name(self->type);
  if (iface == NULL)
    goto done;
  
  if (iface->estimate_size == NULL)
    goto done;
  
  max_size = (iface->estimate_size) (self);
  if (max_size < 0)
    goto done;
  
  SU_TRY(self->average > 0);

  max_size /= self->average;

  elapsed.tv_sec  = max_size / self->samp_rate;
  elapsed.tv_usec = (1000000 
    * (max_size - elapsed.tv_sec * self->samp_rate))
    / self->samp_rate;

  suscan_source_config_get_start_time(self, &start);
  timeradd(&start, &elapsed, tv);

  ok = SU_TRUE;

done:
  return ok;
}

SUBOOL
suscan_source_config_get_freq_limits(
    const suscan_source_config_t *self,
    SUFREQ *min,
    SUFREQ *max)
{
  const struct suscan_source_interface *iface;
  SUBOOL ok = SU_FALSE;

  iface = suscan_source_interface_lookup_by_name(self->type);
  if (iface == NULL)
    goto done;
  
  if (iface->get_freq_limits == NULL)
    goto done;
  
  ok = (iface->get_freq_limits) (self, min, max);

done:
  return ok;
}

void 
suscan_source_config_get_start_time(
  const suscan_source_config_t *config,
  struct timeval *tv)
{
  *tv = config->start_time; 
}

void 
suscan_source_config_set_start_time(
    suscan_source_config_t *config,
    struct timeval tv)
{
  config->start_time = tv;
}

SUPRIVATE SUBOOL
suscan_source_config_set_gains_from_device(
    suscan_source_config_t *config,
    const suscan_source_device_t *dev)
{
  unsigned int i;
  struct suscan_source_gain_value *gain = NULL;
  float value;
  PTR_LIST_LOCAL(struct suscan_source_gain_value, gain);
  SUBOOL ok = SU_FALSE;

  for (i = 0; i < dev->gain_desc_count; ++i) {
    /* Attempt to reuse existing gains */
    if ((gain = suscan_source_config_lookup_gain(
      config,
      dev->gain_desc_list[i]->name)) != NULL)
      value = gain->val;
    else
      value = dev->gain_desc_list[i]->def;

    SU_TRYCATCH(
        gain = suscan_source_gain_value_new(dev->gain_desc_list[i], value),
        goto done);

    SU_TRYCATCH(
        PTR_LIST_APPEND_CHECK(gain, gain) != -1,
        goto done);

    gain = NULL;
  }

  /* TODO: How about a little swap here */
  suscan_source_config_clear_gains(config);

  config->gain_list  = gain_list;
  config->gain_count = gain_count;

  gain_list  = NULL;
  gain_count = 0;

  ok = SU_TRUE;

done:
  if (gain != NULL)
    suscan_source_gain_value_destroy(gain);

  if (gain_list != NULL) {
    for (i = 0; i < gain_count; ++i)
      suscan_source_gain_value_destroy(gain_list[i]);

    free(gain_list);
  }

  return ok;
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

  SU_TRYCATCH(
      suscan_source_config_set_gains_from_device(config, dev),
      return SU_FALSE);

  config->interface = dev->interface;
  config->device = dev;

  return SU_TRUE;
}

SUBOOL
suscan_source_config_set_interface(
    suscan_source_config_t *self,
    const char *interface)
{
  if (strcmp(interface, SUSCAN_SOURCE_LOCAL_INTERFACE) == 0) {
    self->interface = SUSCAN_SOURCE_LOCAL_INTERFACE;
  } else if (strcmp(interface, SUSCAN_SOURCE_REMOTE_INTERFACE) == 0) {
    self->interface = SUSCAN_SOURCE_REMOTE_INTERFACE;
  } else {
    SU_ERROR("Unsupported interface `%s'\n", interface);
    return SU_FALSE;
  }

  return SU_TRUE;
}

SUSCAN_SERIALIZER_PROTO(suscan_source_config)
{
  SUSCAN_PACK_BOILERPLATE_START;
  struct suscan_source_gain_value *gain;
  unsigned int i;
  char *dup = NULL;
  const char *host;
  const char *port_str;
  uint16_t port;

  SUSCAN_PACK(str, self->label);
  SUSCAN_PACK(str, self->interface);
  SUSCAN_PACK(str, self->type);

  /* We don't set source format, or anything related to the sender system */
  SUSCAN_PACK(freq,  self->freq);
  SUSCAN_PACK(freq,  self->lnb_freq);
  SUSCAN_PACK(float, self->bandwidth);
  SUSCAN_PACK(bool,  self->iq_balance);
  SUSCAN_PACK(bool,  self->dc_remove);
  SUSCAN_PACK(float, self->ppm);
  SUSCAN_PACK(uint,  self->start_time.tv_sec);
  SUSCAN_PACK(uint,  self->start_time.tv_usec);
  SUSCAN_PACK(uint,  self->samp_rate);
  SUSCAN_PACK(uint,  self->average);
  SUSCAN_PACK(bool,  self->loop);

  SUSCAN_PACK(str,   self->antenna);
  SUSCAN_PACK(uint,  self->channel);

  if (self->path == NULL) {
    SUSCAN_PACK(str, "<no file>");
  } else {
    SU_TRYCATCH(dup = strdup(self->path), goto fail);
    SUSCAN_PACK(str,  basename(dup));
  }

  if (self->device == NULL) {
    SUSCAN_PACK(str, "");
    SUSCAN_PACK(str, "");
    SUSCAN_PACK(str, "");
    SUSCAN_PACK(uint, 0);
  } else {
    if ((host = SoapySDRKwargs_get(self->soapy_args, "host")) == NULL)
      host = "";

    if ((port_str = SoapySDRKwargs_get(self->soapy_args, "port")) == NULL)
      port_str = "";

    if (sscanf(port_str, "%hu", &port) != 1)
      port = 0;

    SUSCAN_PACK(str,  suscan_source_device_get_desc(self->device));
    SUSCAN_PACK(str,  suscan_source_device_get_driver(self->device));
    SUSCAN_PACK(str,  host);
    SUSCAN_PACK(uint, port);

    SUSCAN_PACK(uint, self->gain_count);

    for (i = 0; i < self->gain_count; ++i) {
      gain = self->gain_list[i];

      SUSCAN_PACK(str,   gain->desc->name);
      SUSCAN_PACK(float, gain->desc->min);
      SUSCAN_PACK(float, gain->desc->max);
      SUSCAN_PACK(float, gain->desc->step);
      SUSCAN_PACK(float, gain->desc->def);
      SUSCAN_PACK(float, gain->val);
    }
  }

  SUSCAN_PACK_BOILERPLATE_FINALLY;

  if (dup != NULL)
    free(dup);

  SUSCAN_PACK_BOILERPLATE_RETURN;
}

SUBOOL
suscan_source_config_deserialize_ex(
    struct suscan_source_config *self,
    grow_buf_t *buffer,
    const char *force_host)
{
  SUSCAN_UNPACK_BOILERPLATE_START;
  struct suscan_source_gain_desc gain_desc, *new_desc = NULL;
  struct suscan_source_gain_value *gain = NULL;
  suscan_source_device_t *device = NULL;
  SoapySDRKwargs args;
  uint64_t sec = 0;
  uint32_t usec = 0;
  char *type = NULL;
  char *iface = NULL;

  char *driver = NULL;
  char *desc = NULL;

  char *host = NULL;
  uint16_t port = 0;
  char port_str[8];
  unsigned int gain_count = 0, i;

  memset(&args, 0, sizeof (SoapySDRKwargs));
  memset(&gain_desc, 0, sizeof (struct suscan_source_gain_desc));

  SUSCAN_UNPACK(str, self->label);
  SUSCAN_UNPACK(str, iface);

  if (strcmp(iface, SUSCAN_SOURCE_LOCAL_INTERFACE) == 0) {
    SU_ERROR(
        "Deserialization of local device profiles is disabled for security reasons\n");
    goto fail;
    /* self->interface = SUSCAN_SOURCE_LOCAL_INTERFACE; */
  } else if (strcmp(iface, SUSCAN_SOURCE_REMOTE_INTERFACE) == 0) {
    self->interface = SUSCAN_SOURCE_REMOTE_INTERFACE;
  } else {
    SU_ERROR("Unsupported analyzer interface `%s'\n", iface);
    goto fail;
  }

  SUSCAN_UNPACK(str,    self->type);
  SUSCAN_UNPACK(freq,   self->freq);
  SUSCAN_UNPACK(freq,   self->lnb_freq);
  SUSCAN_UNPACK(float,  self->bandwidth);
  SUSCAN_UNPACK(bool,   self->iq_balance);
  SUSCAN_UNPACK(bool,   self->dc_remove);
  SUSCAN_UNPACK(float,  self->ppm);
  SUSCAN_UNPACK(uint64, sec);
  SUSCAN_UNPACK(uint32, usec);

  self->start_time.tv_sec  = sec;
  self->start_time.tv_usec = usec;

  SUSCAN_UNPACK(uint32, self->samp_rate);
  SUSCAN_UNPACK(uint32, self->average);

  SUSCAN_UNPACK(bool,   self->loop);

  SUSCAN_UNPACK(str,    self->antenna);
  SUSCAN_UNPACK(uint32, self->channel);

  SUSCAN_UNPACK(str,    self->path);
  SUSCAN_UNPACK(str,    desc);
  SUSCAN_UNPACK(str,    driver);
  SUSCAN_UNPACK(str,    host);
  SUSCAN_UNPACK(uint16, port);

  snprintf(port_str, sizeof(port_str), "%hu", port);

  if (strlen(driver) > 0) {
    SoapySDRKwargs_set(&args, "label",  desc);
    SoapySDRKwargs_set(&args, "driver", driver);

    if (force_host != NULL)
      SoapySDRKwargs_set(&args, "host",   force_host);
    else
      SoapySDRKwargs_set(&args, "host",   host);

    SoapySDRKwargs_set(&args, "port",   port_str);

    /* FIXME: Add a remote device deserializer? */
    SU_TRYCATCH(
        device = suscan_source_device_assert(
            self->interface,
            &args),
        goto fail);

    /* FIXME: Acquire g_device_list_mutex!!! */
    device->available = SU_FALSE;
    suscan_source_config_set_device(self, device);

    SUSCAN_UNPACK(uint32, gain_count);

    for (i = 0; i < gain_count; ++i) {
      SUSCAN_UNPACK(str,   gain_desc.name);
      SUSCAN_UNPACK(float, gain_desc.min);
      SUSCAN_UNPACK(float, gain_desc.max);
      SUSCAN_UNPACK(float, gain_desc.step);
      SUSCAN_UNPACK(float, gain_desc.def);

      SU_TRYCATCH(
          new_desc = suscan_source_device_assert_gain_unsafe(
              device,
              gain_desc.name,
              gain_desc.min,
              gain_desc.max,
              gain_desc.step),
          goto fail);

      if (gain_desc.name != NULL)
        free(gain_desc.name);

      memset(&gain_desc, 0, sizeof (struct suscan_source_gain_desc));

      SU_TRYCATCH(gain = suscan_source_gain_value_new(new_desc, 0), goto fail);

      SUSCAN_UNPACK(float, gain->val);

      SU_TRYCATCH(PTR_LIST_APPEND_CHECK(self->gain, gain) != -1, goto fail);

      gain = NULL;
    }

    /* FIXME: Return g_device_list_mutex!!! */
    device->available = SU_TRUE;
  } else {
    self->device = suscan_source_get_null_device();
  }

  SUSCAN_UNPACK_BOILERPLATE_FINALLY;

  SoapySDRKwargs_clear(&args);

  if (gain_desc.name != NULL)
    free(gain_desc.name);

  if (type != NULL)
    free(type);

  if (iface != NULL)
    free(iface);

  if (driver != NULL)
    free(driver);

  if (desc != NULL)
    free(desc);

  if (host != NULL)
    free(host);

  /* Not a destructor */
  if (gain != NULL)
    free(gain);

  SUSCAN_UNPACK_BOILERPLATE_RETURN;
}

SUSCAN_DESERIALIZER_PROTO(suscan_source_config)
{
  return suscan_source_config_deserialize_ex(self, buffer, NULL);
}

suscan_source_config_t *
suscan_source_config_new(
    const char *type,
    enum suscan_source_format format)
{
  suscan_source_config_t *new = NULL;

  SU_TRYCATCH(new = calloc(1, sizeof(suscan_source_config_t)), goto fail);

  SU_TRY_FAIL(new->type = strdup(type));

  new->format = format;
  new->average = 1;
  new->dc_remove = SU_TRUE;
  new->interface = SUSCAN_SOURCE_LOCAL_INTERFACE;
  new->loop = SU_TRUE;
  
  gettimeofday(&new->start_time, NULL);

  SU_TRYCATCH(new->soapy_args = calloc(1, sizeof(SoapySDRKwargs)), goto fail);

  SU_TRYCATCH(suscan_source_get_null_device() != NULL, goto fail);

  SU_TRYCATCH(
      suscan_source_config_set_device(
          new,
          suscan_source_get_null_device()),
      goto fail);

  return new;

fail:
  if (new != NULL)
    suscan_source_config_destroy(new);

  return NULL;
}

suscan_source_config_t *
suscan_source_config_new_default(void)
{
  suscan_source_config_t *new = NULL;

  SU_TRYCATCH(
        new = suscan_source_config_new("soapysdr", SUSCAN_SOURCE_FORMAT_AUTO),
        goto fail);

  SU_TRYCATCH(
      suscan_source_config_set_label(new, SUSCAN_SOURCE_DEFAULT_NAME),
      goto fail);

  suscan_source_config_set_freq(new, SUSCAN_SOURCE_DEFAULT_FREQ);

  suscan_source_config_set_samp_rate(new, SUSCAN_SOURCE_DEFAULT_SAMP_RATE);

  suscan_source_config_set_bandwidth(new, SUSCAN_SOURCE_DEFAULT_BANDWIDTH);

  SU_TRYCATCH(
      suscan_source_config_set_device(
          new,
          suscan_source_device_find_first_sdr()),
      goto fail);

  suscan_source_config_set_dc_remove(new, SU_TRUE);

  return new;

fail:
  suscan_source_config_destroy(new);

  return NULL;
}

void
suscan_source_config_swap(
    suscan_source_config_t *config1,
    suscan_source_config_t *config2)
{
  suscan_source_config_t tmp;

  tmp = *config2;
  *config2 = *config1;
  *config1 = tmp;
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
  new->interface = config->interface;

  for (i = 0; i < config->gain_count; ++i) {
    SU_TRYCATCH(
        suscan_source_config_set_gain(
            new,
            config->gain_list[i]->desc->name,
            config->gain_list[i]->val),
        goto fail);
  }

  /* Copy hidden gains too */
  for (i = 0; i < config->hidden_gain_count; ++i)
    SU_TRYCATCH(
        suscan_source_config_set_gain(
            new,
            config->hidden_gain_list[i]->desc->name,
            config->hidden_gain_list[i]->val),
        goto fail);

  for (i = 0; i < config->soapy_args->size; ++i) {
    /* ----8<----------------- DANGER DANGER DANGER ----8<----------------- */
    SoapySDRKwargs_set(
        new->soapy_args,
        config->soapy_args->keys[i],
        config->soapy_args->vals[i]);
    /* ----8<----------------- DANGER DANGER DANGER ----8<----------------- */
  }

  new->freq = config->freq;
  new->lnb_freq = config->lnb_freq;
  new->bandwidth = config->bandwidth;
  new->iq_balance = config->iq_balance;
  new->dc_remove = config->dc_remove;
  new->samp_rate = config->samp_rate;
  new->average = config->average;
  new->ppm     = config->ppm;
  new->channel = config->channel;
  new->loop = config->loop;
  new->device = config->device;
  new->start_time = config->start_time;

  return new;

fail:
  if (new != NULL)
    suscan_source_config_destroy(new);

  return NULL;
}

SUPRIVATE const char *
suscan_source_config_helper_format_to_str(enum suscan_source_format type)
{
  switch (type) {
    case SUSCAN_SOURCE_FORMAT_AUTO:
      return "AUTO";

    case SUSCAN_SOURCE_FORMAT_RAW_FLOAT32:
      return "RAW_FLOAT32";

    case SUSCAN_SOURCE_FORMAT_RAW_UNSIGNED8:
      return "RAW_UNSIGNED8";

    case SUSCAN_SOURCE_FORMAT_RAW_SIGNED16:
      return "RAW_SIGNED16";

    case SUSCAN_SOURCE_FORMAT_RAW_SIGNED8:
      return "RAW_SIGNED8";

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
      return SUSCAN_SOURCE_FORMAT_RAW_FLOAT32; /* backward compat */
    else if (strcasecmp(format, "RAW_FLOAT32") == 0)
      return SUSCAN_SOURCE_FORMAT_RAW_FLOAT32;
    else if (strcasecmp(format, "RAW_UNSIGNED8") == 0)
      return SUSCAN_SOURCE_FORMAT_RAW_UNSIGNED8;
    else if (strcasecmp(format, "RAW_SIGNED16") == 0)
      return SUSCAN_SOURCE_FORMAT_RAW_SIGNED16;
    else if (strcasecmp(format, "RAW_SIGNED8") == 0)
      return SUSCAN_SOURCE_FORMAT_RAW_SIGNED8;
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
  SU_TRYCATCH(suscan_object_set_field_value(new, "type", cfg->type), goto fail);
  SU_TRYCATCH(
        tmp = suscan_source_config_helper_format_to_str(cfg->format),
        goto fail);
  SU_TRYCATCH(suscan_object_set_field_value(new, "format", tmp), goto fail);

  if (cfg->label != NULL)
    SU_CFGSAVE(value, label);

  if (cfg->path != NULL)
    SU_CFGSAVE(value, path);

  if (cfg->antenna != NULL)
    SU_CFGSAVE(value, antenna);

  if (cfg->interface != NULL)
    SU_CFGSAVE(value, interface);

  /* XXX: This is terrible. Either change this or define SUFREQ as uint64_t */
  SU_CFGSAVE(double, freq);
  SU_CFGSAVE(double, lnb_freq);
  SU_CFGSAVE(float,  bandwidth);
  SU_CFGSAVE(bool,   iq_balance);
  SU_CFGSAVE(bool,   dc_remove);
  SU_CFGSAVE(float,  ppm);
  SU_CFGSAVE(tv,     start_time);
  SU_CFGSAVE(bool,   loop);
  SU_CFGSAVE(uint,   samp_rate);
  SU_CFGSAVE(uint,   average);
  SU_CFGSAVE(uint,   channel);

  /* Save SoapySDR kwargs */
  SU_TRYCATCH(obj = suscan_object_new(SUSCAN_OBJECT_TYPE_OBJECT), goto fail);

  for (i = 0; i < cfg->soapy_args->size; ++i)
    SU_TRYCATCH(
        suscan_object_set_field_value(
            obj,
            cfg->soapy_args->keys[i],
            cfg->soapy_args->vals[i]),
        goto fail);

  SU_TRYCATCH(suscan_object_set_field(new, "sdr_args", obj), goto fail);
  obj = NULL;

  /* Save gains */
  SU_TRYCATCH(obj = suscan_object_new(SUSCAN_OBJECT_TYPE_OBJECT), goto fail);

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
  const char *type = NULL;
  struct timeval default_time;
  unsigned int i, count;
  SUFLOAT val;

  const char *tmp;

  gettimeofday(&default_time, NULL);

#define SU_CFGLOAD(kind, field, dfl)                            \
        JOIN(suscan_source_config_set_, field)(                 \
            new,                                                \
            JOIN(suscan_object_get_field_, kind)(               \
            object,                                             \
            STRINGIFY(field),                                   \
            dfl))

  /* Fix type field */
  type = suscan_object_get_field_value(object, "type");
  if (type == NULL)
    type = "soapysdr";
  if (strcmp(type, "FILE") == 0)
    type = "file";
  else if (strcmp(type, "SDR") == 0)
    type = "soapysdr";
  
  SU_TRYCATCH(
      new = suscan_source_config_new(
          type,
          suscan_source_type_config_helper_str_to_format(
              suscan_object_get_field_value(object, "format"))),
      goto fail);

  if ((tmp = suscan_object_get_field_value(object, "label")) != NULL)
    SU_TRYCATCH(suscan_source_config_set_label(new, tmp), goto fail);

  if ((tmp = suscan_object_get_field_value(object, "path")) != NULL)
    SU_TRYCATCH(suscan_source_config_set_path(new, tmp), goto fail);

  if ((tmp = suscan_object_get_field_value(object, "antenna")) != NULL)
    SU_TRYCATCH(suscan_source_config_set_antenna(new, tmp), goto fail);

  if ((tmp = suscan_object_get_field_value(object, "interface")) != NULL) {
    if (strcmp(tmp, SUSCAN_SOURCE_LOCAL_INTERFACE) == 0) {
      new->interface = SUSCAN_SOURCE_LOCAL_INTERFACE;
    } else if (strcmp(tmp, SUSCAN_SOURCE_REMOTE_INTERFACE) == 0) {
      new->interface = SUSCAN_SOURCE_REMOTE_INTERFACE;
    } else {
      SU_WARNING("Invalid interface `%s'. Defaulting to local\n", tmp);
      new->interface = SUSCAN_SOURCE_LOCAL_INTERFACE;
    }
  }

  SU_CFGLOAD(double, freq, 0);
  SU_CFGLOAD(double, lnb_freq, 0);
  SU_CFGLOAD(float,  bandwidth, 0);
  SU_CFGLOAD(bool,   iq_balance, SU_FALSE);
  SU_CFGLOAD(bool,   dc_remove, SU_FALSE);
  SU_CFGLOAD(float,  ppm, 0);
  SU_CFGLOAD(tv,     start_time, &default_time);
  SU_CFGLOAD(bool,   loop, SU_FALSE);
  SU_CFGLOAD(uint,   samp_rate, 1.8e6);
  SU_CFGLOAD(uint,   channel, 0);

  SU_TRYCATCH(SU_CFGLOAD(uint, average, 1), goto fail);

  if ((obj = suscan_object_get_field(object, "sdr_args")) != NULL) {
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
          new->device = device = suscan_source_device_assert(
              new->interface,
              new->soapy_args),
          goto fail);

      /* This step is not critical, but we must try it anyways */
      if (!suscan_source_device_is_populated(device))
        (void) suscan_source_device_populate_info(device);
    }
  }

  /* Retrieve gains */
  if ((obj = suscan_object_get_field(object, "gains")) != NULL) {
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
          new->device = device = suscan_source_device_assert(
              new->interface,
              new->soapy_args),
          goto fail);

      /* This step is not critical, but we must try it anyways */
      if (!suscan_source_device_is_populated(device))
        (void) suscan_source_device_populate_info(device);
    }
  }

  return new;

fail:
  if (new != NULL)
    suscan_source_config_destroy(new);

  return NULL;
}

/*************************** API initialization ******************************/
SUPRIVATE SUBOOL
suscan_source_add_default(void)
{
  suscan_source_config_t *new = NULL;

  SU_TRYCATCH(new = suscan_source_config_new_default(), goto fail);

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

SUBOOL
suscan_load_sources(void)
{
  suscan_config_context_t *ctx = NULL;
  suscan_source_config_t *cfg = NULL;
  const suscan_object_t *list = NULL;
  const suscan_object_t *cfgobj = NULL;
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
          cfg = NULL;
        }
      }
    }
  }

  if (config_count == 0)
    SU_TRYCATCH(suscan_source_add_default(), goto fail);

  return SU_TRUE;

fail:
  if (cfg != NULL)
    suscan_source_config_destroy(cfg);

  return SU_FALSE;
}