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

#define SU_LOG_DOMAIN "suscan-inspector"

#include <sigutils/sigutils.h>
#include <sigutils/detect.h>
#include <sigutils/sampling.h>

#include "mq.h"
#include "msg.h"

#define SUSCAN_INSPECTOR_DEFAULT_ROLL_OFF .35
#define SUSCAN_INSPECTOR_MAX_MF_SPAN      1024

SUPRIVATE SUSCOUNT
suscan_inspector_mf_span(SUSCOUNT span)
{
  if (span > SUSCAN_INSPECTOR_MAX_MF_SPAN) {
    SU_WARNING(
        "Matched filter sample span too big (%d), truncating to %d\n",
        span, SUSCAN_INSPECTOR_MAX_MF_SPAN);
    span = SUSCAN_INSPECTOR_MAX_MF_SPAN;
  }

  return span;
}

SUPRIVATE void
suscan_inspector_params_lock(suscan_inspector_t *insp)
{
  (void) pthread_mutex_lock(&insp->params_mutex);
}

SUPRIVATE void
suscan_inspector_params_unlock(suscan_inspector_t *insp)
{
  (void) pthread_mutex_unlock(&insp->params_mutex);
}

SUPRIVATE void
suscan_inspector_request_params(
    suscan_inspector_t *insp,
    struct suscan_inspector_params *params_request)
{
  suscan_inspector_params_lock(insp);

  insp->params_request = *params_request;

  insp->params_requested = SU_TRUE;

  suscan_inspector_params_unlock(insp);
}

SUPRIVATE void
suscan_inspector_assert_params(suscan_inspector_t *insp)
{
  SUSCOUNT fs;
  SUBOOL mf_changed;
  su_iir_filt_t mf = su_iir_filt_INITIALIZER;

  if (insp->params_requested) {
    suscan_inspector_params_lock(insp);

    mf_changed =
        (insp->params.baud != insp->params_request.baud)
        || (insp->params.mf_rolloff != insp->params_request.mf_rolloff);
    insp->params = insp->params_request;

    fs = su_channel_detector_get_fs(insp->fac_baud_det);

    /* Update inspector according to params */
    if (insp->params.baud > 0)
      insp->sym_period = 1. / SU_ABS2NORM_BAUD(fs, insp->params.baud);
    else
      insp->sym_period = 0;

    /* Update local oscillator frequency and phase */
    su_ncqo_set_freq(
        &insp->lo,
        SU_ABS2NORM_FREQ(fs, insp->params.fc_off));
    insp->phase = SU_C_EXP(I * insp->params.fc_phi);

    /* Update baudrate */
    su_clock_detector_set_baud(
        &insp->cd,
        SU_ABS2NORM_BAUD(fs, insp->params.baud));

    insp->cd.alpha = insp->params.br_alpha;
    insp->cd.beta = insp->params.br_beta;

    /* Update matched filter */
    if (mf_changed) {
      if (!su_iir_rrc_init(
          &mf,
          suscan_inspector_mf_span(6 * insp->sym_period),
          insp->sym_period,
          insp->params.mf_rolloff)) {
        SU_ERROR("No memory left to update matched filter!\n");
      } else {
        su_iir_filt_finalize(&insp->mf);
        insp->mf = mf;
      }
    }

    insp->params_requested = SU_FALSE;

    suscan_inspector_params_unlock(insp);
  }
}

void
suscan_inspector_destroy(suscan_inspector_t *insp)
{
  pthread_mutex_destroy(&insp->params_mutex);

  if (insp->fac_baud_det != NULL)
    su_channel_detector_destroy(insp->fac_baud_det);

  if (insp->nln_baud_det != NULL)
    su_channel_detector_destroy(insp->nln_baud_det);

  su_agc_finalize(&insp->agc);

  su_costas_finalize(&insp->costas_2);

  su_costas_finalize(&insp->costas_4);

  su_clock_detector_finalize(&insp->cd);

  free(insp);
}

/*
 * Spike durations measured in symbol times
 * SUSCAN_INSPECTOR_FAST_RISE_FRAC has been doubled to reduce phase noise
 * induced by the non-linearity of the AGC
 */
#define SUSCAN_INSPECTOR_FAST_RISE_FRAC   (2 * 3.9062e-1)
#define SUSCAN_INSPECTOR_FAST_FALL_FRAC   (2 * SUSCAN_INSPECTOR_FAST_RISE_FRAC)
#define SUSCAN_INSPECTOR_SLOW_RISE_FRAC   (10 * SUSCAN_INSPECTOR_FAST_RISE_FRAC)
#define SUSCAN_INSPECTOR_SLOW_FALL_FRAC   (10 * SUSCAN_INSPECTOR_FAST_FALL_FRAC)
#define SUSCAN_INSPECTOR_HANG_MAX_FRAC    (SUSCAN_INSPECTOR_FAST_RISE_FRAC * 5)
#define SUSCAN_INSPECTOR_DELAY_LINE_FRAC  (SUSCAN_INSPECTOR_FAST_RISE_FRAC * 10)
#define SUSCAN_INSPECTOR_MAG_HISTORY_FRAC (SUSCAN_INSPECTOR_FAST_RISE_FRAC * 10)

void
suscan_inspector_params_initialize(struct suscan_inspector_params *params)
{
  memset(params, 0, sizeof (struct suscan_inspector_params));

  params->gc_ctrl = SUSCAN_INSPECTOR_GAIN_CONTROL_AUTOMATIC;
  params->gc_gain = 1;

  params->br_ctrl = SUSCAN_INSPECTOR_BAUDRATE_CONTROL_MANUAL;
  params->br_alpha = SU_PREFERED_CLOCK_ALPHA;
  params->br_beta  = SU_PREFERED_CLOCK_BETA;

  params->fc_ctrl = SUSCAN_INSPECTOR_CARRIER_CONTROL_MANUAL;

  params->mf_conf = SUSCAN_INSPECTOR_MATCHED_FILTER_BYPASS;
  params->mf_rolloff = SUSCAN_INSPECTOR_DEFAULT_ROLL_OFF;
}

suscan_inspector_t *
suscan_inspector_new(
    const suscan_analyzer_t *analyzer,
    const struct sigutils_channel *channel)
{
  suscan_inspector_t *new;
  struct sigutils_channel_detector_params params =
      sigutils_channel_detector_params_INITIALIZER;
  struct su_agc_params agc_params = su_agc_params_INITIALIZER;
  SUFLOAT tau;

  SU_TRYCATCH(new = calloc(1, sizeof (suscan_inspector_t)), goto fail);

  new->state = SUSCAN_ASYNC_STATE_CREATED;

  /* Initialize inspector parameters */
  SU_TRYCATCH(pthread_mutex_init(&new->params_mutex, NULL) != -1, goto fail);

  suscan_inspector_params_initialize(&new->params);

  /* Common channel parameters */
  su_channel_params_adjust_to_channel(&params, channel);

  params.samp_rate = su_channel_detector_get_fs(analyzer->source.detector);
  params.window_size = SUSCAN_SOURCE_DEFAULT_BUFSIZ;
  params.alpha = 1e-4;

  /* Initialize spectrum parameters */
  new->interval_psd = .1;

  /* Create generic autocorrelation-based detector */
  params.mode = SU_CHANNEL_DETECTOR_MODE_AUTOCORRELATION;
  SU_TRYCATCH(new->fac_baud_det = su_channel_detector_new(&params), goto fail);

  /* Create non-linear baud rate detector */
  params.mode = SU_CHANNEL_DETECTOR_MODE_NONLINEAR_DIFF;
  SU_TRYCATCH(new->nln_baud_det = su_channel_detector_new(&params), goto fail);

  /* Create clock detector */
  SU_TRYCATCH(
      su_clock_detector_init(
          &new->cd,
          1.,
          .5 * SU_ABS2NORM_BAUD(params.samp_rate, params.bw),
          32),
      goto fail);

  /* Initialize local oscillator */
  su_ncqo_init(&new->lo, 0);
  new->phase = 1.;

  /* Initialize AGC */
  tau = params.samp_rate / params.bw; /* Samples per symbol */

  agc_params.fast_rise_t = tau * SUSCAN_INSPECTOR_FAST_RISE_FRAC;
  agc_params.fast_fall_t = tau * SUSCAN_INSPECTOR_FAST_FALL_FRAC;
  agc_params.slow_rise_t = tau * SUSCAN_INSPECTOR_SLOW_RISE_FRAC;
  agc_params.slow_fall_t = tau * SUSCAN_INSPECTOR_SLOW_FALL_FRAC;
  agc_params.hang_max    = tau * SUSCAN_INSPECTOR_HANG_MAX_FRAC;

  /* TODO: Check whether these sizes are too big */
  agc_params.delay_line_size  = tau * SUSCAN_INSPECTOR_DELAY_LINE_FRAC;
  agc_params.mag_history_size = tau * SUSCAN_INSPECTOR_MAG_HISTORY_FRAC;

  SU_TRYCATCH(su_agc_init(&new->agc, &agc_params), goto fail);

  /* Initialize matched filter, with T = tau */
  SU_TRYCATCH(
      su_iir_rrc_init(
          &new->mf,
          suscan_inspector_mf_span(6 * tau),
          tau,
          new->params.mf_rolloff),
      goto fail);

  /* Initialize PLLs */
  SU_TRYCATCH(
      su_costas_init(
          &new->costas_2,
          SU_COSTAS_KIND_BPSK,
          0,
          SU_ABS2NORM_FREQ(params.samp_rate, params.bw),
          3,
          1e-2 * SU_ABS2NORM_FREQ(params.samp_rate, params.bw)),
      goto fail);

  SU_TRYCATCH(
      su_costas_init(
          &new->costas_4,
          SU_COSTAS_KIND_QPSK,
          0,
          SU_ABS2NORM_FREQ(params.samp_rate, params.bw),
          3,
          1e-2 * SU_ABS2NORM_FREQ(params.samp_rate, params.bw)),
      goto fail);

  return new;

fail:
  if (new != NULL)
    suscan_inspector_destroy(new);

  return NULL;
}

int
suscan_inspector_feed_bulk(
    suscan_inspector_t *insp,
    const SUCOMPLEX *x,
    int count)
{
  int i;
  SUFLOAT alpha;
  SUCOMPLEX det_x;
  SUCOMPLEX sample;
  SUCOMPLEX samp_phase_samples = insp->params.sym_phase * insp->sym_period;
  SUBOOL ok = SU_FALSE;

  insp->sym_new_sample = SU_FALSE;

  for (i = 0; i < count && !insp->sym_new_sample; ++i) {
    /*
     * Feed channel detectors. TODO: use su_channel_detector_get_last_sample
     * with nln_baud_det.
     */
    SU_TRYCATCH(
        su_channel_detector_feed(insp->fac_baud_det, x[i]),
        goto done);
    SU_TRYCATCH(
        su_channel_detector_feed(insp->nln_baud_det, x[i]),
        goto done);

    det_x = su_channel_detector_get_last_sample(insp->fac_baud_det);

    /* Re-center carrier */
    det_x *= SU_C_CONJ(su_ncqo_read(&insp->lo)) * insp->phase;

    /* Perform gain control */
    switch (insp->params.gc_ctrl) {
      case SUSCAN_INSPECTOR_GAIN_CONTROL_MANUAL:
        det_x *= 2 * insp->params.gc_gain;
        break;

      case SUSCAN_INSPECTOR_GAIN_CONTROL_AUTOMATIC:
        det_x  = 2 * su_agc_feed(&insp->agc, det_x) * 1.4142;
        break;
    }

    /* Perform frequency correction */
    switch (insp->params.fc_ctrl) {
      case SUSCAN_INSPECTOR_CARRIER_CONTROL_MANUAL:
        sample = det_x;
        break;

      case SUSCAN_INSPECTOR_CARRIER_CONTROL_COSTAS_2:
        su_costas_feed(&insp->costas_2, det_x);
        sample = insp->costas_2.y;
        break;

      case SUSCAN_INSPECTOR_CARRIER_CONTROL_COSTAS_4:
        su_costas_feed(&insp->costas_4, det_x);
        sample = insp->costas_4.y;
        break;
    }

    /* Add matched filter, if enabled */
    if (insp->params.mf_conf == SUSCAN_INSPECTOR_MATCHED_FILTER_MANUAL)
      sample = su_iir_filt_feed(&insp->mf, sample);

    /* Check if channel sampler is enabled */
    if (insp->params.br_ctrl == SUSCAN_INSPECTOR_BAUDRATE_CONTROL_MANUAL) {
      if (insp->sym_period >= 1.) {
        insp->sym_phase += 1.;
        if (insp->sym_phase >= insp->sym_period)
          insp->sym_phase -= insp->sym_period;

        insp->sym_new_sample =
            (int) SU_FLOOR(insp->sym_phase - samp_phase_samples) == 0;

        if (insp->sym_new_sample) {
          alpha = insp->sym_phase - SU_FLOOR(insp->sym_phase);

          insp->sym_sampler_output =
              .5 * ((1 - alpha) * insp->sym_last_sample + alpha * sample);

        }
      }
      insp->sym_last_sample = sample;
    } else {
      /* Automatic baudrate control enabled */
      su_clock_detector_feed(&insp->cd, sample);

      insp->sym_new_sample = su_clock_detector_read(&insp->cd, &sample, 1) == 1;
      if (insp->sym_new_sample)
        insp->sym_sampler_output = .5 * sample;
    }
  }

  ok = SU_TRUE;

done:
  return ok ? i : -1;
}

/*
 * TODO: Store *one port* only per worker. This port is read once all
 * consumers have finished with their buffer.
 */
SUPRIVATE SUBOOL
suscan_inspector_wk_cb(
    struct suscan_mq *mq_out,
    void *wk_private,
    void *cb_private)
{
  suscan_consumer_t *consumer = (suscan_consumer_t *) wk_private;
  suscan_inspector_t *insp = (suscan_inspector_t *) cb_private;
  unsigned int i;
  int fed;
  SUSCOUNT samp_count;
  const SUCOMPLEX *samp_buf;
  struct suscan_analyzer_sample_batch_msg *batch_msg = NULL;
  SUBOOL restart = SU_FALSE;

  samp_buf   = suscan_consumer_get_buffer(consumer);
  samp_count = suscan_consumer_get_buffer_size(consumer);

  insp->per_cnt_psd += samp_count;

  while (samp_count > 0) {
    /* Ensure the current inspector parameters are up-to-date */
    suscan_inspector_assert_params(insp);

    SU_TRYCATCH(
        (fed = suscan_inspector_feed_bulk(insp, samp_buf, samp_count)) >= 0,
        goto done);

    if (insp->sym_new_sample) {
      /* Sampler was triggered */
      if (batch_msg == NULL)
        SU_TRYCATCH(
            batch_msg = suscan_analyzer_sample_batch_msg_new(
                insp->params.inspector_id),
            goto done);

      SU_TRYCATCH(
          suscan_analyzer_sample_batch_msg_append_sample(
              batch_msg,
              insp->sym_sampler_output),
          goto done);

    }

    samp_buf   += fed;
    samp_count -= fed;
  }

  /* Check spectrum update */
  if (insp->interval_psd > 0)
    if (insp->per_cnt_psd
        >= insp->interval_psd
        * su_channel_detector_get_fs(insp->fac_baud_det)) {
      insp->per_cnt_psd = 0;

      switch (insp->params.psd_source) {
        case SUSCAN_INSPECTOR_PSD_SOURCE_FAC:
          if (!suscan_inspector_send_psd(insp, consumer, insp->fac_baud_det))
            goto done;
          break;

        case SUSCAN_INSPECTOR_PSD_SOURCE_NLN:
          if (!suscan_inspector_send_psd(insp, consumer, insp->nln_baud_det))
            goto done;
          break;

        default:
          /* Prevent warnings */
          break;
      }
    }

  /* Got samples, send message batch */
  if (batch_msg != NULL) {
    SU_TRYCATCH(
        suscan_mq_write(
            consumer->analyzer->mq_out,
            SUSCAN_ANALYZER_MESSAGE_TYPE_SAMPLES,
            batch_msg),
        goto done);
    batch_msg = NULL;
  }

  restart = insp->state == SUSCAN_ASYNC_STATE_RUNNING;

done:
  if (!restart) {
    insp->state = SUSCAN_ASYNC_STATE_HALTED;
    suscan_consumer_remove_task(consumer);
  }

  if (batch_msg != NULL)
    suscan_analyzer_sample_batch_msg_destroy(batch_msg);

  return restart;
}

SUINLINE suscan_inspector_t *
suscan_analyzer_get_inspector(
    const suscan_analyzer_t *analyzer,
    SUHANDLE handle)
{
  suscan_inspector_t *brinsp;

  if (handle < 0 || handle >= analyzer->inspector_count)
    return NULL;

  brinsp = analyzer->inspector_list[handle];

  if (brinsp != NULL && brinsp->state != SUSCAN_ASYNC_STATE_RUNNING)
    return NULL;

  return brinsp;
}

SUPRIVATE SUBOOL
suscan_analyzer_dispose_inspector_handle(
    suscan_analyzer_t *analyzer,
    SUHANDLE handle)
{
  if (handle < 0 || handle >= analyzer->inspector_count)
    return SU_FALSE;

  if (analyzer->inspector_list[handle] == NULL)
    return SU_FALSE;

  analyzer->inspector_list[handle] = NULL;

  return SU_TRUE;
}

SUPRIVATE SUHANDLE
suscan_analyzer_register_inspector(
    suscan_analyzer_t *analyzer,
    suscan_inspector_t *brinsp)
{
  SUHANDLE hnd;

  if (brinsp->state != SUSCAN_ASYNC_STATE_CREATED)
    return SU_FALSE;

  /* Plugged. Append handle to list */
  /* TODO: Find inspectors in HALTED state, and free them */
  if ((hnd = PTR_LIST_APPEND_CHECK(analyzer->inspector, brinsp)) == -1)
    return -1;

  /* Mark it as running and push to worker */
  brinsp->state = SUSCAN_ASYNC_STATE_RUNNING;

  if (!suscan_analyzer_push_task(
      analyzer,
      suscan_inspector_wk_cb,
      brinsp)) {
    suscan_analyzer_dispose_inspector_handle(analyzer, hnd);
    return -1;
  }

  return hnd;
}

/*
 * We have ownership on msg, this messages are urgent: they are placed
 * in the beginning of the queue
 */

/*
 * TODO: Protect access to inspector object!
 */

SUBOOL
suscan_analyzer_parse_inspector_msg(
    suscan_analyzer_t *analyzer,
    struct suscan_analyzer_inspector_msg *msg)
{
  suscan_inspector_t *new = NULL;
  suscan_inspector_t *insp = NULL;
  SUHANDLE handle = -1;
  SUBOOL ok = SU_FALSE;
  SUBOOL update_baud;

  switch (msg->kind) {
    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_OPEN:
      if ((new = suscan_inspector_new(
          analyzer,
          &msg->channel)) == NULL)
        goto done;

      handle = suscan_analyzer_register_inspector(analyzer, new);
      if (handle == -1)
        goto done;
      new = NULL;

      msg->handle = handle;
      break;

    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_GET_INFO:
      if ((insp = suscan_analyzer_get_inspector(
          analyzer,
          msg->handle)) == NULL) {
        /* No such handle */
        msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_HANDLE;
      } else {
        /* Retrieve current esimate for message kind */
        msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_INFO;
        msg->baud.fac = insp->fac_baud_det->baud;
        msg->baud.nln = insp->nln_baud_det->baud;
      }
      break;

    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_GET_PARAMS:
      if ((insp = suscan_analyzer_get_inspector(
          analyzer,
          msg->handle)) == NULL) {
        /* No such handle */
        msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_HANDLE;
      } else {
        /* Retrieve current inspector params */
        msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_PARAMS;
        msg->params = insp->params;
      }
      break;

    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_PARAMS:
      if ((insp = suscan_analyzer_get_inspector(
          analyzer,
          msg->handle)) == NULL) {
        /* No such handle */
        msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_HANDLE;
      } else {
        /* Store the parameter update request */
        suscan_inspector_request_params(insp, &msg->params);
      }
      break;

    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_CLOSE:
      if ((insp = suscan_analyzer_get_inspector(
          analyzer,
          msg->handle)) == NULL) {
        msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_HANDLE;
      } else {
        msg->inspector_id = insp->params.inspector_id;

        if (insp->state == SUSCAN_ASYNC_STATE_HALTED) {
          /*
           * Inspector has been halted. It's safe to dispose the handle
           * and free the object.
           */
          (void) suscan_analyzer_dispose_inspector_handle(
              analyzer,
              msg->handle);
          suscan_inspector_destroy(insp);
        } else {
          /*
           * Inspector is still running. Mark it as halting, so it will not
           * come back to the worker queue.
           */
          insp->state = SUSCAN_ASYNC_STATE_HALTING;
        }

        /* We can't trust the inspector contents from here on out */
        insp = NULL;
      }
      break;

    default:
      msg->status = msg->kind;
      msg->kind = SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_KIND;
  }

  /*
   * If request has referenced an existing inspector, we include the
   * inspector ID in the response.
   */
  if (insp != NULL)
    msg->inspector_id = insp->params.inspector_id;

  if (!suscan_mq_write(
      analyzer->mq_out,
      SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR,
      msg))
    goto done;

  ok = SU_TRUE;

done:
  if (new != NULL)
    suscan_inspector_destroy(new);

  return ok;
}

/************************* Channel inspector API ****************************/
SUBOOL
suscan_inspector_open_async(
    suscan_analyzer_t *analyzer,
    const struct sigutils_channel *channel,
    uint32_t req_id)
{
  struct suscan_analyzer_inspector_msg *req = NULL;
  uint32_t type;
  SUBOOL ok = SU_FALSE;

  if ((req = suscan_analyzer_inspector_msg_new(
      SUSCAN_ANALYZER_INSPECTOR_MSGKIND_OPEN,
      req_id)) == NULL) {
    SU_ERROR("Failed to craft open message\n");
    goto done;
  }

  req->channel = *channel;

  if (!suscan_analyzer_write(
      analyzer,
      SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR,
      req)) {
    SU_ERROR("Failed to send open command\n");
    goto done;
  }

  req = NULL; /* Now it belongs to the queue */

  ok = SU_TRUE;

done:
  if (req != NULL)
    suscan_analyzer_inspector_msg_destroy(req);

  return ok;
}

SUHANDLE
suscan_inspector_open(
    suscan_analyzer_t *analyzer,
    const struct sigutils_channel *channel)
{
  struct suscan_analyzer_inspector_msg *resp = NULL;
  uint32_t req_id = rand();
  SUHANDLE handle = -1;

  SU_TRYCATCH(
      suscan_inspector_open_async(analyzer, channel, req_id),
      goto done);

  SU_TRYCATCH(
      resp = suscan_analyzer_read_inspector_msg(analyzer),
      goto done);

  if (resp->req_id != req_id) {
    SU_ERROR("Unmatched response received\n");
    goto done;
  } else if (resp->kind != SUSCAN_ANALYZER_INSPECTOR_MSGKIND_OPEN) {
    SU_ERROR("Unexpected message kind\n");
    goto done;
  }

  handle = resp->handle;

done:
  if (resp != NULL)
    suscan_analyzer_inspector_msg_destroy(resp);

  return handle;
}

SUBOOL
suscan_inspector_close_async(
    suscan_analyzer_t *analyzer,
    SUHANDLE handle,
    uint32_t req_id)
{
  struct suscan_analyzer_inspector_msg *req = NULL;
  uint32_t type;
  SUBOOL ok = SU_FALSE;

  if ((req = suscan_analyzer_inspector_msg_new(
      SUSCAN_ANALYZER_INSPECTOR_MSGKIND_CLOSE,
      req_id)) == NULL) {
    SU_ERROR("Failed to craft close message\n");
    goto done;
  }
  req->handle = handle;

  if (!suscan_analyzer_write(
      analyzer,
      SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR,
      req)) {
    SU_ERROR("Failed to send close command\n");
    goto done;
  }

  req = NULL;

  ok = SU_TRUE;

done:
  if (req != NULL)
    suscan_analyzer_inspector_msg_destroy(req);

  return ok;
}

SUBOOL
suscan_inspector_close(
    suscan_analyzer_t *analyzer,
    SUHANDLE handle)
{

  struct suscan_analyzer_inspector_msg *resp = NULL;
  uint32_t req_id = rand();
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      suscan_inspector_close_async(analyzer, handle, req_id),
      goto done);

  SU_TRYCATCH(
      resp = suscan_analyzer_read_inspector_msg(analyzer),
      goto done);

  if (resp->req_id != req_id) {
    SU_ERROR("Unmatched response received\n");
    goto done;
  }

  if (resp->kind == SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_HANDLE) {
    SU_WARNING("Wrong handle passed to analyzer\n");
    goto done;
  } else if (resp->kind != SUSCAN_ANALYZER_INSPECTOR_MSGKIND_CLOSE) {
    SU_ERROR("Unexpected message kind\n");
    goto done;
  }

  ok = SU_TRUE;

done:
  if (resp != NULL)
    suscan_analyzer_inspector_msg_destroy(resp);

  return ok;
}

SUBOOL
suscan_inspector_get_info_async(
    suscan_analyzer_t *analyzer,
    SUHANDLE handle,
    uint32_t req_id)
{
  struct suscan_analyzer_inspector_msg *req = NULL;
  uint32_t type;
  SUBOOL ok = SU_FALSE;

  if ((req = suscan_analyzer_inspector_msg_new(
      SUSCAN_ANALYZER_INSPECTOR_MSGKIND_GET_INFO,
      req_id)) == NULL) {
    SU_ERROR("Failed to craft get_info message\n");
    goto done;
  }

  req->handle = handle;

  if (!suscan_analyzer_write(
      analyzer,
      SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR,
      req)) {
    SU_ERROR("Failed to send get_info command\n");
    goto done;
  }

  req = NULL;

  ok = SU_TRUE;

done:
  if (req != NULL)
    suscan_analyzer_inspector_msg_destroy(req);

  return ok;
}

SUBOOL
suscan_inspector_get_info(
    suscan_analyzer_t *analyzer,
    SUHANDLE handle,
    struct suscan_baud_det_result *result)
{
  struct suscan_analyzer_inspector_msg *resp = NULL;
  uint32_t req_id = rand();
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      suscan_inspector_get_info_async(analyzer, handle, req_id),
      goto done);

  SU_TRYCATCH(
      resp = suscan_analyzer_read_inspector_msg(analyzer),
      goto done);

  if (resp->req_id != req_id) {
    SU_ERROR("Unmatched response received\n");
    goto done;
  }

  if (resp->kind == SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_HANDLE) {
    SU_WARNING("Wrong handle passed to analyzer\n");
    goto done;
  } else if (resp->kind != SUSCAN_ANALYZER_INSPECTOR_MSGKIND_INFO) {
    SU_ERROR("Unexpected message kind %d\n", resp->kind);
    goto done;
  }

  *result = resp->baud;

  ok = SU_TRUE;

done:
  if (resp != NULL)
    suscan_analyzer_inspector_msg_destroy(resp);

  return ok;
}

SUBOOL
suscan_inspector_set_params_async(
    suscan_analyzer_t *analyzer,
    SUHANDLE handle,
    const struct suscan_inspector_params *params,
    uint32_t req_id)
{
  struct suscan_analyzer_inspector_msg *req = NULL;
  uint32_t type;
  SUBOOL ok = SU_FALSE;

  if ((req = suscan_analyzer_inspector_msg_new(
      SUSCAN_ANALYZER_INSPECTOR_MSGKIND_PARAMS,
      req_id)) == NULL) {
    SU_ERROR("Failed to craft get_info message\n");
    goto done;
  }

  req->handle = handle;
  req->params = *params;

  if (!suscan_analyzer_write(
      analyzer,
      SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR,
      req)) {
    SU_ERROR("Failed to send set_params command\n");
    goto done;
  }

  req = NULL;

  ok = SU_TRUE;

done:
  if (req != NULL)
    suscan_analyzer_inspector_msg_destroy(req);

  return ok;
}

