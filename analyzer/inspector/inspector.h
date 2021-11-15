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
#include <analyzer/corrector.h>
#include <util/com.h>

#define SUHANDLE int32_t

#define SUSCAN_ANALYZER_CPU_USAGE_UPDATE_ALPHA .025

#define SUSCAN_INSPECTOR_TUNER_BUF_SIZE    SU_BLOCK_STREAM_BUFFER_SIZE
#define SUSCAN_INSPECTOR_SAMPLER_BUF_SIZE  SU_BLOCK_STREAM_BUFFER_SIZE
#define SUSCAN_INSPECTOR_SPECTRUM_BUF_SIZE 8192

struct suscan_inspector_factory;

enum suscan_aync_state {
  SUSCAN_ASYNC_STATE_CREATED,
  SUSCAN_ASYNC_STATE_RUNNING,
  SUSCAN_ASYNC_STATE_HALTING,
  SUSCAN_ASYNC_STATE_HALTED
};

/* TODO: protect baudrate access with mutexes */
struct suscan_inspector {
  SUSCAN_REFCOUNT;              /* Reference counter */

  struct suscan_inspector_factory *factory; /* Inspector factory */
  void *factory_userdata;                   /* Per-inspector factory data */
  pthread_mutex_t                  mutex;
  SUBOOL                           mutex_init;

  SUHANDLE handle;              /* Owner handle */
  uint32_t inspector_id;        /* Set by client */
  struct suscan_mq *mq_out;     /* Non-owned */
  enum suscan_aync_state state; /* Used to remove analyzer from queue */

  /* Specific inspector interface being used */
  const struct suscan_inspector_interface *iface;
  void *privdata;
  void *userdata;

  struct suscan_inspector_sampling_info samp_info; /* Sampling information */
  
  /* Frequency corrector (in channel data callback) */
  pthread_mutex_t               corrector_mutex;
  SUBOOL                        corrector_init;
  suscan_frequency_corrector_t *corrector;
  
  /* Spectrum and estimator state */
  SUFLOAT  interval_estimator;
  SUFLOAT  interval_spectrum;
  SUFLOAT  interval_orbit_report;

  uint64_t last_estimator;
  uint64_t last_spectrum;
  uint64_t last_orbit_report;

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

SUINLINE struct suscan_inspector_factory *
suscan_inspector_get_factory(const suscan_inspector_t *self)
{
  return self->factory;
}

SUINLINE void
suscan_inspector_set_userdata(suscan_inspector_t *self, void *userdata)
{
  self->userdata = userdata;
}

SUINLINE void *
suscan_inspector_get_userdata(const suscan_inspector_t *self)
{
  return self->userdata;
}

SUINLINE void
suscan_inspector_get_sampling_info(
  const suscan_inspector_t *self,
  struct suscan_inspector_sampling_info *samp_info)
{
  *samp_info = self->samp_info;
}

SUINLINE SUBOOL
suscan_inspector_set_msg_watermark(suscan_inspector_t *self, SUSCOUNT wm)
{
  if (wm > SUSCAN_INSPECTOR_SAMPLER_BUF_SIZE)
    return SU_FALSE;

  self->sample_msg_watermark = wm;

  return SU_TRUE;
}

SUINLINE SUSCOUNT
suscan_inspector_sampler_buf_avail(const suscan_inspector_t *self)
{
  return SUSCAN_INSPECTOR_SAMPLER_BUF_SIZE - self->sampler_ptr;
}

SUINLINE SUBOOL
suscan_inspector_push_sample(suscan_inspector_t *self, SUCOMPLEX samp)
{
  if (self->sampler_ptr >= SUSCAN_INSPECTOR_SAMPLER_BUF_SIZE)
    return SU_FALSE;

  self->sampler_buf[self->sampler_ptr++] = samp;

  return SU_TRUE;
}

SUINLINE SUSCOUNT
suscan_inspector_get_output_length(const suscan_inspector_t *self)
{
  return self->sampler_ptr;
}

SUINLINE const SUCOMPLEX *
suscan_inspector_get_output_buffer(const suscan_inspector_t *self)
{
  return self->sampler_buf;
}

SUINLINE suscan_config_t *
suscan_inspector_create_config(const suscan_inspector_t *self)
{
  return suscan_config_new(self->iface->cfgdesc);
}

SUINLINE SUFLOAT
suscan_inspector_get_equiv_fs(const suscan_inspector_t *self)
{
  return self->samp_info.equiv_fs;
}

SUINLINE SUFLOAT
suscan_inspector_get_equiv_bw(const suscan_inspector_t *self)
{
  return self->samp_info.bw;
}

/******************************* Public API **********************************/
void suscan_inspector_lock(suscan_inspector_t *insp);

void suscan_inspector_unlock(suscan_inspector_t *insp);

void suscan_inspector_reset_equalizer(suscan_inspector_t *insp);

SUBOOL suscan_inspector_set_corrector(
  suscan_inspector_t *self, 
  suscan_frequency_corrector_t *corrector);

SUBOOL suscan_inspector_disable_corrector(suscan_inspector_t *self);

SUBOOL suscan_inspector_get_correction(
  suscan_inspector_t *self, 
  const struct timeval *tv,
  SUFREQ abs_freq,
  SUFLOAT *freq);

SUBOOL suscan_inspector_deliver_report(
  suscan_inspector_t *self,
  const struct timeval *tv,
  SUFREQ abs_freq);

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
    struct suscan_inspector_factory *owner,
    const char *name,
    const struct suscan_inspector_sampling_info *samp_info,
    struct suscan_mq *mq_out,
    void *userdata);

SUBOOL suscan_inspector_sampler_loop(
    suscan_inspector_t *insp,
    const SUCOMPLEX *samp_buf,
    SUSCOUNT samp_count,
    struct suscan_mq *mq_out);

SUBOOL suscan_inspector_spectrum_loop(
    suscan_inspector_t *insp,
    const SUCOMPLEX *samp_buf,
    SUSCOUNT samp_count,
    struct suscan_mq *mq_out);

SUBOOL suscan_inspector_estimator_loop(
    suscan_inspector_t *insp,
    const SUCOMPLEX *samp_buf,
    SUSCOUNT samp_count,
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
