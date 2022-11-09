/*

  Copyright (C) 2017 Gonzalo José Carracedo Carballal

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

#include <pthread.h>
#include <string.h>
#include <sigutils/sigutils.h>
#include <confdb.h>

#include <util.h>
#include "suscan.h"

#define SUSCAN_MAX_MESSAGES 1024
#define SUSCAN_WISDOM_FILE_NAME "wisdom.dat"

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

SUPRIVATE xyz_t  g_qth;
SUPRIVATE SUBOOL g_have_qth;
SUPRIVATE SUBOOL g_qth_tested;

SUBOOL
suscan_get_qth(xyz_t *xyz)
{
  suscan_config_context_t *ctx = NULL;
  const suscan_object_t *list = NULL;
  const suscan_object_t *qthobj = NULL;
  unsigned int count;
  const char *tmp;

  if (!g_qth_tested) {
    g_qth_tested = SU_TRUE;
    if ((ctx = suscan_config_context_assert("qth")) != NULL) {
      suscan_config_context_set_save(ctx, SU_TRUE);
      list = suscan_config_context_get_list(ctx);
      count = suscan_object_set_get_count(list);

      if (count > 0
        && (qthobj = suscan_object_set_get(list, 0)) != NULL 
        && (tmp = suscan_object_get_class(qthobj)) != NULL
        && strcmp(tmp, "Location") == 0) {
        g_qth.lat    = suscan_object_get_field_double(qthobj, "lat", NAN);
        g_qth.lon    = suscan_object_get_field_double(qthobj, "lon", NAN);
        g_qth.height = suscan_object_get_field_double(qthobj, "alt", NAN);

        if (!isnan(g_qth.lat) && !isnan(g_qth.lon) && !isnan(g_qth.height)) {
          g_qth.lat    = SU_DEG2RAD(g_qth.lat);
          g_qth.lon    = SU_DEG2RAD(g_qth.lon);
          g_qth.height *= 1e-3;
          g_have_qth = SU_TRUE;
        }
      }
    }

    if (!g_have_qth)
      SU_WARNING(
        "No valid QTH configuration found. Doppler corrections will be disabled.\n");
  }

  if (g_have_qth)
    *xyz = g_qth;

  return g_have_qth;
}

void
suscan_set_qth(const xyz_t *qth)
{
  if (qth != NULL) {
    g_have_qth = SU_TRUE;
    g_qth = *qth;
  } else {
    g_have_qth = SU_FALSE;
  }
}

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
              && message_ring[id]->time.tv_usec > since.tv_usec)) {
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

SUPRIVATE void
suscan_atexit_handler(void)
{
  if (!su_lib_save_wisdom()) {
    fprintf(stderr, "suscan: failed to save FFT wisdom, next run may be slow.\n");
  }
}

SUBOOL
suscan_fft_threads_init(void)
{
  SUBOOL ok = SU_FALSE;
  int n = 16;

  if (!SU_FFTW(_init_threads)()) {
    SU_WARNING("Failed to initialize multi-thread support for FFTW3");
  } else {
    SU_FFTW(_plan_with_nthreads)(n);
    ok = SU_TRUE;

    SU_INFO("FFTW3 threads: %d\n", n);
  }

  return ok;
}

SUBOOL
suscan_sigutils_init(enum suscan_mode mode)
{
  struct sigutils_log_config config = sigutils_log_config_INITIALIZER;
  struct sigutils_log_config *config_p = NULL;
  const char *userpath = NULL;
  char *wisdom_file = NULL;
  SUBOOL ok = SU_FALSE;
  
  SIGUTILS_ABI_CHECK();

  if (mode != SUSCAN_MODE_NOLOG) {
    if (mode == SUSCAN_MODE_DELAYED_LOG) {
      config.exclusive = SU_FALSE; /* We handle concurrency manually */
      config.log_func = suscan_log_func;

      config_p = &config;
    }

    if (!su_lib_init_ex(config_p))
      goto done;
  }

  suscan_fft_threads_init();
  

  SU_TRY(userpath = suscan_confdb_get_user_path());
  SU_TRY(wisdom_file = strbuild("%s/" SUSCAN_WISDOM_FILE_NAME, userpath));

  SU_TRY(su_lib_set_wisdom_file(wisdom_file));
  SU_TRY(su_lib_set_wisdom_enabled(SU_TRUE));

  /* Save FFT wisdom on exit */
  atexit(suscan_atexit_handler);

  ok = SU_TRUE;

done:
  if (wisdom_file != NULL)
    free(wisdom_file);
  
  return ok;
}


