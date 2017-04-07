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
#include <stdarg.h>
#include <ctype.h>
#include <libgen.h>
#include <pthread.h>
#include <stdint.h>

#include "mq.h"
#include "source.h"
#include "xsig.h"
#include "analyzer.h"
#include "msg.h"

SUPRIVATE void
suscan_analyzer_req_halt(suscan_analyzer_t *analyzer)
{
  suscan_mq_write_urgent(
      &analyzer->mq_in,
      SUSCAN_WORKER_MESSAGE_TYPE_HALT,
      NULL);
}

SUPRIVATE void
suscan_analyzer_ack_halt(suscan_analyzer_t *analyzer)
{
  suscan_mq_write_urgent(
      analyzer->mq_out,
      SUSCAN_WORKER_MESSAGE_TYPE_HALT,
      NULL);
}

SUPRIVATE void
suscan_wait_for_halt(suscan_analyzer_t *analyzer)
{
  uint32_t type;
  void *private;

  for (;;) {
    private = suscan_mq_read(&analyzer->mq_in, &type);
    if (type == SUSCAN_WORKER_MESSAGE_TYPE_HALT) {
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
  su_block_port_t port = su_block_port_INITIALIZER;
  su_channel_detector_t *detector = NULL;
  struct sigutils_channel_detector_params params =
      sigutils_channel_detector_params_INITIALIZER;
  struct xsig_source *instance;

  void *private;
  uint32_t type;
  su_block_t *src_block = NULL;
  unsigned int count = 0;
  int ret;
  SUBOOL halt_acked = SU_FALSE;
  SUCOMPLEX sample;

  if ((src_block = (analyzer->config->source->ctor)(analyzer->config)) == NULL) {
    suscan_analyzer_send_status(
        analyzer,
        SUSCAN_WORKER_MESSAGE_TYPE_SOURCE_INIT,
        SUSCAN_ANALYZER_INIT_FAILURE,
        "Failed to initialize source type `%s'",
        analyzer->config->source->name);
    goto done;
  }

  if ((instance = su_block_get_property_ref(
        src_block,
        SU_PROPERTY_TYPE_OBJECT,
        "instance")) == NULL) {
    suscan_analyzer_send_status(
        analyzer,
        SUSCAN_WORKER_MESSAGE_TYPE_SOURCE_INIT,
        SUSCAN_ANALYZER_INIT_FAILURE,
        "Failed to get instance data of source `%s'",
        analyzer->config->source->name);
    goto done;
  }

  params.samp_rate = instance->samp_rate;
  params.alpha = 1e-2;
  params.window_size = 4096;

  if ((detector = su_channel_detector_new(&params)) == NULL) {
    suscan_analyzer_send_status(
        analyzer,
        SUSCAN_WORKER_MESSAGE_TYPE_SOURCE_INIT,
        SUSCAN_ANALYZER_INIT_FAILURE,
        "Failed to initialize channel detector");
    goto done;
  }

  if (!su_block_port_plug(&port, src_block, 0)) {
    suscan_analyzer_send_status(
        analyzer,
        SUSCAN_WORKER_MESSAGE_TYPE_SOURCE_INIT,
        SUSCAN_ANALYZER_INIT_FAILURE,
        "Failed to plug source port");
    goto done;
  }

  /* Signal initialization success */
  suscan_analyzer_send_status(
      analyzer,
      SUSCAN_WORKER_MESSAGE_TYPE_SOURCE_INIT,
      SUSCAN_ANALYZER_INIT_SUCCESS,
      NULL);

  for (;;) {
    /*
     * TODO: add a condition variable to wait for events, both from the source
     * and the input queue.
     */
    if ((ret = su_block_port_read(&port, &sample, 1)) == 1) {
      su_channel_detector_feed(detector, sample);
      if (++count == params.window_size) {
        count = 0;

        if (!suscan_analyzer_send_detector_channels(analyzer, detector))
          goto done;
      }
    } else {
      switch (ret) {
        case SU_BLOCK_PORT_READ_END_OF_STREAM:
          suscan_analyzer_send_status(
              analyzer,
              SUSCAN_WORKER_MESSAGE_TYPE_EOS,
              ret,
              "End of stream reached");
          break;

        case SU_BLOCK_PORT_READ_ERROR_NOT_INITIALIZED:
          suscan_analyzer_send_status(
                analyzer,
                SUSCAN_WORKER_MESSAGE_TYPE_EOS,
                ret,
                "Port not initialized");
          break;

        case SU_BLOCK_PORT_READ_ERROR_ACQUIRE:
          suscan_analyzer_send_status(
                analyzer,
                SUSCAN_WORKER_MESSAGE_TYPE_EOS,
                ret,
                "Acquire failed (source I/O error)");
          break;

        case SU_BLOCK_PORT_READ_ERROR_PORT_DESYNC:
          suscan_analyzer_send_status(
                analyzer,
                SUSCAN_WORKER_MESSAGE_TYPE_EOS,
                ret,
                "Port desync");
          break;

        default:
          suscan_analyzer_send_status(
                analyzer,
                SUSCAN_WORKER_MESSAGE_TYPE_EOS,
                ret,
                "Unexpected read result %d", ret);
      }

      goto done;
    }

    /* Pop all messages from queue before reading from the source */
    while (suscan_mq_poll(&analyzer->mq_in, &type, &private)) {
      switch (type) {
        case SUSCAN_WORKER_MESSAGE_TYPE_HALT:
          suscan_analyzer_ack_halt(analyzer);
          halt_acked = SU_TRUE;
          /* Nothing to dispose, safe to break the loop */
          goto done;

          /* TODO: parse analyzer commands here */
      }

      suscan_analyzer_dispose_message(type, private);
    }
  }

done:
  if (detector != NULL)
    su_channel_detector_destroy(detector);

  if (src_block != NULL)
    su_block_destroy(src_block);

  if (!halt_acked)
    suscan_wait_for_halt(analyzer);

halt:
  pthread_exit(NULL);

  return NULL;
}

void *
suscan_analyzer_read(suscan_analyzer_t *analyzer, uint32_t *type)
{
  return suscan_mq_read(analyzer->mq_out, type);
}

void
suscan_analyzer_destroy(suscan_analyzer_t *analyzer)
{
  uint32_t type;
  void *private;

  if (analyzer->running) {
    suscan_analyzer_req_halt(analyzer);

    /*
     * TODO: this cannot wait forever. Add suscan_mq_read_with_timeout
     */
    do {
      private = suscan_mq_read(analyzer->mq_out, &type);
      suscan_analyzer_dispose_message(type, private);
    } while (type != SUSCAN_WORKER_MESSAGE_TYPE_HALT);

    if (pthread_join(analyzer->thread, NULL) == -1) {
      SU_ERROR("Thread failed to join, memory leak ahead\n");
      return;
    }
  }

  if (analyzer->config != NULL)
    suscan_source_config_destroy(analyzer->config);

  suscan_mq_finalize(&analyzer->mq_in);

  free(analyzer);
}

suscan_analyzer_t *
suscan_analyzer_new(
    struct suscan_source_config *config,
    struct suscan_mq *mq)
{
  suscan_analyzer_t *analyzer = NULL;

  if ((analyzer = calloc(1, sizeof (suscan_analyzer_t))) == NULL)
    goto fail;

  if (!suscan_mq_init(&analyzer->mq_in))
    goto fail;

  analyzer->mq_out = mq;
  analyzer->config = config;

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
