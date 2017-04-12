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
#include "xsig.h"
#include "analyzer.h"
#include "msg.h"

/************************* Channel analyzer API ******************************/
void
suscan_channel_analyzer_destroy(suscan_channel_analyzer_t *chanal)
{
  if (chanal->fac_baud_det != NULL)
    su_channel_detector_destroy(chanal->fac_baud_det);

  if (chanal->nln_baud_det != NULL)
    su_channel_detector_destroy(chanal->nln_baud_det);

  if (chanal->read_buf != NULL)
    free(chanal->read_buf);

  free(chanal);
}

suscan_channel_analyzer_t *
suscan_channel_analyzer_new(const struct sigutils_channel *channel)
{
  suscan_channel_analyzer_t *new;
  struct sigutils_channel_detector_params params =
      sigutils_channel_detector_params_INITIALIZER;

  if ((new = calloc(1, sizeof (suscan_channel_analyzer_t))) == NULL)
    goto fail;

  new->state = SUSCAN_ASYNC_STATE_CREATED;

  /* Common channel parameters */
  su_channel_params_adjust_to_channel(&params, channel);

  params.window_size = 4096;
  params.alpha = 1e-3;

  if (params.decimation < 2)
    params.decimation = 1;
  else
    params.decimation /= 2;

  /* Create generic autocorrelation-based detector */
  params.mode = SU_CHANNEL_DETECTOR_MODE_AUTOCORRELATION;
  if ((new->fac_baud_det = su_channel_detector_new(&params)) == NULL)
    goto fail;

  /* Create non-linear baud rate detector */
  params.mode = SU_CHANNEL_DETECTOR_MODE_NONLINEAR_DIFF;
  if ((new->nln_baud_det = su_channel_detector_new(&params)) == NULL)
    goto fail;

  /* Create read window */
  new->read_size = params.window_size;
  if ((new->read_buf = malloc(sizeof(SUCOMPLEX) * new->read_size)) == NULL)
    goto fail;

  return new;

fail:
  if (new != NULL)
    suscan_channel_analyzer_destroy(new);

  return NULL;
}
/******************* Channel analyzer worker callback ************************/
SUPRIVATE SUBOOL
suscan_channel_analyzer_wk_cb(
    struct suscan_mq *mq_out,
    void *wk_private,
    void *cb_private)
{
  suscan_analyzer_t *analyzer = (suscan_analyzer_t *) wk_private;
  suscan_channel_analyzer_t *chanal = (suscan_channel_analyzer_t *) cb_private;
  unsigned int i;
  SUSDIFF got;
  SUCOMPLEX sample;
  SUBOOL restart = SU_FALSE;

  got = su_block_port_read(&chanal->port, chanal->read_buf, chanal->read_size);

  if (got > 0) {
    /* Got samples, forward them to baud detectors */
    for (i = 0; i < got; ++i) {
      if (!su_channel_detector_feed(chanal->fac_baud_det, chanal->read_buf[i]))
        goto done;

      if (!su_channel_detector_feed(chanal->nln_baud_det, chanal->read_buf[i]))
        goto done;
    }
  } else {
    /* Failed to get samples, figure out why */
    switch (got) {
      case SU_BLOCK_PORT_READ_ERROR_PORT_DESYNC:
        /*
         * This is not necessarily an error: port desyncs happen
         * naturally if the system is running tight on resources. We
         * just silently resync the port
         */
        if (!su_block_port_resync(&chanal->port))
          goto done;
        break;

      case SU_BLOCK_PORT_READ_END_OF_STREAM:
        suscan_analyzer_send_status(
            analyzer,
            SUSCAN_ANALYZER_MESSAGE_TYPE_EOS,
            got,
            "End of stream reached");
        goto done;

      case SU_BLOCK_PORT_READ_ERROR_ACQUIRE:
        suscan_analyzer_send_status(
            analyzer,
            SUSCAN_ANALYZER_MESSAGE_TYPE_EOS,
            got,
            "Acquire failed (source I/O error)");
        goto done;

      default:
        suscan_analyzer_send_status(
            analyzer,
            SUSCAN_ANALYZER_MESSAGE_TYPE_EOS,
            got,
            "Unexpected read result %d", got);
    }
  }

  restart = SU_TRUE;

done:
  if (!restart)
    chanal->state = SUSCAN_ASYNC_STATE_HALTED;

  return restart;
}

/************************ Source worker callback *****************************/
SUPRIVATE SUBOOL
suscan_source_wk_cb(
    struct suscan_mq *mq_out,
    void *wk_private,
    void *cb_private)
{
  suscan_analyzer_t *analyzer = (suscan_analyzer_t *) wk_private;
  struct suscan_analyzer_source *source =
      (struct suscan_analyzer_source *) cb_private;
  int ret;
  SUCOMPLEX sample;
  SUBOOL restart = SU_FALSE;

  if ((ret = su_block_port_read(&source->port, &sample, 1)) == 1) {
    su_channel_detector_feed(source->detector, sample);
    if (source->samp_count++ >= .1 * source->detector->params.samp_rate) {
      source->samp_count = 0;

      if (!suscan_analyzer_send_detector_channels(analyzer, source->detector))
        goto done;
    }
  } else {
    switch (ret) {
      case SU_BLOCK_PORT_READ_END_OF_STREAM:
        suscan_analyzer_send_status(
            analyzer,
            SUSCAN_ANALYZER_MESSAGE_TYPE_EOS,
            ret,
            "End of stream reached");
        break;

      case SU_BLOCK_PORT_READ_ERROR_NOT_INITIALIZED:
        suscan_analyzer_send_status(
              analyzer,
              SUSCAN_ANALYZER_MESSAGE_TYPE_EOS,
              ret,
              "Port not initialized");
        break;

      case SU_BLOCK_PORT_READ_ERROR_ACQUIRE:
        suscan_analyzer_send_status(
              analyzer,
              SUSCAN_ANALYZER_MESSAGE_TYPE_EOS,
              ret,
              "Acquire failed (source I/O error)");
        break;

      case SU_BLOCK_PORT_READ_ERROR_PORT_DESYNC:
        suscan_analyzer_send_status(
              analyzer,
              SUSCAN_ANALYZER_MESSAGE_TYPE_EOS,
              ret,
              "Port desync");
        break;

      default:
        suscan_analyzer_send_status(
              analyzer,
              SUSCAN_ANALYZER_MESSAGE_TYPE_EOS,
              ret,
              "Unexpected read result %d", ret);
    }

    goto done;
  }

  restart = SU_TRUE;

done:
  return restart;
}


/************************* Main analyzer thread *******************************/
SUPRIVATE void
suscan_analyzer_req_halt(suscan_analyzer_t *analyzer)
{
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
suscan_analyzer_source_init(
    struct suscan_analyzer_source *source,
    struct suscan_source_config *config)
{
  SUBOOL ok;
  const uint64_t *samp_rate;
  const uint64_t *fc;

  struct sigutils_channel_detector_params params =
        sigutils_channel_detector_params_INITIALIZER;

  source->config = config;

  if ((source->block = (config->source->ctor)(config)) == NULL)
    goto done;

  /* Master/slave flow controller */
  if (!su_block_set_flow_controller(
      source->block,
      0,
      SU_FLOW_CONTROL_KIND_MASTER_SLAVE))
    goto done;

  /* Retrieve sample rate */
  if (strcmp(source->config->source->name, "wavfile") == 0
      || strcmp(source->config->source->name, "iqfile") == 0) {
    /* This source is special, and exposes an "instance" */
    if ((source->instance = su_block_get_property_ref(
        source->block,
        SU_PROPERTY_TYPE_OBJECT,
        "instance")) != NULL) {
      params.samp_rate = source->instance->samp_rate;
    } else {
      SU_ERROR("Failed to get sample rate");
      goto done;
    }
  } else {
    /* Other sources must populate samp_rate */
    if ((samp_rate = su_block_get_property_ref(
        source->block,
        SU_PROPERTY_TYPE_INTEGER,
        "samp_rate")) != NULL) {
      params.samp_rate = *samp_rate;
    } else {
      SU_ERROR("Failed to get sample rate");
      goto done;
    }
  }

  /* Retrieve center frequency, if any */
  if ((fc = su_block_get_property_ref(
      source->block,
      SU_PROPERTY_TYPE_INTEGER,
      "fc")) != NULL) {
    source->fc = *fc;
  }

  params.mode = SU_CHANNEL_DETECTOR_MODE_DISCOVERY;

  params.alpha = 1e-2;
  params.window_size = 4096;

  source->samp_count = 0;

  if ((source->detector = su_channel_detector_new(&params)) == NULL)
    goto done;

  if (!su_block_port_plug(&source->port, source->block, 0))
    goto done;

  /*
   * This is the master port. Other readers must wait for this reader
   * to complete before asking block for additional samples.
   */
  if (!su_block_set_master_port(source->block, 0, &source->port))
    goto done;

  ok = SU_TRUE;

done:
  return ok;
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

        case SUSCAN_ANALYZER_MESSAGE_TYPE_CHANNEL:
          /* Forward these messages to output */
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

  /* TODO: finalize all workers */

halt:
  pthread_exit(NULL);

  return NULL;
}

void *
suscan_analyzer_read(suscan_analyzer_t *analyzer, uint32_t *type)
{
  return suscan_mq_read(analyzer->mq_out, type);
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
  if (worker->state == SUSCAN_WORKER_STATE_RUNNING) {
    suscan_worker_req_halt(worker);

    while (!suscan_analyzer_consume_mq_until_halt(worker->mq_out))
      suscan_mq_wait(worker->mq_out);
  }

  return suscan_worker_destroy(worker);
}

SUPRIVATE unsigned int
suscan_get_min_consumer_workers(void)
{
  long count;

  if ((count = sysconf(_SC_NPROCESSORS_ONLN)) < 2)
    count = 2;

  return count - 1;
}

SUPRIVATE SUBOOL
suscan_analyzer_push_consumer(
    suscan_analyzer_t *analyzer,
    SUBOOL (*func) (
          struct suscan_mq *mq_out,
          void *wk_private,
          void *cb_private),
    void *private)
{
  if (!suscan_worker_push(
      analyzer->consumer_wk_list[analyzer->next_consumer],
      func,
      private))
    return SU_FALSE;

  /* Increment next consumer counter */
  analyzer->next_consumer =
      (analyzer->next_consumer + 1) % analyzer->consumer_wk_count;

  return SU_TRUE;
}

/********************** Suscan analyzer public API ***************************/
void
suscan_analyzer_destroy(suscan_analyzer_t *analyzer)
{
  uint32_t type;
  unsigned int i;

  void *private;

  if (analyzer->running) {
    suscan_analyzer_req_halt(analyzer);

    /*
     * TODO: this cannot wait forever. Add suscan_mq_read_with_timeout
     */
    while (!suscan_analyzer_consume_mq_until_halt(analyzer->mq_out))
      suscan_mq_wait(analyzer->mq_out);

    if (pthread_join(analyzer->thread, NULL) == -1) {
      SU_ERROR("Thread failed to join, memory leak ahead\n");
      return;
    }
  }

  if (analyzer->source_wk != NULL)
    if (!suscan_analyzer_halt_worker(analyzer->source_wk)) {
      SU_ERROR("Source worker destruction failed, memory leak ahead\n");
      return;
    }

  for (i = 0; i < analyzer->consumer_wk_count; ++i)
    if (analyzer->consumer_wk_list[i] != NULL)
      if (!suscan_analyzer_halt_worker(analyzer->consumer_wk_list[i])) {
        SU_ERROR("Consumer worker destruction failed, memory leak ahead\n");
        return;
      }

  if (suscan_analyzer_consume_mq_until_halt(&analyzer->mq_in)) {
    SU_ERROR("Unexpected HALT message! Leaking memory just in case...\n");
    return;
  }


  /* All workers destroyed, it's safe now to delete the worker list */
  if (analyzer->consumer_wk_list != NULL)
    free(analyzer->consumer_wk_list);

  /* Remove all channel analyzers */
  for (i = 0; i < analyzer->chan_analyzer_count; ++i)
    if (analyzer->chan_analyzer_list[i] != NULL)
      suscan_channel_analyzer_destroy(analyzer->chan_analyzer_list[i]);

  if (analyzer->chan_analyzer_list != NULL)
    free(analyzer->chan_analyzer_list);

  /* Delete source information */
  suscan_analyzer_source_finalize(&analyzer->source);

  suscan_mq_finalize(&analyzer->mq_in);

  free(analyzer);
}

SUBOOL
suscan_analyzer_dispose_channel_analyzer_handle(
    suscan_analyzer_t *analyzer,
    SUHANDLE handle)
{
  if (handle < 0 || handle >= analyzer->chan_analyzer_count)
    return SU_FALSE;

  if (analyzer->chan_analyzer_list[handle] == NULL)
    return SU_FALSE;

  analyzer->chan_analyzer_list[handle] = NULL;

  return SU_TRUE;
}

SUHANDLE
suscan_analyzer_register_channel_analyzer(
    suscan_analyzer_t *analyzer,
    suscan_channel_analyzer_t *chanal)
{
  SUHANDLE hnd;

  if (chanal->state != SUSCAN_ASYNC_STATE_CREATED)
    return SU_FALSE;

  /*
   * Plug to source. Since we are using master/slave flow control,
   * this will not block all consumers until actual read takes place
   * at worker context
   */
  if (!su_block_port_plug(&chanal->port, analyzer->source.block, 0))
    return -1;

  /* Plugged. Append handle to list */
  if ((hnd = PTR_LIST_APPEND_CHECK(analyzer->chan_analyzer, chanal)) == -1) {
    su_block_port_unplug(&chanal->port);
    return -1;
  }

  /* Mark it as running and push to worker */
  chanal->state = SUSCAN_ASYNC_STATE_RUNNING;

  if (!suscan_analyzer_push_consumer(
      analyzer,
      suscan_channel_analyzer_wk_cb,
      chanal)) {
    chanal->state = SUSCAN_ASYNC_STATE_CREATED;
    suscan_analyzer_dispose_channel_analyzer_handle(analyzer, hnd);
    su_block_port_unplug(&chanal->port);
    return -1;
  }

  return hnd;
}

suscan_analyzer_t *
suscan_analyzer_new(
    struct suscan_source_config *config,
    struct suscan_mq *mq)
{
  suscan_analyzer_t *analyzer = NULL;
  suscan_worker_t *worker;
  unsigned int worker_count;
  unsigned int i;

  if ((analyzer = calloc(1, sizeof (suscan_analyzer_t))) == NULL)
    goto fail;

  /* Create input message queue */
  if (!suscan_mq_init(&analyzer->mq_in))
    goto fail;

  /* Initialize source */
  if (!suscan_analyzer_source_init(&analyzer->source, config))
    goto fail;

  /* Create source worker */
  if ((analyzer->source_wk = suscan_worker_new(&analyzer->mq_in, analyzer))
      == NULL)
    goto fail;

  /* Create consumer workers */
  worker_count = suscan_get_min_consumer_workers();
  for (i = 0; i < worker_count; ++i) {
    if ((worker = suscan_worker_new(&analyzer->mq_in, analyzer)) == NULL)
      goto fail;

    if (PTR_LIST_APPEND_CHECK(analyzer->consumer_wk, worker) == -1) {
      suscan_worker_destroy(worker);
      goto fail;
    }
  }

  analyzer->mq_out = mq;

  if (pthread_create(
      &analyzer->thread,
      NULL,
      suscan_analyzer_thread,
      analyzer) == -1)
    goto fail;

  analyzer->running = SU_TRUE;

  return analyzer;

fail:
  if (analyzer != NULL)
    suscan_analyzer_destroy(analyzer);

  return NULL;
}
