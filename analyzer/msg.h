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
#define SUSCAN_ANALYZER_MESSAGE_TYPE_BR_INSPECTOR  0x6 /* Baudrate inspector */

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

/* Channel inspector command */
enum suscan_analyzer_inspector_msgkind {
  SUSCAN_ANALYZER_INSPECTOR_MSGKIND_OPEN,
  SUSCAN_ANALYZER_INSPECTOR_MSGKIND_GET_INFO,
  SUSCAN_ANALYZER_INSPECTOR_MSGKIND_CLOSE,
  SUSCAN_ANALYZER_INSPECTOR_MSGKIND_INFO,
  SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_HANDLE,
  SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_KIND
};

struct suscan_analyzer_inspector_msg {
  enum suscan_analyzer_inspector_msgkind kind;
  uint32_t req_id;
  uint32_t handle;
  int status;

  union {
    struct sigutils_channel channel;
    struct suscan_baudrate_inspector_result baudrate;
  };
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

/************************* Message parsing methods ***************************/
SUBOOL suscan_analyzer_parse_baud(
    suscan_analyzer_t *analyzer,
    struct suscan_analyzer_inspector_msg *msg);

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
struct suscan_analyzer_inspector_msg *suscan_analyzer_inspector_msg_new(
    enum suscan_analyzer_inspector_msgkind kind,
    uint32_t req_id);
void suscan_analyzer_inspector_msg_destroy(
    struct suscan_analyzer_inspector_msg *msg);

void suscan_analyzer_dispose_message(uint32_t type, void *ptr);

#endif /* _MSG_H */
