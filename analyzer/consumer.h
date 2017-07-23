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

#ifndef _CONSUMER_H
#define _CONSUMER_H

#include <sigutils/sigutils.h>

#define SUSCAN_CONSUMER_IDLE_COUNTER 30

struct suscan_analyzer;

/* Per-worker object: used to centralize reads */
struct suscan_consumer {
  pthread_mutex_t lock; /* Must be recursive */
  suscan_worker_t *worker;
  struct suscan_analyzer *analyzer;
  su_block_port_t port; /* Slave reading port */

  SUCOMPLEX *buffer; /* TODO: make int const. Don't own this buffer */
  SUSCOUNT   buffer_size;
  SUSCOUNT   buffer_pos;

  unsigned int tasks;
  unsigned int idle_counter; /* Turns left on tasks == 0 before stop consuming */

  SUBOOL consuming; /* Whether we should be reading */
  SUBOOL failed;    /* Whether the consumer callback failed somehow */
};

typedef struct suscan_consumer suscan_consumer_t;

SUBOOL suscan_consumer_destroy(suscan_consumer_t *cons);

SUBOOL suscan_consumer_remove_task(suscan_consumer_t *consumer);

const SUCOMPLEX *suscan_consumer_get_buffer(const suscan_consumer_t *consumer);

SUSCOUNT suscan_consumer_get_buffer_size(const suscan_consumer_t *consumer);

SUSCOUNT suscan_consumer_get_buffer_pos(const suscan_consumer_t *consumer);

SUBOOL suscan_consumer_push_task(
    suscan_consumer_t *consumer,
    SUBOOL (*func) (
              struct suscan_mq *mq_out,
              void *wk_private,
              void *cb_private),
    void *private);

suscan_consumer_t *suscan_consumer_new(struct suscan_analyzer *analyzer);

#endif /* _CONSUMER_H */
