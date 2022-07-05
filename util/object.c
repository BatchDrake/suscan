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

#include <string.h>

#define SU_LOG_DOMAIN "object"

#include <sigutils/log.h>
#include "object.h"
#include <inttypes.h>

void
suscan_object_destroy(suscan_object_t *obj)
{
  unsigned int i;

  switch (obj->type) {
    case SUSCAN_OBJECT_TYPE_OBJECT:
      for (i = 0; i < obj->field_count; ++i)
        if (obj->field_list[i] != NULL)
          suscan_object_destroy(obj->field_list[i]);

      if (obj->field_list != NULL)
        free(obj->field_list);

      break;

    case SUSCAN_OBJECT_TYPE_SET:
      for (i = 0; i < obj->object_count; ++i)
        if (obj->object_list[i] != NULL)
          suscan_object_destroy(obj->object_list[i]);

      if (obj->object_list != NULL)
        free(obj->object_list);

      break;

    case SUSCAN_OBJECT_TYPE_FIELD:
      if (obj->value != NULL)
        free(obj->value);
  }

  if (obj->name != NULL)
    free(obj->name);

  if (obj->class_name != NULL)
    free(obj->class_name);

  free(obj);
}

suscan_object_t *
suscan_object_new(enum suscan_object_type type)
{
  suscan_object_t *new = NULL;

  SU_TRYCATCH(new = calloc(1, sizeof(suscan_object_t)), goto fail);

  new->type = type;

  return new;

fail:
  if (new != NULL)
    suscan_object_destroy(new);

  return NULL;
}

suscan_object_t *
suscan_object_copy(const suscan_object_t *object)
{
  suscan_object_t *new = NULL;
  suscan_object_t *dup = NULL;
  unsigned int i;

  SU_MAKE_FAIL(new, suscan_object, object->type);

  if (object->name != NULL)
    SU_TRY_FAIL(suscan_object_set_name(new, object->name));

  if (object->class_name != NULL)
    SU_TRY_FAIL(suscan_object_set_class(new, object->class_name));

  switch (object->type) {
    case SUSCAN_OBJECT_TYPE_FIELD:
      SU_TRY_FAIL(suscan_object_set_value(new, object->value));
      break;

    case SUSCAN_OBJECT_TYPE_OBJECT:
      for (i = 0; i < object->field_count; ++i) {
        if (object->field_list[i] != NULL)
          SU_TRY_FAIL(dup = suscan_object_copy(object->field_list[i]));
        SU_TRYC_FAIL(PTR_LIST_APPEND_CHECK(new->field, dup));
        dup = NULL;
      }
    break;

    case SUSCAN_OBJECT_TYPE_SET:
      for (i = 0; i < object->object_count; ++i) {
        if (object->object_list[i] != NULL)
          SU_TRY_FAIL(dup = suscan_object_copy(object->field_list[i]));
        SU_TRYC_FAIL(PTR_LIST_APPEND_CHECK(new->field, dup));
        dup = NULL;
      }
      break;

    default:
      SU_ERROR("Invalid object type during deep copy (%d)\n", object->type);
      goto fail;
  }

  return new;

fail:
  if (dup != NULL)
    suscan_object_destroy(dup);

  if (new != NULL)
    suscan_object_destroy(new);

  return NULL;
}

const char *
suscan_object_get_class(const suscan_object_t *object)
{
  return object->class_name;
}

SUBOOL
suscan_object_set_class(suscan_object_t *object, const char *class)
{
  char *classdup = NULL;

  if (object->class_name != class) {
    if (class != NULL)
      SU_TRYCATCH(classdup = strdup(class), return SU_FALSE);

    if (object->class_name != NULL)
      free(object->class_name);

    object->class_name = classdup;
  }

  return SU_TRUE;
}

const char *
suscan_object_get_name(const suscan_object_t *object)
{
  return object->name;
}

SUBOOL
suscan_object_set_name(suscan_object_t *object, const char *name)
{
  char *namedup = NULL;

  if (object->name != name) {
    if (name != NULL)
      SU_TRYCATCH(namedup = strdup(name), return SU_FALSE);

    if (object->name != NULL)
      free(object->name);

    object->name = namedup;
  }

  return SU_TRUE;
}

enum suscan_object_type
suscan_object_get_type(const suscan_object_t *object)
{
  return object->type;
}


suscan_object_t **
suscan_object_lookup(const suscan_object_t *object, const char *name)
{
  unsigned int i;

  SU_TRYCATCH(object->type == SUSCAN_OBJECT_TYPE_OBJECT, return NULL);

  for (i = 0; i < object->field_count; ++i)
    if (object->field_list[i] != NULL)
      if (strcmp(object->field_list[i]->name, name) == 0)
        return object->field_list + i;

  return NULL;
}

SUBOOL
suscan_object_set_field(
    suscan_object_t *object,
    const char *name,
    suscan_object_t *new)
{
  suscan_object_t **entry = NULL;

  SU_TRYCATCH(object->type == SUSCAN_OBJECT_TYPE_OBJECT, return SU_FALSE);

  if (new != NULL)
    SU_TRYCATCH(suscan_object_set_name(new, name), return SU_FALSE);

  entry = suscan_object_lookup(object, name);

  if (entry != NULL) {
    if (*entry != new) {
      suscan_object_destroy(*entry);
      *entry = new;
    }
  } else if (new != NULL) {
    SU_TRYCATCH(
        PTR_LIST_APPEND_CHECK(object->field, new) != -1,
        return SU_FALSE);
  }

  return SU_TRUE;
}

suscan_object_t *
suscan_object_get_field(const suscan_object_t *object, const char *name)
{
  suscan_object_t **entry = NULL;

  SU_TRYCATCH(object->type == SUSCAN_OBJECT_TYPE_OBJECT, return NULL);

  entry = suscan_object_lookup(object, name);

  if (entry != NULL)
    return *entry;

  return NULL;
}

SUBOOL
suscan_object_clear_fields(suscan_object_t *object)
{
  unsigned int i;

  SU_TRYCATCH(object->type == SUSCAN_OBJECT_TYPE_OBJECT, return SU_FALSE);

  for (i = 0; i < object->field_count; ++i)
    if (object->field_list[i] != NULL)
      suscan_object_destroy(object->field_list[i]);

  if (object->field_list != NULL)
    free(object->field_list);

  object->field_list = NULL;
  object->field_count = 0;

  return SU_TRUE;
}

const char *
suscan_object_get_value(const suscan_object_t *object)
{
  SU_TRYCATCH(object->type == SUSCAN_OBJECT_TYPE_FIELD, return SU_FALSE);

  return object->value;
}

SUBOOL
suscan_object_set_value(
    suscan_object_t *object,
    const char *value)
{
  char *valuedup = NULL;

  SU_TRYCATCH(object->type == SUSCAN_OBJECT_TYPE_FIELD, return SU_FALSE);

  if (value != object->value) {
    if (value != NULL)
      SU_TRYCATCH(valuedup = strdup(value), return SU_FALSE);

    if (object->value != NULL)
      free(object->value);

    object->value = valuedup;
  }

  return SU_TRUE;
}

SUBOOL
suscan_object_set_field_value(
    suscan_object_t *object,
    const char *name,
    const char *value)
{
  suscan_object_t **entry;
  suscan_object_t *new = NULL;

  SU_TRYCATCH(object->type == SUSCAN_OBJECT_TYPE_OBJECT, return SU_FALSE);

  entry = suscan_object_lookup(object, name);

  if (entry != NULL) {
    return suscan_object_set_value(*entry, value);
  } else {
    /* Entry does not exist, create new */
    SU_TRYCATCH(new = suscan_object_new(SUSCAN_OBJECT_TYPE_FIELD), goto fail);
    SU_TRYCATCH(suscan_object_set_value(new, value), goto fail);
    SU_TRYCATCH(suscan_object_set_field(object, name, new), goto fail);
  }

  return SU_TRUE;

fail:
  if (new != NULL)
    suscan_object_destroy(new);

  return SU_FALSE;
}

const char *
suscan_object_get_field_value(const suscan_object_t *object, const char *name)
{
  suscan_object_t *field;

  if ((field = suscan_object_get_field(object, name)) != NULL)
    return field->value;

  return NULL;
}


int
suscan_object_get_field_int(
    const suscan_object_t *object,
    const char *name,
    int dfl)
{
  const char *text;
  int got;

  if ((text = suscan_object_get_field_value(object, name)) != NULL)
    if (sscanf(text, "%i", &got) == 1)
      dfl = got;

  return dfl;
}

struct timeval
suscan_object_get_field_tv(
    const suscan_object_t *object,
    const char *name,
    const struct timeval *tv)
{
  struct timeval result = *tv;
  const char *text;
  uint64_t secs;
  uint32_t usecs;

  if ((text = suscan_object_get_field_value(object, name)) != NULL)
    if (sscanf(text, "%" SCNu64 ".%06u", &secs, &usecs) == 2) {
      result.tv_sec  = secs;
      result.tv_usec = usecs;
    }

  return result;
}

unsigned int
suscan_object_get_field_uint(
    const suscan_object_t *object,
    const char *name,
    unsigned int dfl)
{
  const char *text;
  unsigned int got;

  if ((text = suscan_object_get_field_value(object, name)) != NULL)
    if (sscanf(text, "%u", &got) == 1)
      dfl = got;

  return dfl;
}


SUFLOAT
suscan_object_get_field_float(
    const suscan_object_t *object,
    const char *name,
    SUFLOAT dfl)
{
  const char *text;
  SUFLOAT got;

  if ((text = suscan_object_get_field_value(object, name)) != NULL)
    if (sscanf(text, SUFLOAT_SCANF_FMT, &got) == 1)
      dfl = got;

  return dfl;
}

SUDOUBLE
suscan_object_get_field_double(
    const suscan_object_t *object,
    const char *name,
    SUDOUBLE dfl)
{
  const char *text;
  SUDOUBLE got;

  if ((text = suscan_object_get_field_value(object, name)) != NULL)
    if (sscanf(text, SUDOUBLE_SCANF_FMT, &got) == 1)
      dfl = got;

  return dfl;
}

SUBOOL
suscan_object_get_field_bool(
    const suscan_object_t *object,
    const char *name,
    SUBOOL dfl)
{
  const char *text;

  if ((text = suscan_object_get_field_value(object, name)) != NULL) {
    if (strcasecmp(text, "false") == 0
        || strcasecmp(text, "0") == 0
        || strcasecmp(text, "no") == 0)
      dfl = SU_FALSE;
    else if (strcasecmp(text, "true") == 0
        || strcasecmp(text, "1") == 0
        || strcasecmp(text, "yes") == 0)
      dfl = SU_TRUE;
  }

  return dfl;
}

SUBOOL
suscan_object_set_field_uint(
    suscan_object_t *object,
    const char *name,
    unsigned int value)
{
  char *as_text = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(as_text = strbuild("%u", value), goto done);

  SU_TRYCATCH(suscan_object_set_field_value(object, name, as_text), goto done);

  ok = SU_TRUE;

done:
  if (as_text != NULL)
    free(as_text);

  return ok;
}

SUBOOL
suscan_object_set_field_int(
    suscan_object_t *object,
    const char *name,
    int value)
{
  char *as_text = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(as_text = strbuild("%i", value), goto done);

  SU_TRYCATCH(suscan_object_set_field_value(object, name, as_text), goto done);

  ok = SU_TRUE;

done:
  if (as_text != NULL)
    free(as_text);

  return ok;
}

SUBOOL
suscan_object_set_field_tv(
    suscan_object_t *object,
    const char *name,
    struct timeval tv)
{
  char *as_text = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
    as_text = strbuild("%lu.%06u", tv.tv_sec, tv.tv_usec), 
    goto done);

  SU_TRYCATCH(suscan_object_set_field_value(object, name, as_text), goto done);

  ok = SU_TRUE;

done:
  if (as_text != NULL)
    free(as_text);

  return ok;
}

SUBOOL
suscan_object_set_field_float(
    suscan_object_t *object,
    const char *name,
    SUFLOAT value)
{
  char *as_text = NULL;
  char *comma = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(as_text = strbuild(SUFLOAT_PRECISION_FMT, value), goto done);

  /* Thanks, GTK */
  if ((comma = strchr(as_text, ',')) != NULL)
    *comma = '.';

  SU_TRYCATCH(suscan_object_set_field_value(object, name, as_text), goto done);

  ok = SU_TRUE;

done:
  if (as_text != NULL)
    free(as_text);

  return ok;
}

SUBOOL
suscan_object_set_field_double(
    suscan_object_t *object,
    const char *name,
    SUDOUBLE value)
{
  char *as_text = NULL;
  char *comma = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(as_text = strbuild(SUDOUBLE_PRECISION_FMT, value), goto done);

  /* Thanks, GTK */
  if ((comma = strchr(as_text, ',')) != NULL)
    *comma = '.';

  SU_TRYCATCH(suscan_object_set_field_value(object, name, as_text), goto done);

  ok = SU_TRUE;

done:
  if (as_text != NULL)
    free(as_text);

  return ok;
}

SUBOOL
suscan_object_set_field_bool(
    suscan_object_t *object,
    const char *name,
    SUBOOL value)
{

  return suscan_object_set_field_value(
      object,
      name,
      value ? "true" : "false");
}

/* Convenience methods, to iterate through all fields */
unsigned int
suscan_object_field_count(const suscan_object_t *object)
{
  SU_TRYCATCH(object->type == SUSCAN_OBJECT_TYPE_OBJECT, return 0);

  return object->object_count;
}

suscan_object_t *
suscan_object_get_field_by_index(
    const suscan_object_t *object,
    unsigned int index)
{
  SU_TRYCATCH(object->type == SUSCAN_OBJECT_TYPE_OBJECT, return NULL);
  SU_TRYCATCH(index < object->object_count, return NULL);

  return object->object_list[index];
}

/* For set-type objects only */
unsigned int
suscan_object_set_get_count(const suscan_object_t *object)
{
  SU_TRYCATCH(object->type == SUSCAN_OBJECT_TYPE_SET, return 0);

  return object->object_count;
}

suscan_object_t *
suscan_object_set_get(const suscan_object_t *object, unsigned int index)
{
  SU_TRYCATCH(object->type == SUSCAN_OBJECT_TYPE_SET, return NULL);
  SU_TRYCATCH(index < object->object_count, return NULL);

  return object->object_list[index];
}

SUBOOL
suscan_object_set_put(
    suscan_object_t *object,
    unsigned int index,
    suscan_object_t *new)
{
  SU_TRYCATCH(object->type == SUSCAN_OBJECT_TYPE_SET, return SU_FALSE);
  SU_TRYCATCH(index < object->object_count, return SU_FALSE);

  if (object->object_list[index] != NULL && new != object->object_list[index])
    suscan_object_destroy(object->object_list[index]);

  object->object_list[index] = new;

  return SU_TRUE;
}

SUBOOL
suscan_object_set_delete(suscan_object_t *object, unsigned int index)
{
  return suscan_object_set_put(object, index, NULL);
}

SUBOOL
suscan_object_set_append(suscan_object_t *object, suscan_object_t *new)
{
  SU_TRYCATCH(object->type == SUSCAN_OBJECT_TYPE_SET, return SU_FALSE);

  SU_TRYCATCH(
      PTR_LIST_APPEND_CHECK(object->object, new) != -1,
      return SU_FALSE);

  return SU_TRUE;
}

SUBOOL
suscan_object_set_clear(suscan_object_t *object)
{
  unsigned int i;

  SU_TRYCATCH(object->type == SUSCAN_OBJECT_TYPE_SET, return SU_FALSE);

  for (i = 0; i < object->object_count; ++i)
    if (object->object_list[i] != NULL)
      suscan_object_destroy(object->object_list[i]);

  if (object->object_list != NULL)
    free(object->object_list);

  object->object_list = NULL;
  object->object_count = 0;

  return SU_TRUE;
}
