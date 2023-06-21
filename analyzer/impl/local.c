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

#define SU_LOG_DOMAIN "local-analyzer"

#include "local.h"

#include "mq.h"
#include "msg.h"
#include "realtime.h"
#include <confdb.h>
#include "sgdp4/sgdp4-types.h"
#include <src/suscan.h>
#include <analyzer/source.h>

SUPRIVATE struct suscan_analyzer_interface *g_local_analyzer_interface;

/* Forward declarations */
SUPRIVATE SUBOOL suscan_local_analyzer_is_real_time(const void *ptr);
SUPRIVATE unsigned int suscan_local_analyzer_get_samp_rate(const void *ptr);

SUBOOL
suscan_analyzer_is_local(const suscan_analyzer_t *self)
{
  return self->iface == g_local_analyzer_interface;
}

/************************ Source worker callback *****************************/
SUBOOL
suscan_local_analyzer_lock_loop(suscan_local_analyzer_t *self)
{
  return pthread_mutex_lock(&self->loop_mutex) != -1;
}

void
suscan_local_analyzer_unlock_loop(suscan_local_analyzer_t *self)
{
  (void) pthread_mutex_unlock(&self->loop_mutex);
}

/************************* Baseband filter API *******************************/
SUPRIVATE struct suscan_analyzer_baseband_filter *
suscan_analyzer_baseband_filter_new(
    suscan_analyzer_baseband_filter_func_t func,
    void *privdata)
{
  struct suscan_analyzer_baseband_filter *filter;

  SU_TRYCATCH(
      filter = malloc(sizeof (struct suscan_analyzer_baseband_filter)),
      return NULL);

  filter->func = func;
  filter->privdata = privdata;

  return filter;
}

SUPRIVATE void
suscan_analyzer_baseband_filter_destroy(
    struct suscan_analyzer_baseband_filter *filter)
{
  free(filter);
}

/************************ Local analyzer thread ******************************/
SUPRIVATE void
suscan_local_analyzer_ack_halt(suscan_local_analyzer_t *self)
{
  suscan_mq_write_urgent(
      self->parent->mq_out,
      SUSCAN_WORKER_MSG_TYPE_HALT,
      NULL);
}

SUPRIVATE void
suscan_local_analyzer_wait_for_halt(suscan_local_analyzer_t *self)
{
  uint32_t type;
  void *private;

  for (;;) {
    private = suscan_mq_read(&self->mq_in, &type);
    if (type == SUSCAN_WORKER_MSG_TYPE_HALT) {
      suscan_local_analyzer_ack_halt(self);
      break;
    }

    suscan_analyzer_dispose_message(type, private);
  }
}

SUPRIVATE SUBOOL
suscan_local_analyzer_override_throttle(
    suscan_local_analyzer_t *self,
    SUSCOUNT val)
{
  SUBOOL mutex_acquired = SU_FALSE;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      pthread_mutex_lock(&self->throttle_mutex) != -1,
      goto done);
  mutex_acquired = SU_TRUE;

  suscan_throttle_init(&self->throttle, val);

  self->effective_samp_rate = val;

  /* XXX: Maybe protect this setting */
  self->source_info.effective_samp_rate = self->effective_samp_rate;

  SU_TRYCATCH(
    suscan_local_analyzer_set_inspector_throttle_overridable(
      self,
      val / suscan_local_analyzer_get_samp_rate(self)),
    goto done);
    
  ok = SU_TRUE;

done:

  if (mutex_acquired)
    pthread_mutex_unlock(&self->throttle_mutex);

  return ok;
}

SUPRIVATE SUBOOL
suscan_local_analyzer_reset_throttle(suscan_local_analyzer_t *self)
{
  return suscan_local_analyzer_override_throttle(
      self,
      suscan_local_analyzer_get_samp_rate(self));
}

SUPRIVATE SUBOOL
suscan_local_analyzer_readjust_detector(
    suscan_local_analyzer_t *self,
    struct sigutils_channel_detector_params *params)
{
  su_channel_detector_t *new_detector = NULL;

  su_channel_params_adjust(params);

  if (!su_channel_detector_set_params(self->detector, params)) {
    /* If not possibe, re-create detector object */
    SU_TRYCATCH(
        new_detector = su_channel_detector_new(params),
        return SU_FALSE);

    su_channel_detector_destroy(self->detector);
    self->detector = new_detector;
  }

  return SU_TRUE;
}

/* Locally-defined prototypes. These should not appear in analyzer.h */
SUBOOL suscan_source_wide_wk_cb(
    struct suscan_mq *mq_out,
    void *wk_private,
    void *cb_private);

SUBOOL suscan_source_channel_wk_cb(
    struct suscan_mq *mq_out,
    void *wk_private,
    void *cb_private);

SUBOOL suscan_local_analyzer_init_channel_worker(suscan_local_analyzer_t *self);

SUBOOL
suscan_local_analyzer_notify_params(suscan_local_analyzer_t *self)
{
  struct suscan_analyzer_params *dup = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      dup = calloc(1, sizeof (struct suscan_analyzer_params)),
      goto done);

  *dup = self->parent->params;

  dup->channel_update_int = self->interval_channels;
  dup->psd_update_int     = self->interval_psd;

  SU_TRYCATCH(
      suscan_mq_write(
          self->parent->mq_out,
          SUSCAN_ANALYZER_MESSAGE_TYPE_PARAMS,
          dup),
      goto done);

  dup = NULL;

  ok = SU_TRUE;

done:
  if (dup != NULL)
    free(dup);

  return ok;
}

SUPRIVATE void *
suscan_analyzer_thread(void *data)
{
  suscan_local_analyzer_t *self = (suscan_local_analyzer_t *) data;
  struct sigutils_channel_detector_params new_det_params;
  const struct suscan_analyzer_params *new_params;
  const struct suscan_analyzer_throttle_msg *throttle;
  const struct suscan_analyzer_seek_msg *seek;
  void *private = NULL;
  uint32_t type;
  SUBOOL mutex_acquired = SU_FALSE;
  SUBOOL halt_acked = SU_FALSE;

  switch (self->parent->params.mode) {
    case SUSCAN_ANALYZER_MODE_CHANNEL:
      if (!suscan_worker_push(
          self->source_wk,
            suscan_source_channel_wk_cb,
            self->source)) {
          suscan_analyzer_send_status(
              self->parent,
              SUSCAN_ANALYZER_MESSAGE_TYPE_SOURCE_INIT,
              SUSCAN_ANALYZER_INIT_FAILURE,
              "Failed to push source callback to worker (channel mode)");
          goto done;
        }
      break;

    case SUSCAN_ANALYZER_MODE_WIDE_SPECTRUM:
      if (!suscan_worker_push(
          self->source_wk,
            suscan_source_wide_wk_cb,
            self->source)) {
          suscan_analyzer_send_status(
              self->parent,
              SUSCAN_ANALYZER_MESSAGE_TYPE_SOURCE_INIT,
              SUSCAN_ANALYZER_INIT_FAILURE,
              "Failed to push source callback to worker (wide spectrum mode)");
          goto done;
        }
      break;
  }


  /* Signal initialization success */
  suscan_analyzer_send_status(
      self->parent,
      SUSCAN_ANALYZER_MESSAGE_TYPE_SOURCE_INIT,
      SUSCAN_ANALYZER_INIT_SUCCESS,
      NULL);

  /* Send source info */
  suscan_analyzer_send_source_info(
      self->parent,
      &self->source_info);

  /* Notify source of the new parameters */
  suscan_local_analyzer_notify_params(self);

  /* Pop all messages from queue before reading from the source */
  for (;;) {
    /* First read: blocks */
    private = suscan_mq_read(&self->mq_in, &type);

    do {
      switch (type) {
        case SUSCAN_WORKER_MSG_TYPE_HALT:
          suscan_local_analyzer_ack_halt(self);
          halt_acked = SU_TRUE;
          /* Nothing to dispose, safe to break the loop */
          goto done;

        case SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR:
          /* Baudrate inspector command. Handle separately */
          SU_TRYCATCH(
              suscan_local_analyzer_parse_inspector_msg(self, private),
              goto done);

          /*
           * We don't dispose this message: it has been processed
           * by the baud inspector API and forwarded to the output mq
           */
          private = NULL;

          break;

        case SUSCAN_ANALYZER_MESSAGE_TYPE_SEEK:
          seek = (const struct suscan_analyzer_seek_msg *) private;
          SU_TRYCATCH(
            suscan_local_analyzer_slow_seek(self, &seek->position),
            goto done);
          break;

        /* Forward these messages to output */
        case SUSCAN_ANALYZER_MESSAGE_TYPE_EOS:
        case SUSCAN_ANALYZER_MESSAGE_TYPE_CHANNEL:
          SU_TRYCATCH(
              suscan_mq_write(self->parent->mq_out, type, private),
              goto done);

          /* Not belonging to us anymore */
          private = NULL;

          break;

        case SUSCAN_ANALYZER_MESSAGE_TYPE_THROTTLE:
          throttle = (const struct suscan_analyzer_throttle_msg *) private;
          if (throttle->samp_rate == 0) {
            SU_TRYCATCH(
                suscan_local_analyzer_reset_throttle(self),
                goto done);
            SU_TRYCATCH(
                suscan_local_analyzer_set_psd_samp_rate_overridable(
                    self,
                    self->effective_samp_rate),
                goto done);
          } else {
            SU_TRYCATCH(
                suscan_local_analyzer_set_psd_samp_rate_overridable(
                    self,
                    throttle->samp_rate),
                goto done);
            SU_TRYCATCH(
                suscan_local_analyzer_override_throttle(
                    self,
                    throttle->samp_rate),
                goto done);
          }
          break;

        case SUSCAN_ANALYZER_MESSAGE_TYPE_PARAMS:
          /*
           * Parameter messages affect the source worker, that must get their
           * objects updated. In order to do that, we protect their access
           * through the source's params_mutex
           */

          new_params = (const struct suscan_analyzer_params *) private;

          if (self->parent->params.mode == SUSCAN_ANALYZER_MODE_CHANNEL) {
              SU_TRYCATCH(
                  suscan_local_analyzer_set_analyzer_params_overridable(
                      self,
                      new_params),
                  goto done);
          }  else {
            SU_TRYCATCH(
                pthread_mutex_lock(&self->loop_mutex) != -1,
                goto done);
            mutex_acquired = SU_TRUE;

            /* vvvvvvvvvvvvvvv Source parameters update start vvvvvvvvvvvvv */

            /* Attempt to update detector parameters */
            new_det_params = self->detector->params; /* Not all parameters are allowed */

            new_det_params.window_size = new_params->detector_params.window_size;
            new_det_params.window = new_params->detector_params.window;
            new_det_params.fc = new_params->detector_params.fc;
            su_channel_params_adjust(&new_det_params);

            SU_TRYCATCH(
                suscan_local_analyzer_readjust_detector(self, &new_det_params),
                goto done);

            self->interval_channels = new_params->channel_update_int;

            if (sufcmp(self->interval_psd, new_params->psd_update_int, 1e-6)) {
              self->interval_psd = new_params->psd_update_int;
              self->det_num_psd = 0;
              self->last_psd = suscan_gettime_coarse();
            }

            self->parent->params.detector_params = new_det_params;

            SU_TRYCATCH(suscan_local_analyzer_notify_params(self), goto done);

            /* ^^^^^^^^^^^^^ Source parameters update end ^^^^^^^^^^^^^^^^^  */
            SU_TRYCATCH(
                pthread_mutex_unlock(&self->loop_mutex) != -1,
                goto done);
            mutex_acquired = SU_FALSE;

          }
          break;

        case SUSCAN_ANALYZER_MESSAGE_TYPE_GET_PARAMS:
          SU_TRYCATCH(
              pthread_mutex_lock(&self->loop_mutex) != -1,
              goto done);
          mutex_acquired = SU_TRUE;

          SU_TRYCATCH(suscan_local_analyzer_notify_params(self), goto done);

          SU_TRYCATCH(
              pthread_mutex_unlock(&self->loop_mutex) != -1,
              goto done);
          mutex_acquired = SU_FALSE;
      }

      if (private != NULL) {
        suscan_analyzer_dispose_message(type, private);
        private = NULL;
      }

      /* Next reads: until message queue is empty */
    } while (suscan_mq_poll(&self->mq_in, &type, &private));
  }

done:
  if (mutex_acquired)
    (void) pthread_mutex_unlock(&self->loop_mutex);

  if (private != NULL)
    suscan_analyzer_dispose_message(type, private);

  if (suscan_source_is_capturing(self->source))
    suscan_source_stop_capture(self->source);

  if (!halt_acked)
    suscan_local_analyzer_wait_for_halt(self);

  self->parent->running = SU_FALSE;

  return NULL;
}

SUPRIVATE void
suscan_local_analyzer_init_detector_params(
    suscan_local_analyzer_t *self,
    struct sigutils_channel_detector_params *params)
{
  /* Recover template */
  *params = self->parent->params.detector_params;

  /* Populate members with source information */
  params->mode = SU_CHANNEL_DETECTOR_MODE_SPECTRUM;

  params->samp_rate = suscan_local_analyzer_get_samp_rate(self);

  /* Adjust parameters that depend on sample rate */
  su_channel_params_adjust(params);

#if 0
  /* Make alpha a little bigger, to provide a more dynamic spectrum */
  if (params->alpha <= .05)
    params->alpha *= 20;
#endif
}

/*************************** Analyzer interface *******************************/
SUPRIVATE SUBOOL
suscan_local_analyzer_source_init(
    suscan_local_analyzer_t *self,
    suscan_source_config_t *config)
{
  SU_TRYCATCH(self->source = suscan_source_new(config), goto fail);

  /* For non-realtime sources (i.e. file sources), enable throttling */
  if (!suscan_local_analyzer_is_real_time(self)) {
    /* Create throttle mutex */
      (void) pthread_mutex_init(&self->throttle_mutex, NULL); /* Always succeeds */
      self->throttle_mutex_init = SU_TRUE;

    suscan_throttle_init(
        &self->throttle,
        suscan_local_analyzer_get_samp_rate(self));
  }

  return SU_TRUE;

fail:
  return SU_FALSE;
}

#ifdef DEBUG_ANALYZER_PARAMS
void
suscan_analyzer_params_debug(const struct suscan_analyzer_params *params)
{
  printf("Mode: %d\n", params->mode);
  printf("Detector.samp_rate: %ld\n", params->detector_params.samp_rate);
  printf("Detector.window_size: %ld\n", params->detector_params.window_size);
  printf("Detector FC: %g\n", params->detector_params.fc);
  printf("Detector.softtune: %d\n", params->detector_params.tune);
  printf("Freq range: %lg, %lg\n", params->min_freq, params->max_freq);
}
#endif /* DEBUG_ANALYZER_PARAMS */

SUPRIVATE void suscan_local_analyzer_dtor(void *ptr);

SUPRIVATE SUBOOL
suscan_local_analyzer_source_info_add_gain(
    void *private,
    struct suscan_source_gain_value *gain)
{
  SUBOOL ok = SU_FALSE;
  struct suscan_source_gain_info *ginfo;
  struct suscan_source_info *info =
      (struct suscan_source_info *) private;

  SU_TRYCATCH(ginfo = suscan_source_gain_info_new(gain), goto fail);

  SU_TRYCATCH(PTR_LIST_APPEND_CHECK(info->gain, ginfo) != -1, goto fail);

  ginfo = NULL;

  ok = SU_TRUE;

fail:
  if (ginfo != NULL)
    suscan_source_gain_info_destroy(ginfo);

  return ok;
}

SUPRIVATE void
suscan_local_analyzer_get_freq_limits(
    const suscan_local_analyzer_t *self,
    SUFREQ *min,
    SUFREQ *max)
{
  SUFREQ fdiff = 
      suscan_source_get_base_samp_rate(self->source) 
    - suscan_source_get_samp_rate(self->source);
  SUFREQ f0;

  const suscan_source_config_t *config = suscan_source_get_config(self->source);
  const suscan_source_device_t *dev = suscan_source_config_get_device(config);

  if (suscan_source_config_get_type(config) == SUSCAN_SOURCE_TYPE_SDR) {
    *min = suscan_source_device_get_min_freq(dev);
    *max = suscan_source_device_get_max_freq(dev);
  } else {
    f0   = suscan_source_config_get_freq(config);
    *min = f0 - fdiff / 2;
    *max = f0 + fdiff / 2;
  }
}

SUPRIVATE SUBOOL
suscan_local_analyzer_populate_source_info(suscan_local_analyzer_t *self)
{
  struct suscan_source_info *info = &self->source_info;
  const suscan_source_device_t *dev = NULL;
  struct suscan_source_device_info dev_info =
      suscan_source_device_info_INITIALIZER;
  const suscan_source_config_t *config = suscan_source_get_config(self->source);
  const char *ant = suscan_source_config_get_antenna(config);
  unsigned int i;
  char *dup = NULL;
  SUBOOL ok = SU_FALSE;

  info->permissions         =  suscan_local_analyzer_is_real_time(self)
    ? SUSCAN_ANALYZER_ALL_SDR_PERMISSIONS
    : SUSCAN_ANALYZER_ALL_FILE_PERMISSIONS;
  
  info->source_samp_rate    = suscan_source_get_samp_rate(self->source);
  info->effective_samp_rate = self->effective_samp_rate;
  info->measured_samp_rate  = self->measured_samp_rate;
  
  info->frequency           = suscan_source_get_freq(self->source);

  suscan_local_analyzer_get_freq_limits(
      self,
      &info->freq_min,
      &info->freq_max);

  info->lnb       = suscan_source_config_get_lnb_freq(config);
  info->bandwidth = suscan_source_config_get_bandwidth(config);
  info->dc_remove = suscan_source_config_get_dc_remove(config);
  info->ppm       = suscan_source_config_get_ppm(config);
  info->iq_reverse = self->iq_rev;
  info->agc = SU_FALSE;

  info->have_qth = suscan_get_qth(&info->qth);

  suscan_source_get_time(self->source, &info->source_time);

  info->seekable = !suscan_local_analyzer_is_real_time(self);
  if (info->seekable) {
    suscan_source_get_start_time(self->source, &info->source_start);
    suscan_source_get_end_time(self->source, &info->source_end);
  }

  if (ant != NULL) {
    SU_TRYCATCH(info->antenna = strdup(ant), goto done);
  } else {
    info->antenna = NULL;
  }

  if (suscan_source_config_get_type(config) == SUSCAN_SOURCE_TYPE_SDR) {
    SU_TRYCATCH(
        suscan_source_config_walk_gains_ex(
            config,
            suscan_local_analyzer_source_info_add_gain,
            info),
        goto done);

    dev = suscan_source_config_get_device(config);
    if (suscan_source_device_get_info(dev, 0, &dev_info)) {
      for (i = 0; i < dev_info.antenna_count; ++i) {
        SU_TRYCATCH(dup = strdup(dev_info.antenna_list[i]), goto done);
        SU_TRYCATCH(PTR_LIST_APPEND_CHECK(info->antenna, dup) != -1, goto done);
        dup = NULL;
      }
    }
  }

  ok = SU_TRUE;

done:
  if (dup != NULL)
    free(dup);

  suscan_source_device_info_finalize(&dev_info);

  return ok;
}

SUPRIVATE void
suscan_local_analyzer_bbfilt_dtor(void *obj, void *userdata)
{
  suscan_analyzer_baseband_filter_destroy(obj);
}

void *
suscan_local_analyzer_ctor(suscan_analyzer_t *parent, va_list ap)
{
  suscan_local_analyzer_t *new = NULL;
  const struct suscan_source_info *source_info = NULL;
  struct sigutils_specttuner_params st_params =
      sigutils_specttuner_params_INITIALIZER;
  struct sigutils_channel_detector_params det_params;
  suscan_source_config_t *config;
  pthread_mutexattr_t attr;
  static SUBOOL insp_server_init = SU_FALSE;

  SU_TRYCATCH(new = calloc(1, sizeof(suscan_local_analyzer_t)), goto fail);

  config = va_arg(ap, suscan_source_config_t *);

  new->parent = parent;

  /* Create input message queue */
  if (!suscan_mq_init(&new->mq_in)) {
    SU_ERROR("Cannot allocate input MQ\n");
    goto fail;
  }

  /* Initialize source */
  if (!suscan_local_analyzer_source_init(new, config)) {
    SU_ERROR("Failed to initialize source\n");
    goto fail;
  }

  /* Periodic updates */
  new->interval_channels = parent->params.channel_update_int;
  new->interval_psd      = parent->params.psd_update_int;
  new->last_psd          = suscan_gettime_coarse();
  new->last_channels     = suscan_gettime_coarse();

  /* Create channel detector */
  (void) pthread_mutex_init(&new->loop_mutex, NULL); /* Always succeeds */
  new->loop_init = SU_TRUE;

  /* Create source worker */
  if ((new->source_wk = suscan_worker_new_ex(
    "source-worker", 
    &new->mq_in, 
    new))
      == NULL) {
    SU_ERROR("Cannot create source worker thread\n");
    goto fail;
  }

  /* Create slow worker */
  if ((new->slow_wk = suscan_worker_new_ex(
    "slow-worker", 
    &new->mq_in, 
    new))
      == NULL) {
    SU_ERROR("Cannot create slow worker thread\n");
    goto fail;
  }

  /* Initialize gain request mutex */
  SU_TRYCATCH(pthread_mutex_init(&new->hotconf_mutex, NULL) == 0, goto fail);
  new->gain_req_mutex_init = SU_TRUE;

  /* Create spectral tuner, with matching read size */
  st_params.window_size = parent->params.detector_params.window_size;
  SU_TRYCATCH(new->stuner = su_specttuner_new(&st_params), goto fail);

  /* Initialize baseband filters */
  SU_MAKE_FAIL(new->bbfilt_tree, rbtree);
  rbtree_set_dtor(new->bbfilt_tree, suscan_local_analyzer_bbfilt_dtor, NULL);

  /* 
   * Spectral tuner mutex should be recursive in order to enable
   * closure of channels inside the data callback 
   */
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  SU_TRYCATCH(pthread_mutex_init(&new->stuner_mutex, &attr) == 0, goto fail);
  new->stuner_init = SU_TRUE;

  /* Initialization of the inspector handling API */
  if (suscan_inspector_factory_class_lookup("local-analyzer") == NULL)
    SU_TRYCATCH(
      suscan_local_analyzer_register_factory(),
      goto fail);

  SU_TRYCATCH(
    new->insp_factory = suscan_inspector_factory_new("local-analyzer", new),
    goto fail);
  
  SU_TRYCATCH(
    suscan_inspector_request_manager_init(&new->insp_reqmgr),
    goto fail);

  if (!insp_server_init) {
    SU_TRYCATCH(suscan_insp_server_init(), goto fail);
    insp_server_init = SU_TRUE;
  }

  /* Initialize rbtree */
  SU_TRYCATCH(new->insp_hash = rbtree_new(), goto fail);
  SU_TRYCATCH(pthread_mutex_init(&new->insp_mutex, NULL) == 0, goto fail);
  new->insp_init = SU_TRUE;
  
  SU_TRYCATCH(suscan_source_start_capture(new->source), goto fail);

  new->effective_samp_rate = suscan_local_analyzer_get_samp_rate(new);

  /* Allocate read buffer */
  new->read_size =
      new->effective_samp_rate <= SUSCAN_ANALYZER_SLOW_RATE
      ? SUSCAN_ANALYZER_SLOW_READ_SIZE
      : SUSCAN_ANALYZER_FAST_READ_SIZE;

  source_info = suscan_source_get_info(new->source);
  if (new->read_size < source_info->mtu)
    new->read_size = source_info->mtu;

  if ((new->read_buf = malloc(
      new->read_size * sizeof(SUCOMPLEX))) == NULL) {
    SU_ERROR("Failed to allocate read buffer\n");
    goto fail;
  }

  /* In wide spectrum mode, additional tests are required */
  if (parent->params.mode == SUSCAN_ANALYZER_MODE_WIDE_SPECTRUM) {
    det_params = parent->params.detector_params;
    suscan_local_analyzer_init_detector_params(new, &det_params);
    SU_TRYCATCH(
        new->detector = su_channel_detector_new(&det_params),
        goto fail);

    /*
     * In case the source rejected our initial sample rate configuration, we
     * update the detector accordingly.
     *
     * We do this here and not in the header thread because, although this
     * can be slower, we ensure this way we can provide an accurate value of the
     * sample rate right after the analyzer object is created.
     */
    if (new->effective_samp_rate != new->detector->params.samp_rate) {
      det_params = new->detector->params;
      det_params.samp_rate = new->effective_samp_rate;
      SU_TRYCATCH(
          suscan_local_analyzer_readjust_detector(new, &det_params),
          goto fail);
    }

    SU_TRYCATCH(
        parent->params.max_freq - parent->params.min_freq >=
        suscan_local_analyzer_get_samp_rate(new),
        goto fail);
    new->current_sweep_params.fft_min_samples =
            SUSCAN_ANALYZER_MIN_POST_HOP_FFTS * det_params.window_size;
    new->current_sweep_params.max_freq = parent->params.max_freq;
    new->current_sweep_params.min_freq = parent->params.min_freq;
  } else {
    SU_TRYCATCH(
        suscan_local_analyzer_init_channel_worker(new),
        goto fail);
  }

  /* Populate source info. */
  SU_TRYCATCH(suscan_local_analyzer_populate_source_info(new), goto fail);

  /* Get ahead of the initialization. analyzer_thread
     need this to be properly initialized. */
  new->parent->impl = new;

  if (pthread_create(
      &new->thread,
      NULL,
      suscan_analyzer_thread,
      new) == -1) {
    SU_ERROR("Cannot create main thread\n");
    goto fail;
  }

  new->thread_running = SU_TRUE;

return new;

fail:
  if (new != NULL)
    suscan_local_analyzer_dtor(new);

  return NULL;
}

SUPRIVATE void
suscan_local_analyzer_dtor(void *ptr)
{
  suscan_local_analyzer_t *self = (suscan_local_analyzer_t *) ptr;

  /* Prevent source from entering in timeout loops */
  if (self->source != NULL)
    suscan_source_force_eos(self->source);

  if (self->thread_running) {
    /* TODO: add a timeout here too */
    if (pthread_join(self->thread, NULL) == -1) {
      SU_ERROR("Thread failed to join, memory leak ahead\n");
      return;
    }
  }

  /* TODO: Concurrently-force EOS in source object */
  if (self->source_wk != NULL)
    if (!suscan_analyzer_halt_worker(self->source_wk)) {
      SU_ERROR("Source worker destruction failed, memory leak ahead\n");
      return;
    }

  if (self->slow_wk != NULL)
    if (!suscan_analyzer_halt_worker(self->slow_wk)) {
      SU_ERROR("Slow worker destruction failed, memory leak ahead\n");
      return;
    }

  /* Destroy global inspector table */
  suscan_local_analyzer_destroy_global_handles_unsafe(self);

  /* Free channel detector */
  if (self->detector != NULL)
    su_channel_detector_destroy(self->detector);

  if (self->smooth_psd != NULL)
    su_smoothpsd_destroy(self->smooth_psd);

  if (self->loop_init)
    pthread_mutex_destroy(&self->loop_mutex);

  /* Deinitialize request manager */
  suscan_inspector_request_manager_finalize(&self->insp_reqmgr);

  /* Delete inspector hash tree */
  if (self->insp_hash)
    rbtree_destroy(self->insp_hash);

  /* Destroy inspectors */
  if (self->insp_factory != NULL)
    suscan_inspector_factory_destroy(self->insp_factory);

  /* 
   * Free spectral tuner. It must be done after destroying
   * the factory, as the local factory implementation holds
   * pointers to specttuner channels.
   */
  if (self->stuner_init)
    pthread_mutex_destroy(&self->stuner_mutex);
  
  if (self->stuner != NULL)
    su_specttuner_destroy(self->stuner);
  
  /* Free read buffer */
  if (self->read_buf != NULL)
    free(self->read_buf);

  /* Delete source information */
  if (self->source != NULL)
    suscan_source_destroy(self->source);

  /* Release slow worker data */
  suscan_local_analyzer_destroy_slow_worker_data(self);

  if (self->throttle_mutex_init)
    pthread_mutex_destroy(&self->throttle_mutex);

  /* Delete all baseband filters (triggered by the dtor) */
  if (self->bbfilt_tree != NULL)
    rbtree_destroy(self->bbfilt_tree);

  /* Finalize source info */
  suscan_source_info_finalize(&self->source_info);

  /* Consume any pending messages */
  suscan_analyzer_consume_mq(&self->mq_in);

  /* Finalize queue */
  suscan_mq_finalize(&self->mq_in);

  free(self);
}

/* Source-related methods */
SUPRIVATE SUBOOL
suscan_local_analyzer_set_frequency(void *ptr, SUFREQ freq, SUFREQ lnb)
{
  suscan_local_analyzer_t *self = (suscan_local_analyzer_t *) ptr;

  return suscan_local_analyzer_slow_set_freq(self, freq, lnb);
}

SUPRIVATE SUBOOL
suscan_local_analyzer_seek(void *ptr, const struct timeval *pos)
{
  suscan_local_analyzer_t *self = (suscan_local_analyzer_t *) ptr;

  return suscan_local_analyzer_slow_seek(self, pos);
}


SUPRIVATE SUBOOL
suscan_local_analyzer_set_gain(void *ptr, const char *name, SUFLOAT value)
{
  suscan_local_analyzer_t *self = (suscan_local_analyzer_t *) ptr;

  return suscan_local_analyzer_slow_set_gain(self, name, value);
}

SUPRIVATE SUBOOL
suscan_local_analyzer_set_antenna(void *ptr, const char *name)
{
  suscan_local_analyzer_t *self = (suscan_local_analyzer_t *) ptr;

  return suscan_local_analyzer_slow_set_antenna(self, name);
}

SUPRIVATE SUBOOL
suscan_local_analyzer_set_bandwidth(void *ptr, SUFLOAT value)
{
  suscan_local_analyzer_t *self = (suscan_local_analyzer_t *) ptr;

  return suscan_local_analyzer_slow_set_bw(self, value);
}

SUPRIVATE SUBOOL
suscan_local_analyzer_set_ppm(void *ptr, SUFLOAT ppm)
{
  suscan_local_analyzer_t *self = (suscan_local_analyzer_t *) ptr;

  return suscan_local_analyzer_slow_set_ppm(self, ppm);
}

SUPRIVATE SUBOOL
suscan_local_analyzer_set_dc_remove(void *ptr, SUBOOL value)
{
  suscan_local_analyzer_t *self = (suscan_local_analyzer_t *) ptr;

  return suscan_local_analyzer_slow_set_dc_remove(self, value);
}

SUPRIVATE SUBOOL
suscan_local_analyzer_set_iq_reverse(void *ptr, SUBOOL value)
{
  suscan_local_analyzer_t *self = (suscan_local_analyzer_t *) ptr;

  if (self->iq_rev != value) {
    self->iq_rev = value;
    self->source_info.iq_reverse = self->iq_rev;
    return suscan_analyzer_send_source_info(self->parent, &self->source_info);
  }
  
  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_local_analyzer_set_agc(void *ptr, SUBOOL value)
{
  suscan_local_analyzer_t *self = (suscan_local_analyzer_t *) ptr;

  return suscan_local_analyzer_slow_set_agc(self, value);
}

SUPRIVATE SUBOOL
suscan_local_analyzer_force_eos(void *ptr)
{
  suscan_local_analyzer_t *self = (suscan_local_analyzer_t *) ptr;

  if (self->source == NULL)
    return SU_FALSE;

  suscan_source_force_eos(self->source);

  return SU_TRUE;
}

SUBOOL
suscan_local_analyzer_is_real_time_ex(const suscan_local_analyzer_t *self)
{
  return suscan_source_get_type(self->source) == SUSCAN_SOURCE_TYPE_SDR;
}

SUPRIVATE SUBOOL
suscan_local_analyzer_is_real_time(const void *ptr)
{
  const suscan_local_analyzer_t *self = (const suscan_local_analyzer_t *) ptr;

  return suscan_local_analyzer_is_real_time_ex(self);
}

SUPRIVATE unsigned int
suscan_local_analyzer_get_samp_rate(const void *ptr)
{
  const suscan_local_analyzer_t *self = (const suscan_local_analyzer_t *) ptr;

  return suscan_source_get_samp_rate(self->source);
}

SUPRIVATE SUFLOAT
suscan_local_analyzer_get_measured_samp_rate(const void *ptr)
{
  const suscan_local_analyzer_t *self = (const suscan_local_analyzer_t *) ptr;

  return self->measured_samp_rate;
}

SUPRIVATE struct suscan_source_info *
suscan_local_analyzer_get_source_info_pointer(const void *ptr)
{
  const suscan_local_analyzer_t *self = (const suscan_local_analyzer_t *) ptr;

  return (struct suscan_source_info *) &self->source_info;
}

SUPRIVATE void
suscan_local_analyzer_get_source_time(const void *ptr, struct timeval *tv)
{
  const suscan_local_analyzer_t *self = (const suscan_local_analyzer_t *) ptr;

  suscan_source_get_time(self->source, tv);
}

SUPRIVATE SUBOOL
suscan_local_analyzer_commit_source_info(void *ptr)
{
  return SU_TRUE;
}

/* Worker specific methods */
SUPRIVATE SUBOOL
suscan_local_analyzer_set_sweep_strategy(
    void *ptr,
    enum suscan_analyzer_sweep_strategy strategy)
{
  suscan_local_analyzer_t *self = (suscan_local_analyzer_t *) ptr;

  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      self->parent->params.mode == SUSCAN_ANALYZER_MODE_WIDE_SPECTRUM,
      goto done);


  self->pending_sweep_params =
        self->sweep_params_requested
        ? self->pending_sweep_params
        : self->current_sweep_params;
  self->pending_sweep_params.strategy = strategy;
  self->sweep_params_requested = SU_TRUE;

  ok = SU_TRUE;

done:
  return ok;
}

SUPRIVATE SUBOOL
suscan_local_analyzer_set_spectrum_partitioning(
    void *ptr,
    enum suscan_analyzer_spectrum_partitioning partitioning)
{
  suscan_local_analyzer_t *self = (suscan_local_analyzer_t *) ptr;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      self->parent->params.mode == SUSCAN_ANALYZER_MODE_WIDE_SPECTRUM,
      goto done);

  self->pending_sweep_params =
      self->sweep_params_requested
      ? self->pending_sweep_params
      : self->current_sweep_params;
  self->pending_sweep_params.partitioning = partitioning;
  self->sweep_params_requested = SU_TRUE;

  ok = SU_TRUE;

done:
  return ok;
}

SUPRIVATE SUBOOL
suscan_local_analyzer_set_hop_range(void *ptr, SUFREQ min, SUFREQ max)
{
  suscan_local_analyzer_t *self = (suscan_local_analyzer_t *) ptr;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      self->parent->params.mode == SUSCAN_ANALYZER_MODE_WIDE_SPECTRUM,
      goto done);

  SU_TRYCATCH(max - min >= 0, goto done);

  self->pending_sweep_params =
      self->sweep_params_requested
        ? self->pending_sweep_params
        : self->current_sweep_params;
  self->pending_sweep_params.min_freq = min;
  self->pending_sweep_params.max_freq = max;
  self->sweep_params_requested = SU_TRUE;

  ok = SU_TRUE;

done:
  return ok;
}

SUPRIVATE SUBOOL
suscan_local_analyzer_set_buffering_size(
    void *ptr,
    SUSCOUNT size)
{
  suscan_local_analyzer_t *self = (suscan_local_analyzer_t *) ptr;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      self->parent->params.mode == SUSCAN_ANALYZER_MODE_WIDE_SPECTRUM,
      goto done);

  self->pending_sweep_params =
        self->sweep_params_requested
        ? self->pending_sweep_params
        : self->current_sweep_params;
  self->pending_sweep_params.fft_min_samples = size;
  self->sweep_params_requested = SU_TRUE;

  ok = SU_TRUE;

done:
  return ok;
}

SUPRIVATE SUBOOL
suscan_local_analyzer_register_baseband_filter(
    void *ptr,
    suscan_analyzer_baseband_filter_func_t func,
    void *privdata,
    int64_t prio)
{
  struct suscan_analyzer_baseband_filter *new = NULL;
  suscan_local_analyzer_t *self = (suscan_local_analyzer_t *) ptr;
  SUBOOL automatic_priority = prio == SUSCAN_ANALYZER_BBFILT_PRIO_DEFAULT;
  
  SU_TRYCATCH(
      self->parent->params.mode == SUSCAN_ANALYZER_MODE_CHANNEL,
      goto fail);

  SU_TRYCATCH(
      new = suscan_analyzer_baseband_filter_new(func, privdata),
      goto fail);

  new->func = func;
  new->privdata = privdata;

  if (automatic_priority) {
    prio = 0;
    while (rbtree_search(self->bbfilt_tree, prio, RB_EXACT) != NULL)
      ++prio;
  }

  if (rbtree_search(self->bbfilt_tree, prio, RB_EXACT) != NULL) {
    SU_ERROR(
      "A baseband filter with priority %lld has already been installed\n",
      prio);
    goto fail;
  }

  SU_TRYC_FAIL(rbtree_insert(self->bbfilt_tree, prio, new));

  return SU_TRUE;

fail:
  if (new != NULL)
    suscan_analyzer_baseband_filter_destroy(new);

  return SU_FALSE;
}


/* Fast methods */
SUPRIVATE SUBOOL
suscan_local_analyzer_set_inspector_frequency(
    void *ptr,
    SUHANDLE handle,
    SUFREQ freq)
{
  suscan_local_analyzer_t *self = (suscan_local_analyzer_t *) ptr;

  return suscan_local_analyzer_set_inspector_freq_slow(
      self,
      handle,
      freq);
}

SUPRIVATE SUBOOL
suscan_local_analyzer_set_inspector_bandwidth(
    void *ptr,
    SUHANDLE handle,
    SUFLOAT bw)
{
  suscan_local_analyzer_t *self = (suscan_local_analyzer_t *) ptr;

  return suscan_local_analyzer_set_inspector_bandwidth_slow(
      self,
      handle,
      bw);
}

SUPRIVATE SUBOOL
suscan_local_analyzer_write(void *ptr, uint32_t type, void *priv)
{
  suscan_local_analyzer_t *self = (suscan_local_analyzer_t *) ptr;

  return suscan_mq_write(&self->mq_in, type, priv);
}

SUPRIVATE void
suscan_local_analyzer_req_halt(void *ptr)
{
  suscan_local_analyzer_t *self = (suscan_local_analyzer_t *) ptr;

  suscan_mq_write_urgent(
        &self->mq_in,
        SUSCAN_WORKER_MSG_TYPE_HALT,
        NULL);
}

#define SET_CALLBACK(name) iface.name = JOIN(suscan_local_analyzer_, name)

const struct suscan_analyzer_interface *
suscan_local_analyzer_get_interface(void)
{
  static struct suscan_analyzer_interface iface;

  if (g_local_analyzer_interface == NULL) {
    iface.name          = "local";

    SET_CALLBACK(ctor);
    SET_CALLBACK(dtor);
    SET_CALLBACK(set_frequency);
    SET_CALLBACK(set_gain);
    SET_CALLBACK(set_antenna);
    SET_CALLBACK(set_bandwidth);
    SET_CALLBACK(set_ppm);
    SET_CALLBACK(set_dc_remove);
    SET_CALLBACK(set_iq_reverse);
    SET_CALLBACK(set_agc);
    SET_CALLBACK(force_eos);
    SET_CALLBACK(is_real_time);
    SET_CALLBACK(get_samp_rate);
    SET_CALLBACK(get_source_time);
    SET_CALLBACK(seek);
    SET_CALLBACK(register_baseband_filter);
    SET_CALLBACK(get_measured_samp_rate);
    SET_CALLBACK(get_source_info_pointer);
    SET_CALLBACK(commit_source_info);
    SET_CALLBACK(set_sweep_strategy);
    SET_CALLBACK(set_spectrum_partitioning);
    SET_CALLBACK(set_hop_range);
    SET_CALLBACK(set_buffering_size);
    SET_CALLBACK(set_inspector_frequency);
    SET_CALLBACK(set_inspector_bandwidth);
    SET_CALLBACK(write);
    SET_CALLBACK(req_halt);

    g_local_analyzer_interface = &iface;
  }

  return g_local_analyzer_interface;
}

#undef SET_CALLBACK
