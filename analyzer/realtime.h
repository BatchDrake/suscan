/*

  Copyright (C) 2020 Josef 'Jeff' Sipek <jeffpc@josefsipek.net>

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

#ifndef _REALTIME_H
#define _REALTIME_H

#include "sigutils/types.h"

#include <time.h>

/*
 * Various time getting functions.
 */

/* helper that can get any clock */
SUINLINE uint64_t
suscan_gettime_helper(int clock)
{
  struct timespec ts;

  /* FIXME: assert that clock_gettime returned 0 */
  clock_gettime(clock, &ts);

  return (ts.tv_sec * 1000000000ull) + ts.tv_nsec;
}

SUINLINE uint64_t
suscan_gettime_coarse(void)
{
#ifdef CLOCK_MONOTONIC_COARSE
  return suscan_gettime_helper(CLOCK_MONOTONIC_COARSE);
#else
  return suscan_gettime_helper(CLOCK_MONOTONIC);
#endif
}

SUINLINE uint64_t
suscan_gettime_raw(void)
{
#ifdef CLOCK_MONOTONIC_RAW
  return suscan_gettime_helper(CLOCK_MONOTONIC_RAW);
#else
  return suscan_gettime_helper(CLOCK_MONOTONIC);
#endif
}

SUINLINE uint64_t
suscan_gettime(void)
{
  return suscan_gettime_helper(CLOCK_MONOTONIC);
}

#endif
