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

#define SU_LOG_DOMAIN "remote-analyzer"

#include <util/compat-socket.h>
#include <util/compat-unistd.h>
#include <util/compat-netdb.h>
#include <util/compat-inet.h>
#include <util/compat-fcntl.h>
#include <util/compat-poll.h>

#include "remote.h"
#include "msg.h"
#include "multicast.h"
#include <zlib.h>
#include <analyzer/realtime.h>

#ifdef bool
#  undef bool
#endif /* bool */

SUPRIVATE struct suscan_analyzer_interface *g_remote_analyzer_interface;

enum suscan_remote_analyzer_auth_result {
  SUSCAN_REMOTE_ANALYZER_AUTH_RESULT_SUCCESS,
  SUSCAN_REMOTE_ANALYZER_AUTH_RESULT_INVALID_SERVER,
  SUSCAN_REMOTE_ANALYZER_AUTH_RESULT_INCOMPATIBLE_VERSION,
  SUSCAN_REMOTE_ANALYZER_AUTH_RESULT_STARTUP_ERROR,
  SUSCAN_REMOTE_ANALYZER_AUTH_RESULT_REJECTED,
  SUSCAN_REMOTE_ANALYZER_AUTH_RESULT_DISCONNECTED
};

#if 0
void
grow_buffer_debug(const grow_buf_t *buffer)
{
  unsigned int i;

  for (i = 0; i < buffer->size; ++i)
    fprintf(stderr, "%02x", buffer->bytes[i]);

  fprintf(stderr, "\n");
}
#endif

SUBOOL
suscan_remote_partial_pdu_state_read(
  struct suscan_remote_partial_pdu_state *self,
  const char *remote,
  int sfd)
{
  size_t chunksize;
  size_t ret;
  SUBOOL do_close = SU_TRUE;
  SUBOOL ok = SU_FALSE;

  if (!self->have_header) {
    chunksize =
        sizeof(struct suscan_analyzer_remote_pdu_header) - self->header_ptr;

    ret = read(sfd, self->header_bytes + self->header_ptr, chunksize);

    if (ret == 0) {
      SU_INFO("%s: peer left\n", remote);
    } else if (ret == -1) {
      SU_INFO("%s: read error: %s\n", remote, strerror(errno));
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

      if (self->header.magic != SUSCAN_REMOTE_PDU_HEADER_MAGIC
      && self->header.magic != SUSCAN_REMOTE_COMPRESSED_PDU_HEADER_MAGIC) {
        SU_ERROR("Protocol error: invalid remote PDU header magic\n");
        goto done;
      }

      self->have_header = self->header.size != 0;

      grow_buf_shrink(&self->incoming_pdu);
    }
  } else if (!self->have_body) {
    if ((chunksize = self->header.size) > SUSCAN_REMOTE_READ_BUFFER)
      chunksize = SUSCAN_REMOTE_READ_BUFFER;

    if ((ret = read(sfd, self->read_buffer, chunksize)) < 1) {
      SU_ERROR("Failed to read from socket: %s\n", strerror(errno));
      goto done;
    }

    SU_TRYCATCH(
        grow_buf_append(&self->incoming_pdu, self->read_buffer, ret) != -1,
        goto done);

    self->header.size -= ret;

    if (self->header.size == 0) {
      if (self->header.magic == SUSCAN_REMOTE_COMPRESSED_PDU_HEADER_MAGIC)
        SU_TRYCATCH(
          suscan_remote_inflate_pdu(&self->incoming_pdu), 
          goto done);

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

SUBOOL
suscan_remote_partial_pdu_state_take(
  struct suscan_remote_partial_pdu_state *self,
  grow_buf_t *pdu)
{
  if (self->have_header && self->have_body) {
    *pdu = self->incoming_pdu;
    memset(&self->incoming_pdu, 0, sizeof(grow_buf_t));

    self->header_ptr  = 0;
    self->have_header = SU_FALSE;
    self->have_body   = SU_FALSE;

    return SU_TRUE;
  }

  return SU_FALSE;
}

void
suscan_remote_partial_pdu_state_finalize(
  struct suscan_remote_partial_pdu_state *self)
{
  grow_buf_finalize(&self->incoming_pdu);
}

SUSCAN_SERIALIZER_PROTO(suscan_analyzer_multicast_info) {
  SUSCAN_PACK_BOILERPLATE_START;

  SUSCAN_PACK(uint, self->multicast_addr);
  SUSCAN_PACK(uint, self->multicast_port);

  SUSCAN_PACK_BOILERPLATE_END;
}

SUSCAN_DESERIALIZER_PROTO(suscan_analyzer_multicast_info) {
  SUSCAN_UNPACK_BOILERPLATE_START;

  SUSCAN_UNPACK(uint32, self->multicast_addr);
  SUSCAN_UNPACK(uint16, self->multicast_port);

  SUSCAN_UNPACK_BOILERPLATE_END;
}


SUSCAN_SERIALIZER_PROTO(suscan_analyzer_server_hello) {
  SUSCAN_PACK_BOILERPLATE_START;

  SUSCAN_PACK(str,  self->server_name);
  SUSCAN_PACK(uint, self->protocol_version_major);
  SUSCAN_PACK(uint, self->protocol_version_minor);
  SUSCAN_PACK(uint, self->auth_mode);
  SUSCAN_PACK(uint, self->enc_type);
  SUSCAN_PACK(blob, self->sha256buf, SHA256_BLOCK_SIZE);
  SUSCAN_PACK(uint, self->flags);

  if (self->flags & SUSCAN_REMOTE_FLAGS_MULTICAST)
    SU_TRYCATCH(
      suscan_analyzer_multicast_info_serialize(&self->mc_info, buffer),
      goto fail);

  SUSCAN_PACK_BOILERPLATE_END;
}

SUSCAN_DESERIALIZER_PROTO(suscan_analyzer_server_hello) {
  SUSCAN_UNPACK_BOILERPLATE_START;
  size_t size = 0;

  SUSCAN_UNPACK(str,   self->server_name);
  SUSCAN_UNPACK(uint8, self->protocol_version_major);
  SUSCAN_UNPACK(uint8, self->protocol_version_minor);
  SUSCAN_UNPACK(uint8, self->auth_mode);
  SUSCAN_UNPACK(uint8, self->enc_type);
  SUSCAN_UNPACK(blob,  self->sha256buf, &size);
  SUSCAN_UNPACK(uint32, self->flags);

  if (size != SHA256_BLOCK_SIZE) {
    SU_ERROR("Invalid salt size %d (expected %d)\n", size, SHA256_BLOCK_SIZE);
    goto fail;
  }

  if (self->flags & SUSCAN_REMOTE_FLAGS_MULTICAST)
    SU_TRYCATCH(
      suscan_analyzer_multicast_info_deserialize(&self->mc_info, buffer),
      goto fail);

  SUSCAN_UNPACK_BOILERPLATE_END;
}

SUBOOL
suscan_analyzer_server_hello_init(
    struct suscan_analyzer_server_hello *self,
    const char *name)
{
  SUBOOL ok = SU_FALSE;
  unsigned int i;

  memset(self, 0, sizeof (struct suscan_analyzer_server_hello));

  SU_TRYCATCH(self->server_name = strdup(name), goto done);
  SU_TRYCATCH(self->sha256salt  = malloc(SHA256_BLOCK_SIZE), goto done);

  self->protocol_version_major = SUSCAN_REMOTE_PROTOCOL_MAJOR_VERSION;
  self->protocol_version_minor = SUSCAN_REMOTE_PROTOCOL_MINOR_VERSION;

  self->auth_mode = SUSCAN_REMOTE_AUTH_MODE_USER_PASSWORD;
  self->enc_type  = SUSCAN_REMOTE_ENC_TYPE_NONE;

  srand(suscan_gettime_raw());

  /* XXX: Find truly random bytes */
  for (i = 0; i < SHA256_BLOCK_SIZE; ++i)
    self->sha256salt[i] = rand();

  ok = SU_TRUE;

done:
  if (!ok)
    suscan_analyzer_server_hello_finalize(self);

  return ok;
}

void
suscan_analyzer_server_hello_finalize(
    struct suscan_analyzer_server_hello *self)
{
  if (self->sha256salt != NULL)
    free(self->sha256salt);

  if (self->server_name)
    free(self->server_name);
}

SUSCAN_SERIALIZER_PROTO(suscan_analyzer_server_client_auth) {
  SUSCAN_PACK_BOILERPLATE_START;

  SUSCAN_PACK(str,  self->client_name);
  SUSCAN_PACK(uint, self->protocol_version_major);
  SUSCAN_PACK(uint, self->protocol_version_minor);
  SUSCAN_PACK(str,  self->user);
  SUSCAN_PACK(blob, self->sha256buf, SHA256_BLOCK_SIZE);
  SUSCAN_PACK(uint, self->flags);

  SUSCAN_PACK_BOILERPLATE_END;
}

SUSCAN_DESERIALIZER_PROTO(suscan_analyzer_server_client_auth) {
  SUSCAN_UNPACK_BOILERPLATE_START;
  size_t size = 0;

  SUSCAN_UNPACK(str,   self->client_name);
  SUSCAN_UNPACK(uint8, self->protocol_version_major);
  SUSCAN_UNPACK(uint8, self->protocol_version_minor);
  SUSCAN_UNPACK(str,   self->user);
  SUSCAN_UNPACK(blob,  self->sha256buf, &size);

  if (size != SHA256_BLOCK_SIZE) {
    SU_ERROR("Invalid token size %d (expected %d)\n", size, SHA256_BLOCK_SIZE);
    goto fail;
  }

  SUSCAN_UNPACK(uint32, self->flags);

  SUSCAN_UNPACK_BOILERPLATE_END;
}

void
suscan_analyzer_server_compute_auth_token(
    uint8_t *result,
    const char *user,
    const char *password,
    const uint8_t *sha256salt)
{
  SHA256_CTX ctx;

  suscan_sha256_init(&ctx);

  suscan_sha256_update(&ctx, (const uint8_t *) user, strlen(user) + 1);
  suscan_sha256_update(&ctx, (const uint8_t *) password, strlen(password) + 1);
  suscan_sha256_update(&ctx, (const uint8_t *) sha256salt, SHA256_BLOCK_SIZE);

  suscan_sha256_final(&ctx, result);
}

SUBOOL
suscan_analyzer_server_client_auth_init(
    struct suscan_analyzer_server_client_auth *self,
    const struct suscan_analyzer_server_hello *hello,
    const char *name,
    const char *user,
    const char *password)
{
  SUBOOL ok = SU_FALSE;

  memset(self, 0, sizeof (struct suscan_analyzer_server_hello));

  SU_TRYCATCH(self->client_name  = strdup(name), goto done);
  SU_TRYCATCH(self->user         = strdup(user), goto done);
  SU_TRYCATCH(self->sha256token  = malloc(SHA256_BLOCK_SIZE), goto done);

  self->protocol_version_major = SUSCAN_REMOTE_PROTOCOL_MAJOR_VERSION;
  self->protocol_version_minor = SUSCAN_REMOTE_PROTOCOL_MINOR_VERSION;

  suscan_analyzer_server_compute_auth_token(
      self->sha256token,
      user,
      password,
      hello->sha256salt);

  ok = SU_TRUE;

done:
  if (!ok)
    suscan_analyzer_server_client_auth_finalize(self);

  return ok;
}

void
suscan_analyzer_server_client_auth_finalize(
    struct suscan_analyzer_server_client_auth *self)
{
  if (self->client_name != NULL)
    free(self->client_name);

  if (self->user != NULL)
    free(self->user);

  if (self->sha256token != NULL)
    free(self->sha256token);
}

SUSCAN_SERIALIZER_PROTO(suscan_analyzer_remote_call)
{
  SUSCAN_PACK_BOILERPLATE_START;

  SUSCAN_PACK(uint, self->type);

  switch (self->type) {
    case SUSCAN_ANALYZER_REMOTE_AUTH_INFO:
      SU_TRYCATCH(
          suscan_analyzer_server_client_auth_serialize(
              &self->client_auth,
              buffer),
          goto fail);
      break;

    case SUSCAN_ANALYZER_REMOTE_SOURCE_INFO:
      SU_TRYCATCH(
          suscan_analyzer_source_info_serialize(&self->source_info, buffer),
          goto fail);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_FREQUENCY:
      SUSCAN_PACK(freq, self->freq);
      SUSCAN_PACK(freq, self->lnb);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_GAIN:
      SUSCAN_PACK(str, self->gain.name);
      SUSCAN_PACK(float, self->gain.value);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_ANTENNA:
      SUSCAN_PACK(str, self->antenna);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_BANDWIDTH:
      SUSCAN_PACK(float, self->bandwidth);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_PPM:
      SUSCAN_PACK(float, self->ppm);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_DC_REMOVE:
      SUSCAN_PACK(bool, self->dc_remove);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_IQ_REVERSE:
      SUSCAN_PACK(bool, self->iq_reverse);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_AGC:
      SUSCAN_PACK(bool, self->agc);
      break;

    case SUSCAN_ANALYZER_REMOTE_FORCE_EOS:
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_SWEEP_STRATEGY:
      SUSCAN_PACK(uint, self->sweep_strategy);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_SPECTRUM_PARTITIONING:
      SUSCAN_PACK(uint, self->spectrum_partitioning);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_HOP_RANGE:
      SUSCAN_PACK(freq, self->hop_range.min);
      SUSCAN_PACK(freq, self->hop_range.max);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_BUFFERING_SIZE:
      SUSCAN_PACK(uint, self->buffering_size);
      break;

    case SUSCAN_ANALYZER_REMOTE_MESSAGE:
      SU_TRYCATCH(
          suscan_analyzer_msg_serialize(self->msg.type, self->msg.ptr, buffer),
          goto fail);
      break;

    case SUSCAN_ANALYZER_REMOTE_REQ_HALT:
      break;

    case SUSCAN_ANALYZER_REMOTE_AUTH_REJECTED:
      break;

    case SUSCAN_ANALYZER_REMOTE_STARTUP_ERROR:
      break;

    default:
      SU_ERROR("Invalid remote call `%d'\n", self->type);
      break;
  }

  SUSCAN_PACK_BOILERPLATE_END;
}

SUSCAN_DESERIALIZER_PROTO(suscan_analyzer_remote_call)
{
  SUSCAN_UNPACK_BOILERPLATE_START;

  SUSCAN_UNPACK(uint32, self->type);

  switch (self->type) {
    case SUSCAN_ANALYZER_REMOTE_AUTH_INFO:
      SU_TRYCATCH(
          suscan_analyzer_server_client_auth_deserialize(
              &self->client_auth,
              buffer),
          goto fail);
      break;

    case SUSCAN_ANALYZER_REMOTE_SOURCE_INFO:
      SU_TRYCATCH(
          suscan_analyzer_source_info_deserialize(&self->source_info, buffer),
          goto fail);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_FREQUENCY:
      SUSCAN_UNPACK(freq, self->freq);
      SUSCAN_UNPACK(freq, self->lnb);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_GAIN:
      SUSCAN_UNPACK(str,   self->gain.name);
      SUSCAN_UNPACK(float, self->gain.value);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_ANTENNA:
      SUSCAN_UNPACK(str, self->antenna);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_BANDWIDTH:
      SUSCAN_UNPACK(float, self->bandwidth);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_PPM:
      SUSCAN_UNPACK(float, self->ppm);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_DC_REMOVE:
      SUSCAN_UNPACK(bool, self->dc_remove);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_IQ_REVERSE:
      SUSCAN_UNPACK(bool, self->iq_reverse);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_AGC:
      SUSCAN_UNPACK(bool, self->agc);
      break;

    case SUSCAN_ANALYZER_REMOTE_FORCE_EOS:
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_SWEEP_STRATEGY:
      SUSCAN_UNPACK(uint32, self->sweep_strategy);
      SU_TRYCATCH(self->sweep_strategy < 2, goto fail);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_SPECTRUM_PARTITIONING:
      SUSCAN_UNPACK(uint32, self->spectrum_partitioning);
      SU_TRYCATCH(self->spectrum_partitioning < 2, goto fail);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_HOP_RANGE:
      SUSCAN_UNPACK(freq, self->hop_range.min);
      SUSCAN_UNPACK(freq, self->hop_range.max);

      SU_TRYCATCH(self->hop_range.min < self->hop_range.max, goto fail);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_BUFFERING_SIZE:
      SUSCAN_UNPACK(uint32, self->buffering_size);
      break;

    case SUSCAN_ANALYZER_REMOTE_MESSAGE:
      SU_TRYCATCH(
          suscan_analyzer_msg_deserialize(
              &self->msg.type,
              &self->msg.ptr,
              buffer),
          goto fail);
      break;

    case SUSCAN_ANALYZER_REMOTE_REQ_HALT:
      break;

    case SUSCAN_ANALYZER_REMOTE_AUTH_REJECTED:
      break;

    case SUSCAN_ANALYZER_REMOTE_STARTUP_ERROR:
      break;

    default:
      SU_ERROR("Invalid remote call `%d'\n", self->type);
      break;
  }

  SUSCAN_UNPACK_BOILERPLATE_END;
}

void
suscan_analyzer_remote_call_init(
    struct suscan_analyzer_remote_call *self,
    enum suscan_analyzer_remote_type type)
{
  memset(self, 0, sizeof(struct suscan_analyzer_remote_call));

  self->type = type;
}

SUBOOL
suscan_analyzer_remote_call_take_source_info(
    struct suscan_analyzer_remote_call *self,
    struct suscan_analyzer_source_info *info)
{
  SU_TRYCATCH(
      self->type == SUSCAN_ANALYZER_REMOTE_SOURCE_INFO,
      return SU_FALSE);

  suscan_analyzer_source_info_finalize(info);
  *info = self->source_info;
  self->type = SUSCAN_ANALYZER_REMOTE_NONE;

  return SU_TRUE;
}

SUBOOL
suscan_analyzer_remote_call_deliver_message(
    struct suscan_analyzer_remote_call *self,
    suscan_remote_analyzer_t *analyzer)
{
  uint32_t type = 0;
  struct suscan_analyzer_psd_msg *psd_msg;
  void *priv = NULL;

  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      self->type == SUSCAN_ANALYZER_REMOTE_MESSAGE,
      return SU_FALSE);

  type = self->msg.type;
  priv = self->msg.ptr;

  /* Certain messages imply an update of the remote state */
  switch (type) {
    case SUSCAN_ANALYZER_MESSAGE_TYPE_SOURCE_INFO:
      /* Source info must be kept in sync. */
      suscan_analyzer_source_info_finalize(&analyzer->source_info);
      SU_TRYCATCH(
          suscan_analyzer_source_info_init_copy(&analyzer->source_info, priv),
          goto done);
      break;

    case SUSCAN_ANALYZER_MESSAGE_TYPE_PSD:
      /* Timestamp is also important */
      psd_msg = priv;
      analyzer->source_info.source_time = psd_msg->timestamp;
      break;
  }
  
  SU_TRYCATCH(
    suscan_mq_write(analyzer->parent->mq_out, type, priv),
    goto done);

  self->type = SUSCAN_ANALYZER_REMOTE_NONE;

  ok = SU_TRUE;

done:
  if (!ok && priv != NULL)
    suscan_analyzer_dispose_message(type, priv);

  return ok;
}


void
suscan_analyzer_remote_call_finalize(struct suscan_analyzer_remote_call *self)
{
  switch (self->type) {
    case SUSCAN_ANALYZER_REMOTE_AUTH_INFO:
      suscan_analyzer_server_client_auth_finalize(&self->client_auth);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_GAIN:
      if (self->gain.name != NULL)
        free(self->gain.name);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_ANTENNA:
      if (self->antenna != NULL)
        free(self->antenna);
      break;

    case SUSCAN_ANALYZER_REMOTE_SOURCE_INFO:
      suscan_analyzer_source_info_finalize(&self->source_info);
      break;

    case SUSCAN_ANALYZER_REMOTE_MESSAGE:
      /* Messages can become null if they are forwarded to the user */
      if (self->msg.ptr != NULL)
        suscan_analyzer_dispose_message(self->msg.type, self->msg.ptr);
      break;
  }

  self->type = SUSCAN_ANALYZER_REMOTE_NONE;
}

/**************************** Network thread **********************************/
size_t
suscan_remote_read(
    int sfd,
    int cancelfd,
    void *buffer,
    size_t size,
    int timeout_ms)
{
  uint8_t *as_bytes;
  char cancel_byte;
  size_t got = 0;
  size_t ret = 0;
  struct pollfd fds[2];

  fds[0].events  = POLLIN;
  fds[0].fd      = sfd;
  fds[0].revents = 0;

  fds[1].events  = POLLIN;
  fds[1].fd      = cancelfd;
  fds[1].revents = 0;

  as_bytes = (uint8_t *) buffer;

  while (size > 0) {
    SU_TRYCATCH(poll(fds, 2, timeout_ms) != -1, return -1);

    if (fds[1].revents & POLLIN) {
      IGNORE_RESULT(int, read(cancelfd, &cancel_byte, 1));
      errno = ECANCELED;
      return -1;
    } else if (fds[0].revents & POLLIN) {
      ret = read(sfd, as_bytes, size);

      if (ret == 0)
        goto done;
      else if (ret == -1)
        return -1; /* Connection error, return immediately */

      got      += ret;
      as_bytes += ret;
      size     -= ret;
    } else {
      /* Connection error due to timeout, return immediately */
      errno = ETIMEDOUT;
      return -1;
    }
  }

done:
  return got;
}

SUBOOL
suscan_remote_deflate_pdu(grow_buf_t *buffer, grow_buf_t *dest)
{
  z_stream stream;
  grow_buf_t tmpbuf      = grow_buf_INITIALIZER;
  grow_buf_t swapbuf;
  uint8_t *buffer_bytes  = grow_buf_get_buffer(buffer);
  size_t   buffer_size   = grow_buf_get_size(buffer);
  uint8_t *output        = NULL;
  uint32_t avail_size;
  SUBOOL   deflate_init  = SU_TRUE;
  int      last_err      = Z_OK;
  int      flush         = Z_NO_FLUSH;
  SUBOOL   ok = SU_FALSE;

  if (dest == NULL)
    dest = &tmpbuf;

  SU_TRYCATCH(grow_buf_get_size(dest) == 0, goto done);
  SU_TRYCATCH(output = grow_buf_alloc(dest, sizeof(uint32_t) + 5), goto done);

  /* Step 1: allocate and compress */
  stream.zalloc   = Z_NULL;
  stream.zfree    = Z_NULL;
  stream.opaque   = Z_NULL;

  stream.next_in   = buffer_bytes;
  stream.avail_in  = buffer_size;

  stream.next_out  = output + sizeof(uint32_t);
  stream.avail_out = grow_buf_get_size(dest) - sizeof(uint32_t);

  SU_TRYCATCH(
    deflateInit(&stream, 9) == Z_OK,
    goto done);

  deflate_init     = SU_TRUE;
  /* Begin compression */
  while ((last_err = deflate(&stream, flush)) == Z_OK) {
    /* Buffer was not completely consumed, reallocate */

    if (stream.avail_out == 0) {
      avail_size = grow_buf_get_size(dest);

      if (avail_size > 4 * buffer_size) {
        SU_ERROR("Compressed buffer grew beyond a reasonable size.\n");
        goto done;
      }

      SU_TRYCATCH(
          output = grow_buf_alloc(dest, avail_size), 
          goto done);

      /* Update stream properties */
      stream.next_out  = output;
      stream.avail_out = avail_size;
    }

    if (stream.total_in == buffer_size)
      flush = Z_FINISH;
  }

  SU_TRYCATCH(last_err == Z_STREAM_END, goto done);

  /* TODO: Expose API!! */
  dest->size = stream.total_out + sizeof(uint32_t);

  /* Step 2: update PDU size */
  output = grow_buf_get_buffer(dest);
  *(uint32_t *) output = htonl(buffer_size);

  if (dest == &tmpbuf) {
    swapbuf = tmpbuf;
    tmpbuf  = *buffer;
    *buffer = swapbuf;
  }

  ok = SU_TRUE;

done:
  if (deflate_init)
    deflateEnd(&stream);

  grow_buf_finalize(&tmpbuf);

  return ok;
}

SUBOOL
suscan_remote_inflate_pdu(grow_buf_t *buffer)
{
  uint32_t cmpsize;
  uint8_t *cmpbytes;
  uint32_t size;
  z_stream stream;
  uint8_t *output;
  uint32_t out_alloc;
  grow_buf_t swapbuf;
  grow_buf_t tmpbuf = grow_buf_INITIALIZER;
  int last_err;
  int flush = Z_NO_FLUSH;
  SUBOOL inflate_init = SU_FALSE;
  SUBOOL ok = SU_FALSE;

  cmpsize  = grow_buf_get_size(buffer);
  cmpbytes = grow_buf_get_buffer(buffer);

  if (cmpsize <= sizeof(uint32_t)) {
    SU_ERROR("Compressed frame too short\n");
    goto done;
  }

  size = ntohl(*(uint32_t *) cmpbytes);

  cmpsize  -= sizeof(uint32_t);
  cmpbytes += sizeof(uint32_t);

  /* Initialize buffers */
  stream.zalloc = Z_NULL;
  stream.zfree  = Z_NULL;
  stream.opaque = Z_NULL;

  out_alloc = cmpsize;
  
  SU_TRYCATCH(
    output = grow_buf_alloc(&tmpbuf, out_alloc), 
    goto done);

  stream.next_in   = cmpbytes;
  stream.avail_in  = cmpsize;

  stream.next_out  = output;
  stream.avail_out = out_alloc;

  /* Initialize inflate */
  SU_TRYCATCH(inflateInit(&stream) == Z_OK, goto done);
  inflate_init     = SU_TRUE;

  /* Begin decompression */
  while ((last_err = inflate(&stream, flush)) == Z_OK) {
    if (stream.avail_out == 0) {
      /* Buffer was not completely consumed, reallocate */
      out_alloc = grow_buf_get_size(&tmpbuf);

      if (grow_buf_get_size(&tmpbuf) + out_alloc > size)
        out_alloc = size - grow_buf_get_size(&tmpbuf);

      if (out_alloc > 0) {
        SU_TRYCATCH(
        output = grow_buf_alloc(&tmpbuf, out_alloc), 
        goto done);
      } else {
        output = Z_NULL;
      }
      
      stream.next_out  = output;
      stream.avail_out = out_alloc;
    }

    if (stream.total_in == cmpsize)
      flush = Z_FINISH;
  }

  if (last_err != Z_STREAM_END) {
    SU_ERROR(
      "Inflate error %d (%d/%d bytes decompressed, corrupted data?)\n", 
      last_err,
      stream.total_out,
      size);
    SU_ERROR("%02x %02x %02x %02x\n", cmpbytes[0], cmpbytes[1], cmpbytes[2], cmpbytes[3]);
    SU_ERROR("Consumed: %d/%d\n", cmpsize - stream.avail_in, cmpsize);
    goto done;
  }

  if (size != stream.total_out) {
    SU_ERROR(
      "Inflated packet size mismatch (%d != %d)\n", 
      stream.total_out, 
      size);
    goto done;
  }

  /* Swap these */
  swapbuf = *buffer;
  *buffer = tmpbuf;
  tmpbuf  = swapbuf;

  ok = SU_TRUE;

done:
  if (inflate_init)
    inflateEnd(&stream);

  grow_buf_finalize(&tmpbuf);

  return ok;
}

SUBOOL
suscan_remote_read_pdu(
    int sfd,
    int cancelfd,
    grow_buf_t *buffer,
    int timeout_ms)
{
  uint32_t chunksiz;
  struct suscan_analyzer_remote_pdu_header header;
  SUBOOL compressed = SU_FALSE;
  void *chunk;
  size_t got;
  SUBOOL ok = SU_FALSE;

  grow_buf_clear(buffer);

  /* Attempt to read header */
  got = suscan_remote_read(
    sfd,
    cancelfd,
    &header,
    sizeof(struct suscan_analyzer_remote_pdu_header),
    timeout_ms);
    
  if (got != sizeof(struct suscan_analyzer_remote_pdu_header)) {
    if (got >= 0)
      SU_WARNING("Connection closed while waiting for PDU header\n");
    else
      SU_ERROR(
        "suscan_remote_read returned %d (errno = %s)\n",
        got,
        strerror(errno));
    goto done;
  }

  header.size  = ntohl(header.size);
  header.magic = ntohl(header.magic);

  switch (header.magic) {
    case SUSCAN_REMOTE_PDU_HEADER_MAGIC:
      break;

    case SUSCAN_REMOTE_COMPRESSED_PDU_HEADER_MAGIC:
      compressed = SU_TRUE;
      break;

    default:
      SU_ERROR("Protocol error (unrecognized PDU magic)\n");
      goto done;
  }

  /* Start to read */
  while (header.size > 0) {
    chunksiz = header.size;
    if (chunksiz > SUSCAN_REMOTE_READ_BUFFER)
      chunksiz = SUSCAN_REMOTE_READ_BUFFER;

    SU_TRYCATCH(chunk = grow_buf_alloc(buffer, chunksiz), goto done);
    got = suscan_remote_read(
      sfd,
      cancelfd,
      chunk,
      chunksiz,
      SUSCAN_REMOTE_ANALYZER_PDU_BODY_TIMEOUT_MS);
      
    if (got != chunksiz) {
      if (got >= 0)
        SU_WARNING("Connection closed while waiting for PDU payload\n");
      else
        SU_ERROR(
          "suscan_remote_read returned %d while reading PDU payload (errno = %s)\n",
          got,
          strerror(errno));
      goto done;
    }

    /*
     * No need to advance growbuf pointer. We are just incrementing
     * its size.
     */
    header.size -= chunksiz;
  }

  if (compressed)
    SU_TRYCATCH(suscan_remote_inflate_pdu(buffer), goto done);
    
  ok = SU_TRUE;

done:
  return ok;
}

SUINLINE SUBOOL
suscan_remote_write_pdu_internal(
    int sfd,
    uint32_t magic,
    const grow_buf_t *buffer)
{
  uint8_t *buffer_bytes = grow_buf_get_buffer(buffer);
  size_t   buffer_size  = grow_buf_get_size(buffer);
  size_t   chunksize;

  struct suscan_analyzer_remote_pdu_header header;

  header.magic = htonl(magic);
  header.size  = htonl(buffer_size);

  if (write(sfd, &header, sizeof(struct suscan_analyzer_remote_pdu_header))
      != sizeof(struct suscan_analyzer_remote_pdu_header)) {
    SU_ERROR("Protocol header write error\n");
    return SU_FALSE;
  }

  while (buffer_size > 0) {
    chunksize = buffer_size;

    if (chunksize > SUSCAN_REMOTE_READ_BUFFER)
      chunksize = SUSCAN_REMOTE_READ_BUFFER;

    if (write(sfd, buffer_bytes, chunksize) != chunksize) {
      SU_ERROR("Protocol body write error\n");
      return SU_FALSE;
    }

    buffer_size  -= chunksize;
    buffer_bytes += chunksize;
  }

  return SU_TRUE;
}

SUINLINE SUBOOL
suscan_remote_write_compressed_pdu(
    int sfd,
    const grow_buf_t *buffer)
{
  grow_buf_t compressed = grow_buf_INITIALIZER;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
    suscan_remote_deflate_pdu((grow_buf_t *) buffer, &compressed),
    goto done);

  SU_TRYCATCH(
    suscan_remote_write_pdu_internal(
      sfd, 
      SUSCAN_REMOTE_COMPRESSED_PDU_HEADER_MAGIC, 
      &compressed),
    goto done);

  ok = SU_TRUE;

done:
  grow_buf_finalize(&compressed);

  return ok;
}

SUBOOL
suscan_remote_write_pdu(
    int sfd,
    const grow_buf_t *buffer,
    unsigned int threshold)
{
  if (threshold > 0 && grow_buf_get_size(buffer) > threshold)
    return suscan_remote_write_compressed_pdu(sfd, buffer);
  else
    return suscan_remote_write_pdu_internal(
      sfd, 
      SUSCAN_REMOTE_PDU_HEADER_MAGIC, 
      buffer);
}

/*
 * User is in charge of releasing the pointer
 */
SUPRIVATE struct suscan_analyzer_remote_call *
suscan_remote_analyzer_receive_call(
    suscan_remote_analyzer_t *self,
    int sfd,
    int cancelfd,
    SUBOOL mc,
    int timeout_ms)
{
  struct suscan_analyzer_remote_call *call = NULL, *qcall = NULL;
  uint32_t type;
  uint8_t *read_buf = self->peer.pdu_state.read_buffer;
  struct sockaddr_in addr;
  grow_buf_t buf = grow_buf_INITIALIZER;
  int n = 2, active;
  socklen_t len = sizeof(struct sockaddr_in);
  ssize_t ret;
  struct pollfd fds[3];
  SUBOOL ok = SU_FALSE;

  memset(&addr, 0, len);

  fds[0].fd      = cancelfd;
  fds[0].events  = POLLIN;
  fds[0].revents = 0;

  fds[1].fd      = sfd;
  fds[1].events  = POLLIN;
  fds[1].revents = 0;

  if (mc && self->peer.mc_processor != NULL) {
    fds[2].fd      = self->peer.mc_fd;
    fds[2].events  = POLLIN;
    fds[2].revents = 0;

    ++n;
  }

  while (call == NULL) {
    /* 
     * Check whether there are any pending calls in the multicast
     * call queue.
     */
    if (suscan_mq_poll(&self->peer.call_queue, &type, (void **) &qcall)) {
      call = suscan_remote_analyzer_acquire_call(
                self,
                SUSCAN_ANALYZER_REMOTE_NONE);
      memcpy(call, qcall, sizeof(struct suscan_analyzer_remote_call));
      free(qcall);
      qcall = NULL;
      break;
    }

    /* No calls. Wait for data. */
    SU_TRYC(active = poll(fds, n, timeout_ms));

    /* Timeout */
    if (active == 0)
      return NULL;

    /* Explicit cancellation */
    if (fds[0].revents & POLLIN)
      return NULL;

    /* Data from the control socket */
    if (fds[1].revents & POLLIN) {
      SU_TRY(suscan_remote_partial_pdu_state_read(
        &self->peer.pdu_state,
        self->peer.hostname,
        sfd));

      /* Complete PDU */
      if (suscan_remote_partial_pdu_state_take(&self->peer.pdu_state, &buf)) {
          call = suscan_remote_analyzer_acquire_call(
                self,
                SUSCAN_ANALYZER_REMOTE_NONE);
          SU_TRY(suscan_analyzer_remote_call_deserialize(call, &buf));
          break;
      }
    }

    /* Data from the multicast interface */
    if (n > 2 && (fds[2].revents & POLLIN)) {
      ret = recvfrom(
        self->peer.mc_fd,
        (void *) read_buf,
        SUSCAN_REMOTE_READ_BUFFER,
        0,
        (struct sockaddr *) &addr,
        &len);
      
      if (ret > 0)
        SU_TRY(
          suscli_multicast_processor_process_datagram(
            self->peer.mc_processor,
            read_buf,
            ret));

      /* Loop now */
    }
  }

  ok = SU_TRUE;

done:
  if (qcall != NULL) {
    suscan_analyzer_remote_call_finalize(qcall);
    free(qcall);
  }

  if (!ok && call != NULL) {
      (void) suscan_remote_analyzer_release_call(self, call);
      call = NULL;
  }

  grow_buf_finalize(&buf);
  
  return call;
}

SUPRIVATE SUBOOL
suscan_remote_analyzer_deliver_call(
    suscan_remote_analyzer_t *self,
    int sfd,
    struct suscan_analyzer_remote_call *call)
{
  SUBOOL serialized = SU_FALSE;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      suscan_analyzer_remote_call_serialize(call, &self->peer.write_buffer),
      goto done);
  serialized = SU_TRUE;

  SU_TRYCATCH(suscan_remote_analyzer_release_call(self, call), goto done);

  SU_TRYCATCH(
      suscan_remote_write_pdu(
        self->peer.control_fd, 
        &self->peer.write_buffer,
        0),
      goto done);

  ok = SU_TRUE;

done:
  if (!serialized)
    (void) suscan_remote_analyzer_release_call(self, call);

  return ok;
}

SUPRIVATE int
suscan_remote_analyzer_network_connect_cancellable(
    struct in_addr ipaddr,
    uint16_t port,
    int cancelfd,
    int timeout_ms)
{
  struct sockaddr_in addr;
  struct pollfd fds[2];

  int sfd = -1;
  int ret = -1;
  int sockerr;
  socklen_t socklen;
  int flags;
  char cancel_byte;

  SU_TRYCATCH((sfd = socket(AF_INET, SOCK_STREAM, 0)) != -1, goto done);

  addr.sin_family = AF_INET;
  addr.sin_addr   = ipaddr;
  addr.sin_port   = htons(port);

  SU_TRYCATCH((flags = fcntl(sfd, F_GETFL, NULL)) != -1, goto done);
  flags |= O_NONBLOCK;
  SU_TRYCATCH(fcntl(sfd, F_SETFL, flags) != -1, goto done);

  ret = connect(sfd, (struct sockaddr *) &addr, sizeof(struct sockaddr_in));

  if (ret == -1) {
    SU_TRYCATCH(errno == EINPROGRESS, goto done);

    /* Inspect the socket. Connection is ready as soon as we can write on it */
    fds[0].events = POLLOUT;
    fds[0].fd     = sfd;

    /* Also inspect the cancellation fd. It being readable means cancellation */
    fds[1].events = POLLIN;
    fds[1].fd     = cancelfd;

    ret = poll(fds, 2, timeout_ms);

    switch (ret) {
      case 0:
        /* None ready, timed out */
        ret = -1;
        errno = ETIMEDOUT;
        goto done;

      case -1:
        /* Poll failed (socket weirdness?) */
        goto done;

      default:
        if (fds[1].revents & POLLIN) {
          /* Cancel requested */
          IGNORE_RESULT(int, read(cancelfd, &cancel_byte, 1));
          ret = -1;
          errno = ECANCELED;
          goto done;
        } else if (fds[0].revents & POLLOUT) {
          sockerr = 0;
          socklen = sizeof(int);

          SU_TRYCATCH(
              getsockopt(sfd, SOL_SOCKET, SO_ERROR, &sockerr, &socklen) != -1,
              goto done);

          if (sockerr != 0) {
            ret = -1;
            errno = sockerr;
            goto done;
          }
        } else {
          SU_ERROR("Invalid socket condition\n");
          ret = -1;
          goto done;
        }
    }
  }

  ret = sfd;
  sfd = -1;

  /*
   * Socket is left in nonblock mode. This is mandatory in order to perform
   * polling between multiple descriptors, including the cancellation
   * descriptor.
   */
done:
  if (sfd != -1)
    close(sfd);

  return ret;
}

SUPRIVATE enum suscan_remote_analyzer_auth_result
suscan_remote_analyzer_auth_peer(suscan_remote_analyzer_t *self)
{
  struct suscan_analyzer_remote_call *call = NULL;
  struct suscan_analyzer_server_hello hello;
  char hostname[64];
  SUBOOL write_ok = SU_FALSE;
  enum suscan_remote_analyzer_auth_result result =
      SUSCAN_REMOTE_ANALYZER_AUTH_RESULT_INVALID_SERVER;

  memset(&hello, 0, sizeof(struct suscan_analyzer_server_hello));

  SU_TRYCATCH(
      suscan_remote_read_pdu(
          self->peer.control_fd,
          self->cancel_pipe[0],
          &self->peer.read_buffer,
          SUSCAN_REMOTE_ANALYZER_AUTH_TIMEOUT_MS),
      goto done);

  SU_TRYCATCH(
      suscan_analyzer_server_hello_deserialize(&hello, &self->peer.read_buffer),
      goto done);

  if (hello.protocol_version_major < SUSCAN_REMOTE_PROTOCOL_MAJOR_VERSION) {
    result = SUSCAN_REMOTE_ANALYZER_AUTH_RESULT_INCOMPATIBLE_VERSION;
    SU_ERROR(
        "Remote server is too old (protocol version %d.%d)\n",
        hello.protocol_version_major,
        hello.protocol_version_minor);
    goto done;
  } else if (hello.protocol_version_major == 0) {
    /* These are the experimental protocols */
    SU_WARNING(
      "Server hello declares an experimental SuRPC protocol version (0.x)\n");
    SU_WARNING(
      "Protocol specification may change any time between releases without\n");
    SU_WARNING(
      "further notice. Make sure both client and server versions match after\n");
    SU_WARNING(
      "upgrading Suscan from the develop branch.\n");

    if (hello.protocol_version_minor < SUSCAN_REMOTE_PROTOCOL_MINOR_VERSION) {
      result = SUSCAN_REMOTE_ANALYZER_AUTH_RESULT_INCOMPATIBLE_VERSION;
      SU_ERROR(
        "Server protocol version is too old (%d.%d). Please upgrade server.\n",
        hello.protocol_version_major,
        hello.protocol_version_minor);
      goto done;
    } else if (hello.protocol_version_minor > SUSCAN_REMOTE_PROTOCOL_MINOR_VERSION) {
      result = SUSCAN_REMOTE_ANALYZER_AUTH_RESULT_INCOMPATIBLE_VERSION;
      SU_ERROR(
        "Server protocol version is too recent (%d.%d). Please upgrade client.\n",
        hello.protocol_version_major,
        hello.protocol_version_minor);
      goto done;
    }
  }
  
  if (self->peer.mc_processor != NULL) {
    if (!(hello.flags & SUSCAN_REMOTE_FLAGS_MULTICAST)) {
      SU_WARNING("Server does not support multicast, disabling\n");
      suscli_multicast_processor_destroy(self->peer.mc_processor);
      self->peer.mc_processor = NULL;

      close(self->peer.mc_fd);
      self->peer.mc_fd = -1;
    } else {
      SU_INFO("Remote server reports multicast support.\n");
    }
  } else {
    SU_INFO("Multicast support not enabled, running in unicast mode\n");
  }

  SU_TRYCATCH(
      call = suscan_remote_analyzer_acquire_call(
          self,
          SUSCAN_ANALYZER_REMOTE_AUTH_INFO),
      goto done);

  /* Prepare authentication message */
  (void) gethostname(hostname, sizeof(hostname));
  hostname[sizeof(hostname) - 1] = '\0';

  SU_TRYCATCH(
      suscan_analyzer_server_client_auth_init(
          &call->client_auth,
          &hello,
          hostname,
          self->peer.user,
          self->peer.password),
      goto done);

  if (self->peer.mc_processor != NULL)
    call->client_auth.flags |= SUSCAN_REMOTE_FLAGS_MULTICAST;

  write_ok = suscan_remote_analyzer_deliver_call(
      self,
      self->peer.control_fd,
      call);
  call = NULL;

  result = SUSCAN_REMOTE_ANALYZER_AUTH_RESULT_DISCONNECTED;

  SU_TRYCATCH(write_ok, goto done);

  SU_TRYCATCH(
      call = suscan_remote_analyzer_receive_call(
          self,
          self->peer.control_fd,
          self->cancel_pipe[0],
          SU_FALSE,
          SUSCAN_REMOTE_ANALYZER_AUTH_TIMEOUT_MS),
      goto done);

  /* Check server response */
  if (call->type != SUSCAN_ANALYZER_REMOTE_SOURCE_INFO) {
    switch (call->type) {
      case SUSCAN_ANALYZER_REMOTE_AUTH_REJECTED:
        result = SUSCAN_REMOTE_ANALYZER_AUTH_RESULT_REJECTED;
        break;

      case SUSCAN_ANALYZER_REMOTE_STARTUP_ERROR:
        result = SUSCAN_REMOTE_ANALYZER_AUTH_RESULT_STARTUP_ERROR;
        break;

      default:
        SU_WARNING(
          "Unexpected server message type %d, incompatible versions?",
          call->type);
        result = SUSCAN_REMOTE_ANALYZER_AUTH_RESULT_INCOMPATIBLE_VERSION;
        break;
    }

    goto done;
  }

  SU_INFO("Authentication successful, source info received\n");

  SU_TRYCATCH(
      suscan_analyzer_remote_call_take_source_info(
          call,
          &self->source_info),
      goto done);
  SU_TRYCATCH(
      suscan_analyzer_send_source_info(self->parent, &self->source_info),
      goto done);

  /* TODO: Warn client about new source info */
  suscan_remote_analyzer_release_call(self, call);
  call = NULL;

  result = SUSCAN_REMOTE_ANALYZER_AUTH_RESULT_SUCCESS;

done:
  if (call != NULL)
    suscan_remote_analyzer_release_call(self, call);

  suscan_analyzer_server_hello_finalize(&hello);

  return result;
}

SUPRIVATE SUBOOL
suscan_remote_analyzer_on_mc_call(
    struct suscli_multicast_processor *mc,
    void *userdata,
    struct suscan_analyzer_remote_call *call)
{
  suscan_remote_analyzer_t *self = 
    (suscan_remote_analyzer_t *) userdata;
  struct suscan_analyzer_remote_call *copy = NULL;
  SUBOOL ok = SU_FALSE;

  /* New call! Queue and process later */
  SU_ALLOCATE(copy, struct suscan_analyzer_remote_call);

  memcpy(copy, call, sizeof(struct suscan_analyzer_remote_call));
  memset(call,    0, sizeof(struct suscan_analyzer_remote_call));

  SU_TRY(suscan_mq_write(&self->peer.call_queue, 1, copy));
  copy = NULL;

  ok = SU_TRUE;

done:
  if (copy != NULL) {
    suscan_analyzer_remote_call_finalize(copy);
    free(copy);
  }

  return ok;
}
    
SUBOOL
suscan_remote_analyzer_open_multicast(
  suscan_remote_analyzer_t *self)
{
  const char *iface = self->peer.mc_if;
  struct sockaddr_in addr;
  struct ip_mreq     group;
  int reuse = 1;
  SUBOOL ok = SU_FALSE;

  SU_TRYC(self->peer.mc_fd = socket(AF_INET, SOCK_DGRAM, 0));

  SU_TRYC(
      setsockopt(
          self->peer.mc_fd,
          SOL_SOCKET,
          SO_REUSEADDR,
          (char *) &reuse,
          sizeof(int)));

  memset(&addr, 0, sizeof(struct sockaddr_in));

  addr.sin_family      = AF_INET;
  addr.sin_port        = htons(SUSCLI_MULTICAST_PORT);
  addr.sin_addr.s_addr = INADDR_ANY;

  SU_TRYC(
      bind(
          self->peer.mc_fd,
          (struct sockaddr *) &addr,
          sizeof(struct sockaddr)));

  group.imr_multiaddr.s_addr = inet_addr(SUSCLI_MULTICAST_GROUP);
  group.imr_interface.s_addr = suscan_ifdesc_to_addr(iface);

  if (ntohl(group.imr_interface.s_addr) == 0xffffffff) {
    SU_ERROR(
        "Invalid interface address `%s' (does not look like a valid IP address)\n",
        iface);
    goto done;
  }

  if ((ntohl(group.imr_interface.s_addr) & 0xf0000000) == 0xe0000000) {
    SU_ERROR("Invalid interface address. Please note that mc_if= "
        "expects the IP address of a configured local network interface, not a "
        "multicast group.\n");
    goto done;
  }

  if (setsockopt(
          self->peer.mc_fd,
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

    goto done;
  }

  /* All set to initialize a multicast processor */
  SU_MAKE(
    self->peer.mc_processor,
    suscli_multicast_processor,
    suscan_remote_analyzer_on_mc_call,
    self);

  ok = SU_TRUE;

done:
  if (!ok) {
    if (self->peer.mc_fd != -1)
      close(self->peer.mc_fd);
    
    self->peer.mc_fd = -1;
  }

  return ok;
}

SUPRIVATE SUBOOL
suscan_remote_analyzer_connect_to_peer(suscan_remote_analyzer_t *self)
{
  struct hostent *ent;
  enum suscan_remote_analyzer_auth_result auth_result;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      suscan_analyzer_send_status(
          self->parent,
          SUSCAN_ANALYZER_MESSAGE_TYPE_SOURCE_INIT,
          SUSCAN_ANALYZER_INIT_PROGRESS,
          "Resolving remote host `%s'...",
          self->peer.hostname),
      goto done);

  if ((ent = gethostbyname(self->peer.hostname)) == NULL
      || ent->h_length == 0) {
    (void) suscan_analyzer_send_status(
        self->parent,
        SUSCAN_ANALYZER_MESSAGE_TYPE_SOURCE_INIT,
        SUSCAN_ANALYZER_INIT_FAILURE,
        "Cannot resolve host `%s'",
        self->peer.hostname);
    goto done;
  }

  self->peer.hostaddr = *((struct in_addr *) ent->h_addr_list[0]);

  if (self->peer.mc_if != NULL) {
    if (!suscan_remote_analyzer_open_multicast(self)) {
      SU_WARNING("Failed to initialize multicast support\n");
      SU_WARNING("Multicast features will be disabled\n");
    }
  }

  SU_TRYCATCH(
      suscan_analyzer_send_status(
          self->parent,
          SUSCAN_ANALYZER_MESSAGE_TYPE_SOURCE_INIT,
          SUSCAN_ANALYZER_INIT_PROGRESS,
          "Host name resolved, connecting to control server on port %d...",
          self->peer.port),
      goto done);

  self->peer.control_fd = suscan_remote_analyzer_network_connect_cancellable(
      self->peer.hostaddr,
      self->peer.port,
      self->cancel_pipe[0],
      SUSCAN_REMOTE_ANALYZER_CONNECT_TIMEOUT_MS);

  if (self->peer.control_fd == -1) {
    (void) suscan_analyzer_send_status(
        self->parent,
        SUSCAN_ANALYZER_MESSAGE_TYPE_SOURCE_INIT,
        SUSCAN_ANALYZER_INIT_FAILURE,
        "Cannot connect to %s:%d (TCP): %s",
        self->peer.hostname,
        self->peer.port,
        strerror(errno));
    goto done;
  }

  SU_TRYCATCH(
    suscan_analyzer_send_status(
        self->parent,
        SUSCAN_ANALYZER_MESSAGE_TYPE_SOURCE_INIT,
        SUSCAN_ANALYZER_INIT_PROGRESS,
        "Connection successful. Authenticating against peer...",
        self->peer.port),
    goto done);

  auth_result = suscan_remote_analyzer_auth_peer(self);

  switch (auth_result) {
    case SUSCAN_REMOTE_ANALYZER_AUTH_RESULT_DISCONNECTED:
      (void) suscan_analyzer_send_status(
          self->parent,
          SUSCAN_ANALYZER_MESSAGE_TYPE_SOURCE_INIT,
          SUSCAN_ANALYZER_INIT_FAILURE,
          "Connection reset during authentication");
      goto done;

    case SUSCAN_REMOTE_ANALYZER_AUTH_RESULT_INCOMPATIBLE_VERSION:
      (void) suscan_analyzer_send_status(
          self->parent,
          SUSCAN_ANALYZER_MESSAGE_TYPE_SOURCE_INIT,
          SUSCAN_ANALYZER_INIT_FAILURE,
          "Incompatible server protocol");
      goto done;

    case SUSCAN_REMOTE_ANALYZER_AUTH_RESULT_INVALID_SERVER:
      (void) suscan_analyzer_send_status(
          self->parent,
          SUSCAN_ANALYZER_MESSAGE_TYPE_SOURCE_INIT,
          SUSCAN_ANALYZER_INIT_FAILURE,
          "Connection opened, but host is not a valid Suscan device server");
      goto done;

    case SUSCAN_REMOTE_ANALYZER_AUTH_RESULT_REJECTED:
      (void) suscan_analyzer_send_status(
          self->parent,
          SUSCAN_ANALYZER_MESSAGE_TYPE_SOURCE_INIT,
          SUSCAN_ANALYZER_INIT_FAILURE,
          "Authentication rejected (wrong user and/or password?)");
      goto done;

    case SUSCAN_REMOTE_ANALYZER_AUTH_RESULT_STARTUP_ERROR:
      (void) suscan_analyzer_send_status(
          self->parent,
          SUSCAN_ANALYZER_MESSAGE_TYPE_SOURCE_INIT,
          SUSCAN_ANALYZER_INIT_FAILURE,
          "Server-side analyzer failed to start (remote device error?)");
      goto done;

    case SUSCAN_REMOTE_ANALYZER_AUTH_RESULT_SUCCESS:
      break;
  }

  SU_TRYCATCH(
    suscan_analyzer_send_status(
        self->parent,
        SUSCAN_ANALYZER_MESSAGE_TYPE_SOURCE_INIT,
        SUSCAN_ANALYZER_INIT_SUCCESS,
        NULL),
    goto done);


  ok = SU_TRUE;

done:
  if (!ok)
    usleep(1000);

  return ok;
}


SUPRIVATE void *
suscan_remote_analyzer_rx_thread(void *ptr)
{
  suscan_remote_analyzer_t *self = (suscan_remote_analyzer_t *) ptr;

  struct suscan_analyzer_remote_call *call;

  while ((call = suscan_remote_analyzer_receive_call(
      self,
      self->peer.control_fd,
      self->cancel_pipe[0],
      SU_TRUE,
      -1)) != NULL) {
    switch (call->type) {
      case SUSCAN_ANALYZER_REMOTE_SOURCE_INFO:
        SU_TRYCATCH(
            suscan_analyzer_remote_call_take_source_info(
                call,
                &self->source_info),
            goto done);

        SU_TRYCATCH(
            suscan_analyzer_send_source_info(self->parent, &self->source_info),
            goto done);
        break;

      case SUSCAN_ANALYZER_REMOTE_FORCE_EOS:
        self->parent->eos = SU_TRUE;
        suscan_analyzer_send_status(
            self->parent,
            SUSCAN_ANALYZER_MESSAGE_TYPE_EOS,
            0,
            "End of stream reached");
        goto done;

      case SUSCAN_ANALYZER_REMOTE_MESSAGE:
        SU_TRYCATCH(
            suscan_analyzer_remote_call_deliver_message(call, self),
            goto done);
        break;
    }

    suscan_remote_analyzer_release_call(self, call);
    call = NULL;
  }

done:
  if (call != NULL)
    suscan_remote_analyzer_release_call(self, call);

  (void) suscan_mq_write(&self->pdu_queue, SUSCAN_REMOTE_HALT, NULL);

  return NULL;
}

SUPRIVATE void *
suscan_remote_analyzer_tx_thread(void *ptr)
{
  suscan_remote_analyzer_t *self = (suscan_remote_analyzer_t *) ptr;
  uint32_t is_ctl = 0;
  grow_buf_t *as_growbuf = NULL;
  void *msgptr = NULL;

  SU_TRYCATCH(suscan_remote_analyzer_connect_to_peer(self), goto done);

  SU_TRYCATCH(
      pthread_create(
          &self->rx_thread,
          NULL,
          suscan_remote_analyzer_rx_thread,
          self) != -1,
      goto done);
  self->rx_thread_init = SU_TRUE;

  while ((msgptr = suscan_mq_read(&self->pdu_queue, &is_ctl)) != NULL) {
    switch (is_ctl) {
      case SU_TRUE:
      case SU_FALSE:
        as_growbuf = (grow_buf_t *) msgptr;

        /* We only support control messages for now */
        SU_TRYCATCH(
            suscan_remote_write_pdu(
              self->peer.control_fd, 
              msgptr,
              0 /* temptatively disabled */),
            goto done);

        grow_buf_finalize(as_growbuf);
        free(as_growbuf);
        as_growbuf = NULL;
        break;

      case SUSCAN_REMOTE_HALT:
        goto done;
    }
  }

done:
  self->parent->running = SU_FALSE;

  if (as_growbuf != NULL) {
    grow_buf_finalize(as_growbuf);
    free(as_growbuf);
  }

  suscan_mq_write_urgent(
      self->parent->mq_out,
      SUSCAN_WORKER_MSG_TYPE_HALT,
      NULL);

  return NULL;
}

/***************************** Call queueing **********************************/
struct suscan_analyzer_remote_call *
suscan_remote_analyzer_acquire_call(
    suscan_remote_analyzer_t *self,
    enum suscan_analyzer_remote_type type)
{
  SU_TRYCATCH(pthread_mutex_lock(&self->call_mutex) == 0, return NULL);

  suscan_analyzer_remote_call_init(&self->call, type);

  return &self->call;
}

SUBOOL
suscan_remote_analyzer_release_call(
    suscan_remote_analyzer_t *self,
    struct suscan_analyzer_remote_call *call)
{
  SU_TRYCATCH(call == &self->call, return SU_FALSE);

  suscan_analyzer_remote_call_finalize(&self->call);

  SU_TRYCATCH(pthread_mutex_unlock(&self->call_mutex) == 0, return SU_FALSE);

  return SU_TRUE;
}


SUBOOL
suscan_remote_analyzer_queue_call(
    suscan_remote_analyzer_t *self,
    struct suscan_analyzer_remote_call *call,
    SUBOOL is_control)
{
  grow_buf_t *buf = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(buf = calloc(1, sizeof(grow_buf_t)), goto done);
  SU_TRYCATCH(
      suscan_analyzer_remote_call_serialize(&self->call, buf),
      goto done);

  SU_TRYCATCH(suscan_mq_write(&self->pdu_queue, is_control, buf), goto done);

  ok = SU_TRUE;

done:
  if (!ok) {
    grow_buf_finalize(buf);
    free(buf);
  }
  return ok;
}

/*************************** Analyzer interface *******************************/
SUPRIVATE void suscan_remote_analyzer_dtor(void *ptr);

SUPRIVATE void
suscan_remote_analyzer_consume_pdu_queue(suscan_remote_analyzer_t *self)
{
  grow_buf_t *buffer;
  uint32_t type;

  while (suscan_mq_poll(&self->pdu_queue, &type, (void **) &buffer)) {
    if (type != SUSCAN_REMOTE_HALT) {
      grow_buf_finalize(buffer);
      free(buffer);
    }
  }
}

void *
suscan_remote_analyzer_ctor(suscan_analyzer_t *parent, va_list ap)
{
  suscan_remote_analyzer_t *new = NULL;
  suscan_source_config_t *config;
  const char *val;
  const char *portstr;
  unsigned int port;

  config = va_arg(ap, suscan_source_config_t *);

#if 0
  if ((driver = suscan_source_config_get_param(config, "transport")) == NULL) {
    SU_ERROR("Cannot initialize remote source: no driver specified\n");
    goto fail;
  }

  if (strcmp(driver, "transport") != 0) {
    SU_ERROR(
        "Cannot initialize remote source: unsupported driver `%s'\n",
        driver);
    goto fail;
  }
#endif

  SU_TRYCATCH(new = calloc(1, sizeof(suscan_remote_analyzer_t)), goto fail);

  /*
   * We tentatively set this to the configured sample rate. It may change
   * later but at least it makes the user believe this is a regular analyzer.
   */
  new->source_info.source_samp_rate =
      suscan_source_config_get_samp_rate(config);

  new->parent = parent;
  new->peer.control_fd = -1;
  new->peer.data_fd    = -1;
  new->peer.mc_fd      = -1;
  new->cancel_pipe[0]  = -1;
  new->cancel_pipe[1]  = -1;

  SU_TRY_FAIL(suscan_mq_init(&new->peer.call_queue));
  new->peer.call_queue_init = SU_TRUE;

  val = suscan_source_config_get_param(config, "host");
  if (val == NULL) {
    SU_ERROR("Cannot initialize remote source: no remote host provided\n");
    goto fail;
  }
  SU_TRYCATCH(new->peer.hostname = strdup(val), goto fail);

  portstr = suscan_source_config_get_param(config, "port");
  if (portstr == NULL) {
    SU_ERROR("Cannot initialize remote source: no remote port provided\n");
    goto fail;
  }

  if (sscanf(portstr, "%u", &port) < 1 || port > 65535) {
    SU_ERROR("Cannot initialize remote source: invalid port\n");
    goto fail;
  }
  new->peer.port = port;

  val = suscan_source_config_get_param(config, "user");
  if (val == NULL) {
    SU_ERROR("No username provided\n");
    goto fail;
  }
  SU_TRYCATCH(new->peer.user = strdup(val), goto fail);

  val = suscan_source_config_get_param(config, "password");
  if (val == NULL) {
    SU_ERROR("No password provided\n");
    goto fail;
  }
  SU_TRYCATCH(new->peer.password = strdup(val), goto fail);

  /* Optional: set multicast IF */
  val = suscan_source_config_get_param(config, "mc_if");
  if (val != NULL)
    SU_TRYCATCH(new->peer.mc_if = strdup(val), goto fail);
  
  SU_TRYCATCH(pthread_mutex_init(&new->call_mutex, NULL) == 0, goto fail);
  new->call_mutex_initialized = SU_TRUE;

  SU_TRYCATCH(pipe(new->cancel_pipe) != -1, goto fail);

  SU_TRYCATCH(
      pthread_create(
          &new->tx_thread,
          NULL,
          suscan_remote_analyzer_tx_thread,
          new) != -1,
      goto fail);
  new->tx_thread_init = SU_TRUE;

  return new;

fail:
  if (new != NULL)
    suscan_remote_analyzer_dtor(new);

  return NULL;
}

SUPRIVATE void
suscan_remote_analyzer_dtor(void *ptr)
{
  suscan_remote_analyzer_t *self = (suscan_remote_analyzer_t *) ptr;
  struct suscan_analyzer_remote_call *call;
  uint32_t type;
  char b = 1;

  if (self->tx_thread_init) {
    if (self->rx_thread_init) {
      IGNORE_RESULT(int, write(self->cancel_pipe[1], &b, 1));
      pthread_join(self->rx_thread, NULL);
    }

    suscan_mq_write(&self->pdu_queue, SUSCAN_REMOTE_HALT, NULL);
    pthread_join(self->tx_thread, NULL);
  }

  /* Free all unprocessed multicast calls */
  if (self->peer.call_queue_init) {
    while (suscan_mq_poll(&self->peer.call_queue, &type, (void **) &call)) {
      suscan_analyzer_remote_call_finalize(call);
      free(call);
    }
  }

  if (self->peer.mc_if != NULL)
    free(self->peer.mc_if);

  if (self->peer.hostname != NULL)
    free(self->peer.hostname);

  if (self->peer.user != NULL)
    free(self->peer.user);

  if (self->peer.password != NULL)
    free(self->peer.password);

  if (self->peer.control_fd != -1)
    close(self->peer.control_fd);

  if (self->peer.data_fd != -1)
    close(self->peer.data_fd);

  if (self->peer.mc_fd != -1)
    close(self->peer.mc_fd);

  suscan_remote_partial_pdu_state_finalize(&self->peer.pdu_state);

  if (self->peer.mc_processor != NULL)
    suscli_multicast_processor_destroy(self->peer.mc_processor);

  if (self->call_mutex_initialized)
    pthread_mutex_destroy(&self->call_mutex);

  suscan_remote_analyzer_consume_pdu_queue(self);

  if (self->cancel_pipe[0] != -1)
    close(self->cancel_pipe[0]);

  if (self->cancel_pipe[1] != -1)
    close(self->cancel_pipe[1]);

  free(self);
}

/* Source-related methods */
SUPRIVATE SUBOOL
suscan_remote_analyzer_set_frequency(void *ptr, SUFREQ freq, SUFREQ lnb)
{
  suscan_remote_analyzer_t *self = (suscan_remote_analyzer_t *) ptr;
  struct suscan_analyzer_remote_call *call = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      call = suscan_remote_analyzer_acquire_call(
          self,
          SUSCAN_ANALYZER_REMOTE_SET_FREQUENCY),
      goto done);

  call->freq = freq;
  call->lnb  = lnb;

  SU_TRYCATCH(
      suscan_remote_analyzer_queue_call(self, call, SU_TRUE),
      goto done);

  ok = SU_TRUE;

done:
  if (call != NULL)
    suscan_remote_analyzer_release_call(self, call);

  return ok;
}

SUPRIVATE SUBOOL
suscan_remote_analyzer_set_gain(void *ptr, const char *name, SUFLOAT value)
{
  suscan_remote_analyzer_t *self = (suscan_remote_analyzer_t *) ptr;
  struct suscan_analyzer_remote_call *call = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      call = suscan_remote_analyzer_acquire_call(
          self,
          SUSCAN_ANALYZER_REMOTE_SET_GAIN),
      goto done);

  SU_TRYCATCH(call->gain.name = strdup(name), goto done);
  call->gain.value = value;

  SU_TRYCATCH(
      suscan_remote_analyzer_queue_call(self, call, SU_TRUE),
      goto done);

  ok = SU_TRUE;

done:
  if (call != NULL)
    suscan_remote_analyzer_release_call(self, call);

  return ok;
}

SUPRIVATE SUBOOL
suscan_remote_analyzer_set_antenna(void *ptr, const char *name)
{
  suscan_remote_analyzer_t *self = (suscan_remote_analyzer_t *) ptr;
  struct suscan_analyzer_remote_call *call = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      call = suscan_remote_analyzer_acquire_call(
          self,
          SUSCAN_ANALYZER_REMOTE_SET_ANTENNA),
      goto done);

  SU_TRYCATCH(call->antenna = strdup(name), goto done);

  SU_TRYCATCH(
      suscan_remote_analyzer_queue_call(self, call, SU_TRUE),
      goto done);

  ok = SU_TRUE;

done:
  if (call != NULL)
    suscan_remote_analyzer_release_call(self, call);

  return ok;
}

SUPRIVATE SUBOOL
suscan_remote_analyzer_set_bandwidth(void *ptr, SUFLOAT value)
{
  suscan_remote_analyzer_t *self = (suscan_remote_analyzer_t *) ptr;
  struct suscan_analyzer_remote_call *call = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      call = suscan_remote_analyzer_acquire_call(
          self,
          SUSCAN_ANALYZER_REMOTE_SET_BANDWIDTH),
      goto done);

  call->bandwidth = value;

  SU_TRYCATCH(
      suscan_remote_analyzer_queue_call(self, call, SU_TRUE),
      goto done);

  ok = SU_TRUE;

done:
  if (call != NULL)
    suscan_remote_analyzer_release_call(self, call);

  return ok;
}

SUPRIVATE SUBOOL
suscan_remote_analyzer_set_ppm(void *ptr, SUFLOAT value)
{
  suscan_remote_analyzer_t *self = (suscan_remote_analyzer_t *) ptr;
  struct suscan_analyzer_remote_call *call = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      call = suscan_remote_analyzer_acquire_call(
          self,
          SUSCAN_ANALYZER_REMOTE_SET_PPM),
      goto done);

  call->ppm = value;

  SU_TRYCATCH(
      suscan_remote_analyzer_queue_call(self, call, SU_TRUE),
      goto done);

  ok = SU_TRUE;

done:
  if (call != NULL)
    suscan_remote_analyzer_release_call(self, call);

  return ok;
}

SUPRIVATE SUBOOL
suscan_remote_analyzer_set_dc_remove(void *ptr, SUBOOL value)
{
  suscan_remote_analyzer_t *self = (suscan_remote_analyzer_t *) ptr;
  struct suscan_analyzer_remote_call *call = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      call = suscan_remote_analyzer_acquire_call(
          self,
          SUSCAN_ANALYZER_REMOTE_SET_DC_REMOVE),
      goto done);

  call->dc_remove = value;

  SU_TRYCATCH(
      suscan_remote_analyzer_queue_call(self, call, SU_TRUE),
      goto done);

  ok = SU_TRUE;

done:
  if (call != NULL)
    suscan_remote_analyzer_release_call(self, call);

  return ok;
}

SUPRIVATE SUBOOL
suscan_remote_analyzer_set_iq_reverse(void *ptr, SUBOOL value)
{
  suscan_remote_analyzer_t *self = (suscan_remote_analyzer_t *) ptr;
  struct suscan_analyzer_remote_call *call = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      call = suscan_remote_analyzer_acquire_call(
          self,
          SUSCAN_ANALYZER_REMOTE_SET_IQ_REVERSE),
      goto done);

  call->iq_reverse = value;

  SU_TRYCATCH(
      suscan_remote_analyzer_queue_call(self, call, SU_TRUE),
      goto done);

  ok = SU_TRUE;

done:
  if (call != NULL)
    suscan_remote_analyzer_release_call(self, call);

  return ok;

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_remote_analyzer_set_agc(void *ptr, SUBOOL value)
{
  suscan_remote_analyzer_t *self = (suscan_remote_analyzer_t *) ptr;
  struct suscan_analyzer_remote_call *call = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      call = suscan_remote_analyzer_acquire_call(
          self,
          SUSCAN_ANALYZER_REMOTE_SET_AGC),
      goto done);

  call->agc = value;

  SU_TRYCATCH(
      suscan_remote_analyzer_queue_call(self, call, SU_TRUE),
      goto done);

  ok = SU_TRUE;

done:
  if (call != NULL)
    suscan_remote_analyzer_release_call(self, call);

  return ok;
}

SUPRIVATE SUBOOL
suscan_remote_analyzer_force_eos(void *ptr)
{
  suscan_remote_analyzer_t *self = (suscan_remote_analyzer_t *) ptr;
  struct suscan_analyzer_remote_call *call = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      call = suscan_remote_analyzer_acquire_call(
          self,
          SUSCAN_ANALYZER_REMOTE_FORCE_EOS),
      goto done);

  /* TODO: Implement me */

  SU_TRYCATCH(
      suscan_remote_analyzer_queue_call(self, call, SU_TRUE),
      goto done);

  ok = SU_TRUE;

done:
  if (call != NULL)
    suscan_remote_analyzer_release_call(self, call);

  return ok;
}

SUPRIVATE SUBOOL
suscan_remote_analyzer_is_real_time(const void *ptr)
{
  return SU_TRUE;
}

SUPRIVATE unsigned int
suscan_remote_analyzer_get_samp_rate(const void *ptr)
{
  suscan_remote_analyzer_t *self = (suscan_remote_analyzer_t *) ptr;

  return self->source_info.source_samp_rate;
}

SUPRIVATE SUFLOAT
suscan_remote_analyzer_get_measured_samp_rate(const void *ptr)
{
  const suscan_remote_analyzer_t *self = (const suscan_remote_analyzer_t *) ptr;

  return self->source_info.measured_samp_rate;
}

SUPRIVATE void
suscan_remote_analyzer_get_source_time(const void *ptr, struct timeval *tv)
{
  const suscan_remote_analyzer_t *self = (const suscan_remote_analyzer_t *) ptr;

  /* In remote analyzers, the only way to query the source time is to obtain
     the last cached source time. */
  *tv = self->source_info.source_time;
}

SUPRIVATE SUBOOL
suscan_remote_analyzer_seek(void *ptr, const struct timeval *tv)
{
  suscan_remote_analyzer_t *self = (suscan_remote_analyzer_t *) ptr;

  return suscan_analyzer_seek_async(self->parent, tv, 0);
}

SUPRIVATE struct suscan_analyzer_source_info *
suscan_remote_analyzer_get_source_info_pointer(const void *ptr)
{
  suscan_remote_analyzer_t *self = (suscan_remote_analyzer_t *) ptr;

  return &self->source_info;
}

SUPRIVATE SUBOOL
suscan_remote_analyzer_commit_source_info(void *ptr)
{
  return SU_TRUE;
}

/* Worker specific methods */
SUPRIVATE SUBOOL
suscan_remote_analyzer_set_sweep_strategy(
    void *ptr,
    enum suscan_analyzer_sweep_strategy strategy)
{
  suscan_remote_analyzer_t *self = (suscan_remote_analyzer_t *) ptr;
  struct suscan_analyzer_remote_call *call = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      call = suscan_remote_analyzer_acquire_call(
          self,
          SUSCAN_ANALYZER_REMOTE_SET_SWEEP_STRATEGY),
      goto done);

  call->sweep_strategy = strategy;

  SU_TRYCATCH(
      suscan_remote_analyzer_queue_call(self, call, SU_TRUE),
      goto done);

  ok = SU_TRUE;

done:
  if (call != NULL)
    suscan_remote_analyzer_release_call(self, call);

  return ok;
}

SUPRIVATE SUBOOL
suscan_remote_analyzer_set_spectrum_partitioning(
    void *ptr,
    enum suscan_analyzer_spectrum_partitioning partitioning)
{
  suscan_remote_analyzer_t *self = (suscan_remote_analyzer_t *) ptr;
  struct suscan_analyzer_remote_call *call = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      call = suscan_remote_analyzer_acquire_call(
          self,
          SUSCAN_ANALYZER_REMOTE_SET_SPECTRUM_PARTITIONING),
      goto done);

  call->spectrum_partitioning = partitioning;

  SU_TRYCATCH(
      suscan_remote_analyzer_queue_call(self, call, SU_TRUE),
      goto done);

  ok = SU_TRUE;

done:
  if (call != NULL)
    suscan_remote_analyzer_release_call(self, call);

  return ok;
}

SUPRIVATE SUBOOL
suscan_remote_analyzer_set_hop_range(void *ptr, SUFREQ min, SUFREQ max)
{
  suscan_remote_analyzer_t *self = (suscan_remote_analyzer_t *) ptr;
  struct suscan_analyzer_remote_call *call = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      call = suscan_remote_analyzer_acquire_call(
          self,
          SUSCAN_ANALYZER_REMOTE_SET_HOP_RANGE),
      goto done);

  call->hop_range.min = min;
  call->hop_range.max = max;

  SU_TRYCATCH(
      suscan_remote_analyzer_queue_call(self, call, SU_TRUE),
      goto done);

  ok = SU_TRUE;

done:
  if (call != NULL)
    suscan_remote_analyzer_release_call(self, call);

  return ok;
}

SUPRIVATE SUBOOL
suscan_remote_analyzer_set_buffering_size(void *ptr, SUSCOUNT size)
{
  suscan_remote_analyzer_t *self = (suscan_remote_analyzer_t *) ptr;
  struct suscan_analyzer_remote_call *call = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      call = suscan_remote_analyzer_acquire_call(
          self,
          SUSCAN_ANALYZER_REMOTE_SET_BUFFERING_SIZE),
      goto done);

  call->buffering_size = size;

  SU_TRYCATCH(
      suscan_remote_analyzer_queue_call(self, call, SU_TRUE),
      goto done);

  ok = SU_TRUE;

done:
  if (call != NULL)
    suscan_remote_analyzer_release_call(self, call);

  return ok;
}

SUPRIVATE SUBOOL
suscan_remote_analyzer_write(void *ptr, uint32_t type, void *priv)
{
  suscan_remote_analyzer_t *self = (suscan_remote_analyzer_t *) ptr;
  struct suscan_analyzer_remote_call *call = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      call = suscan_remote_analyzer_acquire_call(
          self,
          SUSCAN_ANALYZER_REMOTE_MESSAGE),
      goto done);

  /* Transfer ownership */
  call->msg.type = type;
  call->msg.ptr  = priv;

  SU_TRYCATCH(
      suscan_remote_analyzer_queue_call(self, call, SU_TRUE),
      goto done);

  ok = SU_TRUE;

done:
  if (call != NULL)
    suscan_remote_analyzer_release_call(self, call);
  else
    suscan_analyzer_dispose_message(type, priv);

  return ok;
}

SUPRIVATE void
suscan_remote_analyzer_req_halt(void *ptr)
{
  suscan_remote_analyzer_t *self = (suscan_remote_analyzer_t *) ptr;
  struct suscan_analyzer_remote_call *call = NULL;

  SU_TRYCATCH(
      call = suscan_remote_analyzer_acquire_call(
          self,
          SUSCAN_ANALYZER_REMOTE_REQ_HALT),
      goto done);

  SU_TRYCATCH(
      suscan_remote_analyzer_queue_call(self, call, SU_TRUE),
      goto done);

done:
  if (call != NULL)
    suscan_remote_analyzer_release_call(self, call);
}

#define SET_CALLBACK(name) iface.name = JOIN(suscan_remote_analyzer_, name)

const struct suscan_analyzer_interface *
suscan_remote_analyzer_get_interface(void)
{
  static struct suscan_analyzer_interface iface;

  if (g_remote_analyzer_interface == NULL) {
    iface.name = "remote";

    SET_CALLBACK(ctor);
    SET_CALLBACK(dtor);
    SET_CALLBACK(set_frequency);
    SET_CALLBACK(set_gain);
    SET_CALLBACK(set_antenna);
    SET_CALLBACK(set_bandwidth);
    SET_CALLBACK(set_ppm);
    SET_CALLBACK(set_dc_remove);
    SET_CALLBACK(set_iq_reverse);
    SET_CALLBACK(set_agc);
    SET_CALLBACK(force_eos);
    SET_CALLBACK(is_real_time);
    SET_CALLBACK(get_samp_rate);
    SET_CALLBACK(get_source_time);
    SET_CALLBACK(seek);
    SET_CALLBACK(get_measured_samp_rate);
    SET_CALLBACK(get_source_info_pointer);
    SET_CALLBACK(commit_source_info);
    SET_CALLBACK(set_sweep_strategy);
    SET_CALLBACK(set_spectrum_partitioning);
    SET_CALLBACK(set_hop_range);
    SET_CALLBACK(set_buffering_size);
    SET_CALLBACK(write);
    SET_CALLBACK(req_halt);

    g_remote_analyzer_interface = &iface;
  }

  return g_remote_analyzer_interface;
}

#undef SET_CALLBACK
