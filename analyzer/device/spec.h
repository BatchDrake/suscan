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

#ifndef _DEVICE_SPEC_H
#define _DEVICE_SPEC_H

#include <strmap.h>
#include <analyzer/serialize.h>
#include <util/object.h>

#ifdef __cplusplus
extern "C" {
#endif

struct suscan_device_properties;

SUSCAN_SERIALIZABLE(suscan_device_spec) {
  /* The following fields identify a single device alone */
  char *analyzer;   /* Analyzer type (local or remote) */
  char *source;     /* Signal source type (soapysdr...) */
  strmap_t traits;  /* Device traits */
  uint64_t uuid;

  /* The following fields are the ones may be modified for the same device */
  strmap_t params;

  int epoch;
  struct suscan_device_properties *properties; /* @mutable */
};

typedef struct suscan_device_spec suscan_device_spec_t;

SU_INSTANCER(suscan_device_spec);
SU_COLLECTOR(suscan_device_spec);
SU_DESTRUCTOR(suscan_device_spec);

char    *suscan_device_make_uri(const char *, const char *, strmap_t const *);
uint64_t suscan_device_make_uuid(const char *, const char *, strmap_t const *);

SU_GETTER(suscan_device_spec, suscan_device_spec_t *, copy);
SU_GETTER(suscan_device_spec, struct suscan_device_properties *, properties);
SU_GETTER(suscan_device_spec, strmap_t *, make_args);
SU_GETTER(suscan_device_spec, const char *, analyzer);
SU_GETTER(suscan_device_spec, const char *, source);
SU_GETTER(suscan_device_spec, const char *, get, const char *);
SU_GETTER(suscan_device_spec, strmap_t *, get_all);
SU_GETTER(suscan_device_spec, char *, to_uri);
SU_GETTER(suscan_device_spec, suscan_object_t *, to_object);
SU_GETTER(suscan_device_spec, uint64_t, uuid);

suscan_device_spec_t *suscan_device_spec_from_uri(const char *);
suscan_device_spec_t *suscan_device_spec_from_object(const suscan_object_t *object);

/* Used to handle the user-specific tweaks */
SU_METHOD(suscan_device_spec, void,   reset);
SU_METHOD(suscan_device_spec, void,   swap, suscan_device_spec_t *);
SU_METHOD(suscan_device_spec, SUBOOL, set, const char *, const char *);
SU_METHOD(suscan_device_spec, SUBOOL, set_analyzer, const char *);
SU_METHOD(suscan_device_spec, SUBOOL, set_source, const char *);
SU_METHOD(suscan_device_spec, SUBOOL, set_traits, const strmap_t *);
SU_METHOD(suscan_device_spec, SUBOOL, set_params, const strmap_t *);
SU_METHOD(suscan_device_spec, void,   update_uuid);

#ifdef __cplusplus
}
#endif

#endif /* _DEVICE_SPEC_H */
