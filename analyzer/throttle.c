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
suscan_throttle_init(suscan_throttle_t *throttle, SUSCOUNT samp_rate)
{
  memset(throttle, 0, sizeof(suscan_throttle_t));
  throttle->samp_rate = samp_rate;
  throttle->t0 = suscan_gettime_raw();

  /*
   * In some circumstances, if both calls to suscan_gettime_raw happen
   * almost simultaneously, the difference in t0 is below the clock
   * resolution, entering in a full speed read that will hog the
   * CPU. This is definitely a bug, and this a workaround.
   */
  usleep(100000);
}

SUSCOUNT
suscan_throttle_get_portion(suscan_throttle_t *throttle, SUSCOUNT h)
{
  struct timespec sleep_time;
  uint64_t tn;
  uint64_t sub;
  SUSCOUNT samps;
  SUSDIFF  nsecs;
  SUSDIFF  avail;
  SUBOOL  retry;

  if (h > 0) {
    do {
      retry = SU_FALSE;
      tn = suscan_gettime_raw();

      sub = tn - throttle->t0;

      if (sub > SUSCAN_THROTTLE_LATE_READER_THRESHOLD_NS) {
        /* Reader is really late, get a rough estimate */
        avail = throttle->samp_rate * (sub / 1000000000ull) - throttle->samp_count;
      } else {
        nsecs = sub;
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

        retry = SU_TRUE;
      } else {
        /* Check to avoid slow readers to overflow the available counter */
        if (avail > SUSCAN_THROTTLE_RESET_THRESHOLD) {
          throttle->samp_count = 0;
          throttle->t0 = tn;
        }

        h = MIN(avail, h);
      }
    } while (retry);
  }

  return h;
}

void
suscan_throttle_advance(suscan_throttle_t *throttle, SUSCOUNT got)
{
  throttle->samp_count += got;
}
