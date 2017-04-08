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

#ifndef _ANALYZER_H
#define _ANALYZER_H

#include <sigutils/sigutils.h>
#include <sigutils/detect.h>

struct suscan_channel_analyzer {
  struct sigutils_channel channel;
  su_channel_detector_t *fac_baud_det; /* FAC baud detector */
  su_channel_detector_t *nln_baud_det; /* Non-linear baud detector */
};

struct suscan_analyzer {
  struct suscan_mq mq_in;   /* To-thread messages */
  struct suscan_mq *mq_out; /* From-thread messages */
  struct suscan_source_config *config;
  SUBOOL running;

  pthread_t thread;
};

typedef struct suscan_analyzer suscan_analyzer_t;


/****************************** Analyzer API **********************************/
void *suscan_analyzer_read(suscan_analyzer_t *analyzer, uint32_t *type);
void suscan_analyzer_dispose_message(uint32_t type, void *ptr);
void suscan_analyzer_destroy(suscan_analyzer_t *analyzer);
suscan_analyzer_t *suscan_analyzer_new(
    struct suscan_source_config *config,
    struct suscan_mq *mq);

enum ctk_dialog_response suscan_open_source_dialog(
    struct suscan_source_config **config);

#endif /* _ANALYZER_H */
