/*

  Copyright (C) 2017 Gonzalo José Carracedo Carballal

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

#ifndef _MQ_H
#define _MQ_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <pthread.h>
#include <sigutils/sigutils.h>
#include <sys/time.h>

#define SUSCAN_MQ_USE_POOL

#define SUSCAN_MQ_POOL_WARNING_THRESHOLD 100

struct suscan_msg {
  uint32_t type;
  void *privdata;
  struct suscan_msg *next;

#ifdef SUSCAN_MQ_USE_POOL
  struct suscan_msg *free_next; /* Next free message */
#endif
};

struct suscan_mq {
  pthread_mutex_t acquire_lock;
  pthread_cond_t  acquire_cond;

  struct suscan_msg *head;
  struct suscan_msg *tail;
};

/*************************** Message queue API *******************************/
SUBOOL suscan_mq_init(struct suscan_mq *mq);
void   suscan_mq_finalize(struct suscan_mq *mq);

void  *suscan_mq_read(struct suscan_mq *mq, uint32_t *type);
void  *suscan_mq_read_timeout(
    struct suscan_mq *mq,
    uint32_t *type,
    const struct timeval *timeout);

void  *suscan_mq_read_w_type(struct suscan_mq *mq, uint32_t type);
void  *suscan_mq_read_w_type_timeout(
    struct suscan_mq *mq,
    uint32_t type,
    const struct timeval *timeout);

struct suscan_msg *suscan_mq_read_msg(struct suscan_mq *mq);
struct suscan_msg *suscan_mq_read_msg_timeout(
    struct suscan_mq *mq,
    const struct timeval *timeout);

struct suscan_msg *suscan_mq_read_msg_w_type(struct suscan_mq *mq, uint32_t type);
struct suscan_msg *suscan_mq_read_msg_w_type_timeout(
    struct suscan_mq *mq,
    uint32_t type,
    const struct timeval *timeout);

SUBOOL suscan_mq_poll(struct suscan_mq *mq, uint32_t *type, void **privdata);
SUBOOL suscan_mq_poll_w_type(struct suscan_mq *mq, uint32_t type, void **privdata);
struct suscan_msg *suscan_mq_poll_msg(struct suscan_mq *mq);
struct suscan_msg *suscan_mq_poll_msg_w_type(struct suscan_mq *mq, uint32_t type);
SUBOOL suscan_mq_write(struct suscan_mq *mq, uint32_t type, void *privdata);
void   suscan_mq_wait(struct suscan_mq *mq);
SUBOOL suscan_mq_write_urgent(struct suscan_mq *mq, uint32_t type, void *privdata);
void suscan_mq_write_msg(struct suscan_mq *mq, struct suscan_msg *msg);
void suscan_mq_write_msg_urgent(struct suscan_mq *mq, struct suscan_msg *msg);
void suscan_msg_destroy(struct suscan_msg *msg);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _MQ_H */
