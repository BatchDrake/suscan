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
#include <pthread.h>

#include "worker.h"
#include "source.h"
#include "throttle.h"
#include "inspector.h"
#include "consumer.h"

struct suscan_analyzer_params {
  struct sigutils_channel_detector_params detector_params;
  SUFLOAT  channel_update_int;
  SUFLOAT  psd_update_int;
};

#define suscan_analyzer_params_INITIALIZER {                                \
  sigutils_channel_detector_params_INITIALIZER, /* detector_params */       \
  .1,                                           /* channel_update_int */    \
  .04                                           /* psd_update_int */        \
}

struct suscan_analyzer_source {
  struct suscan_source_config *config;
  su_block_t *block;
  su_block_port_t port; /* Master reading port */
  suscan_throttle_t throttle; /* Throttle object */
  struct xsig_source *instance;

  pthread_mutex_t det_mutex;
  su_channel_detector_t *detector; /* Channel detector */
  SUFLOAT interval_channels;
  SUFLOAT interval_psd;

  SUSCOUNT per_cnt_channels;
  SUSCOUNT per_cnt_psd;
  uint64_t fc; /* Center frequency of source */
};

struct suscan_analyzer;

struct suscan_analyzer {
  struct suscan_mq mq_in;   /* To-thread messages */
  struct suscan_mq *mq_out; /* From-thread messages */
  SUBOOL running;
  SUBOOL halt_requested;
  SUBOOL eos;

  /* Usage statistics (CPU, etc) */
  SUFLOAT cpu_usage;
  struct timespec read_start;
  struct timespec process_start;
  struct timespec process_end;

  /* Source worker objects */
  struct suscan_analyzer_source source;
  suscan_worker_t *source_wk; /* Used by one source only */
  SUCOMPLEX *read_buf;
  SUSCOUNT   read_size;

  /* Inspector objects */
  PTR_LIST(suscan_inspector_t, inspector);

  /* Consumer workers (initially idle) */
  PTR_LIST(suscan_consumer_t, consumer);

  unsigned int next_consumer; /* Next consumer worker to use */

  /* Analyzer thread */
  pthread_t thread;
};

typedef struct suscan_analyzer suscan_analyzer_t;

void *suscan_analyzer_read(suscan_analyzer_t *analyzer, uint32_t *type);
struct suscan_analyzer_inspector_msg *suscan_analyzer_read_inspector_msg(
    suscan_analyzer_t *analyzer);
SUBOOL suscan_analyzer_write(
    suscan_analyzer_t *analyzer,
    uint32_t type,
    void *priv);
void suscan_analyzer_consume_mq(struct suscan_mq *mq);
void suscan_analyzer_dispose_message(uint32_t type, void *ptr);
void suscan_analyzer_destroy(suscan_analyzer_t *analyzer);
void suscan_analyzer_req_halt(suscan_analyzer_t *analyzer);
SUBOOL suscan_analyzer_halt_worker(suscan_worker_t *worker);
suscan_analyzer_t *suscan_analyzer_new(
    const struct suscan_analyzer_params *params,
    struct suscan_source_config *config,
    struct suscan_mq *mq);
SUBOOL suscan_analyzer_push_task(
    suscan_analyzer_t *analyzer,
    SUBOOL (*func) (
          struct suscan_mq *mq_out,
          void *wk_private,
          void *cb_private),
    void *private);


SUBOOL suscan_analyzer_set_params_async(
    suscan_analyzer_t *analyzer,
    const struct suscan_analyzer_params *params,
    uint32_t req_id);

SUBOOL suscan_analyzer_open_async(
    suscan_analyzer_t *analyzer,
    const struct sigutils_channel *channel,
    uint32_t req_id);

SUHANDLE suscan_analyzer_open(
    suscan_analyzer_t *analyzer,
    const struct sigutils_channel *channel);

SUBOOL suscan_analyzer_close_async(
    suscan_analyzer_t *analyzer,
    SUHANDLE handle,
    uint32_t req_id);

SUBOOL suscan_analyzer_close(
    suscan_analyzer_t *analyzer,
    SUHANDLE handle);

SUBOOL
suscan_analyzer_set_inspector_id_async(
    suscan_analyzer_t *analyzer,
    SUHANDLE handle,
    uint32_t inspector_id,
    uint32_t req_id);

SUBOOL suscan_analyzer_set_inspector_config_async(
    suscan_analyzer_t *analyzer,
    SUHANDLE handle,
    const suscan_config_t *config,
    uint32_t req_id);

SUBOOL suscan_analyzer_inspector_estimator_cmd_async(
    suscan_analyzer_t *analyzer,
    SUHANDLE handle,
    uint32_t estimator_id,
    SUBOOL enabled,
    uint32_t req_id);

SUBOOL suscan_analyzer_inspector_set_spectrum_async(
    suscan_analyzer_t *analyzer,
    SUHANDLE handle,
    uint32_t spectsrc_id,
    uint32_t req_id);

SUBOOL suscan_analyzer_reset_equalizer_async(
    suscan_analyzer_t *analyzer,
    SUHANDLE handle,
    uint32_t req_id);

#endif /* _ANALYZER_H */
