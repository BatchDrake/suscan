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

#include <string.h>

#define SU_LOG_DOMAIN "fmspect-spectsrc"

#include "spectsrc.h"

#define FMSPECT_GAIN 1e-5

void *
suscan_spectsrc_fmspect_ctor(suscan_spectsrc_t *src)
{
  return calloc(1, sizeof(SUCOMPLEX));
}

SUBOOL
suscan_spectsrc_fmspect_preproc(
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
    diff = SU_C_ARG(buffer[i] * SU_C_CONJ(prev));
    prev = buffer[i];
    buffer[i] = FMSPECT_GAIN * diff;
  }

  *last = prev;

  return SU_TRUE;
}

SUBOOL
suscan_spectsrc_fmspect_postproc(
    suscan_spectsrc_t *src,
    void *private,
    SUCOMPLEX *buffer,
    SUSCOUNT size)
{
  return SU_TRUE;
}

void
suscan_spectsrc_fmspect_dtor(void *private)
{
  free(private);
}

SUBOOL
suscan_spectsrc_fmspect_register(void)
{
  static const struct suscan_spectsrc_class class = {
    .name = "fmspect",
    .desc = "FM baseband spectrum",
    .ctor = suscan_spectsrc_fmspect_ctor,
    .preproc  = suscan_spectsrc_fmspect_preproc,
    .postproc = suscan_spectsrc_fmspect_postproc,
    .dtor = suscan_spectsrc_fmspect_dtor
  };

  SU_TRYCATCH(suscan_spectsrc_class_register(&class), return SU_FALSE);

  return SU_TRUE;
}
