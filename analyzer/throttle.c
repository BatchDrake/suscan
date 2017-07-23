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

void
suscan_throttle_init(suscan_throttle_t *throttle, SUSCOUNT samp_rate)
{
  throttle->samp_count = 0;
  throttle->samp_rate = samp_rate;

  clock_gettime(CLOCK_MONOTONIC_RAW, &throttle->t0);
}

SUSCOUNT
suscan_throttle_get_portion(suscan_throttle_t *throttle, SUSCOUNT h)
{
  struct timespec tn;
  struct timespec sleep_time;
  struct timespec sub;
  SUSCOUNT samps;
  SUSDIFF  nsecs;
  SUSDIFF  avail;

  if (h > 0) {
    do {
      clock_gettime(CLOCK_MONOTONIC_RAW, &tn);

      timespecsub(&tn, &throttle->t0, &sub);

      if (sub.tv_sec > 0) {
        /* Reader is really late, get a rough estimate */
        avail = throttle->samp_rate * sub.tv_sec - throttle->samp_count;
      } else {
        nsecs = sub.tv_sec * 1000000000ll + sub.tv_nsec;
        avail = (throttle->samp_rate * nsecs) / 1000000000ll
            - throttle->samp_count;
      }

      if (avail == 0) {
        /*
         * Stream exhausted. We wait a fraction of the time it would take
         * for h samples to be available, then we try again.
         */
        throttle->samp_count = 0;
        throttle->t0 = tn;

        samps = SUSCAN_THROTTLE_MAX_READ_UNIT_FRAC * h;
        nsecs = (samps * 1000000000) / throttle->samp_rate;

        sleep_time.tv_sec  = nsecs / 1000000000;
        sleep_time.tv_nsec = nsecs % 1000000000;

        (void) nanosleep(&sleep_time, NULL);
        continue;
      } else {
        /* Check to avoid slow readers to overflow the available counter */
        if (avail > SUSCAN_THROTTLE_RESET_THRESHOLD) {
          throttle->samp_count = 0;
          throttle->t0 = tn;
        }

        h = MIN(avail, h);
      }
    } while (SU_FALSE);
  }

  return h;
}

void
suscan_throttle_advance(suscan_throttle_t *throttle, SUSCOUNT got)
{
  throttle->samp_count += got;
}
