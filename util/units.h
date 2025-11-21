/*

  Copyright (C) 2025 Gonzalo José Carracedo Carballal

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

#ifndef _UTIL_UNITS_H
#define _UTIL_UNITS_H

#include <sigutils/types.h>
#include <string.h>

SUINLINE const char *
suscan_units_format_frequency(SUFREQ freq, char *buf, size_t size)
{
  if (freq < 1e3)
    snprintf(buf, size, "%.0lf Hz", freq);
  else if (freq < 1e6)
    snprintf(buf, size, "%.3lf kHz", freq * 1e-3);
  else if (freq < 1e9)
    snprintf(buf, size, "%.6lf MHz", freq * 1e-6);
  else if (freq < 1e12)
    snprintf(buf, size, "%.9lf GHz", freq * 1e-9);
  else
    snprintf(buf, size, "%.12lf THz", freq * 1e-12);

  return buf;
}


SUINLINE const char *
suscan_units_format_time(SUFLOAT delta, char *buf, size_t size)
{
  unsigned int hour, min, sec;
  SUFLOAT decim;

  if (delta > 1) {
    sec = SU_FLOOR(delta);
    decim = delta - sec;

    hour = sec / 3600;
    min  = (sec / 60) % 60;
    sec %= 60;
  }

  if (delta < 1e-9)
    snprintf(buf, size, "%.3g ps", delta * 1e12);
  else if (delta < 1e-6)
    snprintf(buf, size, "%.3g ns", delta * 1e9);
  else if (delta < 1e-3)
    snprintf(buf, size, "%.3g us", delta * 1e6);
  else if (delta < 1)
    snprintf(buf, size, "%.3g ms", delta * 1e3);
  else if (delta < 60)
    snprintf(buf, size, "%.3g s", delta);
  else if (delta < 3600)
    snprintf(buf, size, "00:%02d:%02d.%.3g", min, sec, decim);
  else
    snprintf(buf, size, "%02d:%02d:%02d", hour, min, sec);

  return buf;
}

#endif /* _UTIL_UNITS_H */
