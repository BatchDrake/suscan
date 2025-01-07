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

#define SU_LOG_DOMAIN "chanloop"

#include <sigutils/log.h>
#include <analyzer/msg.h>
#include "chanloop.h"

#define SUSCAN_CHANLOOP_PSD_STARTUP_INTERVAL 1e-6
#define SUSCAN_CHANLOOP_MSG_TIMEOUT_MS       5000
#define SUSCAN_CHANLOOP_REQ_ID               0xc1009ll

SUPRIVATE void
suscli_frequency_format(char *obuf, size_t osize, SUFREQ freq, const char *unit)
{
  const char *prefixes = "kMGT";
  char pfx = prefixes[0];
  unsigned int i;

  if (unit == NULL)
    unit = "Hz";
  
  for (i = 1; fabs(freq) >= 1e3 && i < 5; ++i) {
    freq *= 1e-3;
    pfx = prefixes[i - 1];
  }

  obuf[osize - 1] = '\0';
  snprintf(obuf, osize - 1, "%6.3lf %c%s", freq, pfx, unit);
}

suscli_chanloop_t *
suscli_chanloop_open(
    const struct suscli_chanloop_params *params,
    suscan_source_config_t *cfg)
{
  struct suscan_analyzer_inspector_msg *msg;
  void *rawmsg;
  uint32_t type;
  struct timeval timeout;
  SUSCOUNT true_samp_rate;
  SUFREQ   bandwidth;
  SUFREQ   lofreq;
  SUBOOL   have_inspector = SU_FALSE;
  char     freqline[64];

  struct sigutils_channel ch = sigutils_channel_INITIALIZER;

  suscli_chanloop_t *new = NULL;
  struct suscan_analyzer_params analyzer_params =
      suscan_analyzer_params_INITIALIZER;

  SU_TRYCATCH(params->on_data != NULL, goto fail);

  SU_TRYCATCH(params->relbw > 0,  goto fail);
  SU_TRYCATCH(params->relbw <= 1, goto fail);

  SU_TRYCATCH(params->rello - .5 * params->relbw > -.5,  goto fail);
  SU_TRYCATCH(params->rello + .5 * params->relbw < +.5, goto fail);

  /* Neither PSD nor channel detector */
  analyzer_params.channel_update_int = 0;
  analyzer_params.psd_update_int     = 0;

  SU_TRYCATCH(new = calloc(1, sizeof(suscli_chanloop_t)), goto fail);

  new->params = *params;
  new->lnb_freq = suscan_source_config_get_lnb_freq(cfg);

  if (new->params.type == NULL)
    new->params.type = "raw";

  /* First step: open analyzer, get true sample rate */
  SU_TRYCATCH(suscan_mq_init(&new->mq), goto fail);
  SU_TRYCATCH(
      new->analyzer = suscan_analyzer_new(
          &analyzer_params,
          cfg,
          &new->mq),
      goto fail);

  /* Wait for analyzer to be initialized */
  SU_TRY_FAIL(suscan_analyzer_wait_until_ready(new->analyzer, NULL));

  true_samp_rate = suscan_analyzer_get_samp_rate(new->analyzer);

  /* Second step: deduce bandwidth/lo from sample rate and relative bw/lo */
  bandwidth = true_samp_rate * params->relbw;
  lofreq    = true_samp_rate * params->rello;

  /* Third step: open inspector and wait for its creation */
  ch.ft   = 0;
  ch.fc   = lofreq;
  ch.f_lo = lofreq - .5 * bandwidth;
  ch.f_hi = lofreq + .5 * bandwidth;

  new->chan = ch;

  timeout.tv_sec  = SUSCAN_CHANLOOP_MSG_TIMEOUT_MS / 1000;
  timeout.tv_usec = (SUSCAN_CHANLOOP_MSG_TIMEOUT_MS % 1000) * 1000;

  SU_TRY_FAIL(
      suscan_analyzer_open_ex_async(
          new->analyzer,
          new->params.type,
          &ch,
          SU_TRUE, /* Precise centering */
          -1, /* parent = source channelizer */
          SUSCAN_CHANLOOP_REQ_ID));

  while (!have_inspector && (rawmsg = suscan_analyzer_read_timeout(
      new->analyzer,
      &type,
      &timeout)) != NULL) {
    switch (type) {
      case SUSCAN_ANALYZER_MESSAGE_TYPE_EOS:
        suscan_analyzer_dispose_message(type, rawmsg);
        goto fail;

      case SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR:
        msg = rawmsg;
        if (msg->kind == SUSCAN_ANALYZER_INSPECTOR_MSGKIND_OPEN) {
          fprintf(stderr, "Inspector opened!\n");
          fprintf(stderr, "  Inspector ID: 0x%08x\n", msg->inspector_id);
          fprintf(stderr, "  Request ID:   0x%08x\n", msg->req_id);
          fprintf(stderr, "  Handle:       0x%08x\n", msg->handle);

          suscli_frequency_format(
            freqline,
            sizeof(freqline),
            msg->equiv_fs,
            "sps");
          fprintf(stderr, "  EquivFS:      %s\n", freqline);

          suscli_frequency_format(
            freqline,
            sizeof(freqline),
            msg->channel.ft,
            "Hz");
          fprintf(stderr, "  Ft:           %s\n", freqline);

          suscli_frequency_format(
            freqline,
            sizeof(freqline),
            msg->bandwidth,
            "Hz");
          fprintf(stderr, "  BW:           %s\n", freqline);

          suscli_frequency_format(
            freqline,
            sizeof(freqline),
            msg->lo,
            "Hz");
          fprintf(stderr, "  LO:           %s\n", freqline);

          new->handle   = msg->handle;
          new->ft       = msg->channel.ft;
          new->bw       = msg->bandwidth;
          new->equiv_fs = msg->equiv_fs;
          
          SU_TRYCATCH(new->inspcfg = suscan_config_dup(msg->config), goto fail);
          have_inspector = SU_TRUE;

          /* Set parameters */
          if (new->params.on_open != NULL) {
            if ((new->params.on_open) (
                new->analyzer,
                new->inspcfg,
                new->params.userdata)) {
              SU_TRYCATCH(
                  suscan_analyzer_set_inspector_config_async(
                      new->analyzer,
                      msg->handle,
                      new->inspcfg,
                      0),
                  goto fail);
            }
          }
        }
        break;

      default:
        break;
    }

    suscan_analyzer_dispose_message(type, rawmsg);
  }

  if (!have_inspector) {
    SU_ERROR("Timeout while waiting for inspector creation\n");
    goto fail;
  }

  return new;

fail:
  if (new != NULL)
    suscli_chanloop_destroy(new);

  return NULL;
}

SUBOOL
suscli_chanloop_work(suscli_chanloop_t *self)
{
  struct suscan_analyzer_sample_batch_msg *msg;
  void *rawmsg;
  uint32_t type;
  struct timeval timeout;
  SUBOOL ok = SU_FALSE;

  timeout.tv_sec  = SUSCAN_CHANLOOP_MSG_TIMEOUT_MS / 1000;
  timeout.tv_usec = (SUSCAN_CHANLOOP_MSG_TIMEOUT_MS % 1000) * 1000;

  while ((rawmsg = suscan_analyzer_read_timeout(
        self->analyzer,
        &type,
        &timeout)) != NULL) {
      switch (type) {
        case SUSCAN_ANALYZER_MESSAGE_TYPE_EOS:
          suscan_analyzer_dispose_message(type, rawmsg);
          ok = SU_TRUE;
          goto fail;

        case SUSCAN_ANALYZER_MESSAGE_TYPE_READ_ERROR:
          suscan_analyzer_dispose_message(type, rawmsg);
          goto fail;

        case SUSCAN_ANALYZER_MESSAGE_TYPE_SAMPLES:
          msg = rawmsg;

          /* There is only one inspector opened. No need to check the handle. */
          if (!(self->params.on_data) (
              self->analyzer,
              msg->samples,
              msg->sample_count,
              self->params.userdata)) {
            suscan_analyzer_dispose_message(type, rawmsg);
            ok = SU_TRUE;
            goto fail;
          }

          break;

        default:
          break;
      }

      suscan_analyzer_dispose_message(type, rawmsg);
    }

  ok = SU_TRUE;

fail:
  return ok;
}

SUBOOL
suscli_chanloop_set_frequency(suscli_chanloop_t *self, SUFREQ freq)
{
  return suscan_analyzer_set_freq(self->analyzer, freq, self->lnb_freq);
}

SUBOOL
suscli_chanloop_set_lofreq(suscli_chanloop_t *self, SUFREQ lofreq)
{
  return suscan_analyzer_set_inspector_freq_async(
    self->analyzer,
    self->handle,
    lofreq,
    0);
}

SUBOOL
suscli_chanloop_commit_config(suscli_chanloop_t *self)
{
  return suscan_analyzer_set_inspector_config_async(
      self->analyzer,
      self->handle,
      self->inspcfg,
      0);
}

SUBOOL
suscli_chanloop_cancel(suscli_chanloop_t *self)
{
  suscan_analyzer_force_eos(self->analyzer);

  return SU_FALSE;
}

void
suscli_chanloop_destroy(suscli_chanloop_t *self)
{
  if (self->analyzer != NULL)
    suscan_analyzer_destroy(self->analyzer);

  if (self->inspcfg != NULL)
    suscan_config_destroy(self->inspcfg);

  suscan_analyzer_consume_mq(&self->mq);

  suscan_mq_finalize(&self->mq);

  free(self);
}
