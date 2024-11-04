/*

  Copyright (C) 2024 Gonzalo José Carracedo Carballal

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

#define SU_LOG_DOMAIN "local-source"

#include "local.h"

hashlist_t *g_name2source = NULL;

SUPRIVATE SUBOOL
suscan_local_source_ensure_hashlist()
{
  SUBOOL ok = SU_FALSE;

  if (g_name2source == NULL)
    SU_MAKE(g_name2source, hashlist);

  ok = SU_TRUE;

done:
  return ok;
}

/* Internal */
const struct suscan_source_interface *
suscan_local_source_interface_lookup_by_name(const char *name)
{
  struct suscan_source_interface *new = NULL;

  SU_TRY(suscan_local_source_ensure_hashlist());
  new = hashlist_get(g_name2source, name);

done:
  return new;
}

/* Internal */
SUBOOL
suscan_local_source_interface_walk(
    SUBOOL (*function) (
      const struct suscan_source_interface *iface,
      void *private),
    void *private)
{
  hashlist_iterator_t it;
  SUBOOL ok = SU_FALSE;

  SU_TRY(suscan_local_source_ensure_hashlist());

  it = hashlist_begin(g_name2source);
  while (!hashlist_iterator_end(&it)) {
    if (!(function)(it.value, private))
      goto done;
    
    hashlist_iterator_advance(&it);
  }

  ok = SU_TRUE;

done:
  return ok;
}

/* Internal */
SUBOOL
suscan_local_source_register(const struct suscan_source_interface *iface)
{
  SUBOOL ok = SU_FALSE;

  SU_TRY(suscan_local_source_ensure_hashlist());

  if (hashlist_contains(g_name2source, iface->name)) {
    SU_WARNING("Attempting to register source `%s' twice\n", iface->name);
    goto done;
  }

  SU_TRY(hashlist_set(g_name2source, iface->name, (struct suscan_source_interface *) iface));

  ok = SU_TRUE;

done:
  return ok;
}

