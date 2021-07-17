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

#define SU_LOG_DOMAIN "insp-interface"

#include "interface.h"

PTR_LIST_CONST(struct suscan_inspector_interface, insp_iface);

const struct suscan_inspector_interface *
suscan_inspector_interface_lookup(const char *name)
{
  unsigned int i;

  for (i = 0; i < insp_iface_count; ++i)
    if (strcmp(insp_iface_list[i]->name, name) == 0)
      return insp_iface_list[i];

  return NULL;
}

SUBOOL
suscan_inspector_interface_register(
    const struct suscan_inspector_interface *iface)
{
  SU_TRYCATCH(
      suscan_inspector_interface_lookup(iface->name) == NULL,
      return SU_FALSE);

  SU_TRYCATCH(
      PTR_LIST_APPEND_CHECK(insp_iface, (void *) iface) != -1,
      return SU_FALSE);

  return SU_TRUE;
}

void
suscan_inspector_interface_get_list(
    const struct suscan_inspector_interface ***iface_list,
    unsigned int *iface_count)
{
  *iface_list = insp_iface_list;
  *iface_count = insp_iface_count;
}

SUBOOL
suscan_inspector_interface_add_spectsrc(
    struct suscan_inspector_interface *iface,
    const char *name)
{
  const struct suscan_spectsrc_class *class;

  if (!suscan_spectsrcs_initialized())
    return SU_FALSE;

  SU_TRYCATCH(class = suscan_spectsrc_class_lookup(name), return SU_FALSE);

  SU_TRYCATCH(
      PTR_LIST_APPEND_CHECK(iface->spectsrc, (void *) class) != -1,
      return SU_FALSE);

  return SU_TRUE;
}

SUBOOL
suscan_inspector_interface_add_estimator(
    struct suscan_inspector_interface *iface,
    const char *name)
{
  const struct suscan_estimator_class *class;

  if (!suscan_estimators_initialized())
    return SU_FALSE;

  SU_TRYCATCH(class = suscan_estimator_class_lookup(name), return SU_FALSE);

  SU_TRYCATCH(
      PTR_LIST_APPEND_CHECK(iface->estimator, (void *) class) != -1,
      return SU_FALSE);

  return SU_TRUE;
}
