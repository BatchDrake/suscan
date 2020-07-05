/*

  Copyright (C) 2020 Gonzalo José Carracedo Carballal

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

#define SU_LOG_DOMAIN "cli"

#include <sigutils/log.h>
#include <analyzer/analyzer.h>
#include <codec/codec.h>
#include <string.h>

#include <cli/cli.h>
#include <cli/cmds.h>

PTR_LIST(SUPRIVATE struct suscli_command, command);
SUPRIVATE uint32_t init_mask = 0;

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
        || strcasecmp(value, "1") == 0) {
      dfl = SU_TRUE;
    } else if (strcasecmp(value, "false") == 0
        || strcasecmp(value, "no") == 0
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
      suscan_init_sources());

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
          "Root mean square of signal",
          SUSCLI_COMMAND_REQ_SOURCES | SUSCLI_COMMAND_REQ_INSPECTORS,
          suscli_rms_cb) != -1,
      goto fail);

  SU_TRYCATCH(
      suscli_command_register(
          "rmstone",
          "Play audible tone according to signal power",
          SUSCLI_COMMAND_REQ_SOURCES | SUSCLI_COMMAND_REQ_INSPECTORS,
          suscli_rmstone_cb) != -1,
      goto fail);

  ok = SU_TRUE;

fail:
  return ok;
}

