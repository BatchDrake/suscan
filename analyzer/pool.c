/*
  Copyright (C) 2023 Gonzalo José Carracedo Carballal

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

#define SU_LOG_DOMAIN "pool"

#include <sigutils/log.h>
#include <sys/mman.h>

#include "pool.h"

/****************** Construct the suscan sample buffer ************************/
SU_INSTANCER(suscan_sample_buffer, suscan_sample_buffer_pool_t *parent)
{
  suscan_sample_buffer_t *self = NULL;

  SU_ALLOCATE_FAIL(self, suscan_sample_buffer_t);

  self->parent   = parent;
  self->rindex   = -1;
  self->circular = parent->params.vm_circularity;
  self->acquired = SU_FALSE;
  self->size     = parent->params.alloc_size;

  SU_ALLOCATE_MANY_FAIL(self->data, self->size, SUCOMPLEX);

  return self;

fail:
  if (self != NULL)
    suscan_sample_buffer_destroy(self);
  
  return NULL;
}

SU_COLLECTOR(suscan_sample_buffer)
{
  /* TODO: If this is mmaped, free using munmap */
  if (self->data != NULL)
    free(self->data);

  free(self);
}

/***************** Construct the suscan sample buffer pool ********************/
SU_CONSTRUCTOR(suscan_sample_buffer_pool,
  const struct suscan_sample_buffer_pool_params *params)
{
  SUBOOL ok = SU_FALSE;

  memset(self, 0, sizeof (suscan_sample_buffer_pool_t));

  SU_CONSTRUCT(suscan_mq, &self->free_mq);
  self->free_mq_init = SU_TRUE;

  SU_TRYZ(pthread_mutex_init(&self->mutex, NULL));
  self->mutex_init = SU_TRUE;

  ok = SU_TRUE;

done:
  if (!ok)
    SU_DESTRUCT(suscan_sample_buffer_pool, self);
  
  return ok;
}

SU_DESTRUCTOR(suscan_sample_buffer_pool)
{
  unsigned int i;

  if (self->free_mq_init) {
    suscan_mq_write_urgent(&self->free_mq, SUSCAN_POOL_MQ_TYPE_HALT, NULL);
    SU_DESTRUCT(suscan_mq, &self->free_mq);
  }

  if (self->mutex_init)
    pthread_mutex_destroy(&self->mutex);

  for (i = 0; i < self->buffer_count; ++i)
    if (self->buffer_list[i] != NULL)
      suscan_sample_buffer_destroy(self->buffer_list[i]);

  if (self->buffer_list != NULL)
    free(self->buffer_list);
}

SU_INSTANCER(
  suscan_sample_buffer_pool,
  const struct suscan_sample_buffer_pool_params *params)
{
  suscan_sample_buffer_pool_t *new = NULL;

  SU_ALLOCATE_FAIL(new, suscan_sample_buffer_t);

  SU_CONSTRUCT_FAIL(suscan_sample_buffer_pool, new, params);

  return new;

fail:
  if (new != NULL)
    suscan_sample_buffer_pool_destroy(new);

  return new;
}

SU_COLLECTOR(suscan_sample_buffer_pool)
{
  SU_DESTRUCT(suscan_sample_buffer_pool, self);
  free(self);
}

SU_METHOD(suscan_sample_buffer_pool, suscan_sample_buffer_t *, acquire)
{
  suscan_sample_buffer_t *ret = NULL;

  if (self->buffer_count == self->params.max_buffers) {
    /* Cannot allocate new buffers, perform blocking read on freemq */
    uint32_t type;

    ret = suscan_mq_read(&self->free_mq, &type);

    if (type != SUSCAN_POOL_MQ_TYPE_BUFFER) {
      SU_WARNING("acquire() aborted due to non-buffer entry\n");
      return NULL;
    }

    ret->acquired = SU_TRUE;

  } else {
    /* Room for new buffers. Perform a try_acquire. */
    ret = suscan_sample_buffer_pool_try_acquire(self);
  }

  return ret;
}

SU_METHOD(suscan_sample_buffer_pool, suscan_sample_buffer_t *, try_acquire)
{
  suscan_sample_buffer_t *tmp = NULL;
  suscan_sample_buffer_t *ret = NULL;
  int rindex;

  uint32_t type;

  if (suscan_mq_poll(&self->free_mq, &type, (void **) &ret)) {
    /* We have free elements here! */
    if (type != SUSCAN_POOL_MQ_TYPE_BUFFER) {
      SU_WARNING("acquire() aborted due to non-buffer entry\n");
      goto fail;
    }
  } else {
    /* No free elements, allocate and return */
    SU_MAKE_FAIL(tmp, suscan_sample_buffer, self);
    SU_TRYC_FAIL(rindex = PTR_LIST_APPEND_CHECK(self->buffer, tmp));
    tmp->rindex = rindex;
    ret = tmp;
  }

  ret->acquired = SU_TRUE;

  return ret;

fail:
  if (tmp != NULL)
    suscan_sample_buffer_destroy(tmp);

  return NULL;
}

SU_METHOD(suscan_sample_buffer_pool, SUBOOL, give, suscan_sample_buffer_t *buf)
{
  SUBOOL ok = SU_FALSE;

  if (!buf->acquired) {
    SU_ERROR("BUG: Sample buffer is not acquired\n");
    goto done;
  }

  if (buf->parent != self) {
    SU_ERROR("BUG: Attempting to return a sample buffer to the wrong pool!\n");
    goto done;
  }

  if (buf->rindex < 0 || buf->rindex >= self->buffer_count) {
    SU_ERROR("BUG: Buffer rindex out of bounds\n");
    goto done;
  }

  if (self->buffer_list[buf->rindex] != buf) {
    SU_ERROR("BUG: Buffer rindex does not match buffer pool list\n");
    goto done;
  }

  buf->acquired = SU_FALSE;

  SU_TRY(suscan_mq_write(&self->free_mq, SUSCAN_POOL_MQ_TYPE_BUFFER, buf));

  ok = SU_TRUE;

done:
  return ok;
}

