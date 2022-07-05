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

#ifndef _WORKER_H
#define _WORKER_H

#include <pthread.h>
#include <sigutils/sigutils.h>

#include "mq.h"

#define SUSCAN_WORKER_MSG_TYPE_CALLBACK 0
#define SUSCAN_WORKER_MSG_TYPE_HALT     0xffffffff

enum suscan_worker_state {
  SUSCAN_WORKER_STATE_CREATED,
  SUSCAN_WORKER_STATE_RUNNING,
  SUSCAN_WORKER_STATE_HALTED
};

struct suscan_worker {
  struct suscan_mq mq_in; /* Receive callbacks from here */
  struct suscan_mq *mq_out; /* Send callbacks to here */
  void *privdata; /* Worker private data */
  SUBOOL halt_req;
  enum suscan_worker_state state;
  pthread_t thread;
};

typedef struct suscan_worker suscan_worker_t;

struct suscan_worker_callback {
  SUBOOL (*func) (
      struct suscan_mq *mq_out,
      void *wk_private,
      void *cb_private);
  void *privdata;
};

/******************************* Worker API ***********************************/
SUBOOL suscan_worker_push(
    suscan_worker_t *worker,
    SUBOOL (*func) (
        struct suscan_mq *mq_out,
        void *wk_private,
        void *cb_private),
    void *privdata);
void suscan_worker_req_halt(suscan_worker_t *worker);
SUBOOL suscan_worker_destroy(suscan_worker_t *worker);
SUBOOL suscan_worker_halt(suscan_worker_t *worker);

suscan_worker_t *suscan_worker_new_ex(
  const char *name,
  struct suscan_mq *mq_out,
  void *privdata);

suscan_worker_t *suscan_worker_new(
    struct suscan_mq *mq_out,
    void *privdata);


#endif /* _WORKER_H */
