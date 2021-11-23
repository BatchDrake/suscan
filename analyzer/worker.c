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

#define _GNU_SOURCE

#define SU_LOG_DOMAIN "worker"

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
  cb->privdata = private;

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
  struct suscan_msg *msg = NULL;
  struct suscan_worker_callback *cb;
  SUBOOL halt_acked = SU_FALSE;

  while (!worker->halt_req) {
    /* First read: blocking read of a message */
    if ((msg = suscan_mq_read_msg(&worker->mq_in)) == NULL)
      break;

    do {
      switch (msg->type) {
        case SUSCAN_WORKER_MSG_TYPE_CALLBACK:
          cb = (struct suscan_worker_callback *) msg->privdata;
          if (!(cb->func) (worker->mq_out, worker->privdata, cb->privdata)) {
            /* Callback returns FALSE: remove from message queue */
            suscan_worker_callback_destroy(cb);
            suscan_msg_destroy(msg);
          } else {
            /* Callback returns TRUE: queue again */
            suscan_mq_write_msg(&worker->mq_in, msg);
          }
          break;

        case SUSCAN_WORKER_MSG_TYPE_HALT:
          /* Implies halt_req = SU_TRUE */
          goto done;

        default:
          SU_WARNING("Unexpected worker message type #%d\n", msg->type);
          suscan_msg_destroy(msg); /* Destroy message anyways */
      }

      /*
       * We reached this point, one way or another, we don't hold the
       * ownership of the message anymore.
       */
      msg = NULL;

      /* Next reads: until queue is empty */
    } while (
        !worker->halt_req
        && (msg = suscan_mq_poll_msg(&worker->mq_in)) != NULL);
  }

done:
  worker->state = SUSCAN_WORKER_STATE_HALTED;

  if (worker->halt_req) {
    halt_acked = SU_TRUE;

    /*
     * No need to deal with callbacks here. If we reached this point
     * and message is not NULL, it is because we received MSG_TYPE_HALT.
     */
    if (msg != NULL)
      suscan_msg_destroy(msg);

    suscan_worker_ack_halt(worker);
  }

  if (!halt_acked)
    suscan_worker_wait_for_halt(worker);

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
  worker->halt_req = SU_TRUE;

  suscan_mq_write_urgent(
      &worker->mq_in,
      SUSCAN_WORKER_MSG_TYPE_HALT,
      NULL);
}

SUBOOL
suscan_worker_destroy(suscan_worker_t *worker)
{
  void *cb;
  uint32_t type;

  if (worker->state == SUSCAN_WORKER_STATE_RUNNING) {
    SU_ERROR("Cannot destroy worker %p: still running\n", worker);
    return SU_FALSE;
  }

  if (worker->state == SUSCAN_WORKER_STATE_HALTED)
    if (pthread_join(worker->thread, NULL) == -1) {
      SU_ERROR("Thread failed to join, memory leak ahead\n");
      return SU_FALSE;
    }

  /* Thread stopped, pop all messages and release memory */
  while (suscan_mq_poll(&worker->mq_in, &type, &cb))
    if (type == SUSCAN_WORKER_MSG_TYPE_CALLBACK)
      suscan_worker_callback_destroy((struct suscan_worker_callback *) cb);

  suscan_mq_finalize(&worker->mq_in);

  free(worker);

  return SU_TRUE;
}

SUBOOL
suscan_worker_halt(suscan_worker_t *worker)
{
  uint32_t type;

  while (worker->state == SUSCAN_WORKER_STATE_RUNNING) {
    suscan_worker_req_halt(worker);

    /* This worker should not push messages */
    suscan_mq_read(worker->mq_out, &type);

    if (type != SUSCAN_WORKER_MSG_TYPE_HALT) {
      SU_ERROR("Unexpected worker message type\n");
      return SU_FALSE;
    }
  }

  return suscan_worker_destroy(worker);
}

suscan_worker_t *
suscan_worker_new_ex(
    const char *name,
    struct suscan_mq *mq_out,
    void *private)
{
  suscan_worker_t *new = NULL;

  if ((new = calloc(1, sizeof (suscan_worker_t))) == NULL)
    goto fail;

  new->state = SUSCAN_WORKER_STATE_CREATED;
  new->mq_out = mq_out;
  new->privdata = private;

  if (!suscan_mq_init(&new->mq_in))
    goto fail;

  if (pthread_create(
      &new->thread,
      NULL,
      suscan_worker_thread,
      new) == -1)
    goto fail;

#if defined(__GNUC__) && !defined(__APPLE__)
  (void) pthread_setname_np(new->thread, name);
#endif /* __GNUC__ */

  new->state = SUSCAN_WORKER_STATE_RUNNING;

  return new;

fail:
  if (new != NULL)
    suscan_worker_destroy(new);

  return NULL;
}

suscan_worker_t *
suscan_worker_new(
    struct suscan_mq *mq_out,
    void *private)
{
  return suscan_worker_new_ex("suscan_worker", mq_out, private);
}