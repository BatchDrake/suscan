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

struct walk_state {
  int counter;
};

#define walk_state_INITIALIZER { 0 }

SUPRIVATE SUBOOL
walk_all_sources(suscan_source_config_t *config, void *privdata)
{
  struct walk_state *state = (struct walk_state *) privdata;

  printf(
      "[%3d] \"%s\"\n",
      ++state->counter,
      suscan_source_config_get_label(config));

  return SU_TRUE;
}

SUBOOL
suscli_profiles_cb(const hashlist_t *params)
{
  struct walk_state state = walk_state_INITIALIZER;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(suscan_source_config_walk(walk_all_sources, &state), goto fail);

  ok = SU_TRUE;

fail:
  return ok;
}
