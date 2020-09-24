/*

  Copyright (C) 2020 Gonzalo José Carracedo Carballal

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

#ifndef _SUSCAN_ANALYZER_IMPL_REMOTE_H
#define _SUSCAN_ANALYZER_IMPL_REMOTE_H

#include <analyzer/analyzer.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

enum suscan_analyzer_remote_type {
  SUSCAN_ANALYZER_REMOTE_NONE,
  SUSCAN_ANALYZER_REMOTE_AUTH_INFO,
  SUSCAN_ANALYZER_REMOTE_SOURCE_INFO,
  SUSCAN_ANALYZER_REMOTE_SET_FREQUENCY,
  SUSCAN_ANALYZER_REMOTE_SET_GAIN,
  SUSCAN_ANALYZER_REMOTE_SET_ANTENNA,
  SUSCAN_ANALYZER_REMOTE_SET_BANDWIDTH,
  SUSCAN_ANALYZER_REMOTE_SET_DC_REMOVE,
  SUSCAN_ANALYZER_REMOTE_SET_IQ_REVERSE,
  SUSCAN_ANALYZER_REMOTE_SET_AGC,
  SUSCAN_ANALYZER_REMOTE_SET_FORCE_EOS,
  SUSCAN_ANALYZER_REMOTE_SET_SWEEP_STRATEGY,
  SUSCAN_ANALYZER_REMOTE_SET_SPECTRUM_PARTITIONING,
  SUSCAN_ANALYZER_REMOTE_SET_HOP_RANGE,
  SUSCAN_ANALYZER_REMOTE_SET_BUFFERING_SIZE,
  SUSCAN_ANALYZER_REMOTE_MESSAGE,
  SUSCAN_ANALYZER_REMOTE_REQ_HALT,
};

SUSCAN_SERIALIZABLE(suscan_analyzer_remote_call)
{
  uint32_t type;

  union {
    struct suscan_analyzer_source_info source_info;
    struct {
      SUFREQ freq;
      SUFREQ lnb;
    };

    struct {
      char   *name;
      SUFLOAT value;
    } gain;

    char *antenna;
    SUFLOAT bandwidth;
    SUBOOL dc_remove;
    SUBOOL iq_reverse;
    SUBOOL agc;
    uint32_t sweep_strategy;
    uint32_t spectrum_partitioning;
    uint32_t buffering_size;

    struct {
      SUFREQ min;
      SUFREQ max;
    } hop_range;

    struct {
      uint32_t type;
      void *ptr;
    } msg;
  };
};

void suscan_analyzer_remote_call_init(
    struct suscan_analyzer_remote_call *self);
void suscan_analyzer_remote_call_finalize(
    struct suscan_analyzer_remote_call *self);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _SUSCAN_ANALYZER_IMPL_REMOTE_H */
