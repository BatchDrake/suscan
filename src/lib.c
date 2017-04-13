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

#include <pthread.h>
#include <string.h>
#include <sigutils/sigutils.h>
#include <util.h>
#include "suscan.h"

#define SUSCAN_MAX_MESSAGES 1024

struct suscan_message {
  enum sigutils_log_severity severity;
  struct timeval time;
  char *category;
  char *message;
};

SUPRIVATE pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
SUPRIVATE struct suscan_message *message_ring[SUSCAN_MAX_MESSAGES];
SUPRIVATE unsigned int message_ptr;
SUPRIVATE unsigned int message_count;

SUPRIVATE char
suscan_severity_to_char(enum sigutils_log_severity sev)
{
  const char *sevstr = "di!ex";

  if (sev < 0 || sev > SU_LOG_SEVERITY_CRITICAL)
    return '?';

  return sevstr[sev];
}


SUPRIVATE SUBOOL
suscan_log_lock(void)
{
  return pthread_mutex_lock(&log_mutex) != -1;
}

SUPRIVATE void
suscan_log_unlock(void)
{
  (void) pthread_mutex_unlock(&log_mutex);
}

SUPRIVATE void
suscan_message_destroy(struct suscan_message *msg)
{
  if (msg->category != NULL)
    free(msg->category);

  if (msg->message != NULL)
    free(msg->message);
}

SUPRIVATE struct suscan_message *
suscan_message_new_from_log_message(const struct sigutils_log_message *logmsg)
{
  struct suscan_message *msg = NULL;

  if ((msg = calloc(1, sizeof (struct suscan_message))) == NULL)
    goto fail;

  if ((msg->category = strdup(logmsg->domain)) == NULL)
    goto fail;

  if ((msg->message = strdup(logmsg->message)) == NULL)
    goto fail;

  msg->time = logmsg->time;
  msg->severity = logmsg->severity;

  return msg;

fail:
  if (msg != NULL)
    suscan_message_destroy(msg);

  return NULL;
}

/* Keep a circular list of log messages */
SUPRIVATE SUBOOL
suscan_log_push_log_message(const struct sigutils_log_message *logmsg)
{
  struct suscan_message *msg;

  if ((msg = suscan_message_new_from_log_message(logmsg)) == NULL)
    return SU_FALSE;

  if (message_ring[message_ptr] != NULL)
    suscan_message_destroy(message_ring[message_ptr]);

  message_ring[message_ptr++] = msg;

  if (message_ptr == SUSCAN_MAX_MESSAGES)
    message_ptr = 0;

  if (message_count < SUSCAN_MAX_MESSAGES)
    ++message_count;

  return SU_TRUE;
}

SUPRIVATE void
suscan_log_func(void *private, const struct sigutils_log_message *logmsg)
{
  if (!suscan_log_lock())
    return;

  (void) suscan_log_push_log_message(logmsg);

  suscan_log_unlock();
}

char *
suscan_log_get_last_messages(struct timeval since, unsigned int max)
{
  char *result = NULL;
  char *tmp = NULL;
  unsigned int i, id;
  unsigned int count;

  if ((result = calloc(1, 1)) == NULL)
    goto fail;

  if (!suscan_log_lock())
    goto fail;

  count = MIN(message_count, max);

  for (i = 0; i < count; ++i) {
    id =
        (SUSCAN_MAX_MESSAGES + message_ptr - count + i)
        % SUSCAN_MAX_MESSAGES;

    if (message_ring[id] != NULL)
      if (message_ring[id]->time.tv_sec > since.tv_sec
          || (message_ring[id]->time.tv_sec == since.tv_sec
              && message_ring[id]->time.tv_usec == since.tv_usec)) {
        if ((tmp = strbuild(
            "%s(%c) %s",
            result,
            suscan_severity_to_char(message_ring[id]->severity),
            message_ring[id]->message))
            == NULL)
          goto fail;

        free(result);
        result = tmp;
        tmp = NULL;
      }
  }

  suscan_log_unlock();

  return result;

fail:
  if (result != NULL)
    free(result);

  if (tmp != NULL)
    free(tmp);

  suscan_log_unlock();

  return NULL;
}

SUBOOL
suscan_sigutils_init(void)
{
  struct sigutils_log_config config = sigutils_log_config_INITIALIZER;

  config.exclusive = SU_FALSE; /* We handle concurrency manually */
  config.log_func = suscan_log_func;

  return su_lib_init_ex(&config);
}


