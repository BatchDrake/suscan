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

/*
 * This is the channel analyzer worker: receives data, channelizes and
 * feeds inspectors.
 */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <stdint.h>
#include <time.h>

#define SU_LOG_DOMAIN "channel-analyzer"

#include <sigutils/sigutils.h>
#include <sigutils/detect.h>
#include <analyzer/impl/local.h>

#include "realtime.h"

#include "mq.h"
#include "msg.h"

/*********************** Performance measurement *****************************/
SUINLINE void
suscan_local_analyzer_read_start(suscan_local_analyzer_t *analyzeryzer)
{
  analyzeryzer->read_start = suscan_gettime_coarse();
}

SUINLINE void
suscan_local_analyzer_process_start(suscan_local_analyzer_t *analyzer)
{
  analyzer->process_start = suscan_gettime_coarse();
}

SUINLINE void
suscan_local_analyzer_process_end(suscan_local_analyzer_t *analyzer)
{
  uint64_t total, cpu;

  analyzer->process_end = suscan_gettime_coarse();

  if (analyzer->read_start != 0) {
    total = analyzer->process_end - analyzer->read_start;
    cpu = analyzer->process_end - analyzer->process_start;

    /* Update CPU usage */
    if (total == 0)
      analyzer->cpu_usage +=
          SUSCAN_ANALYZER_CPU_USAGE_UPDATE_ALPHA
          * (1. - analyzer->cpu_usage);
    else
      analyzer->cpu_usage +=
          SUSCAN_ANALYZER_CPU_USAGE_UPDATE_ALPHA
          * ((SUFLOAT) cpu / (SUFLOAT) total - analyzer->cpu_usage);
  }
}


/********************* Related channel analyzer funcs ************************/
SUPRIVATE SUBOOL
suscan_local_analyzer_feed_baseband_filters(
    suscan_local_analyzer_t *analyzer,
    const SUCOMPLEX *samples,
    SUSCOUNT length)
{
  struct rbtree_node *this;
  struct suscan_analyzer_baseband_filter *bbfilt;

  this = rbtree_get_first(analyzer->bbfilt_tree);

  while (this != NULL) {
    bbfilt = rbtree_node_data(this);
    if (bbfilt != NULL) {
      if (!bbfilt->func(
          bbfilt->privdata,
          analyzer->parent,
          samples,
          length))
        return SU_FALSE;
    }

    this = rbtree_node_next(this);
  }

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_local_analyzer_feed_inspectors(
    suscan_local_analyzer_t *self,
    const SUCOMPLEX *data,
    SUSCOUNT size)
{
  SUSDIFF got;
  SUBOOL ok = SU_TRUE;

  /*
   * No opened channels. We can avoid doing extra work. However, we
   * should clean the tuner in this case to keep it from having
   * samples from previous calls to feed_bulk.
   */
  if (su_specttuner_get_channel_count(self->stuner) == 0)
    return SU_TRUE;

  /* This must be performed in a serialized way */
  while (size > 0) {

    /*
     * Must be protected from access by the analyzer thread: right now,
     * only the source worker can access the tuner.
     */
    if (pthread_mutex_lock(&self->stuner_mutex) != 0)
      return SU_FALSE;

    got = su_specttuner_feed_bulk_single(self->stuner, data, size);

    if (su_specttuner_new_data(self->stuner)) {
      /*
       * New data has been queued to the existing inspectors. We must
       * ensure that all of them are done by issuing a barrier at the end
       * of the worker queue.
       */

      suscan_inspector_factory_force_sync(self->insp_factory);

      su_specttuner_ack_data(self->stuner);
    }

    (void) pthread_mutex_unlock(&self->stuner_mutex);

    if (got == -1)
      ok = SU_FALSE;

    data += got;
    size -= got;
  }

  return ok;
}

SUPRIVATE SUBOOL
suscan_local_analyzer_on_channel_data(
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


/*********************** Channel opening and closing *************************/
SUPRIVATE su_specttuner_channel_t *
suscan_local_analyzer_open_channel_ex(
    suscan_local_analyzer_t *self,
    const struct sigutils_channel *chan_info,
    SUBOOL precise,
    SUBOOL (*on_data) (
        const struct sigutils_specttuner_channel *channel,
        void *privdata,
        const SUCOMPLEX *data, /* This pointer remains valid until the next call to feed */
        SUSCOUNT size),
        void *privdata)
{
  SUBOOL mutex_acquired = SU_FALSE;
  su_specttuner_channel_t *channel = NULL;
  struct sigutils_specttuner_channel_params params =
      sigutils_specttuner_channel_params_INITIALIZER;

  params.f0 =
      SU_NORM2ANG_FREQ(
          SU_ABS2NORM_FREQ(
              suscan_analyzer_get_samp_rate(self->parent),
              chan_info->fc - chan_info->ft));

  if (params.f0 < 0)
    params.f0 += 2 * PI;

  params.bw =
      SU_NORM2ANG_FREQ(
          SU_ABS2NORM_FREQ(
              suscan_analyzer_get_samp_rate(self->parent),
              chan_info->f_hi - chan_info->f_lo));
  params.guard    = SUSCAN_ANALYZER_GUARD_BAND_PROPORTION;
  params.on_data  = on_data;
  params.privdata = privdata;
  params.precise  = precise;

  SU_TRYCATCH(pthread_mutex_lock(&self->stuner_mutex) == 0, goto done);
  mutex_acquired = SU_TRUE;

  SU_TRYCATCH(
      channel = su_specttuner_open_channel(self->stuner, &params),
      goto done);

done:
  if (mutex_acquired)
    (void) pthread_mutex_unlock(&self->stuner_mutex);

  return channel;
}

/* TODO: Move this logic to factory impl */
SUPRIVATE SUBOOL
suscan_local_analyzer_close_channel(
    suscan_local_analyzer_t *self,
    su_specttuner_channel_t *channel)
{
  SUBOOL mutex_acquired = SU_FALSE;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(pthread_mutex_lock(&self->stuner_mutex) == 0, goto done);
  mutex_acquired = SU_TRUE;

  ok = su_specttuner_close_channel(self->stuner, channel);

done:
  if (mutex_acquired)
    (void) pthread_mutex_unlock(&self->stuner_mutex);

  return ok;
}

/**************** Implementation of the local inspector factory **************/
SUPRIVATE void *
suscan_local_inspector_factory_ctor(suscan_inspector_factory_t *parent, va_list ap)
{
  suscan_local_analyzer_t *self;

  self = va_arg(ap, suscan_local_analyzer_t *);

  suscan_inspector_factory_set_mq_out(parent, self->parent->mq_out);
  suscan_inspector_factory_set_mq_ctl(parent, &self->mq_in);

  return self;
}

SUPRIVATE void
suscan_local_inspector_factory_get_time(void *userdata, struct timeval *tv)
{
  suscan_local_analyzer_t *self = (suscan_local_analyzer_t *) userdata;

  suscan_source_get_time(self->source, tv);
}

SUPRIVATE void *
suscan_local_inspector_factory_open(
  void *userdata, 
  const char **inspclass, 
  struct suscan_inspector_sampling_info *samp_info, 
  va_list ap)
{
  suscan_local_analyzer_t *self = (suscan_local_analyzer_t *) userdata;
  unsigned int samp_rate = suscan_analyzer_get_samp_rate(self->parent);
  const char *classname;
  const struct sigutils_channel *channel;
  su_specttuner_channel_t *schan;
  SUBOOL precise;

  classname = va_arg(ap, const char *);
  channel   = va_arg(ap, const struct sigutils_channel *);
  precise   = va_arg(ap, SUBOOL);

  schan = suscan_local_analyzer_open_channel_ex(
    self,
    channel,
    precise,
    suscan_local_analyzer_on_channel_data,
    NULL);

  if (schan == NULL) {
    SU_ERROR("Local inspector factory: failed to open channel (invalid channel?)\n");
    return NULL;
  }

  /* Prepare output fields */
  *inspclass = classname;

  /* Initialize sampling info */
  samp_info->equiv_fs = SU_ASFLOAT(samp_rate) / schan->decimation;
  samp_info->bw_bd    = SU_ANG2NORM_FREQ(su_specttuner_channel_get_bw(schan));
  samp_info->bw       = .5 * schan->decimation * samp_info->bw_bd;
  samp_info->f0       = SU_ANG2NORM_FREQ(su_specttuner_channel_get_f0(schan));
  samp_info->fft_size = schan->size;

  return schan;
}

SUPRIVATE void
suscan_local_inspector_factory_bind(
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
suscan_local_inspector_factory_close(
  void *userdata, 
  void *insp_self)
{
  suscan_local_analyzer_t *self = (suscan_local_analyzer_t *) userdata;
  su_specttuner_channel_t *chan = (su_specttuner_channel_t *) insp_self;
  suscan_inspector_t *insp      = (suscan_inspector_t *) chan->params.privdata;

  SU_DEREF(insp, specttuner);

  if (!suscan_local_analyzer_close_channel(self, chan))
    SU_WARNING("Failed to close channel!\n");
}

SUPRIVATE void
suscan_local_inspector_factory_free_buf(
  void *self, 
  void *insp_self, 
  SUCOMPLEX *data,
  SUSCOUNT len)
{
  /* TODO: No-op */
}

SUPRIVATE SUBOOL
suscan_local_inspector_factory_set_bandwidth(
  void *userdata, 
  void *insp_userdata, 
  SUFLOAT bandwidth)
{
  suscan_local_analyzer_t *self = (suscan_local_analyzer_t *) userdata;
  su_specttuner_channel_t *chan = (su_specttuner_channel_t *) insp_userdata;
  SUFLOAT relbw;

  relbw = SU_NORM2ANG_FREQ(
    SU_ABS2NORM_FREQ(
      suscan_analyzer_get_samp_rate(self->parent),
      bandwidth));

  (void) su_specttuner_set_channel_bandwidth(self->stuner, chan, relbw);

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_local_inspector_factory_set_frequency(
  void *userdata, 
  void *insp_userdata, 
  SUFREQ frequency)
{
  suscan_local_analyzer_t *self = (suscan_local_analyzer_t *) userdata;
  su_specttuner_channel_t *chan = (su_specttuner_channel_t *) insp_userdata;
  SUFLOAT f0;

  f0 = SU_NORM2ANG_FREQ(
        SU_ABS2NORM_FREQ(
            suscan_analyzer_get_samp_rate(self->parent),
            frequency));

  if (f0 < 0)
    f0 += 2 * PI;

  (void) su_specttuner_set_channel_freq(self->stuner, chan, f0);

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_local_inspector_factory_set_domain(
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
suscan_local_inspector_factory_get_abs_freq(
  void *userdata, 
  void *insp_userdata)
{
  suscan_local_analyzer_t *self = (suscan_local_analyzer_t *) userdata;
  su_specttuner_channel_t *chan = (su_specttuner_channel_t *) insp_userdata;
  unsigned int samp_rate = suscan_analyzer_get_samp_rate(self->parent);
  SUFREQ tuner_freq = self->source_info.frequency;
  SUFREQ channel_freq = 
    tuner_freq + SU_NORM2ABS_FREQ(
      samp_rate,
      SU_ANG2NORM_FREQ(su_specttuner_channel_get_f0(chan)));

  return channel_freq;
}

SUPRIVATE SUBOOL
suscan_local_inspector_factory_set_freq_correction(
  void *userdata, 
  void *insp_userdata,
  SUFLOAT delta)
{
  suscan_local_analyzer_t *self = (suscan_local_analyzer_t *) userdata;
  su_specttuner_channel_t *chan = (su_specttuner_channel_t *) insp_userdata;
  unsigned int samp_rate = suscan_analyzer_get_samp_rate(self->parent);

  SUFLOAT domega = SU_NORM2ANG_FREQ(SU_ABS2NORM_FREQ(samp_rate, delta));
  
  su_specttuner_set_channel_delta_f(self->stuner, chan, domega);

  return SU_TRUE;
}

SUPRIVATE void
suscan_local_inspector_factory_dtor(void *self)
{
  /* No-op */
}

static struct suscan_inspector_factory_class g_local_factory = {
  .name                = "local-analyzer",
  .ctor                = suscan_local_inspector_factory_ctor,
  .get_time            = suscan_local_inspector_factory_get_time,
  .open                = suscan_local_inspector_factory_open,
  .bind                = suscan_local_inspector_factory_bind,
  .close               = suscan_local_inspector_factory_close,
  .free_buf            = suscan_local_inspector_factory_free_buf,
  .set_bandwidth       = suscan_local_inspector_factory_set_bandwidth,
  .set_frequency       = suscan_local_inspector_factory_set_frequency,
  .set_domain          = suscan_local_inspector_factory_set_domain,
  .get_abs_freq        = suscan_local_inspector_factory_get_abs_freq,
  .set_freq_correction = suscan_local_inspector_factory_set_freq_correction,
  .dtor                = suscan_local_inspector_factory_dtor
};

SUBOOL
suscan_local_analyzer_register_factory(void)
{
  return suscan_inspector_factory_class_register(&g_local_factory);
}

/******************** Source worker for channel mode *************************/
SUINLINE SUBOOL
suscan_local_analyzer_parse_seek_overridable(suscan_local_analyzer_t *self)
{
  SUSCOUNT pos;

  if (self->seek_req) {
    pos = self->seek_req_value;
    suscan_source_seek(self->source, pos);
    self->seek_req = self->seek_req_value != pos;
  }

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_local_analyzer_parse_overridable(suscan_local_analyzer_t *self)
{
  /* Parse pending overridable inspector requests. */
  SU_TRYCATCH(
    suscan_inspector_request_manager_commit_overridable(&self->insp_reqmgr),
    return SU_FALSE);

  /* Parse pending overridable seek requests */
  SU_TRYCATCH(
    suscan_local_analyzer_parse_seek_overridable(self),
    return SU_FALSE);

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_local_analyzer_on_psd(
    void *userdata,
    const SUFLOAT *psd,
    unsigned int size)
{
  suscan_local_analyzer_t *self = (suscan_local_analyzer_t *) userdata;

  SU_TRYCATCH(
      suscan_analyzer_send_psd_from_smoothpsd(
        self->parent, 
        self->smooth_psd,
        suscan_source_has_looped(self->source)),
      return SU_FALSE);

  return SU_TRUE;
}

SUBOOL
suscan_local_analyzer_init_channel_worker(suscan_local_analyzer_t *self)
{
  struct sigutils_smoothpsd_params sp_params =
      sigutils_smoothpsd_params_INITIALIZER;
  /* Create smooth PSD */
  sp_params.fft_size     = self->parent->params.detector_params.window_size;
  sp_params.samp_rate    = self->effective_samp_rate;
  sp_params.refresh_rate = 1. / self->interval_psd;

  self->sp_params = sp_params;

  SU_TRYCATCH(
      self->smooth_psd = su_smoothpsd_new(
          &sp_params,
          suscan_local_analyzer_on_psd,
          self),
      return SU_FALSE);

  return SU_TRUE;
}

SUBOOL
suscan_source_channel_wk_cb(
    struct suscan_mq *mq_out,
    void *wk_private,
    void *cb_private)
{
  suscan_local_analyzer_t *self = (suscan_local_analyzer_t *) wk_private;
  SUSDIFF got;
  SUSCOUNT read_size;
  SUBOOL mutex_acquired = SU_FALSE;
  SUBOOL restart = SU_FALSE;
  SUFLOAT seconds;

  SU_TRYCATCH(suscan_local_analyzer_lock_loop(self), goto done);
  mutex_acquired = SU_TRUE;

  /* With non-real time sources, use throttle to control CPU usage */
  if (suscan_local_analyzer_is_real_time_ex(self)) {
    read_size = self->read_size;
  } else {
    SU_TRYCATCH(
        pthread_mutex_lock(&self->throttle_mutex) != -1,
        goto done);
    read_size = suscan_throttle_get_portion(
        &self->throttle,
        self->read_size);
    SU_TRYCATCH(
        pthread_mutex_unlock(&self->throttle_mutex) != -1,
        goto done);
  }

  SU_TRYCATCH(suscan_local_analyzer_parse_overridable(self), goto done);

  /* Ready to read */
  suscan_local_analyzer_read_start(self);

  if ((got = suscan_source_read(
      self->source,
      self->read_buf,
      read_size)) > 0) {
    suscan_local_analyzer_process_start(self);

    if (self->iq_rev)
      suscan_analyzer_do_iq_rev(self->read_buf, got);

    if (!suscan_local_analyzer_is_real_time_ex(self)) {
      SU_TRYCATCH(
          pthread_mutex_lock(&self->throttle_mutex) != -1,
          goto done);
      suscan_throttle_advance(&self->throttle, got);
      SU_TRYCATCH(
          pthread_mutex_unlock(&self->throttle_mutex) != -1,
          goto done);
    }

    SU_TRYCATCH(
        suscan_local_analyzer_feed_baseband_filters(
            self,
            self->read_buf,
            got),
        goto done);

    SU_TRYCATCH(
        su_smoothpsd_feed(self->smooth_psd, self->read_buf, got),
        goto done);

    if (SUSCAN_ANALYZER_FS_MEASURE_INTERVAL > 0) {
      seconds = (self->read_start - self->last_measure) * 1e-9;

      if (seconds >= SUSCAN_ANALYZER_FS_MEASURE_INTERVAL) {
        self->measured_samp_rate =
            self->measured_samp_count / seconds;
        self->measured_samp_count = 0;
        self->last_measure = self->read_start;
#ifdef SUSCAN_DEBUG_THROTTLE
        printf("Read rate: %g\n", self->measured_samp_rate);
#endif /* SUSCAN_DEBUG_THROTTLE */
      }

      self->measured_samp_count += got;
    }

    /* Feed inspectors! */
    SU_TRYCATCH(
        suscan_local_analyzer_feed_inspectors(self, self->read_buf, got),
        goto done);

  } else {
    self->parent->eos = SU_TRUE; /* TODO: Use force_eos? */
    self->cpu_usage = 0;

    switch (got) {
      case SU_BLOCK_PORT_READ_END_OF_STREAM:
        suscan_analyzer_send_status(
            self->parent,
            SUSCAN_ANALYZER_MESSAGE_TYPE_EOS,
            got,
            "End of stream reached");
        break;

      case SU_BLOCK_PORT_READ_ERROR_NOT_INITIALIZED:
        suscan_analyzer_send_status(
            self->parent,
            SUSCAN_ANALYZER_MESSAGE_TYPE_EOS,
            got,
            "Port not initialized");
        break;

      case SU_BLOCK_PORT_READ_ERROR_ACQUIRE:
        suscan_analyzer_send_status(
            self->parent,
            SUSCAN_ANALYZER_MESSAGE_TYPE_READ_ERROR,
            got,
            "Acquire failed (source I/O error)");
        break;

      case SU_BLOCK_PORT_READ_ERROR_PORT_DESYNC:
        suscan_analyzer_send_status(
            self->parent,
            SUSCAN_ANALYZER_MESSAGE_TYPE_EOS,
            got,
            "Port desync");
        break;

      default:
        suscan_analyzer_send_status(
            self->parent,
            SUSCAN_ANALYZER_MESSAGE_TYPE_EOS,
            got,
            "Unexpected read result %d", got);
    }

    goto done;
  }

  /* Finish processing */
  suscan_local_analyzer_process_end(self);

  restart = !self->parent->halt_requested;

done:
  if (mutex_acquired)
    (void) suscan_local_analyzer_unlock_loop(self);

  return restart;
}

