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

void
suscan_worker_status_msg_destroy(struct suscan_worker_status_msg *status)
{
  if (status->err_msg != NULL)
    free(status->err_msg);

  free(status);
}

struct suscan_worker_status_msg *
suscan_worker_status_msg_new(uint32_t code, const char *msg)
{
  char *msg_dup = NULL;
  struct suscan_worker_status_msg *new;

  if (msg != NULL)
    if ((msg_dup = strdup(msg)) == NULL)
      return NULL;

  if ((new = malloc(sizeof(struct suscan_worker_status_msg))) == NULL) {
    if (msg_dup != NULL)
      free(msg_dup);
    return NULL;
  }

  new->err_msg = msg_dup;
  new->code = code;

  return new;
}

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

  if ((err_msg = vstrbuild(err_msg_fmt, ap)) == NULL)
    goto done;

  if ((msg = suscan_worker_status_msg_new(code, err_msg)) == NULL)
    goto done;

  suscan_mq_write(worker->mq_out, type, msg);

  ok = SU_TRUE;

done:
  if (err_msg != NULL)
    free(err_msg);

  va_end(ap);

  return ok;
}

void
suscan_worker_dispose_message(uint32_t type, void *ptr)
{
  switch (type) {
    case SUSCAN_WORKER_MESSAGE_TYPE_SOURCE_INIT:
      suscan_worker_status_msg_destroy(ptr);
      break;
  }
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
  uint32_t result;

  for (;;) {
    suscan_mq_read(&worker->mq_in, &result);
    if (result == SUSCAN_WORKER_MESSAGE_TYPE_HALT) {
      suscan_worker_ack_halt(worker);
      break;
    }
  }
}

SUPRIVATE void *
suscan_worker_thread(void *data)
{
  suscan_worker_t *worker = (suscan_worker_t *) data;
  void *private;
  uint32_t type;
  su_block_t *src_block = NULL;

  if ((src_block = (worker->config->source->ctor)(worker->config)) == NULL) {
    suscan_worker_send_status(
        worker,
        SUSCAN_WORKER_MESSAGE_TYPE_SOURCE_INIT,
        -1,
        "Failed to initialize source type `%s'",
        worker->config->source->name);
    goto done;
  }

  for (;;) {
    private = suscan_mq_read(&worker->mq_in, &type);
    if (type == SUSCAN_WORKER_MESSAGE_TYPE_HALT) {
      suscan_worker_ack_halt(worker);
      goto halt;
    }
  }

done:
  if (src_block != NULL)
    su_block_destroy(src_block);

  suscan_wait_for_halt(worker);

halt:
  worker->running = SU_FALSE;

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
  uint32_t result;

  if (worker->running) {
    suscan_worker_req_halt(worker);
    (void) suscan_mq_read(worker->mq_out, &result);

    /* Couldn't stop thread, leave to avoid memory corruption */
    if (result != SUSCAN_WORKER_MESSAGE_TYPE_HALT)
      return;

    pthread_join(worker->thread, NULL);
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
