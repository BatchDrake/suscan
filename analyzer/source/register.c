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

#include <analyzer/analyzer.h>
#include <analyzer/source.h>
#include <analyzer/device/discovery.h>

SUBOOL
suscan_source_interface_walk(
  const char *analyzer,
  SUBOOL (*function) (
    const struct suscan_source_interface *iface,
    void *private),
  void *private)
{
  SUBOOL ok = SU_FALSE;
  const struct suscan_analyzer_interface *aif = NULL;

  SU_TRY(aif = suscan_analyzer_interface_lookup(analyzer));

  ok = (aif->walk_sources) (function, private);

done:
  return ok;
}

SUBOOL
suscan_source_register(const struct suscan_source_interface *iface)
{
  SUBOOL ok = SU_FALSE;
  const struct suscan_analyzer_interface *aif = NULL;

  SU_TRY(aif = suscan_analyzer_interface_lookup(iface->analyzer));

  ok = (aif->register_source) (iface);

done:
  return ok;
}

const struct suscan_source_interface *
suscan_source_lookup(const char *analyzer, const char *name)
{
  const struct suscan_source_interface *iface = NULL;
  const struct suscan_analyzer_interface *aif = NULL;

  SU_TRY(aif = suscan_analyzer_interface_lookup(analyzer));

  iface = (aif->lookup_source) (name);

done:
  return iface;
}

SUBOOL
suscan_source_init_source_types(void)
{
  SUBOOL ok = SU_FALSE;

#ifndef SUSCAN_THIN_CLIENT
  SU_TRY(suscan_source_register_file());
  SU_TRY(suscan_source_register_soapysdr());
  SU_TRY(suscan_source_register_stdin());
  SU_TRY(suscan_source_register_tonegen());

  ok = SU_TRUE;

done:
#else
  ok = SU_TRUE;
#endif // SUSCAN_THIN_CLIENT

  return ok;
}
