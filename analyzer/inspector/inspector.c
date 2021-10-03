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

#include "inspector/inspector.h"
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
suscan_inspector_destroy(suscan_inspector_t *insp)
{
  unsigned int i;

  pthread_mutex_destroy(&insp->mutex);
  pthread_mutex_destroy(&insp->corrector_mutex);

  if (insp->corrector != NULL)
    suscan_frequency_corrector_destroy(insp->corrector);

  if (insp->privdata != NULL)
    (insp->iface->close) (insp->privdata);

  for (i = 0; i < insp->estimator_count; ++i)
    suscan_estimator_destroy(insp->estimator_list[i]);

  if (insp->estimator_list != NULL)
    free(insp->estimator_list);

  for (i = 0; i < insp->spectsrc_count; ++i)
    suscan_spectsrc_destroy(insp->spectsrc_list[i]);

  if (insp->spectsrc_list != NULL)
    free(insp->spectsrc_list);

  free(insp);
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

  SU_TRYCATCH(
      src = suscan_spectsrc_new(
          class,
          insp->samp_info.equiv_fs,
          1. / insp->interval_spectrum,
          SUSCAN_INSPECTOR_SPECTRUM_BUF_SIZE,
          SU_CHANNEL_DETECTOR_WINDOW_BLACKMANN_HARRIS,
          suscan_inspector_on_spectrum_data,
          insp),
      goto fail);

  SU_TRYCATCH(PTR_LIST_APPEND_CHECK(insp->spectsrc, src) != -1, goto fail);

  return SU_TRUE;

fail:
  if (src != NULL)
    suscan_spectsrc_destroy(src);

  return SU_FALSE;
}

/*
 * TODO: Accurate sample rate and bandwidth can only be obtained after
 * the channel is opened. The recently created inspector must be updated
 * according to the specttuner channel opened in the analyzer.
 */
suscan_inspector_t *
suscan_inspector_new(
    const char *name,
    SUFLOAT fs,
    su_specttuner_channel_t *channel,
    struct suscan_mq *mq_out)
{
  suscan_inspector_t *new = NULL;
  const struct suscan_inspector_interface *iface = NULL;
  unsigned int i;

  if ((iface = suscan_inspector_interface_lookup(name)) == NULL) {
    SU_ERROR("Unknown inspector type: `%s'\n", name);
    goto fail;
  }

  SU_TRYCATCH(new = calloc(1, sizeof (suscan_inspector_t)), goto fail);

  new->state = SUSCAN_ASYNC_STATE_CREATED;
  new->mq_out = mq_out;

  SU_TRYCATCH(pthread_mutex_init(&new->mutex, NULL) == 0, goto fail);
  SU_TRYCATCH(pthread_mutex_init(&new->corrector_mutex, NULL) == 0, goto fail);

  /* Initialize sampling info */
  new->samp_info.schan = channel;
  new->samp_info.equiv_fs = fs / channel->decimation;
  new->samp_info.bw = SU_ANG2NORM_FREQ(
      .5 * channel->decimation * su_specttuner_channel_get_bw(channel));

  /* Spectrum and estimator updates */
  new->interval_estimator = .1;
  new->interval_spectrum  = .1;
  new->interval_estimator = .25;

  /* Initialize clocks */
  new->last_estimator = suscan_gettime();
  new->last_spectrum = suscan_gettime();

  /* All set to call specific inspector */
  new->iface = iface;
  SU_TRYCATCH(new->privdata = (iface->open) (&new->samp_info), goto fail);

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
  return (insp->iface->feed) (insp->privdata, insp, x, count);
}

SUBOOL
suscan_init_inspectors(void)
{
  SU_TRYCATCH(suscan_tle_corrector_init(), return SU_FALSE);

  SU_TRYCATCH(suscan_ask_inspector_register(), return SU_FALSE);
  SU_TRYCATCH(suscan_psk_inspector_register(), return SU_FALSE);
  SU_TRYCATCH(suscan_fsk_inspector_register(), return SU_FALSE);
  SU_TRYCATCH(suscan_audio_inspector_register(), return SU_FALSE);
  SU_TRYCATCH(suscan_raw_inspector_register(), return SU_FALSE);

  return SU_TRUE;
}

