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

#ifndef _CONFIG_H
#define _CONFIG_H

#include <util.h>
#include <object.h>
#include <sigutils/sigutils.h>

enum suscan_field_type {
  SUSCAN_FIELD_TYPE_STRING,
  SUSCAN_FIELD_TYPE_INTEGER,
  SUSCAN_FIELD_TYPE_FLOAT,
  SUSCAN_FIELD_TYPE_FILE,
  SUSCAN_FIELD_TYPE_BOOLEAN
};

struct suscan_field;

struct suscan_field_value {
  SUBOOL set;
  const struct suscan_field *field;
  union {
    uint64_t as_int;
    SUBOOL   as_bool;
    SUFLOAT  as_float;
    char     as_string[0];
  };
};

struct suscan_field {
  enum suscan_field_type type;
  SUBOOL optional;
  char *name;
  char *desc;
};

struct suscan_config_desc {
  PTR_LIST(struct suscan_field, field);
};

typedef struct suscan_config_desc suscan_config_desc_t;

struct suscan_config {
  const suscan_config_desc_t *desc;
  struct suscan_field_value **values;
};

typedef struct suscan_config suscan_config_t;

struct suscan_field *suscan_config_desc_lookup_field(
    const suscan_config_desc_t *source,
    const char *name);

SUBOOL suscan_config_desc_has_prefix(
    const suscan_config_desc_t *desc,
    const char *pfx);

SUBOOL suscan_config_desc_add_field(
    suscan_config_desc_t *source,
    enum suscan_field_type type,
    SUBOOL optional,
    const char *name,
    const char *desc);

void suscan_config_destroy(suscan_config_t *config);

suscan_config_t *suscan_config_new(const suscan_config_desc_t *desc);

void suscan_config_desc_destroy(suscan_config_desc_t *cfgdesc);

suscan_config_desc_t *suscan_config_desc_new(void);

SUBOOL suscan_config_set_integer(
    suscan_config_t *cfg,
    const char *name,
    uint64_t value);

SUBOOL suscan_config_set_bool(
    suscan_config_t *cfg,
    const char *name,
    SUBOOL value);

SUBOOL suscan_config_set_float(
    suscan_config_t *cfg,
    const char *name,
    SUFLOAT value);

SUBOOL suscan_config_set_string(
    suscan_config_t *cfg,
    const char *name,
    const char *value);

SUBOOL suscan_config_set_file(
    suscan_config_t *cfg,
    const char *name,
    const char *value);

SUBOOL suscan_config_copy(
    suscan_config_t *dest,
    const suscan_config_t *src);

struct suscan_field_value *suscan_config_get_value(
    const suscan_config_t *cfg,
    const char *name);

suscan_config_t *suscan_string_to_config(
    const suscan_config_desc_t *desc,
    const char *string);

char *suscan_config_to_string(const suscan_config_t *config);

suscan_object_t *suscan_config_to_object(const suscan_config_t *config);

#endif /* _CONFIG_H */
