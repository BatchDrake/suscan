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

#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

#define SU_LOG_DOMAIN "confdb"

#include <sigutils/log.h>
#include "confdb.h"
#include "compat.h"

PTR_LIST(SUPRIVATE suscan_config_context_t, context);

SUPRIVATE const char *confdb_user_path;

SUPRIVATE const char *confdb_system_path;
SUPRIVATE const char *confdb_local_path;
SUPRIVATE const char *confdb_tle_path;

const char *
suscan_confdb_get_system_path(void)
{
  const char *path = NULL;
  
  if (confdb_system_path == NULL) {
    if ((path = getenv("SUSCAN_CONFIG_PATH")) != NULL) {
      confdb_system_path = path;
    } else {
      /*
       * In some distributions, config files are retrieved from an
       * application bundle (like MacOS).
       */
      path = suscan_bundle_get_confdb_path();
      if (path != NULL)
        confdb_system_path = path;
      else
        confdb_system_path = PKGDATADIR "/config";
    }
  }

  return confdb_system_path;
}

const char *
suscan_confdb_get_user_path(void)
{
  struct passwd *pwd;
  const char *homedir = NULL;
  char *tmp = NULL;

  if (confdb_user_path == NULL) {
    if ((pwd = getpwuid(getuid())) != NULL)
      homedir = pwd->pw_dir;
    else
      homedir = getenv("HOME");

    if (homedir == NULL) {
      SU_WARNING("No homedir information found!\n");
      return NULL;
    }

    SU_TRYCATCH(tmp = strbuild("%s/.suscan", homedir), goto fail);

    if (access(tmp, F_OK) == -1)
      SU_TRYCATCH(mkdir(tmp, 0700) != -1, goto fail);

    confdb_user_path = tmp;
  }

  return confdb_user_path;
  
fail:
  if (tmp != NULL)
    free(tmp);

  return NULL;
}


const char *
suscan_confdb_get_local_path(void)
{
  const char *user_path;
  char *tmp = NULL;

  if (confdb_local_path == NULL) {
    SU_TRYCATCH(user_path = suscan_confdb_get_user_path(), goto fail);
    SU_TRYCATCH(tmp = strbuild("%s/config", user_path), goto fail);

    if (access(tmp, F_OK) == -1)
      SU_TRYCATCH(mkdir(tmp, 0700) != -1, goto fail);

    confdb_local_path = tmp;
  }

  return confdb_local_path;

fail:
  if (tmp != NULL)
    free(tmp);

  return NULL;
}

const char *
suscan_confdb_get_local_tle_path(void)
{
  const char *user_path;
  char *tmp = NULL;

  if (confdb_tle_path == NULL) {
    SU_TRYCATCH(user_path = suscan_confdb_get_user_path(), goto fail);
    SU_TRYCATCH(tmp = strbuild("%s/tle", user_path), goto fail);

    if (access(tmp, F_OK) == -1)
      SU_TRYCATCH(mkdir(tmp, 0700) != -1, goto fail);

    confdb_tle_path = tmp;
  }

  return confdb_tle_path;

fail:
  if (tmp != NULL)
    free(tmp);

  return NULL;
}

SUPRIVATE void
suscan_config_context_destroy(suscan_config_context_t *ctx)
{
  unsigned int i;

  if (ctx->name != NULL)
    free(ctx->name);

  if (ctx->save_file != NULL)
    free(ctx->save_file);

  if (ctx->list != NULL)
    suscan_object_destroy(ctx->list);

  for (i = 0; i < ctx->path_count; ++i)
    if (ctx->path_list[i] != NULL)
      free(ctx->path_list[i]);

  if (ctx->path_list != NULL)
    free(ctx->path_list);

  free(ctx);
}

SUPRIVATE suscan_config_context_t *
suscan_config_context_new(const char *name)
{
  suscan_config_context_t *new = NULL;

  SU_TRYCATCH(new = calloc(1, sizeof(suscan_config_context_t)), goto fail);

  SU_TRYCATCH(new->name = strdup(name), goto fail);
  SU_TRYCATCH(new->save_file = strbuild("%s.xml", name), goto fail);
  SU_TRYCATCH(new->list = suscan_object_new(SUSCAN_OBJECT_TYPE_SET), goto fail);

  new->save = SU_TRUE;

  return new;

fail:
  if (new != NULL)
    suscan_config_context_destroy(new);

  return NULL;
}

SUBOOL
suscan_config_context_add_path(suscan_config_context_t *ctx, const char *path)
{
  char *newpath = NULL;

  SU_TRYCATCH(newpath = strdup(path), goto fail);

  SU_TRYCATCH(PTR_LIST_APPEND_CHECK(ctx->path, newpath) != -1, goto fail);

  return SU_TRUE;

fail:
  if (newpath != NULL)
    free(newpath);

  return SU_FALSE;
}

suscan_config_context_t *
suscan_config_context_lookup(const char *name)
{
  unsigned int i;

  for (i = 0; i < context_count; ++i)
    if (strcmp(context_list[i]->name, name) == 0)
      return context_list[i];

  return NULL;
}

SUPRIVATE SUBOOL
suscan_config_context_register(suscan_config_context_t *context)
{
  return PTR_LIST_APPEND_CHECK(context, context) != -1;
}

suscan_config_context_t *
suscan_config_context_assert(const char *name)
{
  suscan_config_context_t *ctx = NULL;

  if ((ctx = suscan_config_context_lookup(name)) == NULL) {
    SU_TRYCATCH(ctx = suscan_config_context_new(name), goto fail);
    SU_TRYCATCH(suscan_config_context_register(ctx), goto fail);
  }

  return ctx;

fail:
  if (ctx != NULL)
    suscan_config_context_destroy(ctx);

  return NULL;
}

SUBOOL
suscan_config_context_put(
    suscan_config_context_t *context,
    suscan_object_t *obj)
{
  SU_TRYCATCH(suscan_object_set_append(context->list, obj), return SU_FALSE);

  return SU_TRUE;
}

SUBOOL
suscan_config_context_remove(
    suscan_config_context_t *context,
    suscan_object_t *obj)
{
  unsigned int i, count;

  count = suscan_object_set_get_count(context->list);

  for (i = 0; i < count; ++i) {
    if (suscan_object_set_get(context->list, i) == obj) {
      (void) suscan_object_set_put(context->list, i, NULL);

      return SU_TRUE;
    }
  }

  return SU_FALSE;
}

void
suscan_config_context_flush(suscan_config_context_t *context)
{
  unsigned int i;

  for (i = 0; i < context->list->object_count; ++i)
    SU_TRYCATCH(suscan_object_set_delete(context->list, i), return);
}

SUBOOL
suscan_config_context_scan(suscan_config_context_t *context)
{
  char *path = NULL;
  unsigned int i, j;
  int fd = -1;
  void *mmap_base = (void *) -1;
  suscan_object_t *set = NULL;
  struct stat sbuf;

  SUBOOL ok = SU_FALSE;

  for (i = 0; i < context->path_count; ++i) {
    SU_TRYCATCH(
        path = strbuild("%s/%s", context->path_list[i], context->save_file),
        goto done);

    if (stat(path, &sbuf) != -1) {
      SU_TRYCATCH((fd = open(path, O_RDONLY)) != -1, goto done);

      SU_TRYCATCH(
          (mmap_base = mmap(
              NULL,
              sbuf.st_size,
              PROT_READ,
              MAP_PRIVATE,
              fd,
              0)) != (void *) -1,
          goto done);

      close(fd);
      fd = -1;

      set = suscan_object_from_xml(path, mmap_base, sbuf.st_size);

      if (set != NULL) {
        for (j = 0; j < set->object_count; ++j)
          if (set->object_list[j] != NULL) {
            SU_TRYCATCH(
                suscan_object_set_append(context->list, set->object_list[j]),
                goto done);
            set->object_list[j] = NULL;
          }

        /* All set. Just destroy this object. */
        suscan_object_destroy(set);
        set = NULL;
      }


      munmap(mmap_base, sbuf.st_size);
      mmap_base = (void *) -1;
    }

    free(path);
    path = NULL;
  }

  ok = SU_TRUE;

done:
  if (set != NULL)
    suscan_object_destroy(set);

  if (fd != -1)
    close(fd);

  if (mmap_base != (void *) -1)
    munmap(mmap_base, sbuf.st_size);

  if (path != NULL)
    free(path);

  return ok;
}

void
suscan_config_context_set_save(
    suscan_config_context_t *ctx,
    SUBOOL save)
{
  ctx->save = save;
}

/*
 * TODO: This code is not robust. Save to a temporary file and
 * then move it to the right destination.
 */

SUPRIVATE SUBOOL
suscan_config_context_save(suscan_config_context_t *context)
{
  char *path = NULL;
  unsigned int i;
  int fd = -1;
  size_t size;
  void *data = NULL;
  SUBOOL ok = SU_FALSE;

  if (!context->save)
    return SU_TRUE;

  if (context->on_save != NULL)
    SU_TRYCATCH((context->on_save)(context, context->userdata), goto done);

  SU_TRYCATCH(suscan_object_to_xml(context->list, &data, &size), goto done);

  for (i = 0; i < context->path_count; ++i) {
    SU_TRYCATCH(
        path = strbuild("%s/%s", context->path_list[i], context->save_file),
        goto done);

    if ((fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600)) != -1) {
      if (write(fd, data, size) != size) {
        SU_ERROR(
            "Unexpected write error while saving config context `%s'\n",
            context->name);
        goto done;
      }

      ok = SU_TRUE;
      goto done;
    }

    free(path);
    path = NULL;
  }

  SU_ERROR(
      "Couldn't save configuration context `%s': no suitable target directory found\n",
      context->name);

done:
  if (fd != -1)
    close(fd);

  if (path != NULL)
    free(path);

  if (data != NULL)
    free(data);

  return ok;
}

SUBOOL
suscan_confdb_scan_all(void)
{
  unsigned int i;

  for (i = 0; i < context_count; ++i)
    if (!suscan_config_context_scan(context_list[i]))
      SU_WARNING(
          "Failed to scan configuration context `%s'\n",
          context_list[i]->name);

  return SU_TRUE;
}

SUBOOL
suscan_confdb_save_all(void)
{
  unsigned int i;

  for (i = 0; i < context_count; ++i)
    if (!suscan_config_context_save(context_list[i]))
      SU_WARNING(
          "Failed to save configuration context `%s'\n",
          context_list[i]->name);

  return SU_TRUE;
}

SUBOOL
suscan_confdb_use(const char *name)
{
  suscan_config_context_t *ctx = NULL;

  SU_TRYCATCH(ctx = suscan_config_context_assert(name), return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_context_add_path(ctx, suscan_confdb_get_local_path()),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_context_add_path(ctx, suscan_confdb_get_system_path()),
      return SU_FALSE);

  SU_TRYCATCH(suscan_config_context_scan(ctx), return SU_FALSE);

  return SU_TRUE;
}

