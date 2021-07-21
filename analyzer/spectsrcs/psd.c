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

#define SU_LOG_DOMAIN "psd-spectsrc"

#include "spectsrc.h"

void *
suscan_spectsrc_psd_ctor(suscan_spectsrc_t *src)
{
  return src; /* Anynon-NULL works */
}

void
suscan_spectsrc_psd_dtor(void *private)
{
  /* No-op */
}

SUBOOL
suscan_spectsrc_psd_register(void)
{
  static const struct suscan_spectsrc_class class = {
    .name = "psd",
    .desc = "Power spectrum",
    .ctor = suscan_spectsrc_psd_ctor,
    .preproc = NULL,
    .dtor = suscan_spectsrc_psd_dtor
  };

  SU_TRYCATCH(suscan_spectsrc_class_register(&class), return SU_FALSE);

  return SU_TRUE;
}
