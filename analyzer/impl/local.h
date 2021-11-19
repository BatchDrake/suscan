/*

  Copyright (C) 2020 Gonzalo José Carracedo Carballal

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

#ifndef _SUSCAN_ANALYZER_IMPL_LOCAL_H
#define _SUSCAN_ANALYZER_IMPL_LOCAL_H

#include <analyzer/analyzer.h>
#include <sigutils/smoothpsd.h>
#include <analyzer/inspector/factory.h>
#include <analyzer/inspector/overridable.h>

#include <rbtree.h>

#define SUSCAN_LOCAL_ANALYZER_MIN_RADIO_FREQ -3e11
#define SUSCAN_LOCAL_ANALYZER_MAX_RADIO_FREQ +3e11

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define SULIMPL(analyzer) ((suscan_local_analyzer_t *) ((analyzer)->impl))
#define SUSCAN_LOCAL_ANALYZER_AS_ANALYZER(local) ((local)->parent)

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
  struct sigutils_smoothpsd_params sp_params;
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

  /* Seek request */
  SUBOOL   seek_req;
  SUSCOUNT seek_req_value; /* The seek request is a sample number */

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

  /* PPM request */
  SUBOOL  ppm_req;
  SUFLOAT ppm_req_value;

  /* Gain request */
  SUBOOL gain_req_mutex_init;
  PTR_LIST(struct suscan_analyzer_gain_info, gain_request);

  /* PSD request */
  SUBOOL   psd_params_req;

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
  su_smoothpsd_t  *smooth_psd;
  suscan_worker_t *source_wk; /* Used by one source only */
  suscan_worker_t *slow_wk; /* Worker for slow operations */
  SUCOMPLEX *read_buf;
  SUSCOUNT   read_size;
  PTR_LIST(struct suscan_analyzer_baseband_filter, bbfilt);

  /* Spectral tuner */
  su_specttuner_t    *stuner;
  pthread_mutex_t     stuner_mutex;
  SUBOOL              stuner_init;

  /* Wide sweep parameters */
  SUBOOL sweep_params_requested;
  struct suscan_analyzer_sweep_params current_sweep_params;
  struct suscan_analyzer_sweep_params pending_sweep_params;
  SUFREQ   curr_freq;
  SUSCOUNT part_ndx;
  SUSCOUNT fft_samples; /* Number of FFT frames */

  suscan_inspector_factory_t         *insp_factory;
  suscan_inspector_request_manager_t  insp_reqmgr;

  /* Global inspector table */
  rbtree_t                   *insp_hash;
  pthread_mutex_t             insp_mutex;
  SUBOOL                      insp_init;

  /* Analyzer thread */
  pthread_t thread;
  SUBOOL    thread_running;
};

typedef struct suscan_local_analyzer suscan_local_analyzer_t;

/* Internal */
SUBOOL suscan_local_analyzer_register_factory(void);

/* Internal */
SUBOOL suscan_local_analyzer_is_real_time_ex(const suscan_local_analyzer_t *self);

/* Internal */
SUBOOL suscan_local_analyzer_lock_loop(suscan_local_analyzer_t *analyzer);

/* Internal */
void suscan_local_analyzer_unlock_loop(suscan_local_analyzer_t *analyzer);

/* Internal */
SUBOOL suscan_local_analyzer_lock_inspector_list(suscan_local_analyzer_t *analyzer);

/* Internal */
void suscan_local_analyzer_unlock_inspector_list(suscan_local_analyzer_t *analyzer);

/* Internal */
SUBOOL suscan_local_analyzer_notify_params(suscan_local_analyzer_t *self);

/* Internal */
SUBOOL suscan_insp_server_init(void);

/* Internal */
SUBOOL suscan_local_analyzer_parse_inspector_msg(
    suscan_local_analyzer_t *analyzer,
    struct suscan_analyzer_inspector_msg *msg);

/* Internal */
SUHANDLE suscan_local_analyzer_register_inspector(
  suscan_local_analyzer_t *self,
  suscan_inspector_t *insp);

/* Internal */
SUBOOL suscan_local_analyzer_unregister_inspector(
  suscan_local_analyzer_t *self,
  SUHANDLE handle);

/* Internal */
suscan_inspector_t *suscan_local_analyzer_acquire_inspector(
  suscan_local_analyzer_t *self,
  SUHANDLE handle);

/* Internal */
void suscan_local_analyzer_return_inspector(
  suscan_local_analyzer_t *self,
  suscan_inspector_t *insp);

/* Internal */
void
suscan_local_analyzer_destroy_global_handles_unsafe(
  suscan_local_analyzer_t *self);

/* Internal */
void suscan_local_analyzer_destroy_slow_worker_data(suscan_local_analyzer_t *);

/* Internal */
SUBOOL suscan_local_analyzer_set_inspector_freq_slow(
    suscan_local_analyzer_t *self,
    SUHANDLE handle,
    SUFREQ freq);

/* Internal */
SUBOOL suscan_local_analyzer_set_inspector_bandwidth_slow(
    suscan_local_analyzer_t *self,
    SUHANDLE handle,
    SUFLOAT bw);

/* Internal */
SUBOOL suscan_local_analyzer_set_inspector_freq_overridable(
    suscan_local_analyzer_t *analyzer,
    SUHANDLE handle,
    SUFREQ freq);

/* Internal */
SUBOOL suscan_local_analyzer_set_inspector_bandwidth_overridable(
    suscan_local_analyzer_t *self,
    SUHANDLE handle,
    SUFLOAT bw);

/* Internal */
SUBOOL suscan_local_analyzer_set_analyzer_params_overridable(
    suscan_local_analyzer_t *self,
    const struct suscan_analyzer_params *params);

/* Internal */
SUBOOL suscan_local_analyzer_set_psd_samp_rate_overridable(
    suscan_local_analyzer_t *self,
    SUSCOUNT throttle);

/* Internal */
SUBOOL suscan_local_analyzer_slow_set_freq(
    suscan_local_analyzer_t *self,
    SUFREQ freq,
    SUFREQ lnb);

/* Internal */
SUBOOL suscan_local_analyzer_slow_seek(
    suscan_local_analyzer_t *self,
    const struct timeval *tv);

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
SUBOOL
suscan_local_analyzer_slow_set_ppm(
    suscan_local_analyzer_t *analyzer,
    SUFLOAT ppm);

/* Internal */
SUBOOL suscan_local_analyzer_slow_set_gain(
    suscan_local_analyzer_t *analyzer,
    const char *name,
    SUFLOAT value);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _SUSCAN_ANALYZER_IMPL_LOCAL */
