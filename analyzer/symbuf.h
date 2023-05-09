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

#ifndef _SYMBUF_H
#define _SYMBUF_H

#include <sigutils/util/util.h>
#include <sigutils/sigutils.h>

/*
 * These structures ARE NOT THREAD SAFE. They are used just for chaining
 * symbol operations (e.g. codecs) all together
 */

struct suscan_symbuf;

struct suscan_symbuf_listener {
  struct suscan_symbuf *source;
  int index; /* w.r.t listener_list */
  SUSCOUNT ptr;
  SUSDIFF (*data_func) (void *priv, const SUBITS *new_data, SUSCOUNT size);
  void    (*eos_func) (void *priv, const SUBITS *new_data, SUSCOUNT size);
  void *private;
};

typedef struct suscan_symbuf_listener suscan_symbuf_listener_t;

suscan_symbuf_listener_t *suscan_symbuf_listener_new(
    SUSDIFF (*data_func) (void *priv, const SUBITS *new_data, SUSCOUNT size),
    void    (*eos_func) (void *priv, const SUBITS *new_data, SUSCOUNT size),
    void *private);

void suscan_symbuf_listener_seek(
    suscan_symbuf_listener_t *listener,
    SUSCOUNT ptr);

SUBOOL suscan_symbuf_listener_is_plugged(
    const suscan_symbuf_listener_t *listener);

void suscan_symbuf_listener_destroy(suscan_symbuf_listener_t *listener);

struct suscan_symbuf {
  grow_buf_t buffer;
  PTR_LIST(suscan_symbuf_listener_t, listener); /* Borrowed, with holes */
};

typedef struct suscan_symbuf suscan_symbuf_t;

SUBOOL suscan_symbuf_plug_listener(
    suscan_symbuf_t *symbuf,
    suscan_symbuf_listener_t *listener);

SUBOOL suscan_symbuf_unplug_listener(
    suscan_symbuf_t *symbuf,
    suscan_symbuf_listener_t *listener);

SUBOOL suscan_symbuf_append(
    suscan_symbuf_t *symbuf,
    const SUBITS *data,
    SUSCOUNT size);

const SUBITS *suscan_symbuf_get_buffer(const suscan_symbuf_t *symbuf);

SUSCOUNT suscan_symbuf_get_size(const suscan_symbuf_t *symbuf);

suscan_symbuf_t *suscan_symbuf_new(void);

void suscan_symbuf_destroy(suscan_symbuf_t *symbuf);

#endif /* _SYMBUF_H */
