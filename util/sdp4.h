/*

  SDP4 implementation, adapted from libpredict's implementation in
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

#ifndef _UTIL_SDP4_H_
#define _UTIL_SDP4_H_

#include <sigutils/types.h>

enum suscan_ephemeris_model {
  EPHEMERIS_SGP4 = 0,
  EPHEMERIS_SDP4 = 1,
  EPHEMERIS_SGP8 = 2,
  EPHEMERIS_SDP8 = 3
};

struct suscan_tle {
  enum suscan_ephemeris_model model;

  int satellite_number;
  long element_number;
  char designator[10];
  int epoch_year;
  SUDOUBLE epoch_day;
  SUDOUBLE inclination;
  SUDOUBLE right_ascension;
  SUDOUBLE eccentricity;
  SUDOUBLE argument_of_perigee;
  SUDOUBLE mean_anomaly;
  SUDOUBLE mean_motion;
  SUDOUBLE derivative_mean_motion;
  SUDOUBLE second_derivative_mean_motion;
  SUDOUBLE bstar_drag_term;
  int revolutions_at_epoch;

  void *ephemeris_data;
};

typedef struct suscan_tle suscan_tle_t;

struct suscan_ephemeris_model_output {
  SUDOUBLE xinck;
  SUDOUBLE omgadf;
  SUDOUBLE xnodek;

  SUDOUBLE pos[3];
  SUDOUBLE vel[3];

  SUDOUBLE phase;
};


struct suscan_deep_arg_fixed {
  SUDOUBLE eosq;
  SUDOUBLE sinio;
  SUDOUBLE cosio;
  SUDOUBLE betao;
  SUDOUBLE aodp;
  SUDOUBLE theta2;
  SUDOUBLE sing;
  SUDOUBLE cosg;
  SUDOUBLE betao2;
  SUDOUBLE xmdot;
  SUDOUBLE omgdot;
  SUDOUBLE xnodot;
  SUDOUBLE xnodp;

  SUDOUBLE ds50;
};

typedef struct suscan_deep_arg_fixed suscan_deep_arg_fixed_t;

struct suscan_deep_arg_dynamic {
  SUDOUBLE xll;
  SUDOUBLE omgadf;
  SUDOUBLE xnode;
  SUDOUBLE em;
  SUDOUBLE xinc;
  SUDOUBLE xn;
  SUDOUBLE t;

  SUDOUBLE pl;
  SUDOUBLE pinc;
  SUDOUBLE pe;
  SUDOUBLE sh1;
  SUDOUBLE sghl;
  SUDOUBLE shs;
  SUDOUBLE savtsn;
  SUDOUBLE atime;
  SUDOUBLE xni;
  SUDOUBLE xli;
  SUDOUBLE sghs;

  int loop_flag;

  int epoch_restart_flag;
};

typedef struct suscan_deep_arg_dynamic suscan_deep_arg_dynamic_t;

struct suscan_sdp4_params {
  SUBOOL lunar_terms_done;
  SUBOOL resonance_flag;
  SUBOOL synchronous_flag;

  SUDOUBLE x3thm1, c1, x1mth2, c4, xnodcf, t2cof, xlcof,
  aycof, x7thm1;
  suscan_deep_arg_fixed_t deep_arg;

  SUDOUBLE thgr, xnq, xqncl, omegaq, zmol, zmos, ee2, e3,
    xi2, xl2, xl3, xl4, xgh2, xgh3, xgh4, xh2, xh3, sse, ssi, ssg, xi3,
    se2, si2, sl2, sgh2, sh2, se3, si3, sl3, sgh3, sh3, sl4, sgh4, ssl,
    ssh, d3210, d3222, d4410, d4422, d5220, d5232, d5421, d5433, del1,
    del2, del3, fasx2, fasx4, fasx6, xlamo, xfact, stepp,
    stepn, step2, preep, d2201, d2211,
    zsingl, zcosgl, zsinhl, zcoshl, zsinil, zcosil;

  SUDOUBLE xnodeo;
  SUDOUBLE omegao;
  SUDOUBLE xmo;
  SUDOUBLE xincl;
  SUDOUBLE eo;
  SUDOUBLE xno;
  SUDOUBLE bstar;
  SUDOUBLE epoch;
};

SUINLINE SUDOUBLE
suscan_sdp4_year_to_jd(SUDOUBLE year)
{
  /* Astronomical Formulae for Calculators, Jean Meeus, */
  /* pages 23-25. Calculate Julian Date of 0.0 Jan year */

  int A, B, i;
  SUDOUBLE jdoy;

  year = year - 1;
  i    = year / 100;
  A    = i;
  i    = A / 4;
  B    = 2 - A + i;
  i    = 365.25 * year;
  i   += 30.6001 * 14;
  jdoy = i + 1720994.5 + B;

  return jdoy;
}


void suscan_sdp4_params_init(
    const suscan_tle_t *orbital_elements,
    struct suscan_sdp4_params *m);

void suscan_sdp4_predict(
    const struct suscan_sdp4_params *m,
    double tsince,
    struct suscan_ephemeris_model_output *output);

#endif // ifndef _SDP4_H_
