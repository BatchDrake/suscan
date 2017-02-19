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

#include <pthread.h>
#include "suscan.h"


SUBOOL
suscan_worker_send_status(
    suscan_worker_t *worker,
    uint32_t type,
    int code,
    const char *err_msg_fmt, ...)
{
  struct suscan_worker_status_msg *msg;
  va_list ap;
  char *err_msg = NULL;
  SUBOOL ok = SU_FALSE;

  va_start(ap, err_msg_fmt);

  if (err_msg_fmt != NULL)
    if ((err_msg = vstrbuild(err_msg_fmt, ap)) == NULL)
      goto done;

  if ((msg = suscan_worker_status_msg_new(code, err_msg)) == NULL)
    goto done;

  if (!suscan_mq_write(worker->mq_out, type, msg)) {
    suscan_worker_dispose_message(type, msg);
    goto done;
  }

  ok = SU_TRUE;

done:
  if (err_msg != NULL)
    free(err_msg);

  va_end(ap);

  return ok;
}

SUPRIVATE void
suscan_worker_req_halt(suscan_worker_t *worker)
{
  suscan_mq_write_urgent(
      &worker->mq_in,
      SUSCAN_WORKER_MESSAGE_TYPE_HALT,
      NULL);
}

SUPRIVATE void
suscan_worker_ack_halt(suscan_worker_t *worker)
{
  suscan_mq_write_urgent(
      worker->mq_out,
      SUSCAN_WORKER_MESSAGE_TYPE_HALT,
      NULL);
}

SUPRIVATE void
suscan_wait_for_halt(suscan_worker_t *worker)
{
  uint32_t type;
  void *private;

  for (;;) {
    private = suscan_mq_read(&worker->mq_in, &type);
    if (type == SUSCAN_WORKER_MESSAGE_TYPE_HALT) {
      suscan_worker_ack_halt(worker);
      break;
    }

    suscan_worker_dispose_message(type, private);
  }
}

SUPRIVATE void *
suscan_worker_thread(void *data)
{
  suscan_worker_t *worker = (suscan_worker_t *) data;
  su_block_port_t port = su_block_port_INITIALIZER;
  su_channel_detector_t *detector = NULL;
  struct sigutils_channel_detector_params params =
      sigutils_channel_detector_params_INITIALIZER;
  struct suscan_worker_channel_msg *channel_msg = NULL;
  struct sigutils_channel **ch_list;
  struct xsig_source *instance;
  unsigned int ch_count;

  void *private;
  uint32_t type;
  su_block_t *src_block = NULL;
  unsigned int count = 0;
  int ret;
  SUBOOL halt_acked = SU_FALSE;
  SUCOMPLEX sample;

  if ((src_block = (worker->config->source->ctor)(worker->config)) == NULL) {
    suscan_worker_send_status(
        worker,
        SUSCAN_WORKER_MESSAGE_TYPE_SOURCE_INIT,
        SUSCAN_WORKER_INIT_FAILURE,
        "Failed to initialize source type `%s'",
        worker->config->source->name);
    goto done;
  }

  if ((instance = su_block_get_property_ref(
        src_block,
        SU_PROPERTY_TYPE_OBJECT,
        "instance")) == NULL) {
    suscan_worker_send_status(
        worker,
        SUSCAN_WORKER_MESSAGE_TYPE_SOURCE_INIT,
        SUSCAN_WORKER_INIT_FAILURE,
        "Failed to get instance data of source `%s'",
        worker->config->source->name);
    goto done;
  }

  params.samp_rate = instance->samp_rate;
  params.alpha = 1e-2;

  if ((detector = su_channel_detector_new(&params)) == NULL) {
    suscan_worker_send_status(
        worker,
        SUSCAN_WORKER_MESSAGE_TYPE_SOURCE_INIT,
        SUSCAN_WORKER_INIT_FAILURE,
        "Failed to initialize channel detector");
    goto done;
  }

  if (!su_block_port_plug(&port, src_block, 0)) {
    suscan_worker_send_status(
        worker,
        SUSCAN_WORKER_MESSAGE_TYPE_SOURCE_INIT,
        SUSCAN_WORKER_INIT_FAILURE,
        "Failed to plug source port");
    goto done;
  }

  /* Signal initialization success */
  suscan_worker_send_status(
      worker,
      SUSCAN_WORKER_MESSAGE_TYPE_SOURCE_INIT,
      SUSCAN_WORKER_INIT_SUCCESS,
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
        su_channel_detector_get_channel_list(detector, &ch_list, &ch_count);

        if ((channel_msg =
              suscan_worker_channel_msg_new(worker, ch_list, ch_count))
            == NULL) {
          suscan_worker_send_status(
              worker,
              SUSCAN_WORKER_MESSAGE_TYPE_INTERNAL,
              ret,
              "Cannot create message: %s",
              strerror(errno));
          goto done;
        }

        if (!suscan_mq_write(
            worker->mq_out,
            SUSCAN_WORKER_MESSAGE_TYPE_CHANNEL,
            channel_msg)) {
          suscan_worker_dispose_message(
              SUSCAN_WORKER_MESSAGE_TYPE_CHANNEL,
              channel_msg);
          suscan_worker_send_status(
              worker,
              SUSCAN_WORKER_MESSAGE_TYPE_INTERNAL,
              ret,
              "Cannot write message: %s",
              strerror(errno));
          goto done;
        }
      }
    } else {
      switch (ret) {
        case SU_BLOCK_PORT_READ_END_OF_STREAM:
          suscan_worker_send_status(
              worker,
              SUSCAN_WORKER_MESSAGE_TYPE_EOS,
              ret,
              "End of stream reached");
          break;

        case SU_BLOCK_PORT_READ_ERROR_NOT_INITIALIZED:
          suscan_worker_send_status(
                worker,
                SUSCAN_WORKER_MESSAGE_TYPE_EOS,
                ret,
                "Port not initialized");
          break;

        case SU_BLOCK_PORT_READ_ERROR_ACQUIRE:
          suscan_worker_send_status(
                worker,
                SUSCAN_WORKER_MESSAGE_TYPE_EOS,
                ret,
                "Acquire failed (source I/O error)");
          break;

        case SU_BLOCK_PORT_READ_ERROR_PORT_DESYNC:
          suscan_worker_send_status(
                worker,
                SUSCAN_WORKER_MESSAGE_TYPE_EOS,
                ret,
                "Port desync");
          break;

        default:
          suscan_worker_send_status(
                worker,
                SUSCAN_WORKER_MESSAGE_TYPE_EOS,
                ret,
                "Unexpected read result %d", ret);
      }

      goto done;
    }

    /* Pop all messages from queue before reading from the source */
    while (suscan_mq_poll(&worker->mq_in, &type, &private)) {
      if (type == SUSCAN_WORKER_MESSAGE_TYPE_HALT) {
        suscan_worker_ack_halt(worker);
        halt_acked = SU_TRUE;
        goto done;
      }

      suscan_worker_dispose_message(type, private);
    }
  }

done:
  if (detector != NULL)
    su_channel_detector_destroy(detector);

  if (src_block != NULL)
    su_block_destroy(src_block);

  if (!halt_acked)
    suscan_wait_for_halt(worker);

halt:
  pthread_exit(NULL);

  return NULL;
}

void *
suscan_worker_read(suscan_worker_t *worker, uint32_t *type)
{
  return suscan_mq_read(worker->mq_out, type);
}

void
suscan_worker_destroy(suscan_worker_t *worker)
{
  uint32_t type;
  void *private;

  if (worker->running) {
    suscan_worker_req_halt(worker);

    /*
     * TODO: this cannot wait forever. Add suscan_mq_read_with_timeout
     */
    do {
      private = suscan_mq_read(worker->mq_out, &type);
      suscan_worker_dispose_message(type, private);
    } while (type != SUSCAN_WORKER_MESSAGE_TYPE_HALT);

    if (pthread_join(worker->thread, NULL) == -1) {
      SU_ERROR("Thread failed to join, memory leak ahead\n");
      return;
    }
  }

  if (worker->config != NULL)
    suscan_source_config_destroy(worker->config);

  suscan_mq_finalize(&worker->mq_in);

  free(worker);
}

suscan_worker_t *
suscan_worker_new(
    struct suscan_source_config *config,
    struct suscan_mq *mq)
{
  suscan_worker_t *worker = NULL;

  if ((worker = calloc(1, sizeof (suscan_worker_t))) == NULL)
    goto fail;

  if (!suscan_mq_init(&worker->mq_in))
    goto fail;

  worker->mq_out = mq;
  worker->config = config;

  if (pthread_create(
      &worker->thread,
      NULL,
      suscan_worker_thread,
      worker) == -1)
    goto fail;

  worker->running = SU_TRUE;

  return worker;

fail:
  if (worker != NULL)
    suscan_worker_destroy(worker);

  return NULL;
}
