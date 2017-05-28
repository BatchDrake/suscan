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

#include <sigutils/sigutils.h>
#include <sigutils/detect.h>

#include "mq.h"
#include "source.h"
#include "analyzer.h"
#include "msg.h"

/************************ Source worker callback *****************************/
SUPRIVATE void
timespecsub(
    struct timespec *a,
    struct timespec *b,
    struct timespec *sub)
{
  sub->tv_sec = a->tv_sec - b->tv_sec;
  sub->tv_nsec = a->tv_nsec - b->tv_nsec;

  if (sub->tv_nsec < 0) {
    sub->tv_nsec += 1000000000;
    --sub->tv_sec;
  }
}

SUPRIVATE SUBOOL
suscan_source_wk_cb(
    struct suscan_mq *mq_out,
    void *wk_private,
    void *cb_private)
{
  suscan_analyzer_t *analyzer = (suscan_analyzer_t *) wk_private;
  struct suscan_analyzer_source *source =
      (struct suscan_analyzer_source *) cb_private;
  SUSDIFF got;
  struct timespec read_start;
  struct timespec process_start;
  struct timespec process_end;
  struct timespec sub;
  uint64_t total, cpu;
  SUBOOL restart = SU_FALSE;

  clock_gettime(CLOCK_MONOTONIC_RAW, &read_start);
  if ((got = su_block_port_read(
      &source->port,
      analyzer->read_buf,
      analyzer->read_size)) > 0) {
    clock_gettime(CLOCK_MONOTONIC_RAW, &process_start);
    if (su_channel_detector_feed_bulk(
        source->detector,
        analyzer->read_buf,
        got) < got)
      goto done;
    clock_gettime(CLOCK_MONOTONIC_RAW, &process_end);

    /* Compute CPU usage */
    timespecsub(&process_end, &read_start, &sub);
    total = sub.tv_sec * 1000000000 + sub.tv_nsec;

    timespecsub(&process_end, &process_start, &sub);
    cpu = sub.tv_sec * 1000000000 + sub.tv_nsec;

    if (total == 0)
      analyzer->cpu_usage = 1.;
    else
      analyzer->cpu_usage = (SUFLOAT) cpu / (SUFLOAT) total;

    source->per_cnt_channels += got;
    source->per_cnt_psd += got;

    /* Check channel update */
    if (source->interval_channels > 0) {
      if (source->per_cnt_channels
          >= source->interval_channels * source->detector->params.samp_rate) {
        source->per_cnt_channels = 0;

        if (!suscan_analyzer_send_detector_channels(analyzer, source->detector))
          goto done;
      }
    }

    /* Check spectrum update */
    if (source->interval_psd > 0) {
      if (source->per_cnt_psd
          >= source->interval_psd * source->detector->params.samp_rate) {
        source->per_cnt_psd = 0;

        if (!suscan_analyzer_send_psd(analyzer, source->detector))
          goto done;
      }
    }
  } else {
    analyzer->eos = SU_TRUE;
    analyzer->cpu_usage = 0;

    switch (got) {
      case SU_BLOCK_PORT_READ_END_OF_STREAM:
        suscan_analyzer_send_status(
            analyzer,
            SUSCAN_ANALYZER_MESSAGE_TYPE_EOS,
            got,
            "End of stream reached");
        break;

      case SU_BLOCK_PORT_READ_ERROR_NOT_INITIALIZED:
        suscan_analyzer_send_status(
            analyzer,
            SUSCAN_ANALYZER_MESSAGE_TYPE_EOS,
            got,
            "Port not initialized");
        break;

      case SU_BLOCK_PORT_READ_ERROR_ACQUIRE:
        suscan_analyzer_send_status(
            analyzer,
            SUSCAN_ANALYZER_MESSAGE_TYPE_EOS,
            got,
            "Acquire failed (source I/O error)");
        break;

      case SU_BLOCK_PORT_READ_ERROR_PORT_DESYNC:
        suscan_analyzer_send_status(
            analyzer,
            SUSCAN_ANALYZER_MESSAGE_TYPE_EOS,
            got,
            "Port desync");
        break;

      default:
        suscan_analyzer_send_status(
            analyzer,
            SUSCAN_ANALYZER_MESSAGE_TYPE_EOS,
            got,
            "Unexpected read result %d", got);
    }

    goto done;
  }

  restart = SU_TRUE;

done:
  return restart;
}


/************************* Main analyzer thread *******************************/
void
suscan_analyzer_req_halt(suscan_analyzer_t *analyzer)
{
  analyzer->halt_requested = SU_TRUE;

  suscan_mq_write_urgent(
      &analyzer->mq_in,
      SUSCAN_WORKER_MSG_TYPE_HALT,
      NULL);
}

SUPRIVATE void
suscan_analyzer_ack_halt(suscan_analyzer_t *analyzer)
{
  suscan_mq_write_urgent(
      analyzer->mq_out,
      SUSCAN_WORKER_MSG_TYPE_HALT,
      NULL);
}

SUPRIVATE void
suscan_wait_for_halt(suscan_analyzer_t *analyzer)
{
  uint32_t type;
  void *private;

  for (;;) {
    private = suscan_mq_read(&analyzer->mq_in, &type);
    if (type == SUSCAN_WORKER_MSG_TYPE_HALT) {
      suscan_analyzer_ack_halt(analyzer);
      break;
    }

    suscan_analyzer_dispose_message(type, private);
  }
}


SUPRIVATE void *
suscan_analyzer_thread(void *data)
{
  suscan_analyzer_t *analyzer = (suscan_analyzer_t *) data;
  void *private;
  uint32_t type;
  SUBOOL halt_acked = SU_FALSE;

  if (!suscan_worker_push(
      analyzer->source_wk,
      suscan_source_wk_cb,
      &analyzer->source)) {
    suscan_analyzer_send_status(
        analyzer,
        SUSCAN_ANALYZER_MESSAGE_TYPE_SOURCE_INIT,
        SUSCAN_ANALYZER_INIT_FAILURE,
        "Failed to push async callback to worker");
    goto done;
  }

  /* Signal initialization success */
  suscan_analyzer_send_status(
      analyzer,
      SUSCAN_ANALYZER_MESSAGE_TYPE_SOURCE_INIT,
      SUSCAN_ANALYZER_INIT_SUCCESS,
      NULL);

  /* Pop all messages from queue before reading from the source */
  for (;;) {
    /* First read: blocks */
    private = suscan_mq_read(&analyzer->mq_in, &type);

    do {
      switch (type) {
        case SUSCAN_WORKER_MSG_TYPE_HALT:
          suscan_analyzer_ack_halt(analyzer);
          halt_acked = SU_TRUE;
          /* Nothing to dispose, safe to break the loop */
          goto done;

        case SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR:
          /* Baudrate inspector command. Handle separately */
          if (!suscan_analyzer_parse_inspector_msg(analyzer, private)) {
            suscan_analyzer_dispose_message(type, private);
            goto done;
          }

          /*
           * We don't dispose this message: it has been processed
           * by the baud inspector API and forwarded it to the
           * output mq
           */
          private = NULL;

          break;

        /* Forward these messages to output */
        case SUSCAN_ANALYZER_MESSAGE_TYPE_EOS:
        case SUSCAN_ANALYZER_MESSAGE_TYPE_CHANNEL:
          if (!suscan_mq_write(analyzer->mq_out, type, private))
            goto done;

          /* Not belonging to us anymore */
          private = NULL;

          break;
      }

      if (private != NULL)
        suscan_analyzer_dispose_message(type, private);

      /* Next reads: until message queue is empty */
    } while (suscan_mq_poll(&analyzer->mq_in, &type, &private));
  }

done:
  if (!halt_acked)
    suscan_wait_for_halt(analyzer);

  analyzer->running = SU_FALSE;

  pthread_exit(NULL);

  return NULL;
}

void *
suscan_analyzer_read(suscan_analyzer_t *analyzer, uint32_t *type)
{
  return suscan_mq_read(analyzer->mq_out, type);
}

struct suscan_analyzer_inspector_msg *
suscan_analyzer_read_inspector_msg(suscan_analyzer_t *analyzer)
{
  /* TODO: use poll and wait to wait for EOS and inspector messages */
  return suscan_mq_read_w_type(
      analyzer->mq_out,
      SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR);
}

SUBOOL
suscan_analyzer_write(suscan_analyzer_t *analyzer, uint32_t type, void *priv)
{
  return suscan_mq_write(&analyzer->mq_in, type, priv);
}

void
suscan_analyzer_consume_mq(struct suscan_mq *mq)
{
  void *private;
  uint32_t type;

  while (suscan_mq_poll(mq, &type, &private))
    suscan_analyzer_dispose_message(type, private);
}

SUPRIVATE SUBOOL
suscan_analyzer_consume_mq_until_halt(struct suscan_mq *mq)
{
  void *private;
  uint32_t type;

  while (suscan_mq_poll(mq, &type, &private))
    if (type == SUSCAN_WORKER_MSG_TYPE_HALT)
      return SU_TRUE;
    else
      suscan_analyzer_dispose_message(type, private);

  return SU_FALSE;
}

SUPRIVATE SUBOOL
suscan_analyzer_halt_worker(suscan_worker_t *worker)
{
  while (worker->state == SUSCAN_WORKER_STATE_RUNNING) {
    suscan_worker_req_halt(worker);

    while (!suscan_analyzer_consume_mq_until_halt(worker->mq_out))
      suscan_mq_wait(worker->mq_out);
  }

  return suscan_worker_destroy(worker);
}

/************************ Suscan consumer API *********************************/

/*
 * Consumer objects keep a local copy of the last retrieved samples. A consumer
 * is enabled as soon as its task counter becomes to non-zero. Then, it pushes
 * a persistent callback that reads from the consumer's slave port in
 * each run, populating its buffer. Consumer tasks will use this buffer to read
 * directly.
 */

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
suscan_consumer_new(suscan_analyzer_t *analyzer)
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

/******************* Suscan analyzer source methods **************************/
SUPRIVATE void
suscan_analyzer_source_finalize(struct suscan_analyzer_source *source)
{
  su_block_port_unplug(&source->port);

  if (source->detector != NULL)
    su_channel_detector_destroy(source->detector);

  if (source->block != NULL)
    su_block_destroy(source->block);
}

SUPRIVATE SUBOOL
suscan_analyzer_source_init_from_block_properties(
    struct suscan_analyzer_source *source,
    struct sigutils_channel_detector_params *params)
{
  const uint64_t *samp_rate;
  const uint64_t *fc;

  /* Retrieve sample rate. All source blocks must expose this property */
  if ((samp_rate = su_block_get_property_ref(
      source->block,
      SU_PROPERTY_TYPE_INTEGER,
      "samp_rate")) != NULL) {
    params->samp_rate = *samp_rate;
  } else {
    SU_ERROR("Failed to get sample rate");
    return SU_FALSE;
  }

  /* Retrieve center frequency, if any */
  if ((fc = su_block_get_property_ref(
      source->block,
      SU_PROPERTY_TYPE_INTEGER,
      "fc")) != NULL)
    source->fc = *fc;

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_analyzer_source_init(
    struct suscan_analyzer_source *source,
    struct suscan_source_config *config)
{
  SUBOOL ok = SU_FALSE;
  struct sigutils_channel_detector_params params =
        sigutils_channel_detector_params_INITIALIZER;

  params.mode = SU_CHANNEL_DETECTOR_MODE_DISCOVERY;
  params.alpha = 1e-2;
  params.window_size = config->bufsiz;

  source->config = config;


  source->per_cnt_channels  = 0;
  source->per_cnt_psd       = 0;

  source->interval_channels = .1;
  source->interval_psd      = .1;

  if ((source->block = (config->source->ctor)(config)) == NULL)
    goto done;

  /*
   * Analyze block properties, and initialize source and detector
   * parameters accordingly.
   */
  if (!suscan_analyzer_source_init_from_block_properties(source, &params))
    goto done;

  if ((source->detector = su_channel_detector_new(&params)) == NULL)
    goto done;

  if (!su_block_port_plug(&source->port, source->block, 0))
    goto done;

  if (config->source->real_time) {
    /*
     * With real time sources, we must use the master/slave flow controller.
     * This way, we ensure that at least the channel detector works
     * correctly.
     */
    if (!su_block_set_flow_controller(
        source->block,
        0,
        SU_FLOW_CONTROL_KIND_MASTER_SLAVE))
      goto done;

    /*
     * This is the master port. Other readers must wait for this reader
     * to complete before asking block for additional samples.
     */
    if (!su_block_set_master_port(source->block, 0, &source->port))
      goto done;
  } else {
    /*
     * If source is not realtime (e.g. iqfile or wavfile) we can afford
     * having a BARRIER flow controller here: samples will be available on
     * demand, and it's safe to pause until all workers are ready to consume
     * more samples
     */
    if (!su_block_set_flow_controller(
        source->block,
        0,
        SU_FLOW_CONTROL_KIND_BARRIER))
      goto done;
  }

  ok = SU_TRUE;

done:
  return ok;
}

/********************** Suscan analyzer public API ***************************/
SUPRIVATE unsigned int
suscan_get_min_consumer_workers(void)
{
  long count;

  if ((count = sysconf(_SC_NPROCESSORS_ONLN)) < 2)
    count = 2;

  return count - 1;
}

SUBOOL
suscan_analyzer_push_task(
    suscan_analyzer_t *analyzer,
    SUBOOL (*func) (
          struct suscan_mq *mq_out,
          void *wk_private,
          void *cb_private),
    void *private)
{
  if (!suscan_consumer_push_task(
      analyzer->consumer_list[analyzer->next_consumer],
      func,
      private))
    return SU_FALSE;

  /* Increment next consumer counter */
  analyzer->next_consumer =
      (analyzer->next_consumer + 1) % analyzer->consumer_count;

  return SU_TRUE;
}

void
suscan_analyzer_destroy(suscan_analyzer_t *analyzer)
{
  uint32_t type;
  unsigned int i;

  void *private;

  if (analyzer->running) {
    if (!analyzer->halt_requested) {
      suscan_analyzer_req_halt(analyzer);

      /*
       * TODO: this cannot wait forever. Add suscan_mq_read_with_timeout
       */
      while (!suscan_analyzer_consume_mq_until_halt(analyzer->mq_out))
        suscan_mq_wait(analyzer->mq_out);
    }

    /* TODO: add a timeout here too */
    if (pthread_join(analyzer->thread, NULL) == -1) {
      SU_ERROR("Thread failed to join, memory leak ahead\n");
      return;
    }
  }

  /* We attempt to wakeup all threads by marking the source as EOS */
  if (analyzer->source.block != NULL)
    su_block_force_eos(analyzer->source.block, 0);

  if (analyzer->source_wk != NULL)
    if (!suscan_analyzer_halt_worker(analyzer->source_wk)) {
      SU_ERROR("Source worker destruction failed, memory leak ahead\n");
      return;
    }

  for (i = 0; i < analyzer->consumer_count; ++i)
    if (analyzer->consumer_list[i] != NULL)
      if (!suscan_consumer_destroy(analyzer->consumer_list[i])) {
        SU_ERROR("Consumer worker destruction failed, memory leak ahead\n");
        return;
      }

  /* Consume any pending messages */
  suscan_analyzer_consume_mq(&analyzer->mq_in);

  /* All workers destroyed, it's safe now to delete the worker list */
  if (analyzer->consumer_list != NULL)
    free(analyzer->consumer_list);

  /* Free read buffer */
  if (analyzer->read_buf != NULL)
    free(analyzer->read_buf);

  /* Remove all channel analyzers */
  for (i = 0; i < analyzer->inspector_count; ++i)
    if (analyzer->inspector_list[i] != NULL)
      suscan_inspector_destroy(analyzer->inspector_list[i]);

  if (analyzer->inspector_list != NULL)
    free(analyzer->inspector_list);

  /* Delete source information */
  suscan_analyzer_source_finalize(&analyzer->source);

  suscan_mq_finalize(&analyzer->mq_in);

  free(analyzer);
}

suscan_analyzer_t *
suscan_analyzer_new(
    struct suscan_source_config *config,
    struct suscan_mq *mq)
{
  suscan_analyzer_t *analyzer = NULL;
  suscan_consumer_t *consumer;
  unsigned int worker_count;
  unsigned int i;

  if ((analyzer = calloc(1, sizeof (suscan_analyzer_t))) == NULL) {
    SU_ERROR("Cannot allocate analyzer\n");
    goto fail;
  }

  /* Allocate read buffer */
  if ((analyzer->read_buf = malloc(config->bufsiz * sizeof(SUCOMPLEX)))
      == NULL) {
    SU_ERROR("Failed to allocate read buffer\n");
    goto fail;
  }

  analyzer->read_size = config->bufsiz;

  /* Create input message queue */
  if (!suscan_mq_init(&analyzer->mq_in)) {
    SU_ERROR("Cannot allocate input MQ\n");
    goto fail;
  }

  /* Initialize source */
  if (!suscan_analyzer_source_init(&analyzer->source, config)) {
    SU_ERROR("Failed to initialize source\n");
    goto fail;
  }

  /* Create source worker */
  if ((analyzer->source_wk = suscan_worker_new(&analyzer->mq_in, analyzer))
      == NULL) {
    SU_ERROR("Cannot create source worker thread\n");
    goto fail;
  }

  /* Create consumer workers */
  worker_count = suscan_get_min_consumer_workers();
  for (i = 0; i < worker_count; ++i) {
    if ((consumer = suscan_consumer_new(analyzer)) == NULL) {
      SU_ERROR("Failed to create consumer object\n");
      goto fail;
    }

    if (PTR_LIST_APPEND_CHECK(analyzer->consumer, consumer) == -1) {
      SU_ERROR("Cannot append consumer to list\n");
      suscan_consumer_destroy(consumer);
      goto fail;
    }
  }

  analyzer->mq_out = mq;

  if (pthread_create(
      &analyzer->thread,
      NULL,
      suscan_analyzer_thread,
      analyzer) == -1) {
    SU_ERROR("Cannot create main thread\n");
    goto fail;
  }

  analyzer->running = SU_TRUE;

  return analyzer;

fail:
  if (analyzer != NULL)
    suscan_analyzer_destroy(analyzer);

  return NULL;
}
