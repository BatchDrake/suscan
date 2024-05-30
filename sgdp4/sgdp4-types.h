/*

  SGDP4 implementation, adapted from the original work by Paul S. Crawford and
  Andrew R. Brooks from Dundee University.

  Original implementation can (hopefully) still be found in
  https://github.com/cbassa/strf/

  Copyright (C) 2021 Gonzalo José Carracedo Carballal

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

#ifndef _SGDP4_TYPES_H
#define _SGDP4_TYPES_H

#include <sigutils/types.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* SGDP4 function return values. */
enum sgdp4_status_code {
  SGDP4_ERROR     = -1,
  SGDP4_NOT_INIT  = 0,
  SGDP4_ZERO_ECC  = 1,
  SGDP4_NEAR_SIMP = 2,
  SGDP4_NEAR_NORM = 3,
  SGDP4_DEEP_NORM = 4,
  SGDP4_DEEP_RESN = 5,
  SGDP4_DEEP_SYNC = 6
};

/*
 * Original SGP4/SDP4 put all its state in global variables. This
 * is not acceptable for 21st century standards. The following object
 * encapsulates the global state into something that can be allocated
 * somewhere else.
 */

struct sgdp4_ctx {
  /* TLE parameters */
  SUDOUBLE xno;  /* Mean motion (rad/min) */
  SUFLOAT xmo;    /* Mean "mean anomaly" at epoch (rad). */
  SUFLOAT eo;     /* Eccentricity. */
  SUFLOAT xincl;  /* Equatorial inclination (rad). */
  SUFLOAT omegao; /* Mean argument of perigee at epoch (rad). */
  SUFLOAT xnodeo; /* Mean longitude of ascending node (rad, east). */
  SUFLOAT bstar;  /* Drag term. */
  SUDOUBLE SGDP4_jd0;  /* Julian Day for epoch (available to outside functions. */

  int imode;

  /* SGP4 state */
  SUFLOAT sinIO, cosIO, sinXMO, cosXMO;
  SUFLOAT c1, c2, c3, c4, c5, d2, d3, d4;
  SUFLOAT omgcof, xmcof, xlcof, aycof;
  SUFLOAT t2cof, t3cof, t4cof, t5cof;
  SUFLOAT xnodcf, delmo, x7thm1, x3thm1, x1mth2;
  SUFLOAT aodp, eta, omgdot, xnodot;
  SUDOUBLE xnodp, xmdot;

  /* SDP4 state */
  int isynfl, iresfl;

  SUDOUBLE atime, xli, xni, xnq, xfact;

  SUFLOAT ssl, ssg, ssh, sse, ssi;
  SUFLOAT xlamo, omegaq, omgdt, thgr;
  SUFLOAT del1, del2, del3, fasx2, fasx4, fasx6;
  SUFLOAT d2201, d2211, d3210, d3222, d4410, d4422;
  SUFLOAT d5220, d5232, d5421, d5433;

  SUFLOAT xnddt, xndot, xldot;  /* Integrator terms. */
  SUFLOAT xnddt0, xndot0, xldot0; /* Integrator at epoch. */

  int ilsd, ilsz;

  SUFLOAT zmos, se2, se3, si2, si3, sl2, sl3, sl4;
  SUFLOAT sgh2, sgh3, sgh4, sh2, sh3;
  SUFLOAT zmol, ee2, e3 ,xi2, xi3, xl2, xl3, xl4;
  SUFLOAT xgh2, xgh3, xgh4, xh2, xh3;

  SUFLOAT pe, pinc, pgh, ph, pl;
  SUFLOAT pgh0, ph0, pe0, pinc0, pl0; /* Added terms to save the epoch values of perturbations. */

  int Set_LS_zero; /* Set to 1 to zero Lunar-Solar terms at epoch. */
  long Isat;  /* 16-bit compilers need 'long' integer for higher space catalogue numbers. */
  SUDOUBLE perigee, period, apogee;

  long Icount;
  int MaxNR;
};

typedef struct sgdp4_ctx sgdp4_ctx_t;

#define sgdp4_ctx_INITIALIZER {0}

typedef struct orbit_s {
  char   *name;     /* Name of the satellite */
  /* Add the epoch time if required. */
  int32_t ep_year;  /* Year of epoch, e.g. 94 for 1994, 100 for 2000AD */
  double  ep_day;   /* Day of epoch from 00:00 Jan 1st ( = 1.0 ) */
  double  rev;      /* Mean motion, revolutions per day */
  double  drevdt;   /* First derivative of mean motion */
  double  d2revdt2; /* Second derivative of mean motion */
  double  bstar;    /* Drag term */
  double  eqinc;    /* Equatorial inclination, radians */
  double  ecc;      /* Eccentricity */
  double  mnan;     /* Mean anomaly at epoch from elements, radians */
  double  argp;     /* Argument of perigee, radians */
  double  ascn;     /* Right ascension (ascending node), radians */
  double  smjaxs;   /* Semi-major axis, km */
  int64_t norb;     /* Orbit number, for elements */
  int32_t satno;    /* Satellite number. */
} orbit_t;

#define orbit_INITIALIZER {                     \
  NULL,                                         \
  0,                                            \
  .0, .0, .0, .0, .0, .0, .0, .0, .0, .0, .0,   \
  0,                                            \
  0}

SUBOOL orbit_is_geo(const orbit_t *orbit);
SUBOOL orbit_is_decayed(const orbit_t *orbit, const struct timeval *tv);

typedef struct xyz_s {
  union {
    SUDOUBLE x;
    SUDOUBLE lon;
    SUDOUBLE azimuth;
  };
  
  union {
    SUDOUBLE y;
    SUDOUBLE lat;
    SUDOUBLE elevation;
  };

  union {
    SUDOUBLE z;
    SUDOUBLE height;
    SUDOUBLE distance;
  };
} xyz_t;

#define xyz_INITIALIZER {0., 0., 0.}

typedef struct kep_s {
  SUDOUBLE theta;     /* Angle "theta" from equatorial plane (rad) = U. */
  SUDOUBLE ascn;      /* Right ascension (rad). */
  SUDOUBLE eqinc;     /* Equatorial inclination (rad). */
  SUDOUBLE radius;    /* Radius (km). */
  SUDOUBLE rdotk;
  SUDOUBLE rfdotk;

  /*
   * Following are without short-term perturbations but used to
   * speed searchs.
   */

  SUDOUBLE argp;  /* Argument of perigee at 'tsince' (rad). */
  SUDOUBLE smjaxs;  /* Semi-major axis at 'tsince' (km). */
  SUDOUBLE ecc;    /* Eccentricity at 'tsince'. */
} kep_t;

struct suscan_orbit_report {
  struct timeval rx_time;
  xyz_t          satpos;
  SUFLOAT        freq_corr;
  SUDOUBLE       vlos_vel;
};

struct sgdp4_prediction {
  sgdp4_ctx_t    ctx;
  orbit_t        orbit;
  xyz_t          site;
  struct timeval tv;
  SUBOOL         init;

  /* Predicted members */
  kep_t          state;
  SUDOUBLE       alt;
  xyz_t          pos_ecef;
  xyz_t          vel_ecef;

  xyz_t          pos_azel;
  xyz_t          vel_azel;
};

typedef struct sgdp4_prediction sgdp4_prediction_t;

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _SGDP4_TYPES_H */

