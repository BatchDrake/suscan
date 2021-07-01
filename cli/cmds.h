/*

  Copyright (C) 2020 Gonzalo José Carracedo Carballal

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

#ifndef _CLI_CMDS_H
#define _CLI_CMDS_H

#include <sigutils/sigutils.h>
#include <util/hashlist.h>

SUBOOL suscli_devserv_cb(const hashlist_t *params);
SUBOOL suscli_profiles_cb(const hashlist_t *params);
SUBOOL suscli_rms_cb(const hashlist_t *params);
SUBOOL suscli_profinfo_cb(const hashlist_t *params);
SUBOOL suscli_radio_cb(const hashlist_t *params);
SUBOOL suscli_devices_cb(const hashlist_t *params);

#endif /* _CLI_CMDS_H */
