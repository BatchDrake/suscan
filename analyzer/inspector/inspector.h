/*

  Copyright (C) 2018 Gonzalo José Carracedo Carballal

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

#ifndef _INSPECTOR_H
#define _INSPECTOR_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <sigutils/sigutils.h>
#include "interface.h"

#define SUHANDLE int32_t

#define SUSCAN_ANALYZER_CPU_USAGE_UPDATE_ALPHA .025

#define SUSCAN_INSPECTOR_TUNER_BUF_SIZE    SU_BLOCK_STREAM_BUFFER_SIZE
#define SUSCAN_INSPECTOR_SAMPLER_BUF_SIZE  SU_BLOCK_STREAM_BUFFER_SIZE
#define SUSCAN_INSPECTOR_SPECTRUM_BUF_SIZE 8192

enum suscan_aync_state {
  SUSCAN_ASYNC_STATE_CREATED,
  SUSCAN_ASYNC_STATE_RUNNING,
  SUSCAN_ASYNC_STATE_HALTING,
  SUSCAN_ASYNC_STATE_HALTED
};

/* TODO: protect baudrate access with mutexes */
struct suscan_inspector {
  pthread_mutex_t mutex;
  uint32_t inspector_id;        /* Set by client */
  struct suscan_mq *mq_out;     /* Non-owned */
  enum suscan_aync_state state; /* Used to remove analyzer from queue */

  /* Specific inspector interface being used */
  const struct suscan_inspector_interface *iface;
  void *privdata;
  void *userdata;

  struct suscan_inspector_sampling_info samp_info; /* Sampling information */

  /* Spectrum and estimator state */
  SUFLOAT  interval_estimator;
  SUFLOAT  interval_spectrum;
  uint64_t last_estimator;
  uint64_t last_spectrum;

  uint32_t spectsrc_index;

  SUBOOL    params_requested;    /* New parameters requested */
  SUBOOL    bandwidth_notified;  /* New bandwidth set */
  SUFREQ    new_bandwidth;

  /* Sampler output */
  SUCOMPLEX sampler_buf[SUSCAN_INSPECTOR_SAMPLER_BUF_SIZE];
  SUSCOUNT  sampler_ptr;
  SUSCOUNT  sample_msg_watermark; /* Watermark. When reached, message is sent */

  PTR_LIST(suscan_estimator_t, estimator); /* Parameter estimators */
  PTR_LIST(suscan_spectsrc_t, spectsrc); /* Spectrum source */
};

typedef struct suscan_inspector suscan_inspector_t;

SUINLINE void
suscan_inspector_set_userdata(suscan_inspector_t *insp, void *userdata)
{
  insp->userdata = userdata;
}

SUINLINE void *
suscan_inspector_get_userdata(const suscan_inspector_t *insp)
{
  return insp->userdata;
}

SUINLINE SUBOOL
suscan_inspector_set_msg_watermark(suscan_inspector_t *insp, SUSCOUNT wm)
{
  if (wm > SUSCAN_INSPECTOR_SAMPLER_BUF_SIZE)
    return SU_FALSE;

  insp->sample_msg_watermark = wm;

  return SU_TRUE;
}

SUINLINE SUSCOUNT
suscan_inspector_sampler_buf_avail(const suscan_inspector_t *insp)
{
  return SUSCAN_INSPECTOR_SAMPLER_BUF_SIZE - insp->sampler_ptr;
}

SUINLINE SUBOOL
suscan_inspector_push_sample(suscan_inspector_t *insp, SUCOMPLEX samp)
{
  if (insp->sampler_ptr >= SUSCAN_INSPECTOR_SAMPLER_BUF_SIZE)
    return SU_FALSE;

  insp->sampler_buf[insp->sampler_ptr++] = samp;

  return SU_TRUE;
}

SUINLINE SUSCOUNT
suscan_inspector_get_output_length(const suscan_inspector_t *insp)
{
  return insp->sampler_ptr;
}

SUINLINE const SUCOMPLEX *
suscan_inspector_get_output_buffer(const suscan_inspector_t *insp)
{
  return insp->sampler_buf;
}

SUINLINE suscan_config_t *
suscan_inspector_create_config(const suscan_inspector_t *insp)
{
  return suscan_config_new(insp->iface->cfgdesc);
}

SUINLINE SUFLOAT
suscan_inspector_get_equiv_fs(const suscan_inspector_t *insp)
{
  return insp->samp_info.equiv_fs;
}

SUINLINE SUFLOAT
suscan_inspector_get_equiv_bw(const suscan_inspector_t *insp)
{
  return insp->samp_info.bw;
}

SUINLINE su_specttuner_channel_t *
suscan_inspector_get_channel(const suscan_inspector_t *insp)
{
  return insp->samp_info.schan;
}

/******************************* Public API **********************************/
void suscan_inspector_lock(suscan_inspector_t *insp);

void suscan_inspector_unlock(suscan_inspector_t *insp);

void suscan_inspector_reset_equalizer(suscan_inspector_t *insp);

void suscan_inspector_assert_params(suscan_inspector_t *insp);

void suscan_inspector_destroy(suscan_inspector_t *insp);

SUBOOL suscan_inspector_set_config(
    suscan_inspector_t *insp,
    const suscan_config_t *config);

SUBOOL suscan_inspector_notify_bandwidth(
    suscan_inspector_t *insp,
    SUFREQ new_bandwidth);

SUBOOL suscan_inspector_get_config(
    const suscan_inspector_t *insp,
    suscan_config_t *config);

suscan_inspector_t *suscan_inspector_new(
    const char *name,
    SUFLOAT fs,
    su_specttuner_channel_t *channel,
    struct suscan_mq *mq_out);

SUSDIFF suscan_inspector_feed_bulk(
    suscan_inspector_t *insp,
    const SUCOMPLEX *x,
    int count);

SUBOOL suscan_init_inspectors(void);

/* Builtin inspectors */
SUBOOL suscan_ask_inspector_register(void);
SUBOOL suscan_fsk_inspector_register(void);
SUBOOL suscan_psk_inspector_register(void);
SUBOOL suscan_audio_inspector_register(void);
SUBOOL suscan_raw_inspector_register(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _INSPECTOR_H */
