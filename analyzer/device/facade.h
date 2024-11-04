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

#ifndef _DEVICE_FACADE_H
#define _DEVICE_FACADE_H

#include <sigutils/types.h>
#include <sigutils/defs.h>
#include <rbtree.h>
#include "discovery.h"
#include <analyzer/worker.h>

struct suscan_device_spec;
struct suscan_device_facade;
struct suscan_device_properties;

struct suscan_device_discovery_thread {
  const struct suscan_device_discovery_interface *iface;
  suscan_device_discovery_t                      *discovery;
  suscan_worker_t                                *worker;
  SUBOOL                                          in_progress;
};

typedef struct suscan_device_discovery_thread suscan_device_discovery_thread_t;

SU_INSTANCER(suscan_device_discovery_thread, const char *, struct suscan_mq *);
SU_COLLECTOR(suscan_device_discovery_thread);

SU_METHOD(suscan_device_discovery_thread, SUBOOL, discovery);
SU_METHOD(suscan_device_discovery_thread, SUBOOL, cancel);

struct suscan_device_facade {
  pthread_mutex_t  list_mutex;
  SUBOOL           have_mutex;
  rbtree_t        *uuid2device;
  suscan_worker_t *list_worker;
  
  pthread_mutex_t  disc_lock;
  SUBOOL           have_disc_lock;
  pthread_cond_t   disc_cond;
  SUBOOL           have_disc_cond;
  const char      *disc_last;

  struct suscan_mq output_mq;
  struct suscan_mq list_worker_mq;
  SUBOOL           halting;

  PTR_LIST(struct suscan_device_properties, device);
  PTR_LIST(suscan_device_discovery_thread_t, thread);
};

typedef struct suscan_device_facade suscan_device_facade_t;

suscan_device_facade_t *suscan_device_facade_instance(void);

SU_INSTANCER(suscan_device_facade);
SU_COLLECTOR(suscan_device_facade);

SU_GETTER(suscan_device_facade, struct suscan_device_properties *, get_properties, const struct suscan_device_spec *);
SU_GETTER(suscan_device_facade, suscan_device_discovery_thread_t *, get_thread, const char *);
SU_GETTER(suscan_device_facade, int,    get_all_devices, struct suscan_device_properties ***);
SU_GETTER(suscan_device_facade, int,    get_epoch_for_uuid, uint64_t);
SU_GETTER(suscan_device_facade, struct suscan_device_properties *, get_device_by_uuid, uint64_t);
SU_METHOD(suscan_device_facade, SUBOOL, discover_all);
SU_METHOD(suscan_device_facade, SUBOOL, start_discovery, const char *);
SU_METHOD(suscan_device_facade, SUBOOL, stop_discovery, const char *);
SU_GETTER(suscan_device_facade, char *, wait_for_devices, unsigned int);

#endif /* _DEVICE_FACADE_H */
