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

#define SU_LOG_DOMAIN "device-facade"

#include <sigutils/log.h>
#include <analyzer/worker.h>
#include <sigutils/util/compat-time.h>
#include "facade.h"
#include "properties.h"
#include "spec.h"

#include <analyzer/analyzer.h>
#include <analyzer/source.h>

SUPRIVATE suscan_device_facade_t *g_dev_facade = NULL;

#define SUSCAN_DEVICE_FACADE_DISCOVERY_SUCCEEDED 0xfacade

suscan_device_facade_t *
suscan_device_facade_instance(void)
{
  if (g_dev_facade == NULL) {
    SU_TRY_FAIL(suscan_discovery_register_soapysdr());
    SU_TRY_FAIL(suscan_discovery_register_multicast());
    SU_MAKE_FAIL(g_dev_facade, suscan_device_facade);
  }

  return g_dev_facade;

fail:
  SU_ERROR("CRITICAL! Device facade failed to initialize!\n");
  return NULL;
}

SU_INSTANCER(
  suscan_device_discovery_thread,
  const char *name,
  struct suscan_mq *oumq)
{
  const struct suscan_device_discovery_interface *iface = NULL;
  char *worker_name = NULL;
  suscan_device_discovery_thread_t *new = NULL;

  if ((iface = suscan_device_discovery_lookup(name)) == NULL) {
    SU_ERROR("Failed to create discovery thread: discovery type `%s' does not exist\n", name);
    goto fail;
  }

  SU_ALLOCATE_FAIL(new, suscan_device_discovery_thread_t);

  new->iface = iface;
  SU_MAKE_FAIL(new->discovery, suscan_device_discovery, name);

  SU_TRY_FAIL(worker_name = strbuild("%s-discovery", name));
  SU_TRY_FAIL(new->worker = suscan_worker_new_ex(worker_name, oumq, new));
  free(worker_name);

  return new;

fail:
  if (worker_name != NULL)
    free(worker_name);

  if (new != NULL)
    SU_DISPOSE(suscan_device_discovery_thread, new);

  return NULL;
}

SU_COLLECTOR(suscan_device_discovery_thread)
{
  if (self->worker != NULL)
    if (!suscan_worker_halt(self->worker))
      return;
  
  suscan_device_discovery_destroy(self->discovery);

  free(self);
}

SUPRIVATE SUBOOL
suscan_device_discovery_thread_discovery_cb(
  struct suscan_mq *mq_out,
  void *wk_private,
  void *cb_private)
{
  suscan_device_discovery_thread_t *self = wk_private;

  self->in_progress = SU_TRUE;

  SU_INFO("Discovery[%s]: starting\n", self->iface->name);

  if (suscan_device_discovery_start(self->discovery)) {
    suscan_device_discovery_accept(self->discovery);
    SU_INFO("Discovery[%s]: success\n", self->iface->name);
    suscan_mq_write(mq_out, SUSCAN_DEVICE_FACADE_DISCOVERY_SUCCEEDED, self);
  } else {
    SU_ERROR("Discovery[%s]: failed\n", self->iface->name);
  }

  self->in_progress = SU_FALSE;
  
  return SU_FALSE;
}

SU_METHOD(suscan_device_discovery_thread, SUBOOL, cancel)
{
  if (self->in_progress) {
    if (!suscan_device_discovery_cancel(self->discovery)) {
      SU_ERROR("Discovery[%s]: cannot cancel\n", self->iface->name);
      return SU_FALSE;
    }
  }

  return SU_TRUE;
}

SU_METHOD(suscan_device_discovery_thread, SUBOOL, discovery)
{
  if (self->in_progress) {
    SU_INFO(
      "Discovery[%s]: another discovery is in progress\n",
      self->iface->name);
    return SU_TRUE;
  }

  return suscan_worker_push(
    self->worker,
    suscan_device_discovery_thread_discovery_cb,
    NULL);
}

SUPRIVATE SUBOOL
suscan_device_facade_update_from_discovery(
  suscan_device_facade_t *self,
  suscan_device_discovery_t *discovery)
{
  struct rbtree_node *node;
  struct suscan_device_properties *prop = NULL;
  uint64_t uuid;
  PTR_LIST_LOCAL(struct suscan_device_properties, dev);
  unsigned int i;
  SUBOOL ok = SU_FALSE;

  pthread_mutex_lock(&self->list_mutex);

  dev_count = suscan_device_discovery_devices(discovery, &dev_list);
  if (dev_count == -1) {
    SU_ERROR(
      "Discovery[%s]: failed to retrieve devices\n",
      discovery->iface->name);
    goto done;
  }

  for (i = 0; i < dev_count; ++i) {
    uuid = suscan_device_properties_uuid(dev_list[i]);
    node = rbtree_search(self->uuid2device, uuid, RB_EXACT);

    if (node != NULL && node->data != NULL) {
      /* Already exists. Replace in list. */
      prop = node->data;
      suscan_device_properties_swap(dev_list[i], prop);
      prop = NULL;
    } else {
      /* New entry. Insert in device list. */
      prop = dev_list[i];
      SU_TRYC(PTR_LIST_APPEND_CHECK(self->device, prop));
      dev_list[i] = NULL;
      rbtree_insert(self->uuid2device, uuid, prop);
#if 0
      char *uri = NULL;
      uri = suscan_device_make_uri(
        prop->analyzer->name,
        prop->source->name,
        &prop->traits);
    
      SU_INFO("FACADE: %s\n", uri);
      free(uri);
#endif
    }
  }

  ok = SU_TRUE;

done:
  pthread_mutex_unlock(&self->list_mutex);

  return ok;
}

SUPRIVATE SUBOOL
suscan_device_facade_list_worker_cb(
  struct suscan_mq *mq_out,
  void *wk_private,
  void *cb_private)
{
  suscan_device_facade_t *self = wk_private;
  suscan_device_discovery_thread_t *thread = NULL;

  struct timeval tv = {
    .tv_sec  = 0,
    .tv_usec = 500000
  };

  if (self->halting)
    return SU_FALSE;

  while ((thread = suscan_mq_read_w_type_timeout(
    &self->output_mq,
    SUSCAN_DEVICE_FACADE_DISCOVERY_SUCCEEDED,
    &tv)) != NULL) {
    if (self->halting)
      return SU_FALSE;

    if (thread != NULL) {
      /* New devices in discovery! */
      suscan_device_facade_update_from_discovery(self, thread->discovery);
      pthread_mutex_lock(&self->disc_lock);
      self->disc_last = thread->iface->name;
      pthread_cond_signal(&self->disc_cond);
      pthread_mutex_unlock(&self->disc_lock);
    }
  }

  return SU_TRUE;
}

SU_INSTANCER(suscan_device_facade)
{
  suscan_device_facade_t *new = NULL;
  suscan_device_discovery_thread_t *thread = NULL;

  char **names = NULL;
  unsigned int i = 0;

  SU_ALLOCATE_FAIL(new, suscan_device_facade_t);

  SU_MAKE_FAIL(new->uuid2device, rbtree);

  SU_TRYZ_FAIL(pthread_mutex_init(&new->list_mutex, NULL));
  new->have_mutex = SU_TRUE;

  SU_TRYZ_FAIL(pthread_mutex_init(&new->disc_lock, NULL));
  new->have_disc_lock = SU_TRUE;

  SU_TRYZ_FAIL(pthread_cond_init(&new->disc_cond, NULL));
  new->have_disc_cond = SU_TRUE;

  SU_CONSTRUCT_FAIL(suscan_mq, &new->output_mq);
  SU_CONSTRUCT_FAIL(suscan_mq, &new->list_worker_mq);

  SU_TRY_FAIL(
    new->list_worker = suscan_worker_new_ex(
      "discovery-list",
      &new->list_worker_mq,
      new));

  SU_TRY_FAIL(
    suscan_worker_push(
      new->list_worker,
      suscan_device_facade_list_worker_cb,
      NULL));
  
  SU_TRY_FAIL(names = suscan_device_discovery_get_names());

  while (names[i] != NULL) {
    SU_MAKE_FAIL(
      thread,
      suscan_device_discovery_thread,
      names[i],
      &new->output_mq);

    SU_TRYC_FAIL(PTR_LIST_APPEND_CHECK(new->thread, thread));

    ++i;
  }

  thread = NULL;

  free(names);

  return new;

fail:
  if (new != NULL)
    SU_DISPOSE(suscan_device_facade, new);
  
  if (names != NULL)
    free(names);

  if (thread != NULL)
    SU_DISPOSE(suscan_device_discovery_thread, thread);
  
  return NULL;
}

SU_COLLECTOR(suscan_device_facade)
{
  unsigned int i;

  for (i = 0; i < self->thread_count; ++i)
    SU_DISPOSE(suscan_device_discovery_thread, self->thread_list[i]);
  
  if (self->thread_list != NULL)
    free(self->thread_list);
  
  if (self->list_worker != NULL)
    if (!suscan_worker_halt(self->list_worker))
      return;

  SU_DESTRUCT(suscan_mq, &self->output_mq);
  SU_DESTRUCT(suscan_mq, &self->list_worker_mq);
  SU_DISPOSE(rbtree, self->uuid2device);

  if (self->have_mutex)
    pthread_mutex_destroy(&self->list_mutex);
  
  if (self->have_disc_cond)
    pthread_cond_destroy(&self->disc_cond);

  if (self->have_disc_lock)
    pthread_mutex_destroy(&self->disc_lock);
  
  free(self);
}

SU_GETTER(suscan_device_facade, int, get_epoch_for_uuid, uint64_t uuid)
{
  struct rbtree_node *node;
  suscan_device_facade_t *mut = (suscan_device_facade_t *) self;
  struct suscan_device_properties *existing = NULL;
  int epoch = -1;

  pthread_mutex_lock(&mut->list_mutex);

  /* Nope, we do not have such a device */
  if ((node = rbtree_search(self->uuid2device, uuid, RB_EXACT)) == NULL 
      || node->data == NULL)
    goto done;

  existing = node->data;

  epoch = existing->epoch;

done:
  pthread_mutex_unlock(&mut->list_mutex);

  return epoch;
}

SU_GETTER(
  suscan_device_facade,
  struct suscan_device_properties *, get_device_by_uuid, uint64_t uuid)
{
  struct rbtree_node *node;
  suscan_device_facade_t *mut = (suscan_device_facade_t *) self;
  struct suscan_device_properties *existing = NULL;
  struct suscan_device_properties *new = NULL;

  pthread_mutex_lock(&mut->list_mutex);

  /* Nope, we do not have such a device */
  if ((node = rbtree_search(self->uuid2device, uuid, RB_EXACT)) == NULL 
      || node->data == NULL)
    goto done;

  existing = node->data;

  if (existing != NULL)
    SU_TRY(new = suscan_device_properties_dup(existing));
  
done:
  pthread_mutex_unlock(&mut->list_mutex);

  return new;
}


SU_GETTER(
  suscan_device_facade,
  struct suscan_device_properties *,
  get_properties,
  const suscan_device_spec_t *spec)
{
  struct rbtree_node *node;
  struct suscan_device_properties *existing = NULL;
  struct suscan_device_properties *props = NULL;
  suscan_device_facade_t *mut = (suscan_device_facade_t *) self;

  uint64_t uuid = suscan_device_spec_uuid(spec);

  pthread_mutex_lock(&mut->list_mutex);

  /* Nope, we do not have such a device */
  if ((node = rbtree_search(self->uuid2device, uuid, RB_EXACT)) == NULL 
      || node->data == NULL)
    goto done;

  existing = node->data;

  /* We have the device BUT it we didn't see it in the last discovery */
  /* TODO: Add methods for accessing these */
  if (existing->epoch + 1 < existing->discovery->epoch)
    goto done;

  SU_TRY(props = suscan_device_properties_dup(existing));

done:
  pthread_mutex_unlock(&mut->list_mutex);

  return props;
}

SU_GETTER(
  suscan_device_facade,
  int, get_all_devices, struct suscan_device_properties ***properties)
{
  SUBOOL ok = SU_FALSE;
  struct suscan_device_properties *dev;
  suscan_device_facade_t *mut = (suscan_device_facade_t *) self;
  PTR_LIST_LOCAL(struct suscan_device_properties, dev);
  unsigned int i, n = 0;

  pthread_mutex_lock(&mut->list_mutex);
  dev_count = self->device_count;

  SU_ALLOCATE_MANY(
    dev_list,
    dev_count,
    struct suscan_device_properties *);

  for (i = 0; i < dev_count; ++i) {
    /* We only include the devices from the latest discovery */
    dev = self->device_list[i];
    if (dev->epoch + 1 == dev->discovery->epoch)
      SU_TRY(dev_list[n++] = suscan_device_properties_dup(self->device_list[i]));
  }

  dev_count = n;

  *properties = dev_list;
  ok = SU_TRUE;

done:
  pthread_mutex_unlock(&mut->list_mutex);

  if (!ok) {
    for (i = 0; i < dev_count; ++i)
      if (dev_list[i] != NULL)
        SU_DISPOSE(suscan_device_properties, dev_list[i]);
    if (dev_list != NULL)
      free(dev_list);

    return -1;
  }

  return dev_count;
}

SU_METHOD(suscan_device_facade, SUBOOL, discover_all)
{
  unsigned int i;
  SUBOOL ok = SU_TRUE;

  for (i = 0; i < self->thread_count; ++i)
    ok = suscan_device_discovery_thread_discovery(self->thread_list[i]) && ok;

  return ok;
}

SU_GETTER(
  suscan_device_facade,
  suscan_device_discovery_thread_t *, get_thread, const char *name)
{
  unsigned int i;

  for (i = 0; i < self->thread_count; ++i)
    if (strcmp(self->thread_list[i]->iface->name, name) == 0)
      return self->thread_list[i];

  return NULL;
}

SU_METHOD(suscan_device_facade, SUBOOL, start_discovery, const char *name)
{
  suscan_device_discovery_thread_t *thread = 
    suscan_device_facade_get_thread(self, name);

  if (thread == NULL) {
    SU_ERROR("Cannot find discovery thread `%s'\n", name);
    return SU_FALSE;
  }

  return suscan_device_discovery_thread_discovery(thread);
}

SU_METHOD(suscan_device_facade, SUBOOL, stop_discovery, const char *name)
{
  suscan_device_discovery_thread_t *thread = 
    suscan_device_facade_get_thread(self, name);

  if (thread == NULL) {
    SU_ERROR("Cannot find discovery thread `%s'\n", name);
    return SU_FALSE;
  }

  return suscan_device_discovery_thread_cancel(thread);
}

SU_GETTER(
  suscan_device_facade, 
  char *,
  wait_for_devices,
  unsigned timeout_ms)
{
  suscan_device_facade_t *mut = (suscan_device_facade_t *) self;
  int error;
  char *copy = NULL;
  struct timeval tv, inc;
  struct timespec ts;

  pthread_mutex_lock(&mut->disc_lock);

  inc.tv_sec = timeout_ms / 1000;
  inc.tv_usec = (timeout_ms % 1000) * 1000;

  gettimeofday(&tv, NULL);
  timeradd(&tv, &inc, &tv);

  ts.tv_sec  = tv.tv_sec;
  ts.tv_nsec = tv.tv_usec * 1000;

  while (mut->disc_last == NULL) {
    if ((error = pthread_cond_timedwait(&mut->disc_cond, &mut->disc_lock, &ts)) != 0) {
      if (error == ETIMEDOUT)
        break;
    }
  }

  if (mut->disc_last != NULL)
    copy = strdup(mut->disc_last); 
  
  mut->disc_last = NULL;

  pthread_mutex_unlock(&mut->disc_lock);

  return copy;
}
