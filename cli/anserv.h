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

#ifndef _SUSCAN_CLI_ANSERV_H
#define _SUSCAN_CLI_ANSERV_H

#include <analyzer/impl/remote.h>
#include <util/rbtree.h>
#include <arpa/inet.h>

#define SUSCLI_ANSERV_LISTEN_FD 0
#define SUSCLI_ANSERV_CANCEL_FD 1
#define SUSCLI_ANSERV_FD_OFFSET 2

struct suscli_analyzer_client {
  int sfd;
  SUBOOL auth;
  SUBOOL has_source_info;
  SUBOOL failed;
  struct timeval conntime;
  struct in_addr remote_addr;

  SUBOOL have_header;
  SUBOOL have_body;
  uint8_t header_ptr;

  grow_buf_t incoming_pdu;
  struct suscan_analyzer_remote_call incoming_call;

  grow_buf_t outcoming_pdu;
  struct suscan_analyzer_remote_call outcoming_call;

  union {
    struct suscan_analyzer_remote_pdu_header header;
    uint8_t header_bytes[0];
  };

  uint8_t read_buffer[SUSCAN_REMOTE_READ_BUFFER];

  struct suscli_analyzer_client *next;
  struct suscli_analyzer_client *prev;
};

typedef struct suscli_analyzer_client suscli_analyzer_client_t;

SUINLINE SUBOOL
suscli_analyzer_client_is_failed(const suscli_analyzer_client_t *self)
{
  return self->failed;
}

SUINLINE SUBOOL
suscli_analyzer_client_is_auth(const suscli_analyzer_client_t *self)
{
  return self->auth;
}

SUINLINE SUBOOL
suscli_analyzer_client_has_source_info(const suscli_analyzer_client_t *self)
{
  return self->has_source_info;
}


SUINLINE const char *
suscli_analyzer_client_string_addr(const suscli_analyzer_client_t *self)
{
  return inet_ntoa(self->remote_addr);
}

SUINLINE void
suscli_analyzer_client_set_auth(suscli_analyzer_client_t *self, SUBOOL auth)
{
  self->auth = auth;
}

SUINLINE void
suscli_analyzer_client_set_has_source_info(
    suscli_analyzer_client_t *self,
    SUBOOL has_info)
{
  self->has_source_info = has_info;
}

SUINLINE void
suscli_analyzer_client_mark_failed(suscli_analyzer_client_t *self)
{
  self->failed = SU_TRUE;
}


suscli_analyzer_client_t *suscli_analyzer_client_new(int sfd);
SUBOOL suscli_analyzer_client_read(suscli_analyzer_client_t *self);
struct suscan_analyzer_remote_call *suscli_analyzer_client_take_call(
    suscli_analyzer_client_t *);
struct suscan_analyzer_remote_call *suscli_analyzer_client_get_outcoming_call(
    suscli_analyzer_client_t *);
void suscli_analyzer_client_return_outcoming_call(
    suscli_analyzer_client_t *self,
    struct suscan_analyzer_remote_call *call);

SUBOOL suscli_analyzer_client_shutdown(suscli_analyzer_client_t *self);
SUBOOL suscli_analyzer_client_deliver_call(suscli_analyzer_client_t *self);
SUBOOL suscli_analyzer_client_write_buffer(
    suscli_analyzer_client_t *self,
    const grow_buf_t *buffer);

void suscli_analyzer_client_destroy(suscli_analyzer_client_t *self);

struct pollfd;

struct suscli_analyzer_client_list {
  suscli_analyzer_client_t *client_head;
  int cancel_fd;
  int listen_fd;
  rbtree_t *client_tree;
  struct pollfd *client_pfds;
  unsigned int client_pfds_alloc;
  unsigned int client_count;
  SUBOOL cleanup_requested;
  pthread_mutex_t client_mutex;
  SUBOOL client_mutex_initialized;
};

SUBOOL suscli_analyzer_client_list_init(
    struct suscli_analyzer_client_list *,
    int listen_fd,
    int cancel_fd);
SUBOOL suscli_analyzer_client_list_append_client(
    struct suscli_analyzer_client_list *self,
    suscli_analyzer_client_t *client);

suscli_analyzer_client_t *suscli_analyzer_client_list_lookup(
    const struct suscli_analyzer_client_list *self,
    int fd);

SUBOOL suscli_analyzer_client_list_remove_unsafe(
    struct suscli_analyzer_client_list *self,
    suscli_analyzer_client_t *client);

SUBOOL suscli_analyzer_client_list_attempt_cleanup(
    struct suscli_analyzer_client_list *self);

SUINLINE unsigned int
suscli_analyzer_client_list_get_count(
    const struct suscli_analyzer_client_list *self)
{
  return self->client_count;
}

void suscli_analyzer_client_list_finalize(struct suscli_analyzer_client_list *);


struct suscli_analyzer_server {
  struct suscli_analyzer_client_list client_list;

  uint16_t listen_port;

  suscan_analyzer_t *analyzer;
  suscan_source_config_t *config;
  struct suscan_mq mq;

  pthread_t rx_thread; /* Poll on client_pfds */
  pthread_t tx_thread; /* Wait on suscan_mq_read */
  int cancel_pipefd[2];
  grow_buf_t broadcast_pdu;
  struct suscan_analyzer_remote_call broadcast_call;

  SUBOOL rx_thread_running;
  SUBOOL tx_thread_running;
  SUBOOL tx_halted;
};

typedef struct suscli_analyzer_server suscli_analyzer_server_t;

SUINLINE suscan_source_config_t *
suscli_analyzer_server_get_profile(const suscli_analyzer_server_t *self)
{
  return self->config;
}

SUINLINE uint16_t
suscli_analyzer_server_get_port(const suscli_analyzer_server_t *self)
{
  return self->listen_port;
}

suscli_analyzer_server_t *
suscli_analyzer_server_new(suscan_source_config_t *profile, uint16_t port);

SUINLINE SUBOOL
suscli_analyzer_server_is_running(suscli_analyzer_server_t *self)
{
  return self->rx_thread_running;
}

void suscli_analyzer_server_destroy(suscli_analyzer_server_t *self);

#endif /* _SUSCAN_CLI_ANSERV_H */
