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

#define SU_LOG_DOMAIN "fmcyclo-spectsrc"

#include "spectsrc.h"

#define FMCYCLO_GAIN 1e-5

struct fmcyclo_ctx {
  SUCOMPLEX fm_prev;
  SUFLOAT   pd_prev;
};

void *
suscan_spectsrc_fmcyclo_ctor(suscan_spectsrc_t *src)
{
  return calloc(1, sizeof(struct fmcyclo_ctx));
}

SUBOOL
suscan_spectsrc_fmcyclo_preproc(
    suscan_spectsrc_t *src,
    void *private,
    SUCOMPLEX *buffer,
    SUSCOUNT size)
{
  struct fmcyclo_ctx *ctx = (struct fmcyclo_ctx *) private;
  SUCOMPLEX fm_prev = ctx->fm_prev;
  SUFLOAT   pd_prev = ctx->pd_prev;
  SUFLOAT   phase_diff;
  SUSCOUNT i;

  for (i = 0; i < size; ++i) {
    phase_diff = SU_C_ARG(buffer[i] * SU_C_CONJ(fm_prev));
    fm_prev = buffer[i];
    buffer[i] = FMCYCLO_GAIN * SU_ABS(phase_diff - pd_prev);
    pd_prev = phase_diff;
  }

  ctx->fm_prev = fm_prev;
  ctx->pd_prev = pd_prev;

  return SU_TRUE;
}

SUBOOL
suscan_spectsrc_fmcyclo_postproc(
    suscan_spectsrc_t *src,
    void *private,
    SUCOMPLEX *buffer,
    SUSCOUNT size)
{
  return SU_TRUE;
}

void
suscan_spectsrc_fmcyclo_dtor(void *private)
{
  free(private);
}

SUBOOL
suscan_spectsrc_fmcyclo_register(void)
{
  static const struct suscan_spectsrc_class class = {
    .name = "fmcyclo",
    .desc = "FM cyclostationary analysis",
    .ctor = suscan_spectsrc_fmcyclo_ctor,
    .preproc  = suscan_spectsrc_fmcyclo_preproc,
    .postproc = suscan_spectsrc_fmcyclo_postproc,
    .dtor = suscan_spectsrc_fmcyclo_dtor
  };

  SU_TRYCATCH(suscan_spectsrc_class_register(&class), return SU_FALSE);

  return SU_TRUE;
}
