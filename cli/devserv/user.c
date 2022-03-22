/*

  Copyright (C) 2022 Gonzalo José Carracedo Carballal

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

#define SU_LOG_DOMAIN "user"

#include <cli/devserv/devserv.h>
#include <util/hashlist.h>
#include <util/confdb.h>

#ifndef _POSIX_SOURCE
#  define _POSIX_SOURCE
#endif /* _POSIX_SOURCE */

#include <regex.h>

SUPRIVATE hashlist_t *g_user_hash;
PTR_LIST_PRIVATE(struct suscli_user_entry, g_user);

SUPRIVATE const char *g_perm_strings[] = 
{
  "analyzer.halt",
  "source.frequency",
  "source.gain",
  "source.antenna",
  "source.bandwidth",
  "source.ppm",
  "source.dc-remove",
  "source.iq-reverse",
  "source.agc",
  "inspector.open.audio",
  "inspector.open.raw",
  "inspector.open.inspector",
  "fft.size",
  "fft.rate",
  "fft.window",
  "source.seek",
  "source.throttle"
};

SUPRIVATE SUBOOL
suscli_devserv_ensure_user_list(void)
{
  SUBOOL ok = SU_FALSE;

  if (g_user_hash == NULL)
    SU_MAKE(g_user_hash, hashlist);

  ok = SU_TRUE;

done:
  return ok;
}

void
suscli_devserv_flush_users(void)
{
  unsigned int i;

  /* Flush user list */
  for (i = 0; i < g_user_count; ++i)
    suscli_user_entry_destroy(g_user_list[i]);
  if (g_user_list != NULL)
    g_user_list = NULL;
  g_user_count = 0;

  /* Destroy hash */
  if (g_user_hash != NULL) {
    hashlist_destroy(g_user_hash);
    g_user_hash = NULL;
  }
}

struct suscli_user_entry *
suscli_devserv_lookup_user(const char *user)
{
  if (g_user_hash == NULL)
    return NULL;

  return hashlist_get(g_user_hash, user);
}

SUBOOL
suscli_devserv_register_user(
  const char *user,
  const char *pass,
  uint64_t permissions)
{
  struct suscli_user_entry *entry = NULL, *ref;
  char *tmp = NULL;
  SUBOOL ok = SU_FALSE;

  entry = suscli_devserv_lookup_user(user);

  if (entry != NULL) {
    SU_TRY(tmp = strdup(pass));
    free(entry->password);
    entry->password = tmp;
    entry->permissions = permissions;
  } else {
    SU_TRY(suscli_devserv_ensure_user_list());
    SU_MAKE(entry, suscli_user_entry, user, pass, permissions);
    SU_TRYC(PTR_LIST_APPEND_CHECK(g_user, entry));
    ref = entry;
    entry = NULL;
    SU_TRY(hashlist_set(g_user_hash, user, ref));
  }

  entry = NULL;

  ok = SU_TRUE;

done:
  if (tmp != NULL)
    free(tmp);

  if (entry != NULL)
    suscli_user_entry_destroy(entry);

  return ok;
}

uint64_t
suscli_devserv_permission_match(const char *expr)
{
  uint64_t mask = 0;
  unsigned int i, count;
  SUBOOL compiled = SU_FALSE;
  regex_t preg;

  SU_TRY(regcomp(&preg, expr, REG_EXTENDED) == 0);
  compiled = SU_TRUE;

  count = sizeof(g_perm_strings) / sizeof(g_perm_strings[0]);

  for (i = 0; i < count; ++i)
    if (regexec(&preg, g_perm_strings[i], (size_t) 0, NULL, 0) == 0)
      mask |= 1ull << i;
  
done:
  if (compiled)
    regfree(&preg);

  return mask;
}

SUBOOL
suscli_analyzer_server_add_all_users(suscli_analyzer_server_t *server)
{
  unsigned int i;
  SUBOOL ok = SU_FALSE;

  for (i = 0; i < g_user_count; ++i)
    SU_TRY(
      suscli_analyzer_server_add_user(
        server,
        g_user_list[i]->user,
        g_user_list[i]->password,
        g_user_list[i]->permissions));

  ok = SU_TRUE;

done:
  return ok;
}

SUPRIVATE SUBOOL
suscli_dervserv_process_user_entry(const suscan_object_t *obj)
{
  const char *class;
  const char *user, *pass, *def_access;
  SUBOOL blacklist = SU_FALSE;
  const suscan_object_t *exceptions, *entry;
  uint64_t mask = 0, bits = 0;
  const char *value;
  unsigned int i, count;
  SUBOOL ok = SU_FALSE;

  if (suscan_object_get_type(obj) != SUSCAN_OBJECT_TYPE_OBJECT) {
    SU_WARNING("Non-object entry in user list database\n");
    goto done;
  }

  class = suscan_object_get_class(obj);
  if (class == NULL || strcmp(class, "UserEntry") != 0) {
    SU_WARNING(
      "User not added: unrecognized object in user list database (class is not UserEntry)");
    goto done;
  }

  user = suscan_object_get_field_value(obj, "user");
  if (user == NULL) {
    SU_WARNING("User not added: missing user name in UserEntry\n");
    goto done;
  }

  pass = suscan_object_get_field_value(obj, "password");
  if (pass == NULL) {
    SU_WARNING(
      "User not added: missing user password for user `%s' in UserEntry\n",
      user);
    goto done;
  }

  def_access = suscan_object_get_field_value(obj, "default_access");
  if (def_access != NULL) {
    if (strcmp(def_access, "deny") == 0) {
      blacklist = SU_FALSE;
    } else if (strcmp(def_access, "allow") == 0) {
      blacklist = SU_TRUE;
    } else {
      SU_WARNING(
        "User not added: invalid default access for user entry `%s' (must be either allow or deny)",
        user);
      goto done;
    }
  }

  exceptions = suscan_object_get_field(obj, "exceptions");
  if (exceptions != NULL) {
    if (suscan_object_get_type(exceptions) != SUSCAN_OBJECT_TYPE_SET) {
      SU_WARNING(
        "User not added: invalid type for user `%s' exceptions (must be a set)\n",
        user);
      goto done;
    }

    count = suscan_object_set_get_count(exceptions);
    for (i = 0; i < count; ++i) {
      entry = suscan_object_set_get(exceptions, i);
      if (suscan_object_get_type(entry) != SUSCAN_OBJECT_TYPE_FIELD) {
        SU_WARNING(
          "User not added: invalid exception type for user `%s' (must be a value)\n",
          user);
        goto done;
      }

      value = suscan_object_get_value(entry);

      if (value != NULL) {
        bits = suscli_devserv_permission_match(value);
        if (bits == 0) {
          SU_WARNING(
            "User not added: invalid permission mask `%s' for user `%s'\n",
            value,
            user);
          goto done;
        }

        mask |= bits;
      }
    }
  }

  if (blacklist)
    mask = ~mask;

  SU_TRY(suscli_devserv_register_user(user, pass, mask));

  ok = SU_TRUE;

done:
  return ok;
}

SUBOOL
suscli_devserv_load_users(void)
{
  suscan_config_context_t *ctx = NULL;
  const suscan_object_t *set = NULL, *entry;
  unsigned int i, count;
  SUBOOL ok = SU_FALSE;

  SU_TRY(ctx = suscan_config_context_lookup("users"));
  SU_TRY(set = suscan_config_context_get_list(ctx));

  count = suscan_object_set_get_count(set);
  for (i = 0; i < count; ++i) {
    entry = suscan_object_set_get(set, i);
    (void) suscli_dervserv_process_user_entry(entry);
  }

  ok = g_user_count > 0;

done:
  return ok;
}
