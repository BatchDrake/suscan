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

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <stdint.h>
#include <time.h>

#define SU_LOG_DOMAIN "analyzer"

#include <sigutils/sigutils.h>
#include <sigutils/detect.h>

#include "analyzer.h"

#include "mq.h"
#include "msg.h"

/************************ Overridable request API ****************************/
SUPRIVATE void
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
suscan_analyzer_acquire_overridable(
    suscan_analyzer_t *self,
    SUHANDLE handle)
{
  struct suscan_inspector_overridable_request *req = NULL;
  struct suscan_inspector_overridable_request *own_req = NULL;
  suscan_inspector_t *insp = NULL;

  /*
   * TODO: Maybe acquire scheduler mutex?
   */

  SU_TRYCATCH(suscan_analyzer_lock_inspector_list(self), goto done);
  SU_TRYCATCH(insp = suscan_analyzer_get_inspector(self, handle), goto done);
  SU_TRYCATCH(insp->state == SUSCAN_ASYNC_STATE_RUNNING, goto done);

  if ((req = suscan_inspector_get_userdata(insp)) == NULL) {
    /* No userdata. Release mutex, create object and lock again. */
    suscan_analyzer_unlock_inspector_list(self);

    SU_TRYCATCH(
        own_req = suscan_inspector_overridable_request_new(insp),
        goto done);

    SU_TRYCATCH(suscan_analyzer_lock_inspector_list(self), goto done);

    /* Many things may have happened here */
    SU_TRYCATCH(handle >= 0 && handle < self->inspector_count, goto done);
    SU_TRYCATCH(insp = suscan_analyzer_get_inspector(self, handle), goto done);
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
suscan_analyzer_release_overridable(
    suscan_analyzer_t *self,
    struct suscan_inspector_overridable_request *rq)
{
  suscan_analyzer_unlock_inspector_list(self);

  return SU_TRUE;
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
      PTR_LIST_APPEND_CHECK(self->bbfilt, new) != -1,
      goto fail);

  return SU_TRUE;

fail:
  if (new != NULL)
    suscan_analyzer_baseband_filter_destroy(new);

  return SU_FALSE;
}

/************************ Source worker callback *****************************/

SUBOOL
suscan_analyzer_lock_loop(suscan_analyzer_t *analyzer)
{
  return pthread_mutex_lock(&analyzer->loop_mutex) != -1;
}

void
suscan_analyzer_unlock_loop(suscan_analyzer_t *analyzer)
{
  (void) pthread_mutex_unlock(&analyzer->loop_mutex);
}

SUBOOL
suscan_analyzer_lock_inspector_list(suscan_analyzer_t *analyzer)
{
  return pthread_mutex_lock(&analyzer->inspector_list_mutex) != -1;
}

void
suscan_analyzer_unlock_inspector_list(suscan_analyzer_t *analyzer)
{
  (void) pthread_mutex_unlock(&analyzer->inspector_list_mutex);
}

/************************* Main analyzer thread *******************************/
void
suscan_analyzer_req_halt(suscan_analyzer_t *analyzer)
{
  analyzer->halt_requested = SU_TRUE;

  suscan_mq_write_urgent(
      &analyzer->mq_in,
      SUSCAN_WORKER_MSG_TYPE_HALT,
      NULL);
}

SUPRIVATE void
suscan_analyzer_ack_halt(suscan_analyzer_t *analyzer)
{
  suscan_mq_write_urgent(
      analyzer->mq_out,
      SUSCAN_WORKER_MSG_TYPE_HALT,
      NULL);
}

SUPRIVATE void
suscan_wait_for_halt(suscan_analyzer_t *analyzer)
{
  uint32_t type;
  void *private;

  for (;;) {
    private = suscan_mq_read(&analyzer->mq_in, &type);
    if (type == SUSCAN_WORKER_MSG_TYPE_HALT) {
      suscan_analyzer_ack_halt(analyzer);
      break;
    }

    suscan_analyzer_dispose_message(type, private);
  }
}

SUPRIVATE SUBOOL
suscan_analyzer_override_throttle(
    suscan_analyzer_t *analyzer,
    SUSCOUNT val)
{
  SUBOOL mutex_acquired = SU_FALSE;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(!suscan_analyzer_is_real_time(analyzer), goto done);

  SU_TRYCATCH(
      pthread_mutex_lock(&analyzer->throttle_mutex) != -1,
      goto done);
  mutex_acquired = SU_TRUE;

  suscan_throttle_init(&analyzer->throttle, val);

  analyzer->effective_samp_rate = val;

  ok = SU_TRUE;

done:

  if (mutex_acquired)
    pthread_mutex_unlock(&analyzer->throttle_mutex);

  return ok;
}

SUPRIVATE SUBOOL
suscan_analyzer_reset_throttle(suscan_analyzer_t *analyzer)
{
  return suscan_analyzer_override_throttle(
      analyzer,
      suscan_analyzer_get_samp_rate(analyzer));
}

SUPRIVATE SUBOOL
suscan_analyzer_readjust_detector(
    suscan_analyzer_t *analyzer,
    struct sigutils_channel_detector_params *params)
{
  su_channel_detector_t *new_detector = NULL;

  su_channel_params_adjust(params);

  if (!su_channel_detector_set_params(analyzer->detector, params)) {
    /* If not possibe, re-create detector object */
    SU_TRYCATCH(
        new_detector = su_channel_detector_new(params),
        return SU_FALSE);

    su_channel_detector_destroy(analyzer->detector);
    analyzer->detector = new_detector;
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

SUPRIVATE void *
suscan_analyzer_thread(void *data)
{
  suscan_analyzer_t *self = (suscan_analyzer_t *) data;
  struct sigutils_channel_detector_params new_det_params;
  const struct suscan_analyzer_params *new_params;
  const struct suscan_analyzer_throttle_msg *throttle;
  void *private = NULL;
  uint32_t type;
  SUBOOL mutex_acquired = SU_FALSE;
  SUBOOL halt_acked = SU_FALSE;

  switch (self->params.mode) {
    case SUSCAN_ANALYZER_MODE_CHANNEL:
      if (!suscan_worker_push(
            self->source_wk,
            suscan_source_channel_wk_cb,
            self->source)) {
          suscan_analyzer_send_status(
              self,
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
              self,
              SUSCAN_ANALYZER_MESSAGE_TYPE_SOURCE_INIT,
              SUSCAN_ANALYZER_INIT_FAILURE,
              "Failed to push source callback to worker (wide spectrum mode)");
          goto done;
        }
      break;
  }


  /* Signal initialization success */
  suscan_analyzer_send_status(
      self,
      SUSCAN_ANALYZER_MESSAGE_TYPE_SOURCE_INIT,
      SUSCAN_ANALYZER_INIT_SUCCESS,
      NULL);

  /* Pop all messages from queue before reading from the source */
  for (;;) {
    /* First read: blocks */
    private = suscan_mq_read(&self->mq_in, &type);

    do {
      switch (type) {
        case SUSCAN_WORKER_MSG_TYPE_HALT:
          suscan_analyzer_ack_halt(self);
          halt_acked = SU_TRUE;
          /* Nothing to dispose, safe to break the loop */
          goto done;

        case SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR:
          /* Baudrate inspector command. Handle separately */
          SU_TRYCATCH(
              suscan_analyzer_parse_inspector_msg(self, private),
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
              suscan_mq_write(self->mq_out, type, private),
              goto done);

          /* Not belonging to us anymore */
          private = NULL;

          break;

        case SUSCAN_ANALYZER_MESSAGE_TYPE_THROTTLE:
          throttle = (const struct suscan_analyzer_throttle_msg *) private;
          if (throttle->samp_rate == 0) {
            SU_TRYCATCH(
                suscan_analyzer_reset_throttle(self),
                goto done);
          } else {
            SU_TRYCATCH(
                suscan_analyzer_override_throttle(self, throttle->samp_rate),
                goto done);
          }
          break;

        case SUSCAN_ANALYZER_MESSAGE_TYPE_PARAMS:
          /*
           * Parameter messages affect the source worker, that must get their
           * objects updated. In order to do that, we protect their access
           * through the source's params_mutex
           */

          SU_TRYCATCH(
              pthread_mutex_lock(&self->loop_mutex) != -1,
              goto done);
          mutex_acquired = SU_TRUE;

          /* vvvvvvvvvvvvvvv Source parameters update start vvvvvvvvvvvvv */
          new_params = (const struct suscan_analyzer_params *) private;

          /* Attempt to update detector parameters */
          new_det_params = self->detector->params; /* Not all parameters are allowed */

          new_det_params.window_size = new_params->detector_params.window_size;
          new_det_params.window = new_params->detector_params.window;
          new_det_params.fc = new_params->detector_params.fc;
          su_channel_params_adjust(&new_det_params);

          SU_TRYCATCH(
              suscan_analyzer_readjust_detector(self, &new_det_params),
              goto done);

          self->interval_channels = new_params->channel_update_int;

          if (SU_ABS(self->interval_psd - new_params->psd_update_int) > 1e-6) {
            self->interval_psd = new_params->psd_update_int;
            self->det_num_psd = 0;
#ifdef __linux__
            clock_gettime(CLOCK_MONOTONIC_COARSE, &self->last_psd);
#else
            clock_gettime(CLOCK_MONOTONIC, &self->last_psd);
#endif /* __linux__ */
          }

          /* ^^^^^^^^^^^^^ Source parameters update end ^^^^^^^^^^^^^^^^^  */

          SU_TRYCATCH(
              pthread_mutex_unlock(&self->loop_mutex) != -1,
              goto done);
          mutex_acquired = SU_FALSE;

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
    suscan_wait_for_halt(self);

  self->running = SU_FALSE;

  pthread_exit(NULL);

  return NULL;
}

void *
suscan_analyzer_read(suscan_analyzer_t *analyzer, uint32_t *type)
{
  return suscan_mq_read(analyzer->mq_out, type);
}

void *
suscan_analyzer_read_timeout(
    suscan_analyzer_t *analyzer,
    uint32_t *type,
    const struct timeval *timeout)
{
  return suscan_mq_read_timeout(analyzer->mq_out, type, timeout);
}

struct suscan_analyzer_inspector_msg *
suscan_analyzer_read_inspector_msg(suscan_analyzer_t *analyzer)
{
  /* TODO: use poll and wait to wait for EOS and inspector messages */
  return suscan_mq_read_w_type(
      analyzer->mq_out,
      SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR);
}

struct suscan_analyzer_inspector_msg *
suscan_analyzer_read_inspector_msg_timeout(
    suscan_analyzer_t *analyzer,
    const struct timeval *timeout)
{
  /* TODO: use poll and wait to wait for EOS and inspector messages */
  return suscan_mq_read_w_type_timeout(
      analyzer->mq_out,
      SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR,
      timeout);
}

SUBOOL
suscan_analyzer_write(suscan_analyzer_t *analyzer, uint32_t type, void *priv)
{
  return suscan_mq_write(&analyzer->mq_in, type, priv);
}

void
suscan_analyzer_consume_mq(struct suscan_mq *mq)
{
  void *private;
  uint32_t type;

  while (suscan_mq_poll(mq, &type, &private))
    suscan_analyzer_dispose_message(type, private);
}

SUPRIVATE SUBOOL
suscan_analyzer_consume_mq_until_halt(struct suscan_mq *mq)
{
  void *private;
  uint32_t type;

  while (suscan_mq_poll(mq, &type, &private))
    if (type == SUSCAN_WORKER_MSG_TYPE_HALT)
      return SU_TRUE;
    else
      suscan_analyzer_dispose_message(type, private);

  return SU_FALSE;
}

SUBOOL
suscan_analyzer_halt_worker(suscan_worker_t *worker)
{
  while (worker->state == SUSCAN_WORKER_STATE_RUNNING) {
    suscan_worker_req_halt(worker);

    while (!suscan_analyzer_consume_mq_until_halt(worker->mq_out))
      suscan_mq_wait(worker->mq_out);
  }

  return suscan_worker_destroy(worker);
}


SUPRIVATE void
suscan_analyzer_init_detector_params(
    suscan_analyzer_t *self,
    struct sigutils_channel_detector_params *params)
{
  /* Recover template */
  *params = self->params.detector_params;

  /* Populate members with source information */
  params->mode = SU_CHANNEL_DETECTOR_MODE_SPECTRUM;

  params->samp_rate = suscan_analyzer_get_samp_rate(self);

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
suscan_analyzer_source_barrier(suscan_analyzer_t *analyzer)
{
  pthread_barrier_wait(&analyzer->barrier);
}

void
suscan_analyzer_enter_sched(suscan_analyzer_t *analyzer)
{
  pthread_mutex_lock(&analyzer->sched_lock);
}

void
suscan_analyzer_leave_sched(suscan_analyzer_t *analyzer)
{
  pthread_mutex_unlock(&analyzer->sched_lock);
}

su_specttuner_channel_t *
suscan_analyzer_open_channel_ex(
    suscan_analyzer_t *analyzer,
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
              suscan_analyzer_get_samp_rate(analyzer),
              chan_info->fc - chan_info->ft));

  if (params.f0 < 0)
    params.f0 += 2 * PI;

  params.bw =
      SU_NORM2ANG_FREQ(
          SU_ABS2NORM_FREQ(
              suscan_analyzer_get_samp_rate(analyzer),
              chan_info->f_hi - chan_info->f_lo));
  params.guard = SUSCAN_ANALYZER_GUARD_BAND_PROPORTION;
  params.on_data = on_data;
  params.privdata = privdata;
  params.precise = precise;

  suscan_analyzer_enter_sched(analyzer);

  SU_TRYCATCH(
      channel = su_specttuner_open_channel(analyzer->stuner, &params),
      goto done);

done:
  suscan_analyzer_leave_sched(analyzer);

  return channel;
}

su_specttuner_channel_t *
suscan_analyzer_open_channel(
    suscan_analyzer_t *analyzer,
    const struct sigutils_channel *chan_info,
    SUBOOL (*on_data) (
        const struct sigutils_specttuner_channel *channel,
        void *privdata,
        const SUCOMPLEX *data, /* This pointer remains valid until the next call to feed */
        SUSCOUNT size),
        void *privdata)
{
  return suscan_analyzer_open_channel_ex(
      analyzer,
      chan_info,
      SU_FALSE,
      on_data,
      privdata);
}

SUBOOL
suscan_analyzer_close_channel(
    suscan_analyzer_t *analyzer,
    su_specttuner_channel_t *channel)
{
  SUBOOL ok;

  suscan_analyzer_enter_sched(analyzer);

  ok = su_specttuner_close_channel(analyzer->stuner, channel);

  suscan_analyzer_leave_sched(analyzer);

  return ok;
}

/*
 * There is no explicit UNBIND. Unbind happens inside
 * suscan_analyzer_on_channel_data when the inspector state
 * is different from RUNNING.
 */

SUBOOL
suscan_analyzer_bind_inspector_to_channel(
    suscan_analyzer_t *analyzer,
    su_specttuner_channel_t *channel,
    suscan_inspector_t *insp)
{
  struct suscan_inspector_task_info *task_info = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      task_info = suscan_inspector_task_info_new(insp),
      return SU_FALSE);

  task_info->channel = channel;

  suscan_analyzer_enter_sched(analyzer);

  SU_TRYCATCH(
      suscan_inspsched_append_task_info(analyzer->sched, task_info),
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
  suscan_analyzer_leave_sched(analyzer);

  if (task_info != NULL)
    suscan_inspector_task_info_destroy(task_info);

  return ok;
}

SUBOOL
suscan_analyzer_force_eos(suscan_analyzer_t *self)
{
  if (self->source == NULL)
    return SU_FALSE;

  suscan_source_force_eos(self->source);

  return SU_TRUE;
}
void
suscan_analyzer_destroy(suscan_analyzer_t *analyzer)
{
  unsigned int i;
  struct suscan_inspector_overridable_request *req;

  /* Prevent source from entering in timeout loops */
  if (analyzer->source != NULL)
    suscan_source_force_eos(analyzer->source);

  if (analyzer->running) {
    if (!analyzer->halt_requested) {
      suscan_analyzer_req_halt(analyzer);

      /*
       * TODO: this cannot wait forever. Add suscan_mq_read_with_timeout
       */
      while (!suscan_analyzer_consume_mq_until_halt(analyzer->mq_out))
        suscan_mq_wait(analyzer->mq_out);
    }

    /* TODO: add a timeout here too */
    if (pthread_join(analyzer->thread, NULL) == -1) {
      SU_ERROR("Thread failed to join, memory leak ahead\n");
      return;
    }
  }

  /* TODO: Concurrently-force EOS in source object */

  if (analyzer->source_wk != NULL)
    if (!suscan_analyzer_halt_worker(analyzer->source_wk)) {
      SU_ERROR("Source worker destruction failed, memory leak ahead\n");
      return;
    }

  if (analyzer->slow_wk != NULL)
    if (!suscan_analyzer_halt_worker(analyzer->slow_wk)) {
      SU_ERROR("Slow worker destruction failed, memory leak ahead\n");
      return;
    }

  /* Halt all inspector scheduler workers */
  if (analyzer->sched != NULL) {
    if (!suscan_inspsched_destroy(analyzer->sched)) {
      SU_ERROR("Failed to shutdown inspector scheduler, memory leak ahead\n");
      return;
    }

    /* FIXME: Add flag to prevent initialization errors! */
    pthread_barrier_destroy(&analyzer->barrier);

    pthread_mutex_destroy(&analyzer->sched_lock);
  }

  /* Consume any pending messages */
  suscan_analyzer_consume_mq(&analyzer->mq_in);

  /* Free channel detector */
  if (analyzer->detector != NULL)
    su_channel_detector_destroy(analyzer->detector);

  if (analyzer->loop_init)
    pthread_mutex_destroy(&analyzer->loop_mutex);

  if (analyzer->inspector_list_init)
    pthread_mutex_destroy(&analyzer->inspector_list_mutex);

  /* Free spectral tuner */
  if (analyzer->stuner != NULL)
    su_specttuner_destroy(analyzer->stuner);

  /* Free all pending overridable requests */
  while (analyzer->insp_overridable != NULL) {
    req = analyzer->insp_overridable->next;

    suscan_inspector_overridable_request_destroy(
        analyzer->insp_overridable);

    analyzer->insp_overridable = req;
  }

  /* Free read buffer */
  if (analyzer->read_buf != NULL)
    free(analyzer->read_buf);

  /* Remove all channel analyzers */
  for (i = 0; i < analyzer->inspector_count; ++i)
    if (analyzer->inspector_list[i] != NULL)
      suscan_inspector_destroy(analyzer->inspector_list[i]);

  if (analyzer->inspector_list != NULL)
    free(analyzer->inspector_list);

  /* Delete source information */
  if (analyzer->source != NULL)
    suscan_source_destroy(analyzer->source);

  /* Release slow worker data */
  suscan_analyzer_destroy_slow_worker_data(analyzer);

  if (analyzer->throttle_mutex_init)
    pthread_mutex_destroy(&analyzer->throttle_mutex);

  /* Delete all baseband filters */
  for (i = 0; i < analyzer->bbfilt_count; ++i)
    if (analyzer->bbfilt_list[i] != NULL)
      suscan_analyzer_baseband_filter_destroy(analyzer->bbfilt_list[i]);

  if (analyzer->bbfilt_list != NULL)
    free(analyzer->bbfilt_list);

  suscan_mq_finalize(&analyzer->mq_in);

  free(analyzer);
}

SUBOOL
suscan_analyzer_source_init(
    suscan_analyzer_t *analyzer,
    suscan_source_config_t *config)
{
  SU_TRYCATCH(analyzer->source = suscan_source_new(config), goto fail);

  /* For non-realtime sources (i.e. file sources), enable throttling */
  if (!suscan_analyzer_is_real_time(analyzer)) {
    /* Create throttle mutex */
      (void) pthread_mutex_init(&analyzer->throttle_mutex, NULL); /* Always succeeds */
      analyzer->throttle_mutex_init = SU_TRUE;

    suscan_throttle_init(
        &analyzer->throttle,
        suscan_analyzer_get_samp_rate(analyzer));
  }

  return SU_TRUE;

fail:
  return SU_FALSE;
}

SUBOOL
suscan_analyzer_set_iq_reverse(suscan_analyzer_t *analyzer, SUBOOL rev)
{
  analyzer->iq_rev = rev;
  return SU_TRUE;
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

suscan_analyzer_t *
suscan_analyzer_new(
    const struct suscan_analyzer_params *params,
    suscan_source_config_t *config,
    struct suscan_mq *mq)
{
  suscan_analyzer_t *new = NULL;
  struct sigutils_specttuner_params st_params =
      sigutils_specttuner_params_INITIALIZER;
  struct sigutils_channel_detector_params det_params;

#ifdef DEBUG_ANALYZER_PARAMS
  suscan_analyzer_params_debug(params);
#endif /* DEBUG_ANALYZER_PARAMS */
  if ((new = calloc(1, sizeof (suscan_analyzer_t))) == NULL) {
    SU_ERROR("Cannot allocate analyzer\n");
    goto fail;
  }

  new->params = *params;

  /* Allocate read buffer */

  new->read_size = SUSCAN_ANALYZER_READ_SIZE; /* params->detector_params.window_size; */

  if ((new->read_buf = malloc(
      new->read_size * sizeof(SUCOMPLEX))) == NULL) {
    SU_ERROR("Failed to allocate read buffer\n");
    goto fail;
  }

  /* Create input message queue */
  if (!suscan_mq_init(&new->mq_in)) {
    SU_ERROR("Cannot allocate input MQ\n");
    goto fail;
  }

  /* Initialize source */
  if (!suscan_analyzer_source_init(new, config)) {
    SU_ERROR("Failed to initialize source\n");
    goto fail;
  }

  /* Periodic updates */
  new->interval_channels = params->channel_update_int;
  new->interval_psd      = params->psd_update_int;

#ifdef __linux__
  clock_gettime(CLOCK_MONOTONIC_COARSE, &new->last_psd);
  clock_gettime(CLOCK_MONOTONIC_COARSE, &new->last_channels);
#else
  clock_gettime(CLOCK_MONOTONIC, &new->last_psd);
  clock_gettime(CLOCK_MONOTONIC, &new->last_channels);
#endif


  /* Create channel detector */
  (void) pthread_mutex_init(&new->loop_mutex, NULL); /* Always succeeds */
  new->loop_init = SU_TRUE;
  det_params = params->detector_params;
  suscan_analyzer_init_detector_params(new, &det_params);
  SU_TRYCATCH(
      new->detector = su_channel_detector_new(&det_params),
      goto fail);

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
  st_params.window_size = det_params.window_size;
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

  new->mq_out = mq;

  SU_TRYCATCH(suscan_source_start_capture(new->source), goto fail);

  if (new->read_size < new->source->mtu) {
    new->read_size = new->source->mtu;
    SUCOMPLEX *temp;
    SU_TRYCATCH(
        temp = realloc(new->read_buf, new->read_size * sizeof(SUCOMPLEX)),
        goto fail);
    new->read_buf = temp;
  }

  new->effective_samp_rate = suscan_analyzer_get_samp_rate(new);

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
        suscan_analyzer_readjust_detector(new, &det_params),
        goto fail);
  }

  /* In wide spectrum mode, additional tests are required */
  if (params->mode == SUSCAN_ANALYZER_MODE_WIDE_SPECTRUM) {
    SU_TRYCATCH(
        params->max_freq - params->min_freq >=
          suscan_analyzer_get_samp_rate(new),
        goto fail);
    new->current_sweep_params.fft_min_samples =
            SUSCAN_ANALYZER_MIN_POST_HOP_FFTS * det_params.window_size;
    new->current_sweep_params.max_freq = params->max_freq;
    new->current_sweep_params.min_freq = params->min_freq;
  }

  if (pthread_create(
      &new->thread,
      NULL,
      suscan_analyzer_thread,
      new) == -1) {
    SU_ERROR("Cannot create main thread\n");
    goto fail;
  }

  new->running = SU_TRUE;

  return new;

fail:
  if (new != NULL)
    suscan_analyzer_destroy(new);

  return NULL;
}
