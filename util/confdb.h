/*

  Copyright (C) 2018 Gonzalo José Carracedo Carballal

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

#ifndef _UTIL_CONFDB_H
#define _UTIL_CONFDB_H

#include <sigutils/types.h>
#include "util.h"
#include "object.h"

/* Helper functions */
const char *suscan_confdb_get_system_path(void);

const char *suscan_confdb_get_local_path(void);

struct suscan_config_context {
  char *name;
  char *save_file;

  PTR_LIST(char, path);

  suscan_object_t *list;
};

typedef struct suscan_config_context suscan_config_context_t;

suscan_config_context_t *suscan_config_context_assert(const char *name);

SUBOOL suscan_config_context_add_path(
    suscan_config_context_t *ctx,
    const char *path);

SUBOOL suscan_config_context_put(
    suscan_config_context_t *context,
    suscan_object_t *obj);

SUBOOL suscan_config_context_scan(suscan_config_context_t *context);

SUBOOL suscan_confdb_scan_all(void);

SUBOOL suscan_confdb_save_all(void);

SUBOOL suscan_confdb_use(const char *name);

#endif /* _UTIL_CONFDB_H */
