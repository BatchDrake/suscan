/*

  Copyright (C) 2020 Gonzalo José Carracedo Carballal

  This program is free software: you can redistribute it and/or modify
  it under the teradio of the GNU Lesser General Public License as
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

#define SU_LOG_DOMAIN "cli-devserv"

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sigutils/log.h>
#include <analyzer/analyzer.h>
#include <analyzer/discovery.h>
#include <string.h>
#include <pthread.h>

#include <cli/anserv.h>
#include <cli/cli.h>
#include <cli/cmds.h>

#define SUSCLI_DEVSERV_DEFAULT_PORT_BASE 28000

struct suscli_devserv_ctx {
  int fd;
  SUBOOL halting;
  uint16_t port_base;
  struct sockaddr_in mc_addr;

  union {
    struct suscan_device_net_discovery_pdu *pdu;
    void *alloc_buf;
  };

  PTR_LIST(suscli_analyzer_server_t, server);
  size_t alloc_size;
  size_t pdu_size;
};

SUPRIVATE void
suscli_devserv_ctx_destroy(
    struct suscli_devserv_ctx *self)
{
  unsigned int i;

  for (i = 0; i < self->server_count; ++i)
    suscli_analyzer_server_destroy(self->server_list[i]);

  if (self->server_list != NULL)
    free(self->server_list);

  if (self->fd != -1)
    close(self->fd);

  if (self->alloc_buf != NULL)
    free(self->alloc_buf);

  free(self);
}

struct suscan_device_net_discovery_pdu *
suscli_devserv_ctx_alloc_pdu(struct suscli_devserv_ctx *self, size_t size)
{
  void *tmp;

  if (size > self->alloc_size) {
    SU_TRYCATCH(tmp = realloc(self->alloc_buf, size), return SU_FALSE);
    self->alloc_buf = tmp;
    self->alloc_size = size;
  }

  self->pdu_size = size;

  return self->alloc_buf;
}

SUPRIVATE struct suscli_devserv_ctx *
suscli_devserv_ctx_new(const char *iface, const char *mcaddr)
{
  struct suscli_devserv_ctx *new = NULL;
  suscan_source_config_t *cfg;
  suscli_analyzer_server_t *server = NULL;
  int i;
  char loopch = 0;
  struct in_addr mc_if;

  SU_TRYCATCH(
      new = calloc(1, sizeof(struct suscli_devserv_ctx)),
      goto fail);

  SU_TRYCATCH((new->fd = socket(AF_INET, SOCK_DGRAM, 0)) != -1, goto fail);

  new->port_base = SUSCLI_DEVSERV_DEFAULT_PORT_BASE;

  SU_TRYCATCH(
      setsockopt(
          new->fd,
          IPPROTO_IP,
          IP_MULTICAST_LOOP,
          (char *) &loopch,
          sizeof(loopch)) != -1,
      goto fail);

  mc_if.s_addr = inet_addr(iface);
  if (setsockopt(
          new->fd,
          IPPROTO_IP,
          IP_MULTICAST_IF,
          (char *) &mc_if,
          sizeof (struct in_addr)) == -1) {
    SU_ERROR(
        "failed to set network interface for multicast: %s\n",
        strerror(errno));
    goto fail;
  }


  memset(&new->mc_addr, 0, sizeof(struct sockaddr_in));
  new->mc_addr.sin_family = AF_INET;
  new->mc_addr.sin_addr.s_addr = inet_addr(mcaddr);
  new->mc_addr.sin_port = htons(SURPC_DISCOVERY_PROTOCOL_PORT);

  /* Populate servers */

  for (i = 1; i <= suscli_get_source_count(); ++i) {
    cfg = suscli_get_source(i);

    if (cfg != NULL) {
      SU_TRYCATCH(
          server = suscli_analyzer_server_new(cfg, new->port_base + i),
          goto fail);

      SU_TRYCATCH(PTR_LIST_APPEND_CHECK(new->server, server) != -1, goto fail);
      server = NULL;
    }
  }

  return new;

fail:
  if (server != NULL)
    suscli_analyzer_server_destroy(server);

  if (new != NULL)
    suscli_devserv_ctx_destroy(new);

  return NULL;
}

SUPRIVATE void *
suscli_devserv_announce_thread(void *ptr)
{
  unsigned int i;
  struct suscli_devserv_ctx *ctx = (struct suscli_devserv_ctx *) ptr;
  struct suscan_device_net_discovery_pdu *pdu;
  suscan_source_config_t *cfg;
  size_t len;

  while (!ctx->halting) {
    for (i = 0; i < ctx->server_count; ++i) {
      cfg = suscli_analyzer_server_get_profile(ctx->server_list[i]);
      if (cfg != NULL) {
        len =
            sizeof (struct suscan_device_net_discovery_pdu)
            + strlen(suscan_source_config_get_label(cfg))
            + 1;

        if ((pdu = suscli_devserv_ctx_alloc_pdu(ctx, len)) != NULL) {
          pdu->port = htons(
              suscli_analyzer_server_get_port(ctx->server_list[i]));
          memcpy(
              pdu->name,
              suscan_source_config_get_label(cfg),
              strlen(suscan_source_config_get_label(cfg)) + 1);

          if (sendto(
              ctx->fd,
              pdu,
              len,
              0,
              (struct sockaddr *) &ctx->mc_addr,
              sizeof (struct sockaddr_in)) != len) {
            SU_ERROR("sendto() failed: %s\n", strerror(errno));
          }
        }
      }
    }

    sleep(1);
  }

  return NULL;
}

SUBOOL
suscli_devserv_cb(const hashlist_t *params)
{
  struct suscli_devserv_ctx *ctx = NULL;
  const char *iface, *mc;
  pthread_t thread;
  SUBOOL thread_running = SU_FALSE;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      suscli_param_read_string(params, "if", &iface, NULL),
      goto done);

  if (iface == NULL) {
    fprintf(
        stderr,
        "devserv: need to specify a multicast interface address with if=\n");
    goto done;
  }

  SU_TRYCATCH(
      suscli_param_read_string(
          params,
          "group",
          &mc,
          SURPC_DISCOVERY_MULTICAST_ADDR),
      goto done);

  SU_TRYCATCH(ctx = suscli_devserv_ctx_new(iface, mc), goto done);

  SU_TRYCATCH(
      pthread_create(&thread, NULL, suscli_devserv_announce_thread, ctx) != -1,
      goto done);
  thread_running = SU_TRUE;

  printf("Announcing...\n");

  for (;;)
    sleep(1);

  ok = SU_TRUE;

done:
  if (thread_running) {
    ctx->halting = SU_TRUE;
    pthread_join(thread, NULL);
  }

  if (ctx != NULL)
    suscli_devserv_ctx_destroy(ctx);

  return ok;
}
