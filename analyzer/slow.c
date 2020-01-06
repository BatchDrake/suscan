/*

  Copyright (C) 2019 Gonzalo José Carracedo Carballal

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

#define SU_LOG_DOMAIN "slow-worker"

#include "analyzer.h"
#include <string.h>

/*
 * Some tasks take some time to complete, time that is several orders of
 * magnitude beyond what it takes to process a block of samples. Instead
 * of processing them directly in the source thread (which is quite busy
 * already), we create a separate worker (namely the slow worker) which takes
 * these tasks that are usually human-triggered and whose completion time is
 * not critical.
 */

SUPRIVATE void
suscan_analyzer_gain_request_destroy(struct suscan_analyzer_gain_request *req)
{
  if (req->name != NULL)
    free(req->name);

  free(req);
}

SUPRIVATE struct suscan_analyzer_gain_request *
suscan_analyzer_gain_request_new(const char *name, SUFLOAT value)
{
  struct suscan_analyzer_gain_request *new = NULL;

  SU_TRYCATCH(
      new = calloc(1, sizeof(struct suscan_analyzer_gain_request)),
      goto fail);

  SU_TRYCATCH(new->name = strdup(name), goto fail);
  new->value = value;

  return new;

fail:
  if (new != NULL)
    suscan_analyzer_gain_request_destroy(new);

  return NULL;
}


void
suscan_analyzer_destroy_slow_worker_data(suscan_analyzer_t *analyzer)
{
  unsigned int i;

  /* Delete all pending gain requessts */
  for (i = 0; i < analyzer->gain_request_count; ++i)
    suscan_analyzer_gain_request_destroy(analyzer->gain_request_list[i]);

  if (analyzer->gain_request_list != NULL)
    free(analyzer->gain_request_list);

  if (analyzer->gain_req_mutex_init)
    pthread_mutex_destroy(&analyzer->hotconf_mutex);

  if (analyzer->antenna_req != NULL)
    free(analyzer->antenna_req);
}

/***************************** Slow worker callbacks *************************/
SUPRIVATE SUBOOL
suscan_analyzer_set_gain_cb(
    struct suscan_mq *mq_out,
    void *wk_private,
    void *cb_private)
{
  suscan_analyzer_t *analyzer = (suscan_analyzer_t *) wk_private;
  SUBOOL mutex_acquired = SU_FALSE;
  PTR_LIST_LOCAL(struct suscan_analyzer_gain_request, request);
  unsigned int i;

  /* vvvvvvvvvvvvvvvvvv Acquire hotconf request mutex vvvvvvvvvvvvvvvvvvvvvvvvv */
  SU_TRYCATCH(pthread_mutex_lock(&analyzer->hotconf_mutex) != -1, goto fail);
  mutex_acquired = SU_TRUE;

  request_list  = analyzer->gain_request_list;
  request_count = analyzer->gain_request_count;

  analyzer->gain_request_list  = NULL;
  analyzer->gain_request_count = 0;

  pthread_mutex_unlock(&analyzer->hotconf_mutex);
  mutex_acquired = SU_FALSE;
  /* ^^^^^^^^^^^^^^^^^^ Release hotconf request mutex ^^^^^^^^^^^^^^^^^^^^^^^^^ */

  /* Process all requests */
  for (i = 0; i < request_count; ++i) {
    SU_TRYCATCH(
        suscan_source_set_gain(
            analyzer->source,
            request_list[i]->name,
            request_list[i]->value),
        goto fail);
  }

fail:
  if (mutex_acquired)
    pthread_mutex_unlock(&analyzer->hotconf_mutex);

  for (i = 0; i < request_count; ++i)
    suscan_analyzer_gain_request_destroy(request_list[i]);

  if (request_list != NULL)
    free(request_list);

  return SU_FALSE;
}

SUPRIVATE SUBOOL
suscan_analyzer_set_antenna_cb(
    struct suscan_mq *mq_out,
    void *wk_private,
    void *cb_private)
{
  suscan_analyzer_t *analyzer = (suscan_analyzer_t *) wk_private;
  SUBOOL mutex_acquired = SU_FALSE;
  char *req = NULL;
  unsigned int i;

  /* vvvvvvvvvvvvvvvvvv Acquire hotconf request mutex vvvvvvvvvvvvvvvvvvvvvvvvv */
  SU_TRYCATCH(pthread_mutex_lock(&analyzer->hotconf_mutex) != -1, goto fail);
  mutex_acquired = SU_TRUE;

  req = analyzer->antenna_req;
  analyzer->antenna_req = NULL;

  pthread_mutex_unlock(&analyzer->hotconf_mutex);
  mutex_acquired = SU_FALSE;
  /* ^^^^^^^^^^^^^^^^^^ Release hotconf request mutex ^^^^^^^^^^^^^^^^^^^^^^^^^ */

  suscan_source_set_antenna(analyzer->source, req);

fail:
  if (mutex_acquired)
    pthread_mutex_unlock(&analyzer->hotconf_mutex);

  if (req != NULL)
    free(req);

  return SU_FALSE;
}

SUPRIVATE SUBOOL
suscan_analyzer_set_dc_remove_cb(
    struct suscan_mq *mq_out,
    void *wk_private,
    void *cb_private)
{
  suscan_analyzer_t *analyzer = (suscan_analyzer_t *) wk_private;
  SUBOOL remove = (SUBOOL) (uintptr_t) cb_private;

  (void) suscan_source_set_dc_remove(analyzer->source, remove);

  return SU_FALSE;
}

SUPRIVATE SUBOOL
suscan_analyzer_set_agc_cb(
    struct suscan_mq *mq_out,
    void *wk_private,
    void *cb_private)
{
  suscan_analyzer_t *analyzer = (suscan_analyzer_t *) wk_private;
  SUBOOL set = (SUBOOL) (uintptr_t) cb_private;

  (void) suscan_source_set_agc(analyzer->source, set);

  return SU_FALSE;
}

SUPRIVATE SUBOOL
suscan_analyzer_set_bw_cb(
    struct suscan_mq *mq_out,
    void *wk_private,
    void *cb_private)
{
  suscan_analyzer_t *analyzer = (suscan_analyzer_t *) wk_private;
  SUFLOAT bw;

  if (analyzer->bw_req) {
    bw = analyzer->bw_req_value;
    if (suscan_source_set_bandwidth(analyzer->source, bw)) {
      /* XXX: Use a proper frequency adjust method */
      analyzer->detector->params.bw = bw;
    }
    analyzer->bw_req = analyzer->bw_req_value != bw;
  }

  return SU_FALSE;
}

SUPRIVATE SUBOOL
suscan_analyzer_set_freq_cb(
    struct suscan_mq *mq_out,
    void *wk_private,
    void *cb_private)
{
  suscan_analyzer_t *analyzer = (suscan_analyzer_t *) wk_private;
  SUFREQ freq;
  SUFREQ lnb_freq;

  if (analyzer->freq_req) {
    freq = analyzer->freq_req_value;
    lnb_freq = analyzer->lnb_req_value;
    if (suscan_source_set_freq2(analyzer->source, freq, lnb_freq)) {
      /* XXX: Use a proper frequency adjust method */
      analyzer->detector->params.fc = freq;
    }
    analyzer->freq_req = (analyzer->freq_req_value != freq ||
        analyzer->lnb_req_value != lnb_freq);
  }

  return SU_FALSE;
}

/****************************** Slow methods **********************************/
SUBOOL
suscan_analyzer_set_freq(suscan_analyzer_t *self, SUFREQ freq, SUFREQ lnb)
{
  SU_TRYCATCH(
      self->params.mode == SUSCAN_ANALYZER_MODE_CHANNEL,
      return SU_FALSE);

  self->freq_req_value = freq;
  self->lnb_req_value  = lnb;
  self->freq_req = SU_TRUE;

  /* This operation is rather slow. Do it somewhere else. */
  return suscan_worker_push(
      self->slow_wk,
      suscan_analyzer_set_freq_cb,
      NULL);
}

SUBOOL
suscan_analyzer_set_dc_remove(suscan_analyzer_t *analyzer, SUBOOL remove)
{
  return suscan_worker_push(
        analyzer->slow_wk,
        suscan_analyzer_set_dc_remove_cb,
        (void *) (uintptr_t) remove);
}

SUBOOL
suscan_analyzer_set_agc(suscan_analyzer_t *analyzer, SUBOOL set)
{
  return suscan_worker_push(
        analyzer->slow_wk,
        suscan_analyzer_set_agc_cb,
        (void *) (uintptr_t) set);
}

SUBOOL
suscan_analyzer_set_antenna(
    suscan_analyzer_t *analyzer,
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
      suscan_analyzer_set_antenna_cb,
      NULL);

fail:
  if (mutex_acquired)
    pthread_mutex_unlock(&analyzer->hotconf_mutex);

  if (req != NULL)
    free(req);

  return SU_FALSE;
}

SUBOOL
suscan_analyzer_set_bw(suscan_analyzer_t *analyzer, SUFLOAT bw)
{
  analyzer->bw_req_value = bw;
  analyzer->bw_req = SU_TRUE;

  /* This operation is rather slow. Do it somewhere else. */
  return suscan_worker_push(
      analyzer->slow_wk,
      suscan_analyzer_set_bw_cb,
      NULL);
}

SUBOOL
suscan_analyzer_set_gain(
    suscan_analyzer_t *analyzer,
    const char *name,
    SUFLOAT value)
{
  struct suscan_analyzer_gain_request *req = NULL;
  SUBOOL mutex_acquired = SU_FALSE;

  SU_TRYCATCH(req = suscan_analyzer_gain_request_new(name, value), goto fail);

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
      suscan_analyzer_set_gain_cb,
      NULL);

fail:
  if (mutex_acquired)
    pthread_mutex_unlock(&analyzer->hotconf_mutex);

  if (req != NULL)
    suscan_analyzer_gain_request_destroy(req);

  return SU_FALSE;
}

