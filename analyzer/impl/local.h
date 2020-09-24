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

#ifndef _SUSCAN_ANALYZER_IMPL_LOCAL_H
#define _SUSCAN_ANALYZER_IMPL_LOCAL_H

#include <analyzer/analyzer.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define SULIMPL(analyzer) ((suscan_local_analyzer_t *) ((analyzer)->impl))

/*!
 * \brief Baseband filter description
 *
 * Structure holding a pointer to a function that would perform some kind
 * of baseband processing (i.e. before channelization).
 * \author Gonzalo José Carracedo Carballal
 */
struct suscan_analyzer_baseband_filter {
  suscan_analyzer_baseband_filter_func_t func;
  void *privdata;
};

struct suscan_inspector_overridable_request
{
  suscan_inspector_t *insp;

  SUBOOL  dead;
  SUBOOL  freq_request;
  SUFREQ  new_freq;
  SUBOOL  bandwidth_request;
  SUFLOAT new_bandwidth;

  struct suscan_inspector_overridable_request *next;
};

struct suscan_local_analyzer {
  suscan_analyzer_t *parent;
  struct suscan_mq mq_in;   /* Input queue */

  struct suscan_analyzer_source_info source_info;

  /* Source members */
  suscan_source_t *source;
  SUBOOL loop_init;
  pthread_mutex_t loop_mutex;
  suscan_throttle_t throttle; /* For non-realtime sources */
  SUBOOL throttle_mutex_init;
  pthread_mutex_t throttle_mutex;
  SUSCOUNT effective_samp_rate; /* Used for GUI */
  SUFLOAT  measured_samp_rate; /* Used for statistics */
  SUSCOUNT measured_samp_count;
  uint64_t last_measure;
  SUBOOL   iq_rev;

  /* Periodic updates */
  SUFLOAT  interval_channels;
  SUFLOAT  interval_psd;
  SUSCOUNT det_count;
  SUSCOUNT det_num_psd;

  /* This mutex shall protect hot-config requests */
  /* XXX: This is cumbersome. Create a hotconf object to handle these things */
  pthread_mutex_t hotconf_mutex;

  /* Frequency request */
  SUBOOL freq_req;
  SUFREQ freq_req_value;
  SUFREQ lnb_req_value;

  /* XXX: Define list for inspector frequency set */
  SUBOOL   inspector_freq_req;
  SUHANDLE inspector_freq_req_handle;
  SUFREQ   inspector_freq_req_value;

  SUBOOL   inspector_bw_req;
  SUHANDLE inspector_bw_req_handle;
  SUFLOAT  inspector_bw_req_value;

  /* Bandwidth request */
  SUBOOL  bw_req;
  SUFLOAT bw_req_value;

  /* Gain request */
  SUBOOL gain_req_mutex_init;
  PTR_LIST(struct suscan_analyzer_gain_info, gain_request);

  /* Atenna request */
  char *antenna_req;

  /* Usage statistics (CPU, etc) */
  SUFLOAT cpu_usage;
  uint64_t read_start;
  uint64_t process_start;
  uint64_t process_end;
  uint64_t last_psd;
  uint64_t last_channels;

  /* Source worker objects */
  su_channel_detector_t *detector; /* Channel detector */
  suscan_worker_t *source_wk; /* Used by one source only */
  suscan_worker_t *slow_wk; /* Worker for slow operations */
  SUCOMPLEX *read_buf;
  SUSCOUNT   read_size;
  PTR_LIST(struct suscan_analyzer_baseband_filter, bbfilt);

  /* Spectral tuner */
  su_specttuner_t    *stuner;

  /* Wide sweep parameters */
  SUBOOL sweep_params_requested;
  struct suscan_analyzer_sweep_params current_sweep_params;
  struct suscan_analyzer_sweep_params pending_sweep_params;
  SUFREQ   curr_freq;
  SUSCOUNT part_ndx;
  SUSCOUNT fft_samples; /* Number of FFT frames */

  /* Inspector objects */
  PTR_LIST(suscan_inspector_t, inspector); /* This list owns inspectors */
  pthread_mutex_t     inspector_list_mutex; /* Inspector list lock */
  SUBOOL                inspector_list_init;
  suscan_inspsched_t *sched; /* Inspector scheduler */
  pthread_mutex_t     sched_lock;
  pthread_barrier_t   barrier; /* Sched barrier */

  struct suscan_inspector_overridable_request *insp_overridable;

  /* Analyzer thread */
  pthread_t thread;
};

typedef struct suscan_local_analyzer suscan_local_analyzer_t;


/* Internal */
void suscan_local_analyzer_source_barrier(suscan_local_analyzer_t *analyzer);

/* Internal */
void suscan_local_analyzer_enter_sched(suscan_local_analyzer_t *analyzer);

/* Internal */
void suscan_local_analyzer_leave_sched(suscan_local_analyzer_t *analyzer);

/* Internal */
su_specttuner_channel_t *suscan_local_analyzer_open_channel_ex(
    suscan_local_analyzer_t *analyzer,
    const struct sigutils_channel *chan_info,
    SUBOOL precise,
    SUBOOL (*on_data) (
        const struct sigutils_specttuner_channel *channel,
        void *privdata,
        const SUCOMPLEX *data, /* This pointer remains valid until the next call to feed */
        SUSCOUNT size),
        void *privdata);

/* Internal */
su_specttuner_channel_t *suscan_local_analyzer_open_channel(
    suscan_local_analyzer_t *analyzer,
    const struct sigutils_channel *chan_info,
    SUBOOL (*on_data) (
        const struct sigutils_specttuner_channel *channel,
        void *privdata,
        const SUCOMPLEX *data, /* This pointer remains valid until the next call to feed */
        SUSCOUNT size),
        void *privdata);

/* Internal */
SUBOOL suscan_local_analyzer_close_channel(
    suscan_local_analyzer_t *analyzer,
    su_specttuner_channel_t *channel);

/* Internal */
SUBOOL suscan_local_analyzer_bind_inspector_to_channel(
    suscan_local_analyzer_t *analyzer,
    su_specttuner_channel_t *channel,
    suscan_inspector_t *insp);

/* Internal */
SUBOOL suscan_local_analyzer_lock_loop(suscan_local_analyzer_t *analyzer);

/* Internal */
void suscan_local_analyzer_unlock_loop(suscan_local_analyzer_t *analyzer);

/* Internal */
SUBOOL suscan_local_analyzer_lock_inspector_list(suscan_local_analyzer_t *analyzer);

/* Internal */
void suscan_local_analyzer_unlock_inspector_list(suscan_local_analyzer_t *analyzer);

/* Internal */
SUBOOL suscan_local_analyzer_parse_inspector_msg(
    suscan_local_analyzer_t *analyzer,
    struct suscan_analyzer_inspector_msg *msg);

/* Internal */
suscan_inspector_t *suscan_local_analyzer_get_inspector(
    const suscan_local_analyzer_t *analyzer,
    SUHANDLE handle);

/* Internal */
struct suscan_inspector_overridable_request *
suscan_local_analyzer_acquire_overridable(
    suscan_local_analyzer_t *self,
    SUHANDLE handle);

/* Internal */
SUBOOL suscan_local_analyzer_release_overridable(
    suscan_local_analyzer_t *self,
    struct suscan_inspector_overridable_request *rq);

/* Internal */
void suscan_local_analyzer_destroy_slow_worker_data(suscan_local_analyzer_t *);

/* Internal */
SUBOOL suscan_local_analyzer_set_inspector_bandwidth_overridable(
    suscan_local_analyzer_t *self,
    SUHANDLE handle,
    SUFLOAT bw);

/* Internal */
SUBOOL suscan_local_analyzer_set_inspector_freq_overridable(
    suscan_local_analyzer_t *analyzer,
    SUHANDLE handle,
    SUFREQ freq);

/* Internal */
SUBOOL suscan_local_analyzer_slow_set_freq(
    suscan_local_analyzer_t *self,
    SUFREQ freq,
    SUFREQ lnb);

/* Internal */
SUBOOL suscan_local_analyzer_slow_set_dc_remove(
    suscan_local_analyzer_t *analyzer,
    SUBOOL remove);

/* Internal */
SUBOOL suscan_local_analyzer_slow_set_agc(
    suscan_local_analyzer_t *analyzer,
    SUBOOL set);

/* Internal */
SUBOOL suscan_local_analyzer_slow_set_antenna(
    suscan_local_analyzer_t *analyzer,
    const char *name);

/* Internal */
SUBOOL suscan_local_analyzer_slow_set_bw(
    suscan_local_analyzer_t *analyzer,
    SUFLOAT bw);

/* Internal */
SUBOOL suscan_local_analyzer_slow_set_gain(
    suscan_local_analyzer_t *analyzer,
    const char *name,
    SUFLOAT value);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _SUSCAN_ANALYZER_IMPL_LOCAL */
