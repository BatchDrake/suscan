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

#define SU_LOG_DOMAIN "analyzer-server"

#include "anserv.h"
#include <sigutils/log.h>
#include <sys/poll.h>

/************************** Analyzer Client API *******************************/
suscli_analyzer_client_t *
suscli_analyzer_client_new(int sfd)
{
  struct sockaddr_in sin;
  socklen_t len = sizeof(struct sockaddr_in);
  suscli_analyzer_client_t *new = NULL;

  SU_TRYCATCH(new = calloc(1, sizeof(suscli_analyzer_client_t)), goto fail);

  new->sfd = -1;

  gettimeofday(&new->conntime, NULL);

  SU_TRYCATCH(
      getsockname(sfd, (struct sockaddr *) &sin, &len) != -1,
      goto fail);

  new->remote_addr = sin.sin_addr;

  return new;

fail:
  if (new != NULL)
    suscli_analyzer_client_destroy(new);

  return NULL;
}

SUBOOL
suscli_analyzer_client_read(suscli_analyzer_client_t *self)
{
  size_t chunksize;
  size_t ret;

  SUBOOL ok = SU_FALSE;

  if (!self->have_header) {
    chunksize =
        sizeof(struct suscan_analyzer_remote_pdu_header) - self->header_ptr;

    if ((ret = read(
        self->sfd,
        self->header_bytes + self->header_ptr,
        chunksize))
        < 1) {
      SU_ERROR("Failed to read from socket: %s\n", strerror(errno));
      goto done;
    }

    self->header_ptr += ret;

    if (self->header_ptr == sizeof(struct suscan_analyzer_remote_pdu_header)) {
      /* Full header received */
      self->header.magic = ntohl(self->header.magic);
      self->header.size  = ntohl(self->header.size);
      self->header_ptr   = 0;

      if (self->header.magic != SUSCAN_REMOTE_PDU_HEADER_MAGIC) {
        SU_ERROR("Protocol error: invalid remote PDU header magic\n");
        goto done;
      }

      self->have_header = self->header.size != 0;

      grow_buf_clear(&self->incoming_pdu);
    }
  } else if (!self->have_body) {
    if ((chunksize = self->header.size) > SUSCAN_REMOTE_READ_BUFFER)
      chunksize = SUSCAN_REMOTE_READ_BUFFER;

    if ((ret = read(self->sfd, self->read_buffer, chunksize)) < 1) {
      SU_ERROR("Failed to read from socket: %s\n", strerror(errno));
      goto done;
    }

    SU_TRYCATCH(
        grow_buf_append(&self->incoming_pdu, self->read_buffer, ret) != -1,
        goto done);

    self->header.size -= chunksize;

    if (self->header.size == 0) {
      self->have_body = SU_TRUE;
    }
  } else {
    SU_ERROR("BUG: Current PDU not consumed yet\n");
    goto done;
  }

  ok = SU_TRUE;

done:
  return ok;
}

struct suscan_analyzer_remote_call *
suscli_analyzer_client_take_call(suscli_analyzer_client_t *self)
{
  struct suscan_analyzer_remote_call *call = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(self->have_header && self->have_body, goto done);

  call = &self->incoming_call;

  suscan_analyzer_remote_call_finalize(call);
  suscan_analyzer_remote_call_init(call, SUSCAN_ANALYZER_REMOTE_NONE);

  if (!suscan_analyzer_remote_call_deserialize(call, &self->incoming_pdu)) {
    SU_ERROR("Protocol error: failed to deserialize remote call\n");
    goto done;
  }

  ok = SU_TRUE;

done:
  if (!ok)
    call = NULL;

  return call;
}

struct suscan_analyzer_remote_call *
suscli_analyzer_client_get_outcoming_call(
    suscli_analyzer_client_t *self)
{
  return &self->outcoming_call;
}

SUBOOL
suscli_analyzer_client_deliver_call(suscli_analyzer_client_t *self)
{
  struct suscan_analyzer_remote_pdu_header header;
  const uint8_t *data;
  size_t size, chunksize;
  SUBOOL ok = SU_FALSE;

  grow_buf_clear(&self->outcoming_pdu);

  SU_TRYCATCH(
      suscan_analyzer_remote_call_serialize(
          &self->outcoming_call,
          &self->outcoming_pdu),
      goto done);

  data = grow_buf_get_buffer(&self->outcoming_pdu);
  size = grow_buf_get_size(&self->outcoming_pdu);

  header.magic = htonl(SUSCAN_REMOTE_PDU_HEADER_MAGIC);
  header.size  = htonl(size);

  chunksize = sizeof(struct suscan_analyzer_remote_pdu_header);

  SU_TRYCATCH(write(self->sfd, &header, chunksize) == chunksize, goto done);

  /* Calls can be extremely big, so we better send them in small chunks */
  while (size > 0) {
    chunksize = size;
    if (chunksize > SUSCAN_REMOTE_READ_BUFFER)
      chunksize = SUSCAN_REMOTE_READ_BUFFER;

    SU_TRYCATCH(write(self->sfd, data, chunksize) == chunksize, goto done);

    data += chunksize;
    size -= chunksize;
  }

  ok = SU_TRUE;

done:
  return ok;
}

void
suscli_analyzer_client_destroy(suscli_analyzer_client_t *self)
{
  close(self->sfd);

  grow_buf_finalize(&self->incoming_pdu);
  grow_buf_finalize(&self->outcoming_pdu);

  suscan_analyzer_remote_call_finalize(&self->incoming_call);
  suscan_analyzer_remote_call_finalize(&self->outcoming_call);

  free(self);
}

/**************************** Client list API ********************************/
SUPRIVATE SUBOOL
suscli_analyzer_client_list_update_pollfds_unsafe(
    struct suscli_analyzer_client_list *self)
{
  unsigned int i, count = self->client_count;
  suscli_analyzer_client_t *client;
  struct pollfd *pollfds = NULL;
  struct rbtree_node *this;
  SUBOOL ok = SU_FALSE;

  this = rbtree_get_first(self->client_tree);

  if (count + 1 > self->client_pfds_alloc) {
    SU_TRYCATCH(
        pollfds = realloc(self->client_pfds, (count + 1) * sizeof(struct pollfd)),
        goto done);
    self->client_pfds = pollfds;
    self->client_pfds_alloc = count + 1;
  } else {
    pollfds = self->client_pfds;
  }

  /* We always have one socket to poll from: the listen socket */
  pollfds[0].fd      = self->listen_fd;
  pollfds[0].events  = POLLIN;
  pollfds[0].revents = 0;

  while (this != NULL) {
    if (this->data != NULL) {
      SU_TRYCATCH(i < count, goto done);
      client = this->data;

      ++i;

      pollfds[i].fd      = client->sfd;
      pollfds[i].events  = POLLIN;
      pollfds[i].revents = 0;
    }

    this = this->next;
  }

  ok = SU_TRUE;

done:
  return ok;
}

SUPRIVATE SUBOOL
suscli_analyzer_client_list_cleanup_unsafe(
    struct suscli_analyzer_client_list *self)
{
  suscli_analyzer_client_t *client;
  struct rbtree_node *this;
  SUBOOL changed = SU_FALSE;

  this = rbtree_get_first(self->client_tree);

  while (this != NULL) {
    if (this->data != NULL) {
      client = this->data;
      if (suscli_analyzer_client_is_failed(client)) {
        suscli_analyzer_client_list_remove_unsafe(self, client);
        suscli_analyzer_client_destroy(client);
        changed = SU_TRUE;
      }
    }
    this = this->next;
  }

  return changed;
}

SUBOOL
suscli_analyzer_client_list_attempt_cleanup(
    struct suscli_analyzer_client_list *self)
{
  SUBOOL mutex_acquired = SU_FALSE;
  SUBOOL ok = SU_FALSE;

  if (pthread_mutex_trylock(&self->client_mutex) == 0) {
    mutex_acquired = SU_TRUE;

    if (suscli_analyzer_client_list_cleanup_unsafe(self))
      SU_TRYCATCH(
          suscli_analyzer_client_list_update_pollfds_unsafe(self),
          goto done);
  }

  ok = SU_TRUE;

done:
  if (mutex_acquired)
    (void) pthread_mutex_unlock(&self->client_mutex);

  return ok;
}

SUBOOL
suscli_analyzer_client_list_init(
    struct suscli_analyzer_client_list *self,
    int listen_fd)
{
  SUBOOL ok = SU_FALSE;

  memset(self, 0, sizeof(struct suscli_analyzer_client_list));

  SU_TRYCATCH(self->client_tree = rbtree_new(), goto done);

  SU_TRYCATCH(pthread_mutex_init(&self->client_mutex, NULL), goto done);
  self->client_mutex_initialized = SU_TRUE;

  self->listen_fd = listen_fd;

  SU_TRYCATCH(
      suscli_analyzer_client_list_update_pollfds_unsafe(self),
      goto done);

  ok = SU_TRUE;

done:
  if (!ok)
    suscli_analyzer_client_list_finalize(self);

  return ok;
}

SUBOOL
suscli_analyzer_client_list_append_client(
    struct suscli_analyzer_client_list *self,
    suscli_analyzer_client_t *client)
{
  struct rbtree_node *node;
  SUBOOL mutex_acquired = SU_FALSE;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(pthread_mutex_lock(&self->client_mutex) != -1, goto done);
  mutex_acquired = SU_TRUE;

  node = rbtree_search(self->client_tree, client->sfd, RB_EXACT);

  SU_TRYCATCH(node == NULL || node->data == NULL, goto done);

  if (node != NULL) {
    node->data = client;
  } else {
    SU_TRYCATCH(
        rbtree_insert(self->client_tree, client->sfd, client) != -1,
        goto done);
  }

  client->next = self->client_head;
  client->prev = NULL;

  if (self->client_head != NULL)
    self->client_head->prev = client;

  self->client_head = client;

  ++self->client_count;

  if (self->cleanup_requested) {
    self->cleanup_requested = SU_FALSE;
    (void) suscli_analyzer_client_list_cleanup_unsafe(self);
  }

  SU_TRYCATCH(
      suscli_analyzer_client_list_update_pollfds_unsafe(self),
      goto done);

  ok = SU_TRUE;

done:
  if (mutex_acquired)
    (void) pthread_mutex_unlock(&self->client_mutex);

  return ok;
}

suscli_analyzer_client_t *
suscli_analyzer_client_list_lookup(
    const struct suscli_analyzer_client_list *self,
    int fd)
{
  struct rbtree_node *node;

  if ((node = rbtree_search(self->client_tree, fd, RB_EXACT)) == NULL)
    return NULL;

  return node->data;
}

SUBOOL
suscli_analyzer_client_list_remove_unsafe(
    struct suscli_analyzer_client_list *self,
    suscli_analyzer_client_t *client)
{
  suscli_analyzer_client_t *prev, *next;
  struct rbtree_node *node;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(pthread_mutex_lock(&self->client_mutex) != -1, goto done);

  prev = client->prev;
  next = client->next;

  SU_TRYCATCH(
      node = rbtree_search(self->client_tree, client->sfd, RB_EXACT),
      goto done);

  /* This redundancy is intentional */
  SU_TRYCATCH(node->data != NULL, goto done);
  SU_TRYCATCH(node->data == client, goto done);

  /* Set it to NULL. This marks an empty place. */
  node->data = NULL;

  if (prev != NULL)
    prev->next = next;
  else
    self->client_head = next;

  if (next != NULL)
    next->prev = prev;

  client->prev = client->next = NULL;

  --self->client_count;

  ok = SU_TRUE;

done:
  return ok;
}

void
suscli_analyzer_client_list_finalize(struct suscli_analyzer_client_list *self)
{
  suscli_analyzer_client_t *this, *next;

  if (self->client_mutex_initialized)
    pthread_mutex_destroy(&self->client_mutex);

  this = self->client_head;

  while (this != NULL) {
    next = this->next;
    suscli_analyzer_client_destroy(this);
    this = next;
  }

  if (self->client_tree != NULL)
    rbtree_destroy(self->client_tree);

  if (self->client_pfds != NULL)
    free(self->client_pfds);
}

/***************************** RX Thread **************************************/
SUPRIVATE SUBOOL
suscli_analyzer_server_process_auth_message(
    suscli_analyzer_server_t *self,
    suscli_analyzer_client_t *client,
    const struct suscan_analyzer_remote_call *call)
{
  SUBOOL ok = SU_FALSE;

  /* Check authentication message */

  ok = SU_TRUE;

done:
  return ok;
}

SUPRIVATE SUBOOL
suscli_analyzer_server_start_analyzer(suscli_analyzer_server_t *self)
{
  SUBOOL ok = SU_FALSE;

  /* TODO: Make analyzer object */
  /* TODO: Create TX thread */

  ok = SU_TRUE;

done:
  return ok;
}

SUPRIVATE SUBOOL
suscli_analyzer_server_process_call(
    suscli_analyzer_server_t *self,
    suscli_analyzer_client_t *client,
    const struct suscan_analyzer_remote_call *call)
{
  SUBOOL ok = SU_FALSE;

  if (suscli_analyzer_client_is_auth(client)) {
    /* TODO: Process different call types */
  } else {
    SU_TRYCATCH(
        suscli_analyzer_server_process_auth_message(self, client, call),
        goto done);

    if (suscli_analyzer_client_is_auth(client)) {
      /*
       * Authentication successful! Now the client is entitled to make
       * changes in the server. First, ensure the analyzer object is
       * running.
       */
      if (self->analyzer == NULL)
        SU_TRYCATCH(suscli_analyzer_server_start_analyzer(self), goto done);
    } else {
      /* Authentication failed. Mark as failed. */
      client->failed = SU_TRUE;
    }
  }

  ok = SU_TRUE;

done:
  return ok;
}

SUPRIVATE SUBOOL
suscli_analyzer_server_register_clients(suscli_analyzer_server_t *self)
{
  int fd;
  suscli_analyzer_client_t *client = NULL;
  struct sockaddr_in inaddr;
  socklen_t len = sizeof(struct sockaddr_in);
  SUBOOL ok = SU_FALSE;

  while ((fd = accept(
      self->client_list.listen_fd,
      (struct sockaddr *) &inaddr,
      &len)) != -1) {
    SU_TRYCATCH(client = suscli_analyzer_client_new(fd), goto done);
    SU_TRYCATCH(
        suscli_analyzer_client_list_append_client(&self->client_list, client),
        goto done);

    /* TODO: Send authentication challenge */
    client = NULL;
  }

  SU_TRYCATCH(errno != EAGAIN, goto done);

  ok = SU_TRUE;

done:
  if (!ok)
    SU_ERROR("errno: %s\n", strerror(errno));

  if (client != NULL)
    suscli_analyzer_client_destroy(client);

  return ok;
}

SUPRIVATE void *
suscli_analyzer_server_rx_thread(void *userdata)
{
  suscli_analyzer_server_t *self =
      (suscli_analyzer_server_t *) userdata;
  suscli_analyzer_client_t *client = NULL;
  struct suscan_analyzer_remote_call *call;
  struct rbtree_node *node;
  int count;
  unsigned int i;
  struct pollfd *pfds;
  SUBOOL ok = SU_FALSE;

  for (;;) {
    pfds = self->client_list.client_pfds;

    SU_TRYCATCH(
        (count = poll(pfds, self->client_list.client_count + 1, -1)) > 0,
        goto done);

    if (pfds[0].revents & POLLIN) {
      /* New client(s)! */
      SU_TRYCATCH(suscli_analyzer_server_register_clients(self), goto done);
      --count;
    }

    for (i = 1; count > 0 && i <= self->client_list.client_count; ++i) {
      if (pfds[0].revents && POLLIN) {
        SU_TRYCATCH(
            node = rbtree_search(
                self->client_list.client_tree,
                pfds[0].fd,
                RB_EXACT),
            goto done);
        client = node->data;
        SU_TRYCATCH(client != NULL, goto done);

        if (client != NULL) {
          SU_TRYCATCH(suscli_analyzer_client_read(client), goto done);
          if ((call = suscli_analyzer_client_take_call(client)) != NULL) {
            /* Call completed from client, process it and do stuff */
            SU_TRYCATCH(
                suscli_analyzer_server_process_call(self, client, call),
                goto done);
          }
        }
      }

      --count;
    }

    /* This is actually a consistency condition */
    SU_TRYCATCH(count == 0, goto done);

    /* Some sockets may have been marked as dead. Clean them */
    SU_TRYCATCH(
        suscli_analyzer_client_list_attempt_cleanup(&self->client_list),
        goto done);
  }

  ok = SU_TRUE;

done:
  if (!ok)
    SU_ERROR("errno: %s\n", strerror(errno));

  return NULL;
}
