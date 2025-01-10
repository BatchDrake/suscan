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

#define SU_LOG_DOMAIN "multicast-discovery"

#define _COMPAT_BARRIERS
#include <compat.h>

#include <sigutils/util/compat-socket.h>
#include <sigutils/util/compat-inet.h>
#include <sigutils/util/compat-in.h>
#include <util/compat.h>

#include <analyzer/source/config.h>
#include <analyzer/device/spec.h>
#include <analyzer/device/properties.h>
#include <analyzer/device/discovery.h>

#include <analyzer/analyzer.h>
#include <analyzer/source.h>

#include <sigutils/log.h>

#include "multicast.h"

SUPRIVATE const char *g_mc_if = NULL;

struct multicast_discovery_ctx {
  pthread_t       thread;
  SUBOOL          closed;
  SUBOOL          cancelled;
  pthread_mutex_t mutex;
  SUBOOL          mutex_alloc;
  SUBOOL          thread_running;

  void *alloc_buffer;
  int fd;
  size_t alloc_size;

  /* Devices discovered under this list */
  PTR_LIST(suscan_device_properties_t, property);
  rbtree_t *uuid2property;
};

SUPRIVATE SUBOOL
multicast_discovery_ctx_upsert_device(
  struct multicast_discovery_ctx *self,
  const suscan_source_config_t *cfg)
{
  suscan_device_properties_t *prop = NULL;
  struct rbtree_node *node;
  uint64_t uuid;
  const char *phost;
  const char *pstrport;
  char *source_string = NULL;
  SUBOOL acquired = SU_FALSE;
  SUBOOL ok = SU_FALSE;

  SU_MAKE(prop, suscan_device_properties, suscan_source_config_get_label(cfg));

  SU_TRYZ(pthread_mutex_lock(&self->mutex));
  acquired = SU_TRUE;

  phost    = suscan_source_config_get_param(cfg, "host");
  pstrport = suscan_source_config_get_param(cfg, "port");

  SU_TRY(source_string = strbuild("%s:%s", phost, pstrport));
  
  SU_TRY(suscan_device_properties_set_analyzer(prop, "remote"));
  SU_TRY(suscan_device_properties_set_source(prop, source_string));

  suscan_device_properties_update_uuid(prop);
  uuid = suscan_device_properties_uuid(prop);

  /* Seen before? Go ahead and find it */
  node = rbtree_search(self->uuid2property, uuid, RB_EXACT);

  if (node == NULL || node->data == NULL) {
    SU_TRYC(PTR_LIST_APPEND_CHECK(self->property, prop));
    rbtree_set(self->uuid2property, uuid, prop);

#if 0
    char *uri = suscan_device_make_uri(
        prop->analyzer->name,
        prop->source->name,
        &prop->traits);
    
    SU_INFO("New multicast device: %s\n", uri);
    free(uri);
#endif

    prop = NULL;
  }

  ok = SU_TRUE;

done:
  if (acquired)
    (void) pthread_mutex_unlock(&self->mutex);

  if (source_string != NULL)
    free(source_string);
  
  if (prop != NULL)
    SU_DISPOSE(suscan_device_properties, prop);
  
  return ok;
}

SUPRIVATE void *
multicast_discovery_thread(void *data)
{
  ssize_t sz;
  socklen_t len = sizeof(struct sockaddr_in);
  grow_buf_t buf = grow_buf_INITIALIZER;
  suscan_source_config_t *cfg = NULL;
  struct sockaddr_in addr;
  const char *as_ip;

  struct multicast_discovery_ctx *ctx = data;

  SU_INFO("Multicast discovery: starting thread, alloc size: %d\n", ctx->alloc_size);

  while (!ctx->closed && (sz = recvfrom(
      ctx->fd,
      ctx->alloc_buffer,
      ctx->alloc_size,
      0,
      (struct sockaddr *) &addr,
      &len)) > 0) {

    grow_buf_init_loan(
        &buf,
        ctx->alloc_buffer,
        sz,
        ctx->alloc_size);

    SU_TRY(cfg = suscan_source_config_new_default());

    as_ip = inet_ntoa(addr.sin_addr);

    /* New profile! */
    if (suscan_source_config_deserialize_ex(cfg, &buf, as_ip))
      multicast_discovery_ctx_upsert_device(ctx, cfg);

    suscan_source_config_destroy(cfg);
    cfg = NULL;
  }

  SU_WARNING("Discovery: socket vanished, stopping thread.\n");

done:
  if (cfg != NULL)
    suscan_source_config_destroy(cfg);

  return NULL;
}

SUPRIVATE SUBOOL multicast_discovery_close(void *userdata);

SUPRIVATE void *
multicast_discovery_open()
{
  struct multicast_discovery_ctx *new = NULL;
  int reuse = 1;
  const char *mcaddr = SURPC_DISCOVERY_MULTICAST_ADDR;
  struct sockaddr_in addr;
  struct ip_mreq     group;

  SU_ALLOCATE_FAIL(new, struct multicast_discovery_ctx);

  SU_MAKE_FAIL(new->uuid2property, rbtree);

  SU_TRYC_FAIL(pthread_mutex_init(&new->mutex, NULL));
  new->mutex_alloc = SU_TRUE;

  SU_TRYC_FAIL(new->fd = socket(AF_INET, SOCK_DGRAM, 0));

  new->alloc_size = SURPC_DISCOVERY_MAX_PDU_SIZE;

  SU_ALLOCATE_MANY_FAIL(new->alloc_buffer, new->alloc_size, uint8_t);

  SU_TRYC_FAIL(setsockopt(new->fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int)));

  memset(&addr, 0, sizeof(struct sockaddr_in));

  addr.sin_family = AF_INET;
  addr.sin_port   = htons(SURPC_DISCOVERY_PROTOCOL_PORT);
  addr.sin_addr.s_addr = INADDR_ANY;

  SU_TRYC_FAIL(bind(new->fd, (struct sockaddr *) &addr, sizeof(struct sockaddr)));

  group.imr_multiaddr.s_addr = inet_addr(mcaddr);
  group.imr_interface.s_addr = suscan_ifdesc_to_addr(g_mc_if);

  if (ntohl(group.imr_interface.s_addr) == 0xffffffff) {
    SU_ERROR(
        "Invalid interface address `%s' (does not look like a valid IP address)\n",
        g_mc_if);
    goto fail;
  }

  if ((ntohl(group.imr_interface.s_addr) & 0xf0000000) == 0xe0000000) {
    SU_ERROR("Invalid interface address. Please note that SUSCAN_DISCOVERY_IF "
        "expects the IP address of a configured local network interface, not a "
        "multicast group.\n");

    goto fail;
  }

  if (setsockopt(
          new->fd,
          IPPROTO_IP,
          IP_ADD_MEMBERSHIP,
          (char *) &group,
          sizeof(struct ip_mreq)) == -1) {
    if (errno == ENODEV) {
      SU_ERROR("Invalid interface address. Please verify that there is a "
          "local network interface with IP `%s'\n", g_mc_if);
    } else {
      SU_ERROR(
          "failed to set network interface for multicast: %s\n",
          strerror(errno));
    }

    goto fail;
  }

  SU_TRYZ_FAIL(
    pthread_create(&new->thread, NULL, multicast_discovery_thread, new));
  new->thread_running = SU_TRUE;

  return new;

fail:
  if (new != NULL)
    multicast_discovery_close(new);

  return NULL;
}

SUPRIVATE SUBOOL
multicast_discovery_discovery(void *userdata, suscan_device_discovery_t *disc)
{
  struct multicast_discovery_ctx *self = userdata;
  suscan_device_properties_t *prop = NULL;
  SUBOOL ok = SU_FALSE;
  unsigned int i;
  SUBOOL acquired = SU_FALSE;

  self->cancelled = SU_FALSE;

  sleep(2);

  SU_TRYZ(pthread_mutex_lock(&self->mutex));
  acquired = SU_TRUE;

  for (i = 0; i < self->property_count && !self->cancelled; ++i) {
    SU_TRY(prop = suscan_device_properties_dup(self->property_list[i]));
    SU_TRY(suscan_device_discovery_push_device(disc, prop));
    prop = NULL;
  }

  ok = SU_TRUE;

done:
  if (acquired)
    (void) pthread_mutex_unlock(&self->mutex);
  
  if (prop != NULL)
    SU_DISPOSE(suscan_device_properties, prop);
  
  return ok;
}

SUPRIVATE SUBOOL
multicast_discovery_cancel(void *userdata)
{
  struct multicast_discovery_ctx *self = userdata;

  self->cancelled = SU_TRUE;

  return SU_TRUE;
}

SUPRIVATE SUBOOL
multicast_discovery_close(void *userdata)
{
  struct multicast_discovery_ctx *self = userdata;
  unsigned int i;

  self->closed = SU_TRUE;

  if (self->thread_running)
    pthread_join(self->thread, NULL);

  if (self->mutex_alloc)
    pthread_mutex_destroy(&self->mutex);
  
  for (i = 0; i < self->property_count; ++i)
    if (self->property_list[i] != NULL)
      SU_DISPOSE(suscan_device_properties, self->property_list[i]);
  if (self->property_list != NULL)
    free(self->property_list);

  if (self->alloc_buffer != NULL)
    free(self->alloc_buffer);
  
  if (self->uuid2property != NULL)
    SU_DISPOSE(rbtree, self->uuid2property);
  
  free(self);

  return SU_TRUE;
}

SUPRIVATE struct suscan_device_discovery_interface g_multicast_discovery = 
{
  .name      = "multicast",
  .open      = multicast_discovery_open,
  .discovery = multicast_discovery_discovery,
  .cancel    = multicast_discovery_cancel,
  .close     = multicast_discovery_close
};

SUBOOL
suscan_discovery_register_multicast()
{
  if ((g_mc_if = getenv("SUSCAN_DISCOVERY_IF")) != NULL && strlen(g_mc_if) > 0) {
    SU_INFO("Network discovery explicitly enabled.\n");
    return suscan_device_discovery_register(&g_multicast_discovery);
  }

  return SU_TRUE;
}
