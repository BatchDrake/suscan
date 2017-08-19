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

#define SU_LOG_DOMAIN "analyzer"

#include <sigutils/sigutils.h>
#include <sigutils/detect.h>

#include "analyzer.h"

#include "mq.h"
#include "msg.h"

SUPRIVATE void suscan_analyzer_params_to_detector_params(
    struct sigutils_channel_detector_params *params,
    const struct suscan_analyzer_params *analyzer_params);

/*********************** Performance measurement *****************************/
SUPRIVATE void
suscan_analyzer_read_start(suscan_analyzer_t *analyzer)
{
  clock_gettime(CLOCK_MONOTONIC_RAW, &analyzer->read_start);
}

SUPRIVATE void
suscan_analyzer_process_start(suscan_analyzer_t *analyzer)
{
  clock_gettime(CLOCK_MONOTONIC_RAW, &analyzer->process_start);
}

SUPRIVATE void
suscan_analyzer_process_end(suscan_analyzer_t *analyzer)
{
  struct timespec sub;
  uint64_t total, cpu;

  clock_gettime(CLOCK_MONOTONIC_RAW, &analyzer->process_end);

  if (analyzer->read_start.tv_sec > 0) {
    timespecsub(&analyzer->process_end, &analyzer->read_start, &sub);
    total = sub.tv_sec * 1000000000 + sub.tv_nsec;

    timespecsub(&analyzer->process_end, &analyzer->process_start, &sub);
    cpu = sub.tv_sec * 1000000000 + sub.tv_nsec;

    /* Update CPU usage */
    if (total == 0)
      analyzer->cpu_usage +=
          SUSCAN_ANALYZER_CPU_USAGE_UPDATE_ALPHA
          * (1. - analyzer->cpu_usage);
    else
      analyzer->cpu_usage +=
          SUSCAN_ANALYZER_CPU_USAGE_UPDATE_ALPHA
          * ((SUFLOAT) cpu / (SUFLOAT) total - analyzer->cpu_usage);
  }
}

/************************ Source worker callback *****************************/
#ifdef SUSCAN_DEBUG_THROTTLE
SUBOOL   dbg_rate_set;
struct timespec dbg_rate_source_start;
SUSCOUNT dbg_rate_counter;
SUFLOAT  dbg_rate_mean;
SUSCOUNT dbg_rate_last_second;
#endif

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
  SUSCOUNT read_size;
  SUBOOL mutex_acquired = SU_FALSE;
  SUBOOL restart = SU_FALSE;
#ifdef SUSCAN_DEBUG_THROTTLE
  struct timespec sub;
#endif

  SU_TRYCATCH(pthread_mutex_lock(&source->det_mutex) != -1, goto done);
  mutex_acquired = SU_TRUE;

  /* With non-real time sources, use throttle to control CPU usage */
  if (source->config->source->real_time)
    read_size = analyzer->read_size;
  else
    read_size = suscan_throttle_get_portion(
        &source->throttle,
        analyzer->read_size);

  /* Ready to read */
  suscan_analyzer_read_start(analyzer);

#ifdef SUSCAN_DEBUG_THROTTLE
  if (!dbg_rate_set) {
    dbg_rate_set = SU_TRUE;
    dbg_rate_source_start = analyzer->read_start;
  }
#endif

  if ((got = su_block_port_read(
      &source->port,
      analyzer->read_buf,
      read_size)) > 0) {
    suscan_analyzer_process_start(analyzer);
#ifdef SUSCAN_DEBUG_THROTTLE
    dbg_rate_counter += got;
#endif

    if (!source->config->source->real_time)
      suscan_throttle_advance(&source->throttle, got);

    SU_TRYCATCH(
        su_channel_detector_feed_bulk(
            source->detector,
            analyzer->read_buf,
            got) == got,
        goto done);

    source->per_cnt_channels += got;
    source->per_cnt_psd += got;

    /* Check channel update */
    if (source->interval_channels > 0) {
      if (source->per_cnt_channels
          >= source->interval_channels * source->detector->params.samp_rate) {
        source->per_cnt_channels = 0;

        SU_TRYCATCH(
            suscan_analyzer_send_detector_channels(analyzer, source->detector),
            goto done);
      }
    }

    /* Check spectrum update */
    if (source->interval_psd > 0) {
      if (source->per_cnt_psd
          >= source->interval_psd * source->detector->params.samp_rate) {
        source->per_cnt_psd = 0;

        SU_TRYCATCH(
            suscan_analyzer_send_psd(analyzer, source->detector),
            goto done);
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

  /* Finish processing */
  suscan_analyzer_process_end(analyzer);

#ifdef SUSCAN_DEBUG_THROTTLE
    timespecsub(&analyzer->process_end, &dbg_rate_source_start, &sub);

    if (sub.tv_sec != dbg_rate_last_second) {
      dbg_rate_mean += ((SUFLOAT) dbg_rate_counter / (SUFLOAT) sub.tv_sec - dbg_rate_mean);
      SU_INFO("Current read rate: %lg\n", dbg_rate_mean);
      dbg_rate_last_second = sub.tv_sec;
    }
#endif

  restart = SU_TRUE;

done:
  if (mutex_acquired)
    (void) pthread_mutex_unlock(&source->det_mutex);

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
  su_channel_detector_t *new_detector = NULL;
  struct sigutils_channel_detector_params new_det_params;
  const struct suscan_analyzer_params *new_params;
  void *private = NULL;
  uint32_t type;
  SUBOOL mutex_acquired = SU_FALSE;
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
          SU_TRYCATCH(
              suscan_analyzer_parse_inspector_msg(analyzer, private),
              goto done);

          /*
           * We don't dispose this message: it has been processed
           * by the baud inspector API and forwarded to the output mq
           */
          private = NULL;

          break;

        /* Forward these messages to output */
        case SUSCAN_ANALYZER_MESSAGE_TYPE_EOS:
        case SUSCAN_ANALYZER_MESSAGE_TYPE_CHANNEL:
          SU_TRYCATCH(
              suscan_mq_write(analyzer->mq_out, type, private),
              goto done);

          /* Not belonging to us anymore */
          private = NULL;

          break;

        case SUSCAN_ANALYZER_MESSAGE_TYPE_PARAMS:
          /*
           * Parameter messages affect the source worker, that must get their
           * objects updated. In order to do that, we protect their access
           * through the source's params_mutex
           */

          SU_TRYCATCH(
              pthread_mutex_lock(&analyzer->source.det_mutex) != -1,
              goto done);
          mutex_acquired = SU_TRUE;

          /* vvvvvvvvvvvvvvv Source parameters update start vvvvvvvvvvvvv */
          new_params = (const struct suscan_analyzer_params *) private;

          /* Attempt to update detector parameters */
          new_det_params = analyzer->source.detector->params;
          suscan_analyzer_params_to_detector_params(
              &new_det_params,
              new_params);

          if (!su_channel_detector_set_params(
              analyzer->source.detector,
              &new_det_params)) {
            /* If not possibe, re-create detector object */
            SU_TRYCATCH(
                new_detector = su_channel_detector_new(&new_det_params),
                goto done);

            su_channel_detector_destroy(analyzer->source.detector);
            analyzer->source.detector = new_detector;
          }

          analyzer->source.per_cnt_channels  = 0;
          analyzer->source.per_cnt_psd       = 0;

          analyzer->source.interval_channels = new_params->channel_update_int;
          analyzer->source.interval_psd      = new_params->psd_update_int;
          /* ^^^^^^^^^^^^^ Source parameters update end ^^^^^^^^^^^^^^^^^  */

          SU_TRYCATCH(
              pthread_mutex_unlock(&analyzer->source.det_mutex) != -1,
              goto done);
          mutex_acquired = SU_FALSE;

          break;
      }

      if (private != NULL) {
        suscan_analyzer_dispose_message(type, private);
        private = NULL;
      }

      /* Next reads: until message queue is empty */
    } while (suscan_mq_poll(&analyzer->mq_in, &type, &private));
  }

done:
  if (mutex_acquired)
    (void) pthread_mutex_unlock(&analyzer->source.det_mutex);

  if (private != NULL)
    suscan_analyzer_dispose_message(type, private);

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

SUBOOL
suscan_analyzer_halt_worker(suscan_worker_t *worker)
{
  while (worker->state == SUSCAN_WORKER_STATE_RUNNING) {
    suscan_worker_req_halt(worker);

    while (!suscan_analyzer_consume_mq_until_halt(worker->mq_out))
      suscan_mq_wait(worker->mq_out);
  }

  return suscan_worker_destroy(worker);
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

  pthread_mutex_destroy(&source->det_mutex);
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

SUPRIVATE void
suscan_analyzer_params_to_detector_params(
    struct sigutils_channel_detector_params *params,
    const struct suscan_analyzer_params *analyzer_params)
{
  SUSCOUNT old_samp_rate = params->samp_rate;

  *params = analyzer_params->detector_params;

  params->mode = SU_CHANNEL_DETECTOR_MODE_DISCOVERY;
  params->samp_rate = old_samp_rate;


  /* Adjust parameters that depend on sample rate */
  su_channel_params_adjust(params);

#if 0
  /* Make alpha a little bigger, to provide a more dynamic spectrum */
  if (params->alpha <= .05)
    params->alpha *= 20;
#endif

  params->alpha = analyzer_params->detector_params.alpha;
}

SUPRIVATE SUBOOL
suscan_analyzer_source_init(
    const struct suscan_analyzer_params *analyzer_params,
    struct suscan_analyzer_source *source,
    struct suscan_source_config *config)
{
  struct sigutils_channel_detector_params params =
      analyzer_params->detector_params;
  SUBOOL ok = SU_FALSE;

  source->config = config;

  source->per_cnt_channels  = 0;
  source->per_cnt_psd       = 0;

  source->interval_channels = analyzer_params->channel_update_int;
  source->interval_psd      = analyzer_params->psd_update_int;

  (void) pthread_mutex_init(&source->det_mutex, NULL); /* Always succeeds */

  SU_TRYCATCH(source->block = (config->source->ctor)(config), goto done);

  /*
   * Analyze block properties, and initialize source and detector
   * parameters accordingly.
   */
  SU_TRYCATCH(
      suscan_analyzer_source_init_from_block_properties(source, &params),
      goto done);


  suscan_analyzer_params_to_detector_params(&params, analyzer_params);

  SU_TRYCATCH(
      source->detector = su_channel_detector_new(&params),
      goto done);

  SU_TRYCATCH(
      su_block_port_plug(&source->port, source->block, 0),
      goto done);

  if (config->source->real_time) {
    /*
     * With real time sources, we must use the master/slave flow controller.
     * This way, we ensure that at least the channel detector works
     * correctly.
     */
    SU_TRYCATCH(
        su_block_set_flow_controller(
            source->block,
            0,
            SU_FLOW_CONTROL_KIND_MASTER_SLAVE),
      goto done);

    /*
     * This is the master port. Other readers must wait for this reader
     * to complete before asking block for additional samples.
     */
    SU_TRYCATCH(
        su_block_set_master_port(source->block, 0, &source->port),
        goto done);
  } else {
    /*
     * If source is not realtime (e.g. iqfile or wavfile) we can afford
     * having a BARRIER flow controller here: samples will be available on
     * demand, and it's safe to pause until all workers are ready to consume
     * more samples
     */
    SU_TRYCATCH(
        su_block_set_flow_controller(
            source->block,
            0,
            SU_FLOW_CONTROL_KIND_BARRIER),
        goto done);

    /*
     * To avoid CPU hogging by unlimited input rate, we setup a throttle
     * object to deliver samples at a constat rate specified by the
     * sample rate;
     */
    suscan_throttle_init(&source->throttle, params.samp_rate);
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
    const struct suscan_analyzer_params *params,
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
  if (!suscan_analyzer_source_init(params, &analyzer->source, config)) {
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
