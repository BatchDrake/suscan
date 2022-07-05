/* > deep.c
 *
 * 1.00 around 1980 - Felix R. Hoots & Ronald L. Roehrich, from original
 *          DEEP.FOR used in the SGP deep-space models SDP4
 *          and SDP8.
 *
 ************************************************************************
 *
 *   Made famous by the spacetrack report No.3:
 *   "Models for Propogation of NORAD Element Sets"
 *   Edited and subsequently distributed by Dr. T. S. Kelso.
 *
 ************************************************************************
 *
 *   This conversion by:
 *   Paul S. Crawford and Andrew R. Brooks
 *   Dundee University
 *
 *              NOTE !
 *  This code is supplied "as is" and without warranty of any sort.
 *
 * (c) 1994-2004, Paul Crawford, Andrew Brooks
 *
 ************************************************************************
 *
 * 2.00 psc Mon Dec 19 1994 - Translated from FORTRAN into 'C' (of sorts).
 *
 * 2.01 psc Wed Dec 21 1994 - Re-write of the secular integrator from a
 *              messy FORTRAN block in to something which
 *              (hopefully!) is understandable.
 *
 * 2.02 psc Thu Dec 22 1994 - Final mods and tested against the FORTRAN
 *              version (using ~12 hour resonant and
 *              geostationary (~24 hour) elements).
 *
 * 2.03 psc Mon Jan 02 1995 - Some additional refinements and error-traps.
 *
 * 3.00 psc Mon May 29 1995 - Cleaned up for general use & distrabution (to
 *              remove Dundee specific features).
 *
 * 3.01 psc Mon Jan 12 2004 - Final fix agreed for "Lyddane bug".
 * 3.02 psc Mon Jul 03 2006 - Extended range "Lyddane bug" fix.
 * 3.03 psc Tue Jul 04 2006 - Bug fix for extended range "Lyddane bug" fix.
 */

#define SU_LOG_DOMAIN "sgdp4-deep"

#include <sigutils/types.h>
#include <sigutils/log.h>

#ifndef NO_DEEP_SPACE

#include "sgdp4.h"

/* ======================= Function prototypes ====================== */

static void dot_terms_calculated(sgdp4_ctx_t *self);
static void compute_LunarSolar(sgdp4_ctx_t *self, double tsince);
static void thetag(double ep, SUFLOAT *thegr, double *days50);

/* ===================== Strange constants, etc ===================== */

#define ZNS (SUIMM(1.19459e-5))
#define C1SS (SUIMM(2.9864797e-6))
#define ZES (SUIMM(0.01675))

#define ZNL (SUIMM(1.5835218e-4))
#define C1L (SUIMM(4.7968065e-7))
#define ZEL (SUIMM(0.0549))

#define ZCOSIS (SUIMM(0.91744867))
#define ZSINIS (SUIMM(0.39785416))
#define ZCOSGS (SUIMM(0.1945905))
#define ZSINGS (SUIMM(-0.98088458))

#define Q22 (SUIMM(1.7891679e-6))
#define Q31 (SUIMM(2.1460748e-6))
#define Q33 (SUIMM(2.2123015e-7))

#define G22 (SUIMM(5.7686396))
#define G32 (SUIMM(0.95240898))
#define G44 (SUIMM(1.8014998))
#define G52 (SUIMM(1.0508330))
#define G54 (SUIMM(4.4108898))

#define ROOT22 (SUIMM(1.7891679e-6))
#define ROOT32 (SUIMM(3.7393792e-7))
#define ROOT44 (SUIMM(7.3636953e-9))
#define ROOT52 (SUIMM(1.1428639e-7))
#define ROOT54 (SUIMM(2.1765803e-9))

#define THDT (SUIMM(4.37526908801129966e-3))
//#define THDT  (SUIMM(0.0043752691))

#define STEP 720.0
#define MAX_INTEGRATE (STEP * 10000)
#define SIN_EPS (SUFLOAT)(1.0e-12)

/* ======= Global variables used by dpsec(), from dpinit(). ======== */

/* ======== Global Variables used by dpper(), from dpinit(). ======= */

/* ==================================================================

   ----------------- DEEP SPACE INITIALIZATION ----------------------

  epoch   : Input, epoch time as YYDDD.DDDD as read from 2-line elements.
  omegao  : Input, argument of perigee from elements, radian.
  xnodeo  : Input, right asc. for ascn node from elements, radian.
  xmo     : Input, mean anomaly from elements, radian.
  orb_eo  : Input, eccentricity from elements, dimentionless.
  orb_xincl : Input, equatorial inclination from elements, radian.
  aodp    : Input, original semi-major axis, earth radii.
  self->xmdot  : Input, 1st derivative of "mean anomaly" (xmdot), radian/min.
  omgdot  : Input, 1st derivative of arg. per., radian/min.
  xnodot  : Input, 1st derivative of right asc., radian/min.
  xnodp   : Input, original mean motion, radian/min.

   ================================================================== */

int
sgdp4_ctx_init_deep(sgdp4_ctx_t *self, SUDOUBLE epoch)
{
  SUDOUBLE ds50, day, xnodce, bfact = 0, gam, c;
  SUFLOAT ctem, sinq, cosq, aqnv, stem, eqsq, xnoi, ainv2;
  SUFLOAT zcosg, zsing, zcosi, zsini, zcosh, zsinh;
  SUFLOAT cosomo, zcosgl, zcoshl, zcosil, sinomo;
  SUFLOAT xpidot, zsinil, siniq2, cosiq2;
  SUFLOAT rteqsq, zsinhl, zsingl;
  SUFLOAT eoc, sgh, g200, bsq, xno2;
  SUFLOAT a1, a2, a3, a4, a5, a6, a7, a8, a9, a10;
  SUFLOAT x1, x2, x3, x4, x5, x6, x7, x8;
  SUFLOAT z1, z2, z3, z11, z12, z13, z21, z22, z23, z31, z32, z33;
  SUFLOAT s1, s2, s3, s4, s5, s6, s7, cc, ao, eq, se, shdq, si, sl;
  SUFLOAT zx, zy, ze, zn;
  SUFLOAT g201, g211, g310, g300, g322, g410, g422, g520, g533, g521, g532;
  SUFLOAT f220, f221, f311, f321, f322, f330, f441, f442, f522, f523, f542, f543;
  SUFLOAT siniq, cosiq;
  SUFLOAT temp0, temp1;
  int ls, imode = 0;
  int ishq;

  eq = self->eo;

  /* Decide on direct or Lyddane Lunar-Solar perturbations. */
  self->ilsd = 0;
  if (self->xincl >= SUIMM(0.2))
    self->ilsd = 1;

  /* Drop some terms below 3 deg inclination. */
  ishq = 0;
#define SHQT 0.052359877
  if (self->xincl >= (SUFLOAT)SHQT)
    ishq = 1; /* As per reoprt #3. */

  SU_SINCOS(self->omegao, &sinomo, &cosomo);
  SU_SINCOS(self->xnodeo, &sinq, &cosq);
  SU_SINCOS(self->xincl, &siniq, &cosiq);

  if (fabs(siniq) <= SIN_EPS)
    siniq = SIGN2(SIN_EPS, siniq);

  cosiq2 = cosiq * cosiq;
  siniq2 = siniq * siniq;

  ao     = self->aodp;
  eqsq   = self->eo * self->eo;
  bsq    = SUIMM(1) - eqsq;
  rteqsq = SU_SQRTX(bsq);
  thetag(epoch, &self->thgr, &ds50);

  /*printf("# epoch = %.8f ds50 = %.8f thgr = %f\n", epoch, ds50, SU_RAD2DEG(thgr));*/

  aqnv   = SUIMM(1) / ao;
  xpidot = self->omgdot + self->xnodot;

  /* INITIALIZE LUNAR SOLAR TERMS */
  day        = ds50 + 18261.5;
  xnodce     = 4.523602 - day * 9.2422029e-4;
  temp0      = (SUFLOAT)fmod(xnodce, TWOPI);
  SU_SINCOS(temp0, &stem, &ctem);

  zcosil     = SUIMM(0.91375164) - ctem * SUIMM(0.03568096);
  zsinil     = SU_SQRTX(SUIMM(1) - zcosil * zcosil);
  zsinhl     = stem * SUIMM(0.089683511) / zsinil;
  zcoshl     = SU_SQRTX(SUIMM(1) - zsinhl * zsinhl);
  c          = day * 0.2299715 + 4.7199672;
  gam        = day * 0.001944368 + 5.8351514;
  self->zmol = (SUFLOAT)MOD2PI(c - gam);
  zx         = stem * SUIMM(0.39785416) / zsinil;
  zy         = zcoshl * ctem + zsinhl * SUIMM(0.91744867) * stem;
  zx         = ATAN2(zx, zy);
  zx         = (SUFLOAT)fmod(gam + zx - xnodce, TWOPI);
  SU_SINCOS(zx, &zsingl, &zcosgl);
  self->zmos = (SUFLOAT)MOD2PI(day * 0.017201977 + 6.2565837);

  /* DO SOLAR TERMS */

  zcosg = ZCOSGS;
  zsing = ZSINGS;
  zcosi = ZCOSIS;
  zsini = ZSINIS;
  zcosh = cosq;
  zsinh = sinq;
  cc    = C1SS;
  zn    = ZNS;
  ze    = ZES;
  xnoi  = (SUFLOAT)(1.0 / self->xnodp);

  for (ls = 0; ls < 2; ls++) {
    a1  = zcosg * zcosh + zsing * zcosi * zsinh;
    a3  = -zsing * zcosh + zcosg * zcosi * zsinh;
    a7  = -zcosg * zsinh + zsing * zcosi * zcosh;
    a8  = zsing * zsini;
    a9  = zsing * zsinh + zcosg * zcosi * zcosh;
    a10 = zcosg * zsini;
    a2  = cosiq * a7 + siniq * a8;
    a4  = cosiq * a9 + siniq * a10;
    a5  = -siniq * a7 + cosiq * a8;
    a6  = -siniq * a9 + cosiq * a10;

    x1  = a1 * cosomo + a2 * sinomo;
    x2  = a3 * cosomo + a4 * sinomo;
    x3  = -a1 * sinomo + a2 * cosomo;
    x4  = -a3 * sinomo + a4 * cosomo;
    x5  = a5 * sinomo;
    x6  = a6 * sinomo;
    x7  = a5 * cosomo;
    x8  = a6 * cosomo;

    z31 = x1 * SUIMM(12) * x1 - x3 * SUIMM(3) * x3;
    z32 = x1 * SUIMM(24) * x2 - x3 * SUIMM(6) * x4;
    z33 = x2 * SUIMM(12) * x2 - x4 * SUIMM(3) * x4;
    z1  = (a1 * a1 + a2 * a2) * SUIMM(3) + z31 * eqsq;
    z2  = (a1 * a3 + a2 * a4) * SUIMM(6) + z32 * eqsq;
    z3  = (a3 * a3 + a4 * a4) * SUIMM(3) + z33 * eqsq;
    z11 = a1 * SUIMM(-6) * a5 + eqsq * (x1 * SUIMM(-24) * x7 - x3 *
                                         SUIMM(6) * x5);
    z12 = (a1 * a6 + a3 * a5) * SUIMM(-6) + eqsq * ((x2 * x7 +
                               x1 * x8) *
                                SUIMM(-24) -
                              (x3 * x6 + x4 * x5) * SUIMM(6));
    z13 = a3 * SUIMM(-6) * a6 + eqsq * (x2 * SUIMM(-24) * x8 - x4 *
                                         SUIMM(6) * x6);
    z21 = a2 * SUIMM(6) * a5 + eqsq * (x1 * SUIMM(24) * x5 -
                         x3 * SUIMM(6) * x7);
    z22 = (a4 * a5 + a2 * a6) * SUIMM(6) + eqsq * ((x2 * x5 + x1 * x6) *
                                 SUIMM(24) -
                               (x4 * x7 + x3 * x8) * SUIMM(6));
    z23 = a4 * SUIMM(6) * a6 + eqsq * (x2 * SUIMM(24) * x6 - x4 *
                                       SUIMM(6) * x8);
    z1  = z1 + z1 + bsq * z31;
    z2  = z2 + z2 + bsq * z32;
    z3  = z3 + z3 + bsq * z33;
    s3  = cc * xnoi;
    s2  = s3 * SUIMM(-0.5) / rteqsq;
    s4  = s3 * rteqsq;
    s1  = eq * SUIMM(-15) * s4;
    s5  = x1 * x3 + x2 * x4;
    s6  = x2 * x3 + x1 * x4;
    s7  = x2 * x4 - x1 * x3;
    se  = s1 * zn * s5;
    si  = s2 * zn * (z11 + z13);
    sl  = -zn * s3 * (z1 + z3 - SUIMM(14) - eqsq * SUIMM(6));
    sgh = s4 * zn * (z31 + z33 - SUIMM(6));

    shdq = 0;
    if (ishq) {
      SUFLOAT sh = -zn * s2 * (z21 + z23);
      shdq = sh / siniq;
    }

    self->ee2  = s1 * SUIMM(2) * s6;
    self->e3   = s1 * SUIMM(2) * s7;
    self->xi2  = s2 * SUIMM(2) * z12;
    self->xi3  = s2 * SUIMM(2) * (z13 - z11);
    self->xl2  = s3 * SUIMM(-2) * z2;
    self->xl3  = s3 * SUIMM(-2) * (z3 - z1);
    self->xl4  = s3 * SUIMM(-2) * (SUIMM(-21) - eqsq * SUIMM(9)) * ze;
    self->xgh2 = s4 * SUIMM(2) * z32;
    self->xgh3 = s4 * SUIMM(2) * (z33 - z31);
    self->xgh4 = s4 * SUIMM(-18) * ze;
    self->xh2  = s2 * SUIMM(-2) * z22;
    self->xh3  = s2 * SUIMM(-2) * (z23 - z21);

    if (ls == 1)
      break;

    /* DO LUNAR TERMS */

    self->sse  = se;
    self->ssi  = si;
    self->ssl  = sl;
    self->ssh  = shdq;
    self->ssg  = sgh - cosiq * self->ssh;
    self->se2  = self->ee2;
    self->si2  = self->xi2;
    self->sl2  = self->xl2;
    self->sgh2 = self->xgh2;
    self->sh2  = self->xh2;
    self->se3  = self->e3;
    self->si3  = self->xi3;
    self->sl3  = self->xl3;
    self->sgh3 = self->xgh3;
    self->sh3  = self->xh3;
    self->sl4  = self->xl4;
    self->sgh4 = self->xgh4;
    zcosg      = zcosgl;
    zsing      = zsingl;
    zcosi      = zcosil;
    zsini      = zsinil;
    zcosh      = zcoshl * cosq + zsinhl * sinq;
    zsinh      = sinq * zcoshl - cosq * zsinhl;
    zn         = ZNL;
    cc         = C1L;
    ze         = ZEL;
  }

  self->sse += se;
  self->ssi += si;
  self->ssl += sl;
  self->ssg += sgh - cosiq * shdq;
  self->ssh += shdq;

  if (self->xnodp < 0.0052359877 && self->xnodp > 0.0034906585) {
    /* 24h SYNCHRONOUS RESONANCE TERMS INITIALIZATION */
    self->iresfl = 1;
    self->isynfl = 1;
    g200         = eqsq * (eqsq * SUIMM(0.8125) - SUIMM(2.5)) + SUIMM(1);
    g310         = eqsq * SUIMM(2) + SUIMM(1);
    g300         = eqsq * (eqsq * SUIMM(6.60937) - SUIMM(6)) + SUIMM(1);
    f220         = (cosiq + SUIMM(1)) * SUIMM(0.75) * (cosiq + SUIMM(1));
    f311         = siniq * SUIMM(0.9375) * siniq * (cosiq * SUIMM(3) + SUIMM(1)) - (cosiq + SUIMM(1)) * SUIMM(0.75);
    f330         = cosiq + SUIMM(1);
    f330         = f330 * SUIMM(1.875) * f330 * f330;
    self->del1   = SUIMM(3) * (SUFLOAT)(self->xnodp * self->xnodp * aqnv * aqnv);
    self->del2   = self->del1 * SUIMM(2) * f220 * g200 * Q22;
    self->del3   = self->del1 * SUIMM(3) * f330 * g300 * Q33 * aqnv;
    self->del1   = self->del1 * f311 * g310 * Q31 * aqnv;
    self->fasx2  = SUIMM(0.13130908);
    self->fasx4  = SUIMM(2.8843198);
    self->fasx6  = SUIMM(0.37448087);
    self->xlamo  = self->xmo + self->xnodeo + self->omegao - self->thgr;
    bfact        = self->xmdot + xpidot - THDT;
    bfact       += (double)(self->ssl + self->ssg + self->ssh);
  } else if (self->xnodp >= 0.00826 && self->xnodp <= 0.00924 && eq >= SUIMM(0.5)) {
    /* GEOPOTENTIAL RESONANCE INITIALIZATION FOR 12 HOUR ORBITS */
    self->iresfl = 1;
    self->isynfl = 0;
    eoc = eq * eqsq;
    g201 = SUIMM(-0.306) - (eq - SUIMM(0.64)) * SUIMM(0.44);

#define GEOP(a, b, c, d) (eq * SUIMM(a) - SUIMM(b) - eqsq * SUIMM(c) + eoc * SUIMM(d))

    if (eq <= SUIMM(0.65)) {
      g211 = GEOP(-13.247,  -3.616,   -16.29,   0);
      g310 = GEOP(117.39,   19.302,   228.419,  156.591);
      g322 = GEOP(109.7927, 18.9068,  214.6334, 146.5816);
      g410 = GEOP(242.694,  41.122,   471.094,  313.953);
      g422 = GEOP(841.88,   146.407,  1629.014, 1083.435);
      g520 = GEOP(3017.977, 532.114,  5740.032, 3708.276);
    } else {
      g211 = GEOP(331.819,  72.099,   508.738,  266.724);
      g310 = GEOP(1582.851, 346.844,  2415.925, 1246.113);
      g322 = GEOP(1554.908, 342.585,  2366.899, 1215.972);
      g410 = GEOP(4758.686, 1052.797, 7193.992, 3651.957);
      g422 = GEOP(16178.11, 3581.69,  24462.77, 12422.52);

      if (eq <= SUIMM(0.715))
        g520 = GEOP(-4664.75, -1464.74, -3763.64, 0);
      else
        g520 = GEOP(29936.92, 5149.66,  54087.36, 31324.56);
    }

    if (eq < SUIMM(0.7)) {
      g533 = GEOP(4988.61,   919.2277,  9064.77,   5542.21);
      g521 = GEOP(4568.6173, 822.71072, 8491.4146, 5337.524);
      g532 = GEOP(4690.25,   853.666,   8624.77,   5341.4);
    } else {
      g533 = GEOP(161616.52, 37995.78,  229838.2,  109377.94);
      g521 = GEOP(218913.95, 51752.104, 309468.16, 146349.42);
      g532 = GEOP(170470.89, 40023.88,  242699.48, 115605.82);
    }
#undef GEOP

    f220 = (cosiq * SUIMM(2) + SUIMM(1) + cosiq2) * SUIMM(0.75);
    f221 = siniq2 * SUIMM(1.5);
    f321 = siniq 
        * SUIMM(1.875) 
        * (SUIMM(1) - cosiq * SUIMM(2) - cosiq2 * SUIMM(3));
    f322 = siniq 
        * SUIMM(-1.875) 
        * (cosiq * SUIMM(2) + SUIMM(1) - cosiq2 * SUIMM(3));
    f441 = siniq2 * SUIMM(35) * f220;
    f442 = siniq2 * SUIMM(39.375) * siniq2;
    f522 = siniq * SUIMM(9.84375) 
            * (siniq2 * (SUIMM(1) 
                    - cosiq * SUIMM(2) 
                    - cosiq2 * SUIMM(5)) 
                + (cosiq * SUIMM(4) 
                    - SUIMM(2) 
                    + cosiq2 * SUIMM(6)) 
            * SUIMM(0.33333333));
    f523 = siniq 
            * (siniq2 * SUIMM(4.92187512) * (SUIMM(-2) 
                    - cosiq * SUIMM(4) 
                    + cosiq2 * SUIMM(10)) 
                + (cosiq * SUIMM(2) + SUIMM(1) - cosiq2 * SUIMM(3))
            * SUIMM(6.56250012));
    f542        = siniq 
        * SUIMM(29.53125) 
        * (SUIMM(2) 
            - cosiq * SUIMM(8) 
            + cosiq2 * (cosiq * SUIMM(8) - SUIMM(12) + cosiq2 * SUIMM(10)));
    f543        = siniq 
        * SUIMM(29.53125) 
        * (SUIMM(-2) 
            - cosiq * SUIMM(8) 
            + cosiq2 * (cosiq * SUIMM(8) + SUIMM(12) - cosiq2 * SUIMM(10)));
    xno2        = (SUFLOAT)(self->xnodp * self->xnodp);
    ainv2       = aqnv * aqnv;
    temp1       = xno2 * SUIMM(3) * ainv2;
    temp0       = temp1 * ROOT22;
    self->d2201 = temp0 * f220 * g201;
    self->d2211 = temp0 * f221 * g211;
    temp1      *= aqnv;
    temp0       = temp1 * ROOT32;
    self->d3210 = temp0 * f321 * g310;
    self->d3222 = temp0 * f322 * g322;
    temp1      *= aqnv;
    temp0       = temp1 * SUIMM(2) * ROOT44;
    self->d4410 = temp0 * f441 * g410;
    self->d4422 = temp0 * f442 * g422;
    temp1      *= aqnv;
    temp0       = temp1 * ROOT52;
    self->d5220 = temp0 * f522 * g520;
    self->d5232 = temp0 * f523 * g532;
    temp0       = temp1 * SUIMM(2) * ROOT54;
    self->d5421 = temp0 * f542 * g521;
    self->d5433 = temp0 * f543 * g533;
    self->xlamo = self->xmo + 2 * (self->xnodeo - self->thgr);
    bfact       = self->xmdot + 2 * (self->xnodot - THDT);
    bfact      += (double)(self->ssl + 2 * self->ssh);
  } else {
    /* NON RESONANT ORBITS */
    self->iresfl = 0;
    self->isynfl = 0;
  }

  if (self->iresfl == 0) {
    /* Non-resonant orbits. */
    imode = SGDP4_DEEP_NORM;
  } else {
    /* INITIALIZE INTEGRATOR */
    self->xfact = bfact - self->xnodp;
    self->xli = (double)self->xlamo;
    self->xni = self->xnodp;
    self->atime = 0.0;

    dot_terms_calculated(self);

    /* Save the "dot" terms for integrator re-start. */
    self->xnddt0 = self->xnddt;
    self->xndot0 = self->xndot;
    self->xldot0 = self->xldot;

    if (self->isynfl)
      imode = SGDP4_DEEP_SYNC;
    else
      imode = SGDP4_DEEP_RESN;
  }

  /* Set up for original mode (LS terms at epoch non-zero). */
  self->ilsz = 0;
  self->pgh0 = self->ph0 = self->pe0 = self->pinc0 = self->pl0 = SUIMM(0);

  if (self->Set_LS_zero) {
    /* Save the epoch case Lunar-Solar terms to remove this bias for
     * actual computations later on.
     * Not sure if this is a good idea.
     */
    compute_LunarSolar(self, 0.0);

    self->pgh0 = self->pgh;
    self->ph0 = self->ph;
    self->pe0 = self->pe;
    self->pinc0 = self->pinc;
    self->pl0 = self->pl;
    self->ilsz = 1;
  }

  return imode;
} /* SGDP4_dpinit */

/* =====================================================================

   ------------- ENTRANCE FOR DEEP SPACE SECULAR EFFECTS ---------------

   xll    : Input/Output, modified "mean anomaly" or "mean longitude".
   omgasm   : Input/Output, modified argument of perigee.
   xnodes   : Input/Output, modified right asc of ascn node.
   em     : Input/Output, modified eccentricity.
   xinc   : Input/Output, modified inclination.

   xn     : Output, modified period from 'xnodp'.

   tsince   : Input, time from epoch (minutes).

   ===================================================================== */

int sgdp4_ctx_init_deep_secular(
  sgdp4_ctx_t *self,
  double *xll, SUFLOAT *omgasm, SUFLOAT *xnodes, SUFLOAT *em,
  SUFLOAT *xinc, double *xn, double tsince)
{
  SUDOUBLE delt, ft, xl;
  SUFLOAT temp0;

  *xll += self->ssl * tsince;
  *omgasm += self->ssg * tsince;
  *xnodes += self->ssh * tsince;
  *em += self->sse * tsince;
  *xinc += self->ssi * tsince;

  if (self->iresfl == 0)
    return 0;

    /*
   * A minor increase in some efficiency can be had by restarting if
   * the new time is closer to epoch than to the old integrated
   * time. This also forces a re-start on a change in sign (i.e. going
   * through zero time) as then we have |tsince - atime| > |tsince|
   * as well. Second test is for stepping back towards zero, forcing a restart
   * if close enough rather than integrating to zero.
   */
#define AHYST 1.0
  /* Most accurate (OK, most _consistant_) method. Restart if need to
   * integrate 'backwards' significantly from current point.
   */
  if (fabs(tsince) < STEP ||
    (self->atime > 0.0 && tsince < self->atime - AHYST) ||
    (self->atime < 0.0 && tsince > self->atime + AHYST)) {
    /* Epoch restart if we are at, or have crossed, tsince==0 */
    self->atime = 0.0;
    self->xni   = self->xnodp;
    self->xli   = (double)self->xlamo;

    /* Restore the old "dot" terms. */
    self->xnddt = self->xnddt0;
    self->xndot = self->xndot0;
    self->xldot = self->xldot0;
  }

  ft = tsince - self->atime;

  if (fabs(ft) > MAX_INTEGRATE) {
    SU_ERROR("SGDP4_dpsec: Integration limit reached");
    return -1;
  }

  if (fabs(ft) >= STEP) {
    /*
    Do integration if required. Find the step direction to
    make 'atime' catch up with 'tsince'.
    */
    delt = (tsince >= self->atime ? STEP : -STEP);

    do {
      /* INTEGRATOR (using the last "dot" terms). */
      self->xli   += delt * (self->xldot + delt * SUIMM(0.5) * self->xndot);
      self->xni   += delt * (self->xndot + delt * SUIMM(0.5) * self->xnddt);
      self->atime += delt;

      dot_terms_calculated(self);

      /* Are we close enough now ? */
      ft = tsince - self->atime;
    } while (fabs(ft) >= STEP);
  }

  xl    = self->xli + ft * (self->xldot + ft * SUIMM(0.5) * self->xndot);
  *xn   = self->xni + ft * (self->xndot + ft * SUIMM(0.5) * self->xnddt);

  temp0 = -(*xnodes) + self->thgr + tsince * THDT;

  if (self->isynfl == 0)
    *xll = xl + temp0 + temp0;
  else
    *xll = xl - *omgasm + temp0;

  return 0;
} /* SGDP4_dpsec */

/* =====================================================================

   Here we do the "dot" terms for the integrator. Separate function so we
   can call when initialising and save the atime==0.0 values for later
   epoch re-start of the integrator.

   ===================================================================== */

static void
dot_terms_calculated(sgdp4_ctx_t *self)
{
  SUDOUBLE x2li, x2omi, xomi;

  /* DOT TERMS CALCULATED */
  if (self->isynfl) {
    self->xndot = self->del1 * SU_SINX(self->xli - self->fasx2) 
        + self->del2 * SU_SINX((self->xli - self->fasx4) * SUIMM(2)) 
        + self->del3 * SU_SINX((self->xli - self->fasx6) * SUIMM(3));
    self->xnddt = self->del1 * SU_COSX(self->xli - self->fasx2) 
        + self->del2 * SU_COSX((self->xli - self->fasx4) * SUIMM(2)) * SUIMM(2) 
        + self->del3 * SU_COSX((self->xli - self->fasx6) * SUIMM(3)) * SUIMM(3);
  } else {
    xomi  = self->omegao + self->omgdot * self->atime;
    x2omi = 2 * xomi;
    x2li  = 2 * self->xli;

    self->xndot = self->d2201 * SU_SINX(x2omi + self->xli - G22) 
        + self->d2211 * SU_SINX(self->xli - G22) 
        + self->d3210 * SU_SINX(xomi + self->xli - G32) 
        + self->d3222 * SU_SINX(-xomi + self->xli - G32) 
        + self->d5220 * SU_SINX(xomi + self->xli - G52) 
        + self->d5232 * SU_SINX(-xomi + self->xli - G52) 
        + self->d4410 * SU_SINX(x2omi + x2li - G44) 
        + self->d4422 * SU_SINX(x2li - G44) 
        + self->d5421 * SU_SINX(xomi + x2li - G54) 
        + self->d5433 * SU_SINX(-xomi + x2li - G54);

    self->xnddt = self->d2201 * SU_COSX(x2omi + self->xli - G22) 
        + self->d2211 * SU_COSX(self->xli - G22) 
        + self->d3210 * SU_COSX(xomi + self->xli - G32) 
        + self->d3222 * SU_COSX(-xomi + self->xli - G32) 
        + self->d5220 * SU_COSX(xomi + self->xli - G52) 
        + self->d5232 * SU_COSX(-xomi + self->xli - G52) 
        + SUIMM(2) * (self->d4410 * SU_COSX(x2omi + x2li - G44) 
        + self->d4422 * SU_COSX(x2li - G44) 
        + self->d5421 * SU_COSX(xomi + x2li - G54) 
        + self->d5433 * SU_COSX(-xomi + x2li - G54));
  }

  self->xldot  = (SUFLOAT)(self->xni + self->xfact);
  self->xnddt *= self->xldot;

} /* dot_terms_calculated */

/* =====================================================================

   ---------------- ENTRANCES FOR LUNAR-SOLAR PERIODICS ----------------

   em     : Input/Output, modified eccentricity.
   xinc   : Input/Output, modified inclination.
   omgasm   : Input/Output, modified argument of perigee.
   xnodes   : Input/Output, modified right asc of ascn node.
   xll    : Input/Output, modified "mean anomaly" or "mean longitude".
   tsince   : Input, time from epoch (minutes).

   ===================================================================== */

int 
sgdp4_ctx_init_deep_periodic(
    sgdp4_ctx_t *self, 
    SUFLOAT *em, 
    SUFLOAT *xinc, 
    SUFLOAT *omgasm, 
    SUFLOAT *xnodes,
    double *xll, 
    double tsince)
{
  SUFLOAT sinis, cosis;
  SUFLOAT tmp_ph;
  SUFLOAT alfdp, betdp, dalf, dbet, xls, dls;
  SUFLOAT sinok, cosok;
  int ishift;
  SUFLOAT oldxnode;

  compute_LunarSolar(self, tsince);

  *xinc += self->pinc;
  *em   += self->pe;

  /* Spacetrack report #3 has sin/cos from before perturbations
   * added to xinc (oldxinc), but apparently report # 6 has then
   * from after they are added.
   */
  SU_SINCOS(*xinc, &sinis, &cosis);

  if (self->ilsd) {
    /* APPLY PERIODICS DIRECTLY */
    tmp_ph   = self->ph / sinis;
    *omgasm += self->pgh - cosis * tmp_ph;
    *xnodes += tmp_ph;
    *xll    += self->pl;
  } else {
    /* APPLY PERIODICS WITH LYDDANE MODIFICATION */
    oldxnode = (*xnodes);

    SU_SINCOS(*xnodes, &sinok, &cosok);
    alfdp    = sinis * sinok;
    betdp    = sinis * cosok;
    dalf     = self->ph * cosok + self->pinc * cosis * sinok;
    dbet     = -self->ph * sinok + self->pinc * cosis * cosok;
    alfdp   += dalf;
    betdp   += dbet;
    xls      = (SUFLOAT)*xll + *omgasm + cosis * *xnodes;
    dls      = self->pl + self->pgh - self->pinc * *xnodes * sinis;
    xls     += dls;
    *xnodes  = ATAN2(alfdp, betdp);

    /* Get perturbed xnodes in to same quadrant as original. */
    ishift   = NINT((oldxnode - (*xnodes)) / TWOPI);
    *xnodes += (SUFLOAT)(TWOPI * ishift);
    *xll    += (double)self->pl;
    *omgasm  = xls - (SUFLOAT)*xll - cosis * (*xnodes);
  }

  return 0;
} /* SGDP4_dpper */

/* =====================================================================
   Do the Lunar-Solar terms for the SGDP4_dpper() function (normally only
   every 1/2 hour needed. Seperate function so initialisng could save the
   epoch terms to zero them. Not sure if this is a good thing (some believe
   it the way the equations were intended) as the two-line elements may
   be computed to give the right answer with out this (which I would hope
   as it would make predictions consistant with the 'official' model
   code).
   ===================================================================== */

static void 
compute_LunarSolar(sgdp4_ctx_t *self, double tsince)
{
  SUFLOAT sinzf, coszf;
  SUFLOAT f2, f3, zf, zm;
  SUFLOAT sel, sil, ses, sll, sis, sls;
  SUFLOAT sghs, shs, sghl, shl;

  /* Update Solar terms. */
  zm   = self->zmos + ZNS * tsince;
  zf   = zm + ZES * SUIMM(2) * SU_SINX(zm);
  SU_SINCOS(zf, &sinzf, &coszf);
  f2   = sinzf * SUIMM(0.5) * sinzf - SUIMM(0.25);
  f3   = sinzf * SUIMM(-0.5) * coszf;
  ses  = self->se2 * f2 + self->se3 * f3;
  sis  = self->si2 * f2 + self->si3 * f3;
  sls  = self->sl2 * f2 + self->sl3 * f3 + self->sl4 * sinzf;

  sghs = self->sgh2 * f2 + self->sgh3 * f3 + self->sgh4 * sinzf;
  shs  = self->sh2 * f2 + self->sh3 * f3;

  /* Update Lunar terms. */
  zm   = self->zmol + ZNL * tsince;
  zf   = zm + ZEL * SUIMM(2) * SU_SINX(zm);
  SU_SINCOS(zf, &sinzf, &coszf);
  f2   = sinzf * SUIMM(0.5) * sinzf - SUIMM(0.25);
  f3   = sinzf * SUIMM(-0.5) * coszf;
  sel  = self->ee2 * f2 + self->e3 * f3;
  sil  = self->xi2 * f2 + self->xi3 * f3;
  sll  = self->xl2 * f2 + self->xl3 * f3 + self->xl4 * sinzf;

  sghl = self->xgh2 * f2 + self->xgh3 * f3 + self->xgh4 * sinzf;
  shl  = self->xh2 * f2 + self->xh3 * f3;

  /* Save computed values to calling structure. */
  self->pgh  = sghs + sghl;
  self->ph   = shs + shl;
  self->pe   = ses + sel;
  self->pinc = sis + sil;
  self->pl   = sls + sll;

  if (self->ilsz) {
    /* Correct for previously saved epoch terms. */
    self->pgh  -= self->pgh0;
    self->ph   -= self->ph0;
    self->pe   -= self->pe0;
    self->pinc -= self->pinc0;
    self->pl   -= self->pl0;
  }
}

/* =====================================================================
   This function converts the epoch time (in the form of YYDDD.DDDDDDDD,
   exactly as it appears in the two-line elements) into days from 00:00:00
   hours Jan 1st 1950 UTC. Also it computes the right ascencion of Greenwich
   at the epoch time, but not in a very accurate manner. However, the same
   method is used here to allow exact comparason with the original FORTRAN
   versions of the programs. The calling arguments are:

   ep     : Input, epoch time of elements (as read from 2-line data).
   thegr  : Output, right ascensionm of Greenwich at epoch, radian.
   days50   : Output, days from Jan 1st 1950 00:00:00 UTC.

   ===================================================================== */

#define THETAG 2

/* Version like sat_code. */
#define J1900 (2451545.5 - 36525. - 1.)
#define SECDAY (86400.0)

#define C1 (1.72027916940703639E-2)
#define C1P2P (C1 + TWOPI)
#define THGR70 (1.7321343856509374)
#define FK5R (5.07551419432269442E-15)

static void 
thetag(double ep, SUFLOAT *thegr, double *days50)
{
  double d;
  long n, jy;
  double theta;
  double ts70, ds70, trfac;
  long ids70;

  jy = (long)((ep + 2.0e-7) * 0.001); /* Extract the year. */
  d = ep - jy * 1.0e3;        /* And then the day of year. */

  /* Assume " 8" is 1980, or more sensibly 2008 ? */
  /*
  if (jy < 10) jy += 80;
  */
  if (jy < 50)
    jy += 100;

  if (jy < 70) /* Fix for leap years ? */
    n = (jy - 72) / 4;
  else
    n = (jy - 69) / 4;

  *days50 = (jy - 70) * 365.0 + 7305.0 + n + d;

#if THETAG == 0
  /* Original report #3 code. */
  theta = *days50 * 6.3003880987 + 1.72944494;
#elif THETAG == 1
  {
    /* Method from project pluto code. */
    /* Reference:  The 1992 Astronomical Almanac, page B6. */
    const double omega_E = 1.00273790934; /* Earth rotations per sideSUFLOAT day (non-constant) */
    const double UT = fmod(jd + 0.5, 1.0);
    double t_cen, GMST;
    double jd;

    jd = d + J1900 + jy * 365. + ((jy - 1) / 4);

    t_cen = (jd - UT - 2451545.0) / 36525.0;
    GMST = 24110.54841 + t_cen * (8640184.812866 + t_cen * (0.093104 - t_cen * 6.2E-6));

    GMST = fmod(GMST + SECDAY * omega_E * UT, SECDAY);

    if (GMST < 0.0)
      GMST += SECDAY;

    theta = TWOPI * GMST / SECDAY;
  }
#elif THETAG == 2
    /* Method from SGP4SUB.F code. */

  ts70 = (*days50) - 7305.0;
  ids70 = (long)(ts70 + 1.0e-8);
  ds70 = ids70;
  trfac = ts70 - ds70;

  /* CALCULATE GREENWICH LOCATION AT EPOCH */
  theta = THGR70 + C1 * ds70 + C1P2P * trfac + ts70 * ts70 * FK5R;
#else
#error 'Unknown method for theta-G calculation'
#endif

  theta = fmod(theta, TWOPI);
  if (theta < 0.0)
    theta += TWOPI;

  *thegr = (SUFLOAT)theta;

} /* thetag */

#endif /* !NO_DEEP_SPACE */
