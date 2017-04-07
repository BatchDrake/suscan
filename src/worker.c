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

#include "worker.h"

/*
 * worker.c: It's essentially a consumer of asynchronous callbacks. However,
 * the object they work on *doesn't belong to it*. It's just a way to
 * delegate the burden of expensive calculation to different threads.
 */


SUPRIVATE struct suscan_worker_callback *
suscan_worker_callback_new(
    SUBOOL (*func) (
        struct suscan_mq *mq_out,
      void *worker_private,
      void *callback_private),
  void *private)
{
  struct suscan_worker_callback *cb;

  if ((cb = malloc(sizeof (struct suscan_worker_callback))) == NULL)
    return NULL;

  cb->func = func;
  cb->private = private;

  return cb;
}

SUPRIVATE void
suscan_worker_callback_destroy(struct suscan_worker_callback *callback)
{
  free(callback);
}

SUPRIVATE void
suscan_worker_ack_halt(suscan_worker_t *worker)
{
  suscan_mq_write_urgent(
      worker->mq_out,
      SUSCAN_WORKER_MSG_TYPE_HALT,
      worker); /* Inform which worker has just been halted */
}

SUPRIVATE void
suscan_worker_wait_for_halt(suscan_worker_t *worker)
{
  uint32_t type;
  struct suscan_worker_callback *cb;

  for (;;) {
    cb = suscan_mq_read(&worker->mq_in, &type);
    if (type == SUSCAN_WORKER_MSG_TYPE_HALT) {
      suscan_worker_ack_halt(worker);
      break;
    }

    suscan_worker_callback_destroy(cb);
  }
}

SUPRIVATE void *
suscan_worker_thread(void *data)
{
  suscan_worker_t *worker = (suscan_worker_t *) data;
  struct suscan_worker_callback *cb;
  SUBOOL halt_acked = SU_FALSE;
  uint32_t type;

  while ((cb = suscan_mq_read(&worker->mq_in, &type)) != NULL) {
    if (type == SUSCAN_WORKER_MSG_TYPE_CALLBACK) {
      if (!(cb->func) (worker->mq_out, worker->private, cb->private)) {
        /* Callback returns FALSE: remove from message queue */
        suscan_worker_callback_destroy(cb);
      } else {
        /* Callback returns TRUE: queue again */
        if (!suscan_mq_write(&worker->mq_in, type, cb))
          goto done;
      }
    } else if (type == SUSCAN_WORKER_MSG_TYPE_HALT) {
      suscan_worker_ack_halt(worker);
      halt_acked = SU_TRUE;
      goto done;
    }
  }

done:
  if (!halt_acked)
    suscan_worker_wait_for_halt(worker);

halt:
  worker->state = SUSCAN_WORKER_STATE_HALTED;

  pthread_exit(NULL);

  return NULL;
}

SUBOOL
suscan_worker_push(
    suscan_worker_t *worker,
    SUBOOL (*func) (
          struct suscan_mq *mq_out,
          void *worker_private,
          void *callback_private),
    void *private)
{
  struct suscan_worker_callback *cb;

  if ((cb = suscan_worker_callback_new(func, private)) == NULL)
    return SU_FALSE;

  if (!suscan_mq_write(&worker->mq_in, SUSCAN_WORKER_MSG_TYPE_CALLBACK, cb)) {
    suscan_worker_callback_destroy(cb);
    return SU_FALSE;
  }

  return SU_TRUE;
}

void
suscan_worker_req_halt(suscan_worker_t *worker)
{
  suscan_mq_write_urgent(
      &worker->mq_in,
      SUSCAN_WORKER_MSG_TYPE_HALT,
      NULL);
}

SUBOOL
suscan_worker_destroy(suscan_worker_t *worker)
{
  if (worker->state == SUSCAN_WORKER_STATE_RUNNING) {
    SU_ERROR("Cannot destroy worker %p: still running\n", worker);
    return SU_FALSE;
  }

  if (worker->state == SUSCAN_WORKER_STATE_HALTED)
    if (pthread_join(worker->thread, NULL) == -1) {
      SU_ERROR("Thread failed to join, memory leak ahead\n");
      return SU_FALSE;
    }

  /* Thread stopped, safe to free everything */
  suscan_mq_finalize(&worker->mq_in);

  free(worker);

  return SU_TRUE;
}

suscan_worker_t *
suscan_worker_new(
    struct suscan_mq *mq_out,
    void *private)
{
  suscan_worker_t *new = NULL;

  if ((new = calloc(1, sizeof (suscan_worker_t))) == NULL)
    goto fail;

  new->state = SUSCAN_WORKER_STATE_CREATED;
  new->mq_out = mq_out;
  new->private = private;

  if (!suscan_mq_init(&new->mq_in))
    goto fail;

  if (pthread_create(
      &new->thread,
      NULL,
      suscan_worker_thread,
      new) == -1)
    goto fail;

  new->state = SUSCAN_WORKER_STATE_RUNNING;

  return new;

fail:
  if (new != NULL)
    suscan_worker_destroy(new);

  return NULL;
}
