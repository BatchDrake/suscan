/*

  Copyright (C) 2021 Gonzalo José Carracedo Carballal

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

#define SU_LOG_DOMAIN "analyzer-client"

#include "devserv.h"
#include <analyzer/msg.h>
#include <analyzer/version.h>
#include <sigutils/log.h>
#include <sys/poll.h>
#include <sys/fcntl.h>

#define SUSCLI_ANALYZER_SERVER_NAME "Suscan device server - " SUSCAN_VERSION_STRING

/************************** Analyzer Client API *******************************/
suscli_analyzer_client_t *
suscli_analyzer_client_new(int sfd)
{
  struct sockaddr_in sin;
  socklen_t len = sizeof(struct sockaddr_in);
  suscli_analyzer_client_t *new = NULL;

  SU_TRYCATCH(new = calloc(1, sizeof(suscli_analyzer_client_t)), goto fail);

  new->sfd   = -1;

  new->inspectors.inspector_last_free = -1;

  SU_TRYCATCH(
      pthread_mutex_init(&new->inspectors.inspector_mutex, NULL) == 0,
      goto fail);
  new->inspectors.inspector_mutex_initialized = SU_TRUE;

  SU_TRYCATCH(
      suscan_analyzer_server_hello_init(
          &new->server_hello,
          SUSCLI_ANALYZER_SERVER_NAME),
      goto fail);

  gettimeofday(&new->conntime, NULL);

  SU_TRYCATCH(
      getpeername(sfd, (struct sockaddr *) &sin, &len) != -1,
      goto fail);

  new->remote_addr = sin.sin_addr;

  SU_TRYCATCH(
      new->name = strbuild(
          "[client %s:%d]",
          suscli_analyzer_client_string_addr(new),
          ntohs(sin.sin_port)),
      goto fail);

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

    if (ret == 0) {
      SU_INFO(
          "%s: client left\n",
          suscli_analyzer_client_get_name(self));
    } else if (ret == -1) {
      SU_INFO(
          "%s: read error: %s\n",
          suscli_analyzer_client_get_name(self),
          strerror(errno));
    } else {
      do_close = SU_FALSE;
    }

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

      grow_buf_shrink(&self->incoming_pdu);
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

/* Elements in the freelist are negative! */
SUHANDLE
suscli_analyzer_client_register_inspector_handle_unsafe(
    suscli_analyzer_client_t *self,
    SUHANDLE global_handle,
    int32_t itl_index)
{
  SUHANDLE ret = -1;
  struct suscli_analyzer_client_inspector_entry *tmp = NULL;

  if (self->inspectors.inspector_last_free < 0) {
    /* No "last free". Time to reallocate */
    SU_TRYCATCH(
        tmp = realloc(
            self->inspectors.inspector_list,
            (self->inspectors.inspector_alloc + 1) * sizeof(SUHANDLE)),
        goto done);
    self->inspectors.inspector_list = tmp;
    ret = self->inspectors.inspector_alloc++;
  } else {
    ret = self->inspectors.inspector_last_free;
    self->inspectors.inspector_last_free =
        ~self->inspectors.inspector_list[ret].global_handle;
  }

  self->inspectors.inspector_list[ret].global_handle = global_handle;
  self->inspectors.inspector_list[ret].itl_index     = itl_index;
  ++self->inspectors.inspector_count;

done:
  return ret;
}

SUHANDLE
suscli_analyzer_client_register_inspector_handle(
    suscli_analyzer_client_t *self,
    SUHANDLE global_handle,
    int32_t itl_index)
{
  SUHANDLE handle = -1;
  SUBOOL acquired = SU_FALSE;

  SU_TRYCATCH(
      pthread_mutex_lock(&self->inspectors.inspector_mutex) == 0,
      goto done);

  acquired = SU_TRUE;

  handle = suscli_analyzer_client_register_inspector_handle_unsafe(
      self,
      global_handle,
      itl_index);

done:
  if (acquired)
    pthread_mutex_unlock(&self->inspectors.inspector_mutex);

  return handle;
}

SUPRIVATE SUHANDLE
suscli_analyzer_client_translate_handle_unsafe(
    suscli_analyzer_client_t *self,
    SUHANDLE local_handle)
{
  SU_TRYCATCH(local_handle >= 0, return -1);
  SU_TRYCATCH(local_handle < self->inspectors.inspector_alloc, return -1);
  SU_TRYCATCH(
      self->inspectors.inspector_list[local_handle].global_handle >= 0,
      return -1);

  return self->inspectors.inspector_list[local_handle].global_handle;
}

SUBOOL
suscli_analyzer_client_dispose_inspector_handle_unsafe(
    suscli_analyzer_client_t *self,
    SUHANDLE local_handle)
{
  SU_TRYCATCH(local_handle >= 0, return SU_FALSE);
  SU_TRYCATCH(local_handle < self->inspectors.inspector_alloc, return SU_FALSE);

  SU_TRYCATCH(self->inspectors.inspector_count > 0, return SU_FALSE);

  SU_TRYCATCH(
      self->inspectors.inspector_list[local_handle].global_handle >= 0,
      return SU_FALSE);

  self->inspectors.inspector_list[local_handle].global_handle =
      ~self->inspectors.inspector_last_free;
  self->inspectors.inspector_last_free = local_handle;
  --self->inspectors.inspector_count;

  return SU_TRUE;
}

SUBOOL
suscli_analyzer_client_dispose_inspector_handle(
    suscli_analyzer_client_t *self,
    SUHANDLE local_handle)
{
  SUBOOL ok = SU_FALSE;
  SUBOOL acquired = SU_FALSE;

  SU_TRYCATCH(
      pthread_mutex_lock(&self->inspectors.inspector_mutex) == 0,
      goto done);
  acquired = SU_TRUE;

  ok = suscli_analyzer_client_dispose_inspector_handle_unsafe(
      self,
      local_handle);

done:
  if (acquired)
    pthread_mutex_unlock(&self->inspectors.inspector_mutex);

  return ok;
}

SUBOOL
suscli_analyzer_client_for_each_inspector(
    suscli_analyzer_client_t *self,
    SUBOOL (*func) (
        const suscli_analyzer_client_t *client,
        void *userdata,
        SUHANDLE local_handle,
        SUHANDLE global_handle),
    void *userdata)
{
  SUBOOL ok = SU_FALSE;
  SUBOOL acquired = SU_FALSE;

  SU_TRYCATCH(
      pthread_mutex_lock(&self->inspectors.inspector_mutex) == 0,
      goto done);
  acquired = SU_TRUE;

  ok = suscli_analyzer_client_for_each_inspector_unsafe(self, func, userdata);

done:
  if (acquired)
    pthread_mutex_unlock(&self->inspectors.inspector_mutex);

  return ok;
}

SUBOOL
suscli_analyzer_client_intercept_message(
    suscli_analyzer_client_t *self,
    uint32_t type,
    void *message,
    const struct suscli_analyzer_client_interceptors *interceptors)
{
  struct suscan_analyzer_inspector_msg *inspmsg;
  SUBOOL mutex_acquired = SU_FALSE;
  SUHANDLE handle;
  SUBOOL ok = SU_FALSE;

  /*
   * Some messages must be intercepted prior being delivered to
   * the analyzer. This is the case for messages setting the inspector_id
   */

  if (type == SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR) {
    inspmsg = (struct suscan_analyzer_inspector_msg *) message;

    SU_TRYCATCH(
          pthread_mutex_lock(&self->inspectors.inspector_mutex) == 0,
          goto done);
    mutex_acquired = SU_TRUE;

    /* vvvvvvvvvvvvvvvvvvvvvv Inspector mutex acquired vvvvvvvvvvvvvvvvvvvvv */
    if (inspmsg->kind == SUSCAN_ANALYZER_INSPECTOR_MSGKIND_OPEN) {
      SU_TRYCATCH(
          (interceptors->inspector_open)(interceptors->userdata, self, inspmsg),
          goto done);
    } else {
      handle = inspmsg->handle;

      if ((inspmsg->handle = suscli_analyzer_client_translate_handle_unsafe(
          self,
          handle)) != -1) {
        /* This local handle actually refers to something! */
        if (inspmsg->kind == SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SET_ID) {
          SU_TRYCATCH(
              (interceptors->inspector_set_id)(
                  interceptors->userdata,
                  self,
                  inspmsg,
                  self->inspectors.inspector_list[handle].itl_index),
              goto done);
        }
      } else {
        SU_WARNING("Could not translate handle 0x%x\n", handle);
      }
    }
    /* ^^^^^^^^^^^^^^^^^^^^^^ Inspector mutex acquired ^^^^^^^^^^^^^^^^^^^^^ */
  }

  ok = SU_TRUE;

done:
  if (mutex_acquired)
    pthread_mutex_unlock(&self->inspectors.inspector_mutex);

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

  ok = SU_TRUE;

done:
  return ok;
}

SUBOOL
suscli_analyzer_client_shutdown(suscli_analyzer_client_t *self)
{
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(!self->failed,   goto done);
  SU_TRYCATCH(!self->closed,   goto done);
  SU_TRYCATCH(self->sfd != -1, goto done);

  self->closed = SU_TRUE;

  SU_TRYCATCH(shutdown(self->sfd, 2) == 0, goto done);

  ok = SU_TRUE;

done:
  return ok;
}

SUBOOL
suscli_analyzer_client_send_hello(suscli_analyzer_client_t *self)
{
  grow_buf_t pdu = grow_buf_INITIALIZER;
  SUBOOL ok = SU_FALSE;

  grow_buf_shrink(&pdu);

  SU_TRYCATCH(
      suscan_analyzer_server_hello_serialize(&self->server_hello, &pdu),
      goto done);

  SU_TRYCATCH(suscli_analyzer_client_write_buffer(self, &pdu), goto done);

  ok = SU_TRUE;

done:
  grow_buf_finalize(&pdu);

  return ok;
}

SUBOOL
suscli_analyzer_client_deliver_call(
    suscli_analyzer_client_t *self,
    const struct suscan_analyzer_remote_call *call)
{
  grow_buf_t pdu = grow_buf_INITIALIZER;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      suscan_analyzer_remote_call_serialize(call, &pdu),
      goto done);

  SU_TRYCATCH(suscli_analyzer_client_write_buffer(self, &pdu), goto done);

  ok = SU_TRUE;

done:
  grow_buf_finalize(&pdu);
  return ok;
}

SUBOOL
suscli_analyzer_client_send_source_info(
    suscli_analyzer_client_t *self,
    const struct suscan_analyzer_source_info *info)
{
  struct suscan_analyzer_remote_call *call = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      call = malloc(sizeof(struct suscan_analyzer_remote_call)),
      goto done);

  suscan_analyzer_remote_call_init(call, SUSCAN_ANALYZER_REMOTE_SOURCE_INFO);

  SU_TRYCATCH(
      suscan_analyzer_source_info_init_copy(&call->source_info, info),
      goto done);

  SU_TRYCATCH(suscli_analyzer_client_deliver_call(self, call), goto done);

  suscli_analyzer_client_set_has_source_info(self, SU_TRUE);

  ok = SU_TRUE;

done:
  if (call != NULL) {
    suscan_analyzer_remote_call_finalize(call);
    free(call);
  }

  return ok;
}

SUBOOL
suscli_analyzer_client_send_auth_rejected(suscli_analyzer_client_t *self)
{
  struct suscan_analyzer_remote_call *call = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      call = malloc(sizeof(struct suscan_analyzer_remote_call)),
      goto done);

  suscan_analyzer_remote_call_init(call, SUSCAN_ANALYZER_REMOTE_AUTH_REJECTED);

  SU_TRYCATCH(suscli_analyzer_client_deliver_call(self, call), goto done);

  ok = SU_TRUE;

done:
  if (call != NULL) {
    suscan_analyzer_remote_call_finalize(call);
    free(call);
  }

  return ok;
}

void
suscli_analyzer_client_destroy(suscli_analyzer_client_t *self)
{
  if (self->sfd != -1 && !self->closed)
    close(self->sfd);

  if (self->name != NULL)
    free(self->name);

  grow_buf_finalize(&self->incoming_pdu);

  suscan_analyzer_server_hello_finalize(&self->server_hello);
  suscan_analyzer_remote_call_finalize(&self->incoming_call);

  if (self->inspectors.inspector_list != NULL)
    free(self->inspectors.inspector_list);

  if (self->inspectors.inspector_mutex_initialized)
    pthread_mutex_destroy(&self->inspectors.inspector_mutex);

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

      /*
       * Conditions for removal: either it is marked as failed, or
       * there are no pending analyzer resources.
       */
      if (suscli_analyzer_client_is_failed(client) &&
          (self->epoch != client->epoch
              || !suscli_analyzer_client_has_outstanding_inspectors(client))) {
        suscli_analyzer_client_list_remove_unsafe(self, client);
        SU_INFO(
            "%s: client removed from list (%d outstanding clients)\n",
            suscli_analyzer_client_get_name(client),
            self->client_count);
        suscli_analyzer_client_destroy(client);
        changed = SU_TRUE;
      }
    }
    this = this->next;
  }

  return changed;
}

SUBOOL
suscli_analyzer_client_for_each_inspector_unsafe(
    const suscli_analyzer_client_t *self,
    SUBOOL (*func) (
        const suscli_analyzer_client_t *client,
        void *userdata,
        SUHANDLE local_handle,
        SUHANDLE global_handle),
    void *userdata)
{
  SUHANDLE i;

  SUHANDLE first_free = self->inspectors.inspector_last_free;

  if (first_free > 0)
    while (~self->inspectors.inspector_list[first_free].global_handle > 0)
        first_free = ~self->inspectors.inspector_list[first_free].global_handle;

  for (i = 0; i < self->inspectors.inspector_alloc; ++i)
    if (i != first_free &&
        self->inspectors.inspector_list[i].global_handle >= 0)
      if (!(func) (
          self,
          userdata,
          i,
          self->inspectors.inspector_list[i].global_handle))
        return SU_FALSE;

  return SU_TRUE;
}

SUBOOL
suscli_analyzer_client_list_attempt_cleanup(
    struct suscli_analyzer_client_list *self)
{
  SUBOOL mutex_acquired = SU_FALSE;
  SUBOOL ok = SU_FALSE;

  if (pthread_mutex_trylock(&self->client_mutex) == 0) {
    mutex_acquired = SU_TRUE;
    if (suscli_analyzer_client_list_cleanup_unsafe(self)) {
      SU_TRYCATCH(
          suscli_analyzer_client_list_update_pollfds_unsafe(self),
          goto done);
    }
  }

  ok = SU_TRUE;

done:
  if (mutex_acquired)
    (void) pthread_mutex_unlock(&self->client_mutex);

  return ok;
}

int32_t
suscli_analyzer_client_list_alloc_itl_entry_unsafe(
    struct suscli_analyzer_client_list *self,
    suscli_analyzer_client_t *client)
{
  int32_t ret = -1;
  struct suscli_analyzer_itl_entry *tmp = NULL;

  SU_TRYCATCH(client != NULL, goto done);

  if (self->itl_last_free < 0) {
    /* No "last free". Time to reallocate */
    SU_TRYCATCH(
        tmp = realloc(
            self->itl_table,
            (self->itl_count + 1) * sizeof(struct suscli_analyzer_itl_entry)),
        goto done);

    self->itl_table = tmp;
    ret = self->itl_count++;
  } else {
    ret = self->itl_last_free;
    self->itl_last_free = self->itl_table[ret].next_free;
  }

  self->itl_table[ret].client = client;

done:
  return ret;
}

struct suscli_analyzer_itl_entry *
suscli_analyzer_client_list_get_itl_entry_unsafe(
    const struct suscli_analyzer_client_list *self,
    int32_t entry)
{
  SU_TRYCATCH(entry >= 0, return NULL);
  SU_TRYCATCH(entry < self->itl_count, return NULL);

  return self->itl_table + entry;
}

SUBOOL
suscli_analyzer_client_list_dispose_itl_entry_unsafe(
    struct suscli_analyzer_client_list *self,
    int32_t entry)
{
  SU_TRYCATCH(entry >= 0, return SU_FALSE);
  SU_TRYCATCH(entry < self->itl_count, return SU_FALSE);

  SU_TRYCATCH(self->itl_table[entry].client != NULL, return SU_FALSE);

  self->itl_table[entry].next_free = self->itl_last_free;
  self->itl_table[entry].client = NULL;
  self->itl_last_free = entry;

  return SU_TRUE;
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

  self->itl_last_free = -1;

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

  SU_TRYCATCH(pthread_mutex_lock(&self->client_mutex) == 0, goto done);
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

  client->epoch = self->epoch;
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
suscli_analyzer_client_list_broadcast_unsafe(
    struct suscli_analyzer_client_list *self,
    const grow_buf_t *buffer,
    SUBOOL (*on_client_error) (
        suscli_analyzer_client_t *client,
        void *userdata,
        int error),
    void *userdata)
{
  suscli_analyzer_client_t *this;
  int error;
  SUBOOL ok = SU_FALSE;

  this = self->client_head;

  while (this != NULL) {
    if (suscli_analyzer_client_can_write(this)
        && suscli_analyzer_client_has_source_info(this)) {
      if (!suscli_analyzer_client_write_buffer(this, buffer)) {
        error = errno;
        SU_WARNING(
            "%s: write failed (%s)\n",
            suscli_analyzer_client_get_name(this),
            strerror(error));
        SU_TRYCATCH((on_client_error) (this, userdata, error), goto done);
      }
    }

    this = this->next;
  }

  ok = SU_TRUE;

done:
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

  SU_TRYCATCH(pthread_mutex_lock(&self->client_mutex) == 0, goto done);
  mutex_acquired = SU_TRUE;

  this = self->client_head;

  while (this != NULL) {
    if (!suscli_analyzer_client_is_failed(this)) {
      if (!suscli_analyzer_client_shutdown(this)) {
        error = errno;
        SU_WARNING(
            "%s: shutdown failed (%s)\n",
            suscli_analyzer_client_get_name(this),
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
suscli_analyzer_client_list_lookup_unsafe(
    const struct suscli_analyzer_client_list *self,
    int fd)
{
  suscli_analyzer_client_t *client;
  struct rbtree_node *node;

  if ((node = rbtree_search(self->client_tree, fd, RB_EXACT)) == NULL)
    return NULL;

  client = node->data;

  if (client->sfd != fd) {
    SU_ERROR("client->sfd does not match fd!\n");
    return NULL;
  }

  return client;
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

  if (self->itl_table != NULL)
    free(self->itl_table);

  memset(self, 0, sizeof(struct suscli_analyzer_client_list));
}
