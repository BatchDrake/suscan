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
#include <unistd.h>
#include <pthread.h>
#include <stdint.h>
#include <time.h>

/*
 * Consumer objects keep a local copy of the last retrieved samples. A consumer
 * is enabled as soon as its task counter becomes to non-zero. Then, it pushes
 * a persistent callback that reads from the consumer's slave port in
 * each run, populating its buffer. Consumer tasks will use this buffer to read
 * directly.
 */

#define SU_LOG_DOMAIN "consumer"

#include <sigutils/sigutils.h>
#include <sigutils/detect.h>

#include "analyzer.h"
#include "msg.h"

SUPRIVATE SUBOOL
suscan_consumer_cb(
    struct suscan_mq *mq_out,
    void *wk_private,
    void *cb_private)
{
  suscan_consumer_t *consumer = (suscan_consumer_t *) wk_private;
  SUSDIFF got;
  SUSCOUNT p = 0;
  SUSDIFF size = consumer->buffer_size;
  SUBOOL mutex_acquired = SU_FALSE;

  /*
   * This mutex protects the consumer against push() and remove()
   * operations from different threads. This mutex will not sleep
   * most of the time.
   */
  SU_TRYCATCH(pthread_mutex_lock(&consumer->lock) != -1, goto fail);

  mutex_acquired = SU_TRUE;

  if (consumer->tasks == 0) {
    if (consumer->idle_counter == 0) {
      SU_INFO("Consumer %p passed to idle state\n", consumer);
      consumer->consuming = SU_FALSE;

      su_block_port_unplug(&consumer->port);

      pthread_mutex_unlock(&consumer->lock);

      return SU_FALSE; /* Remove consumer callback */
    } else {
      --consumer->idle_counter;
    }
  }

  while (size > 0) {
    got = su_block_port_read(&consumer->port, consumer->buffer + p, size);

    /* Normal read */
    if (got > 0) {
      p += got;
      size -= got;
    } else {
      switch (got) {
        case SU_BLOCK_PORT_READ_ERROR_PORT_DESYNC:
          SU_WARNING("Samples lost by consumer (normal in slow CPUs)\n");
          su_block_port_resync(&consumer->port);
          break;

        default:
          suscan_analyzer_send_status(
              consumer->analyzer,
              SUSCAN_ANALYZER_MESSAGE_TYPE_EOS,
              got,
              "Consumer worker EOS");
          goto fail;
      }
    }
  }

  consumer->buffer_pos += p;

  SU_TRYCATCH(pthread_mutex_unlock(&consumer->lock) != -1, goto fail);

  return SU_TRUE;

fail:
  consumer->failed = SU_TRUE;

  if (mutex_acquired)
    pthread_mutex_unlock(&consumer->lock);

  suscan_worker_req_halt(consumer->worker);

  return SU_FALSE;
}

const SUCOMPLEX *
suscan_consumer_get_buffer(const suscan_consumer_t *consumer)
{
  return consumer->buffer;
}

SUSCOUNT
suscan_consumer_get_buffer_size(const suscan_consumer_t *consumer)
{
  return consumer->buffer_size;
}

SUSCOUNT
suscan_consumer_get_buffer_pos(const suscan_consumer_t *consumer)
{
  return consumer->buffer_pos;
}

SUBOOL
suscan_consumer_push_task(
    suscan_consumer_t *consumer,
    SUBOOL (*func) (
              struct suscan_mq *mq_out,
              void *wk_private,
              void *cb_private),
    void *private)
{
  SUBOOL mutex_acquired = SU_FALSE;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(pthread_mutex_lock(&consumer->lock) != -1, goto done);

  mutex_acquired = SU_TRUE;

  if (!consumer->consuming) {
    if (!su_block_port_plug(
        &consumer->port,
        consumer->analyzer->source.block,
        0)) {
      SU_ERROR("Failed to push task: cannot plug port\n");
      goto done;
    }

    /*
     * Worker thread will block as suscan_consumer_cb will try to acquire
     * consumer->lock
     */
    if (!suscan_worker_push(consumer->worker, suscan_consumer_cb, NULL)) {
      SU_ERROR("Failed to push consumer callback\n");

      su_block_port_unplug(&consumer->port);

      goto done;
    }

    consumer->consuming = SU_TRUE;
  }

  /* Restart consumer counter */
  if (consumer->tasks++ == 0)
    consumer->idle_counter = SUSCAN_CONSUMER_IDLE_COUNTER;

  /* This task will be executed after suscan_consumer_cb */
  if (!suscan_worker_push(consumer->worker, func, private)) {
    (void) suscan_consumer_remove_task(consumer);

    goto done;
  }

  ok = SU_TRUE;

done:
  if (mutex_acquired)
    SU_TRYCATCH(pthread_mutex_unlock(&consumer->lock) != -1, goto done);

  return ok;
}

SUBOOL
suscan_consumer_remove_task(suscan_consumer_t *consumer)
{
  SUBOOL mutex_acquired = SU_FALSE;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(pthread_mutex_lock(&consumer->lock) != -1, goto done);

  mutex_acquired = SU_TRUE;

  if (consumer->tasks == 0) {
    SU_ERROR("Cannot remove tasks: task counter already 0\n");
    goto done;
  }

  SU_TRYCATCH(!consumer->failed, goto done);

  /* suscan_consumer_cb will handle 0 tasks on its own */
  --consumer->tasks;

  ok = SU_TRUE;

done:
  if (mutex_acquired)
    SU_TRYCATCH(pthread_mutex_unlock(&consumer->lock) != -1, goto done);

  return ok;
}

SUBOOL
suscan_consumer_destroy(suscan_consumer_t *cons)
{
  if (cons->worker != NULL) {
    if (!suscan_analyzer_halt_worker(cons->worker)) {
      SU_ERROR("Consumer worker destruction failed, memory leak ahead\n");
      return SU_FALSE;
    }
  }

  su_block_port_unplug(&cons->port);

  pthread_mutex_destroy(&cons->lock);

  if (cons->buffer != NULL)
    free(cons->buffer);

  free(cons);

  return SU_TRUE;
}

suscan_consumer_t *
suscan_consumer_new(struct suscan_analyzer *analyzer)
{
  suscan_consumer_t *new = NULL;
  pthread_mutexattr_t attr;
  SUBOOL attr_init = SU_FALSE;

  SU_TRYCATCH(new = calloc(1, sizeof (suscan_consumer_t)), goto fail);

  SU_TRYCATCH(pthread_mutexattr_init(&attr) != -1, goto fail);

  attr_init = SU_TRUE;

  SU_TRYCATCH(
      pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE) != -1,
      goto fail);

  SU_TRYCATCH(
      pthread_mutex_init(&new->lock, &attr) != -1,
      goto fail);

  pthread_mutexattr_destroy(&attr);

  attr_init = SU_FALSE;

  new->buffer_size = analyzer->read_size;

  SU_TRYCATCH(
      new->buffer = malloc(new->buffer_size * sizeof(SUCOMPLEX)),
      goto fail);

  new->analyzer = analyzer;

  SU_TRYCATCH(
      new->worker = suscan_worker_new(&analyzer->mq_in, new),
      goto fail);

  return new;

fail:
  if (!attr_init)
    pthread_mutexattr_destroy(&attr);

  if (new != NULL)
    suscan_consumer_destroy(new);

  return NULL;
}
