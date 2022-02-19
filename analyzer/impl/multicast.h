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

#ifndef _SUSCAN_ANALYZER_MULTICAST_H
#define _SUSCAN_ANALYZER_MULTICAST_H

#include <stdint.h>
#include <analyzer/impl/remote.h>
#include <util/compat-in.h>
#include <sigutils/types.h>
#include <sigutils/defs.h>
#include <util/list.h>
#include <analyzer/worker.h>
#include <util/rbtree.h>

#define SUSCLI_MULTICAST_MAX_SUPERFRAME_SIZE (1 << 20)
#define SUSCLI_MULTICAST_GROUP "224.4.4.4"
#define SUSCLI_MULTICAST_PORT  5556
#define SUSCLI_MULTICAST_ANNOUNCE_DELAY_MS 1000
#define SUSCLI_MULTICAST_ANNOUNCE_START_MS 2000
#define SUSCLI_MULTICAST_FRAGMENT_MTU      508 /* 576 - IP hdr - UDP hdr */
#define SUSCLI_MULTICAST_FRAG_MESSAGE      1

#define SUSCLI_MULTICAST_FRAG_SIZE(payload) \
  (sizeof(struct suscan_analyzer_fragment_header) + (payload))

#if __BIG_ENDIAN__
# define su_htonll(x) (x)
# define su_ntohll(x) (x)
#else
# define su_htonll(x) (((uint64_t)htonl((x) & 0xFFFFFFFF) << 32) | htonl((x) >> 32))
# define su_ntohll(x) (((uint64_t)ntohl((x) & 0xFFFFFFFF) << 32) | ntohl((x) >> 32))
#endif

/************************** Multicast manager *******************************/
/*
 * The multicast manager is in charge of splitting messages and (attempting)
 * to deliver them via Multicast UDP sockets. It is used by the server to
 * deliver PSDs, source infos, etc. It is also in charge of announcing its 
 * presence to the net periodically
 */

struct suscli_multicast_manager {
  uint32_t addr;
  uint16_t port;
  int fd;
  int cancel_pipefd[2];
  uint8_t id;
  SUBOOL cancelled;

  struct sockaddr_in mc_addr;

  /* This is how this works:
     - We either retrieve from pool or reallocate a message
     - We then put a message in the message queue
     - We send a callback to the worker
     */

  struct suscan_mq  pool;
  SUBOOL            pool_initialized;

  struct suscan_mq  queue;
  SUBOOL            queue_initialized;

  struct suscan_mq  mq_out;
  SUBOOL            mq_out_initialized;

  struct timeval    last_tx;  
  suscan_worker_t  *tx_worker;

  pthread_t         announce_thread;
  SUBOOL            announce_initialized;
};

typedef struct suscli_multicast_manager suscli_multicast_manager_t;

SU_INSTANCER(suscli_multicast_manager, const char *addr, uint16_t port);
SU_COLLECTOR(suscli_multicast_manager);

SU_METHOD(
  suscli_multicast_manager,
  SUBOOL, 
  deliver_call,
  const struct suscan_analyzer_remote_call *);

/**************************** Multicast processor ****************************/
/*
 * The multicast processor is in charge of reassemblying fragments and
 * delivering full or partial calls to the analyzer, based on previous
 * messages. This is a per-message type behavior that has to be addressed 
 * separately, with separate states.
 */
struct suscli_multicast_processor;

struct suscli_multicast_processor_impl {
  const char *name;
  uint8_t     sf_type;

  void  *(*ctor) (struct suscli_multicast_processor *);
  SUBOOL (*on_fragment) (void *, const struct suscan_analyzer_fragment_header *);
  SUBOOL (*try_flush) (void *, struct suscan_analyzer_remote_call *);
  void   (*dtor) (void *);
};

typedef SUBOOL (*suscli_multicast_processor_call_cb_t) (
    struct suscli_multicast_processor *self,
    void *userdata,
    struct suscan_analyzer_remote_call *);

struct suscli_multicast_processor {
  uint8_t   curr_type;
  uint8_t   curr_id;
  rbtree_t *processor_tree;

  const struct suscli_multicast_processor_impl *curr_impl;
  void *curr_state;
  
  void *userdata;
  suscli_multicast_processor_call_cb_t on_call;
};

typedef struct suscli_multicast_processor suscli_multicast_processor_t;

SUBOOL suscli_multicast_processor_register(
  const struct suscli_multicast_processor_impl *impl);

SUBOOL suscli_multicast_processor_init(void);

SU_INSTANCER(
  suscli_multicast_processor,
  suscli_multicast_processor_call_cb_t,
  void *);

SU_METHOD(
  suscli_multicast_processor,
  SUBOOL,
  trigger_on_call);

SU_METHOD(
  suscli_multicast_processor,
  SUBOOL,
  process,
  const struct suscan_analyzer_fragment_header *);

SU_METHOD(
  suscli_multicast_processor,
  SUBOOL,
  process_datagram,
  const void *data,
  size_t size);

SU_COLLECTOR(suscli_multicast_processor);

#endif /* _SUSCAN_ANALYZER_MULTICAST_H */

