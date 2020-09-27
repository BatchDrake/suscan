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

struct suscli_analyzer_client {
  int sfd;
  SUBOOL auth;
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

suscli_analyzer_client_t *suscli_analyzer_client_new(int sfd);
SUBOOL suscli_analyzer_client_read(suscli_analyzer_client_t *self);
struct suscan_analyzer_remote_call *suscli_analyzer_client_take_call(
    suscli_analyzer_client_t *);
struct suscan_analyzer_remote_call *suscli_analyzer_client_get_outcoming_call(
    suscli_analyzer_client_t *);
SUBOOL suscli_analyzer_client_deliver_call(suscli_analyzer_client_t *self);
void suscli_analyzer_client_destroy(suscli_analyzer_client_t *self);

struct pollfd;

struct suscli_analyzer_client_list {
  suscli_analyzer_client_t *client_head;
  rbtree_t *client_tree;
  struct pollfd *client_pfds;
  unsigned int client_count;
  pthread_mutex_t client_mutex;
  SUBOOL client_mutex_initialized;
};

SUBOOL suscli_analyzer_client_list_init(struct suscli_analyzer_client_list *);
SUBOOL suscli_analyzer_client_list_append_client(
    struct suscli_analyzer_client_list *self,
    suscli_analyzer_client_t *client);

SUINLINE unsigned int
suscli_analyzer_client_list_get_count(
    const struct suscli_analyzer_client_list *self)
{
  return self->client_count;
}

void suscli_analyzer_client_list_finalize(struct suscli_analyzer_client_list *);


struct suscli_analyzer_server {
  struct suscli_analyzer_client_list client_list;

  suscan_analyzer_t *analyzer;
  suscan_source_config_t *config;

  pthread_t rx_thread; /* Poll on client_pfds */
  pthread_t tx_thread; /* Wait on suscan_mq_read */
};

typedef struct suscli_analyzer_server suscli_analyzer_server_t;

#endif /* _SUSCAN_CLI_ANSERV_H */
