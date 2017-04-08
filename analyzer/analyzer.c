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

#include "mq.h"
#include "source.h"
#include "xsig.h"
#include "analyzer.h"
#include "msg.h"

/************************ Source worker callback *****************************/
SUPRIVATE SUBOOL
suscan_source_wk_cb(
    struct suscan_mq *mq_out,
    void *worker_private,
    void *callback_private)
{
  struct suscan_analyzer *analyzer =
      (struct suscan_analyzer *) worker_private;
  struct suscan_analyzer_source *source =
      (struct suscan_analyzer_source *) callback_private;
  int ret;
  SUCOMPLEX sample;
  SUBOOL restart = SU_FALSE;

  if ((ret = su_block_port_read(&source->port, &sample, 1)) == 1) {
    su_channel_detector_feed(source->detector, sample);
    if (++source->samp_count == source->detector->params.window_size) {
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

  if ((source->instance = su_block_get_property_ref(
      source->block,
      SU_PROPERTY_TYPE_OBJECT,
      "instance")) == NULL)
    goto done;

  params.mode = SU_CHANNEL_DETECTOR_MODE_DISCOVERY;
  params.samp_rate = source->instance->samp_rate;
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

  /* All workers destroyed, is safe to free memory now */
  if (analyzer->consumer_wk_list != NULL)
    free(analyzer->consumer_wk_list);

  suscan_analyzer_source_finalize(&analyzer->source);

  suscan_mq_finalize(&analyzer->mq_in);

  free(analyzer);
}

SUPRIVATE unsigned int
suscan_get_min_consumer_workers(void)
{
  long count;

  if ((count = sysconf(_SC_NPROCESSORS_ONLN)) < 2)
    count = 2;

  return count - 1;
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
