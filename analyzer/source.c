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

#include "source.h"
#include "xsig.h"
#include "sources/bladerf.h"

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
    SUBOOL optional,
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

  field->optional = optional;
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
  new->bufsiz = SUSCAN_SOURCE_DEFAULT_BUFSIZ;

  /* Allocate space for all fields */
  for (i = 0; i < source->field_count; ++i) {
    if ((new->values[i] = calloc(1, sizeof(struct suscan_field_value))) == NULL)
      goto fail;
    new->values[i]->field = source->field_list[i];
  }

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
  cfg->values[id]->set = SU_TRUE;

  return SU_TRUE;
}

SUBOOL
suscan_source_config_set_bool(
    struct suscan_source_config *cfg,
    const char *name,
    SUBOOL value)
{
  const struct suscan_field *field;
  int id;

  /* Assert field and field type */
  if ((id = suscan_source_lookup_field_id(cfg->source, name)) == -1)
    return SU_FALSE;

  field = suscan_source_field_id_to_field(cfg->source, id);

  if (field->type != SUSCAN_FIELD_TYPE_BOOLEAN)
    return SU_FALSE;

  cfg->values[id]->as_bool = value;
  cfg->values[id]->set = SU_TRUE;

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
  struct suscan_field_value *tmp;
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
    if ((tmp = realloc(
        cfg->values[id],
        sizeof (struct suscan_field_value) + str_size)) == NULL)
      return SU_FALSE;

    cfg->values[id] = tmp;
  }

  strncpy(cfg->values[id]->as_string, value, str_size);
  cfg->values[id]->set = SU_TRUE;

  return SU_TRUE;
}

SUBOOL
suscan_source_config_set_file(
    struct suscan_source_config *cfg,
    const char *name,
    const char *value)
{
  const struct suscan_field *field;
  struct suscan_field_value *tmp;
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
    if ((tmp = realloc(
        cfg->values[id],
        sizeof (struct suscan_field_value) + str_size)) == NULL)
      return SU_FALSE;

    cfg->values[id] = tmp;
  }

  strncpy(cfg->values[id]->as_string, value, str_size);
  cfg->values[id]->set = SU_TRUE;

  return SU_TRUE;
}

struct suscan_field_value *
suscan_source_config_get_value(
    const struct suscan_source_config *cfg,
    const char *name)
{
  int id;

  /* Assert field and field type */
  if ((id = suscan_source_lookup_field_id(cfg->source, name)) == -1)
    return NULL;

  return cfg->values[id];
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
      "null",
      "Dummy silent source",
      suscan_null_source_ctor)) == NULL)
    return SU_FALSE;

  return SU_TRUE;
}


struct suscan_source_config *
suscan_source_string_to_config(const char *string)
{
  arg_list_t *al;
  struct suscan_field *field = NULL;
  struct suscan_source *src = NULL;
  struct suscan_source_config *config = NULL;
  SUBOOL ok = SU_FALSE;
  char *val;
  char *key;
  unsigned int i;
  uint64_t int_val;
  SUFLOAT float_val;
  SUBOOL bool_val;

  if ((al = csv_split_line(string)) == NULL) {
    SU_ERROR("Failed to parse source string\n");
    goto done;
  }

  if (al->al_argc == 0) {
    SU_ERROR("Invalid source string\n");
    goto done;
  }

  if ((src = suscan_source_lookup(al->al_argv[0])) == NULL) {
    SU_ERROR("Unknown source `%s'\n", al->al_argv[0]);
    goto done;
  }

  if ((config = suscan_source_config_new(src)) == NULL) {
    SU_ERROR("Failed to initialize source config\n");
    goto done;
  }

  for (i = 1; i < al->al_argc; ++i) {
    key = al->al_argv[i];

    if ((val = strchr(key, '=')) == NULL) {
      SU_ERROR("Malformed parameter string: `%s'\n", al->al_argv[i]);
      goto done;
    }

    *val++ = '\0';

    if ((field = suscan_source_lookup_field(src, key)) == NULL) {
      SU_ERROR("Unknown parameter `%s' for source\n", key);
      goto done;
    }

    switch (field->type) {
      case SUSCAN_FIELD_TYPE_FILE:
        if (!suscan_source_config_set_file(config, key, val)) {
          SU_ERROR("Cannot set file parameter `%s'\n", key);
          goto done;
        }
        break;

      case SUSCAN_FIELD_TYPE_STRING:
        if (!suscan_source_config_set_string(config, key, val)) {
          SU_ERROR("Cannot set string parameter `%s'\n", key);
          goto done;
        }
        break;

      case SUSCAN_FIELD_TYPE_INTEGER:
        if (sscanf(val, "%lli", &int_val) < 1) {
          SU_ERROR("Invalid value for parameter `%s': `%s'\n", key, val);
          goto done;
        }

        if (!suscan_source_config_set_integer(config, key, int_val)) {
          SU_ERROR("Cannot set string parameter `%s'\n", key);
          goto done;
        }
        break;

      case SUSCAN_FIELD_TYPE_FLOAT:
        if (sscanf(val, SUFLOAT_FMT, &float_val) < 1) {
          SU_ERROR("Invalid value for parameter `%s': `%s'\n", key, val);
          goto done;
        }

        if (!suscan_source_config_set_float(config, key, float_val)) {
          SU_ERROR("Cannot set string parameter `%s'\n", key);
          goto done;
        }
        break;

      case SUSCAN_FIELD_TYPE_BOOLEAN:
        if (strcasecmp(val, "true") == 0 ||
            strcasecmp(val, "yes")  == 0 ||
            strcasecmp(val, "1")    == 0)
          bool_val = SU_TRUE;
        else if (strcasecmp(val, "false") == 0 ||
            strcasecmp(val, "no")         == 0 ||
            strcasecmp(val, "0")          == 0)
          bool_val = SU_FALSE;
        else {
          SU_ERROR("Invalid boolean value for parameter `%s': %s\n", key, val);
          goto done;
        }

        if (!suscan_source_config_set_bool(config, key, bool_val)) {
          SU_ERROR("Failed to set boolean parameter `%s'\n", key);
          goto done;
        }
        break;

      default:
        SU_ERROR("Parameter `%s' cannot be set for this source\n", key);
        break;
    }
  }

  ok = SU_TRUE;

done:
  if (!ok) {
    if (config != NULL) {
      suscan_source_config_destroy(config);
      config = NULL;
    }
  }

  if (al != NULL)
    free_al(al);

  return config;
}

char *
suscan_source_config_to_string(const struct suscan_source_config *config)
{
  char *result = NULL;
  grow_buf_t gbuf = grow_buf_INITIALIZER;
  const struct suscan_field *field = NULL;
  const struct suscan_field_value *value = NULL;
  char *terminator;
  char num_buffer[32];
  unsigned int i;

  /* Source strings start with the source name */
  SU_TRYCATCH(GROW_BUF_STRCAT(&gbuf, config->source->name) != -1, goto fail);

  /* Convert all parameters */
  for (i = 0; i < config->source->field_count; ++i) {
    value = config->values[i];
    field = value->field;
    SU_TRYCATCH(GROW_BUF_STRCAT(&gbuf, ",") != -1, goto fail);

    SU_TRYCATCH(GROW_BUF_STRCAT(&gbuf, field->name) != -1, goto fail);

    SU_TRYCATCH(GROW_BUF_STRCAT(&gbuf, "=") != -1, goto fail);

    /* FIXME: escape commas! */
    switch (field->type) {
      case SUSCAN_FIELD_TYPE_FILE:
      case SUSCAN_FIELD_TYPE_STRING:
        SU_TRYCATCH(GROW_BUF_STRCAT(&gbuf, value->as_string) != -1, goto fail);
        break;

      case SUSCAN_FIELD_TYPE_INTEGER:
        snprintf(
            num_buffer,
            sizeof(num_buffer),
            "%lli",
            (long long int) value->as_int);
        SU_TRYCATCH(GROW_BUF_STRCAT(&gbuf, num_buffer) != -1, goto fail);
        break;

      case SUSCAN_FIELD_TYPE_FLOAT:
        snprintf(num_buffer, sizeof(num_buffer), SUFLOAT_FMT, value->as_float);
        SU_TRYCATCH(GROW_BUF_STRCAT(&gbuf, num_buffer) != -1, goto fail);
        break;

      case SUSCAN_FIELD_TYPE_BOOLEAN:
        SU_TRYCATCH(
            GROW_BUF_STRCAT(&gbuf, value->as_bool ? "yes" : "no") != -1,
            goto fail);
        break;

      default:
        SU_ERROR("Cannot serialize field type %d\n", field->type);
    }
  }

  SU_TRYCATCH(grow_buf_append(&gbuf, "", 1) != -1, goto fail);

  return (char *) grow_buf_get_buffer(&gbuf);

fail:
  grow_buf_finalize(&gbuf);

  return NULL;
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

  if (!suscan_bladeRF_source_init())
    return SU_FALSE;

  return SU_TRUE;
}
