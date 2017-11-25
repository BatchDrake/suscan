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

#ifndef _SOURCE_H
#define _SOURCE_H

#include <cfg.h>
#include <sigutils/sigutils.h>

#define SUSCAN_SOURCE_DEFAULT_BUFSIZ 4096

struct suscan_source_config;

struct suscan_source {
  const char *name;
  const char *desc;

  SUBOOL real_samp; /* Samples are real numbers */
  SUBOOL real_time; /* Source is real time */

  suscan_config_desc_t *config_desc;

  su_block_t *(*ctor) (const struct suscan_source_config *);
};

struct suscan_source_config {
  const struct suscan_source *source;
  suscan_config_t *config;
  SUSCOUNT bufsiz;
};

/**************************** Source API *************************************/
struct suscan_source *suscan_source_lookup(const char *name);
struct suscan_source *suscan_source_register(
    const char *name,
    const char *desc,
    su_block_t *(*ctor) (const struct suscan_source_config *));
int suscan_source_lookup_field_id(
    const struct suscan_source *source,
    const char *name);
struct suscan_field *suscan_source_field_id_to_field(
    const struct suscan_source *source,
    int id);
struct suscan_field *suscan_source_lookup_field(
    const struct suscan_source *source,
    const char *name);
SUBOOL suscan_source_add_field(
    struct suscan_source *source,
    enum suscan_field_type type,
    SUBOOL optional,
    const char *name,
    const char *desc);
void suscan_source_config_destroy(struct suscan_source_config *config);
struct suscan_source_config *suscan_source_config_new(
    const struct suscan_source *source);
SUBOOL suscan_source_config_set_integer(
    struct suscan_source_config *cfg,
    const char *name,
    uint64_t value);
SUBOOL suscan_source_config_set_float(
    struct suscan_source_config *cfg,
    const char *name,
    SUFLOAT value);
SUBOOL suscan_source_config_set_string(
    struct suscan_source_config *cfg,
    const char *name,
    const char *value);
SUBOOL suscan_source_config_set_file(
    struct suscan_source_config *cfg,
    const char *name,
    const char *value);
SUBOOL suscan_source_config_set_bool(
    struct suscan_source_config *cfg,
    const char *name,
    SUBOOL value);
SUBOOL suscan_source_config_copy(
    struct suscan_source_config *dest,
    const struct suscan_source_config *src);
struct suscan_field_value *suscan_source_config_get_value(
    const struct suscan_source_config *cfg,
    const char *name);

struct suscan_source_config *suscan_source_string_to_config(const char *string);

char *suscan_source_config_to_string(const struct suscan_source_config *config);

SUBOOL suscan_init_sources(void);

#endif /* _SOURCE_H */
