/*

  Copyright (C) 2017 Gonzalo José Carracedo Carballal

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

#ifndef _CONFIG_H
#define _CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <sigutils/util/util.h>
#include <object.h>
#include <sigutils/sigutils.h>
#include <analyzer/serialize.h>

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
  char *global_name;
  SUBOOL registered;
  PTR_LIST(struct suscan_field, field);
};

typedef struct suscan_config_desc suscan_config_desc_t;

SUSCAN_SERIALIZABLE(suscan_config) {
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

suscan_config_t *suscan_config_dup(const suscan_config_t *config);

void suscan_config_desc_destroy(suscan_config_desc_t *cfgdesc);

suscan_config_desc_t *suscan_config_desc_lookup(const char *global_name);
SUBOOL suscan_config_desc_register(suscan_config_desc_t *);

suscan_config_desc_t *suscan_config_desc_new_ex(const char *global_name);
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

SUBOOL suscan_object_to_config(
    suscan_config_t *config,
    const suscan_object_t *object);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _CONFIG_H */
