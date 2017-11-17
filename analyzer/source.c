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

#define SU_LOG_DOMAIN "source"

#include "source.h"
#include "xsig.h"
#include "sources/bladerf.h"
#include "sources/hack_rf.h"
#include "sources/alsa.h"

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
  suscan_config_desc_t *cfgdesc = NULL;
  struct suscan_source *new = NULL;

  /* We cannot have two sources with the same name */
  SU_TRYCATCH(suscan_source_lookup(name) == NULL, goto fail);

  SU_TRYCATCH(name_dup = strdup(name), goto fail);

  SU_TRYCATCH(desc_dup = strdup(desc), goto fail);

  SU_TRYCATCH(cfgdesc = suscan_config_desc_new(), goto fail);

  SU_TRYCATCH(new = calloc(1, sizeof(struct suscan_source)), goto fail);

  new->name = name_dup;
  new->desc = desc_dup;
  new->ctor = ctor;
  new->config_desc = cfgdesc;

  SU_TRYCATCH(PTR_LIST_APPEND_CHECK(source, new) != -1, goto fail);

  return new;

fail:
  if (name_dup != NULL)
    free(name_dup);

  if (desc_dup != NULL)
    free(desc_dup);

  if (new != NULL)
    free(new);

  if (cfgdesc != NULL)
    suscan_config_desc_destroy(cfgdesc);

  return NULL;
}

SUBOOL
suscan_source_add_field(
    struct suscan_source *source,
    enum suscan_field_type type,
    SUBOOL optional,
    const char *name,
    const char *desc)
{
  return suscan_config_desc_add_field(
      source->config_desc,
      type,
      optional,
      name,
      desc);
}

void
suscan_source_config_destroy(struct suscan_source_config *config)
{
  unsigned int i;

  if (config->config != NULL)
    suscan_config_destroy(config->config);

  free(config);
}

struct suscan_source_config *
suscan_source_config_new(const struct suscan_source *source)
{
  struct suscan_source_config *new = NULL;
  unsigned int i;

  SU_TRYCATCH(new = calloc(1, sizeof(struct suscan_source_config)), goto fail);

  SU_TRYCATCH(new->config = suscan_config_new(source->config_desc), goto fail);

  new->source = source;
  new->bufsiz = SUSCAN_SOURCE_DEFAULT_BUFSIZ;

  return new;

fail:
  if (new != NULL)
    suscan_source_config_destroy(new);

  return NULL;
}

SUBOOL
suscan_source_config_copy(
    struct suscan_source_config *dest,
    const struct suscan_source_config *src)
{
  unsigned int i;

  SU_TRYCATCH(dest->source == src->source, return SU_FALSE);

  SU_TRYCATCH(suscan_config_copy(dest->config, src->config), return SU_FALSE);

  return SU_TRUE;
}

struct suscan_field_value *
suscan_source_config_get_value(
    const struct suscan_source_config *cfg,
    const char *name)
{
  return suscan_config_get_value(cfg->config, name);
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
  struct suscan_source *src = NULL;
  struct suscan_source_config *src_config = NULL;
  suscan_config_t *config = NULL;
  const char *p;
  char *dup = NULL;
  size_t len;
  SUBOOL ok = SU_FALSE;

  /* Extract source name (first comma) */
  p = string;

  while (*p && *p != ',')
    ++p;

  len = p - string;

  SU_TRYCATCH(dup = malloc(len + 1), goto done);

  memcpy(dup, string, len);
  dup[len] = '\0';

  if ((src = suscan_source_lookup(dup)) == NULL) {
    SU_ERROR("Unknown source `%s'\n", dup);
    goto done;
  }


  /* Create configuration object */
  SU_TRYCATCH(src_config = suscan_source_config_new(src), goto done);

  /* Parse configuration string */
  if (*p != '\0')
    ++p;

  if ((config = suscan_string_to_config(
      src->config_desc,
      p)) == NULL) {
    SU_ERROR("Failed to convert string to source configuration\n");
    goto done;
  }

  SU_TRYCATCH(suscan_config_copy(src_config->config, config), goto done);

  ok = SU_TRUE;

done:
  if (dup != NULL)
    free(dup);

  if (config != NULL)
    suscan_config_destroy(config);

  if (!ok) {
    if (src_config != NULL) {
      suscan_source_config_destroy(src_config);
      src_config = NULL;
    }
  }

  return src_config;
}

char *
suscan_source_config_to_string(const struct suscan_source_config *config)
{
  char *full_string = NULL;
  char *cfgstring = NULL;

  SU_TRYCATCH(cfgstring = suscan_config_to_string(config->config), goto done);

  SU_TRYCATCH(
      full_string = strbuild("%s,%s", config->source->name, cfgstring),
      goto done);

done:
  if (cfgstring != NULL)
    free(cfgstring);

  return full_string;
}

SUBOOL
suscan_init_sources(void)
{
  SU_TRYCATCH(suscan_null_source_init(), return SU_FALSE);

  SU_TRYCATCH(suscan_wav_source_init(), return SU_FALSE);

  SU_TRYCATCH(suscan_iqfile_source_init(), return SU_FALSE);

  SU_TRYCATCH(suscan_bladeRF_source_init(), return SU_FALSE);

  SU_TRYCATCH(suscan_hackRF_source_init(), return SU_FALSE);

  SU_TRYCATCH(suscan_alsa_source_init(), return SU_FALSE);

  return SU_TRUE;
}
