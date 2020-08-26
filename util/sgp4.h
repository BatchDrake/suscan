/*

  SGP4 implementation, adapted from libpredict's implementation in
  https://github.com/la1k/libpredict, originally released under license
  GPL 2.0.

  Copyright (C) 2020 Gonzalo José Carracedo Carballal

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

#ifndef _UTIL_SGP4_H
#define _UTIL_SGP4_H

#include "sdp4.h"

struct suscan_sgp4_params {
  SUBOOL simple_flag;

  SUDOUBLE aodp, aycof, c1, c4, c5, cosio, d2, d3, d4, delmo,
    omgcof, eta, omgdot, sinio, xnodp, sinmo, t2cof, t3cof, t4cof,
    t5cof, x1mth2, x3thm1, x7thm1, xmcof, xmdot, xnodcf, xnodot, xlcof;

  SUDOUBLE bstar;
  SUDOUBLE xincl;
  SUDOUBLE xnodeo;
  SUDOUBLE eo;
  SUDOUBLE omegao;
  SUDOUBLE xmo;
  SUDOUBLE xno;
};

void suscan_sgp4_params_init(
    const suscan_tle_t *orbital_elements,
    struct suscan_sgp4_params *m);

void sgp4_predict(
    const struct suscan_sgp4_params *m,
    double tsince,
    struct suscan_ephemeris_model_output *output);

#endif /* _UTIL_SGP4_H */
