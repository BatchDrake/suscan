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
  shutdown(self->sfd, 2);

  grow_buf_finalize(&self->incoming_pdu);
  grow_buf_finalize(&self->outcoming_pdu);

  suscan_analyzer_remote_call_finalize(&self->incoming_call);
  suscan_analyzer_remote_call_finalize(&self->outcoming_call);

  free(self);
}
