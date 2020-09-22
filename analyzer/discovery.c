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

SUPRIVATE pthread_t g_discovery_thread;
SUPRIVATE SUBOOL    g_discovery_running = SU_FALSE;

struct suscan_device_net_discovery_ctx {
  union {
    void *alloc_buffer;
    struct suscan_device_net_discovery_pdu *pdu;
  };

  int fd;
  size_t alloc_size;
};

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

  SU_TRYCATCH(
      setsockopt(
          new->fd,
          IPPROTO_IP,
          IP_ADD_MEMBERSHIP,
          (char *) &group,
          sizeof(int)) != -1,
      goto fail);
  return new;

fail:
  if (new != NULL)
    suscan_device_net_discovery_ctx_destroy(new);

  return NULL;
}

SUPRIVATE void *
suscan_device_net_discovery_thread(void *data)
{
  ssize_t sz;
  socklen_t len = sizeof(struct sockaddr_in);
  struct sockaddr_in addr;
  const char *as_ip;

  struct suscan_device_net_discovery_ctx *ctx =
      (struct suscan_device_net_discovery_ctx *) data;

  printf("Entering discovery thread\n");
  while ((sz = recvfrom(
      ctx->fd,
      ctx->pdu,
      ctx->alloc_size,
      0,
      (struct sockaddr *) &addr,
      &len)) > 0) {
    if (sz > 2) {
      as_ip = inet_ntoa(addr.sin_addr);
      printf("%s:%d: %s\n", as_ip, ntohs(ctx->pdu->port), ctx->pdu->name);
    }
  }

  printf("Loop broken\n");

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
