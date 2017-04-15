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

#ifndef _ANALYZER_H
#define _ANALYZER_H

#include <sigutils/sigutils.h>
#include <sigutils/detect.h>

#include "worker.h"
#include "source.h"
#include "xsig.h"

#define SUHANDLE int32_t

enum suscan_aync_state {
  SUSCAN_ASYNC_STATE_CREATED,
  SUSCAN_ASYNC_STATE_RUNNING,
  SUSCAN_ASYNC_STATE_HALTING,
  SUSCAN_ASYNC_STATE_HALTED
};

/* TODO: protect baudrate access with mutexes */
struct suscan_channel_analyzer {
  struct sigutils_channel channel;
  su_block_port_t port;                /* Slave reading port */
  su_channel_detector_t *fac_baud_det; /* FAC baud detector */
  su_channel_detector_t *nln_baud_det; /* Non-linear baud detector */

  SUCOMPLEX *read_buf;
  SUSCOUNT   read_size;

  enum suscan_aync_state state;        /* Used to remove analyzer from queue */
};

typedef struct suscan_channel_analyzer suscan_channel_analyzer_t;

struct suscan_analyzer_source {
  struct suscan_source_config *config;
  su_block_t *block;
  su_block_port_t port; /* Master reading port */
  su_channel_detector_t *detector; /* Channel detector */
  struct xsig_source *instance;
  SUSCOUNT samp_count;
  uint64_t fc; /* Center frequency of source */
};

struct suscan_analyzer {
  struct suscan_mq mq_in;   /* To-thread messages */
  struct suscan_mq *mq_out; /* From-thread messages */
  SUBOOL running;
  SUBOOL eos;

  /* Usage statistics (CPU, etc) */
  SUFLOAT cpu_usage;

  /* Source worker objects */
  struct suscan_analyzer_source source;
  suscan_worker_t *source_wk; /* Used by one source only */
  SUCOMPLEX *read_buf;
  SUSCOUNT   read_size;

  /* Analyzer objects */
  PTR_LIST(suscan_channel_analyzer_t, chan_analyzer);

  /* Consumer workers (initially idle) */
  PTR_LIST(suscan_worker_t, consumer_wk);
  unsigned int next_consumer; /* Next consumer worker to use */

  /* Analyzer thread */
  pthread_t thread;
};

typedef struct suscan_analyzer suscan_analyzer_t;


/************************** Channel Analyzer API ******************************/
void suscan_channel_analyzer_destroy(suscan_channel_analyzer_t *chanal);
suscan_channel_analyzer_t *
suscan_channel_analyzer_new(const struct sigutils_channel *channel);

/****************************** Analyzer API **********************************/
void *suscan_analyzer_read(suscan_analyzer_t *analyzer, uint32_t *type);
void suscan_analyzer_dispose_message(uint32_t type, void *ptr);
void suscan_analyzer_destroy(suscan_analyzer_t *analyzer);
suscan_analyzer_t *suscan_analyzer_new(
    struct suscan_source_config *config,
    struct suscan_mq *mq);

SUHANDLE suscan_analyzer_register_channel_analyzer(
    suscan_analyzer_t *analyzer,
    suscan_channel_analyzer_t *chanal);
SUBOOL suscan_analyzer_dispose_channel_analyzer_handle(
    suscan_analyzer_t *analyzer,
    SUHANDLE handle);

#endif /* _ANALYZER_H */
