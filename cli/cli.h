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

#ifndef _SUSCLI_CLI_H
#define _SUSCLI_CLI_H

#include <sigutils/sigutils.h>
#include <util/hashlist.h>

struct suscli_command {
  char *name;
  char *description;
  SUBOOL (*callback) (const hashlist_t *);
};

SUBOOL suscli_command_register(
    const char *,
    const char *,
    SUBOOL (*callback) (const hashlist_t *));

const struct suscli_command *suscli_command_lookup(const char *);

SUBOOL suscli_run_command(const char *name, const char **argv);

#endif /* _SUSCLI_CLI_H */
