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

#ifndef _ANALYZER_SOURCE_DEVICE_H
#define _ANALYZER_SOURCE_DEVICE_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <sigutils/sigutils.h>
#include <SoapySDR/Types.h>
#include <string.h>

#define SUSCAN_SOURCE_LOCAL_INTERFACE   "local"
#define SUSCAN_SOURCE_REMOTE_INTERFACE  "remote"

/************************** Source device API ********************************/
struct suscan_source_gain_desc {
  int epoch;
  char *name;
  SUFLOAT min;
  SUFLOAT max;
  SUFLOAT step;
  SUFLOAT def;
};

struct suscan_source_device_info {
  /* Borrowed list */
  PTR_LIST_CONST(struct suscan_source_gain_desc, gain_desc);
  PTR_LIST_CONST(char, antenna);
  const double *samp_rate_list;
  unsigned int samp_rate_count;
  SUFREQ freq_min;
  SUFREQ freq_max;
};

#define suscan_source_device_info_INITIALIZER   \
{                                               \
  NULL, /* gain_list */                         \
  0, /* gain_count */                           \
  NULL, /* antenna_list */                      \
  0, /* antenna_count */                        \
  NULL, /* samp_rate_list */                    \
  0, /* samp_rate_count */                      \
  0, /* freq_min */                             \
  0, /* freq_max */                             \
}

void suscan_source_device_info_finalize(struct suscan_source_device_info *info);

struct suscan_source_device {
  const char *interface;
  char *driver;
  char *desc;
  SoapySDRKwargs *args;
  int index;
  SUBOOL available;
  int epoch;

  PTR_LIST(struct suscan_source_gain_desc, gain_desc);
  PTR_LIST(char, antenna);
  double *samp_rate_list;
  unsigned int samp_rate_count;

  SUFREQ freq_min;
  SUFREQ freq_max;
};

typedef struct suscan_source_device suscan_source_device_t;

SUINLINE const char *
suscan_source_device_get_param(
    const suscan_source_device_t *dev,
    const char *key)
{
  return SoapySDRKwargs_get(dev->args, key);
}

SUINLINE const char *
suscan_source_device_get_driver(const suscan_source_device_t *self)
{
  const char *driver;

  if ((driver = suscan_source_device_get_param(self, "driver")) == NULL)
    driver = self->driver;

  return driver;
}

SUINLINE SUBOOL
suscan_source_device_is_remote(const suscan_source_device_t *self)
{
  if (self->interface == NULL)
    return SU_FALSE;

  return strcmp(self->interface, SUSCAN_SOURCE_REMOTE_INTERFACE) == 0;
}

SUINLINE const char *
suscan_source_device_get_desc(const suscan_source_device_t *self)
{
  return self->desc;
}

SUINLINE int
suscan_source_device_get_index(const suscan_source_device_t *self)
{
  return self->index;
}

SUINLINE SUFREQ
suscan_source_device_get_min_freq(const suscan_source_device_t *self)
{
  return self->freq_min;
}

SUINLINE SUFREQ
suscan_source_device_get_max_freq(const suscan_source_device_t *self)
{
  return self->freq_max;
}

SUINLINE SUBOOL
suscan_source_device_is_available(const suscan_source_device_t *self)
{
  return self->available;
}

SUINLINE SUBOOL
suscan_source_device_is_populated(const suscan_source_device_t *self)
{
  /*
   * Remote devices are never populated
   */
  return !suscan_source_device_is_remote(self) && self->antenna_count != 0;
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

/* Internal */
struct suscan_source_gain_desc *suscan_source_device_assert_gain_unsafe(
    suscan_source_device_t *dev,
    const char *name,
    SUFLOAT min,
    SUFLOAT max,
    unsigned int step);

/* Internal */
SUBOOL suscan_source_device_preinit(void);

/* Internal */
SUBOOL suscan_source_register_null_device(void);

/* Internal */
const suscan_source_device_t *suscan_source_device_find_first_sdr(void);

/* Internal */
const struct suscan_source_gain_desc *suscan_source_device_lookup_gain_desc(
    const suscan_source_device_t *self,
    const char *name);

/* Internal */
const struct suscan_source_gain_desc *suscan_source_gain_desc_new_hidden(
    const char *name,
    SUFLOAT value);

/* Internal */
suscan_source_device_t *suscan_source_device_assert(
    const char *interface,
    const SoapySDRKwargs *args);

/* Internal */
SUBOOL suscan_source_device_fix_rates(
    const suscan_source_device_t *dev,
    double **p_samp_rate_list,
    size_t  *p_samp_rate_count);

/* Internal */
SUBOOL suscan_source_device_populate_info(suscan_source_device_t *self);

/* Internal */
suscan_source_device_t *suscan_source_device_new(
    const char *interface,
    const SoapySDRKwargs *args);

/* Internal */
suscan_source_device_t *suscan_source_device_dup(
    const suscan_source_device_t *self);

/* Internal */
void suscan_source_device_destroy(suscan_source_device_t *dev);

unsigned int suscan_source_device_get_count(void);

SUBOOL suscan_source_device_get_info(
    const suscan_source_device_t *self,
    unsigned int channel,
    struct suscan_source_device_info *info);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _ANALYZER_SOURCE_DEVICE_H */
