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
#include <sys/fcntl.h>

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
  new->sfd = sfd;

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
  SUBOOL do_close = SU_TRUE;
  SUBOOL ok = SU_FALSE;

  if (!self->have_header) {
    chunksize =
        sizeof(struct suscan_analyzer_remote_pdu_header) - self->header_ptr;

    ret = read(self->sfd, self->header_bytes + self->header_ptr, chunksize);

    if (ret == 0)
      SU_WARNING(
          "Client[%s]: Unexpected client close\n",
          suscli_analyzer_client_string_addr(self));
    else if (ret == -1)
      SU_ERROR(
          "Client[%s]: Read error: %s\n",
          suscli_analyzer_client_string_addr(self),
          strerror(errno));
    else
      do_close = SU_FALSE;

    if (do_close)
      goto done;

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
      grow_buf_seek(&self->incoming_pdu, 0, SEEK_SET);
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

  if (self->have_header && self->have_body) {
    self->have_header = SU_FALSE;
    self->have_body   = SU_FALSE;
    call = &self->incoming_call;

    suscan_analyzer_remote_call_finalize(call);
    suscan_analyzer_remote_call_init(call, SUSCAN_ANALYZER_REMOTE_NONE);

    if (!suscan_analyzer_remote_call_deserialize(call, &self->incoming_pdu)) {
      SU_ERROR("Protocol error: failed to deserialize remote call\n");
      goto done;
    }

    ok = SU_TRUE;
  }

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
suscli_analyzer_client_write_buffer(
    suscli_analyzer_client_t *self,
    const grow_buf_t *buffer)
{
  struct suscan_analyzer_remote_pdu_header header;
  const uint8_t *data;
  size_t size, chunksize;
  SUBOOL ok = SU_FALSE;

  data = grow_buf_get_buffer(buffer);
  size = grow_buf_get_size(buffer);

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

done:
  return ok;
}

SUBOOL
suscli_analyzer_client_shutdown(suscli_analyzer_client_t *self)
{
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(!self->failed, goto done);

  SU_TRYCATCH(shutdown(self->sfd, 2) == 0, goto done);

  ok = SU_TRUE;

done:
  return ok;
}

SUBOOL
suscli_analyzer_client_deliver_call(suscli_analyzer_client_t *self)
{
  SUBOOL ok = SU_FALSE;

  grow_buf_clear(&self->outcoming_pdu);

  SU_TRYCATCH(
      suscan_analyzer_remote_call_serialize(
          &self->outcoming_call,
          &self->outcoming_pdu),
      goto done);

  SU_TRYCATCH(
      suscli_analyzer_client_write_buffer(self, &self->outcoming_pdu),
      goto done);

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

  if (count + SUSCLI_ANSERV_FD_OFFSET > self->client_pfds_alloc) {
    SU_TRYCATCH(
        pollfds = realloc(
            self->client_pfds,
            (count + SUSCLI_ANSERV_FD_OFFSET) * sizeof(struct pollfd)),
        goto done);
    self->client_pfds = pollfds;
    self->client_pfds_alloc = count + SUSCLI_ANSERV_FD_OFFSET;
  } else {
    pollfds = self->client_pfds;
  }

  /* We always have two fds to poll. The listen socket and the cancel socket */
  pollfds[SUSCLI_ANSERV_LISTEN_FD].fd      = self->listen_fd;
  pollfds[SUSCLI_ANSERV_LISTEN_FD].events  = POLLIN;
  pollfds[SUSCLI_ANSERV_LISTEN_FD].revents = 0;

  pollfds[SUSCLI_ANSERV_CANCEL_FD].fd      = self->cancel_fd;
  pollfds[SUSCLI_ANSERV_CANCEL_FD].events  = POLLIN;
  pollfds[SUSCLI_ANSERV_CANCEL_FD].revents = 0;

  i = 0;
  while (this != NULL) {
    if (this->data != NULL) {
      SU_TRYCATCH(i < count, goto done);
      client = this->data;

      pollfds[i + SUSCLI_ANSERV_FD_OFFSET].fd      = client->sfd;
      pollfds[i + SUSCLI_ANSERV_FD_OFFSET].events  = POLLIN;
      pollfds[i + SUSCLI_ANSERV_FD_OFFSET].revents = 0;

      ++i;
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
    int listen_fd,
    int cancel_fd)
{
  SUBOOL ok = SU_FALSE;

  memset(self, 0, sizeof(struct suscli_analyzer_client_list));

  self->listen_fd = listen_fd;
  self->cancel_fd = cancel_fd;

  SU_TRYCATCH(self->client_tree = rbtree_new(), goto done);

  SU_TRYCATCH(pthread_mutex_init(&self->client_mutex, NULL) == 0, goto done);
  self->client_mutex_initialized = SU_TRUE;

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

  if (node != NULL && node->data != NULL) {
    SU_ERROR(
        "Server state desync: attempting to register a client with the same sfd (%d) twice\n",
        client->sfd);
    goto done;
  }

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

SUBOOL
suscli_analyzer_client_list_broadcast(
    struct suscli_analyzer_client_list *self,
    const grow_buf_t *buffer)
{
  suscli_analyzer_client_t *this;
  int error;
  SUBOOL mutex_acquired = SU_FALSE;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(pthread_mutex_lock(&self->client_mutex) != -1, goto done);
  mutex_acquired = SU_TRUE;

  this = self->client_head;

  while (this != NULL) {
    if (!suscli_analyzer_client_is_failed(this)
        && suscli_analyzer_client_is_auth(this)) {
      if (!suscli_analyzer_client_write_buffer(this, buffer)) {
        error = errno;
        SU_WARNING(
            "Client[%s]: write failed (%s)\n",
            suscli_analyzer_client_string_addr(this),
            strerror(error));
        suscli_analyzer_client_mark_failed(this);
      }
    }

    this = this->next;
  }

  ok = SU_TRUE;

done:
  if (mutex_acquired)
    (void) pthread_mutex_unlock(&self->client_mutex);

  return ok;
}

SUBOOL
suscli_analyzer_client_list_force_shutdown(
    struct suscli_analyzer_client_list *self)
{
  suscli_analyzer_client_t *this;
  int error;
  SUBOOL mutex_acquired = SU_FALSE;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(pthread_mutex_lock(&self->client_mutex) != -1, goto done);
  mutex_acquired = SU_TRUE;

  this = self->client_head;

  while (this != NULL) {
    if (!suscli_analyzer_client_is_failed(this)) {
      if (!suscli_analyzer_client_shutdown(this)) {
        error = errno;
        SU_WARNING(
            "Client[%s]: shutdown failed (%s)\n",
            suscli_analyzer_client_string_addr(this),
            strerror(error));
      }
    }

    this = this->next;
  }

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

  memset(self, 0, sizeof(struct suscli_analyzer_client_list));
}

/***************************** TX Thread **************************************/
SUPRIVATE void *
suscli_analyzer_server_tx_thread(void *ptr)
{
  suscli_analyzer_server_t *self = (suscli_analyzer_server_t *) ptr;
  void *message;
  uint32_t type;

  while ((message = suscan_analyzer_read(self->analyzer, &type)) != NULL) {
    if (type == SUSCAN_WORKER_MSG_TYPE_HALT)
      break;

    self->broadcast_call.type     = SUSCAN_ANALYZER_REMOTE_MESSAGE;
    self->broadcast_call.msg.type = type;
    self->broadcast_call.msg.ptr  = message;

    grow_buf_clear(&self->broadcast_pdu);

    SU_TRYCATCH(
        suscan_analyzer_remote_call_serialize(
            &self->broadcast_call,
            &self->broadcast_pdu),
        goto done);

    suscli_analyzer_client_list_broadcast(
        &self->client_list,
        &self->broadcast_pdu);

    suscan_analyzer_remote_call_finalize(&self->broadcast_call);
  }

done:
  suscan_analyzer_remote_call_finalize(&self->broadcast_call);
  suscli_analyzer_client_list_force_shutdown(&self->client_list);
  suscan_analyzer_destroy(self->analyzer);
  self->analyzer = NULL;

  self->tx_halted = SU_TRUE;

  return NULL;
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

  client->auth = SU_TRUE;
  ok = SU_TRUE;

/*done:*/
  return ok;
}

SUPRIVATE SUBOOL
suscli_analyzer_server_start_analyzer(suscli_analyzer_server_t *self)
{
  struct suscan_analyzer_params params =
      suscan_analyzer_params_INITIALIZER;
  suscan_analyzer_t *analyzer = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(self->analyzer == NULL,   goto done);
  SU_TRYCATCH(!self->tx_thread_running, goto done);

  SU_TRYCATCH(
      analyzer = suscan_analyzer_new(
          &params,
          self->config,
          &self->mq),
      goto done);

  self->analyzer = analyzer;
  self->tx_halted = SU_FALSE;

  SU_TRYCATCH(
      pthread_create(
          &self->tx_thread,
          NULL,
          suscli_analyzer_server_tx_thread,
          self) != -1,
      goto done);
  self->tx_thread_running = SU_TRUE;

  analyzer = NULL;

  ok = SU_TRUE;

done:
  if (analyzer != NULL)
    suscan_analyzer_destroy(analyzer);

  return ok;
}

SUPRIVATE SUBOOL
suscli_analyzer_server_deliver_call(
    suscli_analyzer_server_t *self,
    struct suscan_analyzer_remote_call *call)
{
  SUBOOL ok = SU_FALSE;

  switch (call->type) {
    case SUSCAN_ANALYZER_REMOTE_SET_FREQUENCY:
      SU_TRYCATCH(
          suscan_analyzer_set_freq(self->analyzer, call->freq, call->lnb),
          goto done);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_GAIN:
      SU_TRYCATCH(
          suscan_analyzer_set_gain(
              self->analyzer,
              call->gain.name,
              call->gain.value),
          goto done);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_ANTENNA:
      SU_TRYCATCH(
          suscan_analyzer_set_antenna(self->analyzer, call->antenna),
          goto done);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_BANDWIDTH:
      SU_TRYCATCH(
          suscan_analyzer_set_bw(self->analyzer, call->bandwidth),
          goto done);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_DC_REMOVE:
      SU_TRYCATCH(
          suscan_analyzer_set_dc_remove(self->analyzer, call->dc_remove),
          goto done);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_IQ_REVERSE:
      SU_TRYCATCH(
          suscan_analyzer_set_iq_reverse(self->analyzer, call->iq_reverse),
          goto done);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_AGC:
      SU_TRYCATCH(
          suscan_analyzer_set_agc(self->analyzer, call->agc),
          goto done);
      break;

    case SUSCAN_ANALYZER_REMOTE_FORCE_EOS:
      SU_TRYCATCH(
          suscan_analyzer_force_eos(self->analyzer),
          goto done);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_SWEEP_STRATEGY:
      SU_TRYCATCH(
          suscan_analyzer_set_sweep_stratrgy(
              self->analyzer,
              call->sweep_strategy),
          goto done);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_SPECTRUM_PARTITIONING:
      SU_TRYCATCH(
          suscan_analyzer_set_spectrum_partitioning(
              self->analyzer,
              call->spectrum_partitioning),
          goto done);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_HOP_RANGE:
      SU_TRYCATCH(
          suscan_analyzer_set_hop_range(
              self->analyzer,
              call->hop_range.min,
              call->hop_range.max),
          goto done);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_BUFFERING_SIZE:
      SU_TRYCATCH(
          suscan_analyzer_set_buffering_size(
              self->analyzer,
              call->buffering_size),
          goto done);
      break;

    case SUSCAN_ANALYZER_REMOTE_MESSAGE:
      SU_TRYCATCH(
          suscan_analyzer_write(
              self->analyzer,
              call->msg.type,
              call->msg.ptr),
          goto done);
      call->msg.ptr = NULL;
      break;

    case SUSCAN_ANALYZER_REMOTE_REQ_HALT:
      suscan_analyzer_req_halt(self->analyzer);
      break;

    default:
      SU_ERROR("Invalid call code %d\n", call->type);
      goto done;
  }

  ok = SU_TRUE;

done:
  return ok;
}

SUPRIVATE SUBOOL
suscli_analyzer_server_process_call(
    suscli_analyzer_server_t *self,
    suscli_analyzer_client_t *client,
    struct suscan_analyzer_remote_call *call)
{
  SUBOOL ok = SU_FALSE;

  if (suscli_analyzer_client_is_auth(client)) {
    SU_TRYCATCH(
        suscli_analyzer_server_deliver_call(self, call),
        goto done);
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
        if (!suscli_analyzer_server_start_analyzer(self)) {
          SU_ERROR("Failed to initialize analyzer. Rejecting client\n");
          suscli_analyzer_client_shutdown(client);
        }
    } else {
      /* Authentication failed. Mark as failed. */
      SU_ERROR("Authentication failed. Forcing shutdown\n");
      suscli_analyzer_client_shutdown(client);
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

  SU_TRYCATCH(errno == EAGAIN, goto done);

  ok = SU_TRUE;

done:
  if (!ok)
    SU_ERROR("errno: %s\n", strerror(errno));

  if (client != NULL)
    suscli_analyzer_client_destroy(client);

  return ok;
}

SUPRIVATE void
suscli_analyzer_server_clean_dead_threads(suscli_analyzer_server_t *self)
{
  if (self->tx_thread_running && self->tx_halted) {
    pthread_join(self->tx_thread, NULL);
    self->tx_thread_running = SU_FALSE;
  }
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
        (count = poll(
            pfds,
            self->client_list.client_count + SUSCLI_ANSERV_FD_OFFSET,
            -1)) > 0,
        goto done);

    suscli_analyzer_server_clean_dead_threads(self);

    if (pfds[SUSCLI_ANSERV_CANCEL_FD].revents & POLLIN) {
      /* Cancel requested */
      ok = SU_TRUE;
      goto done;
    }

    if (pfds[SUSCLI_ANSERV_LISTEN_FD].revents & POLLIN) {
      /* New client(s)! */
      SU_TRYCATCH(suscli_analyzer_server_register_clients(self), goto done);
      --count;
    }

    for (i = 0; count > 0 && i <= self->client_list.client_count; ++i) {
      if (pfds[i + SUSCLI_ANSERV_FD_OFFSET].revents && POLLIN) {
        SU_TRYCATCH(
            node = rbtree_search(
                self->client_list.client_tree,
                pfds[i + SUSCLI_ANSERV_FD_OFFSET].fd,
                RB_EXACT),
            goto done);
        client = node->data;
        SU_TRYCATCH(client != NULL, goto done);

        if (client != NULL) {
          if (!suscli_analyzer_client_read(client))
            suscli_analyzer_client_mark_failed(client);
          else if ((call = suscli_analyzer_client_take_call(client)) != NULL) {
            /* Call completed from client, process it and do stuff */
            SU_TRYCATCH(
                suscli_analyzer_server_process_call(self, client, call),
                goto done);
          }
        }
        --count;
      }
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

SUPRIVATE int
suscli_analyzer_server_create_socket(uint16_t port)
{
  struct sockaddr_in addr;
  int enable = 1;
  int sfd = -1;
  int fd = -1;
  int flags;

  SU_TRYCATCH(
      (fd = socket(AF_INET, SOCK_STREAM, 0)) != -1,
      goto done);

  if ((flags = fcntl(fd, F_GETFL)) == -1) {
    SU_ERROR("Failed to perform fcntl on socket: %s\n", strerror(errno));
    goto done;
  }

  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    SU_ERROR("Failed to make socket non blocking: %s\n", strerror(errno));
    goto done;
  }

  SU_TRYCATCH(
      (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int))) != -1,
      goto done);

  addr.sin_family      = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port        = htons(port);

  if (bind(fd, (struct sockaddr *) &addr, sizeof (struct sockaddr_in)) == -1) {
    SU_ERROR(
        "failed to bind socket to port %d for listen: %s\n",
        port,
        strerror(errno));
    goto done;
  }

  if (listen(fd, 5) == -1) {
    SU_ERROR("failed to listen on socket: %s\n", strerror(errno));
    goto done;
  }

  sfd = fd;
  fd = -1;

done:
  if (fd != -1)
    close(fd);

  return sfd;
}

suscli_analyzer_server_t *
suscli_analyzer_server_new(suscan_source_config_t *profile, uint16_t port)
{
  suscli_analyzer_server_t *new = NULL;
  int sfd = -1;

  SU_TRYCATCH(new = calloc(1, sizeof(suscli_analyzer_server_t)), goto done);

  new->client_list.listen_fd = -1;
  new->client_list.cancel_fd = -1;

  new->cancel_pipefd[0] = -1;
  new->cancel_pipefd[1] = -1;

  new->listen_port = port;
  SU_TRYCATCH(new->config = suscan_source_config_clone(profile), goto done);

  SU_TRYCATCH(pipe(new->cancel_pipefd) != -1, goto done);

  SU_TRYCATCH(
      (sfd = suscli_analyzer_server_create_socket(port)) != -1,
      goto done);

  SU_TRYCATCH(
      suscli_analyzer_client_list_init(
          &new->client_list,
          sfd,
          new->cancel_pipefd[0]),
      goto done);

  SU_TRYCATCH(
      pthread_create(
          &new->rx_thread,
          NULL,
          suscli_analyzer_server_rx_thread,
          new) != -1,
      goto done);
  new->rx_thread_running = SU_TRUE;

  return new;

done:
  if (new != NULL)
    suscli_analyzer_server_destroy(new);

  return NULL;
}

SUPRIVATE void
suscli_analyzer_server_cancel_rx_thread(suscli_analyzer_server_t *self)
{
  char b = 1;

  (void) write(self->cancel_pipefd[1], &b, 1);
}

void
suscli_analyzer_server_destroy(suscli_analyzer_server_t *self)
{
  if (self->rx_thread_running) {
    if (self->analyzer != NULL) {
      suscan_analyzer_req_halt(self->analyzer);
      if (self->tx_thread_running)
        pthread_join(self->tx_thread, NULL);

      if (self->analyzer != NULL)
        suscan_analyzer_destroy(self->analyzer);
    }

    suscli_analyzer_server_cancel_rx_thread(self);
    pthread_join(self->rx_thread, NULL);
  }

  if (self->client_list.listen_fd != -1)
    close(self->client_list.listen_fd);

  if (self->cancel_pipefd[0] != -1)
    close(self->cancel_pipefd[0]);

  if (self->cancel_pipefd[1] != -1)
    close(self->cancel_pipefd[1]);

  suscli_analyzer_client_list_finalize(&self->client_list);

  if (self->config != NULL)
    suscan_source_config_destroy(self->config);

  free(self);
}


