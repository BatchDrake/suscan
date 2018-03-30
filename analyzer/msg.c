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
#include <assert.h>
#include <stdarg.h>
#include <ctype.h>
#include <libgen.h>
#include <pthread.h>
#include <stdint.h>
#include <pthread.h>

#define SU_LOG_DOMAIN "msg"

#include "mq.h"
#include "msg.h"
#include "source.h"

/* Status message */
void
suscan_analyzer_status_msg_destroy(struct suscan_analyzer_status_msg *status)
{
  if (status->err_msg != NULL)
    free(status->err_msg);

  free(status);
}

/* Channel list */
struct suscan_analyzer_status_msg *
suscan_analyzer_status_msg_new(uint32_t code, const char *msg)
{
  char *msg_dup = NULL;
  struct suscan_analyzer_status_msg *new;

  if (msg != NULL)
    if ((msg_dup = strdup(msg)) == NULL)
      return NULL;

  if ((new = malloc(sizeof(struct suscan_analyzer_status_msg))) == NULL) {
    if (msg_dup != NULL)
      free(msg_dup);
    return NULL;
  }

  new->err_msg = msg_dup;
  new->code = code;

  return new;
}

void
suscan_analyzer_channel_msg_take_channels(
    struct suscan_analyzer_channel_msg *msg,
    struct sigutils_channel ***pchannel_list,
    unsigned int *pchannel_count)
{
  *pchannel_list = msg->channel_list;
  *pchannel_count = msg->channel_count;

  msg->channel_list = NULL;
  msg->channel_count = 0;
}

void
suscan_analyzer_channel_msg_destroy(struct suscan_analyzer_channel_msg *msg)
{
  unsigned int i;

  for (i = 0; i < msg->channel_count; ++i)
    if (msg->channel_list[i] != NULL)
      su_channel_destroy(msg->channel_list[i]);

  if (msg->channel_list != NULL)
    free(msg->channel_list);

  free(msg);
}

struct suscan_analyzer_channel_msg *
suscan_analyzer_channel_msg_new(
    const suscan_analyzer_t *analyzer,
    struct sigutils_channel **list,
    unsigned int len)
{
  unsigned int i;
  struct suscan_analyzer_channel_msg *new = NULL;
  unsigned int n = 0;

  if ((new = calloc(1, sizeof(struct suscan_analyzer_channel_msg))) == NULL)
    goto fail;

  if (len > 0)
    if ((new->channel_list = calloc(len, sizeof(struct sigutils_channel *)))
        == NULL)
      goto fail;

  new->channel_count = len;
  new->source = analyzer->source.config->source;
  new->sender = analyzer;

  for (i = 0; i < len; ++i)
    if (list[i] != NULL)
      if (SU_CHANNEL_IS_VALID(list[i])) {
        if ((new->channel_list[n] = su_channel_dup(list[i])) == NULL)
          goto fail;

        new->channel_list[n]->fc   += analyzer->source.fc;
        new->channel_list[n]->f_hi += analyzer->source.fc;
        new->channel_list[n]->f_lo += analyzer->source.fc;
        new->channel_list[n]->ft    = analyzer->source.fc;
        ++n;
      }

  new->channel_count = n;

  return new;

fail:
  if (new != NULL)
    suscan_analyzer_channel_msg_destroy(new);

  return NULL;
}

struct suscan_analyzer_inspector_msg *
suscan_analyzer_inspector_msg_new(
    enum suscan_analyzer_inspector_msgkind kind,
    uint32_t req_id)
{
  struct suscan_analyzer_inspector_msg *new;

  if ((new = calloc(1, sizeof (struct suscan_analyzer_inspector_msg))) == NULL)
    return NULL;

  new->kind = kind;
  new->req_id = req_id;

  return new;
}

SUFLOAT *
suscan_analyzer_inspector_msg_take_spectrum(
    struct suscan_analyzer_inspector_msg *msg)
{
  SUFLOAT *result = msg->spectrum_data;

  msg->spectrum_data = NULL;

  return result;
}

void
suscan_analyzer_inspector_msg_destroy(struct suscan_analyzer_inspector_msg *msg)
{
  if (msg->kind == SUSCAN_ANALYZER_INSPECTOR_MSGKIND_GET_CONFIG
      || msg->kind == SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SET_CONFIG
      || msg->kind == SUSCAN_ANALYZER_INSPECTOR_MSGKIND_OPEN) {
    if (msg->config != NULL)
      suscan_config_destroy(msg->config);

    if (msg->estimator_list != NULL)
      free(msg->estimator_list);

    if (msg->spectsrc_list != NULL)
      free(msg->spectsrc_list);

    if (msg->class != NULL)
      free(msg->class);
  } else if (msg->kind == SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SPECTRUM) {
    if (msg->spectrum_data != NULL)
      free(msg->spectrum_data);
  }

  free(msg);
}

void
suscan_analyzer_psd_msg_destroy(struct suscan_analyzer_psd_msg *msg)
{
  if (msg->psd_data != NULL)
    free(msg->psd_data);

  free(msg);
}

struct suscan_analyzer_psd_msg *
suscan_analyzer_psd_msg_new(const su_channel_detector_t *cd)
{
  struct suscan_analyzer_psd_msg *new = NULL;
  unsigned int i;
  SU_TRYCATCH(
      new = calloc(1, sizeof(struct suscan_analyzer_psd_msg)),
      goto fail);

  new->psd_size = cd->params.window_size;
  new->samp_rate = cd->params.samp_rate;

  if (cd->params.decimation > 1)
    new->samp_rate /= cd->params.decimation;

  new->fc = 0;

  SU_TRYCATCH(
      new->psd_data = malloc(sizeof(SUFLOAT) * new->psd_size),
      goto fail);

  switch (cd->params.mode) {
    case SU_CHANNEL_DETECTOR_MODE_AUTOCORRELATION:
      for (i = 0; i < new->psd_size; ++i)
        new->psd_data[i] = SU_C_REAL(cd->fft[i]);
      break;

    default:
      for (i = 0; i < new->psd_size; ++i) {
        new->psd_data[i] = SU_C_REAL(cd->fft[i] * SU_C_CONJ(cd->fft[i]));
        new->psd_data[i] /= cd->params.window_size;;
      }
  }

  return new;

fail:
  if (new != NULL)
    suscan_analyzer_psd_msg_destroy(new);

  return NULL;
}

SUFLOAT *
suscan_analyzer_psd_msg_take_psd(struct suscan_analyzer_psd_msg *msg)
{
  SUFLOAT *result = msg->psd_data;

  msg->psd_data = NULL;

  return result;
}

struct suscan_analyzer_sample_batch_msg *
suscan_analyzer_sample_batch_msg_new(
    uint32_t inspector_id,
    const SUCOMPLEX *samples,
    SUSCOUNT count)
{
  struct suscan_analyzer_sample_batch_msg *new = NULL;

  SU_TRYCATCH(
      new = calloc(1, sizeof(struct suscan_analyzer_sample_batch_msg)),
      goto fail);

  SU_TRYCATCH(
      new->samples = malloc(count * sizeof(SUCOMPLEX)),
      goto fail);

  memcpy(new->samples, samples, count * sizeof(SUCOMPLEX));

  new->sample_count = count;
  new->inspector_id = inspector_id;

  return new;

fail:
  if (new != NULL)
    suscan_analyzer_sample_batch_msg_destroy(new);

  return NULL;
}

void
suscan_analyzer_sample_batch_msg_destroy(
    struct suscan_analyzer_sample_batch_msg *msg)
{
  if (msg->samples != NULL)
    free(msg->samples);

  free(msg);
}

void
suscan_analyzer_dispose_message(uint32_t type, void *ptr)
{
  switch (type) {
    case SUSCAN_ANALYZER_MESSAGE_TYPE_SOURCE_INIT:
    case SUSCAN_ANALYZER_MESSAGE_TYPE_EOS:
      suscan_analyzer_status_msg_destroy(ptr);
      break;

    case SUSCAN_ANALYZER_MESSAGE_TYPE_CHANNEL:
      suscan_analyzer_channel_msg_destroy(ptr);
      break;

    case SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR:
      suscan_analyzer_inspector_msg_destroy(ptr);
      break;

    case SUSCAN_ANALYZER_MESSAGE_TYPE_PSD:
      suscan_analyzer_psd_msg_destroy(ptr);
      break;

    case SUSCAN_ANALYZER_MESSAGE_TYPE_SAMPLES:
      suscan_analyzer_sample_batch_msg_destroy(ptr);
      break;

    case SUSCAN_ANALYZER_MESSAGE_TYPE_THROTTLE:
      free(ptr);
      break;
  }
}

/****************************** Sender methods *******************************/
SUBOOL
suscan_analyzer_send_status(
    suscan_analyzer_t *analyzer,
    uint32_t type,
    int code,
    const char *err_msg_fmt, ...)
{
  struct suscan_analyzer_status_msg *msg;
  va_list ap;
  char *err_msg = NULL;
  SUBOOL ok = SU_FALSE;

  va_start(ap, err_msg_fmt);

  if (err_msg_fmt != NULL)
    if ((err_msg = vstrbuild(err_msg_fmt, ap)) == NULL)
      goto done;

  if ((msg = suscan_analyzer_status_msg_new(code, err_msg)) == NULL)
    goto done;

  msg->sender = analyzer;

  if (!suscan_mq_write(analyzer->mq_out, type, msg)) {
    suscan_analyzer_dispose_message(type, msg);
    goto done;
  }

  ok = SU_TRUE;

done:
  if (err_msg != NULL)
    free(err_msg);

  va_end(ap);

  return ok;
}

SUBOOL
suscan_analyzer_send_detector_channels(
    suscan_analyzer_t *analyzer,
    const su_channel_detector_t *detector)
{
  struct suscan_analyzer_channel_msg *msg = NULL;
  struct sigutils_channel **ch_list;
  unsigned int ch_count;
  SUBOOL ok = SU_FALSE;

  su_channel_detector_get_channel_list(detector, &ch_list, &ch_count);

  if ((msg = suscan_analyzer_channel_msg_new(analyzer, ch_list, ch_count))
      == NULL) {
    suscan_analyzer_send_status(
        analyzer,
        SUSCAN_ANALYZER_MESSAGE_TYPE_INTERNAL,
        -1,
        "Cannot create message: %s",
        strerror(errno));
    goto done;
  }

  if (!suscan_mq_write(
      analyzer->mq_out,
      SUSCAN_ANALYZER_MESSAGE_TYPE_CHANNEL,
      msg)) {
    suscan_analyzer_send_status(
        analyzer,
        SUSCAN_ANALYZER_MESSAGE_TYPE_INTERNAL,
        -1,
        "Cannot write message: %s",
        strerror(errno));
    goto done;
  }

  /* Message queued, forget about it */
  msg = NULL;

  ok = SU_TRUE;

done:
  if (msg != NULL)
    suscan_analyzer_dispose_message(SUSCAN_ANALYZER_MESSAGE_TYPE_CHANNEL, msg);
  return ok;
}

SUBOOL
suscan_analyzer_send_psd(
    suscan_analyzer_t *analyzer,
    const su_channel_detector_t *detector)
{
  struct suscan_analyzer_psd_msg *msg = NULL;
  SUBOOL ok = SU_FALSE;

  if ((msg = suscan_analyzer_psd_msg_new(detector)) == NULL) {
    suscan_analyzer_send_status(
        analyzer,
        SUSCAN_ANALYZER_MESSAGE_TYPE_INTERNAL,
        -1,
        "Cannot create message: %s",
        strerror(errno));
    goto done;
  }

  msg->fc = analyzer->source.fc;
  msg->N0 = detector->N0;

  if (!suscan_mq_write(
      analyzer->mq_out,
      SUSCAN_ANALYZER_MESSAGE_TYPE_PSD,
      msg)) {
    suscan_analyzer_send_status(
        analyzer,
        SUSCAN_ANALYZER_MESSAGE_TYPE_INTERNAL,
        -1,
        "Cannot write message: %s",
        strerror(errno));
    goto done;
  }

  /* Message queued, forget about it */
  msg = NULL;

  ok = SU_TRUE;

done:
  if (msg != NULL)
    suscan_analyzer_dispose_message(SUSCAN_ANALYZER_MESSAGE_TYPE_PSD, msg);

  return ok;
}
