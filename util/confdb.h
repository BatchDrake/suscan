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

#ifndef _UTIL_CONFDB_H
#define _UTIL_CONFDB_H

#include <sigutils/types.h>
#include <sigutils/util/util.h>
#include "object.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Helper functions */
const char *suscan_confdb_get_system_path(void);
const char *suscan_confdb_get_local_path(void);
const char *suscan_confdb_get_local_tle_path(void);

struct suscan_config_context {
  char *name;
  char *save_file;
  SUBOOL save;

  PTR_LIST(char, path);

  suscan_object_t *list;

  void *userdata;
  SUBOOL (*on_save) (struct suscan_config_context *ctx, void *userdata);
};

typedef struct suscan_config_context suscan_config_context_t;

suscan_config_context_t *suscan_config_context_lookup(const char *name);
suscan_config_context_t *suscan_config_context_assert(const char *name);

void suscan_config_context_set_save(
    suscan_config_context_t *ctx,
    SUBOOL save);

SUBOOL suscan_config_context_add_path(
    suscan_config_context_t *ctx,
    const char *path);

void suscan_config_context_flush(suscan_config_context_t *context);

SUBOOL suscan_config_context_put(
    suscan_config_context_t *context,
    suscan_object_t *obj);

SUBOOL suscan_config_context_remove(
    suscan_config_context_t *context,
    suscan_object_t *obj);

SUBOOL suscan_config_context_scan(suscan_config_context_t *context);

SUINLINE const suscan_object_t *
suscan_config_context_get_list(suscan_config_context_t *context)
{
  return context->list;
}

SUINLINE void
suscan_config_context_set_on_save(
    suscan_config_context_t *ctx,
    SUBOOL (*on_save) (struct suscan_config_context *ctx, void *userdata),
    void *userdata)
{
  ctx->on_save = on_save;
  ctx->userdata = userdata;
}

SUINLINE const char *
suscan_config_context_get_save_file(const suscan_config_context_t *self)
{
  return self->save_file;
}

SUBOOL suscan_confdb_scan_all(void);

SUBOOL suscan_confdb_save_all(void);

SUBOOL suscan_confdb_use(const char *name);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _UTIL_CONFDB_H */
