/*
  
  Copyright (C) 2025 Gonzalo José Carracedo Carballal
  
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

#ifndef _SUSCAN_PLUGIN_H
#define _SUSCAN_PLUGIN_H

#include <sigutils/defs.h>
#include <sigutils/types.h>
#include <util/hashlist.h>

#if defined(__cplusplus)
extern "C" {
#endif

#define SUSCAN_PLUGIN_DIR "plugins"
#define SUSCAN_SYM_PFX  SUSCANPLG_

#define SUSCAN_SYM(name)                       \
  JOIN(SUSCAN_SYM_PFX, name)

#define SUSCAN_SYM_NAME(name)                  \
  STRINGIFY(SUSCAN_SYM(name))

#define SUSCAN_DECLARE_SYM(type, name, val)    \
  extern "C" {                                 \
    extern type SUSCAN_SYM(name);              \
    type SUSCAN_SYM(name) = val;               \
  }

#define SUSCAN_PLUGIN(name, desc)              \
  SUSCAN_DECLARE_SYM(                          \
    const char *,                              \
    plugin_name,                               \
    name)                                      \
  SUSCAN_DECLARE_SYM(                          \
    const char *,                              \
    plugin_desc,                               \
    desc)

#define SUSCAN_PLUGIN_VERSION(x, y, z)         \
  SUSCAN_DECLARE_SYM(                          \
    uint32_t,                                  \
    plugin_ver,                                \
    SU_VER(x, y, z));

#define SUSCAN_PLUGIN_API_VERSION(x, y, z)     \
  SUSCAN_DECLARE_SYM(                          \
    uint32_t,                                  \
    api_ver,                                   \
    SU_VER(x, y, z));

#define _SUSCAN_PLUGIN_ENTRY_SYM plugin_entry

#if defined(__cplusplus)
#  define SUSCAN_PLUGIN_ENTRY_PROTO                      \
   extern "C" {                                          \
    SUBOOL _SUSCAN_PLUGIN_ENTRY_SYM(suscan_plugin_t *);  \
   }
#else
#  define SUSCAN_PLUGIN_ENTRY_PROTO                      \
    SUBOOL _SUSCAN_PLUGIN_ENTRY_SYM(suscan_plugin_t *);
#endif

#define SUSCAN_PLUGIN_ENTRY(plugin)                      \
  SUSCAN_PLUGIN_ENTRY_PROTO                              \
  SUBOOL _SUSCAN_PLUGIN_ENTRY_SYM(suscan_plugin_t *plugin)

#define SUSCAN_INVALID_PLUGIN_HANDLE NULL

struct suscan_plugin {
  char        *hash;
  char        *path;

  const char  *name; /* Storage inside plugin */
  const char  *desc; /* Storage inside plugin */
  uint32_t     version;
  uint32_t     api_version;

  hashlist_t  *services;

  void        *handle;

  SUBOOL (*entry_fn) (struct suscan_plugin *);
};

typedef struct suscan_plugin suscan_plugin_t;

struct suscan_plugin_service_desc {
  const char *name;

  void * (*ctor) (suscan_plugin_t *);
  SUBOOL (*post_load) (void *);
  void * (*dtor) (void *);
};

SUINLINE
SU_GETTER(suscan_plugin, uint32_t, get_version)
{
  return self->version;
}

SUINLINE
SU_GETTER(suscan_plugin, uint32_t, get_api_version)
{
  return self->api_version;
}

SUINLINE
SU_GETTER(suscan_plugin, const char *, get_description)
{
  return self->desc;
}

SUINLINE
SU_GETTER(suscan_plugin, const char *, get_name)
{
  return self->name;
}

SUINLINE
SU_GETTER(suscan_plugin, const char *, get_hash)
{
  return self->hash;
}

SUINLINE
SU_GETTER(suscan_plugin, const char *, get_path)
{
  return self->path;
}

SUBOOL suscan_plugin_register_service(const struct suscan_plugin_service_desc *);
suscan_plugin_t *suscan_plugin_lookup(const char *);

SU_METHOD(suscan_plugin, void,         set_hash, char *);
SU_METHOD(suscan_plugin, SUBOOL,       run);
SU_GETTER(suscan_plugin, void *,       get_service, const char *);

SUBOOL suscan_plugin_add_search_path(const char *);
SUBOOL suscan_plugin_load(const char *);
SUBOOL suscan_plugin_load_from_dir(const char *);
SUBOOL suscan_plugin_load_all(void);

#if defined(__cplusplus)
}
#endif

#endif /* _SUSCAN_PLUGIN_H */
