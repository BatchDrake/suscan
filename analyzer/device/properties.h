/*

  Copyright (C) 2024 Gonzalo José Carracedo Carballal

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

#ifndef _DEVICE_PROPERTIES_H
#define _DEVICE_PROPERTIES_H

#include <sigutils/types.h>
#include <sigutils/defs.h>
#include <strmap.h>

#ifdef __cplusplus
extern "C" {
#endif

struct suscan_device_spec;
struct suscan_analyzer_interface;
struct suscan_source_interface;
struct suscan_device_discovery;

#define SUSCAN_DEVICE_UUID_INVALID 0xffffffffffffffffull

/* This replaces the old "source gain desc" */
struct suscan_device_gain_desc {
  char   *name;
  SUFLOAT min;
  SUFLOAT max;
  SUFLOAT step;
  SUFLOAT def;
};

typedef struct suscan_device_gain_desc suscan_device_gain_desc_t;

SU_INSTANCER(suscan_device_gain_desc, const char *, SUFLOAT, SUFLOAT);
SU_COLLECTOR(suscan_device_gain_desc);

struct suscan_device_properties {
  const struct suscan_analyzer_interface *analyzer;
  const struct suscan_source_interface   *source;
  char                                   *label;
  unsigned int                            epoch;
  strmap_t                                traits;
  uint64_t                                uuid;

  double       *samp_rate_list;
  unsigned int  samp_rate_count;
  SUFREQ        freq_min;
  SUFREQ        freq_max;
  unsigned int  channels;

  hashlist_t gain_map;
  PTR_LIST(struct suscan_device_gain_desc, gain_desc);
  PTR_LIST(char,  antenna);

  /* INTERNAL */
  struct suscan_device_discovery *discovery;
};

typedef struct suscan_device_properties suscan_device_properties_t;

SU_METHOD(suscan_device_properties, SUINLINE void, set_epoch, unsigned epoch)
{
  self->epoch = epoch;
}

SU_INSTANCER(suscan_device_properties, const char *);
SU_COPY_INSTANCER(suscan_device_properties);
SU_COLLECTOR(suscan_device_properties);

SU_METHOD(suscan_device_properties, void,   swap, suscan_device_properties_t *);

SU_GETTER(suscan_device_properties, SUBOOL, match, const struct suscan_device_spec *);
SU_GETTER(suscan_device_properties, struct suscan_device_spec *, make_spec);
SU_GETTER(suscan_device_properties, uint64_t, uuid);
SU_GETTER(suscan_device_properties, char *, uri);
SU_GETTER(suscan_device_properties, suscan_device_gain_desc_t *, lookup_gain, const char *);
SU_GETTER(suscan_device_properties, int, get_all_gains, suscan_device_gain_desc_t *const **);
SU_GETTER(suscan_device_properties, const char *, get, const char *);

SU_METHOD(suscan_device_properties, SUBOOL, set_analyzer, const char *);
SU_METHOD(suscan_device_properties, SUBOOL, set_source, const char *);
SU_METHOD(suscan_device_properties, SUBOOL, set_label, const char *);
SU_METHOD(suscan_device_properties, SUBOOL, set_trait, const char *, const char *);
SU_METHOD(suscan_device_properties, SUBOOL, add_antenna, const char *);
SU_METHOD(suscan_device_properties, SUBOOL, add_gain, const suscan_device_gain_desc_t *);
SU_METHOD(suscan_device_properties, suscan_device_gain_desc_t *, make_gain, const char *, SUFLOAT, SUFLOAT);
SU_METHOD(suscan_device_properties, SUBOOL, add_samp_rate, double);
SU_METHOD(suscan_device_properties, SUBOOL, set_freq_range, SUFREQ, SUFREQ);
SU_METHOD(suscan_device_properties, SUBOOL, set_num_channels, unsigned);
SU_METHOD(suscan_device_properties, SUBOOL, update_uuid);

#ifdef __cplusplus
}
#endif

#endif /* _DEVICE_PROPERTIES_H */
