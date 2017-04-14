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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "suscan.h"

SUBOOL
suscan_channel_is_dc(const struct sigutils_channel *ch)
{
  return ch->f_hi >= 0 && ch->f_lo <= 0;
}

SUPRIVATE int
suscan_channel_compare(const void *a, const void *b)
{
  struct sigutils_channel **chan_a = (struct sigutils_channel **) a;
  struct sigutils_channel **chan_b = (struct sigutils_channel **) b;

  if (chan_a[0]->snr > chan_b[0]->snr)
    return -1;
  else if (chan_a[0]->snr < chan_b[0]->snr)
    return 1;

  return 0;
}

void
suscan_channel_list_sort(struct sigutils_channel **list, unsigned int count)
{
  qsort(list, count, sizeof(struct sigutils_channel *), suscan_channel_compare);
}
