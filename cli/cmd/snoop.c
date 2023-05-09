/*

  Copyright (C) 2022 Gonzalo José Carracedo Carballal

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

#define SU_LOG_DOMAIN "cli-snoop"

#include <sigutils/log.h>
#include <analyzer/source.h>
#include <analyzer/analyzer.h>
#include <analyzer/msg.h>
#include <signal.h>

#include <cli/cli.h>
#include <cli/cmds.h>
#include <inttypes.h>
#include <sigutils/util/compat-time.h>

SUPRIVATE SUBOOL g_halting = SU_FALSE;

void
suscli_snoop_int_handler(int sig)
{
  g_halting = SU_TRUE;
}

SUPRIVATE const char *
suscli_snoop_msg_to_string(uint32_t type)
{
  const char *types[] = 
  {
    "SOURCE_INFO", "SOURCE_INIT", "CHANNEL", "EOS",
    "READ_ERROR", "INTERNAL", "SAMPLES_LOST", "INSPECTOR",
    "PSD", "SAMPLES", "THROTTLE", "PARAMS", "GET_PARAMS",
    "SEEK"
  };

  if (type <= SUSCAN_ANALYZER_MESSAGE_TYPE_SEEK)
    return types[type];

  if (type == SUSCAN_WORKER_MSG_TYPE_HALT)
    return "HALT";

  return "UNKNOWN";
}

#define JSON_KEY(key) printf("\"" key "\" : ");
#define JSON_SCALAR_FIELD(key, fmt, value...) JSON_KEY(key); printf(fmt, ##value);

#define JSON_MSG_FIELD_VALUE(field, value)                  \
  do {                                                      \
    printf("  ");                                           \
    JSON_SCALAR_FIELD(STRINGIFY(field), "%s", value);       \
    printf(",\n");                                          \
  } while (0)

#define JSON_MSG_FIELD(field, fmt)                          \
  do {                                                      \
    printf("  ");                                           \
    JSON_SCALAR_FIELD(STRINGIFY(field), fmt, msg->field);   \
    printf(",\n");                                          \
  } while (0)


#define JSON_MSG_FIELD(field, fmt)                          \
  do {                                                      \
    printf("  ");                                           \
    JSON_SCALAR_FIELD(STRINGIFY(field), fmt, msg->field);   \
    printf(",\n");                                          \
  } while (0)

#define JSON_MSG_BOOL(field)                                \
  do {                                                      \
    printf("  ");                                           \
    JSON_SCALAR_FIELD(                                      \
      STRINGIFY(field),                                     \
      "%s",                                                 \
      msg->field ? "true" : "false");                       \
    printf(",\n");                                          \
  } while (0)

#define JSON_MSG_TIMEVAL(field)                             \
  do {                                                      \
    printf("  ");                                           \
    JSON_SCALAR_FIELD(                                      \
      STRINGIFY(field),                                     \
      "%ld.%06ld",                                          \
      msg->field.tv_sec, msg->field.tv_usec);               \
    printf(",\n");                                          \
  } while (0)


#define JSON_MSG_SUSCOUNT(field) JSON_MSG_FIELD(field, "%" PRIu64)
#define JSON_MSG_INT32(field) JSON_MSG_FIELD(field, "%" PRId32)
#define JSON_MSG_HANDLE(field) JSON_MSG_FIELD(field, "%u")
#define JSON_MSG_INT64(field) JSON_MSG_FIELD(field, "%" PRId64)
#define JSON_MSG_SUFLOAT(field) JSON_MSG_FIELD(field, "%g")
#define JSON_MSG_SUFREQ(field) JSON_MSG_FIELD(field, "%.0f")
#define JSON_MSG_STRING(field) JSON_MSG_FIELD(field, "\"%s\"")

SUPRIVATE SUBOOL
suscli_snoop_msg_debug_status(
  const struct suscan_analyzer_status_msg *msg)
{
  JSON_MSG_INT32(code);

  if (msg->message != NULL)
    JSON_MSG_STRING(message);

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscli_snoop_msg_debug_psd_msg(
  const struct suscan_analyzer_psd_msg *msg)
{
  JSON_MSG_INT64(fc);
  JSON_MSG_HANDLE(inspector_id);
  JSON_MSG_TIMEVAL(timestamp);
  JSON_MSG_TIMEVAL(rt_time);
  JSON_MSG_BOOL(looped);
  JSON_MSG_SUFLOAT(samp_rate);

  if (msg->measured_samp_rate > 0)
    JSON_MSG_SUFLOAT(measured_samp_rate);
  JSON_MSG_SUSCOUNT(psd_size);

  return SU_TRUE;
}


SUPRIVATE SUBOOL
suscli_snoop_msg_debug_params(
  const struct suscan_analyzer_params *msg)
{
  if (msg->mode == SUSCAN_ANALYZER_MODE_CHANNEL)
    JSON_MSG_FIELD_VALUE(mode, "\"CHANNEL\"");
  else
    JSON_MSG_FIELD_VALUE(mode, "\"WIDE\"");

  JSON_MSG_SUSCOUNT(detector_params.window_size);

  switch (msg->detector_params.window) {
    case SU_CHANNEL_DETECTOR_WINDOW_NONE:
      JSON_MSG_FIELD_VALUE(window, "\"NONE\"");
      break;
    
    case SU_CHANNEL_DETECTOR_WINDOW_BLACKMANN_HARRIS:
      JSON_MSG_FIELD_VALUE(window, "\"BLACKMANN_HARRIS\"");
      break;
    
    case SU_CHANNEL_DETECTOR_WINDOW_FLAT_TOP:
      JSON_MSG_FIELD_VALUE(window, "\"FLAT_TOP\"");
      break;
    
    case SU_CHANNEL_DETECTOR_WINDOW_HAMMING:
      JSON_MSG_FIELD_VALUE(window, "\"HAMMING\"");
      break;
    
    case SU_CHANNEL_DETECTOR_WINDOW_HANN:
      JSON_MSG_FIELD_VALUE(window, "\"HANN\"");
      break;

    default:
      JSON_MSG_FIELD_VALUE(window, "\"UNKNOWN\"");
      break;
  }

  JSON_MSG_SUFLOAT(psd_update_int);

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscli_snoop_msg_debug_source_info(
  const struct suscan_analyzer_source_info *msg)
{

  struct strlist *list = NULL;
  unsigned int i;
  SUBOOL ok = SU_FALSE;
  
  SU_MAKE(list, strlist);

#define TEST_PERM(perm)                                      \
  if (msg->permissions & JOIN(SUSCAN_ANALYZER_PERM_, perm)) \
    strlist_append_string(list, STRINGIFY(perm))
  
  TEST_PERM(HALT);
  TEST_PERM(SET_FREQ);
  TEST_PERM(SET_GAIN);
  TEST_PERM(SET_ANTENNA);
  TEST_PERM(SET_BW);
  TEST_PERM(SET_PPM);
  TEST_PERM(SET_DC_REMOVE);
  TEST_PERM(SET_IQ_REVERSE);
  TEST_PERM(SET_AGC);
  TEST_PERM(OPEN_AUDIO);
  TEST_PERM(OPEN_RAW);
  TEST_PERM(OPEN_INSPECTOR);
  TEST_PERM(SET_FFT_SIZE);
  TEST_PERM(SET_FFT_FPS);
  TEST_PERM(SET_FFT_WINDOW);
  TEST_PERM(SEEK);
  TEST_PERM(THROTTLE);

#undef TEST_PERM

  printf("  \"permissions\" : [");
  for (i = 0; i < list->strings_count; ++i)
    printf("%s\"%s\"", i > 0 ? ", " : "", list->strings_list[i]);
  printf("],\n");

  JSON_MSG_SUSCOUNT(source_samp_rate);
  JSON_MSG_SUSCOUNT(effective_samp_rate);
  JSON_MSG_SUFLOAT(measured_samp_rate);
  JSON_MSG_SUFREQ(frequency);
  JSON_MSG_SUFREQ(freq_min);
  JSON_MSG_SUFREQ(freq_max);
  JSON_MSG_SUFREQ(lnb);
  JSON_MSG_SUFLOAT(bandwidth);
  JSON_MSG_SUFLOAT(ppm);

  if (msg->antenna != NULL)
    JSON_MSG_STRING(antenna);
  
  JSON_MSG_BOOL(dc_remove);
  JSON_MSG_BOOL(iq_reverse);
  JSON_MSG_BOOL(agc);
  JSON_MSG_BOOL(have_qth);

  if (msg->have_qth) {
    JSON_MSG_SUFLOAT(qth.lat);
    JSON_MSG_SUFLOAT(qth.lon);
    JSON_MSG_SUFLOAT(qth.elevation);
  }

  JSON_MSG_BOOL(seekable);
  JSON_MSG_TIMEVAL(source_time);
  JSON_MSG_TIMEVAL(source_start);
  JSON_MSG_TIMEVAL(source_end);

  printf("  \"antennas\" : [");
  for (i = 0; i < msg->antenna_count; ++i)
    printf("%s\"%s\"", i > 0 ? ", " : "", msg->antenna_list[i]);
  printf("],\n");

  printf("  \"gains\" : [");
  for (i = 0; i < msg->gain_count; ++i) {
    if (i > 0)
      printf(",");
    printf("\n");

    printf("    {\n");
    printf("      \"name\": \"%s\",\n", msg->gain_list[i]->name);
    printf("      \"value\": \"%g\",\n", msg->gain_list[i]->value);
    printf("      \"min\": \"%g\",\n", msg->gain_list[i]->min);
    printf("      \"max\": \"%g\",\n", msg->gain_list[i]->max);
    printf("      \"step\": \"%g\"\n", msg->gain_list[i]->step);
    printf("    }");
  }
  printf("],\n");

  ok = SU_TRUE;

done:
  if (list != NULL)
    strlist_destroy(list);

  return ok;
}

SUPRIVATE void
suscli_snoop_msg_debug(uint32_t type, void *message)
{
  struct timeval tv;

  printf("\x1e{\n");
  printf("  \"type\": \"%s\",\n", suscli_snoop_msg_to_string(type));

  gettimeofday(&tv, NULL);

  switch (type) {
    case SUSCAN_ANALYZER_MESSAGE_TYPE_SOURCE_INFO:
      suscli_snoop_msg_debug_source_info(message);
      break;

    case SUSCAN_ANALYZER_MESSAGE_TYPE_EOS:
    case SUSCAN_ANALYZER_MESSAGE_TYPE_SOURCE_INIT:
    case SUSCAN_ANALYZER_MESSAGE_TYPE_READ_ERROR: 
    case SUSCAN_ANALYZER_MESSAGE_TYPE_INTERNAL:
      suscli_snoop_msg_debug_status(message);
      break;
    
    case SUSCAN_ANALYZER_MESSAGE_TYPE_CHANNEL:
      break;

    case SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR:
      break;

    case SUSCAN_ANALYZER_MESSAGE_TYPE_PSD:
      suscli_snoop_msg_debug_psd_msg(message);
      break;

    case SUSCAN_ANALYZER_MESSAGE_TYPE_SAMPLES:
      break;

    case SUSCAN_ANALYZER_MESSAGE_TYPE_THROTTLE:
      break;

    case SUSCAN_ANALYZER_MESSAGE_TYPE_PARAMS:
      suscli_snoop_msg_debug_params(message);
      break;

    case SUSCAN_ANALYZER_MESSAGE_TYPE_GET_PARAMS:
      break;

    case SUSCAN_ANALYZER_MESSAGE_TYPE_SEEK:
      break;

    default:
      printf("  \"numeric_type\": %u,\n", type);
  }

  printf("  \"local_timestamp\": %ld.%06ld\n", tv.tv_sec, tv.tv_usec);
  printf("}\n");
  fflush(stdout);
}

SUPRIVATE SUBOOL
suscli_snoop_msg_is_final(uint32_t type)
{
  return 
       (type == SUSCAN_ANALYZER_MESSAGE_TYPE_EOS)
    || (type == SUSCAN_ANALYZER_MESSAGE_TYPE_READ_ERROR)
    || (type == SUSCAN_WORKER_MSG_TYPE_HALT);
}

SUBOOL
suscli_snoop_cb(const hashlist_t *params)
{
  SUBOOL ok = SU_FALSE;
  suscan_source_config_t *profile = NULL;
  suscan_analyzer_t *analyzer = NULL;
  struct suscan_analyzer_params aparm = suscan_analyzer_params_INITIALIZER;
  struct suscan_mq omq;
  struct suscan_msg *msg = NULL;
  struct timeval tv;

  SU_TRY(suscan_mq_init(&omq));
  SU_TRY(suscli_param_read_profile(params, "profile", &profile));

  SU_MAKE(analyzer, suscan_analyzer, &aparm, profile, &omq);
  signal(SIGINT, suscli_snoop_int_handler);

  while (!g_halting) {
    tv.tv_sec  = 0;
    tv.tv_usec = 100000;
    msg = suscan_mq_read_msg_timeout(&omq, &tv);

    if (msg != NULL) {
      if (suscli_snoop_msg_is_final(msg->type))
        g_halting = SU_TRUE;

      suscli_snoop_msg_debug(msg->type, msg->privdata);
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
  
  if (analyzer != NULL)
    suscan_analyzer_destroy(analyzer);

  suscan_mq_finalize(&omq);

  return ok;
}
