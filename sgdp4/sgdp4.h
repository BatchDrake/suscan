/*

  SGDP4 implementation, adapted from the original work by Paul S. Crawford and
  Andrew R. Brooks from Dundee University.

  Original implementation can (hopefully) still be found in
  https://github.com/cbassa/strf/

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

#ifndef _SGDP4H_H
#define _SGDP4H_H

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <memory.h>
#include <time.h>
#include <sigutils/types.h>

#define TWOPI   (2.0*PI)    /* Optimising compiler will deal with this! */
#define PB2     (0.5*PI)
#define PI180   (PI/180.0)

#define SOLAR_DAY       (1440.0)             /* Minutes per 24 hours */
#define SIDERIAL_DAY    (23.0*60.0 + 56.0 + 4.09054/60.0)   /* Against stars */

#define EQRAD   (6378.137)                   /* Earth radius at equator, km */
#define LATCON  (1.0/298.257)                /* Latitude radius constant */
#define ECON    ((1.0-LATCON)*(1.0-LATCON))

#define JD1900 2415020.5    /* Julian day number for Jan 1st, 00:00 hours 1900 */


/*
 * =============================== MACROS ============================
 *
 *
 *  Define macro for sign transfer, double to nearest (long) integer,
 *  to square an expression (not nested), and A "safe" square, uses test
 *  to force correct sequence of evaluation when the macro is nested.
 */

/*
 * These macros are safe since they make no assignments.
 */
#define SIGN2(a, b)  ((b) >= 0 ? fabs(a) : -fabs(a))
/* Coordinate conversion macros */
#define GEOC(x) (atan(ECON*tan(x))) /* Geographic to geocentric. */
#define GEOG(x) (atan(tan(x)/ECON))

/*
 * All other compilers can have SUINLINE functions.
 * (SQR is used badly here: do_cal.c, glat2lat.c, satpos.c, vmath.h).
 */
SUINLINE int       NINT(double  a) { return (int)(a > 0 ? a+0.5 : a-0.5); }
SUINLINE long      NLONG(double a) { return (long)(a > 0 ? a+0.5 : a-0.5); }
SUINLINE double    DSQR(double a) { return(a*a); }

SUINLINE double    DCUBE(double a) { return(a*a*a); }

SUINLINE double    DPOW4(double a) { a*=a; return(a*a); }

SUINLINE double    MOD2PI(double a) { a=fmod(a, TWOPI); return a < 0.0 ? a+TWOPI : a; }
SUINLINE double    MOD360(double a) { a=fmod(a, 360.0); return a < 0.0 ? a+360.0 : a; }

/*
 * Unless you have higher than default optimisation the Sun compiler
 * would prefer to be told explicitly about inline functions after their
 * declaration.
 */
#if defined(__SUNPRO_C) && !defined(MACROS_ARE_SAFE)
#pragma inline_routines(NINT, NLONG, DSQR, FSQR, ISQR, DCUBE, FCUBE, ICUBE, DPOW4, FPOW4, IPOW4)
#pragma inline_routines(DMAX, FMAX, IMAX, DMIN, FMIN, IMIN, MOD2PI, MOD360, S_GEOC, S_GEOG)
#endif

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

/* ==================================================================== */

typedef struct orbit_s {
  char   *name; /* Name of the satellite */
  /* Add the epoch time if required. */
  int     ep_year;/* Year of epoch, e.g. 94 for 1994, 100 for 2000AD */
  double  ep_day;  /* Day of epoch from 00:00 Jan 1st ( = 1.0 ) */
  double  rev;  /* Mean motion, revolutions per day */
  double  bstar;  /* Drag term .*/
  double  eqinc;  /* Equatorial inclination, radians */
  double  ecc;  /* Eccentricity */
  double  mnan;  /* Mean anomaly at epoch from elements, radians */
  double  argp;  /* Argument of perigee, radians */
  double  ascn;  /* Right ascension (ascending node), radians */
  double  smjaxs;  /* Semi-major axis, km */
  long    norb;  /* Orbit number, for elements */
  int     satno;  /* Satellite number. */
} orbit_t;

#define orbit_INITIALIZER {NULL}

SUSDIFF orbit_init_from_data(
  orbit_t *self, 
  const void *data, 
  SUSCOUNT len);

SUBOOL orbit_init_from_file(orbit_t *self, const char *file);

void orbit_epoch_to_timeval(const orbit_t *self, struct timeval *tv);

SUDOUBLE orbit_epoch_to_unix(const orbit_t *self);

SUDOUBLE orbit_minutes_from_timeval(
  const orbit_t *self,
  const struct timeval *when);
SUDOUBLE orbit_minutes(const orbit_t *self, SUDOUBLE time);

void orbit_finalize(orbit_t *self);

#define XYZ_MATMUL(d, m, v)                                          \
  do {                                                               \
    (d)->x = m[0][0] * (v)->x + m[1][0] * (v)->y + m[2][0] * (v)->z; \
    (d)->y = m[0][1] * (v)->x + m[1][1] * (v)->y + m[2][1] * (v)->z; \
    (d)->z = m[0][2] * (v)->x + m[1][2] * (v)->y + m[2][2] * (v)->z; \
  } while (0)

#define XYZ_NORM(v) sqrt((v)->x * (v)->x + (v)->y * (v)->y + (v)->z * (v)->z)

typedef struct xyz_s {
  union {
    SUDOUBLE x;
    SUDOUBLE lon;
  };
  
  union {
    SUDOUBLE y;
    SUDOUBLE lat;
  };

  union {
    SUDOUBLE z;
    SUDOUBLE height;
  };
} xyz_t;

#define xyz_INITIALIZER {0., 0., 0.}

void xyz_teme_to_ecef(
  const xyz_t *pos,
  const xyz_t *vel,
  SUDOUBLE jdut1, 
  xyz_t *ecef_pos,
  xyz_t *ecef_vel);

void xyz_ecef_to_geodetic(const xyz_t *pos, xyz_t *geo);

typedef struct kep_s {
    double theta;     /* Angle "theta" from equatorial plane (rad) = U. */
    double ascn;      /* Right ascension (rad). */
    double eqinc;     /* Equatorial inclination (rad). */
    double radius;    /* Radius (km). */
    double rdotk;
    double rfdotk;

  /*
   * Following are without short-term perturbations but used to
   * speed searchs.
   */

  double argp;  /* Argument of perigee at 'tsince' (rad). */
  double smjaxs;  /* Semi-major axis at 'tsince' (km). */
  double ecc;    /* Eccentricity at 'tsince'. */

} kep_t;

/* ================ Single or Double precision options. ================= */

#define DEFAULT_TO_SNGL 0

#if defined( SGDP4_SNGL ) || (DEFAULT_TO_SNGL && !defined( SGDP4_DBLE ))
/* Single precision option. */
// typedef float SUFLOAT;
#ifndef SGDP4_SNGL
#define SGDP4_SNGL
#endif

#else
/* Double precision option. */
// typedef double SUFLOAT;
#ifndef SGDP4_DBLE
#define SGDP4_DBLE
#endif

#endif  /* Single or double choice. */

/* Something silly ? */
#if defined( SGDP4_SNGL ) && defined( SGDP4_DBLE )
#error sgdp4h.h - Cannot have both single and double precision defined
#endif


/* ================= Stack space problems ? ======================== */

#if !defined( MSDOS )
/* Automatic variables, faster (?) but needs more stack space. */
#define LOCAL_SUFLOAT   SUFLOAT
#define LOCAL_DOUBLE double
#else
/* Static variables, slower (?) but little stack space. */
#define LOCAL_SUFLOAT   static SUFLOAT
#define LOCAL_DOUBLE static double
#endif

/* ======== Macro fixes for float/double in math.h type functions. ===== */

#define SIN(x)      (SUFLOAT)sin((double)(x))
#define COS(x)      (SUFLOAT)cos((double)(x))
#define SQRT(x)     (SUFLOAT)sqrt((double)(x))
#define FABS(x)     (SUFLOAT)fabs((double)(x))
#define POW(x,y)    (SUFLOAT)pow((double)(x), (double)(y))
#define FMOD(x,y)   (SUFLOAT)fmod((double)(x), (double)(y))
#define ATAN2(x,y)  (SUFLOAT)atan2((double)(x), (double)(y))

#ifdef SGDP4_SNGL
#define CUBE FCUBE
#define POW4 FPOW4
#else
#define CUBE DCUBE
#define POW4 DPOW4
#endif

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

/* ======================= Function prototypes ====================== */

#ifdef __cplusplus
extern "C" {
#endif

/** deep.c **/

int sgdp4_ctx_init_deep(
  sgdp4_ctx_t *self, 
  SUDOUBLE epoch);

int sgdp4_ctx_init_deep_secular(
  sgdp4_ctx_t *self, 
  double *xll, 
  SUFLOAT *omgasm, 
  SUFLOAT *xnodes, 
  SUFLOAT *em,
  SUFLOAT *xinc, 
  double *xn, 
  double tsince);

int sgdp4_ctx_init_deep_periodic(
  sgdp4_ctx_t *self, 
  SUFLOAT *em, 
  SUFLOAT *xinc, 
  SUFLOAT *omgasm, 
  SUFLOAT *xnodes,
  double *xll, 
  double tsince);

/** sgdp4.c **/
int sgdp4_ctx_init(sgdp4_ctx_t *self, orbit_t *orb);

int sgdp4_ctx_compute(
  sgdp4_ctx_t *self, 
  double tsince, 
  int withvel, 
  kep_t *kep);

void kep_get_pos_vel_teme(
  kep_t *K, 
  xyz_t *pos, 
  xyz_t *vel);

void kep_get_pos_vel_ecef(
  kep_t *K, 
  xyz_t *pos, 
  xyz_t *vel);
  
int sgdp4_ctx_get_pos_vel(
  sgdp4_ctx_t *self, 
  double jd, 
  xyz_t *pos, 
  xyz_t *vel);

SUDOUBLE time_unix_to_julian(SUDOUBLE timestamp);
SUDOUBLE time_timeval_to_julian(const struct timeval *tv);
SUDOUBLE time_julian_to_unix(SUDOUBLE jd);

#ifdef __cplusplus
}
#endif

#endif /* !_SGDP4H_H */
