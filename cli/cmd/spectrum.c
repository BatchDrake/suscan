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

#define SU_LOG_DOMAIN "cli-spectrum"

#include <sigutils/log.h>
#include <sigutils/sampling.h>

#include <analyzer/analyzer.h>
#include <analyzer/source.h>
#include <analyzer/msg.h>
#include <signal.h>

#include <cli/cli.h>
#include <cli/cmds.h>

enum suscli_spectrum_state {
  SUSCLI_SPECTRUM_STARTUP,
  SUSCLI_SPECTRUM_CONFIGURING,
  SUSCLI_SPECTRUM_ACQUIRING
};

struct suscli_spectrum_params {
  suscan_source_config_t *profile;
  SUFLOAT                 psd_rate;
  uint32_t                fft_size;
};

#define suscli_spectrum_params_INITIALIZER {  \
  NULL,  /* profile  */                       \
  25,    /* psd_rate */                       \
  65536, /* fft_size */                       \
}

struct suscli_spectrum {
  struct suscli_spectrum_params params;
  suscan_analyzer_t            *analyzer;
  struct suscan_analyzer_params analyzer_params;
  enum suscli_spectrum_state    state;

  SUFLOAT                       samp_rate;
  SUFLOAT                       psd_rate;

  SUBOOL                        have_source_info;
  SUBOOL                        have_analyzer_params;

};

#define suscli_spectrum_INITIALIZER {                       \
  suscli_spectrum_params_INITIALIZER,                       \
  NULL, /* analyzer */                                      \
  suscan_analyzer_params_INITIALIZER, /* analyzer_params */ \
  SUSCLI_SPECTRUM_STARTUP, /* state */                      \
  0,    /* samp_rate */                                     \
  25,   /* psd_rate */                                      \
  SU_FALSE, /* have_source_info */                          \
  SU_FALSE, /* have_analyzer_params */                      \
}

SUPRIVATE SUBOOL g_halting = SU_FALSE;

void
suscli_spectrum_int_handler(int sig)
{
  g_halting = SU_TRUE;
}

SUPRIVATE SUBOOL
suscli_spectrum_msg_is_final(uint32_t type)
{
  return 
       (type == SUSCAN_ANALYZER_MESSAGE_TYPE_EOS)
    || (type == SUSCAN_ANALYZER_MESSAGE_TYPE_READ_ERROR)
    || (type == SUSCAN_WORKER_MSG_TYPE_HALT);
}


/*
 * Startup state: wait for source info and analyzer params to decide how to
 * configure the PSD rate.
 */

SUPRIVATE SUBOOL
suscli_spectrum_process_startup_message(
  struct suscli_spectrum *self,
  const struct suscan_msg *msg)
{
  struct suscan_source_info *info;
  struct suscan_analyzer_params *params;
  SUBOOL ok = SU_FALSE;

  switch (msg->type) {
    case SUSCAN_ANALYZER_MESSAGE_TYPE_SOURCE_INFO:
      info                   = msg->privdata;
      self->samp_rate        = info->source_samp_rate;
      self->have_source_info = SU_TRUE;
      break;

    case SUSCAN_ANALYZER_MESSAGE_TYPE_PARAMS:
      params                     = msg->privdata;
      self->psd_rate             = 1. / params->psd_update_int;
      self->analyzer_params      = *params;
      self->have_analyzer_params = SU_TRUE;
      break;
  }

  if (self->have_source_info && self->have_analyzer_params) {
    SU_INFO("Source sample rate: %g sps\n", self->samp_rate);
    SU_INFO("PSD refresh rate:   %g fps\n", self->psd_rate);
    SU_INFO("Entering in configuring state...\n");

    self->analyzer_params.psd_update_int              = 1 / self->params.psd_rate;
    self->analyzer_params.detector_params.window_size = self->params.fft_size;

    self->state = SUSCLI_SPECTRUM_CONFIGURING;

    SU_TRY(
      suscan_analyzer_set_params_async(
        self->analyzer,
        &self->analyzer_params,
        0));
  }

  ok = SU_TRUE;

done:
  return ok;
}

SUPRIVATE SUBOOL
suscli_spectrum_process_configuring_message(
  struct suscli_spectrum *self,
  const struct suscan_msg *msg)
{
  if (msg->type == SUSCAN_ANALYZER_MESSAGE_TYPE_PARAMS) {
    struct suscan_analyzer_params *params;

    params                     = msg->privdata;
    self->psd_rate             = 1. / params->psd_update_int;
    self->analyzer_params      = *params;

    SU_INFO("PSD refresh rate (configured): %g fps\n", self->psd_rate);
    SU_INFO("Entering in acquisition state...\n");

    self->state = SUSCLI_SPECTRUM_ACQUIRING;
  }

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscli_spectrum_process_acquiring_message(
  struct suscli_spectrum *self,
  const struct suscan_msg *msg)
{
  if (msg->type == SUSCAN_ANALYZER_MESSAGE_TYPE_PSD) {
    struct suscan_analyzer_psd_msg *psd = msg->privdata;
    fprintf(stderr, "[%ld.%06d] PSD [%ld bins]\n", psd->timestamp.tv_sec, psd->timestamp.tv_usec, psd->psd_size);
  }

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscli_spectrum_process_message(
  struct suscli_spectrum *self,
  const struct suscan_msg *msg)
{
  SUBOOL ok = SU_FALSE;

  switch (self->state) {
    case SUSCLI_SPECTRUM_STARTUP:
      SU_TRY(suscli_spectrum_process_startup_message(self, msg));
      break;

    case SUSCLI_SPECTRUM_CONFIGURING:
      SU_TRY(suscli_spectrum_process_configuring_message(self, msg));
      break;

    case SUSCLI_SPECTRUM_ACQUIRING:
      SU_TRY(suscli_spectrum_process_acquiring_message(self, msg));
      break;
  }

  ok = SU_TRUE;

done:
  return ok;
}

SUBOOL
suscli_spectrum_cb(const hashlist_t *params)
{
  SUBOOL ok = SU_FALSE;
  struct suscli_spectrum state = suscli_spectrum_INITIALIZER;

  struct suscan_analyzer_params aparm = suscan_analyzer_params_INITIALIZER;
  struct suscan_mq omq;
  struct suscan_msg *msg = NULL;
  struct timeval tv;

  SU_TRY(suscan_mq_init(&omq));
  SU_TRY(suscli_param_read_profile(params, "profile", &state.params.profile));

  SU_MAKE(state.analyzer, suscan_analyzer, &aparm, state.params.profile, &omq);

  signal(SIGINT, suscli_spectrum_int_handler);

  while (!g_halting) {
    tv.tv_sec  = 0;
    tv.tv_usec = 100000;
    msg = suscan_mq_read_msg_timeout(&omq, &tv);

    if (msg != NULL) {
      if (suscli_spectrum_msg_is_final(msg->type))
        g_halting = SU_TRUE;

      SU_TRY(suscli_spectrum_process_message(&state, msg));

      suscan_analyzer_dispose_message(msg->type, msg->privdata);
      suscan_msg_destroy(msg);
      msg = NULL;
    }
  }

  ok = SU_TRUE;

done:
  if (msg != NULL) {
    suscan_analyzer_dispose_message(msg->type, msg->privdata);
    suscan_msg_destroy(msg);
  }
  
  if (state.analyzer != NULL)
    suscan_analyzer_destroy(state.analyzer);

  suscan_mq_finalize(&omq);

  return ok;
}

