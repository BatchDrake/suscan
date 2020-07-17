/*

  Copyright (C) 2020 Gonzalo José Carracedo Carballal

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

#define SU_LOG_DOMAIN "cli-datasaver"

#include <sigutils/log.h>
#include "datasaver.h"

SUPRIVATE SUBOOL
suscli_datasaver_writer_cb(
    struct suscan_mq *mq_out,
    void *worker_private,
    void *callback_private)
{
  suscli_datasaver_t *self = (suscli_datasaver_t *) worker_private;
  SUBOOL restart = SU_FALSE;
  SUSDIFF avail;

  if (self->failed)
    goto fail;

  /* Determine wether there are pending samples */
  SU_TRYCATCH(pthread_mutex_lock(&self->mutex) == 0, goto fail);
  avail = self->block_ptr - self->block_consumed;
  SU_TRYCATCH(pthread_mutex_unlock(&self->mutex) == 0, goto fail);

  if (avail > 0) {
    if (!(self->params.write)(
        self->state,
        self->block_buffer + self->block_consumed,
        avail)) {
      suscan_worker_req_halt(self->worker);
      self->failed = SU_TRUE;
      goto fail;
    }
  }

  /* Check whether we should restart writer */
  SU_TRYCATCH(pthread_mutex_lock(&self->mutex) != -1, goto fail);
  self->block_consumed += avail;
  avail = self->block_ptr - self->block_consumed;
  if (avail > 0)
    restart = SU_TRUE;
  SU_TRYCATCH(pthread_mutex_unlock(&self->mutex) != -1, goto fail);

fail:
   return restart;
}

suscli_datasaver_t *
suscli_datasaver_new(const struct suscli_datasaver_params *params)
{
  suscli_datasaver_t *new = NULL;

  SU_TRYCATCH(new = calloc(1, sizeof(suscli_datasaver_t)), goto fail);
  new->params = *params;

  SU_TRYCATCH(
      new->state = (new->params.open)(new->params.userdata),
      goto fail);

  new->block_size = SUSCLI_DATASAVER_BLOCK_SIZE;
  SU_TRYCATCH(
      new->block_buffer = malloc(
          new->block_size * sizeof(struct suscli_sample)),
      goto fail);

  SU_TRYCATCH(pthread_mutex_init(&new->mutex, NULL) == 0, goto fail);
  new->have_mutex = SU_TRUE;

  SU_TRYCATCH(suscan_mq_init(&new->mq), goto fail);
  new->have_mq = SU_TRUE;

  SU_TRYCATCH(new->worker = suscan_worker_new(&new->mq, new), goto fail);

  return new;

fail:
  if (new != NULL)
    suscli_datasaver_destroy(new);

  return NULL;
}

SUBOOL
suscli_datasaver_write(
    suscli_datasaver_t *self,
    SUFLOAT data)
{
  struct suscli_sample *samp;
  struct suscli_sample *tmp;
  struct timeval tv;
  SUSDIFF avail;
  SUBOOL ok = SU_FALSE;

  gettimeofday(&tv, NULL);

  SU_TRYCATCH(!self->failed, goto fail);

  /* Before saving: check whether there's space left for sample */
  SU_TRYCATCH(pthread_mutex_lock(&self->mutex) == 0, goto fail);
  if (self->block_ptr == self->block_consumed)
    self->block_ptr = self->block_consumed = 0;

  avail = self->block_size - self->block_ptr;

  if (avail == 0) {
    tmp = realloc(
        self->block_buffer,
        2 * self->block_size * sizeof (struct suscli_sample));
    if (tmp == NULL) {
      suscan_worker_req_halt(self->worker);
      self->failed = SU_TRUE;
      SU_TRYCATCH(pthread_mutex_unlock(&self->mutex) == 0, goto fail);
      goto fail;
    }

    self->block_size  *= 2;
    self->block_buffer = tmp;
    avail = self->block_size - self->block_ptr;
  }

  samp = self->block_buffer + self->block_ptr;
  SU_TRYCATCH(pthread_mutex_unlock(&self->mutex) == 0, goto fail);

  /* Populate sample */
  samp->timestamp = tv;
  samp->value     = data;

  /*
   * Increment block pointer and check whether the writer worker
   * may not resume writing.
   */
  SU_TRYCATCH(pthread_mutex_lock(&self->mutex) == 0, goto fail);
  avail = self->block_ptr - self->block_consumed;
  ++self->block_ptr;
  SU_TRYCATCH(pthread_mutex_unlock(&self->mutex) == 0, goto fail);

  /*
   * This happens when the worker is not running. Send a
   * tentative callback.
   */
  if (avail == 0)
    SU_TRYCATCH(
        suscan_worker_push(
            self->worker, suscli_datasaver_writer_cb,
            NULL),
        goto fail);

  ok = SU_TRUE;

fail:
  return ok;
}

void
suscli_datasaver_destroy(suscli_datasaver_t *self)
{
  if (self->worker != NULL)
    suscan_worker_halt(self->worker);

  if (self->block_buffer != NULL)
    free(self->block_buffer);

  if (self->have_mq)
    suscan_mq_finalize(&self->mq);

  if (self->have_mutex)
    pthread_mutex_destroy(&self->mutex);

  if (self->state != NULL)
    (self->params.close) (self->state);

  free(self);
}
