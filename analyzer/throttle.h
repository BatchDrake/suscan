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

#ifndef _THROTTLE_H
#define _THROTTLE_H

#include <sigutils/sigutils.h>

#define SUSCAN_THROTTLE_LATE_DELAY_NS        5000000000ull
#define SUSCAN_THROTTLE_MIN_BLOCK_SIZE                   1
#define SUSCAN_THROTTLE_CHECKPOINT_DURATION_NS 10000000ull

struct suscan_throttle {
  uint64_t t0; /* Last checkpoint time */
  SUSCOUNT avail; /* Samples available until next checkpoint */
  SUSCOUNT delta_s; /* Samples per checkpoint */
  SUSCOUNT delta_t; /* Nanoseconds per checkpoint */
};

typedef struct suscan_throttle suscan_throttle_t;

void suscan_throttle_init(suscan_throttle_t *throttle, SUSCOUNT samp_rate);

SUSCOUNT suscan_throttle_get_portion(suscan_throttle_t *throttle, SUSCOUNT h);

void suscan_throttle_advance(suscan_throttle_t *throttle, SUSCOUNT got);

#endif /* _THROTTLE_H */
