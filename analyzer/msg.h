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

#ifndef _MSG_H
#define _MSG_H

#include <util.h>
#include <stdint.h>

#include "analyzer.h"

#define SUSCAN_ANALYZER_MESSAGE_TYPE_KEYBOARD      0x0
#define SUSCAN_ANALYZER_MESSAGE_TYPE_SOURCE_INIT   0x1
#define SUSCAN_ANALYZER_MESSAGE_TYPE_CHANNEL       0x2
#define SUSCAN_ANALYZER_MESSAGE_TYPE_EOS           0x3
#define SUSCAN_ANALYZER_MESSAGE_TYPE_INTERNAL      0x4
#define SUSCAN_ANALYZER_MESSAGE_TYPE_SAMPLES_LOST  0x5
#define SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR     0x6 /* Channel inspector */
#define SUSCAN_ANALYZER_MESSAGE_TYPE_PSD           0x7 /* Main spectrum */
#define SUSCAN_ANALYZER_MESSAGE_TYPE_SAMPLES       0x8 /* Sample batch */
#define SUSCAN_ANALYZER_MESSAGE_TYPE_INSP_PSD      0x9 /* Inspector spectrum */
#define SUSCAN_ANALYZER_MESSAGE_TYPE_PARAMS        0xa /* Analyzer params */

#define SUSCAN_ANALYZER_INIT_SUCCESS               0
#define SUSCAN_ANALYZER_INIT_FAILURE              -1

/* Generic status message */
struct suscan_analyzer_status_msg {
  int code;
  char *err_msg;
  const suscan_analyzer_t *sender;
};

/* Channel notification message */
struct suscan_analyzer_channel_msg {
  const struct suscan_source *source;
  PTR_LIST(struct sigutils_channel, channel);
  const suscan_analyzer_t *sender;
};

/* Channel spectrum message */
struct suscan_analyzer_psd_msg {
  uint64_t fc;
  uint32_t inspector_id;
  SUFLOAT  samp_rate;
  SUSCOUNT psd_size;
  SUFLOAT *psd_data;
  SUFLOAT  N0;
};

/* Channel sample batch */
struct suscan_analyzer_sample_batch_msg {
  uint32_t     inspector_id;
  SUCOMPLEX   *samples;
  unsigned int sample_count;
};

/*
 * Channel inspector command. This is request-response: sample
 * updates are treated separately
 */
enum suscan_analyzer_inspector_msgkind {
  SUSCAN_ANALYZER_INSPECTOR_MSGKIND_OPEN,
  SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SET_ID,
  SUSCAN_ANALYZER_INSPECTOR_MSGKIND_GET_CONFIG,
  SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SET_CONFIG,
  SUSCAN_ANALYZER_INSPECTOR_MSGKIND_ESTIMATOR,
  SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SPECTRUM,
  SUSCAN_ANALYZER_INSPECTOR_MSGKIND_RESET_EQUALIZER,
  SUSCAN_ANALYZER_INSPECTOR_MSGKIND_CLOSE,
  SUSCAN_ANALYZER_INSPECTOR_MSGKIND_INFO,
  SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_HANDLE,
  SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_OBJECT,
  SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_KIND
};

struct suscan_analyzer_inspector_msg {
  enum suscan_analyzer_inspector_msgkind kind;
  uint32_t inspector_id; /* Per-inspector identifier */
  uint32_t req_id;       /* Per-request identifier */
  uint32_t handle;       /* Handle */
  int status;

  union {
    struct {
      struct sigutils_channel channel;
      suscan_config_t *config;
      PTR_LIST_CONST(struct suscan_estimator_class, estimator);
      PTR_LIST_CONST(struct suscan_spectsrc_class, spectsrc);
    };

    struct {
      uint32_t estimator_id;
      SUBOOL   enabled;
      SUFLOAT  value;
    };

    struct {
      uint32_t  spectsrc_id;
      SUFLOAT  *spectrum_data;
      SUSCOUNT  spectrum_size;
      SUSCOUNT  samp_rate;
      SUFLOAT   fc;
      SUFLOAT   N0;
    };

    struct suscan_analyzer_params params;
    struct suscan_inspector_params insp_params;
  };
};

/************************ Message-generating methods *************************/
SUBOOL suscan_inspector_sampler_loop(
    suscan_inspector_t *insp,
    const SUCOMPLEX *samp_buf,
    SUSCOUNT samp_count,
    struct suscan_mq *mq_out);

SUBOOL suscan_inspector_spectrum_loop(
    suscan_inspector_t *insp,
    const SUCOMPLEX *samp_buf,
    SUSCOUNT samp_count,
    struct suscan_mq *mq_out);

SUBOOL suscan_inspector_estimator_loop(
    suscan_inspector_t *insp,
    const SUCOMPLEX *samp_buf,
    SUSCOUNT samp_count,
    struct suscan_mq *mq_out);

/***************************** Sender methods ********************************/
void suscan_analyzer_status_msg_destroy(struct suscan_analyzer_status_msg *status);
struct suscan_analyzer_status_msg *suscan_analyzer_status_msg_new(
    uint32_t code,
    const char *msg);

SUBOOL suscan_analyzer_send_status(
    suscan_analyzer_t *analyzer,
    uint32_t type,
    int code,
    const char *err_msg_fmt, ...);

SUBOOL suscan_analyzer_send_detector_channels(
    suscan_analyzer_t *analyzer,
    const su_channel_detector_t *detector);

SUBOOL suscan_analyzer_send_psd(
    suscan_analyzer_t *analyzer,
    const su_channel_detector_t *detector);

/************************* Message parsing methods ***************************/
SUBOOL suscan_analyzer_parse_inspector_msg(
    suscan_analyzer_t *analyzer,
    struct suscan_analyzer_inspector_msg *msg);

/***************** Message constructors and destructors **********************/
/* Status message */
struct suscan_analyzer_status_msg *suscan_analyzer_status_msg_new(
    uint32_t code,
    const char *msg);
void suscan_analyzer_status_msg_destroy(struct suscan_analyzer_status_msg *status);

/* Channel list update */
struct suscan_analyzer_channel_msg *suscan_analyzer_channel_msg_new(
    const suscan_analyzer_t *analyzer,
    struct sigutils_channel **list,
    unsigned int len);
void suscan_analyzer_channel_msg_take_channels(
    struct suscan_analyzer_channel_msg *msg,
    struct sigutils_channel ***pchannel_list,
    unsigned int *pchannel_count);
void suscan_analyzer_channel_msg_destroy(struct suscan_analyzer_channel_msg *msg);

/* Channel inspector commands */
struct suscan_analyzer_inspector_msg *suscan_analyzer_inspector_msg_new(
    enum suscan_analyzer_inspector_msgkind kind,
    uint32_t req_id);

SUFLOAT *suscan_analyzer_inspector_msg_take_spectrum(
    struct suscan_analyzer_inspector_msg *msg);

void suscan_analyzer_inspector_msg_destroy(
    struct suscan_analyzer_inspector_msg *msg);

/* Spectrum update message */
struct suscan_analyzer_psd_msg *suscan_analyzer_psd_msg_new(
    const su_channel_detector_t *cd);

SUFLOAT *suscan_analyzer_psd_msg_take_psd(struct suscan_analyzer_psd_msg *msg);

void suscan_analyzer_psd_msg_destroy(struct suscan_analyzer_psd_msg *msg);

/* Sample batch message */
struct suscan_analyzer_sample_batch_msg *suscan_analyzer_sample_batch_msg_new(
    uint32_t inspector_id,
    const SUCOMPLEX *samples,
    SUSCOUNT count);

void suscan_analyzer_sample_batch_msg_destroy(
    struct suscan_analyzer_sample_batch_msg *msg);

/* Generic message disposer */
void suscan_analyzer_dispose_message(uint32_t type, void *ptr);

#endif /* _MSG_H */
