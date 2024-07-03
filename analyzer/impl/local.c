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
  if (!suscan_source_override_throttle(self->source, val)) {
    SU_ERROR("Failed to adjust source's effective rate\n");
    return SU_FALSE;
  }

  self->source_info.effective_samp_rate = val;

  /* Propagate throttling to inspectors */
  SU_TRYCATCH(
    suscan_local_analyzer_set_inspector_throttle_overridable(
      self,
      val / suscan_local_analyzer_get_samp_rate(self)),
    return SU_FALSE);
  
  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_local_analyzer_reset_throttle(suscan_local_analyzer_t *self)
{
  return suscan_local_analyzer_override_throttle(
      self,
      suscan_local_analyzer_get_samp_rate(self));
}

SUBOOL
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

SUBOOL suscan_local_analyzer_init_channel_worker(suscan_local_analyzer_t *self);
SUBOOL suscan_local_analyzer_init_wide_worker(suscan_local_analyzer_t *self);
SUBOOL suscan_local_analyzer_start_channel_worker(suscan_local_analyzer_t *self);
SUBOOL suscan_local_analyzer_start_wide_worker(suscan_local_analyzer_t *self);

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
  const struct suscan_analyzer_history_size_msg *history_size;
  const struct suscan_analyzer_replay_msg *replay;

  void *private = NULL;
  uint32_t type;
  SUBOOL mutex_acquired = SU_FALSE;
  SUBOOL halt_acked = SU_FALSE;

  switch (self->parent->params.mode) {
    case SUSCAN_ANALYZER_MODE_CHANNEL:
      SU_TRY(suscan_local_analyzer_start_channel_worker(self));
      break;

    case SUSCAN_ANALYZER_MODE_WIDE_SPECTRUM:
      SU_TRY(suscan_local_analyzer_start_wide_worker(self));
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

        case SUSCAN_ANALYZER_MESSAGE_TYPE_HISTORY_SIZE:
          history_size = (const struct suscan_analyzer_history_size_msg *) private;
          SU_TRYCATCH(
            suscan_local_analyzer_slow_set_history_size(self, history_size->buffer_length),
            goto done);
          break;

        case SUSCAN_ANALYZER_MESSAGE_TYPE_REPLAY:
          replay = (const struct suscan_analyzer_replay_msg *) private;
          SU_TRYCATCH(
            suscan_local_analyzer_slow_set_replay(self, replay->replay),
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
                    self->source_info.effective_samp_rate),
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

  if (!halt_acked)
    suscan_local_analyzer_wait_for_halt(self);

  self->parent->running = SU_FALSE;

  return NULL;
}

/*************************** Analyzer interface *******************************/
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
  struct suscan_sample_buffer_pool_params bp_params = 
    suscan_sample_buffer_pool_params_INITIALIZER;
  
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
  SU_TRYCATCH(new->source = suscan_source_new(config), goto fail);
  source_info = suscan_source_get_info(new->source);
  new->source_info = *source_info;

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

  /* Create spectral tuner, with suitable read size */
  if (new->source_info.effective_samp_rate >= 10000000)
    st_params.window_size = 131072;
  else if (new->source_info.effective_samp_rate >= 5000000)
    st_params.window_size = 65536;
  else if (new->source_info.effective_samp_rate >= 1600000)
    st_params.window_size = 16384;
  else if (new->source_info.effective_samp_rate >= 250000)
    st_params.window_size = 4096;
  else
    st_params.window_size = 2048;

  /* Initialize buffer pools */
  bp_params.alloc_size     = st_params.window_size;
  bp_params.name           = "baseband";

  /*
   * If we support VM circularity, we cannot have early windowing. On the
   * other hand, reads are going to alternate between EVEN and ODD states:
   * 
   * In the EVEN state, we read window_size/2 samples with offset window_size/2
   * In the ODD state, we read window_size/2 samples with offset 0
   */

  if (suscan_vm_circbuf_allowed(st_params.window_size)) {
    bp_params.vm_circularity  = SU_TRUE;
    st_params.early_windowing = SU_FALSE;
    new->circularity          = SU_TRUE;
  }
  
  if ((new->bufpool = suscan_sample_buffer_pool_new(&bp_params)) == NULL) {
    SU_ERROR("Cannot create sample buffer pool\n");
    if (new->circularity) {
      SU_INFO("Trying again with no VM circularity...\n");
      bp_params.vm_circularity  = SU_FALSE;
      new->circularity          = SU_FALSE;

      if ((new->bufpool = suscan_sample_buffer_pool_new(&bp_params)) == NULL) {
        SU_ERROR("Failed to create buffer pool (again)\n");
        goto fail;
      }
    } else {
      goto fail;
    }
  }

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

  /* Allocate read buffer */
  new->read_size =
      new->source_info.effective_samp_rate <= SUSCAN_ANALYZER_SLOW_RATE
      ? SUSCAN_ANALYZER_SLOW_READ_SIZE
      : SUSCAN_ANALYZER_FAST_READ_SIZE;

  if (new->read_size < source_info->mtu)
    new->read_size = source_info->mtu;

  if ((new->read_buf = malloc(
      new->read_size * sizeof(SUCOMPLEX))) == NULL) {
    SU_ERROR("Failed to allocate read buffer\n");
    goto fail;
  }

  /* Populate source info. */
  SU_TRY_FAIL(
    suscan_source_info_init_copy(
      &new->source_info,
      suscan_source_get_info(new->source)));

  if (parent->params.mode == SUSCAN_ANALYZER_MODE_WIDE_SPECTRUM) {
    SU_TRYCATCH(
        suscan_local_analyzer_init_wide_worker(new),
        goto fail);
  } else {
    SU_TRYCATCH(
        suscan_local_analyzer_init_channel_worker(new),
        goto fail);
  }

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

  /* Stop capture source, now that workers using it have stopped */
  if (self->source != NULL && suscan_source_is_capturing(self->source))
    suscan_source_stop_capture(self->source);

  /* Destroy global inspector table */
  suscan_local_analyzer_destroy_global_handles_unsafe(self);

  /* Free channel detector */
  if (self->detector != NULL)
    su_channel_detector_destroy(self->detector);

  if (self->psd_worker != NULL) {
    if (!suscan_analyzer_halt_worker(self->psd_worker)) {
      SU_ERROR("Failed to destroy PSD worker.\n");

      /* Mark smoothPSD object as released */
      self->smooth_psd = NULL;
    }
  }

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

  /* Delete all baseband filters (triggered by the dtor) */
  if (self->bbfilt_tree != NULL)
    rbtree_destroy(self->bbfilt_tree);

  /* Finalize source info */
  suscan_source_info_finalize(&self->source_info);

  /* Consume any pending messages */
  suscan_analyzer_consume_mq(&self->mq_in);

  /* Finalize buffers */
  if (self->bufpool != NULL)
    suscan_sample_buffer_pool_destroy(self->bufpool);
  
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
suscan_local_analyzer_set_history_size(void *ptr, SUSCOUNT size)
{
  suscan_local_analyzer_t *self = (suscan_local_analyzer_t *) ptr;

  return suscan_local_analyzer_slow_set_history_size(self, size);
}

SUPRIVATE SUBOOL
suscan_local_analyzer_replay(void *ptr, SUBOOL replay)
{
  suscan_local_analyzer_t *self = (suscan_local_analyzer_t *) ptr;

  return suscan_local_analyzer_slow_set_replay(self, replay);
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
  return suscan_source_is_real_time(self->source);
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
suscan_local_analyzer_set_rel_bandwidth(void *ptr, SUFLOAT rel_bw)
{
  suscan_local_analyzer_t *self = (suscan_local_analyzer_t *) ptr;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      self->parent->params.mode == SUSCAN_ANALYZER_MODE_WIDE_SPECTRUM,
      goto done);

  SU_TRYCATCH(rel_bw >= 0.001, goto done);

  self->pending_sweep_params =
      self->sweep_params_requested
        ? self->pending_sweep_params
        : self->current_sweep_params;
  self->pending_sweep_params.rel_bw = rel_bw;
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
    SET_CALLBACK(set_history_size);
    SET_CALLBACK(replay);
    SET_CALLBACK(register_baseband_filter);
    SET_CALLBACK(get_measured_samp_rate);
    SET_CALLBACK(get_source_info_pointer);
    SET_CALLBACK(commit_source_info);
    SET_CALLBACK(set_sweep_strategy);
    SET_CALLBACK(set_spectrum_partitioning);
    SET_CALLBACK(set_hop_range);
    SET_CALLBACK(set_rel_bandwidth);
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
