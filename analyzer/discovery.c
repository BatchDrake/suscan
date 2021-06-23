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

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <libgen.h>
#include <pthread.h>

#define SU_LOG_DOMAIN "discovery"

#include "source.h"
#include "discovery.h"

struct suscan_discovered_remote_device {
  const suscan_source_device_t *device;
  suscan_source_config_t *config;
};


struct suscan_device_net_discovery_ctx {
  void *alloc_buffer;
  int fd;
  size_t alloc_size;
};

SUPRIVATE pthread_t       g_discovery_thread;
SUPRIVATE SUBOOL          g_discovery_running = SU_FALSE;
SUPRIVATE pthread_mutex_t g_remote_device_mutex = PTHREAD_MUTEX_INITIALIZER;
PTR_LIST_PRIVATE(struct suscan_discovered_remote_device, g_remote_device);

SUPRIVATE SUBOOL
suscan_source_device_equals(
    const suscan_source_device_t *dev1,
    const suscan_source_device_t *dev2)
{
  unsigned int i;
  const char *val;

  if (dev1->interface != dev2->interface)
    return SU_FALSE;

  for (i = 0; i < dev1->args->size; ++i) {
    val = SoapySDRKwargs_get(dev2->args, dev1->args->keys[i]);

    if (strcmp(val, dev1->args->vals[i]) != 0)
      return SU_FALSE;
  }

  return SU_TRUE;
}

SUPRIVATE struct suscan_discovered_remote_device *
suscan_discovered_remote_device_lookup_ex_unsafe(
    const suscan_source_device_t *self)
{
  unsigned int i;

  for (i = 0; i < g_remote_device_count; ++i)
    if (suscan_source_device_equals(self, g_remote_device_list[i]->device))
      return g_remote_device_list[i];

  return NULL;
}

suscan_source_config_t *
suscan_discovered_remote_device_lookup_unsafe(
    const suscan_source_device_t *self)
{
  struct suscan_discovered_remote_device *remdev;

  if ((remdev = suscan_discovered_remote_device_lookup_ex_unsafe(self)) != NULL)
    return remdev->config;

  return NULL;
}

suscan_source_config_t *
suscan_discovered_remote_device_make_config(const suscan_source_device_t *self)
{
  struct suscan_discovered_remote_device *remdev;
  suscan_source_config_t *config = NULL;
  SUBOOL acquired = SU_FALSE;

  SU_TRYCATCH(
      pthread_mutex_lock(&g_remote_device_mutex) != -1,
      goto done);
  acquired = SU_TRUE;

  if ((remdev = suscan_discovered_remote_device_lookup_ex_unsafe(self))
      != NULL)
    SU_TRYCATCH(
        config = suscan_source_config_clone(remdev->config),
        goto done);

done:
  if (acquired)
    (void) pthread_mutex_unlock(&g_remote_device_mutex);

  return config;
}

SUBOOL
suscan_discovered_remote_device_walk(
    SUBOOL (*function) (
        void *userdata,
        const suscan_source_device_t *device,
        const suscan_source_config_t *config),
    void *userdata)
{
  unsigned int i;
  SUBOOL acquired = SU_FALSE;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      pthread_mutex_lock(&g_remote_device_mutex) != -1,
      goto done);
  acquired = SU_TRUE;

  for (i = 0; i < g_remote_device_count; ++i)
    if (g_remote_device_list[i] != NULL)
      if (!(function) (
          userdata,
          g_remote_device_list[i]->device,
          g_remote_device_list[i]->config))
        goto done;

  ok = SU_TRUE;

done:
  if (acquired)
    (void) pthread_mutex_unlock(&g_remote_device_mutex);

  return ok;
}

SUPRIVATE SUBOOL
suscan_discovered_remote_device_update(suscan_source_config_t *config)
{
  struct suscan_discovered_remote_device *remdev;
  SUBOOL acquired = SU_FALSE;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      pthread_mutex_lock(&g_remote_device_mutex) != -1,
      goto done);
  acquired = SU_TRUE;

  remdev = suscan_discovered_remote_device_lookup_ex_unsafe(
      suscan_source_config_get_device(config));

  if (remdev != NULL) {
    /* A la C++ */
    suscan_source_config_swap(remdev->config, config);
    remdev = NULL;
  } else {
    SU_TRYCATCH(
        remdev = calloc(1, sizeof(struct suscan_discovered_remote_device)),
        goto done);

    remdev->device = suscan_source_config_get_device(config);
    SU_TRYCATCH(
        remdev->config =  suscan_source_config_clone(config),
        goto done);

    SU_TRYCATCH(
        PTR_LIST_APPEND_CHECK(g_remote_device, remdev) != -1,
        goto done);

    remdev = NULL;
  }

  ok = SU_TRUE;

done:
  if (remdev != NULL) {
    if (remdev->config != NULL)
      suscan_source_config_destroy(remdev->config);

    free(remdev);
  }

  if (acquired)
    (void) pthread_mutex_unlock(&g_remote_device_mutex);

  return ok;
}


SUPRIVATE void
suscan_device_net_discovery_ctx_destroy(
    struct suscan_device_net_discovery_ctx *self)
{
  if (self->alloc_buffer != NULL)
    free(self->alloc_buffer);

  if (self->fd != -1)
    close(self->fd);

  free(self);
}

SUPRIVATE struct suscan_device_net_discovery_ctx *
suscan_device_net_discovery_ctx_new(const char *iface, const char *mcaddr)
{
  struct suscan_device_net_discovery_ctx *new = NULL;
  int reuse = 1;
  struct sockaddr_in addr;
  struct ip_mreq     group;

  SU_TRYCATCH(
      new = calloc(1, sizeof(struct suscan_device_net_discovery_ctx)),
      goto fail);

  SU_TRYCATCH((new->fd = socket(AF_INET, SOCK_DGRAM, 0)) != -1, goto fail);

  new->alloc_size = SURPC_DISCOVERY_MAX_PDU_SIZE;

  SU_TRYCATCH(
        new->alloc_buffer = malloc(new->alloc_size),
        goto fail);

  SU_TRYCATCH(
      setsockopt(
          new->fd,
          SOL_SOCKET,
          SO_REUSEADDR,
          (char *) &reuse,
          sizeof(int)) != -1,
      goto fail);

  memset(&addr, 0, sizeof(struct sockaddr_in));

  addr.sin_family = AF_INET;
  addr.sin_port   = htons(SURPC_DISCOVERY_PROTOCOL_PORT);
  addr.sin_addr.s_addr = INADDR_ANY;

  SU_TRYCATCH(
      bind(
          new->fd,
          (struct sockaddr *) &addr,
          sizeof(struct sockaddr)) != -1,
      goto fail);

  group.imr_multiaddr.s_addr = inet_addr(mcaddr);
  group.imr_interface.s_addr = inet_addr(iface);

  if (ntohl(group.imr_interface.s_addr) == 0xffffffff) {
    SU_ERROR(
        "Invalid interface address `%s' (does not look like a valid IP address)\n",
        iface);
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
          "local network interface with IP `%s'\n", iface);
    } else {
      SU_ERROR(
          "failed to set network interface for multicast: %s\n",
          strerror(errno));
    }

    goto fail;
  }

  return new;

fail:
  if (new != NULL)
    suscan_device_net_discovery_ctx_destroy(new);

  return NULL;
}

#if SOAPY_SDR_API_VERSION < 0x00070000
#  define SOAPYSDR_KWARGS_SET(a, k, v)                  \
  (void) SoapySDRKwargs_set((a), (k), (v))
#else
#  define SOAPYSDR_KWARGS_SET(a, k, v)                  \
    SU_TRYCATCH(                                        \
      SoapySDRKwargs_set((a), (k), (v)) == 0,           \
      goto done)
#endif

SUPRIVATE void *
suscan_device_net_discovery_thread(void *data)
{
  ssize_t sz;
  socklen_t len = sizeof(struct sockaddr_in);
  grow_buf_t buf = grow_buf_INITIALIZER;
  suscan_source_config_t *cfg = NULL;
  const suscan_source_device_t *dev = NULL;
  const char *phost;
  const char *pstrport;
  char *name = NULL;
  struct sockaddr_in addr;
  const char *as_ip;

  struct suscan_device_net_discovery_ctx *ctx =
      (struct suscan_device_net_discovery_ctx *) data;

  SU_INFO("Discovery: starting thread, alloc size: %d\n", ctx->alloc_size);

  while ((sz = recvfrom(
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

    SU_TRYCATCH(cfg = suscan_source_config_new_default(), goto done);

    as_ip = inet_ntoa(addr.sin_addr);

    /* New profile! */
    if (suscan_source_config_deserialize_ex(cfg, &buf, as_ip)) {
      dev      = suscan_source_config_get_device(cfg);
      phost    = suscan_source_device_get_param(dev, "host");
      pstrport = suscan_source_device_get_param(dev, "port");

      SU_TRYCATCH(
          name = strbuild(
              "%s (%s:%s)",
              suscan_source_config_get_label(cfg),
              phost,
              pstrport),
          goto done);

      suscan_source_config_set_label(cfg, name);

      SU_TRYCATCH(
          suscan_discovered_remote_device_update(cfg),
          goto done);

      SU_INFO("%d profiles\n", g_remote_device_count);

      free(name);
      name = NULL;
    }

    suscan_source_config_destroy(cfg);
    cfg = NULL;
  }

  SU_WARNING("Discovery: socket vanished, stopping thread.\n");

done:
  if (cfg != NULL)
    suscan_source_config_destroy(cfg);

  if (name != NULL)
    free(name);

  suscan_device_net_discovery_ctx_destroy(ctx);

  return NULL;
}

SUBOOL
suscan_device_net_discovery_start(const char *iface)
{
  struct suscan_device_net_discovery_ctx *ctx = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      ctx = suscan_device_net_discovery_ctx_new(
          iface,
          SURPC_DISCOVERY_MULTICAST_ADDR),
      return SU_FALSE);

  SU_TRYCATCH(
      pthread_create(
          &g_discovery_thread,
          NULL,
          suscan_device_net_discovery_thread,
          ctx) != -1,
      goto done);

  g_discovery_running = SU_TRUE;

  ok = SU_TRUE;

done:
  if (!ok && ctx != NULL)
    suscan_device_net_discovery_ctx_destroy(ctx);

  return ok;
}
