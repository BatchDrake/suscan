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

#define SU_LOG_DOMAIN "device-discovery"

#include "discovery.h"
#include "properties.h"
#include "spec.h"

#include <hashlist.h>
#include <sigutils/log.h>
#include <analyzer/source.h>
#include <analyzer/analyzer.h>

SUPRIVATE hashlist_t *g_discovery_map;

const struct suscan_device_discovery_interface *
suscan_device_discovery_lookup(const char *name)
{
  if (g_discovery_map == NULL)
    return NULL;
  
  return hashlist_get(g_discovery_map, name);
}

char **
suscan_device_discovery_get_names()
{
  PTR_LIST_LOCAL(char, name);
  hashlist_iterator_t it;
  SUBOOL ok = SU_FALSE;

  if (g_discovery_map != NULL) {
    it = hashlist_begin(g_discovery_map);

    while (!hashlist_iterator_end(&it)) {
      SU_TRYC(PTR_LIST_APPEND_CHECK(name, it.name));
      hashlist_iterator_advance(&it);
    }
  }
  
  SU_TRYC(PTR_LIST_APPEND_CHECK(name, NULL));

  ok = SU_TRUE;

done:
  if (!ok) {
    if (name_list != NULL)
      free(name_list);

    name_list = NULL;
  }

  return name_list;
}

SUBOOL
suscan_device_discovery_register(const struct suscan_device_discovery_interface *iface)
{
  SUBOOL ok = SU_FALSE;
  const struct suscan_device_discovery_interface *existing;

  if (iface->name == NULL) {
    SU_ERROR("Anonymous discovery interfaces not allowed\n");
    goto done;
  }

  if (g_discovery_map == NULL)
    SU_MAKE(g_discovery_map, hashlist);

  existing = suscan_device_discovery_lookup(iface->name);
  if (existing != NULL) {
    if (existing != iface) {
      SU_ERROR("Discovery interface `%s' already registered\n", iface->name);
      goto done;
    }
  } else {
    SU_TRY(hashlist_set(g_discovery_map, iface->name, (void *) iface));
  }

  ok = SU_TRUE;

done:
  return ok;
}

SU_INSTANCER(suscan_device_discovery, const char *name)
{
  suscan_device_discovery_t *new = NULL;

  const struct suscan_device_discovery_interface *iface = NULL;

  if ((iface = suscan_device_discovery_lookup(name)) == NULL) {
    SU_ERROR("Discovery interface `%s' does not exist\n", name);
    goto fail;  
  }

  SU_ALLOCATE_FAIL(new, suscan_device_discovery_t);

  SU_TRYZ_FAIL(pthread_mutex_init(&new->mutex, NULL));
  new->have_mutex = SU_TRUE;

  new->iface = iface;

  return new;

fail:
  if (new != NULL)
    SU_DISPOSE(suscan_device_discovery, new);

  return new;
}

SU_COLLECTOR(suscan_device_discovery)
{
  if (self->impl != NULL)
    suscan_device_discovery_stop(self);
  
  suscan_device_discovery_clear_unsafe(self);
  suscan_device_discovery_discard_unsafe(self);

  if (self->have_mutex)
    pthread_mutex_destroy(&self->mutex);
  
  free(self);
}

SU_GETTER(suscan_device_discovery, uint32_t, epoch)
{
  return self->epoch;
}

SU_GETTER(suscan_device_discovery, int, devices, struct suscan_device_properties ***properties)
{
  suscan_device_discovery_t *mut = (suscan_device_discovery_t *) self;
  SUBOOL ok = SU_FALSE;
  PTR_LIST_LOCAL(struct suscan_device_properties, dev);
  unsigned int i;

  pthread_mutex_lock(&mut->mutex);

  dev_count = self->device_count;

  if (dev_count > 0) {
    SU_ALLOCATE_MANY(
      dev_list,
      dev_count,
      struct suscan_device_properties *);
    
    for (i = 0; i < dev_count; ++i) {
      SU_TRY(dev_list[i] = suscan_device_properties_dup(self->device_list[i]));
      dev_list[i]->discovery = mut;
      dev_list[i]->epoch     = self->device_list[i]->epoch;
    }
  }

  *properties = dev_list;
  ok = SU_TRUE;

done:
  pthread_mutex_unlock(&mut->mutex);

  if (!ok) {
    for (i = 0; i < dev_count; ++i)
      if (dev_list[i] != NULL)
        SU_DISPOSE(suscan_device_properties, dev_list[i]);
    if (dev_list != NULL)
      free(dev_list);

    *properties = NULL;
    dev_count = -1;
  }

  return dev_count;
}

SU_METHOD(suscan_device_discovery, SUBOOL, start)
{
  SUBOOL ok = SU_FALSE;

  if (self->next_device_count > 0) {
    SU_ERROR("%s: a previous discovery has not been comitted yet\n", self->iface->name);
    goto done;
  }

  if (self->impl == NULL) {
    if ((self->impl = (self->iface->open)()) == NULL) {
      SU_ERROR("%s: failed to make discovery object\n", self->iface->name);
      goto done;
    }
  }

  if (!(self->iface->discovery)(self->impl, self)) {
    SU_ERROR("%s: failed to start discovery\n", self->iface->name);
    goto done;
  }

  ok = SU_TRUE;

done:
  return ok;
}

SU_METHOD(suscan_device_discovery, SUBOOL, cancel)
{
  SUBOOL ok = SU_FALSE;

  if (self->impl == NULL)
    return SU_TRUE;

  ok = (self->iface->cancel)(self->impl);
  return ok;
}

SU_METHOD(suscan_device_discovery, SUBOOL, stop)
{
  SUBOOL ok = SU_FALSE;

  if (self->impl == NULL)
    return SU_TRUE;

  ok = (self->iface->close)(self->impl);

  self->impl = NULL;
  return ok;
}

SU_METHOD(suscan_device_discovery, SUBOOL, push_device, struct suscan_device_properties *prop)
{
  SUBOOL ok = SU_FALSE;

  /* Make sure the UUID is up to date */
  suscan_device_properties_update_uuid(prop);

  pthread_mutex_lock(&self->mutex);

  suscan_device_properties_set_epoch(prop, self->epoch);

  prop->discovery = self;

  SU_TRYC(PTR_LIST_APPEND_CHECK(self->next_device, prop));

  ok = SU_TRUE;

done:
  pthread_mutex_unlock(&self->mutex);
  return ok;
}

SU_METHOD(suscan_device_discovery, void,  accept)
{
  pthread_mutex_lock(&self->mutex);

  ++self->epoch;

#if 0
  SU_INFO(
    "[%s] Enter epoch %d: %d new devices\n", 
    self->iface->name,
    self->epoch,
    self->next_device_count);
#endif

  suscan_device_discovery_clear_unsafe(self);

  self->device_list  = self->next_device_list;
  self->device_count = self->next_device_count;
  
  self->next_device_list  = NULL;
  self->next_device_count = 0;

  pthread_mutex_unlock(&self->mutex);
}

SU_METHOD(suscan_device_discovery, void,   discard)
{
  pthread_mutex_lock(&self->mutex);
  suscan_device_discovery_discard_unsafe(self);
  pthread_mutex_unlock(&self->mutex);
}

SU_METHOD(suscan_device_discovery, void,   clear)
{
  pthread_mutex_lock(&self->mutex);
  suscan_device_discovery_clear_unsafe(self);
  pthread_mutex_unlock(&self->mutex);
}

SU_METHOD(suscan_device_discovery, void,  clear_unsafe)
{
  unsigned int i;

  for (i = 0; i < self->device_count; ++i)
    if (self->device_list[i] != NULL)
      SU_DISPOSE(suscan_device_properties, self->device_list[i]);
  if (self->device_list != NULL)
    free(self->device_list);

  self->device_list  = NULL;
  self->device_count = 0;
}

SU_METHOD(suscan_device_discovery, void,  discard_unsafe)
{
  unsigned int i;

  for (i = 0; i < self->next_device_count; ++i)
    if (self->next_device_list[i] != NULL)
      SU_DISPOSE(suscan_device_properties, self->next_device_list[i]);
  if (self->next_device_list != NULL)
    free(self->next_device_list);

  self->next_device_list  = NULL;
  self->next_device_count = 0;
}
