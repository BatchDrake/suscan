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

#ifndef _THROTTLE_H
#define _THROTTLE_H

#include <sigutils/sigutils.h>

/*
 * Throttle reset threshold. If the number of available samples keeps growing,
 * it means that the reader is slower than the declared sample rate. In that
 * case, we just reset t0 and set sample_count to 0.
 */
#define SUSCAN_THROTTLE_RESET_THRESHOLD 1000000000ll
#define SUSCAN_THROTTLE_MAX_READ_UNIT_FRAC .25
#define SUSCAN_THROTTLE_LATE_READER_THRESHOLD_NS 1000000000ull

struct suscan_throttle {
  SUSCOUNT samp_rate;
  SUSCOUNT samp_count;
  uint64_t t0;
};

typedef struct suscan_throttle suscan_throttle_t;

void suscan_throttle_init(suscan_throttle_t *throttle, SUSCOUNT samp_rate);

SUSCOUNT suscan_throttle_get_portion(suscan_throttle_t *throttle, SUSCOUNT h);

void suscan_throttle_advance(suscan_throttle_t *throttle, SUSCOUNT got);

#endif /* _THROTTLE_H */
