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

SUPRIVATE struct suscan_analyzer_interface *g_local_analyzer_interface;

/* Forward declarations */
SUPRIVATE SUBOOL suscan_local_analyzer_is_real_time(const void *ptr);
SUPRIVATE unsigned int suscan_local_analyzer_get_samp_rate(const void *ptr);

SUBOOL
suscan_analyzer_is_local(const suscan_analyzer_t *self)
{
  return self->iface == g_local_analyzer_interface;
}

/************************ Overridable request API ****************************/
void
suscan_inspector_overridable_request_destroy(
    struct suscan_inspector_overridable_request *self)
{
  free(self);
}

SUPRIVATE struct suscan_inspector_overridable_request *
suscan_inspector_overridable_request_new(suscan_inspector_t *insp)
{
  struct suscan_inspector_overridable_request *new = NULL;

  SU_TRYCATCH(
      new = calloc(1, sizeof(struct suscan_inspector_overridable_request)),
      goto done);

  new->insp = insp;

  return new;

done:
  if (new != NULL)
    suscan_inspector_overridable_request_destroy(new);

  return NULL;
}

struct suscan_inspector_overridable_request *
suscan_local_analyzer_acquire_overridable(
    suscan_local_analyzer_t *self,
    SUHANDLE handle)
{
  struct suscan_inspector_overridable_request *req = NULL;
  struct suscan_inspector_overridable_request *own_req = NULL;
  suscan_inspector_t *insp = NULL;

  /*
   * TODO: Maybe acquire scheduler mutex?
   */
  SU_TRYCATCH(suscan_local_analyzer_lock_inspector_list(self), goto done);
  SU_TRYCATCH(
      insp = suscan_local_analyzer_get_inspector(self, handle),
      goto done);
  SU_TRYCATCH(insp->state == SUSCAN_ASYNC_STATE_RUNNING, goto done);

  if ((req = suscan_inspector_get_userdata(insp)) == NULL) {
    /* No userdata. Release mutex, create object and lock again. */
    suscan_local_analyzer_unlock_inspector_list(self);

    SU_TRYCATCH(
        own_req = suscan_inspector_overridable_request_new(insp),
        goto done);

    SU_TRYCATCH(suscan_local_analyzer_lock_inspector_list(self), goto done);

    /* Many things may have happened here */
    SU_TRYCATCH(handle >= 0 && handle < self->inspector_count, goto done);
    SU_TRYCATCH(
        insp = suscan_local_analyzer_get_inspector(self, handle),
        goto done);
    SU_TRYCATCH(insp->state == SUSCAN_ASYNC_STATE_RUNNING, goto done);

    req = own_req;
    own_req = NULL;

    req->next = self->insp_overridable;
    self->insp_overridable = req;

    suscan_inspector_set_userdata(insp, req);
  }

done:
  if (own_req != NULL)
    suscan_inspector_overridable_request_destroy(own_req);

  return req;
}

SUBOOL
suscan_local_analyzer_release_overridable(
    suscan_local_analyzer_t *self,
    struct suscan_inspector_overridable_request *rq)
{
  suscan_local_analyzer_unlock_inspector_list(self);

  return SU_TRUE;
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

SUBOOL
suscan_local_analyzer_lock_inspector_list(suscan_local_analyzer_t *self)
{
  return pthread_mutex_lock(&self->inspector_list_mutex) != -1;
}

void
suscan_local_analyzer_unlock_inspector_list(suscan_local_analyzer_t *self)
{
  (void) pthread_mutex_unlock(&self->inspector_list_mutex);
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

SUBOOL
suscan_analyzer_register_baseband_filter(
    suscan_analyzer_t *self,
    suscan_analyzer_baseband_filter_func_t func,
    void *privdata)
{
  struct suscan_analyzer_baseband_filter *new = NULL;

  SU_TRYCATCH(
      self->params.mode == SUSCAN_ANALYZER_MODE_CHANNEL,
      goto fail);

  SU_TRYCATCH(
      new = suscan_analyzer_baseband_filter_new(func, privdata),
      goto fail);

  new->func = func;
  new->privdata = privdata;

  SU_TRYCATCH(
      PTR_LIST_APPEND_CHECK(SULIMPL(self)->bbfilt, new) != -1,
      goto fail);

  return SU_TRUE;

fail:
  if (new != NULL)
    suscan_analyzer_baseband_filter_destroy(new);

  return SU_FALSE;
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

SUPRIVATE void *
suscan_analyzer_thread(void *data)
{
  suscan_local_analyzer_t *self = (suscan_local_analyzer_t *) data;
  struct sigutils_channel_detector_params new_det_params;
  const struct suscan_analyzer_params *new_params;
  const struct suscan_analyzer_throttle_msg *throttle;
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

            /* ^^^^^^^^^^^^^ Source parameters update end ^^^^^^^^^^^^^^^^^  */

            SU_TRYCATCH(
                pthread_mutex_unlock(&self->loop_mutex) != -1,
                goto done);
            mutex_acquired = SU_FALSE;

          }

          break;
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

/********************** Suscan analyzer public API ***************************/
void
suscan_local_analyzer_source_barrier(suscan_local_analyzer_t *self)
{
  pthread_barrier_wait(&self->barrier);
}

void
suscan_local_analyzer_enter_sched(suscan_local_analyzer_t *self)
{
  pthread_mutex_lock(&self->sched_lock);
}

void
suscan_local_analyzer_leave_sched(suscan_local_analyzer_t *self)
{
  pthread_mutex_unlock(&self->sched_lock);
}

su_specttuner_channel_t *
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
  su_specttuner_channel_t *channel = NULL;
  struct sigutils_specttuner_channel_params params =
      sigutils_specttuner_channel_params_INITIALIZER;

  params.f0 =
      SU_NORM2ANG_FREQ(
          SU_ABS2NORM_FREQ(
              suscan_local_analyzer_get_samp_rate(self),
              chan_info->fc - chan_info->ft));

  if (params.f0 < 0)
    params.f0 += 2 * PI;

  params.bw =
      SU_NORM2ANG_FREQ(
          SU_ABS2NORM_FREQ(
              suscan_local_analyzer_get_samp_rate(self),
              chan_info->f_hi - chan_info->f_lo));
  params.guard = SUSCAN_ANALYZER_GUARD_BAND_PROPORTION;
  params.on_data = on_data;
  params.privdata = privdata;
  params.precise = precise;

  suscan_local_analyzer_enter_sched(self);

  SU_TRYCATCH(
      channel = su_specttuner_open_channel(self->stuner, &params),
      goto done);

done:
  suscan_local_analyzer_leave_sched(self);

  return channel;
}

su_specttuner_channel_t *
suscan_local_analyzer_open_channel(
    suscan_local_analyzer_t *analyzer,
    const struct sigutils_channel *chan_info,
    SUBOOL (*on_data) (
        const struct sigutils_specttuner_channel *channel,
        void *privdata,
        const SUCOMPLEX *data, /* This pointer remains valid until the next call to feed */
        SUSCOUNT size),
        void *privdata)
{
  return suscan_local_analyzer_open_channel_ex(
      analyzer,
      chan_info,
      SU_FALSE,
      on_data,
      privdata);
}

SUBOOL
suscan_local_analyzer_close_channel(
    suscan_local_analyzer_t *self,
    su_specttuner_channel_t *channel)
{
  SUBOOL ok;

  suscan_local_analyzer_enter_sched(self);

  ok = su_specttuner_close_channel(self->stuner, channel);

  suscan_local_analyzer_leave_sched(self);

  return ok;
}

/*
 * There is no explicit UNBIND. Unbind happens inside
 * suscan_analyzer_on_channel_data when the inspector state
 * is different from RUNNING.
 */

SUBOOL
suscan_local_analyzer_bind_inspector_to_channel(
    suscan_local_analyzer_t *self,
    su_specttuner_channel_t *channel,
    suscan_inspector_t *insp)
{
  struct suscan_inspector_task_info *task_info = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      task_info = suscan_inspector_task_info_new(insp),
      return SU_FALSE);

  task_info->channel = channel;

  suscan_local_analyzer_enter_sched(self);

  SU_TRYCATCH(
      suscan_inspsched_append_task_info(self->sched, task_info),
      goto done);

  /*
   * Task info registered, binding it to the inspector. Time to bind
   * this task to the channel, so it knows that to do when new data
   * arrives to it.
   */
  channel->params.privdata = task_info;

  /* Now we can say that the inspector is actually running */
  insp->state = SUSCAN_ASYNC_STATE_RUNNING;

  task_info = NULL;

  ok = SU_TRUE;

done:
  suscan_local_analyzer_leave_sched(self);

  if (task_info != NULL)
    suscan_inspector_task_info_destroy(task_info);

  return ok;
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
  struct suscan_analyzer_gain_info *ginfo;
  struct suscan_analyzer_source_info *info =
      (struct suscan_analyzer_source_info *) private;

  SU_TRYCATCH(ginfo = suscan_analyzer_gain_info_new(gain), goto fail);

  SU_TRYCATCH(PTR_LIST_APPEND_CHECK(info->gain, ginfo) != -1, goto fail);

  ginfo = NULL;

  ok = SU_TRUE;

fail:
  if (ginfo != NULL)
    suscan_analyzer_gain_info_destroy(ginfo);

  return ok;
}

SUPRIVATE void
suscan_local_analyzer_get_freq_limits(
    const suscan_local_analyzer_t *self,
    SUFREQ *min,
    SUFREQ *max)
{
  const suscan_source_config_t *config = suscan_source_get_config(self->source);
  const suscan_source_device_t *dev = suscan_source_config_get_device(config);

  if (suscan_source_config_get_type(config) == SUSCAN_SOURCE_TYPE_SDR) {
    *min = suscan_source_device_get_min_freq(dev);
    *max = suscan_source_device_get_max_freq(dev);
  } else {
    *min = SUSCAN_LOCAL_ANALYZER_MIN_RADIO_FREQ;
    *max = SUSCAN_LOCAL_ANALYZER_MAX_RADIO_FREQ;
  }
}

SUPRIVATE SUBOOL
suscan_local_analyzer_populate_source_info(suscan_local_analyzer_t *self)
{
  struct suscan_analyzer_source_info *info = &self->source_info;
  const suscan_source_device_t *dev = NULL;
  struct suscan_source_device_info dev_info =
      suscan_source_device_info_INITIALIZER;
  const suscan_source_config_t *config = suscan_source_get_config(self->source);
  const char *ant = suscan_source_config_get_antenna(config);
  unsigned int i;
  char *dup = NULL;
  SUBOOL ok = SU_FALSE;

  info->source_samp_rate = suscan_source_get_samp_rate(self->source);
  info->effective_samp_rate = self->effective_samp_rate;
  info->measured_samp_rate = self->measured_samp_rate;
  info->frequency = suscan_source_get_freq(self->source);

  suscan_local_analyzer_get_freq_limits(
      self,
      &info->freq_min,
      &info->freq_max);

  info->lnb = suscan_source_config_get_lnb_freq(config);
  info->bandwidth = suscan_source_config_get_bandwidth(config);
  info->dc_remove = suscan_source_config_get_dc_remove(config);
  info->ppm       = suscan_source_config_get_ppm(config);
  info->iq_reverse = self->iq_rev;
  info->agc = SU_FALSE;

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

void *
suscan_local_analyzer_ctor(suscan_analyzer_t *parent, va_list ap)
{
  suscan_local_analyzer_t *new = NULL;
  struct sigutils_specttuner_params st_params =
      sigutils_specttuner_params_INITIALIZER;
  struct sigutils_channel_detector_params det_params;
  suscan_source_config_t *config;

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
  if ((new->source_wk = suscan_worker_new(&new->mq_in, new))
      == NULL) {
    SU_ERROR("Cannot create source worker thread\n");
    goto fail;
  }

  /* Create slow worker */
  if ((new->slow_wk = suscan_worker_new(&new->mq_in, new))
      == NULL) {
    SU_ERROR("Cannot create slow worker thread\n");
    goto fail;
  }

  /* Initialize gain request mutex */
  SU_TRYCATCH(pthread_mutex_init(&new->hotconf_mutex, NULL) != -1, goto fail);
  new->gain_req_mutex_init = SU_TRUE;

  /* Create spectral tuner, with matching read size */
  st_params.window_size = parent->params.detector_params.window_size;
  SU_TRYCATCH(new->stuner = su_specttuner_new(&st_params), goto fail);

  /* Create inspector scheduler and barrier */
  SU_TRYCATCH(new->sched = suscan_inspsched_new(new), goto fail);

  /*
   * During barrier initialization, we take the number of scheduler workers
   * plus 1 (the source worker)
   */
  SU_TRYCATCH(
      pthread_barrier_init(
          &new->barrier,
          NULL,
          suscan_inspsched_get_num_workers(new->sched) + 1) == 0,
      goto fail);

  /*
   * This mutex will protect the spectral tuner from concurrent access by
   * consumer, analyzer and scheduler worker threads
   */
  SU_TRYCATCH(pthread_mutex_init(&new->sched_lock, NULL) == 0, goto fail);

  /*
   * This mutex will protect access to the inspector list. It will allow
   * resolving an inspector handle without blocking an entire callback
   */

  SU_TRYCATCH(
      pthread_mutex_init(&new->inspector_list_mutex, NULL) == 0,
      goto fail);
  new->inspector_list_init = SU_TRUE;

  SU_TRYCATCH(suscan_source_start_capture(new->source), goto fail);

  new->effective_samp_rate = suscan_local_analyzer_get_samp_rate(new);

  /* Allocate read buffer */
  new->read_size =
      new->effective_samp_rate <= SUSCAN_ANALYZER_SLOW_RATE
      ? SUSCAN_ANALYZER_SLOW_READ_SIZE
      : SUSCAN_ANALYZER_FAST_READ_SIZE;

  if (new->read_size < new->source->mtu)
    new->read_size = new->source->mtu;

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

  if (pthread_create(
      &new->thread,
      NULL,
      suscan_analyzer_thread,
      new) == -1) {
    SU_ERROR("Cannot create main thread\n");
    goto fail;
  }

  parent->running = SU_TRUE;

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

  unsigned int i;
  struct suscan_inspector_overridable_request *req;

  /* Prevent source from entering in timeout loops */
  if (self->source != NULL)
    suscan_source_force_eos(self->source);

  if (self->parent->running) {
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

  /* Halt all inspector scheduler workers */
  if (self->sched != NULL) {
    if (!suscan_inspsched_destroy(self->sched)) {
      SU_ERROR("Failed to shutdown inspector scheduler, memory leak ahead\n");
      return;
    }

    /* FIXME: Add flag to prevent initialization errors! */
    pthread_barrier_destroy(&self->barrier);

    pthread_mutex_destroy(&self->sched_lock);
  }

  /* Free channel detector */
  if (self->detector != NULL)
    su_channel_detector_destroy(self->detector);

  if (self->smooth_psd != NULL)
    su_smoothpsd_destroy(self->smooth_psd);

  if (self->loop_init)
    pthread_mutex_destroy(&self->loop_mutex);

  if (self->inspector_list_init)
    pthread_mutex_destroy(&self->inspector_list_mutex);

  /* Free spectral tuner */
  if (self->stuner != NULL)
    su_specttuner_destroy(self->stuner);

  /* Free all pending overridable requests */
  while (self->insp_overridable != NULL) {
    req = self->insp_overridable->next;

    suscan_inspector_overridable_request_destroy(
        self->insp_overridable);

    self->insp_overridable = req;
  }

  /* Free read buffer */
  if (self->read_buf != NULL)
    free(self->read_buf);

  /* Remove all channel analyzers */
  for (i = 0; i < self->inspector_count; ++i)
    if (self->inspector_list[i] != NULL)
      suscan_inspector_destroy(self->inspector_list[i]);

  if (self->inspector_list != NULL)
    free(self->inspector_list);

  /* Delete source information */
  if (self->source != NULL)
    suscan_source_destroy(self->source);

  /* Release slow worker data */
  suscan_local_analyzer_destroy_slow_worker_data(self);

  if (self->throttle_mutex_init)
    pthread_mutex_destroy(&self->throttle_mutex);

  /* Delete all baseband filters */
  for (i = 0; i < self->bbfilt_count; ++i)
    if (self->bbfilt_list[i] != NULL)
      suscan_analyzer_baseband_filter_destroy(self->bbfilt_list[i]);

  if (self->bbfilt_list != NULL)
    free(self->bbfilt_list);

  /* Finalize source info */
  suscan_analyzer_source_info_finalize(&self->source_info);

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

  self->iq_rev = value;

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

SUPRIVATE struct suscan_analyzer_source_info *
suscan_local_analyzer_get_source_info_pointer(const void *ptr)
{
  const suscan_local_analyzer_t *self = (const suscan_local_analyzer_t *) ptr;

  return (struct suscan_analyzer_source_info *) &self->source_info;
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

/* Fast methods */
SUPRIVATE SUBOOL
suscan_local_analyzer_set_inspector_frequency(
    void *ptr,
    SUHANDLE handle,
    SUFREQ freq)
{
  suscan_local_analyzer_t *self = (suscan_local_analyzer_t *) ptr;

  return suscan_local_analyzer_set_inspector_freq_overridable(
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

  return suscan_local_analyzer_set_inspector_bandwidth_overridable(
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
