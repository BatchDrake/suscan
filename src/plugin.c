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

#define SU_LOG_DOMAIN "plugin"

#define _COMPAT_DLFCN

#include "plugin.h"
#include <sigutils/log.h>
#include <util/sha256.h>
#include <util/compat.h>
#include <string.h>
#include <dirent.h>

PTR_LIST_PRIVATE(char,                                    g_search_path);
PTR_LIST_PRIVATE(suscan_plugin_t,                         g_plugin);
PTR_LIST_PRIVATE_CONST(struct suscan_plugin_service_desc, g_service_desc);

SUPRIVATE hashlist_t *g_hash_to_plugin = NULL;
SUPRIVATE hashlist_t *g_name_to_plugin = NULL;

/* Suscan plugin lifecycle is private to this module. */
SUPRIVATE SU_INSTANCER(suscan_plugin, const char *);
SUPRIVATE SU_COLLECTOR(suscan_plugin);

SUPRIVATE SUBOOL
suscan_plugin_ensure_init(void)
{
  SUBOOL ok = SU_FALSE;

  if (g_hash_to_plugin == NULL)
    SU_MAKE(g_hash_to_plugin, hashlist);
  
  if (g_name_to_plugin == NULL)
    SU_MAKE(g_name_to_plugin, hashlist);
  
  ok = SU_TRUE;

done:
  return ok;
}

SUPRIVATE char *
suscan_plugin_hash_file(const char *path)
{
  char *buffer = NULL;
  FILE *fp;
  uint8_t block[SHA256_BLOCK_SIZE];
  SHA256_CTX ctx;
  int32_t got;
  unsigned int i;
  
  if ((fp = fopen(path, "rb")) == NULL)
    goto done;

  suscan_sha256_init(&ctx);

  while ((got = fread(block, 1, SHA256_BLOCK_SIZE, fp)) > 0)
    suscan_sha256_update(&ctx, block, got);
  
  if (got < 0)
    SU_ERROR("sha256: cannot read `%s': %s\n", path, strerror(errno));

  suscan_sha256_final(&ctx, block);

  SU_ALLOCATE_MANY(buffer, 2 * SHA256_BLOCK_SIZE + 1, char);

  for (i = 0; i < SHA256_BLOCK_SIZE; ++i)
    snprintf(buffer + 2 * i, 3, "%02x", block[i]);
  
done:
  if (fp != NULL)
    fclose(fp);

  return buffer;
}

SUPRIVATE const struct suscan_plugin_service_desc *
suscan_plugin_service_desc_lookup(const char *name)
{
  unsigned int i;

  for (i = 0; i < g_service_desc_count; ++i)
    if (strcmp(name, g_service_desc_list[i]->name) == 0)
      return g_service_desc_list[i];

  return NULL;
}

suscan_plugin_t *
suscan_plugin_lookup(const char *name)
{
  SU_TRY_FAIL(suscan_plugin_ensure_init());

  return hashlist_get(g_name_to_plugin, name);

fail:
  return NULL;
}

SUBOOL
suscan_plugin_register_service(const struct suscan_plugin_service_desc *desc)
{
  SUBOOL ok;

  SU_TRYC(desc != NULL && desc->name != NULL && desc->ctor != NULL);

  if (suscan_plugin_service_desc_lookup(desc->name) != NULL) {
    SU_ERROR("Plugin service `%s' already registered.\n", desc->name);
    goto done;
  }

  SU_TRYC(PTR_LIST_APPEND_CHECK(g_service_desc, (void *) desc));

  ok = SU_TRUE;

done:
  return ok;
}

SU_METHOD(suscan_plugin, void, set_hash, char *hash)
{
  if (self->hash != NULL)
    free(self->hash);

  self->hash = hash;
}

SUPRIVATE
SU_INSTANCER(suscan_plugin, const char *path)
{
  suscan_plugin_t *new = NULL;
  const uint32_t *p_plugin_ver, *p_api_ver;
  const char **depends = NULL;

  int errno_saved = errno;
  
  SU_TRY_FAIL(suscan_plugin_ensure_init());

  SU_ALLOCATE_FAIL(new, suscan_plugin_t);

  SU_MAKE_FAIL(new->services, hashlist);

  SU_MAKE_FAIL(new->depends, strlist);

  SU_TRY_FAIL(new->path = strdup(path));

  if ((new->handle = dlopen(path, RTLD_LAZY)) == NULL) {
    SU_ERROR("Cannot open %s: %s\n", path, dlerror());
    goto fail;
  }

  errno = EINVAL;  
  // Not an error, just not a plugin
  if ((new->name = dlsym(
    new->handle,
    SUSCAN_SYM_NAME(plugin_name))) == NULL) {
    SU_WARNING("%s: not a plugin (no plugin name)\n", path);
    goto fail;
  }

  if ((new->desc = dlsym(
    new->handle,
    SUSCAN_SYM_NAME(plugin_desc))) == NULL) {
    SU_WARNING("%s: not a plugin (no plugin desc)\n", path);
    goto fail;
  }

  if ((p_plugin_ver = dlsym(
    new->handle,
    SUSCAN_SYM_NAME(plugin_ver))) == NULL) {
    SU_WARNING("%s: not a valid plugin (plugin version missing)\n", path);
    goto fail;
  }
  new->version     = *p_plugin_ver;

  if ((p_api_ver = dlsym(
    new->handle,
    SUSCAN_SYM_NAME(api_ver))) == NULL) {
    SU_WARNING("%s: not a valid plugin (API version missing)\n", path);
    goto fail;
  }
  new->api_version = *p_api_ver;

  if ((depends = dlsym(new->handle, SUSCAN_SYM_NAME(depends))) != NULL) {
    while (*depends != NULL) {
      SU_TRYC_FAIL(strlist_append_string(new->depends, *depends));
      ++depends;
    }
  }
  
  if ((new->entry_fn = dlsym(
    new->handle,
    STRINGIFY(plugin_entry))) == NULL) {
    SU_WARNING("%s: not a valid plugin (entry point missing))\n", path);
    goto fail;
  }

  errno = errno_saved;
  
  return new;

fail:
  if (new != NULL)
    SU_DISPOSE(suscan_plugin, new);

  return NULL;
}

SUPRIVATE
SU_COLLECTOR(suscan_plugin)
{
  if (self->hash != NULL)
    free(self->hash);

  if (self->path != NULL)
    free(self->path);

  if (self->services != NULL) {
    hashlist_iterator_t it;
    for (
      it = hashlist_begin(self->services);
      !hashlist_iterator_end(&it);
      hashlist_iterator_advance(&it)) {
      const struct suscan_plugin_service_desc *desc;

      if ((desc = suscan_plugin_service_desc_lookup(it.name)) != NULL) {
        if (desc->dtor != NULL)
          (desc->dtor) (it.value);
      } else {
        SU_ERROR("BUG: unknown service `%s'.\n", it.name);
      }
    }

    SU_DISPOSE(hashlist, self->services);
  }

  if (self->depends != NULL)
    strlist_destroy(self->depends);

  if (self->handle != NULL)
    dlclose(self->handle);

  free(self);
}

SU_GETTER(suscan_plugin, void *, get_service, const char *name)
{
  return hashlist_get(self->services, name);
}

SU_METHOD(suscan_plugin, SUBOOL, run)
{
  if ((self->entry_fn) (self)) {
    hashlist_iterator_t it;
    for (
      it = hashlist_begin(self->services);
      !hashlist_iterator_end(&it);
      hashlist_iterator_advance(&it)) {
      const struct suscan_plugin_service_desc *desc;

      if ((desc = suscan_plugin_service_desc_lookup(it.name)) != NULL) {
        if (desc->post_load != NULL)
          (desc->post_load) (it.value);
      }
    }

    return SU_TRUE;
  }

  return SU_FALSE;
}

SUBOOL
suscan_plugin_add_search_path(const char *path)
{
  char *dup = NULL;

  SU_TRY_FAIL(dup = strdup(path));

  SU_TRYC_FAIL(PTR_LIST_APPEND_CHECK(g_search_path, dup));

  return SU_TRUE;

fail:
  if (dup != NULL)
    free(dup);

  return SU_FALSE;
}

SUPRIVATE
SU_METHOD(suscan_plugin, SUBOOL, init_services)
{
  SUBOOL ok = SU_FALSE;
  unsigned int i = 0;

  for (i = 0; i < g_service_desc_count; ++i) {
    void *ptr = (g_service_desc_list[i]->ctor) (self);

    if (ptr == NULL) {
      SU_ERROR(
        "%s: failed to load plugin service `%s'\n",
        self->path,
        g_service_desc_list[i]->name);
      goto done;
    }

    SU_TRY(hashlist_set(self->services, g_service_desc_list[i]->name, ptr));
  }

  ok = SU_TRUE;

done:
  return ok;
}

SUPRIVATE
SU_METHOD(suscan_plugin, SUBOOL, register_globally)
{
  SUBOOL ok = SU_TRUE;

  ok = PTR_LIST_APPEND_CHECK(g_plugin, self) != -1      && ok;

  ok = hashlist_set(g_hash_to_plugin, self->hash, self) && ok;

  ok = hashlist_set(g_name_to_plugin, self->name, self) && ok;

  return ok;
}

SUBOOL
suscan_plugin_load(const char *path)
{
  char *hash = NULL;
  SUBOOL ok = SU_FALSE;
  suscan_plugin_t *plugin = NULL;
  
  SU_TRY(suscan_plugin_ensure_init());

  /* Check if plugin file has already been loaded */
  if ((hash = suscan_plugin_hash_file(path)) == NULL)
    goto done;
  
  if ((plugin = hashlist_get(g_hash_to_plugin, hash)) == NULL) {
    /* Construct plugin */
    if ((plugin = suscan_plugin_new(path)) == NULL)
      goto done;
    
    /* Set plugin hash */
    suscan_plugin_set_hash(plugin, hash);
    hash = NULL;

    /* Plugin has been loaded. Register services before passing control. */
    SU_TRY(suscan_plugin_init_services(plugin));

    /* Services registered. Run plugin's initialization routine. */
    SU_TRY(suscan_plugin_run(plugin));

    /* 
    * From this point, we cannot longer unload this plugin. At least for now,
    * until we find an ordered way to either unload everything this
    * plugin has registgered or by asking the plugin to unload everything
    * politely.
    */

    if (!suscan_plugin_register_globally(plugin))
      SU_ERROR("%s: failed to register plugin globally.\n", path);
  }

  ok = SU_TRUE;

done:
  if (hash != NULL)
    free(hash);
  
  return ok;
}

SUBOOL
suscan_plugin_load_from_dir(const char *path)
{
  DIR *dir = NULL;
  unsigned int plugins = 0, total = 0;
  char *full_path = NULL;
  struct dirent *entry;
  SUBOOL ok = SU_FALSE;

  if ((dir = opendir(path)) == NULL)
    goto done;

  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;
    
    ++total;

    SU_TRY(full_path = strbuild("%s/%s", path, entry->d_name));

    if (suscan_plugin_load(full_path))
      ++plugins;
    
    free(full_path);
    full_path = NULL;
  }

  if (plugins > 0)
    SU_INFO("%s: %d/%d plugins loaded\n", path, plugins, total);
  
  ok = SU_TRUE;

done:
  if (dir != NULL)
    closedir(dir);

  if (full_path != NULL)
    free(full_path);
  
  return ok;
}

SUBOOL
suscan_plugin_load_all(void)
{
  unsigned int i;

  for (i = 0; i < g_search_path_count; ++i)
    suscan_plugin_load_from_dir(g_search_path_list[i]);

  return SU_TRUE;
}

