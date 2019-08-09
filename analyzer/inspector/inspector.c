/*

  Copyright (C) 2018 Gonzalo José Carracedo Carballal

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
#include <sigutils/sampling.h>

#include "inspector/inspector.h"

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

void
suscan_inspector_assert_params(suscan_inspector_t *insp)
{
  if (insp->params_requested) {
    suscan_inspector_lock(insp);

    (insp->iface->commit_config) (insp->privdata);

    insp->params_requested = SU_FALSE;

    suscan_inspector_unlock(insp);
  }
}

void
suscan_inspector_destroy(suscan_inspector_t *insp)
{
  unsigned int i;

  pthread_mutex_destroy(&insp->mutex);

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
suscan_inspector_add_spectsrc(
    suscan_inspector_t *insp,
    const struct suscan_spectsrc_class *class)
{
  suscan_spectsrc_t *src = NULL;

  SU_TRYCATCH(
      src = suscan_spectsrc_new(
          class,
          SUSCAN_INSPECTOR_SPECTRUM_BUF_SIZE,
          SU_CHANNEL_DETECTOR_WINDOW_BLACKMANN_HARRIS),
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
    const su_specttuner_channel_t *channel)
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

  SU_TRYCATCH(pthread_mutex_init(&new->mutex, NULL) != -1, goto fail);

  /* Initialize sampling info */
  new->samp_info.schan = channel;
  new->samp_info.equiv_fs = fs / channel->decimation;
  new->samp_info.bw = SU_ANG2NORM_FREQ(su_specttuner_channel_get_bw(channel));

  /* Spectrum and estimator updates */
  new->interval_estimator = .1 * new->samp_info.equiv_fs;
  new->interval_spectrum  = .1 * new->samp_info.equiv_fs;

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
  SU_TRYCATCH(suscan_ask_inspector_register(), return SU_FALSE);
  SU_TRYCATCH(suscan_psk_inspector_register(), return SU_FALSE);
  SU_TRYCATCH(suscan_fsk_inspector_register(), return SU_FALSE);
  SU_TRYCATCH(suscan_audio_inspector_register(), return SU_FALSE);

  return SU_TRUE;
}

