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

#define SU_LOG_DOMAIN "exp_4-spectsrc"

#include "spectsrc.h"

void *
suscan_spectsrc_exp_4_ctor(suscan_spectsrc_t *src)
{
  return src;
}

SUBOOL
suscan_spectsrc_exp_4_preproc(
    suscan_spectsrc_t *src,
    void *private,
    SUCOMPLEX *buffer,
    SUSCOUNT size)
{
  SUSCOUNT i;

  for (i = 0; i < size; ++i)
    buffer[i] = cpow(buffer[i] / (SU_C_ABS(buffer[i]) + 1e-8), 4);

  return SU_TRUE;
}

SUBOOL
suscan_spectsrc_exp_4_postproc(
    suscan_spectsrc_t *src,
    void *private,
    SUCOMPLEX *buffer,
    SUSCOUNT size)
{
  return SU_TRUE;
}

void
suscan_spectsrc_exp_4_dtor(void *private)
{
  /* No-op */
}

SUBOOL
suscan_spectsrc_exp_4_register(void)
{
  static const struct suscan_spectsrc_class class = {
    .name = "exp_4",
    .desc = "Signal exponentiation (^4)",
    .ctor = suscan_spectsrc_exp_4_ctor,
    .preproc  = suscan_spectsrc_exp_4_preproc,
    .postproc = suscan_spectsrc_exp_4_postproc,
    .dtor = suscan_spectsrc_exp_4_dtor
  };

  SU_TRYCATCH(suscan_spectsrc_class_register(&class), return SU_FALSE);

  return SU_TRUE;
}
