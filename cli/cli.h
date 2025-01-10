/*

  Copyright (C) 2020 Gonzalo José Carracedo Carballal

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

#ifndef _SUSCLI_CLI_H
#define _SUSCLI_CLI_H

#include <sigutils/sigutils.h>
#include <analyzer/source.h>
#include <util/hashlist.h>

#define SUSCLI_COMMAND_REQ_CODECS     1
#define SUSCLI_COMMAND_REQ_SOURCES    2
#define SUSCLI_COMMAND_REQ_ESTIMATORS 4
#define SUSCLI_COMMAND_REQ_SPECTSRCS  8
#define SUSCLI_COMMAND_REQ_INSPECTORS 16

#define SUSCLI_COMMAND_REQ_ALL        0xff

struct suscli_command {
  char *name;
  char *description;
  uint32_t flags;
  SUBOOL (*callback) (const hashlist_t *);
};

suscan_source_config_t *suscli_get_source(unsigned int id);

unsigned int suscli_get_source_count(void);

SUBOOL suscli_command_register(
    const char *,
    const char *,
    uint32_t flags,
    SUBOOL (*callback) (const hashlist_t *));

const struct suscli_command *suscli_command_lookup(const char *);

SUBOOL suscli_param_read_int(
    const hashlist_t *params,
    const char *key,
    int *out,
    int dfl);

SUBOOL suscli_param_read_uuid(
    const hashlist_t *params,
    const char *key,
    uint64_t *out,
    uint64_t dfl);

SUBOOL suscli_param_read_double(
    const hashlist_t *params,
    const char *key,
    SUDOUBLE *out,
    SUDOUBLE dfl);

SUBOOL suscli_param_read_float(
    const hashlist_t *params,
    const char *key,
    SUFLOAT *out,
    SUFLOAT dfl);

SUBOOL suscli_param_read_string(
    const hashlist_t *params,
    const char *key,
    const char **out,
    const char *dfl);

SUBOOL suscli_param_read_bool(
    const hashlist_t *params,
    const char *key,
    SUBOOL *out,
    SUBOOL dfl);

SUBOOL suscli_param_read_profile(
    const hashlist_t *p,
    const char *key,
    suscan_source_config_t **out);

suscan_source_config_t *suscli_lookup_profile(const char *name);

SUBOOL suscli_register_sources(void);
SUBOOL suscli_init_ui_source(void);

SUBOOL suscli_run_command(const char *name, const char **argv);

suscan_source_config_t *suscli_resolve_profile(const char *spec);

SUBOOL suscli_init(void);
void suscli_log_init(void);

#endif /* _SUSCLI_CLI_H */
