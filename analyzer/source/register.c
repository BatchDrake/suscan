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

#define SU_LOG_DOMAIN "source-register"

#include <sigutils/types.h>
#include <sigutils/defs.h>

#include <util/rbtree.h>
#include <util/hashlist.h>

#include <analyzer/source.h>

SUPRIVATE rbtree_t   *g_type_to_source_impl;
SUPRIVATE hashlist_t *g_name_to_source_impl;
SUPRIVATE int         g_source_type_ndx;

SUBOOL
suscan_source_interface_walk(
    SUBOOL (*function) (
      const struct suscan_source_interface *iface,
      void *private),
    void *private)
{
  struct rbtree_node *node;
  const struct suscan_source_interface *iface;
  
  /* The rbtree must be initialized */
  if (g_type_to_source_impl == NULL)
    return SU_FALSE;
  
  node = rbtree_get_first(g_type_to_source_impl);
  
  while (node != NULL) {
    iface = rbtree_node_data(node);
    if (!(function)(iface, private))
      return SU_FALSE;
  }
  
  return SU_TRUE;
}

SUBOOL
suscan_source_register(const struct suscan_source_interface *iface)
{
  struct rbtree_node *node;
  const struct suscan_source_interface *existing;
  int ndx = g_source_type_ndx;
  int ret = -1;

  node = rbtree_search(g_type_to_source_impl, ndx, RB_EXACT);
  if (node != NULL) {
    existing = rbtree_node_data(node);

    SU_ERROR(
      "Failed to register source type `%s': index %d already registered by `%s'\n",
      iface->name,
      ndx,
      existing->name);

    goto done;
  }

  if (hashlist_contains(g_name_to_source_impl, iface->name)) {
    SU_ERROR(
      "Failed to register source type `%s': name already exists\n",
      iface->name);

    goto done;
  }

  SU_TRYC(rbtree_insert(g_type_to_source_impl, ndx, (void *) iface));
  SU_TRY(hashlist_set(g_name_to_source_impl, iface->name, (void *) iface));

  ++g_source_type_ndx;
  ret = ndx;

done:
  return ret;
}

const struct suscan_source_interface *
suscan_source_interface_lookup_by_index(int ndx)
{
  struct rbtree_node *node;

  node = rbtree_search(g_type_to_source_impl, ndx, RB_EXACT);
  if (node != NULL)
    return rbtree_node_data(node);

  return NULL;
}

const struct suscan_source_interface *
suscan_source_interface_lookup_by_name(const char *name)
{
  return hashlist_get(g_name_to_source_impl, name);
}

SUBOOL
suscan_source_init_source_types(void)
{
  SUBOOL ok = SU_FALSE;

  SU_MAKE(g_type_to_source_impl, rbtree);
  SU_MAKE(g_name_to_source_impl, hashlist);

  SU_TRY(suscan_source_register_file());
  SU_TRY(suscan_source_register_soapysdr());
  SU_TRY(suscan_source_register_stdin());
  SU_TRY(suscan_source_register_tonegen());

  ok = SU_TRUE;

done:
  return ok;
}
