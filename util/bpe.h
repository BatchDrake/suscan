/*

  Copyright (C) 2024 Gonzalo José Carracedo Carballal

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

#ifndef _UTIL_BPE_H
#define _UTIL_BPE_H

#include <sigutils/types.h>
#include <sigutils/defs.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/*
 * The BPE (Bayesian Power Estimator) is an idea I came up with when I was
 * preparing some slides for the Micromeet 2024. It leverages a conjugate
 * prior distribution for a normal likelihood in which the variance equals
 * the mean divided by certain averaging factor.
 */

struct suscan_bpe {
  SUDOUBLE alpha, beta, lambda, mu;

  SUBOOL   have_estimate;

  SUDOUBLE pwr_mode;
  SUDOUBLE pwr_delta;

  SUSCOUNT n;
};

typedef struct suscan_bpe suscan_bpe_t;

SU_CONSTRUCTOR(suscan_bpe);

SU_METHOD(suscan_bpe, void, feed, SUDOUBLE, SUDOUBLE);
SU_METHOD(suscan_bpe, SUDOUBLE, get_power);
SU_METHOD(suscan_bpe, SUDOUBLE, get_dispersion);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _UTIL_BPE_H */
