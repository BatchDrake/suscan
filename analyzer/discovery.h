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

#ifndef _SUSCAN_ANALYZER_DISCOVERY_H
#define _SUSCAN_ANALYZER_DISCOVERY_H

#include <sigutils/types.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define SURPC_DISCOVERY_PROTOCOL_PORT   5555
#define SURPC_DISCOVERY_MULTICAST_ADDR "239.255.255.250" /* Same as UPnP */
#define SURPC_DISCOVERY_MAX_PDU_SIZE    4096

SUBOOL suscan_discovered_remote_device_walk(
    SUBOOL (*function) (
        void *userdata,
        const suscan_source_device_t *device,
        const suscan_source_config_t *config),
    void *userdata);

suscan_source_config_t *suscan_discovered_remote_device_make_config(
    const suscan_source_device_t *self);

SUBOOL suscan_device_net_discovery_start(const char *iface);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _SUSCAN_ANALYZER_DISCOVERY_H */
