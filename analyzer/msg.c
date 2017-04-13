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

/* Channel list */
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
  for (i = 0; i < len; ++i)
    if (list[i] != NULL)
      if (SU_CHANNEL_IS_VALID(list[i])) {
        if ((new->channel_list[n] = su_channel_dup(list[i])) == NULL)
          goto fail;

        new->channel_list[n]->fc += analyzer->source.fc;
        ++n;
      }

  new->channel_count = n;

  return new;

fail:
  if (new != NULL)
    suscan_analyzer_channel_msg_destroy(new);

  return NULL;
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
