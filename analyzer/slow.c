/*

  Copyright (C) 2019 Gonzalo José Carracedo Carballal

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

#define SU_LOG_DOMAIN "slow-worker"

#include <analyzer/impl/local.h>
#include <analyzer/msg.h>
#include <string.h>

/*
 * Some tasks take some time to complete, time that is several orders of
 * magnitude beyond what it takes to process a block of samples. Instead
 * of processing them directly in the source thread (which is quite busy
 * already), we create a separate worker (namely the slow worker) which takes
 * these tasks that are usually human-triggered and whose completion time is
 * not critical.
 */


void
suscan_local_analyzer_destroy_slow_worker_data(suscan_local_analyzer_t *self)
{
  unsigned int i;

  /* Delete all pending gain requessts */
  for (i = 0; i < self->gain_request_count; ++i)
    suscan_analyzer_gain_info_destroy(self->gain_request_list[i]);

  if (self->gain_request_list != NULL)
    free(self->gain_request_list);

  if (self->gain_req_mutex_init)
    pthread_mutex_destroy(&self->hotconf_mutex);

  if (self->antenna_req != NULL)
    free(self->antenna_req);
}

/***************************** Slow worker callbacks *************************/
SUPRIVATE SUBOOL
suscan_local_analyzer_set_gain_cb(
    struct suscan_mq *mq_out,
    void *wk_private,
    void *cb_private)
{
  suscan_local_analyzer_t *self = (suscan_local_analyzer_t *) wk_private;
  SUBOOL mutex_acquired = SU_FALSE;
  PTR_LIST_LOCAL(struct suscan_analyzer_gain_info, request);
  unsigned int i, j;

  /* vvvvvvvvvvvvvvvvvv Acquire hotconf request mutex vvvvvvvvvvvvvvvvvvvvvvvvv */
  SU_TRYCATCH(pthread_mutex_lock(&self->hotconf_mutex) != -1, goto fail);
  mutex_acquired = SU_TRUE;

  request_list  = self->gain_request_list;
  request_count = self->gain_request_count;

  self->gain_request_list  = NULL;
  self->gain_request_count = 0;

  pthread_mutex_unlock(&self->hotconf_mutex);
  mutex_acquired = SU_FALSE;
  /* ^^^^^^^^^^^^^^^^^^ Release hotconf request mutex ^^^^^^^^^^^^^^^^^^^^^^^^^ */

  /* Process all requests */
  for (i = 0; i < request_count; ++i) {
    SU_TRYCATCH(
        suscan_source_set_gain(
            self->source,
            request_list[i]->name,
            request_list[i]->value),
        goto fail);

    /* FIXME: This deserves a method */
    for (j = 0; j < self->source_info.gain_count; ++j)
      if (strcmp(
          self->source_info.gain_list[j]->name,
          request_list[i]->name) == 0)
        self->source_info.gain_list[j]->value = request_list[i]->value;
  }

fail:
  if (mutex_acquired)
    pthread_mutex_unlock(&self->hotconf_mutex);

  for (i = 0; i < request_count; ++i)
    suscan_analyzer_gain_info_destroy(request_list[i]);

  if (request_list != NULL)
    free(request_list);

  return SU_FALSE;
}

SUPRIVATE SUBOOL
suscan_local_analyzer_set_antenna_cb(
    struct suscan_mq *mq_out,
    void *wk_private,
    void *cb_private)
{
  suscan_local_analyzer_t *analyzer = (suscan_local_analyzer_t *) wk_private;
  SUBOOL mutex_acquired = SU_FALSE;
  char *req = NULL;

  /* vvvvvvvvvvvvvvvvvv Acquire hotconf request mutex vvvvvvvvvvvvvvvvvvvvvvvvv */
  SU_TRYCATCH(pthread_mutex_lock(&analyzer->hotconf_mutex) != -1, goto fail);
  mutex_acquired = SU_TRUE;

  req = analyzer->antenna_req;
  analyzer->antenna_req = NULL;

  pthread_mutex_unlock(&analyzer->hotconf_mutex);
  mutex_acquired = SU_FALSE;
  /* ^^^^^^^^^^^^^^^^^^ Release hotconf request mutex ^^^^^^^^^^^^^^^^^^^^^^^^^ */

  suscan_source_set_antenna(analyzer->source, req);

  /* TODO: Protect with mutex, set antenna and deliver source info */

fail:
  if (mutex_acquired)
    pthread_mutex_unlock(&analyzer->hotconf_mutex);

  if (req != NULL)
    free(req);

  return SU_FALSE;
}

SUPRIVATE SUBOOL
suscan_local_analyzer_set_dc_remove_cb(
    struct suscan_mq *mq_out,
    void *wk_private,
    void *cb_private)
{
  suscan_local_analyzer_t *analyzer = (suscan_local_analyzer_t *) wk_private;
  SUBOOL remove = (SUBOOL) (uintptr_t) cb_private;

  (void) suscan_source_set_dc_remove(analyzer->source, remove);

  /* Source info changed. Notify update */
  analyzer->source_info.dc_remove = remove;
  suscan_analyzer_send_source_info(
      analyzer->parent,
      &analyzer->source_info);

  return SU_FALSE;
}

SUPRIVATE SUBOOL
suscan_local_analyzer_set_agc_cb(
    struct suscan_mq *mq_out,
    void *wk_private,
    void *cb_private)
{
  suscan_local_analyzer_t *analyzer = (suscan_local_analyzer_t *) wk_private;
  SUBOOL set = (SUBOOL) (uintptr_t) cb_private;

  (void) suscan_source_set_agc(analyzer->source, set);

  /* Source info changed. Notify update */
  analyzer->source_info.agc = set;
  suscan_analyzer_send_source_info(
      analyzer->parent,
      &analyzer->source_info);

  return SU_FALSE;
}

SUPRIVATE SUBOOL
suscan_local_analyzer_set_bw_cb(
    struct suscan_mq *mq_out,
    void *wk_private,
    void *cb_private)
{
  suscan_local_analyzer_t *analyzer = (suscan_local_analyzer_t *) wk_private;
  SUFLOAT bw;

  if (analyzer->bw_req) {
    bw = analyzer->bw_req_value;
    if (suscan_source_set_bandwidth(analyzer->source, bw)) {
      if (analyzer->parent->params.mode == SUSCAN_ANALYZER_MODE_WIDE_SPECTRUM) {
        /* XXX: Use a proper frequency adjust method */
        analyzer->detector->params.bw = bw;
      }

      /* Source info changed. Notify update */
      analyzer->source_info.bandwidth = bw;

      suscan_analyzer_send_source_info(
          analyzer->parent,
          &analyzer->source_info);
    }
    analyzer->bw_req = analyzer->bw_req_value != bw;
  }

  return SU_FALSE;
}

SUPRIVATE SUBOOL
suscan_local_analyzer_set_ppm_cb(
    struct suscan_mq *mq_out,
    void *wk_private,
    void *cb_private)
{
  suscan_local_analyzer_t *analyzer = (suscan_local_analyzer_t *) wk_private;
  SUFLOAT ppm;

  if (analyzer->ppm_req) {
    ppm = analyzer->ppm_req_value;
    if (suscan_source_set_ppm(analyzer->source, ppm)) {
      /* Source info changed. Notify update */
      analyzer->source_info.ppm = ppm;

      suscan_analyzer_send_source_info(
          analyzer->parent,
          &analyzer->source_info);
    }
    analyzer->ppm_req = analyzer->ppm_req_value != ppm;
  }

  return SU_FALSE;
}

SUPRIVATE SUBOOL
suscan_local_analyzer_set_freq_cb(
    struct suscan_mq *mq_out,
    void *wk_private,
    void *cb_private)
{
  suscan_local_analyzer_t *analyzer = (suscan_local_analyzer_t *) wk_private;
  SUFREQ freq;
  SUFREQ lnb_freq;

  if (analyzer->freq_req) {
    freq = analyzer->freq_req_value;
    lnb_freq = analyzer->lnb_req_value;
    if (suscan_source_set_freq2(analyzer->source, freq, lnb_freq)) {
      if (analyzer->parent->params.mode == SUSCAN_ANALYZER_MODE_WIDE_SPECTRUM) {
        /* XXX: Use a proper frequency adjust method */
        analyzer->detector->params.fc = freq;
      }

      /* Source info changed. Notify update */
      analyzer->source_info.frequency = freq;
      analyzer->source_info.lnb       = lnb_freq;

      suscan_analyzer_send_source_info(
          analyzer->parent,
          &analyzer->source_info);
    }

    analyzer->freq_req = (analyzer->freq_req_value != freq ||
        analyzer->lnb_req_value != lnb_freq);
  }

  return SU_FALSE;
}

SUPRIVATE SUBOOL
suscan_local_analyzer_set_inspector_freq_slow(
    suscan_local_analyzer_t *analyzer,
    SUHANDLE handle,
    SUFREQ freq)
{
  SUBOOL ok = SU_FALSE;
  struct suscan_inspector_overridable_request *req;

  SU_TRYCATCH(
      req = suscan_local_analyzer_acquire_overridable(analyzer, handle),
      goto done);

  req->freq_request = SU_TRUE;
  req->new_freq = freq;

  SU_TRYCATCH(
      suscan_local_analyzer_release_overridable(analyzer, req),
      goto done);

  ok = SU_TRUE;

done:

  return ok;
}

SUPRIVATE SUBOOL
suscan_local_analyzer_set_inspector_bandwidth_slow(
    suscan_local_analyzer_t *analyzer,
    SUHANDLE handle,
    SUFLOAT bw)
{
  SUBOOL ok = SU_FALSE;
  struct suscan_inspector_overridable_request *req;

  SU_TRYCATCH(
      req = suscan_local_analyzer_acquire_overridable(analyzer, handle),
      goto done);

  req->bandwidth_request = SU_TRUE;
  req->new_bandwidth = bw;

  SU_TRYCATCH(
      suscan_local_analyzer_release_overridable(analyzer, req),
      goto done);

  ok = SU_TRUE;

done:

  return ok;
}

SUPRIVATE SUBOOL
suscan_local_analyzer_set_inspector_freq_cb(
    struct suscan_mq *mq_out,
    void *wk_private,
    void *cb_private)
{
  suscan_local_analyzer_t *analyzer = (suscan_local_analyzer_t *) wk_private;

  if (analyzer->inspector_freq_req) {
    analyzer->inspector_freq_req = SU_FALSE;
    (void) suscan_local_analyzer_set_inspector_freq_slow(
        analyzer,
        analyzer->inspector_freq_req_handle,
        analyzer->inspector_freq_req_value);
  }

  return SU_FALSE;
}

SUPRIVATE SUBOOL
suscan_local_analyzer_set_psd_params_cb(
    struct suscan_mq *mq_out,
    void *wk_private,
    void *cb_private)
{
  suscan_local_analyzer_t *self = (suscan_local_analyzer_t *) wk_private;

  if (self->psd_params_req) {
    self->psd_params_req = SU_FALSE;

    /* This alters detector params */
    self->parent->params.detector_params.window_size = self->sp_params.fft_size;
    self->parent->params.detector_params.window = self->sp_params.window;
    self->interval_psd = 1. / self->sp_params.refresh_rate;

    (void) su_smoothpsd_set_params(self->smooth_psd, &self->sp_params);
    SU_TRYCATCH(suscan_local_analyzer_notify_params(self), return SU_FALSE);
  }

  return SU_FALSE;
}

SUPRIVATE SUBOOL
suscan_local_analyzer_set_inspector_bandwidth_cb(
    struct suscan_mq *mq_out,
    void *wk_private,
    void *cb_private)
{
  suscan_local_analyzer_t *analyzer = (suscan_local_analyzer_t *) wk_private;

  if (analyzer->inspector_bw_req) {
    analyzer->inspector_bw_req = SU_FALSE;
    (void) suscan_local_analyzer_set_inspector_bandwidth_slow(
        analyzer,
        analyzer->inspector_bw_req_handle,
        analyzer->inspector_bw_req_value);
  }

  return SU_FALSE;
}
/****************************** Slow methods **********************************/
SUBOOL
suscan_local_analyzer_set_inspector_freq_overridable(
    suscan_local_analyzer_t *self,
    SUHANDLE handle,
    SUFREQ freq)
{
  SU_TRYCATCH(
      self->parent->params.mode == SUSCAN_ANALYZER_MODE_CHANNEL,
      return SU_FALSE);

  self->inspector_freq_req_handle = handle;
  self->inspector_freq_req_value  = freq;
  self->inspector_freq_req        = SU_TRUE;

  return suscan_worker_push(
      self->slow_wk,
      suscan_local_analyzer_set_inspector_freq_cb,
      NULL);
}

SUBOOL
suscan_local_analyzer_set_inspector_bandwidth_overridable(
    suscan_local_analyzer_t *self,
    SUHANDLE handle,
    SUFLOAT bw)
{
  SU_TRYCATCH(
      self->parent->params.mode == SUSCAN_ANALYZER_MODE_CHANNEL,
      return SU_FALSE);

  self->inspector_bw_req_handle = handle;
  self->inspector_bw_req_value  = bw;
  self->inspector_bw_req        = SU_TRUE;

  return suscan_worker_push(
      self->slow_wk,
      suscan_local_analyzer_set_inspector_bandwidth_cb,
      NULL);
}

SUBOOL
suscan_local_analyzer_set_analyzer_params_overridable(
    suscan_local_analyzer_t *self,
    const struct suscan_analyzer_params *params)
{
  SU_TRYCATCH(
      self->parent->params.mode == SUSCAN_ANALYZER_MODE_CHANNEL,
      return SU_FALSE);

  self->sp_params.fft_size     = params->detector_params.window_size;
  self->sp_params.window       = params->detector_params.window;
  self->sp_params.refresh_rate = 1. / params->psd_update_int;

  self->psd_params_req = SU_TRUE;

  return suscan_worker_push(
      self->slow_wk,
      suscan_local_analyzer_set_psd_params_cb,
      NULL);
}

SUBOOL
suscan_local_analyzer_set_psd_samp_rate_overridable(
    suscan_local_analyzer_t *self,
    SUSCOUNT throttle)
{
  SU_TRYCATCH(
      self->parent->params.mode == SUSCAN_ANALYZER_MODE_CHANNEL,
      return SU_FALSE);

  self->sp_params.samp_rate = throttle;
  self->psd_params_req = SU_TRUE;

  return suscan_worker_push(
      self->slow_wk,
      suscan_local_analyzer_set_psd_params_cb,
      NULL);
}

SUBOOL
suscan_local_analyzer_slow_set_freq(
    suscan_local_analyzer_t *self,
    SUFREQ freq,
    SUFREQ lnb)
{
  SU_TRYCATCH(
      self->parent->params.mode == SUSCAN_ANALYZER_MODE_CHANNEL,
      return SU_FALSE);

  self->freq_req_value = freq;
  self->lnb_req_value  = lnb;
  self->freq_req = SU_TRUE;

  /* This operation is rather slow. Do it somewhere else. */
  return suscan_worker_push(
      self->slow_wk,
      suscan_local_analyzer_set_freq_cb,
      NULL);
}

SUBOOL
suscan_local_analyzer_slow_set_dc_remove(
    suscan_local_analyzer_t *analyzer,
    SUBOOL remove)
{
  return suscan_worker_push(
        analyzer->slow_wk,
        suscan_local_analyzer_set_dc_remove_cb,
        (void *) (uintptr_t) remove);
}

SUBOOL
suscan_local_analyzer_slow_set_agc(
    suscan_local_analyzer_t *analyzer,
    SUBOOL set)
{
  return suscan_worker_push(
        analyzer->slow_wk,
        suscan_local_analyzer_set_agc_cb,
        (void *) (uintptr_t) set);
}

SUBOOL
suscan_local_analyzer_slow_set_antenna(
    suscan_local_analyzer_t *analyzer,
    const char *name)
{
  char *req = NULL;
  SUBOOL mutex_acquired = SU_FALSE;

  SU_TRYCATCH(req = strdup(name), goto fail);

  /* vvvvvvvvvvvvvvvvvv Acquire hotconf request mutex vvvvvvvvvvvvvvvvvvvvvvv */
  SU_TRYCATCH(
      pthread_mutex_lock(&analyzer->hotconf_mutex) != -1,
      goto fail);
  mutex_acquired = SU_TRUE;

  if (analyzer->antenna_req != NULL)
    free(analyzer->antenna_req);
  analyzer->antenna_req = req;
  req = NULL;

  pthread_mutex_unlock(&analyzer->hotconf_mutex);
  mutex_acquired = SU_FALSE;
  /* ^^^^^^^^^^^^^^^^^^ Release hotconf request mutex ^^^^^^^^^^^^^^^^^^^^^^^ */

  return suscan_worker_push(
      analyzer->slow_wk,
      suscan_local_analyzer_set_antenna_cb,
      NULL);

fail:
  if (mutex_acquired)
    pthread_mutex_unlock(&analyzer->hotconf_mutex);

  if (req != NULL)
    free(req);

  return SU_FALSE;
}

SUBOOL
suscan_local_analyzer_slow_set_bw(suscan_local_analyzer_t *analyzer, SUFLOAT bw)
{
  analyzer->bw_req_value = bw;
  analyzer->bw_req = SU_TRUE;

  /* This operation is rather slow. Do it somewhere else. */
  return suscan_worker_push(
      analyzer->slow_wk,
      suscan_local_analyzer_set_bw_cb,
      NULL);
}

SUBOOL
suscan_local_analyzer_slow_set_ppm(
    suscan_local_analyzer_t *analyzer,
    SUFLOAT ppm)
{
  analyzer->ppm_req_value = ppm;
  analyzer->ppm_req = SU_TRUE;

  return suscan_worker_push(
      analyzer->slow_wk,
      suscan_local_analyzer_set_ppm_cb,
      NULL);
}

SUBOOL
suscan_local_analyzer_slow_set_gain(
    suscan_local_analyzer_t *analyzer,
    const char *name,
    SUFLOAT value)
{
  struct suscan_analyzer_gain_info *req = NULL;
  SUBOOL mutex_acquired = SU_FALSE;

  SU_TRYCATCH(
      req = suscan_analyzer_gain_info_new_value_only(name, value),
      goto fail);

  /* vvvvvvvvvvvvvvvvvv Acquire hotconf request mutex vvvvvvvvvvvvvvvvvvvvvvv */
  SU_TRYCATCH(
      pthread_mutex_lock(&analyzer->hotconf_mutex) != -1,
      goto fail);
  mutex_acquired = SU_TRUE;

  SU_TRYCATCH(
      PTR_LIST_APPEND_CHECK(analyzer->gain_request, req) != -1,
      goto fail);
  req = NULL;

  pthread_mutex_unlock(&analyzer->hotconf_mutex);
  mutex_acquired = SU_FALSE;
  /* ^^^^^^^^^^^^^^^^^^ Release hotconf request mutex ^^^^^^^^^^^^^^^^^^^^^^^ */

  return suscan_worker_push(
      analyzer->slow_wk,
      suscan_local_analyzer_set_gain_cb,
      NULL);

fail:
  if (mutex_acquired)
    pthread_mutex_unlock(&analyzer->hotconf_mutex);

  if (req != NULL)
    suscan_analyzer_gain_info_destroy(req);

  return SU_FALSE;
}

