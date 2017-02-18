/*

  Copyright (C) 2017 Gonzalo José Carracedo Carballal

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

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <libgen.h>
#include <pthread.h>
#include <stdint.h>

#include <pthread.h>
#include "suscan.h"

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
suscan_mq_wait(struct suscan_mq *mq)
{
  pthread_cond_wait(&mq->acquire_cond, &mq->acquire_lock);
}

SUPRIVATE struct suscan_msg *
suscan_msg_new(uint32_t type, void *private)
{
  struct suscan_msg *new;

  if ((new = malloc(sizeof (struct suscan_msg))) == NULL)
      return NULL;

  new->type = type;
  new->private = private;
  new->next = NULL;

  return new;
}

SUPRIVATE void
suscan_msg_destroy(struct suscan_msg *msg)
{
  free(msg);
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

  return msg;
}

void *
suscan_mq_read(struct suscan_mq *mq, uint32_t *type)
{
  struct suscan_msg *msg;
  void *private;

  suscan_mq_enter(mq);

  while ((msg = suscan_mq_pop(mq)) == NULL)
    suscan_mq_wait(mq);

  suscan_mq_leave(mq);

  private = msg->private;
  *type = msg->type;

  suscan_msg_destroy(msg);

  return private;
}

SUBOOL
suscan_mq_write(struct suscan_mq *mq, uint32_t type, void *private)
{
  struct suscan_msg *msg;

  if ((msg = suscan_msg_new(type, private)) == NULL)
    return SU_FALSE;

  suscan_mq_enter(mq);

  suscan_mq_push(mq, msg);

  if (mq->head == mq->tail)
    suscan_mq_notify(mq);

  suscan_mq_leave(mq);

  return SU_TRUE;
}

SUBOOL
suscan_mq_write_urgent(struct suscan_mq *mq, uint32_t type, void *private)
{
  struct suscan_msg *msg;

  if ((msg = suscan_msg_new(type, private)) == NULL)
    return SU_FALSE;

  suscan_mq_enter(mq);

  suscan_mq_push_front(mq, msg);

  if (mq->head == mq->tail)
    suscan_mq_notify(mq);

  suscan_mq_leave(mq);

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

