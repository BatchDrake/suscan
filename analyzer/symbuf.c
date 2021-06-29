/*

  Copyright (C) 2017 Gonzalo José Carracedo Carballal

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

#include <string.h>

#define SU_LOG_DOMAIN "symbuf"

#include "symbuf.h"

suscan_symbuf_listener_t *
suscan_symbuf_listener_new(
    SUSDIFF (*data_func) (void *priv, const SUBITS *new_data, SUSCOUNT size),
    void     (*eos_func) (void *priv, const SUBITS *new_data, SUSCOUNT size),
    void *private)
{
  suscan_symbuf_listener_t *new = NULL;

  SU_TRYCATCH(data_func != NULL, goto fail);

  SU_TRYCATCH(new = malloc(sizeof(suscan_symbuf_listener_t)), goto fail);

  new->data_func = data_func;
  new->eos_func  = eos_func;
  new->index     = -1;
  new->private   = private;
  new->ptr       = 0;
  new->source    = NULL;

  return new;

fail:
  if (new != NULL)
    suscan_symbuf_listener_destroy(new);

  return NULL;
}

void
suscan_symbuf_listener_seek(suscan_symbuf_listener_t *listener, SUSCOUNT ptr)
{
  listener->ptr = ptr;
}

void
suscan_symbuf_listener_destroy(suscan_symbuf_listener_t *listener)
{
  /* If plugged to a source, unplug first */
  if (listener->source != NULL)
    suscan_symbuf_unplug_listener(listener->source, listener);

  free(listener);
}

SUBOOL
suscan_symbuf_listener_is_plugged(const suscan_symbuf_listener_t *listener)
{
  return listener->index != -1 && listener->source != NULL;
}

SUBOOL
suscan_symbuf_plug_listener(
    suscan_symbuf_t *symbuf,
    suscan_symbuf_listener_t *listener)
{
  int index;

  SU_TRYCATCH(
      !suscan_symbuf_listener_is_plugged(listener),
      return SU_FALSE);

  SU_TRYCATCH(
      (index = PTR_LIST_APPEND_CHECK(symbuf->listener, listener)) != -1,
      return SU_FALSE);

  listener->source = symbuf;
  listener->index  = index;

  return SU_TRUE;
}

SUBOOL
suscan_symbuf_unplug_listener(
    suscan_symbuf_t *symbuf,
    suscan_symbuf_listener_t *listener)
{
  SUSCOUNT buffer_size;
  const SUBITS *buffer_data;

  /* Already unplugged? */
  SU_TRYCATCH(
      suscan_symbuf_listener_is_plugged(listener),
      return SU_FALSE);

  /* Belongs to this symbuf? */
  SU_TRYCATCH(listener->source == symbuf, return SU_FALSE);

  /* Index is valid? */
  SU_TRYCATCH(
      listener->index >= 0 && listener->index < symbuf->listener_count,
      return SU_FALSE);

  /* Refers to this listener? */
  SU_TRYCATCH(
      symbuf->listener_list[listener->index] == listener,
      return SU_FALSE);

  /* Run EOS callback if defined */
  if (listener->eos_func != NULL) {
    buffer_data = grow_buf_get_buffer(&symbuf->buffer);
    buffer_size = grow_buf_get_size(&symbuf->buffer) / sizeof(SUBITS);

    (listener->eos_func) (
        listener->private,
        buffer_data + listener->ptr,
        buffer_size - listener->ptr);
  }

  /* Proceed */
  symbuf->listener_list[listener->index] = NULL;

  listener->index = -1;
  listener->source = NULL;

  return SU_TRUE;
}

SUBOOL
suscan_symbuf_append(
    suscan_symbuf_t *symbuf,
    const SUBITS *data,
    SUSCOUNT size)
{
  SUSCOUNT buffer_size;
  const SUBITS *buffer_data;
  SUSDIFF got;
  unsigned int i;

  SU_TRYCATCH(
      grow_buf_append(&symbuf->buffer, data, size * sizeof(SUBITS)) != -1,
      return SU_FALSE);

  buffer_data = grow_buf_get_buffer(&symbuf->buffer);
  buffer_size = grow_buf_get_size(&symbuf->buffer) / sizeof(SUBITS);

  for (i = 0; i < symbuf->listener_count; ++i)
    if (symbuf->listener_list[i] != NULL
        && buffer_size > symbuf->listener_list[i]->ptr) {
      got = (symbuf->listener_list[i]->data_func) (
          symbuf->listener_list[i]->private,
          buffer_data + symbuf->listener_list[i]->ptr,
          buffer_size - symbuf->listener_list[i]->ptr);

      /* TODO: Makes sense to leave this unchecked? */
      symbuf->listener_list[i]->ptr += got;
    }

  return SU_TRUE;
}

const SUBITS *
suscan_symbuf_get_buffer(const suscan_symbuf_t *symbuf)
{
  return grow_buf_get_buffer(&symbuf->buffer);
}

SUSCOUNT
suscan_symbuf_get_size(const suscan_symbuf_t *symbuf)
{
  return grow_buf_get_size(&symbuf->buffer);
}

suscan_symbuf_t *
suscan_symbuf_new(void)
{
  suscan_symbuf_t *new;

  SU_TRYCATCH(new = calloc(1, sizeof (suscan_symbuf_t)), return NULL);

  return new;
}

void
suscan_symbuf_destroy(suscan_symbuf_t *symbuf)
{
  unsigned int i;

  /* This will send the EOS signal to all active listeners */
  for (i = 0; i < symbuf->listener_count; ++i)
    if (symbuf->listener_list[i] != NULL)
      (void) suscan_symbuf_unplug_listener(symbuf, symbuf->listener_list[i]);

  if (symbuf->listener_list != NULL)
    free(symbuf->listener_list);

  grow_buf_finalize(&symbuf->buffer);

  free(symbuf);
}
