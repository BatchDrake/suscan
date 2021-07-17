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

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <libgen.h>
#include <pthread.h>
#include <stdint.h>

#include "mq.h"

#ifdef SUSCAN_MQ_USE_POOL

SUPRIVATE pthread_mutex_t msg_pool_mutex = PTHREAD_MUTEX_INITIALIZER;
SUPRIVATE struct suscan_msg *msg_pool = NULL;
SUPRIVATE int msg_pool_size;
SUPRIVATE int msg_pool_peak;

SUPRIVATE void
suscan_msg_pool_enter(void)
{
  (void) pthread_mutex_lock(&msg_pool_mutex);
}


SUPRIVATE void
suscan_msg_pool_leave(void)
{
  (void) pthread_mutex_unlock(&msg_pool_mutex);
}

SUPRIVATE struct suscan_msg *
suscan_mq_alloc_msg(void)
{
  struct suscan_msg *msg = NULL;
  suscan_msg_pool_enter();

  if (msg_pool != NULL) {
    msg = msg_pool;
    msg_pool = msg->free_next;

    --msg_pool_size;
  }

  suscan_msg_pool_leave();

  /* Fallback to malloc. TODO: add a message limit here */
  if (msg == NULL)
    msg = (struct suscan_msg *) malloc (sizeof (struct suscan_msg));

  return msg;
}

SUPRIVATE void
suscan_mq_return_msg(struct suscan_msg *msg)
{
  int msg_pool_peak_copy = -1;
  suscan_msg_pool_enter();

  msg->free_next = msg_pool;
  msg_pool = msg;

  ++msg_pool_size;
  if (msg_pool_size > msg_pool_peak) {
    msg_pool_peak = msg_pool_size;
    msg_pool_peak_copy = msg_pool_peak;
  }

  suscan_msg_pool_leave();

  if ((msg_pool_peak_copy % SUSCAN_MQ_POOL_WARNING_THRESHOLD) == 0)
    SU_WARNING(
        "Message pool freelist grew to %d elements!\n",
        msg_pool_peak_copy);
}

#else
SUPRIVATE struct suscan_msg *
suscan_mq_alloc_msg(void)
{
  return (struct suscan_msg *) malloc (sizeof (struct suscan_msg));
}

SUPRIVATE void
suscan_mq_return_msg(struct suscan_msg *msg)
{
  free(msg);
}
#endif

SUPRIVATE void
suscan_mq_enter(struct suscan_mq *mq)
{
  pthread_mutex_lock(&mq->acquire_lock);
}

SUPRIVATE void
suscan_mq_leave(struct suscan_mq *mq)
{
  pthread_mutex_unlock(&mq->acquire_lock);
}

SUPRIVATE void
suscan_mq_notify(struct suscan_mq *mq)
{
  pthread_cond_broadcast(&mq->acquire_cond);
}

SUPRIVATE void
suscan_mq_wait_unsafe(struct suscan_mq *mq)
{
  pthread_cond_wait(&mq->acquire_cond, &mq->acquire_lock);
}

SUPRIVATE SUBOOL
suscan_mq_timedwait_unsafe(
    struct suscan_mq *mq,
    const struct timespec *ts)
{
  return pthread_cond_timedwait(
      &mq->acquire_cond,
      &mq->acquire_lock,
      ts) == 0;
}

void
suscan_mq_wait(struct suscan_mq *mq)
{
  suscan_mq_enter(mq);

  suscan_mq_wait_unsafe(mq);

  suscan_mq_leave(mq);
}

SUBOOL
suscan_mq_timedwait(struct suscan_mq *mq, const struct timespec *ts)
{
  SUBOOL result;

  suscan_mq_enter(mq);

  result = suscan_mq_timedwait_unsafe(mq, ts);

  suscan_mq_leave(mq);

  return result;
}

SUPRIVATE struct suscan_msg *
suscan_msg_new(uint32_t type, void *private)
{
  struct suscan_msg *new;

  SU_TRYCATCH(new = suscan_mq_alloc_msg(), return NULL);

  new->type = type;
  new->privdata = private;
  new->next = NULL;

  return new;
}

void
suscan_msg_destroy(struct suscan_msg *msg)
{
  suscan_mq_return_msg(msg);
}

SUPRIVATE void
suscan_mq_push_front(struct suscan_mq *mq, struct suscan_msg *msg)
{
  msg->next = mq->head;
  mq->head = msg;

  if (mq->tail == NULL)
    mq->tail = msg;
}

SUPRIVATE void
suscan_mq_push(struct suscan_mq *mq, struct suscan_msg *msg)
{
  if (mq->tail != NULL)
    mq->tail->next = msg;

  mq->tail = msg;

  if (mq->head == NULL)
    mq->head = msg;
}

SUPRIVATE struct suscan_msg *
suscan_mq_pop(struct suscan_mq *mq)
{
  struct suscan_msg *msg;

  if ((msg = mq->head) == NULL)
    return NULL;

  mq->head = msg->next;

  if (mq->head == NULL)
    mq->tail = NULL;

  msg->next = NULL;

  return msg;
}

SUPRIVATE struct suscan_msg *
suscan_mq_pop_w_type(struct suscan_mq *mq, uint32_t type)
{
  struct suscan_msg *this, *prev;

  prev = NULL;
  this = mq->head;

  while (this != NULL) {
    if (this->type == type)
      break;
    prev = this;
    this = this->next;
  }

  if (this != NULL) {
    if (prev == NULL)
      mq->head = this->next;
    else
      prev->next = this->next;

    if (this == mq->tail)
      mq->tail = prev;

    this->next = NULL;
  }

  return this;
}

SUPRIVATE struct suscan_msg *
suscan_mq_read_msg_internal(
    struct suscan_mq *mq,
    SUBOOL with_type,
    uint32_t type,
    const struct timeval *timeout)
{
  struct suscan_msg *msg = NULL;
  struct timespec ts;
  struct timeval now;
  struct timeval future;

  if (timeout != NULL) {
    gettimeofday(&now, NULL);

    timeradd(&now, timeout, &future);

    ts.tv_sec  = future.tv_sec;
    ts.tv_nsec = future.tv_usec * 1000;

    /*
     * When timedwaits are used, the wait() operation may fail,
     * indicating a timeout.
     */
    suscan_mq_enter(mq);

    if (with_type) {
      while ((msg = suscan_mq_pop_w_type(mq, type)) == NULL)
        if (!suscan_mq_timedwait_unsafe(mq, &ts)) {
          msg = NULL;
          break;
        }
    } else {
      while ((msg = suscan_mq_pop(mq)) == NULL)
        if (!suscan_mq_timedwait_unsafe(mq, &ts)) {
          msg = NULL;
          break;
        }
    }

    suscan_mq_leave(mq);
  } else {
    suscan_mq_enter(mq);

    if (with_type)
      while ((msg = suscan_mq_pop_w_type(mq, type)) == NULL)
        suscan_mq_wait_unsafe(mq);
    else
      while ((msg = suscan_mq_pop(mq)) == NULL)
        suscan_mq_wait_unsafe(mq);

    suscan_mq_leave(mq);
  }

  return msg;
}

SUPRIVATE void *
suscan_mq_read_internal(
    struct suscan_mq *mq,
    uint32_t *ptype,
    uint32_t type,
    const struct timeval *timeout)
{
  struct suscan_msg *msg;
  void *private;

  if ((msg = suscan_mq_read_msg_internal(
      mq,
      ptype == NULL,
      type,
      timeout)) == NULL)
    return NULL;

  private = msg->privdata;

  if (ptype != NULL)
    *ptype = msg->type;

  suscan_msg_destroy(msg);

  return private;
}

void *
suscan_mq_read(struct suscan_mq *mq, uint32_t *type)
{
  return suscan_mq_read_internal(mq, type, 0, NULL);
}

void *
suscan_mq_read_timeout(
    struct suscan_mq *mq,
    uint32_t *type,
    const struct timeval *timeout)
{
  return suscan_mq_read_internal(mq, type, 0, timeout);
}

void *
suscan_mq_read_w_type(struct suscan_mq *mq, uint32_t type)
{
  return suscan_mq_read_internal(mq, NULL, type, NULL);
}

void *
suscan_mq_read_w_type_timeout(
    struct suscan_mq *mq,
    uint32_t type,
    const struct timeval *timeout)
{
  return suscan_mq_read_internal(mq, NULL, type, timeout);
}


struct suscan_msg *
suscan_mq_read_msg(struct suscan_mq *mq)
{
  return suscan_mq_read_msg_internal(mq, SU_FALSE, 0, NULL);
}

struct suscan_msg *
suscan_mq_read_msg_timeout(struct suscan_mq *mq, const struct timeval *timeout)
{
  return suscan_mq_read_msg_internal(mq, SU_FALSE, 0, timeout);
}

struct suscan_msg *
suscan_mq_read_msg_w_type(struct suscan_mq *mq, uint32_t type)
{
  return suscan_mq_read_msg_internal(mq, SU_TRUE, type, NULL);
}

struct suscan_msg *
suscan_mq_read_msg_w_type_timeout(
    struct suscan_mq *mq,
    uint32_t type,
    const struct timeval *timeout)
{
  return suscan_mq_read_msg_internal(mq, SU_TRUE, type, timeout);
}

struct suscan_msg *
suscan_mq_poll_msg_internal(struct suscan_mq *mq, SUBOOL with_type, uint32_t type)
{
  struct suscan_msg *msg;

  suscan_mq_enter(mq);

  if (with_type)
    msg = suscan_mq_pop_w_type(mq, type);
  else
    msg = suscan_mq_pop(mq);

  suscan_mq_leave(mq);

  return msg;
}

SUPRIVATE SUBOOL
suscan_mq_poll_internal(
    struct suscan_mq *mq,
    uint32_t *ptype,
    void **private,
    uint32_t type)
{
  struct suscan_msg *msg;

  msg = suscan_mq_poll_msg_internal(mq, ptype == NULL, type);

  if (msg != NULL) {
    *private = msg->privdata;

    if (ptype != NULL)
      *ptype = msg->type;

    suscan_msg_destroy(msg);

    return SU_TRUE;
  }

  return SU_FALSE;
}

SUBOOL
suscan_mq_poll(struct suscan_mq *mq, uint32_t *type, void **private)
{
  return suscan_mq_poll_internal(mq, type, private, 0);
}

SUBOOL
suscan_mq_poll_w_type(struct suscan_mq *mq, uint32_t type, void **private)
{
  return suscan_mq_poll_internal(mq, NULL, private, type);
}

struct suscan_msg *
suscan_mq_poll_msg(struct suscan_mq *mq)
{
  return suscan_mq_poll_msg_internal(mq, SU_FALSE, 0);
}

struct suscan_msg *
suscan_mq_poll_msg_w_type(struct suscan_mq *mq, uint32_t type)
{
  return suscan_mq_poll_msg_internal(mq, SU_TRUE, type);
}

void
suscan_mq_write_msg(struct suscan_mq *mq, struct suscan_msg *msg)
{
  suscan_mq_enter(mq);

  suscan_mq_push(mq, msg);

  suscan_mq_notify(mq); /* We notify the queue always */

  suscan_mq_leave(mq);
}

void
suscan_mq_write_msg_urgent(struct suscan_mq *mq, struct suscan_msg *msg)
{
  suscan_mq_enter(mq);

  suscan_mq_push_front(mq, msg);

  suscan_mq_notify(mq);

  suscan_mq_leave(mq);
}

SUBOOL
suscan_mq_write(struct suscan_mq *mq, uint32_t type, void *private)
{
  struct suscan_msg *msg;

  if ((msg = suscan_msg_new(type, private)) == NULL)
    return SU_FALSE;

  suscan_mq_write_msg(mq, msg);

  return SU_TRUE;
}

SUBOOL
suscan_mq_write_urgent(struct suscan_mq *mq, uint32_t type, void *private)
{
  struct suscan_msg *msg;

  if ((msg = suscan_msg_new(type, private)) == NULL)
    return SU_FALSE;

  suscan_mq_write_msg_urgent(mq, msg);

  return SU_TRUE;
}

void
suscan_mq_finalize(struct suscan_mq *mq)
{
  struct suscan_msg *msg = NULL;

  if (pthread_cond_destroy(&mq->acquire_cond) == 0) {
    pthread_mutex_destroy(&mq->acquire_lock);

    while ((msg = suscan_mq_pop(mq)) != NULL)
      suscan_msg_destroy(msg);
  }
}

SUBOOL
suscan_mq_init(struct suscan_mq *mq)
{
  if (pthread_mutex_init(&mq->acquire_lock, NULL) == -1)
    return SU_FALSE;

  if (pthread_cond_init(&mq->acquire_cond, NULL) == -1)
    return SU_FALSE;

  mq->head = NULL;
  mq->tail = NULL;

  return SU_TRUE;
}

