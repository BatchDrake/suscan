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

#define SU_LOG_DOMAIN "cli"

#include <sigutils/log.h>
#include <analyzer/analyzer.h>
#include <util/confdb.h>
#include <codec/codec.h>
#include <string.h>

#include <cli/cli.h>
#include <cli/cmds.h>

PTR_LIST_PRIVATE(struct suscli_command, command);
suscan_source_config_t *ui_config;
PTR_LIST_PRIVATE(suscan_source_config_t, cli_config);

SUPRIVATE uint32_t init_mask = 0;

suscan_source_config_t *
suscli_get_source(unsigned int id)
{
  if (id <= 0 || id > cli_config_count)
    return NULL;

  return cli_config_list[id - 1];
}

unsigned int
suscli_get_source_count(void)
{
  return cli_config_count;
}

SUPRIVATE SUBOOL
suscli_walk_all_sources(suscan_source_config_t *config, void *privdata)
{
  return PTR_LIST_APPEND_CHECK(cli_config, config) != -1;
}

suscan_source_config_t *
suscli_lookup_profile(const char *name)
{
  int i;

  if (name == NULL)
    return ui_config;

  for (i = 0; i < cli_config_count; ++i)
    if (suscan_source_config_get_label(cli_config_list[i]) != NULL
        && strcasecmp(
            suscan_source_config_get_label(cli_config_list[i]),
            name) == 0)
      return cli_config_list[i];

  return NULL;
}

SUPRIVATE void
suscli_command_destroy(struct suscli_command *cmd)
{
  if (cmd->name != NULL)
    free(cmd->name);

  if (cmd->description != NULL)
    free(cmd->description);

  free(cmd);
}

SUBOOL
suscli_param_read_int(
    const hashlist_t *params,
    const char *key,
    int *out,
    int dfl)
{
  const char *value;
  SUBOOL ok = SU_FALSE;

  if ((value = hashlist_get(params, key)) != NULL)
    if (sscanf(value, "%i", &dfl) < 1) {
      SU_ERROR("Parameter `%s' is not an integer.\n", key);
      goto fail;
    }

  *out = dfl;

  ok = SU_TRUE;

fail:
  return ok;
}

SUBOOL
suscli_param_read_profile(
    const hashlist_t *p,
    const char *key,
    suscan_source_config_t **out)
{
  int profile_id = 0;
  suscan_source_config_t *profile = NULL;
  const char *profile_name;

  if (suscli_param_read_int(p, key, &profile_id, profile_id)) {
    if (profile_id == 0) {
      profile = ui_config;
    } else if (profile_id > 0 && profile_id <= cli_config_count) {
      profile = cli_config_list[profile_id - 1];
    } else {
      SU_ERROR("Profile index `%d' out ouf bounds.\n", profile_id);
      return SU_FALSE;
    }
  } else {
    SU_TRYCATCH(
        suscli_param_read_string(
            p,
            "profile",
            &profile_name,
            NULL),
        return SU_FALSE);
    if ((profile = suscli_lookup_profile(profile_name)) == NULL) {
        SU_ERROR("Profile `%d' does not exist.\n", profile_name);
        return SU_FALSE;
    }
  }

  *out = profile;

  return SU_TRUE;

}

SUBOOL
suscli_param_read_double(
    const hashlist_t *params,
    const char *key,
    SUDOUBLE *out,
    SUDOUBLE dfl)
{
  const char *value;
  SUBOOL ok = SU_FALSE;

  if ((value = hashlist_get(params, key)) != NULL)
    if (sscanf(value, "%lg", &dfl) < 1) {
      SU_ERROR("Parameter `%s' is not a double-precision real number.\n", key);
      goto fail;
    }

  *out = dfl;

  ok = SU_TRUE;

fail:
  return ok;
}

SUBOOL
suscli_param_read_float(
    const hashlist_t *params,
    const char *key,
    SUFLOAT *out,
    SUFLOAT dfl)
{
  const char *value;
  SUBOOL ok = SU_FALSE;

  if ((value = hashlist_get(params, key)) != NULL)
    if (sscanf(value, "%g", &dfl) < 1) {
      SU_ERROR("Parameter `%s' is not a real number.\n", key);
      goto fail;
    }

  *out = dfl;

  ok = SU_TRUE;

fail:
  return ok;
}

SUBOOL
suscli_param_read_string(
    const hashlist_t *params,
    const char *key,
    const char **out,
    const char *dfl)
{
  const char *value;
  SUBOOL ok = SU_FALSE;

  if ((value = hashlist_get(params, key)) != NULL)
    dfl = value;

  *out = dfl;

  ok = SU_TRUE;

  return ok;
}

SUBOOL
suscli_param_read_bool(
    const hashlist_t *params,
    const char *key,
    SUBOOL *out,
    SUBOOL dfl)
{
  const char *value;
  SUBOOL ok = SU_FALSE;

  if ((value = hashlist_get(params, key)) != NULL) {
    if (strcasecmp(value, "true") == 0
        || strcasecmp(value, "yes") == 0
        || strcasecmp(value, "on") == 0
        || strcasecmp(value, "1") == 0) {
      dfl = SU_TRUE;
    } else if (strcasecmp(value, "false") == 0
        || strcasecmp(value, "no") == 0
        || strcasecmp(value, "off") == 0
        || strcasecmp(value, "0") == 0) {
      dfl = SU_FALSE;
    } else {
      SU_ERROR("Parameter `%s' is not a boolean value.\n", key);
      goto fail;
    }
  }

  *out = dfl;

  ok = SU_TRUE;

fail:
  return ok;
}


SUPRIVATE struct suscli_command *
suscli_command_new(
    const char *name,
    const char *descr,
    uint32_t flags,
    SUBOOL (*callback) (const hashlist_t *))
{
  struct suscli_command *new = NULL;

  SU_TRYCATCH(new = calloc(1, sizeof(struct suscli_command)), goto fail);

  SU_TRYCATCH(new->name = strdup(name), goto fail);
  SU_TRYCATCH(new->description = strdup(descr), goto fail);

  new->flags = flags;
  new->callback = callback;

  return new;

fail:
  if (new != NULL)
    suscli_command_destroy(new);

  return NULL;
}

SUBOOL
suscli_command_register(
    const char *name,
    const char *description,
    uint32_t flags,
    SUBOOL (*callback) (const hashlist_t *))
{
  struct suscli_command *new = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      new = suscli_command_new(name, description, flags, callback),
      goto fail);

  SU_TRYCATCH(PTR_LIST_APPEND_CHECK(command, new) != -1, goto fail);

  ok = SU_TRUE;

fail:
  if (!ok && new != NULL)
    suscli_command_destroy(new);

  return ok;
}

const struct suscli_command *
suscli_command_lookup(const char *name)
{
  int i;

  for (i = 0; i < command_count; ++i)
    if (strcmp(command_list[i]->name, name) == 0)
      return command_list[i];

  return NULL;
}

SUPRIVATE void
suscli_params_dtor(const char *key, void *data, void *userdata)
{
  free(data);
}

SUPRIVATE hashlist_t *
suscli_parse_params(const char **argv)
{
  hashlist_t *new = NULL;
  hashlist_t *result = NULL;
  char *copy = NULL;
  char *val = NULL;
  char *eq = NULL;

  SU_TRYCATCH(new = hashlist_new(), goto fail);
  hashlist_set_dtor(new, suscli_params_dtor);

  while (*argv != NULL) {
    SU_TRYCATCH(copy = strdup(*argv++), goto fail);

    if ((eq = strchr(copy, '=')) != NULL)
      *eq++ = '\0';

    SU_TRYCATCH(val = strdup(eq != NULL ? eq : "1"), goto fail);

    SU_TRYCATCH(hashlist_set(new, copy, val), goto fail);

    val = NULL;
    free(copy);

    copy = NULL;
  }

  result = new;
  new = NULL;

fail:
  if (copy != NULL)
    free(copy);

  if (val != NULL)
    free(val);

  if (new != NULL)
    hashlist_destroy(new);

  return result;
}

#define SUSCLI_ASSERT_INIT(flag, command)      \
if ((init_mask & flag) ^ (cmd->flags &flag)) { \
  SU_TRYCATCH(command, goto fail);             \
  init_mask |= flag;                           \
}

SUBOOL
suscli_init_ui_source(void)
{
  suscan_config_context_t *ctx = NULL;
  suscan_source_config_t *cfg = NULL;
  const suscan_object_t *list = NULL;
  const suscan_object_t *qtuiobj = NULL;
  const suscan_object_t *cfgobj = NULL;
  unsigned int i, count;
  const char *tmp;

  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(suscan_confdb_use("uiconfig"), goto fail);

  SU_TRYCATCH(
      ctx = suscan_config_context_assert("uiconfig"),
      goto fail);

  /*
   * suscan_config_context_set_on_save(ctx, suscan_sources_on_save, NULL);
   */

  list = suscan_config_context_get_list(ctx);

  count = suscan_object_set_get_count(list);

  /* For each object in config */
  for (i = 0; i < count; ++i) {
    if ((qtuiobj = suscan_object_set_get(list, i)) != NULL) {
      if ((tmp = suscan_object_get_class(qtuiobj)) != NULL
          && strcmp(tmp, "qtui") == 0) {

        /* For each QT UI config */
        if ((cfgobj = suscan_object_get_field(qtuiobj, "source")) != NULL
            && (tmp = suscan_object_get_class(cfgobj)) != NULL
            && strcmp(tmp, "source_config") == 0) {
          if ((cfg = suscan_source_config_from_object(cfgobj)) == NULL) {
            SU_WARNING("Could not parse UI source config #%d from config\n", i);
          } else {
            SU_TRYCATCH(
                suscan_source_config_set_label(cfg, "UI profile"),
                goto fail);
            break;
          }
        }
      }
    }
  }

  if (cfg == NULL)
    SU_TRYCATCH(cfg = suscan_source_config_new_default(), goto fail);

  ui_config = cfg;

  ok = SU_TRUE;

fail:
  return ok;
}

SUBOOL
suscli_register_sources(void)
{
  return suscan_source_config_walk(suscli_walk_all_sources, 0);
}

SUBOOL
suscli_run_command(const char *name, const char **argv)
{
  const struct suscli_command *cmd;
  hashlist_t *params = NULL;
  SUBOOL ok = SU_FALSE;

  if ((cmd = suscli_command_lookup(name)) == NULL) {
    fprintf(stderr, "%s: command does not exist\n", name);
    goto fail;
  }
  SUSCLI_ASSERT_INIT(
      SUSCLI_COMMAND_REQ_CODECS,
      suscan_codec_class_register_builtin());

  SUSCLI_ASSERT_INIT(
      SUSCLI_COMMAND_REQ_SOURCES,
      suscan_init_sources()
      && suscli_init_ui_source()
      && suscli_register_sources());

  SUSCLI_ASSERT_INIT(
      SUSCLI_COMMAND_REQ_ESTIMATORS,
      suscan_init_estimators());

  SUSCLI_ASSERT_INIT(
      SUSCLI_COMMAND_REQ_SPECTSRCS,
      suscan_init_spectsrcs());

  SUSCLI_ASSERT_INIT(
      SUSCLI_COMMAND_REQ_INSPECTORS,
      suscan_init_inspectors());

  SU_TRYCATCH(params = suscli_parse_params(argv), goto fail);

  ok = (cmd->callback) (params);

fail:
  if (params != NULL)
    hashlist_destroy(params);

  return ok;
}

SUPRIVATE SUBOOL
suscli_init_cb(const hashlist_t *params)
{
  int i;

  fprintf(
      stderr,
      "Command list:\n");

  for (i = 0; i < command_count; ++i) {
    fprintf(
        stderr,
        "  %-10s%s\n",
        command_list[i]->name,
        command_list[i]->description);
  }

  return SU_TRUE;
}

SUBOOL
suscli_init(void)
{
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      suscli_command_register(
          "list",
          "List all available commands",
          0,
          suscli_init_cb) != -1,
      goto fail);


  SU_TRYCATCH(
      suscli_command_register(
          "profiles",
          "List profiles",
          SUSCLI_COMMAND_REQ_SOURCES,
          suscli_profiles_cb) != -1,
      goto fail);

  SU_TRYCATCH(
      suscli_command_register(
          "rms",
          "Perform different kinds of power measurements",
          SUSCLI_COMMAND_REQ_SOURCES | SUSCLI_COMMAND_REQ_INSPECTORS,
          suscli_rms_cb) != -1,
      goto fail);

  SU_TRYCATCH(
      suscli_command_register(
          "radio",
          "Listen to analog radio",
          SUSCLI_COMMAND_REQ_SOURCES | SUSCLI_COMMAND_REQ_INSPECTORS,
          suscli_radio_cb) != -1,
      goto fail);

  SU_TRYCATCH(
      suscli_command_register(
          "profinfo",
          "Display profile information",
          SUSCLI_COMMAND_REQ_SOURCES,
          suscli_profinfo_cb) != -1,
      goto fail);

  SU_TRYCATCH(
      suscli_command_register(
          "devices",
          "Display detected devices",
          SUSCLI_COMMAND_REQ_SOURCES,
          suscli_devices_cb) != -1,
      goto fail);

  SU_TRYCATCH(
      suscli_command_register(
          "makeprof",
          "Generate profiles from detected devices",
          SUSCLI_COMMAND_REQ_SOURCES,
          suscli_makeprof_cb) != -1,
      goto fail);

  SU_TRYCATCH(
      suscli_command_register(
          "devserv",
          "Start the SuRPC remove device server",
          SUSCLI_COMMAND_REQ_ALL,
          suscli_devserv_cb) != -1,
      goto fail);
  ok = SU_TRUE;

fail:
  return ok;
}

