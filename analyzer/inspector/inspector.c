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

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <stdint.h>

#define SU_LOG_DOMAIN "suscan-inspector"

#include <sigutils/sigutils.h>
#include <sigutils/sampling.h>
#include <sigutils/util/compat-time.h>

#include "factory.h"
#include "correctors/tle.h"

#include "realtime.h"
#include "msg.h"

void
suscan_inspector_lock(suscan_inspector_t *insp)
{
  (void) pthread_mutex_lock(&insp->mutex);
}

void
suscan_inspector_unlock(suscan_inspector_t *insp)
{
  (void) pthread_mutex_unlock(&insp->mutex);
}

void
suscan_inspector_reset_equalizer(suscan_inspector_t *insp)
{
  suscan_inspector_lock(insp);

  SU_WARNING("Reset equalizer not implemented yet!\n");

  suscan_inspector_unlock(insp);
}

/*********************** Channel opening and closing *************************/
SUPRIVATE su_specttuner_channel_t *
suscan_inspector_open_sc_channel_ex(
    suscan_inspector_t *self,
    const struct sigutils_channel *chan_info,
    SUBOOL precise,
    su_specttuner_channel_data_func_t on_data,
    su_specttuner_channel_new_freq_func_t on_new_freq,
    void *privdata)
{
  SUBOOL mutex_acquired = SU_FALSE;
  su_specttuner_channel_t *channel = NULL;
  struct sigutils_specttuner_channel_params params =
      sigutils_specttuner_channel_params_INITIALIZER;

  params.f0 =
      SU_NORM2ANG_FREQ(
          SU_ABS2NORM_FREQ(
              self->samp_info.equiv_fs,
              chan_info->fc - chan_info->ft));

  if (params.f0 < 0)
    params.f0 += 2 * PI;

  params.bw =
      SU_NORM2ANG_FREQ(
          SU_ABS2NORM_FREQ(
              self->samp_info.equiv_fs,
              chan_info->f_hi - chan_info->f_lo));
  
  params.guard    = SUSCAN_ANALYZER_GUARD_BAND_PROPORTION;
  params.privdata = privdata;
  params.precise  = precise;
  
  params.on_data  = on_data;
  params.on_freq_changed = on_new_freq;

  SU_TRYCATCH(pthread_mutex_lock(&self->sc_stuner_mutex) == 0, goto done);
  mutex_acquired = SU_TRUE;

  SU_TRYCATCH(
      channel = su_specttuner_open_channel(self->sc_stuner, &params),
      goto done);

  
done:
  if (mutex_acquired)
    (void) pthread_mutex_unlock(&self->sc_stuner_mutex);

  return channel;
}

/* TODO: Move this logic to factory impl */
SUPRIVATE SUBOOL
suscan_inspector_open_sc_close_channel(
    suscan_inspector_t *self,
    su_specttuner_channel_t *channel)
{
  SUBOOL mutex_acquired = SU_FALSE;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(pthread_mutex_lock(&self->sc_stuner_mutex) == 0, goto done);
  mutex_acquired = SU_TRUE;

  ok = su_specttuner_close_channel(self->sc_stuner, channel);

done:
  if (mutex_acquired)
    (void) pthread_mutex_unlock(&self->sc_stuner_mutex);

  return ok;
}

/**************** Implementation of the local inspector factory **************/
SUPRIVATE void *
suscan_sc_inspector_factory_ctor(suscan_inspector_factory_t *parent, va_list ap)
{
  suscan_inspector_t *self;

  self = va_arg(ap, suscan_inspector_t *);

  suscan_inspector_factory_set_mq_out(parent, self->mq_out);
  suscan_inspector_factory_set_mq_ctl(parent, self->mq_ctl);

  return self;
}

SUPRIVATE void
suscan_sc_inspector_factory_get_time(void *userdata, struct timeval *tv)
{
  suscan_inspector_t *self = (suscan_inspector_t *) userdata;

  suscan_inspector_factory_get_time(self->factory, tv);
}

SUPRIVATE SUBOOL
suscan_sc_inspector_on_channel_data(
    const struct sigutils_specttuner_channel *channel,
    void *userdata,
    const SUCOMPLEX *data,
    SUSCOUNT size)
{
  suscan_inspector_t *insp = (suscan_inspector_t *) userdata;

  if (insp == NULL)
    return SU_TRUE;

  return suscan_inspector_factory_feed(
    suscan_inspector_get_factory(insp),
    insp,
    data,
    size);
}

SUPRIVATE void
suscan_sc_inspector_on_new_freq(
    const struct sigutils_specttuner_channel *channel,
    void *userdata,
    SUFLOAT prev_f0,
    SUFLOAT new_f0)
{
  suscan_inspector_t *insp = (suscan_inspector_t *) userdata;
  
  if (insp == NULL)
    return;

  suscan_inspector_factory_notify_freq(
    suscan_inspector_get_factory(insp),
    insp,
    prev_f0 * channel->decimation,
    new_f0 * channel->decimation);
}

SUPRIVATE void *
suscan_sc_inspector_factory_open(
  void *userdata, 
  const char **inspclass, 
  struct suscan_inspector_sampling_info *samp_info, 
  va_list ap)
{
  suscan_inspector_t *self = (suscan_inspector_t *) userdata;
  const char *classname;
  const struct sigutils_channel *channel;
  su_specttuner_channel_t *schan;
  SUBOOL precise;

  classname = va_arg(ap, const char *);
  channel   = va_arg(ap, const struct sigutils_channel *);
  precise   = va_arg(ap, SUBOOL);

  SU_TRYCATCH(
    schan = suscan_inspector_open_sc_channel_ex(
      self,
      channel,
      precise,
      suscan_sc_inspector_on_channel_data,
      suscan_sc_inspector_on_new_freq,
      NULL),
    return NULL);

  /* Prepare output fields */
  *inspclass = classname;

  /* Initialize sampling info */
  samp_info->equiv_fs = self->samp_info.equiv_fs / schan->decimation;
  samp_info->bw_bd    = SU_ANG2NORM_FREQ(su_specttuner_channel_get_bw(schan));
  samp_info->bw       = .5 * schan->decimation * samp_info->bw_bd;
  samp_info->f0       = 
    SU_ANG2NORM_FREQ(su_specttuner_channel_get_f0(schan)) * schan->decimation;
    
  samp_info->fft_size        = schan->size;
  samp_info->fft_bins        = schan->width;
  samp_info->early_windowing = su_specttuner_uses_early_windowing(self->sc_stuner);
  samp_info->decimation      = self->samp_info.equiv_fs;
  
  return schan;
}

SUPRIVATE void
suscan_sc_inspector_factory_bind(
  void *self, 
  void *insp_self, 
  suscan_inspector_t *insp)
{
  su_specttuner_channel_t *chan = (su_specttuner_channel_t *) insp_self;

  /* We need to do this here. */
  suscan_inspector_set_domain(
    insp,
    suscan_inspector_is_freq_domain(insp));

  
  /* TODO: Assign inspector to channel and open a handle (use SU_REF) */
  chan->params.privdata = insp;

  SU_REF(insp, specttuner);
}

SUPRIVATE void
suscan_sc_inspector_factory_close(
  void *userdata, 
  void *insp_self)
{
  suscan_inspector_t *self      = (suscan_inspector_t *) userdata;
  su_specttuner_channel_t *chan = (su_specttuner_channel_t *) insp_self;
  suscan_inspector_t *insp      = (suscan_inspector_t *) chan->params.privdata;

  if (insp != NULL)
    SU_DEREF(insp, specttuner);

  if (!suscan_inspector_open_sc_close_channel(self, chan))
    SU_WARNING("Failed to close channel!\n");
}

SUPRIVATE void
suscan_sc_inspector_factory_free_buf(
  void *self, 
  void *insp_self, 
  SUCOMPLEX *data,
  SUSCOUNT len)
{
  /* TODO: No-op */
}

SUPRIVATE SUBOOL
suscan_sc_inspector_factory_set_bandwidth(
  void *userdata, 
  void *insp_userdata, 
  SUFLOAT bandwidth)
{
  suscan_inspector_t *self      = (suscan_inspector_t *) userdata;
  su_specttuner_channel_t *chan = (su_specttuner_channel_t *) insp_userdata;
  SUFLOAT relbw;

  relbw = SU_NORM2ANG_FREQ(
    SU_ABS2NORM_FREQ(
      self->samp_info.equiv_fs,
      bandwidth));

  (void) su_specttuner_set_channel_bandwidth(self->sc_stuner, chan, relbw);

  return SU_TRUE;
}

SUPRIVATE SUFLOAT
suscan_sc_inspector_factory_get_bandwidth(
  void *userdata, 
  void *insp_userdata)
{
  suscan_inspector_t *self = (suscan_inspector_t *) userdata;
  su_specttuner_channel_t *chan = (su_specttuner_channel_t *) insp_userdata;
  SUFLOAT relbw = su_specttuner_channel_get_bw(chan);

  return SU_NORM2ABS_FREQ(self->samp_info.equiv_fs, SU_ANG2NORM_FREQ(relbw));
}

SUPRIVATE SUBOOL
suscan_sc_inspector_factory_set_frequency(
  void *userdata, 
  void *insp_userdata, 
  SUFREQ frequency)
{
  suscan_inspector_t *self      = (suscan_inspector_t *) userdata;
  su_specttuner_channel_t *chan = (su_specttuner_channel_t *) insp_userdata;
  SUFLOAT f0;

  f0 = SU_NORM2ANG_FREQ(
        SU_ABS2NORM_FREQ(
            self->samp_info.equiv_fs,
            frequency));

  if (f0 < 0)
    f0 += 2 * PI;

  (void) su_specttuner_set_channel_freq(self->sc_stuner, chan, f0);

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_sc_inspector_factory_set_domain(
  void *userdata, 
  void *insp_userdata, 
  SUBOOL is_freq)
{
  su_specttuner_channel_t *chan = (su_specttuner_channel_t *) insp_userdata;
  
  su_specttuner_channel_set_domain(
    chan,
    is_freq 
    ? SU_SPECTTUNER_CHANNEL_FREQUENCY_DOMAIN
    : SU_SPECTTUNER_CHANNEL_TIME_DOMAIN);    

  return SU_TRUE;
}


SUPRIVATE SUFREQ
suscan_sc_inspector_factory_get_abs_freq(
  void *userdata, 
  void *insp_userdata)
{
  suscan_inspector_t *self      = (suscan_inspector_t *) userdata;
  su_specttuner_channel_t *chan = (su_specttuner_channel_t *) insp_userdata;
  SUFREQ channel_freq 
    = SU_NORM2ABS_FREQ(
        self->samp_info.equiv_fs,
        SU_ANG2NORM_FREQ(su_specttuner_channel_get_f0(chan)));

  return channel_freq;
}

SUPRIVATE SUBOOL
suscan_sc_inspector_factory_set_freq_correction(
  void *userdata, 
  void *insp_userdata,
  SUFLOAT delta)
{
  suscan_inspector_t *self      = (suscan_inspector_t *) userdata;
  su_specttuner_channel_t *chan = (su_specttuner_channel_t *) insp_userdata;
  SUFLOAT domega 
    = SU_NORM2ANG_FREQ(
        SU_ABS2NORM_FREQ(
          self->samp_info.equiv_fs,
          delta));
  
  su_specttuner_set_channel_delta_f(self->sc_stuner, chan, domega);

  return SU_TRUE;
}

SUPRIVATE void
suscan_sc_inspector_factory_dtor(void *self)
{
  /* No-op */
}

static struct suscan_inspector_factory_class g_sc_factory = {
  .name                = "sc-inspector",
  .ctor                = suscan_sc_inspector_factory_ctor,
  .get_time            = suscan_sc_inspector_factory_get_time,
  .open                = suscan_sc_inspector_factory_open,
  .bind                = suscan_sc_inspector_factory_bind,
  .close               = suscan_sc_inspector_factory_close,
  .free_buf            = suscan_sc_inspector_factory_free_buf,
  .set_bandwidth       = suscan_sc_inspector_factory_set_bandwidth,
  .get_bandwidth       = suscan_sc_inspector_factory_get_bandwidth,
  .set_frequency       = suscan_sc_inspector_factory_set_frequency,
  .set_domain          = suscan_sc_inspector_factory_set_domain,
  .get_abs_freq        = suscan_sc_inspector_factory_get_abs_freq,
  .set_freq_correction = suscan_sc_inspector_factory_set_freq_correction,
  .dtor                = suscan_sc_inspector_factory_dtor
};

SUBOOL
suscan_inspector_register_factory(void)
{
  return suscan_inspector_factory_class_register(&g_sc_factory);
}

/********************* Inspector loop methods ***************************/
SUBOOL
suscan_inspector_sampler_loop(
    suscan_inspector_t *insp,
    const SUCOMPLEX *samp_buf,
    SUSCOUNT samp_count)
{
  struct suscan_analyzer_sample_batch_msg *msg = NULL;
  unsigned int length;

  SUSDIFF fed;

  while (samp_count > 0) {
    /* Ensure the current inspector parameters are up-to-date */
    suscan_inspector_assert_params(insp);

    SU_TRYCATCH(
        (fed = suscan_inspector_feed_bulk(insp, samp_buf, samp_count)) >= 0,
        goto fail);

    length = suscan_inspector_get_output_length(insp);

    if (length > 0 && (length >= insp->sample_msg_watermark
        || suscan_inspector_sampler_buf_avail(insp) == 0)) {
      /* New samples produced by sampler: send to client */
      SU_TRYCATCH(
          msg = suscan_analyzer_sample_batch_msg_new(
              insp->inspector_id,
              suscan_inspector_get_output_buffer(insp),
              suscan_inspector_get_output_length(insp)),
          goto fail);

      /* Reset size */
      insp->sampler_ptr = 0;

      SU_TRYCATCH(
          suscan_mq_write(
            insp->mq_out, 
            SUSCAN_ANALYZER_MESSAGE_TYPE_SAMPLES, 
            msg),
          goto fail);

      msg = NULL; /* We don't own this anymore */
    }

    samp_buf   += fed;
    samp_count -= fed;
  }

  return SU_TRUE;

fail:
  if (msg != NULL)
    suscan_analyzer_sample_batch_msg_destroy(msg);

  return SU_FALSE;
}

SUPRIVATE SUBOOL
suscan_inspector_send_freq_domain_psd(
    void *userdata,
    const SUCOMPLEX *x,
    SUSCOUNT size)
{
  struct suscan_analyzer_inspector_msg *msg = NULL;
  suscan_inspector_t *insp = (suscan_inspector_t *) userdata;
  SUSCOUNT i;
  SUBOOL ok = SU_FALSE;
  SUFLOAT K;

  SU_TRYCATCH(
      msg = suscan_analyzer_inspector_msg_new(
          SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SPECTRUM,
          rand()),
      goto done);

  K = (8. / 3.) / insp->samp_info.fft_size;

  msg->inspector_id  = insp->inspector_id;
  msg->spectsrc_id   = insp->spectsrc_index;
  msg->samp_rate     = insp->samp_info.equiv_fs;
  msg->spectrum_size = size;

  SU_TRYCATCH(
      msg->spectrum_data = malloc(size * sizeof(SUFLOAT)),
      goto done);

  for (i = 0; i < size; ++i)
    msg->spectrum_data[i] = K * SU_C_REAL(x[i] * SU_C_CONJ(x[i]));

  /* Provide a more accurate real timestamp */
  gettimeofday(&msg->rt_time, NULL);
  SU_TRYCATCH(
      suscan_mq_write(
          insp->mq_out,
          SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR,
          msg),
      goto done);

  msg = NULL; /* We don't own this anymore */

  ok = SU_TRUE;

done:
  if (msg != NULL)
    suscan_analyzer_inspector_msg_destroy(msg);

  return ok;
}

SUBOOL
suscan_inspector_spectrum_loop(
    suscan_inspector_t *insp,
    const SUCOMPLEX *samp_buf,
    SUSCOUNT samp_count)
{
  suscan_spectsrc_t *src = NULL;
  SUSDIFF fed;

  if (insp->spectsrc_index > 0) {
    src = insp->spectsrc_list[insp->spectsrc_index - 1];

    if (suscan_inspector_is_freq_domain(insp)) {
      uint64_t interval = insp->interval_spectrum * 1e9;
      uint64_t now = suscan_gettime();
      if (now - insp->last_spectrum > interval) {
        insp->last_spectrum = now;

        SU_TRY_FAIL(
          suscan_inspector_send_freq_domain_psd(
            insp,
            samp_buf,
            samp_count));
      }
    } else {
      while (samp_count > 0) {
        fed = suscan_spectsrc_feed(src, samp_buf, samp_count);

        SU_TRY_FAIL(fed >= 0);

        samp_buf   += fed;
        samp_count -= fed;
      }
    }
  }

  return SU_TRUE;

fail:
  return SU_FALSE;
}

SUBOOL
suscan_inspector_send_signal(
    suscan_inspector_t *self,
    const char *name,
    SUDOUBLE value)
{
  struct suscan_analyzer_inspector_msg *msg = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRY(
    msg = suscan_analyzer_inspector_msg_new(
        SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SIGNAL,
        rand()));

  SU_TRY(msg->signal_name = strdup(name));
  msg->signal_value = value;
  msg->inspector_id = self->inspector_id;
  
  SU_TRY(
    suscan_mq_write(
        self->mq_out,
        SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR,
        msg));
  
  msg = NULL;

  ok = SU_TRUE;

done:
  if (msg != NULL)
    suscan_analyzer_inspector_msg_destroy(msg);
  
  return ok;
}

SUBOOL
suscan_inspector_estimator_loop(
    suscan_inspector_t *insp,
    const SUCOMPLEX *samp_buf,
    SUSCOUNT samp_count)
{
  struct suscan_analyzer_inspector_msg *msg = NULL;
  unsigned int i;
  uint64_t now;
  SUFLOAT value;
  SUFLOAT seconds;

  /* Check esimator state and update clients */
  if (insp->interval_estimator > 0) {
    now = suscan_gettime();
    seconds = (now - insp->last_estimator) * 1e-9;
    if (seconds >= insp->interval_estimator) {
      insp->last_estimator = now;
      for (i = 0; i < insp->estimator_count; ++i)
        if (suscan_estimator_is_enabled(insp->estimator_list[i])) {
          if (suscan_estimator_is_enabled(insp->estimator_list[i]))
            SU_TRYCATCH(
                suscan_estimator_feed(
                    insp->estimator_list[i],
                    samp_buf,
                    samp_count),
                goto fail);

          if (suscan_estimator_read(insp->estimator_list[i], &value)) {
            SU_TRYCATCH(
                msg = suscan_analyzer_inspector_msg_new(
                    SUSCAN_ANALYZER_INSPECTOR_MSGKIND_ESTIMATOR,
                    rand()),
                goto fail);

            msg->enabled = SU_TRUE;
            msg->estimator_id = i;
            msg->value = value;
            msg->inspector_id = insp->inspector_id;

            SU_TRYCATCH(
                suscan_mq_write(
                    insp->mq_out,
                    SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR,
                    msg),
                goto fail);
          }
        }
    }
  }

  return SU_TRUE;

fail:
  if (msg != NULL)
    suscan_analyzer_inspector_msg_destroy(msg);

  return SU_FALSE;
}

SUBOOL 
suscan_inspector_set_corrector(
  suscan_inspector_t *self, 
  suscan_frequency_corrector_t *corrector)
{
  SUBOOL ok = SU_FALSE;
  SUBOOL mutex_acquired = SU_FALSE;

  SU_TRYC(pthread_mutex_lock(&self->corrector_mutex));
  mutex_acquired = SU_TRUE;

  if (self->corrector != NULL)
    suscan_frequency_corrector_destroy(self->corrector);

  self->corrector = corrector;

  /* Delegated to factory */
  if (corrector == NULL)
    suscan_inspector_factory_set_inspector_freq_correction(
      self->factory,
      self,
      0.);

  ok = SU_TRUE;

done:
  if (mutex_acquired)
    pthread_mutex_unlock(&self->corrector_mutex);

  return ok;
}

SUBOOL 
suscan_inspector_disable_corrector(suscan_inspector_t *self)
{
  return suscan_inspector_set_corrector(self, NULL);
}

SUBOOL 
suscan_inspector_get_correction(
  suscan_inspector_t *self, 
  const struct timeval *tv,
  SUFREQ abs_freq,
  SUFLOAT *freq)
{
  SUBOOL it_is = SU_FALSE;
  SUBOOL mutex_acquired = SU_FALSE;

  SU_TRYC(pthread_mutex_lock(&self->corrector_mutex));
  mutex_acquired = SU_TRUE;

  if (self->corrector != NULL 
    && suscan_frequency_corrector_is_applicable(self->corrector, tv)) {
    *freq = suscan_frequency_corrector_get_correction(
        self->corrector,
        tv,
        abs_freq);
    it_is = SU_TRUE;
  }

done:
  if (mutex_acquired)
    pthread_mutex_unlock(&self->corrector_mutex);

  return it_is;
}

SUBOOL
suscan_inspector_deliver_report(
  suscan_inspector_t *self,
  const struct timeval *tv,
  SUFREQ abs_freq)
{
  uint64_t now;
  struct suscan_analyzer_inspector_msg *msg = NULL;
  struct suscan_orbit_report report;
  SUFLOAT seconds;
  SUBOOL have_report;
  SUBOOL mutex_acquired = SU_FALSE;
  SUBOOL ok = SU_FALSE;

  if (self->interval_orbit_report > 0) {
    now = suscan_gettime();
    seconds = (now - self->last_orbit_report) * 1e-9;
    if (seconds >= self->interval_orbit_report) {
      self->last_orbit_report = now;

      SU_TRYC(pthread_mutex_lock(&self->corrector_mutex));
      mutex_acquired = SU_TRUE;

      if (self->corrector == NULL) {
        ok = SU_TRUE;
        goto done;
      }

      /* Attempt to acquire a report */
      have_report = suscan_frequency_corrector_tle_get_report(
        self->corrector, 
        tv, 
        abs_freq,
        &report);

      pthread_mutex_unlock(&self->corrector_mutex);
      mutex_acquired = SU_FALSE;

      /* We have a report! Construct and deliver. */
      if (have_report) {
        SU_TRYCATCH(
          msg = suscan_analyzer_inspector_msg_new(
            SUSCAN_ANALYZER_INSPECTOR_MSGKIND_ORBIT_REPORT,
            rand()),
          goto done);
        msg->inspector_id = self->inspector_id;
        msg->orbit_report = report;

        SU_TRYCATCH(
          suscan_mq_write(
            self->mq_out,
            SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR,
            msg),
          goto done);

        msg = NULL;
      }
    }
  }

  ok = SU_TRUE;

done:
  if (mutex_acquired)
    pthread_mutex_unlock(&self->corrector_mutex);

  if (msg != NULL)
    suscan_analyzer_inspector_msg_destroy(msg);

  return ok;
}

void
suscan_inspector_assert_params(suscan_inspector_t *insp)
{
  if (insp->params_requested) {
    suscan_inspector_lock(insp);

    (insp->iface->commit_config) (insp->privdata);
    insp->params_requested = SU_FALSE;

    suscan_inspector_unlock(insp);
  }

  if (insp->bandwidth_notified) {
    suscan_inspector_lock(insp);

    if (insp->iface->new_bandwidth != NULL)
      (insp->iface->new_bandwidth) (insp->privdata, insp->new_bandwidth);
    insp->bandwidth_notified = SU_FALSE;

    suscan_inspector_unlock(insp);
  }
}

void
suscan_inspector_destroy(suscan_inspector_t *self)
{
  unsigned int i;

  SUSCAN_FINALIZE_REFCOUNT(self);

  if (self->sc_factory != NULL)
    suscan_inspector_factory_destroy(self->sc_factory);

  if (self->sc_stuner_init)
    pthread_mutex_destroy(&self->sc_stuner_mutex);

  if (self->sc_stuner != NULL)
    su_specttuner_destroy(self->sc_stuner);
    
  if (self->mutex_init)
    pthread_mutex_destroy(&self->mutex);

  if (self->corrector_init)
    pthread_mutex_destroy(&self->corrector_mutex);

  if (self->corrector != NULL)
    suscan_frequency_corrector_destroy(self->corrector);

  if (self->privdata != NULL)
    (self->iface->close) (self->privdata);

  for (i = 0; i < self->estimator_count; ++i)
    suscan_estimator_destroy(self->estimator_list[i]);

  if (self->estimator_list != NULL)
    free(self->estimator_list);

  for (i = 0; i < self->spectsrc_count; ++i)
    suscan_spectsrc_destroy(self->spectsrc_list[i]);

  if (self->spectsrc_list != NULL)
    free(self->spectsrc_list);

  free(self);
}

SUBOOL
suscan_inspector_set_config(
    suscan_inspector_t *insp,
    const suscan_config_t *config)
{
  /* TODO: Protect? */
  insp->params_requested = SU_TRUE;

  return (insp->iface->parse_config) (insp->privdata, config);
}

void
suscan_inspector_set_throttle_factor(
  suscan_inspector_t *self,
  SUFLOAT factor)
{
  unsigned int i;

  if (factor <= 0.)
    factor = 1.;
    
  for (i = 0; i < self->spectsrc_count; ++i)
    suscan_spectsrc_set_throttle_factor(self->spectsrc_list[i], factor);
}

void
suscan_inspector_set_domain(suscan_inspector_t *self, SUBOOL domain)
{
  self->frequency_domain = domain;
  suscan_inspector_factory_set_inspector_domain(
    self->factory,
    self,
    domain);
}

SUBOOL
suscan_inspector_get_config(
    const suscan_inspector_t *insp,
    suscan_config_t *config)
{
  return (insp->iface->get_config) (insp->privdata, config);
}

SUBOOL
suscan_inspector_notify_bandwidth(
    suscan_inspector_t *insp,
    SUFREQ new_bandwidth)
{
  insp->new_bandwidth = new_bandwidth;
  insp->bandwidth_notified = SU_TRUE;

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_inspector_add_estimator(
    suscan_inspector_t *insp,
    const struct suscan_estimator_class *class)
{
  suscan_estimator_t *estimator = NULL;

  SU_TRYCATCH(
      estimator = suscan_estimator_new(class, insp->samp_info.equiv_fs),
      goto fail);

  SU_TRYCATCH(
      PTR_LIST_APPEND_CHECK(insp->estimator, estimator) != -1,
      goto fail);

  return SU_TRUE;

fail:
  if (estimator != NULL)
    suscan_estimator_destroy(estimator);

  return SU_FALSE;
}

SUPRIVATE SUBOOL
suscan_inspector_on_spectrum_data(
    void *userdata,
    const SUFLOAT *spectrum,
    SUSCOUNT size)
{
  struct suscan_analyzer_inspector_msg *msg = NULL;
  suscan_inspector_t *insp = (suscan_inspector_t *) userdata;

  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      msg = suscan_analyzer_inspector_msg_new(
          SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SPECTRUM,
          rand()),
      goto done);

  msg->inspector_id  = insp->inspector_id;
  msg->spectsrc_id   = insp->spectsrc_index;
  msg->samp_rate     = insp->samp_info.equiv_fs;
  msg->spectrum_size = size;

  SU_TRYCATCH(
      msg->spectrum_data = malloc(size * sizeof(SUFLOAT)),
      goto done);

  memcpy(msg->spectrum_data, spectrum, size * sizeof(SUFLOAT));

  /* Provide a more accurate real timestamp */
  gettimeofday(&msg->rt_time, NULL);
  SU_TRYCATCH(
      suscan_mq_write(
          insp->mq_out,
          SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR,
          msg),
      goto done);

  msg = NULL; /* We don't own this anymore */

  ok = SU_TRUE;

done:
  if (msg != NULL)
    suscan_analyzer_inspector_msg_destroy(msg);

  return ok;
}

SUPRIVATE SUBOOL
suscan_inspector_add_spectsrc(
    suscan_inspector_t *insp,
    const struct suscan_spectsrc_class *class)
{
  suscan_spectsrc_t *src = NULL;
  SUBOOL ok = SU_FALSE;

  SU_MAKE(
    src,
    suscan_spectsrc,
    class,
    insp->samp_info.equiv_fs,
    1. / insp->interval_spectrum,
    SUSCAN_INSPECTOR_SPECTRUM_BUF_SIZE,
    SU_CHANNEL_DETECTOR_WINDOW_BLACKMANN_HARRIS,
    suscan_inspector_on_spectrum_data,
    insp);

  SU_TRYC(PTR_LIST_APPEND_CHECK(insp->spectsrc, src));
  src = NULL;

  ok = SU_TRUE;

done:
  if (src != NULL)
    suscan_spectsrc_destroy(src);

  return ok;
}

SUBOOL
suscan_inspector_feed_sc_stuner(
  suscan_inspector_t *self,
  const SUCOMPLEX *data,
  SUSCOUNT size)
{
  SUSDIFF got;
  SUBOOL ok = SU_FALSE;

  if (self->sc_stuner == NULL) {
    SU_ERROR("Subcarrier inspection not enabled\n");
    goto done;
  }

  if (su_specttuner_get_channel_count(self->sc_stuner) == 0)
    return SU_TRUE;

  /* This must be performed in a serialized way */
  while (size > 0) {

    /*
     * Must be protected from access by the analyzer thread: right now,
     * only the source worker can access the tuner.
     */
    if (pthread_mutex_lock(&self->sc_stuner_mutex) != 0)
      return SU_FALSE;

    got = su_specttuner_feed_bulk_single(self->sc_stuner, data, size);

    if (su_specttuner_new_data(self->sc_stuner)) {
      /*
       * New data has been queued to the existing inspectors. We must
       * ensure that all of them are done by issuing a barrier at the end
       * of the worker queue.
       */

      suscan_inspector_factory_force_sync(self->sc_factory);

      su_specttuner_ack_data(self->sc_stuner);
    }

    (void) pthread_mutex_unlock(&self->sc_stuner_mutex);

    if (got == -1)
      goto done;

    data += got;
    size -= got;
  }

  ok = SU_TRUE;

done:
  return ok;
}

SUBOOL
suscan_inspector_walk_inspectors(
  suscan_inspector_t *self,
  SUBOOL (*callback) (
    void *userdata,
    struct suscan_inspector *insp),
  void *userdata)
{
  if (self->sc_factory != NULL)
    return suscan_inspector_factory_walk_inspectors(
      self->sc_factory,
      callback,
      userdata);

  return SU_TRUE;
}

suscan_inspector_t *
suscan_inspector_new(
    struct suscan_inspector_factory *owner,
    const char *name,
    const struct suscan_inspector_sampling_info *samp_info,
    struct suscan_mq *mq_out,
    struct suscan_mq *mq_ctl,
    void *userdata)
{
  suscan_inspector_t *new = NULL;
  struct sigutils_specttuner_params sparams =
    sigutils_specttuner_params_INITIALIZER;
  static SUBOOL factory_registered = SU_FALSE;
  const struct suscan_inspector_interface *iface = NULL;
  pthread_mutexattr_t attr;
  unsigned int i;

  if ((iface = suscan_inspector_interface_lookup(name)) == NULL) {
    SU_ERROR("Unknown inspector type: `%s'\n", name);
    goto fail;
  }

  SU_TRYCATCH(new = calloc(1, sizeof (suscan_inspector_t)), goto fail);
  new->state            = SUSCAN_ASYNC_STATE_CREATED;
  new->samp_info        = *samp_info;
  new->frequency_domain = iface->frequency_domain;

  /* Initialize reference counting */
  SU_TRYCATCH(SUSCAN_INIT_REFCOUNT(suscan_inspector, new), goto fail);

  /* Initialize mutexes */
  SU_TRYCATCH(pthread_mutex_init(&new->mutex, NULL) == 0, goto fail);
  new->mutex_init = SU_TRUE;

  SU_TRYCATCH(pthread_mutex_init(&new->corrector_mutex, NULL) == 0, goto fail);
  new->corrector_init = SU_TRUE;

  /* Factory specific fields */
  new->factory          = owner;
  new->factory_userdata = userdata;

  /* Cached fields */
  new->mq_out           = mq_out;
  new->mq_ctl           = mq_ctl;

  /* Spectrum and estimator updates */
  new->interval_estimator    = .1;
  new->interval_spectrum     = .1;
  new->interval_orbit_report = .25;

  /* Initialize clocks */
  new->last_estimator = suscan_gettime();
  new->last_spectrum  = suscan_gettime();

  /* All set to call specific inspector */
  new->iface = iface;
  SU_TRYCATCH(new->privdata = (iface->open) (&new->samp_info), goto fail);

  /* 
   * If the interface reports the ability to perform subcarrier inspection,
   * initialize the inspector factory.
   */
  if (iface->sc_factory_class != NULL) {
    if (!factory_registered) {
      SU_TRYCATCH(suscan_inspector_register_factory(), goto fail);
      factory_registered = SU_TRUE;
    }

    sparams.window_size = SUSCAN_INSPECTOR_TUNER_BUF_SIZE;
    sparams.early_windowing = SU_TRUE;

    SU_TRYCATCH(
      new->sc_stuner = su_specttuner_new(&sparams),
      goto fail);

    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    SU_TRYCATCH(
      pthread_mutex_init(&new->sc_stuner_mutex, &attr) == 0,
      goto fail);

    new->sc_stuner_init = SU_TRUE;
    SU_TRYCATCH(
      new->sc_factory = suscan_inspector_factory_new(
        iface->sc_factory_class,
        new),
      goto fail);
  }
  
  /* Creation successful! Add all estimators and spectrum sources */
  for (i = 0; i < iface->spectsrc_count; ++i)
    SU_TRYCATCH(
        suscan_inspector_add_spectsrc(new, iface->spectsrc_list[i]),
        goto fail);

  for (i = 0; i < iface->estimator_count; ++i)
    SU_TRYCATCH(
        suscan_inspector_add_estimator(new, iface->estimator_list[i]),
        goto fail);

  return new;

fail:
  if (new != NULL)
    suscan_inspector_destroy(new);

  return NULL;
}

SUSDIFF
suscan_inspector_feed_bulk(
    suscan_inspector_t *insp,
    const SUCOMPLEX *x,
    int count)
{
  SUSDIFF result;

  suscan_inspector_lock(insp);
  result = (insp->iface->feed) (insp->privdata, insp, x, count);
  suscan_inspector_unlock(insp);

  return result;
}

void
suscan_inspector_notify_freq(
    suscan_inspector_t *insp,
    SUFLOAT prev_freq,
    SUFLOAT next_freq)
{
  if (insp->iface->freq_changed != NULL) {
    suscan_inspector_lock(insp);
    (insp->iface->freq_changed) (insp->privdata, insp, prev_freq, next_freq);
    suscan_inspector_unlock(insp);
  }
}

SUBOOL
suscan_init_inspectors(void)
{
  SU_TRYCATCH(suscan_tle_corrector_init(),       return SU_FALSE);

  SU_TRYCATCH(suscan_ask_inspector_register(),   return SU_FALSE);
  SU_TRYCATCH(suscan_psk_inspector_register(),   return SU_FALSE);
  SU_TRYCATCH(suscan_fsk_inspector_register(),   return SU_FALSE);
  SU_TRYCATCH(suscan_audio_inspector_register(), return SU_FALSE);
  SU_TRYCATCH(suscan_raw_inspector_register(),   return SU_FALSE);
  SU_TRYCATCH(suscan_power_inspector_register(), return SU_FALSE);
  SU_TRYCATCH(suscan_drift_inspector_register(), return SU_FALSE);
  SU_TRYCATCH(suscan_multicarrier_inspector_register(),   return SU_FALSE);


  return SU_TRUE;
}

