/*

  Copyright (C) 2023 Gonzalo José Carracedo Carballal

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

#define SU_LOG_DOMAIN "cli-parse"

#include <sigutils/log.h>
#include <cli/cli.h>

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
