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

#define SUSCAN_ANALYZER_INIT_SUCCESS               0
#define SUSCAN_ANALYZER_INIT_FAILURE              -1

/* Generic status message */
struct suscan_analyzer_status_msg {
  int code;
  char *err_msg;
};

/* Channel notification message */
struct suscan_analyzer_channel_msg {
  const struct suscan_source *source;
  PTR_LIST(struct sigutils_channel, channel);
};

/* Channel analyzer command */
enum suscan_analyzer_channel_analyzer_command {
  SUSCAN_WORKER_CHANNEL_ANALYZER_COMMAND_START,
  SUSCAN_WORKER_CHANNEL_ANALYZER_COMMAND_GET_INFO,
  SUSCAN_WORKER_CHANNEL_ANALYZER_COMMAND_STOP
};

struct suscan_analyzer_channel_analyzer_msg {
  enum suscan_analyzer_channel_analyzer_command command;
  uint32_t req_id;
  uint32_t chanid;
  int status;
};

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

/* Message constructors and destructors */
void suscan_analyzer_status_msg_destroy(struct suscan_analyzer_status_msg *status);
struct suscan_analyzer_status_msg *suscan_analyzer_status_msg_new(
    uint32_t code,
    const char *msg);
void suscan_analyzer_channel_msg_destroy(struct suscan_analyzer_channel_msg *msg);
struct suscan_analyzer_channel_msg *suscan_analyzer_channel_msg_new(
    const suscan_analyzer_t *analyzer,
    struct sigutils_channel **list,
    unsigned int len);
void suscan_analyzer_dispose_message(uint32_t type, void *ptr);

#endif /* _MSG_H */
