/*

  Copyright (C) 2018 Gonzalo José Carracedo Carballal

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

#ifndef _UTIL_OBJECT_H
#define _UTIL_OBJECT_H

#include "util.h"
#include <sigutils/types.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define SUSCAN_YAML_PFX "tag:actinid.org,2022:suscan:"

enum suscan_object_type {
  SUSCAN_OBJECT_TYPE_OBJECT,
  SUSCAN_OBJECT_TYPE_SET,
  SUSCAN_OBJECT_TYPE_FIELD,
};

struct suscan_object {
  enum suscan_object_type type;

  char *name; /* May be NULL */
  char *class_name; /* May be NULL as well */

  union {
    char *value;
    struct {
      PTR_LIST(struct suscan_object, field);
    };
    struct {
      PTR_LIST(struct suscan_object, object);
    };
  };
};

typedef struct suscan_object suscan_object_t;

suscan_object_t *suscan_object_from_xml(
    const char *url,
    const void *data,
    size_t size);

suscan_object_t *suscan_object_from_yaml(
    const void *data,
    size_t size);

SUBOOL suscan_object_to_xml(
    const suscan_object_t *object,
    void **data,
    size_t *size);

SUBOOL suscan_object_to_yaml(
    const suscan_object_t *object,
    void **data,
    size_t *size);

suscan_object_t *suscan_object_new(enum suscan_object_type type);

void suscan_object_destroy(suscan_object_t *object);

const char *suscan_object_get_class(const suscan_object_t *object);

SUBOOL suscan_object_set_class(
    suscan_object_t *object,
    const char *class_name);

enum suscan_object_type suscan_object_get_type(const suscan_object_t *object);

/* For object-type objects only */
suscan_object_t *suscan_object_get_field(
    const suscan_object_t *object,
    const char *name);

SUBOOL suscan_object_clear_fields(suscan_object_t *object);

SUBOOL suscan_object_set_field(
    suscan_object_t *object,
    const char *name,
    suscan_object_t *value);

unsigned int suscan_object_field_count(const suscan_object_t *object);

suscan_object_t *suscan_object_get_field_by_index(
    const suscan_object_t *object,
    unsigned int index);

/* Convenience functions */
int suscan_object_get_field_int(
    const suscan_object_t *object,
    const char *name,
    int dfl);

struct timeval suscan_object_get_field_tv(
    const suscan_object_t *object,
    const char *name,
    const struct timeval *tv);

SUBOOL suscan_object_get_field_bool(
    const suscan_object_t *object,
    const char *name,
    SUBOOL dfl);

unsigned int suscan_object_get_field_uint(
    const suscan_object_t *object,
    const char *name,
    unsigned int dfl);

SUFLOAT suscan_object_get_field_float(
    const suscan_object_t *object,
    const char *name,
    SUFLOAT dfl);

SUDOUBLE suscan_object_get_field_double(
    const suscan_object_t *object,
    const char *name,
    SUDOUBLE dfl);

SUBOOL suscan_object_set_field_uint(
    suscan_object_t *object,
    const char *name,
    unsigned int value);

SUBOOL suscan_object_set_field_int(
    suscan_object_t *object,
    const char *name,
    int value);

SUBOOL suscan_object_set_field_tv(
    suscan_object_t *object,
    const char *name,
    struct timeval tv);

SUBOOL suscan_object_set_field_float(
    suscan_object_t *object,
    const char *name,
    SUFLOAT value);

SUBOOL suscan_object_set_field_double(
    suscan_object_t *object,
    const char *name,
    SUDOUBLE value);

SUBOOL suscan_object_set_field_bool(
    suscan_object_t *object,
    const char *name,
    SUBOOL value);

/* For field-type objects only */
const char *suscan_object_get_name(const suscan_object_t *object);

const char *suscan_object_get_value(const suscan_object_t *object);

SUBOOL suscan_object_set_name(
    suscan_object_t *object,
    const char *name);

SUBOOL suscan_object_set_value(
    suscan_object_t *object,
    const char *value);

SUBOOL suscan_object_set_field_value(
    suscan_object_t *object,
    const char *name,
    const char *value);

const char *suscan_object_get_field_value(
    const suscan_object_t *object,
    const char *name);

/* For set-type objects only */
unsigned int suscan_object_set_get_count(const suscan_object_t *object);

suscan_object_t *suscan_object_set_get(
    const suscan_object_t *object,
    unsigned int index);

SUBOOL suscan_object_set_put(
    suscan_object_t *object,
    unsigned int index,
    suscan_object_t *new_obj);

SUBOOL suscan_object_set_delete(
    suscan_object_t *object,
    unsigned int index);

SUBOOL suscan_object_set_append(
    suscan_object_t *object,
    suscan_object_t *new_obj);

SUBOOL suscan_object_set_clear(suscan_object_t *object);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _UTIL_OBJECT_H */

