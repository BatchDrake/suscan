/*

  Copyright (C) 2019 Gonzalo José Carracedo Carballal

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

#include <string.h>

#define SU_LOG_DOMAIN "timediff-spectsrc"

#include "spectsrc.h"

void *
suscan_spectsrc_timediff_ctor(suscan_spectsrc_t *src)
{
  return calloc(1, sizeof(SUCOMPLEX));
}

SUBOOL
suscan_spectsrc_timediff_preproc(
    suscan_spectsrc_t *src,
    void *private,
    SUCOMPLEX *buffer,
    SUSCOUNT size)
{
  SUCOMPLEX *last = (SUCOMPLEX *) private;
  SUCOMPLEX prev = *last;
  SUCOMPLEX diff;
  SUSCOUNT i;

  for (i = 0; i < size; ++i) {
    diff = buffer[i] - prev;
    prev = buffer[i];
    buffer[i] = diff;
  }

  *last = prev;

  return SU_TRUE;
}

SUBOOL
suscan_spectsrc_abstimediff_preproc(
    suscan_spectsrc_t *src,
    void *private,
    SUCOMPLEX *buffer,
    SUSCOUNT size)
{
  SUCOMPLEX *last = (SUCOMPLEX *) private;
  SUCOMPLEX prev = *last;
  SUCOMPLEX diff;
  SUSCOUNT i;

  for (i = 0; i < size; ++i) {
    diff = buffer[i] - prev;
    prev = buffer[i];
    buffer[i] = diff * SU_C_CONJ(diff);
  }

  *last = prev;

  return SU_TRUE;
}

SUBOOL
suscan_spectsrc_timediff_postproc(
    suscan_spectsrc_t *src,
    void *private,
    SUCOMPLEX *buffer,
    SUSCOUNT size)
{
  return SU_TRUE;
}

void
suscan_spectsrc_timediff_dtor(void *private)
{
  free(private);
}

SUBOOL
suscan_spectsrc_timediff_register(void)
{
  static const struct suscan_spectsrc_class classsgn = {
    .name = "timediff",
    .desc = "Time derivative",
    .ctor = suscan_spectsrc_timediff_ctor,
    .preproc  = suscan_spectsrc_timediff_preproc,
    .postproc = suscan_spectsrc_timediff_postproc,
    .dtor = suscan_spectsrc_timediff_dtor
  };

  static const struct suscan_spectsrc_class classabs = {
    .name = "abstimediff",
    .desc = "Absolute value of time derivative",
    .ctor = suscan_spectsrc_timediff_ctor,
    .preproc  = suscan_spectsrc_abstimediff_preproc,
    .postproc = suscan_spectsrc_timediff_postproc,
    .dtor = suscan_spectsrc_timediff_dtor
  };


  SU_TRYCATCH(suscan_spectsrc_class_register(&classsgn), return SU_FALSE);
  SU_TRYCATCH(suscan_spectsrc_class_register(&classabs), return SU_FALSE);

  return SU_TRUE;
}
