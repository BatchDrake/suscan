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

SUPRIVATE pthread_mutex_t g_msg_pool_mutex = PTHREAD_MUTEX_INITIALIZER;
SUPRIVATE struct suscan_msg *g_msg_pool = NULL;
SUPRIVATE int g_msg_pool_size;
SUPRIVATE int g_msg_pool_peak;

SUPRIVATE void
suscan_msg_pool_enter(void)
{
  (void) pthread_mutex_lock(&g_msg_pool_mutex);
}


SUPRIVATE void
suscan_msg_pool_leave(void)
{
  (void) pthread_mutex_unlock(&g_msg_pool_mutex);
}

SUPRIVATE struct suscan_msg *
suscan_mq_alloc_msg(void)
{
  struct suscan_msg *msg = NULL;
  suscan_msg_pool_enter();

  if (g_msg_pool != NULL) {
    msg = g_msg_pool;
    g_msg_pool = msg->free_next;

    --g_msg_pool_size;
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
  SUBOOL in_pool = SU_FALSE;

  suscan_msg_pool_enter();

  if (g_msg_pool_size < SUSCAN_MQ_POOL_OVERFLOW_THRESHOLD) {
    msg->free_next = g_msg_pool;
    g_msg_pool = msg;

    ++g_msg_pool_size;
    if (g_msg_pool_size > g_msg_pool_peak) {
      g_msg_pool_peak    = g_msg_pool_size;
      msg_pool_peak_copy = g_msg_pool_peak;
    }

    in_pool = SU_TRUE;
  }

  suscan_msg_pool_leave();

  if (!in_pool) {
    /* Pool is full. Just free the message. */
    free(msg);
  } else if ((msg_pool_peak_copy % SUSCAN_MQ_POOL_WARNING_THRESHOLD) == 0) {
    SU_WARNING(
        "Message pool freelist grew to %d elements!\n",
        msg_pool_peak_copy);
  }
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

SUPRIVATE SUBOOL
suscan_mq_trigger_cleanup(struct suscan_mq *mq)
{
  void *cu_user = NULL;
  void *mq_user = mq->callbacks.userdata;
  struct suscan_msg *this, *next, *prev = NULL;
  SUBOOL ok = SU_FALSE;

  /* Allocate context, if needed */
  if (mq->callbacks.pre_cleanup != NULL)
    SU_TRY(cu_user = (mq->callbacks.pre_cleanup) (mq, mq_user));
  
  if (mq->callbacks.try_destroy != NULL) {
    this = mq->head;

    while (this != NULL) {
      next = this->next;

      if ((mq->callbacks.try_destroy) (
        mq_user,
        cu_user,
        this->type,
        this->privdata)) {
        /* 
         * Cleanup callback informs that we should remove
         * this message. There are three cases here:
         * 
         * - Remove the first element
         * - Remove an intermediate element
         * - Remove the last element
         * 
         * These cases are not mutually exclusive, as the
         * first element could be the last.
         */

        if (prev != NULL)
          prev->next = next;
        else
          mq->head = next;
        
        if (next == NULL)
          mq->tail = prev;
        
        /* 
         * Remove this message. try_destroy should have released
         * all associated resources to the message (i.e. privdata).
         */
        suscan_msg_destroy(this);
        --mq->count;
      } else {
        /* We keep this one, move to the next */
        prev = this;
      }

      this = next;
    }
  }

  ok = SU_TRUE;
  
done:
  /* Release context, if needed */
  if (cu_user != NULL && mq->callbacks.post_cleanup != NULL)
    (mq->callbacks.post_cleanup) (mq_user, cu_user);
  
  return ok;
}

SUPRIVATE void
suscan_mq_cleanup_if_needed(struct suscan_mq *mq)
{
  if (mq->cleanup_watermark > 0 && mq->count >= mq->cleanup_watermark) {
    SU_WARNING(
      "Too many messages in queue (%d), triggering cleanup\n",
      mq->count);

    if (!suscan_mq_trigger_cleanup(mq))
      SU_ERROR("Failed to trigger cleanup\n");
  }
}

SUPRIVATE void
suscan_mq_push_front(struct suscan_mq *mq, struct suscan_msg *msg)
{
  msg->next = mq->head;
  mq->head = msg;

  if (mq->tail == NULL)
    mq->tail = msg;

  ++mq->count;
  suscan_mq_cleanup_if_needed(mq);
}

SUPRIVATE void
suscan_mq_push(struct suscan_mq *mq, struct suscan_msg *msg)
{
  if (mq->tail != NULL)
    mq->tail->next = msg;

  mq->tail = msg;

  if (mq->head == NULL)
    mq->head = msg;

  ++mq->count;
  suscan_mq_cleanup_if_needed(mq);
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

  --mq->count;

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

  if (this != NULL)
    --mq->count;

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

void
suscan_mq_write_msg_urgent_unsafe(struct suscan_mq *mq, struct suscan_msg *msg)
{
  suscan_mq_push_front(mq, msg);

  suscan_mq_notify(mq);
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

SUBOOL
suscan_mq_write_urgent_unsafe(struct suscan_mq *mq, uint32_t type, void *private)
{
  struct suscan_msg *msg;

  if ((msg = suscan_msg_new(type, private)) == NULL)
    return SU_FALSE;

  suscan_mq_push_front(mq, msg);
  suscan_mq_notify(mq);

  return SU_TRUE;
}

void
suscan_mq_set_cleanup_watermark(
  struct suscan_mq *self,
  unsigned int watermark)
{
  self->cleanup_watermark = watermark;
}

void
suscan_mq_set_callbacks(
  struct suscan_mq *self,
  const struct suscan_mq_callbacks *callbacks)
{
  self->callbacks = *callbacks;
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
  SUBOOL ok = SU_FALSE;
  SUBOOL mutex_init = SU_FALSE;

  memset(mq, 0, sizeof(struct suscan_mq));
  
  SU_TRYZ(pthread_mutex_init(&mq->acquire_lock, NULL));
  mutex_init = SU_TRUE;

  SU_TRYZ(pthread_cond_init(&mq->acquire_cond, NULL));
  
  ok = SU_TRUE;

done:
  if (!ok && mutex_init)
    pthread_mutex_destroy(&mq->acquire_lock);
  
  return ok;
}

