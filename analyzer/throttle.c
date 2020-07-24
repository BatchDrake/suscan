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

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <stdint.h>
#include <time.h>

#define SU_LOG_DOMAIN "throttle"

#include <sigutils/sigutils.h>
#include "throttle.h"
#include "realtime.h"

void
suscan_throttle_init(suscan_throttle_t *self, SUSCOUNT samp_rate)
{
  SUSCOUNT delta_s;
  SUFLOAT  delta_t;

  memset(self, 0, sizeof(suscan_throttle_t));

  self->t0 = suscan_gettime_raw();

  delta_t = SU_MAX(
      SUSCAN_THROTTLE_CHECKPOINT_DURATION_NS,
      suscan_getres_raw());

  delta_s = samp_rate * (delta_t * SUSCAN_REALTIME_NS);

  if (delta_s < SUSCAN_THROTTLE_MIN_BLOCK_SIZE) {
    delta_s = SUSCAN_THROTTLE_MIN_BLOCK_SIZE;
    delta_t = delta_s / (samp_rate * SUSCAN_REALTIME_NS);
  }

  self->delta_s = delta_s;
  self->delta_t = delta_t;

  self->avail = self->delta_s;
}

SUSCOUNT
suscan_throttle_get_portion(suscan_throttle_t *self, SUSCOUNT h)
{
  struct timespec sleep_time;
  uint64_t sleep_nsec;
  uint64_t skipped;
  uint64_t t = suscan_gettime_raw();
  uint64_t delta_t = t - self->t0;

  /*
   * FIXME: CLOCK OVERFLOW HAS NOT BEEN CONSIDERED HERE.
   */
  if (delta_t < self->delta_t) {
    /* We are reading between the last and the next checkpoint */
    if (self->avail == 0) {
      self->t0   += self->delta_t;
      self->avail = self->delta_s;

      sleep_nsec  = self->delta_t - delta_t;

      sleep_time.tv_sec  = sleep_nsec / 1000000000;
      sleep_time.tv_nsec = sleep_nsec % 1000000000;

      (void) nanosleep(&sleep_time, NULL);
    }
  } else if (delta_t < SUSCAN_THROTTLE_LATE_DELAY_NS) {
    /* We are multiple checkpoints behind */
    skipped      = delta_t / self->delta_t;
    self->t0    += skipped * self->delta_t;
    self->avail += skipped * self->delta_s;
  } else {
    /* Late reader. Reset clock. */
    self->t0 = t;
    self->avail = self->delta_s;
  }

  return SU_MIN(h, self->avail);
}

void
suscan_throttle_advance(suscan_throttle_t *throttle, SUSCOUNT got)
{
  throttle->avail -= SU_MIN(throttle->avail, got);
}
