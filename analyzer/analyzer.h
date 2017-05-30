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
#include <sigutils/agc.h>
#include <sigutils/pll.h>
#include <sigutils/clock.h>
#include <sigutils/detect.h>

#include "worker.h"
#include "source.h"
#include "xsig.h"

#define SUHANDLE int32_t

#define SUSCAN_CONSUMER_IDLE_COUNTER 30

/*
 * Throttle reset threshold. If the number of available samples keeps growing,
 * it means that the reader is slower than the declared sample rate. In that
 * case, we just reset t0 and set sample_count to 0.
 */
#define SUSCAN_THROTTLE_RESET_THRESHOLD 1000000000ll
#define SUSCAN_THROTTLE_MAX_READ_UNIT_FRAC .25

struct suscan_throttle {
  SUSCOUNT samp_rate;
  SUSCOUNT samp_count;
  struct timespec t0;
};


typedef struct suscan_throttle suscan_throttle_t;

enum suscan_aync_state {
  SUSCAN_ASYNC_STATE_CREATED,
  SUSCAN_ASYNC_STATE_RUNNING,
  SUSCAN_ASYNC_STATE_HALTING,
  SUSCAN_ASYNC_STATE_HALTED
};

struct suscan_baud_det_result {
  SUFLOAT fac;
  SUFLOAT nln;
};

struct suscan_analyzer_source {
  struct suscan_source_config *config;
  su_block_t *block;
  su_block_port_t port; /* Master reading port */
  suscan_throttle_t throttle; /* Throttle object */
  su_channel_detector_t *detector; /* Channel detector */
  struct xsig_source *instance;

  SUFLOAT interval_channels;
  SUFLOAT interval_psd;

  SUSCOUNT per_cnt_channels;
  SUSCOUNT per_cnt_psd;
  uint64_t fc; /* Center frequency of source */
};

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

enum suscan_inspector_gain_control {
  SUSCAN_INSPECTOR_GAIN_CONTROL_MANUAL,
  SUSCAN_INSPECTOR_GAIN_CONTROL_AUTOMATIC
};

enum suscan_inspector_carrier_control {
  SUSCAN_INSPECTOR_CARRIER_CONTROL_MANUAL,
  SUSCAN_INSPECTOR_CARRIER_CONTROL_COSTAS_2,
  SUSCAN_INSPECTOR_CARRIER_CONTROL_COSTAS_4,
  SUSCAN_INSPECTOR_CARRIER_CONTROL_COSTAS_8,
};

enum suscan_inspector_matched_filter {
  SUSCAN_INSPECTOR_MATCHED_FILTER_BYPASS,
  SUSCAN_INSPECTOR_MATCHED_FILTER_MANUAL
};

enum suscan_inspector_baudrate_control {
  SUSCAN_INSPECTOR_BAUDRATE_CONTROL_MANUAL,
  SUSCAN_INSPECTOR_BAUDRATE_CONTROL_GARDNER
};

enum suscan_inspector_psd_source {
  SUSCAN_INSPECTOR_PSD_SOURCE_NONE,
  SUSCAN_INSPECTOR_PSD_SOURCE_FAC,
  SUSCAN_INSPECTOR_PSD_SOURCE_NLN
};

struct suscan_inspector_params {
  uint32_t inspector_id;

  /* Gain control parameters */
  enum suscan_inspector_gain_control gc_ctrl;
  SUFLOAT gc_gain;    /* Positive gain (linear) */

  /* Frequency control parameters */
  enum suscan_inspector_carrier_control fc_ctrl;
  SUFLOAT fc_off;     /* Offset frequency */
  SUFLOAT fc_phi;     /* Carrier phase */

  /* Matched filter parameters */
  enum suscan_inspector_matched_filter mf_conf;
  SUFLOAT mf_rolloff; /* Roll-off factor */

  /* Baudrate control parameters */
  enum suscan_inspector_baudrate_control br_ctrl;
  SUFLOAT br_alpha;   /* Baudrate control alpha (linear) */
  SUFLOAT br_beta;    /* Baudrate control beta (linear) */

  /* Spectrum source configuration */
  enum suscan_inspector_psd_source psd_source; /* Spectrum source */
  SUFLOAT sym_phase;  /* Symbol phase */
  SUFLOAT baud;       /* Baudrate */
};

/* TODO: protect baudrate access with mutexes */
struct suscan_inspector {
  struct sigutils_channel channel;
  SUFLOAT                 equiv_fs; /* Equivalent sample rate */
  su_channel_detector_t  *fac_baud_det; /* FAC baud detector */
  su_channel_detector_t  *nln_baud_det; /* Non-linear baud detector */
  su_agc_t                agc;      /* AGC, for sampler */
  su_costas_t             costas_2; /* 2nd order Costas loop */
  su_costas_t             costas_4; /* 4th order Costas loop */
  su_costas_t             costas_8; /* 8th order Costas loop */
  su_iir_filt_t           mf;       /* Matched filter (Root Raised Cosine) */
  su_clock_detector_t     cd;       /* Clock detector */
  su_ncqo_t               lo;       /* Oscillator for manual carrier offset */
  SUCOMPLEX               phase;    /* Local oscillator phase */

  /* Spectrum state */
  SUFLOAT                 interval_psd;
  SUSCOUNT                per_cnt_psd;

  /* Inspector parameters */
  pthread_mutex_t params_mutex;
  struct suscan_inspector_params params;
  struct suscan_inspector_params params_request;
  SUBOOL    params_requested;
  SUBOOL    sym_new_sample;     /* New sample flag */
  SUCOMPLEX sym_last_sample;    /* Last sample fed to inspector */
  SUCOMPLEX sym_sampler_output; /* Sampler output */
  SUFLOAT   sym_phase;          /* Current sampling phase, in samples */
  SUFLOAT   sym_period;         /* In samples */

  enum suscan_aync_state state; /* Used to remove analyzer from queue */
};

typedef struct suscan_inspector suscan_inspector_t;

struct suscan_analyzer {
  struct suscan_mq mq_in;   /* To-thread messages */
  struct suscan_mq *mq_out; /* From-thread messages */
  SUBOOL running;
  SUBOOL halt_requested;
  SUBOOL eos;

  /* Usage statistics (CPU, etc) */
  SUFLOAT cpu_usage;

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

/***************************** Throttle API **********************************/
void suscan_throttle_init(suscan_throttle_t *throttle, SUSCOUNT samp_rate);

SUSCOUNT suscan_throttle_get_portion(suscan_throttle_t *throttle, SUSCOUNT h);

void suscan_throttle_advance(suscan_throttle_t *throttle, SUSCOUNT got);

/***************************** Inspector API *********************************/
void suscan_inspector_destroy(suscan_inspector_t *chanal);

suscan_inspector_t *suscan_inspector_new(
    const suscan_analyzer_t *analyzer,
    const struct sigutils_channel *channel);


/* Baud inspector operations */
SUBOOL suscan_inspector_open_async(
    suscan_analyzer_t *analyzer,
    const struct sigutils_channel *channel,
    uint32_t req_id);

SUHANDLE suscan_inspector_open(
    suscan_analyzer_t *analyzer,
    const struct sigutils_channel *channel);

SUBOOL suscan_inspector_close_async(
    suscan_analyzer_t *analyzer,
    SUHANDLE handle,
    uint32_t req_id);

SUBOOL suscan_inspector_close(
    suscan_analyzer_t *analyzer,
    SUHANDLE handle);

SUBOOL suscan_inspector_get_info_async(
    suscan_analyzer_t *analyzer,
    SUHANDLE handle,
    uint32_t req_id);

SUBOOL suscan_inspector_get_info(
    suscan_analyzer_t *analyzer,
    SUHANDLE handle,
    struct suscan_baud_det_result *result);

SUBOOL suscan_inspector_set_params_async(
    suscan_analyzer_t *analyzer,
    SUHANDLE handle,
    const struct suscan_inspector_params *params,
    uint32_t req_id);

void suscan_inspector_params_initialize(
    struct suscan_inspector_params *params);

/****************************** Consumer API **********************************/
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

suscan_consumer_t *suscan_consumer_new(suscan_analyzer_t *analyzer);

/****************************** Analyzer API **********************************/
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
suscan_analyzer_t *suscan_analyzer_new(
    struct suscan_source_config *config,
    struct suscan_mq *mq);
SUBOOL suscan_analyzer_push_task(
    suscan_analyzer_t *analyzer,
    SUBOOL (*func) (
          struct suscan_mq *mq_out,
          void *wk_private,
          void *cb_private),
    void *private);


#endif /* _ANALYZER_H */
