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

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <libgen.h>
#include <ctk.h>
#include "suscan.h"

/* Will never be freed */
PTR_LIST(struct suscan_source, source);

struct suscan_source *
suscan_source_lookup(const char *name)
{
  unsigned int i;

  for (i = 0; i < source_count; ++i)
    if (source_list[i] != NULL)
      if (strcasecmp(source_list[i]->name, name) == 0)
        return source_list[i];

  return NULL;
}

struct suscan_source *
suscan_source_register(
    const char *name,
    const char *desc,
    su_block_t *(*ctor) (const struct suscan_source_config *))
{
  char *name_dup = NULL;
  char *desc_dup = NULL;
  struct suscan_source *new = NULL;

  /* We cannot have two sources with the same name */
  if (suscan_source_lookup(name) != NULL)
    goto fail;

  if ((name_dup = strdup(name)) == NULL)
    goto fail;

  if ((desc_dup = strdup(desc)) == NULL)
    goto fail;

  if ((new = calloc(1, sizeof(struct suscan_source))) == NULL)
    goto fail;

  new->name = name_dup;
  new->desc = desc_dup;
  new->ctor = ctor;

  if (PTR_LIST_APPEND_CHECK(source, new) == -1)
    goto fail;

  return new;

fail:
  if (name_dup != NULL)
    free(name_dup);

  if (desc_dup != NULL)
    free(desc_dup);

  if (new != NULL)
    free(new);

  return NULL;
}

int
suscan_source_lookup_field_id(
    const struct suscan_source *source,
    const char *name)
{
  int i;

  for (i = 0; i < source->field_count; ++i)
    if (source->field_list[i] != NULL)
      if (strcmp(source->field_list[i]->name, name) == 0)
        return i;

  return -1;
}

struct suscan_field *
suscan_source_field_id_to_field(const struct suscan_source *source, int id)
{
  if (id < 0 || id >= source->field_count)
    return NULL;

  return source->field_list[id];
}

struct suscan_field *
suscan_source_lookup_field(const struct suscan_source *source, const char *name)
{
  return suscan_source_field_id_to_field(
      source,
      suscan_source_lookup_field_id(source, name));
}

SUBOOL
suscan_source_add_field(
    struct suscan_source *source,
    enum suscan_field_type type,
    const char *name,
    const char *desc)
{
  struct suscan_field *field;
  char *name_dup = NULL;
  char *desc_dup = NULL;

  if (suscan_source_lookup_field_id(source, name) != -1)
    goto fail;

  if ((name_dup = strdup(name)) == NULL)
    goto fail;

  if ((desc_dup = strdup(desc)) == NULL)
    goto fail;

  if ((field = calloc(1, sizeof(struct suscan_field))) == NULL)
    goto fail;

  field->type = type;
  field->name = name_dup;
  field->desc = desc_dup;

  if (PTR_LIST_APPEND_CHECK(source->field, field) == -1)
    goto fail;

  return SU_TRUE;

fail:
  if (name_dup != NULL)
    free(name_dup);

  if (desc_dup != NULL)
    free(desc_dup);

  if (field != NULL)
    free(field);

  return SU_FALSE;
}

void
suscan_source_config_destroy(struct suscan_source_config *config)
{
  unsigned int i;

  if (config->source != NULL && config->values != NULL) {
    for (i = 0; i < config->source->field_count; ++i)
      if (config->values[i] != NULL)
        free(config->values[i]);

    free(config->values);
  }

  free(config);
}

struct suscan_source_config *
suscan_source_config_new(const struct suscan_source *source)
{
  struct suscan_source_config *new = NULL;
  unsigned int i;

  if ((new = calloc(1, sizeof(struct suscan_source_config))) == NULL)
    goto fail;

  if ((new->values = calloc(source->field_count, sizeof(void *))) == NULL)
    goto fail;

  new->source = source;

  /* Allocate space for all fields */
  for (i = 0; i < source->field_count; ++i)
    if ((new->values[i] = calloc(1, sizeof(union suscan_field_value))) == NULL)
      goto fail;

  return new;

fail:
  if (new != NULL)
    suscan_source_config_destroy(new);

  return NULL;
}

SUBOOL
suscan_source_config_set_integer(
    struct suscan_source_config *cfg,
    const char *name,
    uint64_t value)
{
  const struct suscan_field *field;
  int id;

  /* Assert field and field type */
  if ((id = suscan_source_lookup_field_id(cfg->source, name)) == -1)
    return SU_FALSE;

  field = suscan_source_field_id_to_field(cfg->source, id);

  if (field->type != SUSCAN_FIELD_TYPE_INTEGER)
    return SU_FALSE;

  cfg->values[id]->as_int = value;

  return SU_TRUE;
}

SUBOOL
suscan_source_config_set_float(
    struct suscan_source_config *cfg,
    const char *name,
    SUFLOAT value)
{
  const struct suscan_field *field;
  int id;

  /* Assert field and field type */
  if ((id = suscan_source_lookup_field_id(cfg->source, name)) == -1)
    return SU_FALSE;

  field = suscan_source_field_id_to_field(cfg->source, id);

  if (field->type != SUSCAN_FIELD_TYPE_FLOAT)
    return SU_FALSE;

  cfg->values[id]->as_float = value;

  return SU_TRUE;
}

SUBOOL
suscan_source_config_set_string(
    struct suscan_source_config *cfg,
    const char *name,
    const char *value)
{
  const struct suscan_field *field;
  union suscan_field_value *tmp;
  size_t str_size;
  int id;

  /* Assert field and field type */
  if ((id = suscan_source_lookup_field_id(cfg->source, name)) == -1)
    return SU_FALSE;

  field = suscan_source_field_id_to_field(cfg->source, id);

  if (field->type != SUSCAN_FIELD_TYPE_STRING)
    return SU_FALSE;

  str_size = strlen(value) + 1;

  /* Acceptable check to avoid unnecessary allocations */
  if (strlen(cfg->values[id]->as_string) < str_size - 1) {
    if ((tmp = realloc(cfg->values[id], str_size)) == NULL)
      return SU_FALSE;

    cfg->values[id] = tmp;
  }

  strncpy(cfg->values[id]->as_string, value, str_size);

  return SU_TRUE;
}

SUBOOL
suscan_source_config_set_file(
    struct suscan_source_config *cfg,
    const char *name,
    const char *value)
{
  const struct suscan_field *field;
  union suscan_field_value *tmp;
  size_t str_size;
  int id;

  /* Assert field and field type */
  if ((id = suscan_source_lookup_field_id(cfg->source, name)) == -1)
    return SU_FALSE;

  field = suscan_source_field_id_to_field(cfg->source, id);

  if (field->type != SUSCAN_FIELD_TYPE_FILE)
    return SU_FALSE;

  str_size = strlen(value) + 1;

  /* Acceptable check to avoid unnecessary allocations */
  if (strlen(cfg->values[id]->as_string) < str_size - 1) {
    if ((tmp = realloc(cfg->values[id], str_size)) == NULL)
      return SU_FALSE;

    cfg->values[id] = tmp;
  }

  strncpy(cfg->values[id]->as_string, value, str_size);

  return SU_TRUE;
}

SUPRIVATE su_block_t *
suscan_null_source_ctor(const struct suscan_source_config *config)
{
  /* :( */
  return NULL;
}

SUBOOL
suscan_null_source_init(void)
{
  struct suscan_source *source = NULL;

  if ((source = suscan_source_register(
      "Null source",
      "Dummy silent source",
      suscan_null_source_ctor)) == NULL)
    return SU_FALSE;

  return SU_TRUE;
}


SUBOOL
suscan_init_sources(void)
{
  if (!suscan_null_source_init())
    return SU_FALSE;

  if (!suscan_wav_source_init())
    return SU_FALSE;

  if (!suscan_iqfile_source_init())
    return SU_FALSE;

  return SU_TRUE;
}
