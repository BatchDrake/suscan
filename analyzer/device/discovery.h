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


#ifndef _DEVICE_DISCOVERY_H
#define _DEVICE_DISCOVERY_H

#include <pthread.h>
#include <sigutils/types.h>
#include <sigutils/defs.h>

#ifdef __cplusplus
extern "C" {
#endif

struct suscan_device_properties;
struct suscan_device_discovery;

struct suscan_device_discovery_interface {
  const char *name;
  void  *(*open)      ();
  SUBOOL (*discovery) (void *, struct suscan_device_discovery *);
  SUBOOL (*cancel)    (void *);
  SUBOOL (*close)     (void *);
};

struct suscan_device_discovery {
  const struct suscan_device_discovery_interface *iface;
  pthread_mutex_t mutex;
  SUBOOL have_mutex;
  void *impl;

  PTR_LIST(struct suscan_device_properties, device);
  PTR_LIST(struct suscan_device_properties, next_device);
  uint32_t epoch;
};

typedef struct suscan_device_discovery suscan_device_discovery_t;

const struct suscan_device_discovery_interface *suscan_device_discovery_lookup(const char *);
SUBOOL suscan_device_discovery_register(const struct suscan_device_discovery_interface *);
char **suscan_device_discovery_get_names();

SU_INSTANCER(suscan_device_discovery, const char *);
SU_COLLECTOR(suscan_device_discovery);


SU_GETTER(suscan_device_discovery, uint32_t, epoch);

SU_GETTER(suscan_device_discovery, int, devices, struct suscan_device_properties ***);

SU_METHOD(suscan_device_discovery, SUBOOL, start);
SU_METHOD(suscan_device_discovery, SUBOOL, cancel);
SU_METHOD(suscan_device_discovery, SUBOOL, stop);
SU_METHOD(suscan_device_discovery, SUBOOL, push_device, struct suscan_device_properties *);
SU_METHOD(suscan_device_discovery, void,   accept);
SU_METHOD(suscan_device_discovery, void,   discard);
SU_METHOD(suscan_device_discovery, void,   clear);

SU_METHOD(suscan_device_discovery, void,   discard_unsafe);
SU_METHOD(suscan_device_discovery, void,   clear_unsafe);

SUBOOL suscan_discovery_register_soapysdr();
SUBOOL suscan_discovery_register_multicast();

#ifdef __cplusplus
}
#endif

#endif /* _DEVICE_DISCOVERY_H */
