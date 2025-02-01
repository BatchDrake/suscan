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

#define SU_LOG_DOMAIN "soapysdr-discovery"

#include <sigutils/log.h>
#include <sigutils/util/compat-stdlib.h>
#include <analyzer/device/spec.h>
#include <analyzer/device/properties.h>
#include <analyzer/device/discovery.h>
#include <util/compat.h>
#include <SoapySDR/Types.h>

#include <SoapySDR/Device.h>
#include <SoapySDR/Modules.h>

SUPRIVATE const char *g_soapysdr_module_path = NULL;

struct soapysdr_discovery_ctx {
  SUBOOL cancelled;
};


SUPRIVATE SUBOOL
soapysdr_device_fix_airspy_rates(
  double **p_samp_rate_list,
  size_t  *p_samp_rate_count)
{
  double *samp_rate_list = NULL;
  size_t samp_rate_count = 0, i;
  SUBOOL needs_fix = SU_TRUE;
  SUBOOL ok = SU_FALSE;

  for (i = 0; i < *p_samp_rate_count; ++i)
    if (sufeq((*p_samp_rate_list)[i], 1e7, 1e6)) {
      needs_fix = SU_FALSE;
      break;
    }

  if (needs_fix) {
    samp_rate_count = *p_samp_rate_count + 1;

    SU_ALLOCATE_MANY(samp_rate_list, samp_rate_count, double);

    if (*p_samp_rate_count > 0 && *p_samp_rate_list != NULL)
      memcpy(
        samp_rate_list, 
        *p_samp_rate_list, 
        *p_samp_rate_count * sizeof(double));

    /* Add the missing sample rate */
    samp_rate_list[*p_samp_rate_count] = 1e7;

    if (*p_samp_rate_list != NULL)
      free(*p_samp_rate_list);

    *p_samp_rate_list = samp_rate_list;
    *p_samp_rate_count = samp_rate_count;

    samp_rate_list = NULL;
  }
  
  ok = SU_TRUE;

done:
  if (samp_rate_list != NULL)
    free(samp_rate_list);

  return ok;
}

SUPRIVATE SUBOOL
soapysdr_device_fix_rtlsdr_rates(
  double **p_samp_rate_list,
  size_t  *p_samp_rate_count)
{
  double *samp_rate_list = NULL;
  size_t samp_rate_count = 0, i;
  size_t valid = 0, p = 0;
  SUBOOL ok = SU_FALSE;

  /* 
   * The RTLSDR has a rather peculiar way to perform decimation.
   * Samples are delivered to the user in big fixed-size buffers, 
   * ensuring a good degree of throughput and interactivity for 
   * rates as high as 3.2 Msps. For smaller rates, however, 
   * decimated samples are queued to the same fixed-size buffers
   * until they get full, increasing the read sleep delay up to 12.8
   * times, resulting in choppy spectrum updates and a poor user
   * experience in general. We will workaround this issue by not 
   * allowing sample rates below 1 Msps.
   */
  for (i = 0; i < *p_samp_rate_count; ++i)
    if ((*p_samp_rate_list)[i] >= 1e6)
      ++valid;

  if (valid != *p_samp_rate_count) {
    if (valid > 0) {
      samp_rate_count = valid;
      SU_ALLOCATE_MANY(samp_rate_list, samp_rate_count, double);
      
      for (i = 0; i < *p_samp_rate_count; ++i)
        if ((*p_samp_rate_list)[i] >= 1e6)
          samp_rate_list[p++] = (*p_samp_rate_list)[i];
    }
    
    if (*p_samp_rate_list != NULL)
      free(*p_samp_rate_list);

    *p_samp_rate_list  = samp_rate_list;
    *p_samp_rate_count = samp_rate_count;

    samp_rate_list = NULL;
  }
  
  ok = SU_TRUE;

done:
  if (samp_rate_list != NULL)
    free(samp_rate_list);

  return ok;
}

SUBOOL
soapysdr_device_fix_rates(
    const char *driver,
    double **p_samp_rate_list,
    size_t  *p_samp_rate_count)
{
  SUBOOL ok = SU_FALSE;

  if (strcmp(driver, "airspy") == 0) {
    SU_TRY(
      soapysdr_device_fix_airspy_rates(
        p_samp_rate_list,
        p_samp_rate_count));
  } else if (strcmp(driver, "rtlsdr") == 0) {
    SU_TRY(
      soapysdr_device_fix_rtlsdr_rates(
        p_samp_rate_list,
        p_samp_rate_count));
  }
   
  ok = SU_TRUE;

done:
  return ok;
}

SUPRIVATE SUBOOL
soapysdr_discovery_populate(suscan_device_properties_t *prop, const SoapySDRKwargs *args)
{
  suscan_device_gain_desc_t *gain = NULL;
  const char *driver;
  const char *key, *val;

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
  unsigned int i;

  SUBOOL ok = SU_FALSE;

  if ((driver = SoapySDRKwargs_get(args, "driver")) == NULL) {
    SU_ERROR("SoapySDRKwargs do not specify a driver. Do not know how to construct.\n");
    goto done;
  }

  /* Make device */
  if ((sdev = SoapySDRDevice_make(args)) == NULL)
    goto done;

  /* Get number of channels */
  SU_TRY(
    suscan_device_properties_set_num_channels(
      prop,
      SoapySDRDevice_getNumChannels(sdev, SOAPY_SDR_RX)));

  /* Set frequency range */
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

    if (isinf(freq_min) || isinf(freq_max))
      freq_min = freq_max = 0;

    SU_TRY(suscan_device_properties_set_freq_range(prop, freq_min, freq_max));
  }

  /* Define antennas */
  if ((antenna_list = SoapySDRDevice_listAntennas(
          sdev,
          SOAPY_SDR_RX,
          0,
          &antenna_count)) != NULL)
    for (i = 0; i < antenna_count; ++i)
      SU_TRY(suscan_device_properties_add_antenna(prop, antenna_list[i]));

  /* Define gains */
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

      gain = suscan_device_properties_make_gain(
        prop,
        gain_list[i],
        range.minimum,
        range.maximum);

      gain->step = range.step;
      gain->def = SoapySDRDevice_getGainElement(
          sdev,
          SOAPY_SDR_RX,
          0,
          gain_list[i]);
    }
  }

  /* Define sample rates */
  SU_TRY(
      samp_rate_list = SoapySDRDevice_listSampleRates(
          sdev,
          SOAPY_SDR_RX,
          0,
          &samp_rate_count));

  /* Fix these sample rates, because sometimes SoapySDR reports them wrong */
  SU_TRY(soapysdr_device_fix_rates(driver, &samp_rate_list, &samp_rate_count));
  if (samp_rate_count == 0) {
    SU_ERROR("Device `%s' reports no sample rates\n", driver);
    goto done;
  }

  for (i = 0; i < samp_rate_count; ++i)
    SU_TRY(suscan_device_properties_add_samp_rate(prop, samp_rate_list[i]));

  /* Define traits. */
  for (i = 0; i < args->size; ++i) {
    key = args->keys[i];
    val = args->vals[i];

    if (strcmp(key, "driver") == 0) { /* Yes, we call the driver "device" */
      SU_TRY(suscan_device_properties_set_trait(prop, "device", val));
    } else if (strcmp(key, "serial") == 0) {
      SU_TRY(suscan_device_properties_set_trait(prop, "serial", val));
    } else if (strcmp(key, "label") == 0) {
      SU_TRY(suscan_device_properties_set_label(prop, val));
    }

    /* Ignore the rest */
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

  if (sdev != NULL)
    SoapySDRDevice_unmake(sdev);

  return ok;
}

SUPRIVATE void *
soapysdr_discovery_open()
{
  struct soapysdr_discovery_ctx *ctx = NULL;

  SU_ALLOCATE_FAIL(ctx, struct soapysdr_discovery_ctx);

  ctx->cancelled = SU_FALSE;

  return ctx;

fail:
  if (ctx != NULL)
    free(ctx);
  
  return NULL;
}

SUPRIVATE SUBOOL
soapysdr_discovery_discovery(void *userdata, suscan_device_discovery_t *disc)
{
  SoapySDRKwargs *soapy_dev_list = NULL;
  suscan_device_properties_t *prop = NULL;

  size_t soapy_dev_len;
  unsigned int i;
  SUBOOL ok = SU_FALSE;

  struct soapysdr_discovery_ctx *self = userdata;

  self->cancelled = SU_FALSE;

  if (g_soapysdr_module_path == NULL)
    g_soapysdr_module_path = suscan_bundle_get_soapysdr_module_path();

  if (g_soapysdr_module_path != NULL)
    setenv("SOAPY_SDR_PLUGIN_PATH", g_soapysdr_module_path, SU_TRUE);

  SU_TRY(soapy_dev_list = SoapySDRDevice_enumerate(NULL, &soapy_dev_len));

  /* First device is always null */
  for (i = 0; i < soapy_dev_len && !self->cancelled; ++i) {
    if (prop == NULL) {
      SU_MAKE(prop, suscan_device_properties, NULL);
      SU_TRY(suscan_device_properties_set_analyzer(prop, "local"));
      SU_TRY(suscan_device_properties_set_source(prop, "soapysdr"));
    }

    if (soapysdr_discovery_populate(prop, soapy_dev_list + i)) {
      SU_TRY(suscan_device_discovery_push_device(disc, prop));
      prop = NULL;
    }
  }

  ok = SU_TRUE;

done:
  if (soapy_dev_list != NULL)
    SoapySDRKwargsList_clear(soapy_dev_list, soapy_dev_len);

  if (prop != NULL)
    SU_DISPOSE(suscan_device_properties, prop);
  
  return ok;
}

SUPRIVATE SUBOOL
soapysdr_discovery_cancel(void *userdata)
{
  struct soapysdr_discovery_ctx *self = userdata;

  self->cancelled = SU_TRUE;

  return SU_TRUE;
}

SUPRIVATE SUBOOL
soapysdr_discovery_close(void *userdata)
{
  struct soapysdr_discovery_ctx *self = userdata;

  free(self);

  return SU_TRUE;
}

SUPRIVATE struct suscan_device_discovery_interface g_soapysdr_discovery = 
{
  .name      = "soapysdr",
  .open      = soapysdr_discovery_open,
  .discovery = soapysdr_discovery_discovery,
  .cancel    = soapysdr_discovery_cancel,
  .close     = soapysdr_discovery_close
};

SUBOOL
suscan_discovery_register_soapysdr()
{
  return suscan_device_discovery_register(&g_soapysdr_discovery);
}
