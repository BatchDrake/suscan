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

#define SU_LOG_DOMAIN "cli-profiles"

#include <sigutils/log.h>
#include <analyzer/analyzer.h>
#include <string.h>

#include <cli/cli.h>
#include <cli/cmds.h>

SUBOOL
suscli_profiles_cb(const hashlist_t *params)
{
  unsigned int i;

  for (i = 1; i <= suscli_get_source_count(); ++i) {
    printf(
          "[%3d] \"%s\"\n",
          i,
          suscan_source_config_get_label(
              suscli_get_source(i)));
  }

  return SU_TRUE;
}
