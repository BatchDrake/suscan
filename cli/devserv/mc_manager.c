/*

  Copyright (C) 2022 Gonzalo José Carracedo Carballal

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

#define SU_LOG_DOMAIN "multicast-manager"

#include <analyzer/impl/multicast.h>
#include <util/compat-poll.h>
#include <util/compat-inet.h>
#include <util/compat-unistd.h>
#include <util/compat-socket.h>
#include <util/compat.h>
#include <analyzer/msg.h>

SUPRIVATE
SU_METHOD(
  suscli_multicast_manager,
  SUBOOL,
  open_multicast_socket,
  const char *ifname,
  uint16_t port)
{
  struct in_addr mc_if;
  char loopch = 0;
  SUBOOL ok = SU_FALSE;
  
  SU_TRYC(self->fd = socket(AF_INET, SOCK_DGRAM, 0));
  SU_TRYC(setsockopt(
          self->fd,
          IPPROTO_IP,
          IP_MULTICAST_LOOP,
          (char *) &loopch,
          sizeof(loopch)));

  mc_if.s_addr = suscan_ifdesc_to_addr(ifname);

  if (ntohl(mc_if.s_addr) == 0xffffffff) {
    SU_ERROR(
        "Invalid interface address `%s' (does not look like a valid IP address)\n",
        ifname);
    goto done;
  }

  if ((ntohl(mc_if.s_addr) & 0xf0000000) == 0xe0000000) {
    SU_ERROR("Invalid interface address. Please note that if= expects the "
        "IP address of a configured local network interface, not a multicast "
        "group.\n");

    goto done;
  }

  if (setsockopt(
          self->fd,
          IPPROTO_IP,
          IP_MULTICAST_IF,
          (char *) &mc_if,
          sizeof (struct in_addr)) == -1) {
    if (errno == EADDRNOTAVAIL) {
      SU_ERROR("Invalid interface address. Please verify that there is a "
          "local network interface with IP `%s'\n", ifname);
    } else {
      SU_ERROR(
          "failed to set network interface for multicast: %s\n",
          strerror(errno));
    }

    goto done;
  }

  memset(&self->mc_addr, 0, sizeof(struct sockaddr_in));
  self->mc_addr.sin_family      = AF_INET;
  self->mc_addr.sin_addr.s_addr = inet_addr(SUSCLI_MULTICAST_GROUP);
  self->mc_addr.sin_port        = htons(SUSCLI_MULTICAST_PORT);

  ok = SU_TRUE;

done:
  return ok;
}

/* All messages are guaranteed to be allocated up to the MTU size */
SUPRIVATE SUBOOL
suscli_multicast_manager_tx_cb(
  struct suscan_mq *out,
  void *wk_private,
  void *cb_private)
{
  suscli_multicast_manager_t *self = 
    (suscli_multicast_manager_t *) wk_private;
  struct suscan_analyzer_fragment_header *header = NULL;
  uint32_t type;
  int ret;
  int size;

  while (!self->cancelled 
      && suscan_mq_poll(&self->queue, &type, (void **) &header)) {
    if (type == SUSCLI_MULTICAST_FRAG_MESSAGE) {
      if (!self->cancelled) {
        size = SUSCLI_MULTICAST_FRAG_SIZE(ntohs(header->size));
        if ((ret = sendto(
          self->fd,
          (void *) header,
          size,
          0,
          (struct sockaddr *) &self->mc_addr,
          sizeof(struct sockaddr_in))) != size) {
          if (ret == 0)
            SU_WARNING("Multicast socket closed!\n");
          else if (ret == -1)
            SU_ERROR("Failed to send announce message: %s\n", strerror(errno));
          else
            SU_ERROR("Datagram truncation (%d/%d)\n", ret, size);
          self->cancelled = SU_TRUE;
        }

        gettimeofday(&self->last_tx, NULL);
      }

      /* TODO: Add growth control here */
      if (!suscan_mq_write(&self->pool, type, header))
        free(header);
    }
  } 

  return SU_FALSE;
}

SUPRIVATE SUBOOL
suscli_multicast_manager_announce_cb(
  struct suscan_mq *out,
  void *wk_private,
  void *cb_private)
{
  suscli_multicast_manager_t *self = 
    (suscli_multicast_manager_t *) wk_private;
  struct suscan_analyzer_fragment_header header;
  struct timeval now, diff;
  uint64_t ellapsed_ms;
  int ret;

  gettimeofday(&now, NULL);
  timersub(&self->last_tx, &now, &diff);

  ellapsed_ms = diff.tv_sec * 1000 + diff.tv_usec - 1000;

  if (ellapsed_ms >= SUSCLI_MULTICAST_ANNOUNCE_START_MS) {
    memset(&header, 0, sizeof(struct suscan_analyzer_fragment_header));

    header.magic     = htonl(SUSCAN_REMOTE_FRAGMENT_HEADER_MAGIC);
    header.sf_type   = SUSCAN_ANALYZER_SUPERFRAME_TYPE_ANNOUNCE;
    header.size      = htons(0);
    header.sf_id     = self->id++;
    header.sf_size   = htonl(0);
    header.sf_offset = htonl(0);

    if (!self->cancelled) {
      if ((ret = sendto(
        self->fd,
        (void *) &header,
        sizeof(header),
        0,
        (struct sockaddr *) &self->mc_addr,
        sizeof(struct sockaddr_in))) != sizeof(header)) {

        if (ret == 0)
          SU_WARNING("Multicast socket closed!\n");
        else if (ret == -1)
          SU_ERROR("Failed to send announce message: %s\n", strerror(errno));
        else
          SU_ERROR("Datagram truncation (%d/%d)\n", ret, sizeof(header));

        self->cancelled = SU_TRUE;
      }
    }
  }
  
  return SU_FALSE;
}

SUPRIVATE void *
suscli_multicast_manager_announce_thread(void *userdata)
{
  suscli_multicast_manager_t *self = 
    (suscli_multicast_manager_t *) userdata;
  struct pollfd fds;
  SUBOOL ok = SU_FALSE;
  char b;

  int n;

  fds.fd = self->cancel_pipefd[0];
  fds.events = POLLIN;

  while (!self->cancelled) {
    fds.revents = 0;

    SU_TRYC(n = poll(&fds, 1, SUSCLI_MULTICAST_ANNOUNCE_DELAY_MS));

    if (fds.revents & POLLIN) {
      (void) read(self->cancel_pipefd[0], &b, 1);
      break;
    }

    SU_TRY(
      suscan_worker_push(
        self->tx_worker,
        suscli_multicast_manager_announce_cb,
        NULL));
  }

  ok = SU_TRUE;

done:
  if (!ok)
    SU_ERROR(
      "Multicast announce thread finished unexpectedly (%s)\n",
      strerror(errno));

  return NULL;
}

SU_INSTANCER(suscli_multicast_manager, const char *ifname, uint16_t port)
{
  suscli_multicast_manager_t *new = NULL;

  SU_ALLOCATE_FAIL(new, suscli_multicast_manager_t);
  new->fd = -1;
  new->cancel_pipefd[0] = -1;
  new->cancel_pipefd[1] = -1;

  SU_TRY_FAIL(
    suscli_multicast_manager_open_multicast_socket(
      new,
      ifname,
      port));

  SU_TRYC_FAIL(pipe(new->cancel_pipefd));

  SU_TRY_FAIL(suscan_mq_init(&new->pool));
  new->pool_initialized = SU_TRUE;

  SU_TRY_FAIL(suscan_mq_init(&new->queue));
  new->queue_initialized = SU_TRUE;

  SU_TRY_FAIL(suscan_mq_init(&new->mq_out));
  new->mq_out_initialized = SU_TRUE;

  SU_TRY_FAIL(
    new->tx_worker = suscan_worker_new_ex(
      "tx-worker",
      &new->mq_out,
      new));

  SU_TRY_FAIL(
    pthread_create(
      &new->announce_thread,
      NULL,
      suscli_multicast_manager_announce_thread,
      new) == 0);
  new->announce_initialized = SU_TRUE;
  
  return new;

fail:
  if (new != NULL)
    suscli_multicast_manager_destroy(new);

  return NULL;
}

SU_COLLECTOR(suscli_multicast_manager)
{
  char b = 1;
  void *data;
  uint32_t type;

  if (self->announce_initialized) {
    self->cancelled = SU_TRUE;

    write(self->cancel_pipefd[1], &b, 1);
    (void) pthread_join(self->announce_thread, NULL);
  }

  if (self->tx_worker != NULL)
    suscan_worker_destroy(self->tx_worker);

  if (self->mq_out_initialized)
    suscan_mq_finalize(&self->mq_out);

  /* Flush message queue */
  if (self->queue_initialized) {
    while (suscan_mq_poll(&self->queue, &type, &data))
      free(data);
    suscan_mq_finalize(&self->queue);
  }

  /* Flush message pool */
  if (self->pool_initialized) {
    while (suscan_mq_poll(&self->pool, &type, &data))
      free(data);
    suscan_mq_finalize(&self->pool);
  }

  if (self->fd != -1)
    close(self->fd);

  free(self);
}

SUPRIVATE SU_METHOD(
  suscli_multicast_manager,
  struct suscan_analyzer_fragment_header *, 
  allocate_message)
{
  struct suscan_analyzer_fragment_header *msg = NULL;
  uint32_t type;
  uint16_t usable;
  void *data = NULL;

  if (!suscan_mq_poll(&self->pool, &type, &data))
    SU_ALLOCATE_MANY(data, SUSCLI_MULTICAST_FRAGMENT_MTU, uint8_t);

  usable = SUSCLI_MULTICAST_FRAGMENT_MTU - SUSCLI_MULTICAST_FRAG_SIZE(0);

  msg = data;
  msg->magic = htonl(SUSCAN_REMOTE_FRAGMENT_HEADER_MAGIC);
  msg->size  = htons(usable);
  
done:
  return msg;
}

SUPRIVATE SU_METHOD(
  suscli_multicast_manager,
  SUBOOL, 
  deliver_psd,
  const struct suscan_analyzer_remote_call *call)
{
  struct suscan_analyzer_psd_msg *msg;
  struct suscan_analyzer_fragment_header *header;
  struct suscan_analyzer_psd_sf_fragment frag, *payload;
  unsigned int usable;
  unsigned int i, count, size;
  uint8_t id = self->id++;
  const unsigned psdsf = sizeof(struct suscan_analyzer_psd_sf_fragment);
  unsigned int sfsize = SUSCLI_MULTICAST_FRAG_SIZE(psdsf);
  SUBOOL ok = SU_FALSE;

  usable = (SUSCLI_MULTICAST_FRAGMENT_MTU - sfsize) / sizeof(SUFLOAT);

  msg = call->msg.ptr;

  /* Calculate the number of fragments */
  count = (msg->psd_size + usable - 1) / usable;

  /* Prepare fragment template */
  frag.fc                 = su_htonll(msg->fc);
  frag.timestamp_sec      = su_htonll(msg->timestamp.tv_sec);
  frag.timestamp_usec     = htonl(msg->timestamp.tv_usec);

  frag.rt_timestamp_sec   = su_htonll(msg->rt_time.tv_sec);
  frag.rt_timestamp_usec  = htonl(msg->rt_time.tv_usec);

  frag.samp_rate          = msg->samp_rate;
  frag.measured_samp_rate = msg->measured_samp_rate;

  frag.samp_rate_u32      = htonl(frag.samp_rate_u32);
  frag.measured_samp_rate_u32 = htonl(frag.measured_samp_rate_u32);

  frag.flags              = su_htonll(1ull & msg->looped);

  /* Chop and deliver */
  for (i = 0; i < count; ++i) {
    SU_TRY(header = suscli_multicast_manager_allocate_message(self));

    size = MIN(usable, msg->psd_size - i * usable);

    /* Size consists of PSD superframe header + data */
    header->size      = htons(psdsf + size * sizeof(SUFLOAT));
    header->sf_type   = SUSCAN_ANALYZER_SUPERFRAME_TYPE_PSD;
    header->sf_id     = id;
    header->sf_size   = htonl(msg->psd_size);
    header->sf_offset = htonl(i * usable);

    payload = (struct suscan_analyzer_psd_sf_fragment *) header->sf_data;

    *payload = frag;

    memcpy(
      payload->bytes,
      msg->psd_data + i * usable,
      size * sizeof(SUFLOAT));

    SU_TRY(
      suscan_mq_write(
        &self->queue,
        SUSCLI_MULTICAST_FRAG_MESSAGE,
        header));
    
    header = NULL;
  }

  /* Messages successfully queued, wake up worker */
  SU_TRY(
    suscan_worker_push(
      self->tx_worker,
      suscli_multicast_manager_tx_cb,
      NULL));

  ok = SU_TRUE;

done:
  if (header != NULL)
    free(header);

  return ok;
}

SUPRIVATE SU_METHOD(
  suscli_multicast_manager,
  SUBOOL, 
  deliver_encap,
  const struct suscan_analyzer_remote_call *call)
{
  struct suscan_analyzer_fragment_header *header = NULL;
  grow_buf_t pdu = grow_buf_INITIALIZER;
  unsigned int usable;
  unsigned int i, count, size;
  unsigned int full_size;
  const uint8_t *as_bytes;
  uint8_t id = self->id++;
  SUBOOL ok = SU_FALSE;

  usable = SUSCLI_MULTICAST_FRAGMENT_MTU 
    - SUSCLI_MULTICAST_FRAG_SIZE(
      sizeof(struct suscan_analyzer_psd_sf_fragment));

  SU_TRY(suscan_analyzer_remote_call_serialize(call, &pdu));

  full_size = grow_buf_get_size(&pdu);
  as_bytes  = grow_buf_get_buffer(&pdu);

  /* Calculate the number of fragments */
  count = (full_size + usable - 1) / usable;

  /* Chop and deliver */
  for (i = 0; i < count; ++i) {
    SU_TRY(header = suscli_multicast_manager_allocate_message(self));

    size = MIN(usable, full_size - i * usable);

    header->size      = htons(size);
    header->sf_type   = SUSCAN_ANALYZER_SUPERFRAME_TYPE_ENCAP;
    header->sf_id     = id;
    header->sf_size   = htonl(full_size);
    header->sf_offset = htonl(i * usable);

    memcpy(header->sf_data, as_bytes + i * usable, size);

    SU_TRY(
      suscan_mq_write(
        &self->queue,
        SUSCLI_MULTICAST_FRAG_MESSAGE,
        header));
    
    header = NULL;
  }

  /* Messages successfully queued, wake up worker */
  SU_TRY(
    suscan_worker_push(
      self->tx_worker,
      suscli_multicast_manager_tx_cb,
      NULL));

  ok = SU_TRUE;

done:
  grow_buf_finalize(&pdu);

  if (header != NULL)
    free(header);

  return ok;
}

SU_METHOD(
  suscli_multicast_manager,
  SUBOOL, 
  deliver_call,
  const struct suscan_analyzer_remote_call *call)
{
  SUBOOL ok = SU_FALSE;

  if (call->type == SUSCAN_ANALYZER_REMOTE_MESSAGE
    && call->msg.type == SUSCAN_ANALYZER_MESSAGE_TYPE_PSD) {
    SU_TRY(suscli_multicast_manager_deliver_psd(self, call));
  } else {
    SU_TRY(suscli_multicast_manager_deliver_encap(self, call));
  }

  ok = SU_TRUE;

done:
  return ok;
}
