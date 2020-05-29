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

#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

#define SU_LOG_DOMAIN "params"

#include "cfg.h"

SUPRIVATE int
suscan_config_desc_lookup_field_id(
    const suscan_config_desc_t *source,
    const char *name)
{
  int i;

  for (i = 0; i < source->field_count; ++i)
    if (source->field_list[i] != NULL)
      if (strcmp(source->field_list[i]->name, name) == 0)
        return i;

  return -1;
}

SUPRIVATE struct suscan_field *
suscan_config_desc_field_id_to_field(const suscan_config_desc_t *source, int id)
{
  if (id < 0 || id >= source->field_count)
    return NULL;

  return source->field_list[id];
}

struct suscan_field *
suscan_config_desc_lookup_field(
    const suscan_config_desc_t *source,
    const char *name)
{
  return suscan_config_desc_field_id_to_field(
      source,
      suscan_config_desc_lookup_field_id(source, name));
}

SUBOOL
suscan_config_desc_has_prefix(const suscan_config_desc_t *desc, const char *pfx)
{
  unsigned int i;
  unsigned int pfxlen = strlen(pfx);

  for (i = 0; i < desc->field_count; ++i)
    if (strncmp(desc->field_list[i]->name, pfx, pfxlen) == 0)
      return SU_TRUE;

  return SU_FALSE;
}

SUPRIVATE void
suscan_field_destroy(struct suscan_field *field)
{
  if (field->desc != NULL)
    free(field->desc);

  if (field->name != NULL)
    free(field->name);

  free(field);
}

void
suscan_config_desc_destroy(suscan_config_desc_t *cfgdesc)
{
  unsigned int i;

  for (i = 0; i < cfgdesc->field_count; ++i)
    if (cfgdesc->field_list[i] != NULL)
      suscan_field_destroy(cfgdesc->field_list[i]);

  if (cfgdesc->field_list != NULL)
    free(cfgdesc->field_list);

  free(cfgdesc);
}

suscan_config_desc_t *
suscan_config_desc_new(void)
{
  return calloc(1, sizeof(suscan_config_desc_t));
}

SUBOOL
suscan_config_desc_add_field(
    suscan_config_desc_t *cfgdesc,
    enum suscan_field_type type,
    SUBOOL optional,
    const char *name,
    const char *desc)
{
  struct suscan_field *field = NULL;
  char *name_dup = NULL;
  char *desc_dup = NULL;

  SU_TRYCATCH(
      suscan_config_desc_lookup_field_id(cfgdesc, name) == -1,
      goto fail);

  SU_TRYCATCH(name_dup = strdup(name), goto fail);

  SU_TRYCATCH(desc_dup = strdup(desc), goto fail);

  SU_TRYCATCH(field = calloc(1, sizeof(struct suscan_field)), goto fail);

  field->optional = optional;
  field->type = type;
  field->name = name_dup;
  field->desc = desc_dup;

  SU_TRYCATCH(PTR_LIST_APPEND_CHECK(cfgdesc->field, field) != -1, goto fail);

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
suscan_config_destroy(suscan_config_t *config)
{
  unsigned int i;

  if (config->desc != NULL && config->values != NULL) {
    for (i = 0; i < config->desc->field_count; ++i)
      if (config->values[i] != NULL)
        free(config->values[i]);

    free(config->values);
  }

  free(config);
}

suscan_config_t *
suscan_config_new(const suscan_config_desc_t *desc)
{
  suscan_config_t *new = NULL;
  unsigned int i;

  SU_TRYCATCH(
      new = calloc(1, sizeof(suscan_config_t)),
      goto fail);

  SU_TRYCATCH(
      new->values = calloc(
          desc->field_count,
          sizeof(struct suscan_field_value *)),
      goto fail);

  new->desc = desc;

  /* Allocate space for all fields */
  for (i = 0; i < desc->field_count; ++i) {
    if ((new->values[i] = calloc(1, sizeof(struct suscan_field_value))) == NULL)
      goto fail;
    new->values[i]->field = desc->field_list[i];
  }

  return new;

fail:
  if (new != NULL)
    suscan_config_destroy(new);

  return NULL;
}

suscan_config_t *
suscan_config_dup(const suscan_config_t *config)
{
  suscan_config_t *new = NULL;
  unsigned int i;
  void *tmp;

  SU_TRYCATCH(
      new = suscan_config_new(config->desc),
      goto fail);

  for (i = 0; i < new->desc->field_count; ++i) {
    switch (new->desc->field_list[i]->type) {
      case SUSCAN_FIELD_TYPE_BOOLEAN:
        new->values[i]->as_bool = config->values[i]->as_bool;
        break;

      case SUSCAN_FIELD_TYPE_FLOAT:
        new->values[i]->as_float = config->values[i]->as_float;
        break;

      case SUSCAN_FIELD_TYPE_INTEGER:
        new->values[i]->as_int = config->values[i]->as_int;
        break;

      case SUSCAN_FIELD_TYPE_FILE:
      case SUSCAN_FIELD_TYPE_STRING:
        SU_TRYCATCH(
            tmp = realloc(
                new->values[i],
                sizeof (struct suscan_field_value) +
                strlen(config->values[i]->as_string) + 1),
            return SU_FALSE);
        new->values[i] = tmp;
        strcpy(new->values[i]->as_string, config->values[i]->as_string);
        break;
    }
  }

  return new;

fail:
  if (new != NULL)
    suscan_config_destroy(new);

  return new;
}

SUBOOL
suscan_config_set_integer(
    suscan_config_t *cfg,
    const char *name,
    uint64_t value)
{
  const struct suscan_field *field;
  int id;

  /* Assert field and field type */
  SU_TRYCATCH(
      (id = suscan_config_desc_lookup_field_id(cfg->desc, name)) != -1,
      return SU_FALSE);

  field = suscan_config_desc_field_id_to_field(cfg->desc, id);

  SU_TRYCATCH(field->type == SUSCAN_FIELD_TYPE_INTEGER, return SU_FALSE);

  cfg->values[id]->as_int = value;
  cfg->values[id]->set = SU_TRUE;

  return SU_TRUE;
}

SUBOOL
suscan_config_set_bool(
    suscan_config_t *cfg,
    const char *name,
    SUBOOL value)
{
  const struct suscan_field *field;
  int id;

  /* Assert field and field type */
  SU_TRYCATCH(
      (id = suscan_config_desc_lookup_field_id(cfg->desc, name)) != -1,
      return SU_FALSE);

  field = suscan_config_desc_field_id_to_field(cfg->desc, id);

  SU_TRYCATCH(field->type == SUSCAN_FIELD_TYPE_BOOLEAN, return SU_FALSE);

  cfg->values[id]->as_bool = value;
  cfg->values[id]->set = SU_TRUE;

  return SU_TRUE;
}

SUBOOL
suscan_config_set_float(
    suscan_config_t *cfg,
    const char *name,
    SUFLOAT value)
{
  const struct suscan_field *field;
  int id;

  /* Assert field and field type */
  SU_TRYCATCH(
      (id = suscan_config_desc_lookup_field_id(cfg->desc, name)) != -1,
      return SU_FALSE);

  field = suscan_config_desc_field_id_to_field(cfg->desc, id);

  SU_TRYCATCH(field->type == SUSCAN_FIELD_TYPE_FLOAT, return SU_FALSE);

  cfg->values[id]->as_float = value;

  return SU_TRUE;
}

SUBOOL
suscan_config_set_string(
    suscan_config_t *cfg,
    const char *name,
    const char *value)
{
  const struct suscan_field *field;
  struct suscan_field_value *tmp;
  size_t str_size;
  int id;

  /* Assert field and field type */
  SU_TRYCATCH(
      (id = suscan_config_desc_lookup_field_id(cfg->desc, name)) != -1,
      return SU_FALSE);

  field = suscan_config_desc_field_id_to_field(cfg->desc, id);

  SU_TRYCATCH(field->type == SUSCAN_FIELD_TYPE_STRING, return SU_FALSE);

  str_size = strlen(value) + 1;

  /* Acceptable check to avoid unnecessary allocations */
  if (strlen(cfg->values[id]->as_string) < str_size - 1) {
    SU_TRYCATCH(
        tmp = realloc(
            cfg->values[id],
            sizeof (struct suscan_field_value) + str_size),
        return SU_FALSE);

    cfg->values[id] = tmp;
  }

  strncpy(cfg->values[id]->as_string, value, str_size);
  cfg->values[id]->set = SU_TRUE;

  return SU_TRUE;
}

SUBOOL
suscan_config_set_file(
    suscan_config_t *cfg,
    const char *name,
    const char *value)
{
  const struct suscan_field *field;
  struct suscan_field_value *tmp;
  size_t str_size;
  int id;

  /* Assert field and field type */
  SU_TRYCATCH(
      (id = suscan_config_desc_lookup_field_id(cfg->desc, name)) != -1,
      return SU_FALSE);

  field = suscan_config_desc_field_id_to_field(cfg->desc, id);

  SU_TRYCATCH(field->type == SUSCAN_FIELD_TYPE_FILE, return SU_FALSE);

  str_size = strlen(value) + 1;

  /* Acceptable check to avoid unnecessary allocations */
  if (strlen(cfg->values[id]->as_string) < str_size - 1) {
    SU_TRYCATCH(
        tmp = realloc(
            cfg->values[id],
            sizeof (struct suscan_field_value) + str_size),
        return SU_FALSE);

    cfg->values[id] = tmp;
  }

  strncpy(cfg->values[id]->as_string, value, str_size);
  cfg->values[id]->set = SU_TRUE;

  return SU_TRUE;
}

SUBOOL
suscan_config_copy(
    suscan_config_t *dest,
    const suscan_config_t *src)
{
  unsigned int i;

  SU_TRYCATCH(dest->desc == src->desc, return SU_FALSE);

  for (i = 0; i < src->desc->field_count; ++i) {
    switch (src->desc->field_list[i]->type) {
      case SUSCAN_FIELD_TYPE_STRING:
        SU_TRYCATCH(
            suscan_config_set_string(
                dest,
                src->desc->field_list[i]->name,
                src->values[i]->as_string),
            return SU_FALSE);
        break;

      case SUSCAN_FIELD_TYPE_INTEGER:
        SU_TRYCATCH(
            suscan_config_set_integer(
                dest,
                src->desc->field_list[i]->name,
                src->values[i]->as_int),
            return SU_FALSE);
        break;

      case SUSCAN_FIELD_TYPE_FLOAT:
        SU_TRYCATCH(
            suscan_config_set_float(
                dest,
                src->desc->field_list[i]->name,
                src->values[i]->as_float),
            return SU_FALSE);
        break;

      case SUSCAN_FIELD_TYPE_BOOLEAN:
        SU_TRYCATCH(
            suscan_config_set_bool(
                dest,
                src->desc->field_list[i]->name,
                src->values[i]->as_bool),
            return SU_FALSE);
        break;

      case SUSCAN_FIELD_TYPE_FILE:
        SU_TRYCATCH(
            suscan_config_set_file(
                dest,
                src->desc->field_list[i]->name,
                src->values[i]->as_string),
            return SU_FALSE);
        break;
    }
  }

  return SU_TRUE;
}

struct suscan_field_value *
suscan_config_get_value(
    const suscan_config_t *cfg,
    const char *name)
{
  int id;

  /* Assert field and field type */
  if ((id = suscan_config_desc_lookup_field_id(cfg->desc, name)) == -1)
    return NULL;

  return cfg->values[id];
}

suscan_config_t *
suscan_string_to_config(const suscan_config_desc_t *desc, const char *string)
{
  arg_list_t *al;
  struct suscan_field *field = NULL;
  suscan_config_t *config = NULL;
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

  if ((config = suscan_config_new(desc)) == NULL) {
    SU_ERROR("Failed to initialize source config\n");
    goto done;
  }

  for (i = 0; i < al->al_argc; ++i) {
    key = al->al_argv[i];

    if ((val = strchr(key, '=')) == NULL) {
      SU_ERROR("Malformed parameter string: `%s'\n", al->al_argv[i]);
      goto done;
    }

    *val++ = '\0';

    if ((field = suscan_config_desc_lookup_field(desc, key)) == NULL) {
      SU_ERROR("Unknown parameter `%s' for source\n", key);
      goto done;
    }

    switch (field->type) {
      case SUSCAN_FIELD_TYPE_FILE:
        if (!suscan_config_set_file(config, key, val)) {
          SU_ERROR("Cannot set file parameter `%s'\n", key);
          goto done;
        }
        break;

      case SUSCAN_FIELD_TYPE_STRING:
        if (!suscan_config_set_string(config, key, val)) {
          SU_ERROR("Cannot set string parameter `%s'\n", key);
          goto done;
        }
        break;

      case SUSCAN_FIELD_TYPE_INTEGER:
        if (sscanf(val, "%"SCNi64, &int_val) < 1) {
          SU_ERROR("Invalid value for parameter `%s': `%s'\n", key, val);
          goto done;
        }

        if (!suscan_config_set_integer(config, key, int_val)) {
          SU_ERROR("Cannot set string parameter `%s'\n", key);
          goto done;
        }
        break;

      case SUSCAN_FIELD_TYPE_FLOAT:
        if (sscanf(val, SUFLOAT_FMT, &float_val) < 1) {
          SU_ERROR("Invalid value for parameter `%s': `%s'\n", key, val);
          goto done;
        }

        if (!suscan_config_set_float(config, key, float_val)) {
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

        if (!suscan_config_set_bool(config, key, bool_val)) {
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
      suscan_config_destroy(config);
      config = NULL;
    }
  }

  if (al != NULL)
    free_al(al);

  return config;
}

char *
suscan_config_to_string(const suscan_config_t *config)
{
  grow_buf_t gbuf = grow_buf_INITIALIZER;
  const struct suscan_field *field = NULL;
  const struct suscan_field_value *value = NULL;
  char num_buffer[32];
  unsigned int i;

  /* Convert all parameters */
  for (i = 0; i < config->desc->field_count; ++i) {
    value = config->values[i];
    field = value->field;

    if (i > 0) {
      SU_TRYCATCH(GROW_BUF_STRCAT(&gbuf, ",") != -1, goto fail);
    }

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

suscan_object_t *
suscan_config_to_object(const suscan_config_t *config)
{
  suscan_object_t *new = NULL;
  unsigned int i;

  SU_TRYCATCH(new = suscan_object_new(SUSCAN_OBJECT_TYPE_OBJECT), goto fail);

  for (i = 0; i < config->desc->field_count; ++i) {
    switch (config->desc->field_list[i]->type) {
      case SUSCAN_FIELD_TYPE_FILE:
      case SUSCAN_FIELD_TYPE_STRING:
        SU_TRYCATCH(
            suscan_object_set_field_value(
                new,
                config->desc->field_list[i]->name,
                config->values[i]->as_string),
            goto fail);
        break;

      case SUSCAN_FIELD_TYPE_INTEGER:
        SU_TRYCATCH(
            suscan_object_set_field_int(
                new,
                config->desc->field_list[i]->name,
                config->values[i]->as_int),
            goto fail);
        break;

      case SUSCAN_FIELD_TYPE_FLOAT:
        SU_TRYCATCH(
            suscan_object_set_field_float(
                new,
                config->desc->field_list[i]->name,
                config->values[i]->as_float),
            goto fail);
        break;

      case SUSCAN_FIELD_TYPE_BOOLEAN:
        SU_TRYCATCH(
            suscan_object_set_field_bool(
                new,
                config->desc->field_list[i]->name,
                config->values[i]->as_bool),
            goto fail);
        break;

      default:
        SU_ERROR(
            "Cannot serialize field type %d\n",
            config->desc->field_list[i]->type);
    }
  }

  return new;

fail:
  if (new != NULL)
    suscan_object_destroy(new);

  return NULL;
}

SUBOOL
suscan_object_to_config(suscan_config_t *config, const suscan_object_t *object)
{
  unsigned int i, count;
  const suscan_object_t *entry;
  const char *key, *val;
  const struct suscan_config_desc *desc;
  struct suscan_field *field = NULL;
  uint64_t int_val;
  SUFLOAT float_val;
  SUBOOL bool_val;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      suscan_object_get_type(object) == SUSCAN_OBJECT_TYPE_OBJECT,
      goto done);

  count = suscan_object_field_count(object);

  desc = config->desc;

  for (i = 0; i < count; ++i) {
    entry = suscan_object_get_field_by_index(object, i);

    if (entry != NULL) {
      key = suscan_object_get_name(entry);
      val = suscan_object_get_value(entry);

      if ((field = suscan_config_desc_lookup_field(desc, key)) == NULL) {
        SU_WARNING("Field `%s' not supported by config, ignored\n", key);
        continue;
      }

      switch (field->type) {
        case SUSCAN_FIELD_TYPE_FILE:
          if (!suscan_config_set_file(config, key, val)) {
            SU_ERROR("Cannot set file parameter `%s'\n", key);
            goto done;
          }
          break;

        case SUSCAN_FIELD_TYPE_STRING:
          if (!suscan_config_set_string(config, key, val)) {
            SU_ERROR("Cannot set string parameter `%s'\n", key);
            goto done;
          }
          break;

        case SUSCAN_FIELD_TYPE_INTEGER:
          if (sscanf(val, "%"SCNi64, &int_val) < 1) {
            SU_ERROR("Invalid value for parameter `%s': `%s'\n", key, val);
            goto done;
          }

          if (!suscan_config_set_integer(config, key, int_val)) {
            SU_ERROR("Cannot set string parameter `%s'\n", key);
            goto done;
          }
          break;

        case SUSCAN_FIELD_TYPE_FLOAT:
          if (sscanf(val, SUFLOAT_FMT, &float_val) < 1) {
            SU_ERROR("Invalid value for parameter `%s': `%s'\n", key, val);
            goto done;
          }

          if (!suscan_config_set_float(config, key, float_val)) {
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

          if (!suscan_config_set_bool(config, key, bool_val)) {
            SU_ERROR("Failed to set boolean parameter `%s'\n", key);
            goto done;
          }
          break;

        default:
          SU_ERROR("Parameter `%s' cannot be set for this config\n", key);
          break;
      }
    }
  }

  ok = SU_TRUE;

done:

  return ok;
}
