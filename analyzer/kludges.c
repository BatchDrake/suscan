/*

  Copyright (C) 2022 Gonzalo José Carracedo Carballal

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

#include <util/compat-stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <libgen.h>

#define SU_LOG_DOMAIN "device-kludges"

#include "source.h"

SUPRIVATE SUBOOL
suscan_source_device_fix_airspy_rates(
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
suscan_source_device_fix_rtlsdr_rates(
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
suscan_source_device_fix_rates(
    const suscan_source_device_t *dev,
    double **p_samp_rate_list,
    size_t  *p_samp_rate_count)
{
  SUBOOL ok = SU_FALSE;
  const char *driver = dev->driver;

  if (strcmp(driver, "airspy") == 0) {
    SU_TRY(
      suscan_source_device_fix_airspy_rates(
        p_samp_rate_list,
        p_samp_rate_count));
  } else if (strcmp(driver, "rtlsdr") == 0) {
    SU_TRY(
      suscan_source_device_fix_rtlsdr_rates(
        p_samp_rate_list,
        p_samp_rate_count));
  }
   
  ok = SU_TRUE;

done:
  return ok;
}