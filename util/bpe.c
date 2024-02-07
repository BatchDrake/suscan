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

#define SU_LOG_DOMAIN "bpe"

#include <sigutils/log.h>
#include <string.h>

#include "bpe.h"

#define SUCAN_BPE_NEWTON_TOL 1e-8

SU_CONSTRUCTOR(suscan_bpe)
{
  memset(self, 0, sizeof(suscan_bpe_t));

  /* Initialize the improper prior. */
  self->alpha = -1;

  return SU_TRUE;
}

/* Perform a Bayesian update */
SU_METHOD(suscan_bpe, void, feed, SUDOUBLE x, SUDOUBLE k)
{
  SUDOUBLE prevLambda   = self->lambda;
  SUDOUBLE prevMu       = self->mu;
  SUDOUBLE prevMuLambda = prevLambda * prevMu;
  SUDOUBLE kx = k * x;

  self->alpha  += .5;
  self->lambda += k;
  self->mu      = (prevMuLambda + kx) / self->lambda;
  self->beta   += .5 * (prevMuLambda * prevMu + kx * x - self->lambda * self->mu * self->mu);

  if (self->have_estimate)
    self->have_estimate = SU_FALSE;
}

/* Calculate mode as central measure of the current power */
SUINLINE
SU_METHOD(suscan_bpe, SUDOUBLE, calc_mode)
{
  SUDOUBLE a, b, c;
  
  a = -(2 * self->alpha + 3);
  b = -self->lambda * self->mu;
  c = 2 * self->beta + self->lambda * self->mu * self->mu;

  return (-b - SU_SQRT(b * b - 4 * a * c)) / (2 * a);
}

SUINLINE SUDOUBLE
flex_poly(SUDOUBLE x, const SUDOUBLE *c)
{
  return (((c[4] * x + c[3]) * x + c[2]) * x + c[1]) * x + c[0];
}

SUINLINE SUDOUBLE
flex_dpdx(SUDOUBLE x, const SUDOUBLE *c)
{
  return ((4 * c[4] * x + 3 * c[3]) * x + 2 * c[2]) * x + c[1];
}

SUINLINE SUDOUBLE
find_flex(SUDOUBLE x0, const SUDOUBLE *c)
{
  SUDOUBLE xn, reldiff;
  SUSCOUNT n =0;

  do {
    xn = x0 - flex_poly(x0, c) / flex_dpdx(x0, c);
    reldiff = SU_ABS(xn - x0) / (SU_ABS(x0) + SU_ABS(xn));
    x0 = xn;
    ++n;
  } while (reldiff > SUCAN_BPE_NEWTON_TOL);

  return xn;
}

/* Calculate dispersion. This is my favourite one. */
SUINLINE
SU_METHOD(suscan_bpe, SUDOUBLE, calc_dispersion, SUDOUBLE mode)
{
  SUDOUBLE c[5];

  SUDOUBLE a = self->alpha;
  SUDOUBLE b = self->beta;
  SUDOUBLE l = self->lambda;
  SUDOUBLE m = self->mu;

  SUDOUBLE a2 = a * a;
  SUDOUBLE b2 = b * b;
  SUDOUBLE l2 = l * l;
  SUDOUBLE m2 = m * m;
  SUDOUBLE m3 = m * m2;
  SUDOUBLE m4 = m2 * m2;

  SUDOUBLE d1, d2;

  c[4] = 4 * a2 + 14 * a + 12;
  c[3] = 4 * m * a * l + 8 * m * l;
  c[2] = m2 * l2 - 4 * m2 * a * l - 9 * m2 * l - 8 * a * b - 18 * b;
  c[1] = -2 * m3 * l2 - 4 * m * b * l;
  c[0] = m4 * l2 + 4 * m2 * b * l + 4 * b2;

  d1 = find_flex(0.5 * mode, c);
  d2 = find_flex(1.5 * mode, c);

  return .5 * (d2 - d1);
}

SUINLINE
SU_METHOD(suscan_bpe, void, ensure_estimates)
{
  if (!self->have_estimate) {
    self->pwr_mode  = suscan_bpe_calc_mode(self);
    self->pwr_delta = suscan_bpe_calc_dispersion(self, self->pwr_mode);
    self->have_estimate = SU_TRUE;
  }
}

SU_METHOD(suscan_bpe, SUDOUBLE, get_power)
{
  suscan_bpe_ensure_estimates(self);

  return self->pwr_mode;
}

SU_METHOD(suscan_bpe, SUDOUBLE, get_dispersion)
{
  suscan_bpe_ensure_estimates(self);

  return self->pwr_delta;
}
