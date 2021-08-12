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

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <libgen.h>

#define SU_LOG_DOMAIN "device"

#include <confdb.h>
#include "source.h"
#include "compat.h"
#include <fcntl.h>
#include <unistd.h>

/* Private device list */
SUPRIVATE pthread_mutex_t g_device_list_mutex;
PTR_LIST(SUPRIVATE suscan_source_device_t, device);

/* Hidden gain list */
PTR_LIST(SUPRIVATE struct suscan_source_gain_desc, hidden_gain);

/* Null device */
SUPRIVATE suscan_source_device_t *null_device;
SUPRIVATE const char *soapysdr_module_path = NULL;

/* Helper global state */
SUPRIVATE SUBOOL stderr_disabled = SU_FALSE;
SUPRIVATE int    stderr_copy = -1;

/********************************** Helpers **********************************/
SUPRIVATE void
suscan_source_disable_stderr(void)
{
  int fd = -1;

  if (!stderr_disabled) {
    SU_TRYCATCH((fd = open("/dev/null", O_WRONLY)) != -1, goto fail);
    SU_TRYCATCH((stderr_copy = dup(STDERR_FILENO)) != -1, goto fail);
    SU_TRYCATCH(dup2(fd, STDERR_FILENO) != -1, goto fail);
    stderr_disabled = SU_TRUE;
  }

fail:
  if (fd != -1)
    close(fd);

  if (!stderr_disabled) {
    if (dup2(stderr_copy, STDERR_FILENO) != -1) {
      close(stderr_copy);
      stderr_copy = -1;
    }
  }
}

SUPRIVATE void
suscan_source_enable_stderr(void)
{
  if (stderr_disabled) {
    SU_TRYCATCH(dup2(stderr_copy, STDERR_FILENO) != -1, return);
    close(stderr_copy);
    stderr_copy = -1;
    stderr_disabled = SU_FALSE;
  }
}

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
const struct suscan_source_gain_desc *
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

const struct suscan_source_gain_desc *
suscan_source_device_lookup_gain_desc(
    const suscan_source_device_t *dev,
    const char *name)
{
  unsigned int i;

  /* We only provide visibility to current gains. */
  for (i = 0; i < dev->gain_desc_count; ++i)
    if (strcmp(dev->gain_desc_list[i]->name, name) == 0
        && dev->gain_desc_list[i]->epoch == dev->epoch)
      return dev->gain_desc_list[i];

  return NULL;
}

void
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

  if (dev->samp_rate_list != NULL)
    free(dev->samp_rate_list);

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
  if (info->gain_desc_list != NULL)
    free(info->gain_desc_list);

  info->gain_desc_list = NULL;
  info->gain_desc_count = 0;
}

SUPRIVATE void
suscan_source_reset_devices(void)
{
  unsigned int i, j;
  SUBOOL mutex_acquired = SU_FALSE;

  SU_TRYCATCH(pthread_mutex_lock(&g_device_list_mutex) == 0, goto done);
  mutex_acquired = SU_TRUE;

  for (i = 0; i < device_count; ++i)
    if (device_list[i] != NULL) {
      ++device_list[i]->epoch;
      device_list[i]->available = SU_FALSE;

      for (j = 0; j < device_list[i]->antenna_count; ++j)
        free(device_list[i]->antenna_list[j]);

      device_list[i]->antenna_count = 0;
      if (device_list[i]->antenna_list != NULL) {
        free(device_list[i]->antenna_list);
        device_list[i]->antenna_list = NULL;
      }

      device_list[i]->samp_rate_count = 0;
      if (device_list[i]->samp_rate_list != NULL) {
        free(device_list[i]->samp_rate_list);
        device_list[i]->samp_rate_list = NULL;
      }
    }

done:
  if (mutex_acquired)
    (void) pthread_mutex_unlock(&g_device_list_mutex);
}

SUPRIVATE char *
suscan_source_device_build_desc(const char *driver, const SoapySDRKwargs *args)
{
  const char *label;
  const char *host, *port;

  label = SoapySDRKwargs_get((SoapySDRKwargs *) args, "label");
  host  = SoapySDRKwargs_get((SoapySDRKwargs *) args, "host");
  port  = SoapySDRKwargs_get((SoapySDRKwargs *) args, "port");

  if (label == NULL)
    label = "Unlabeled device";

  if (host == NULL)
    host = "<invalid host>";

  if (port == NULL)
    port = "<invalid port>";

  if (strcmp(driver, "audio") == 0)
    return strbuild("Audio input (%s)", label);
  else if (strcmp(driver, "hackrf") == 0)
    return strbuild("HackRF One (%s)", label);
  else if (strcmp(driver, "null") == 0)
    return strdup("Dummy device");
  else if (strcmp(driver, "tcp") == 0)
    return strbuild("%s:%s (%s)", host, port, label);
  return strbuild("%s (%s)", driver, label);
}

struct suscan_source_gain_desc *
suscan_source_device_assert_gain_unsafe(
    suscan_source_device_t *dev,
    const char *name,
    SUFLOAT min,
    SUFLOAT max,
    unsigned int step)
{
  unsigned int i;
  struct suscan_source_gain_desc *desc = NULL, *result = NULL;

  for (i = 0; i < dev->gain_desc_count; ++i) {
    if (strcmp(dev->gain_desc_list[i]->name, name) == 0) {
      result = dev->gain_desc_list[i];
      result->min = min;
      result->max = max;
      break;
    }
  }

  if (result == NULL) {
    SU_TRYCATCH(
        desc = suscan_source_gain_desc_new(name, min, max),
        goto done);
    SU_TRYCATCH(PTR_LIST_APPEND_CHECK(dev->gain_desc, desc) != -1, goto done);
    result = desc;
    desc = NULL;
  }

  result->step = step;
  result->epoch = dev->epoch;

done:
  if (desc != NULL)
    suscan_source_gain_desc_destroy(desc);

  return result;
}

SUBOOL
suscan_source_device_populate_info(suscan_source_device_t *dev)
{
  SoapySDRDevice *sdev = NULL;
  SoapySDRRange *freqRanges = NULL;
  SoapySDRRange range;
  SUFREQ freq_min = INFINITY;
  SUFREQ freq_max = -INFINITY;
  char **antenna_list = NULL;
  char **gain_list = NULL;
  char *dup = NULL;
  double *samp_rate_list = NULL;
  size_t antenna_count = 0;
  size_t gain_count = 0;
  size_t range_count;
  size_t samp_rate_count;
  struct suscan_source_gain_desc *desc;
  unsigned int i;

  SUBOOL ok = SU_FALSE;

  if (suscan_source_device_is_remote(dev)) {
    dev->available = SU_TRUE;
    ok = SU_TRUE;
    goto done;
  }

  /*
   * This tends to happen a lot and is an error, but it does not
   * deserve an error message. A previously seen device being unavailable
   * during startup limits what you can do with suscan, but it is
   * not critical. Errors should appear when the user attempts to
   * load a device that has not been populated.
   */

  if ((sdev = SoapySDRDevice_make(dev->args)) == NULL)
    goto done;

  dev->available = SU_TRUE;

  /* Get frequency range */
  if ((freqRanges = SoapySDRDevice_getFrequencyRange(
      sdev,
      SOAPY_SDR_RX,
      0,
      &range_count)) != NULL) {
    for (i = 0; i < range_count; ++i) {
      if (freqRanges[i].minimum < freq_min)
        freq_min = freqRanges[i].minimum;
      if (freqRanges[i].maximum > freq_max)
        freq_max = freqRanges[i].maximum;
    }

    if (isinf(freq_min) || isinf(freq_max)) {
      dev->freq_min = 0;
      dev->freq_max = 0;
    } else {
      dev->freq_min = freq_min;
      dev->freq_max = freq_max;
    }
  }

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
          desc = suscan_source_device_assert_gain_unsafe(
              dev,
              gain_list[i],
              range.minimum,
              range.maximum,
              1), /* This may change in the future */
          goto done);

      desc->def = SoapySDRDevice_getGainElement(
          sdev,
          SOAPY_SDR_RX,
          0,
          gain_list[i]);
    }

    /* Get rates */
    SU_TRYCATCH(
        samp_rate_list = SoapySDRDevice_listSampleRates(
            sdev,
            SOAPY_SDR_RX,
            0,
            &samp_rate_count),
        goto done);

    if (samp_rate_count == 0)
      goto done;

    SU_TRYCATCH(
        dev->samp_rate_list = malloc(samp_rate_count * sizeof(double)),
        goto done);

    memcpy(
        dev->samp_rate_list,
        samp_rate_list,
        samp_rate_count * sizeof(double));
    dev->samp_rate_count = samp_rate_count;
    free(samp_rate_list);
    samp_rate_list = NULL;
  }

  ok = SU_TRUE;

done:
  if (freqRanges != NULL)
    free(freqRanges);

  if (dup != NULL)
    free(dup);

  SoapySDRStrings_clear(&antenna_list, antenna_count);
  SoapySDRStrings_clear(&gain_list, gain_count);

  if (samp_rate_list != NULL)
    free(samp_rate_list);

  /*
   * I literally have no idea what to do with this.
   */

  /*if (freqRanges != NULL)
    free(freqRanges); */

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
  int i;

  info->gain_desc_list = NULL;
  info->gain_desc_count = 0;

  if (strcmp(dev->interface, SUSCAN_SOURCE_LOCAL_INTERFACE) == 0) {
    if (!suscan_source_device_is_populated(dev))
        if (!suscan_source_device_populate_info((suscan_source_device_t *) dev))
            goto fail;

      /*
       * Populate gain desc info. This is performed by checking the epoch. If
       * this gain has not been seen in the current device discovery, just omit it.
       */
      for (i = 0; i < dev->gain_desc_count; ++i) {
        if (dev->gain_desc_list[i]->epoch == dev->epoch) {
          SU_TRYCATCH(
              PTR_LIST_APPEND_CHECK(
                  info->gain_desc,
                  dev->gain_desc_list[i]) != -1,
              goto fail);
        }
      }

      info->antenna_list    = (const char **) dev->antenna_list;
      info->antenna_count   = dev->antenna_count;

      info->samp_rate_list  = (const double *) dev->samp_rate_list;
      info->samp_rate_count = dev->samp_rate_count;

      info->freq_min        = dev->freq_min;
      info->freq_max        = dev->freq_max;
  } else {
    /*
     * In principle, for remote devices we could connect to the server
     * and retrieve this information. However, this is SLOW and may
     * fail, particularly if get_info is called amid parameter edition.
     * We will just keep this list empty, and populate it later.
     */
    info->antenna_list    = NULL;
    info->antenna_count   = 0;

    info->samp_rate_list  = NULL;
    info->samp_rate_count = 0;

    info->freq_min        = 0;
    info->freq_max        = 3e9;
  }

  return SU_TRUE;

fail:
  return SU_FALSE;
}

suscan_source_device_t *
suscan_source_device_new(const char *interface, const SoapySDRKwargs *args)
{
  suscan_source_device_t *new = NULL;
  const char *driver;
  unsigned int i;

  /* Not necessarily an error */
  if ((driver = SoapySDRKwargs_get((SoapySDRKwargs *) args, "driver")) == NULL)
    return NULL;

  SU_TRYCATCH(new = calloc(1, sizeof (suscan_source_device_t)), goto fail);

  new->interface = interface;

  SU_TRYCATCH(
      new->desc = suscan_source_device_build_desc(driver, args),
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

suscan_source_device_t *
suscan_source_device_dup(const suscan_source_device_t *self)
{
  return suscan_source_device_new(self->interface, self->args);
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
  suscan_source_device_t *dev;

  SUBOOL mutex_acquired = SU_FALSE;
  SUBOOL cont = SU_TRUE;

  SU_TRYCATCH(pthread_mutex_lock(&g_device_list_mutex) == 0, goto done);
  mutex_acquired = SU_TRUE;

  for (i = 0; i < device_count; ++i)
    if (device_list[i] != NULL) {
      dev = device_list[i];

      SU_TRYCATCH(pthread_mutex_unlock(&g_device_list_mutex) == 0, goto done);
      mutex_acquired = SU_FALSE;

      if (!(function)(dev, i, private)) {
        cont = SU_FALSE;
        goto done;
      }

      SU_TRYCATCH(pthread_mutex_lock(&g_device_list_mutex) == 0, goto done);
      mutex_acquired = SU_TRUE;
    }

done:
  if (mutex_acquired)
    (void) pthread_mutex_unlock(&g_device_list_mutex);

  return cont;
}

const suscan_source_device_t *
suscan_source_device_get_by_index(unsigned int index)
{
  SUBOOL mutex_acquired = SU_FALSE;
  const suscan_source_device_t *device = NULL;

  SU_TRYCATCH(pthread_mutex_lock(&g_device_list_mutex) == 0, goto done);
  mutex_acquired = SU_TRUE;

  if (index < device_count)
    device = device_list[index];

done:
  if (mutex_acquired)
    (void) pthread_mutex_unlock(&g_device_list_mutex);

  return device;
}

unsigned int
suscan_source_device_get_count(void)
{
  return device_count;
}


const suscan_source_device_t *
suscan_source_device_find_first_sdr(void)
{
  unsigned int i;
  suscan_source_device_t *device = null_device;
  SUBOOL mutex_acquired = SU_FALSE;

  SU_TRYCATCH(pthread_mutex_lock(&g_device_list_mutex) == 0, goto done);
  mutex_acquired = SU_TRUE;

  for (i = 0; i < device_count; ++i)
    if (device_list[i] != NULL && device_list[i] != null_device)
      if (device_list[i]->available &&
          strcmp(device_list[i]->driver, "audio") != 0) {
        device = device_list[i];
        goto done;
      }

done:
  if (mutex_acquired)
    (void) pthread_mutex_unlock(&g_device_list_mutex);

  return device;
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

/* Non-MT safe */
SUPRIVATE int
suscan_source_device_assert_index(const char *iface, const SoapySDRKwargs *args)
{
  int i;
  suscan_source_device_t *dev = NULL;

  if (args->size == 0)
    return null_device->index;

  for (i = 0; i < device_count; ++i)
    if (strcmp(iface, device_list[i]->interface) == 0)
      if (suscan_source_device_soapy_args_are_equal(device_list[i]->args, args))
        goto done;

  i = -1;

  if ((dev = suscan_source_device_new(iface, args)) != NULL) {
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

suscan_source_device_t *
suscan_source_device_assert(const char *interface, const SoapySDRKwargs *args)
{
  int index;
  suscan_source_device_t *result = NULL;

  SUBOOL mutex_acquired = SU_FALSE;

  SU_TRYCATCH(pthread_mutex_lock(&g_device_list_mutex) == 0, goto done);
  mutex_acquired = SU_TRUE;

  if ((index = suscan_source_device_assert_index(interface, args)) == -1)
    goto done;

  result = device_list[index];

done:
  if (mutex_acquired)
    (void) pthread_mutex_unlock(&g_device_list_mutex);

  return result;
}

SUBOOL
suscan_source_register_null_device(void)
{
  SoapySDRKwargs args;
  suscan_source_device_t *dev;

  char *keys[] = {"driver"};
  char *vals[] = {"null"};

  args.size = 1;
  args.keys = keys;
  args.vals = vals;

  SU_TRYCATCH(
      dev = suscan_source_device_assert(SUSCAN_SOURCE_LOCAL_INTERFACE, &args),
      return SU_FALSE);

  null_device = dev;

  return SU_TRUE;
}

SUBOOL
suscan_source_detect_devices(void)
{
  SoapySDRKwargs *soapy_dev_list = NULL;
  suscan_source_device_t *dev = NULL;
  size_t soapy_dev_len;
  unsigned int i;
  SUBOOL mutex_acquired = SU_FALSE;
  SUBOOL ok = SU_FALSE;

  suscan_source_reset_devices();

  if (soapysdr_module_path == NULL)
    soapysdr_module_path = suscan_bundle_get_soapysdr_module_path();

  if (soapysdr_module_path != NULL)
    setenv("SOAPY_SDR_PLUGIN_PATH", soapysdr_module_path, SU_TRUE);

  suscan_source_disable_stderr();

  SU_TRYCATCH(
      soapy_dev_list = SoapySDRDevice_enumerate(NULL, &soapy_dev_len),
      goto done);

  for (i = 0; i < soapy_dev_len; ++i) {
    SU_TRYCATCH(
        dev = suscan_source_device_assert(
            SUSCAN_SOURCE_LOCAL_INTERFACE,
            soapy_dev_list + i),
        goto done);
  }

  SU_TRYCATCH(pthread_mutex_lock(&g_device_list_mutex) != -1, goto done);
  mutex_acquired = SU_TRUE;

  /* First device is always null */
  for (i = 1; i < device_count; ++i) {
    /*
     * Populate device info. If this fails, don't pass exception:
     * there may be a problem with this device, but not with the rest of them.
     */
    if (!suscan_source_device_is_populated(dev))
      SU_TRYCATCH(suscan_source_device_populate_info(dev), continue);
  }

  ok = SU_TRUE;

done:
  if (mutex_acquired)
    (void) pthread_mutex_unlock(&g_device_list_mutex);

  suscan_source_enable_stderr();

  if (soapy_dev_list != NULL)
    SoapySDRKwargsList_clear(soapy_dev_list, soapy_dev_len);

  return ok;
}

const suscan_source_device_t *
suscan_source_get_null_device(void)
{
  return null_device;
}

SUBOOL
suscan_source_device_preinit(void)
{
  SU_TRYCATCH(
      pthread_mutex_init(&g_device_list_mutex, NULL) == 0,
      return SU_FALSE);

  return SU_TRUE;

}
