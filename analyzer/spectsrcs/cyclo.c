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

#include <string.h>

#define SU_LOG_DOMAIN "cyclo-spectsrc"

#include "spectsrc.h"

#define SU_CYCLO_GAIN 1e6

void *
suscan_spectsrc_cyclo_ctor(suscan_spectsrc_t *src)
{
  return calloc(1, sizeof(SUCOMPLEX));
}

SUBOOL
suscan_spectsrc_cyclo_preproc(
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
    diff = buffer[i] * SU_C_CONJ(prev);
    prev = buffer[i];
    buffer[i] = SU_CYCLO_GAIN * diff;
  }

  *last = prev;

  return SU_TRUE;
}

void
suscan_spectsrc_cyclo_dtor(void *private)
{
  free(private);
}

SUBOOL
suscan_spectsrc_cyclo_register(void)
{
  static const struct suscan_spectsrc_class class = {
    .name = "cyclo",
    .desc = "Cyclostationary analysis",
    .ctor = suscan_spectsrc_cyclo_ctor,
    .preproc  = suscan_spectsrc_cyclo_preproc,
    .dtor = suscan_spectsrc_cyclo_dtor
  };

  SU_TRYCATCH(suscan_spectsrc_class_register(&class), return SU_FALSE);

  return SU_TRUE;
}
