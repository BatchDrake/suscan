
/*

  SDP4 implementation, adapted from libpredict's implementation in
  https: /  / github.com / la1k / libpredict, originally released under license
  GPL 2.0.

  Copyright (C) 2020 Gonzalo José Carracedo Carballal

  This program is free software: you can redistribute it and / or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation, either version 3 of the
  License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this program.  If not, see
  <http: /  / www.gnu.org / licenses / >

*/
/*  Explicitly disable single precision here  */
#if defined(_SU_SINGLE_PRECISION)
#  undef _SU_SINGLE_PRECISION
#endif /*  defined(_SU_SINGLE_PRECISION)  */

#include "sdp4.h"
#include "sgdp4defs.h"

enum suscan_sdp4_type {
  SUSCAN_SDP4_TYPE_SECULAR = 1,
  SUSCAN_SDP4_TYPE_PERIODIC = 2
};

SUPRIVATE void suscan_sdp4_deep(
    const struct suscan_sdp4_params *m,
    int ientry,
    const suscan_deep_arg_fixed_t *deep_arg,
    suscan_deep_arg_dynamic_t *deep_dyn);

SUPRIVATE void suscan_sdp4_deep_initialize(
  const suscan_tle_t  * tle,
  struct suscan_sdp4_params  * m,
  suscan_deep_arg_fixed_t  * deep_arg);


SUPRIVATE void suscan_sdp4_deep_initialize_dynamic(
  const struct suscan_sdp4_params *m,
  suscan_deep_arg_dynamic_t *deep_dyn);

void
suscan_sdp4_params_init(
  const suscan_tle_t  * tle,
  struct suscan_sdp4_params  * m)
{
  SUDOUBLE temp1, temp2, temp3, theta4, a1, a3ovk2, ao, c2, coef,
  coef1, x1m5th, xhdot1, del1, delo, eeta, eta, etasq, perigee, psisq,
  tsi, qoms24, s4, pinvsq;

  m->lunar_terms_done  = SU_FALSE;
  m->resonance_flag   = SU_FALSE;
  m->synchronous_flag = SU_FALSE;

  /* Calculate old TLE field values as used in the original sdp4 */
  m->bstar  = tle->bstar_drag_term / AE;
  m->eo     = tle->eccentricity;
  m->xno    = tle->mean_motion * TWO_PI / MINUTES_PER_DAY;

  m->xincl  = SU_DEG2RAD(tle->inclination);
  m->xnodeo = SU_DEG2RAD(tle->right_ascension);
  m->omegao = SU_DEG2RAD(tle->argument_of_perigee);
  m->xmo    = SU_DEG2RAD(tle->mean_anomaly);

  m->epoch = 1000.0 * tle->epoch_year + tle->epoch_day;

  /* Recover original mean motion (xnodp) and   */
  /* semimajor axis (aodp) from input elements. */

  a1                 = SU_POW(XKE / m->xno, TWO_THIRD);
  m->deep_arg.cosio  = SU_COS(m->xincl);
  m->deep_arg.theta2 = m->deep_arg.cosio * m->deep_arg.cosio;
  m->x3thm1          = 3 * m->deep_arg.theta2 - 1;
  m->deep_arg.eosq   = m->eo * m->eo;
  m->deep_arg.betao2 = 1 - m->deep_arg.eosq;
  m->deep_arg.betao  = SU_SQRT(m->deep_arg.betao2);
  del1               = 1.5 * CK2 * m->x3thm1 / (
      a1 * a1 * m->deep_arg.betao * m->deep_arg.betao2);
  ao                 = a1 * (1 - del1 * (0.5 * TWO_THIRD
                        + del1 * (1 + 134.0 / 81.0 * del1)));
  delo               = 1.5 * CK2 * m->x3thm1
      / (ao * ao * m->deep_arg.betao * m->deep_arg.betao2);
  m->deep_arg.xnodp  = m->xno / (1 + delo);
  m->deep_arg.aodp   = ao / (1 - delo);

  /* For perigee below 156 km, the values */
  /* of s and qoms2t are altered.     */

  perigee = (m->deep_arg.aodp * (1 - m->eo) - AE) * EARTH_RADIUS_KM_WGS84;

  if (perigee < 156.0) {
    s4     = perigee <= 98.0 ? 20 : (perigee - 78.0);
    qoms24 = SU_POW((120 - s4) * AE / EARTH_RADIUS_KM_WGS84, 4);
    s4     = s4 / EARTH_RADIUS_KM_WGS84  +  AE;
  } else {
    s4     = S_DENSITY_PARAM;
    qoms24 = QOMS2T;
  }

  SU_SINCOS(m->omegao, &m->deep_arg.sing, &m->deep_arg.cosg);

  pinvsq             = 1 / (m->deep_arg.aodp * m->deep_arg.aodp * m->deep_arg.betao2 * m->deep_arg.betao2);
  tsi                = 1 / (m->deep_arg.aodp-s4);
  eta                = m->deep_arg.aodp * m->eo * tsi;
  etasq              = eta * eta;
  eeta               = m->eo * eta;
  psisq              = SU_ABS(1 - etasq);
  coef               = qoms24 * SU_POW(tsi, 4);
  coef1              = coef / SU_POW(psisq, 3.5);
  c2                 = coef1 * m->deep_arg.xnodp * (m->deep_arg.aodp * (
      1 + 1.5 * etasq+eeta * (4 + etasq)) + 0.75 * CK2 * tsi / psisq * m->x3thm1 * (
          8 + 3 * etasq * (8 + etasq)));

  m->c1              = m->bstar * c2;
  m->deep_arg.sinio  = SU_SIN(m->xincl);
  a3ovk2             = -J3_HARMONIC_WGS72 / CK2 * SU_POW(AE, 3);
  m->x1mth2          = 1 - m->deep_arg.theta2;
  m->c4              =
      2 * m->deep_arg.xnodp * coef1 * m->deep_arg.aodp * m->deep_arg.betao2 * (
          eta * (2+0.5 * etasq)
          + m->eo * (0.5+2 * etasq)
          - 2 * CK2 * tsi / (m->deep_arg.aodp * psisq) * (
              -3 * m->x3thm1 * (1 - 2 * eeta + etasq * (
                  1.5 - 0.5 * eeta))
              + 0.75 * m->x1mth2 * (2 * etasq-eeta * (
                  1 + etasq)) * SU_COS(2 * m->omegao)));

  theta4             = m->deep_arg.theta2 * m->deep_arg.theta2;
  temp1              = 3 * CK2 * pinvsq * m->deep_arg.xnodp;
  temp2              = temp1 * CK2 * pinvsq;
  temp3              = 1.25 * CK4 * pinvsq * pinvsq * m->deep_arg.xnodp;
  m->deep_arg.xmdot  =
      m->deep_arg.xnodp
      + 0.5 * temp1 * m->deep_arg.betao * m->x3thm1
      + 0.0625 * temp2 * m->deep_arg.betao * (
          13 - 78 * m->deep_arg.theta2 + 137 * theta4);

  x1m5th             = 1 - 5 * m->deep_arg.theta2;
  m->deep_arg.omgdot =
      - 0.5 * temp1 * x1m5th
      + 0.0625 * temp2 * (7 - 114 * m->deep_arg.theta2 + 395 * theta4)
      + temp3 * (3 - 36 * m->deep_arg.theta2 + 49 * theta4);

  xhdot1             = -temp1 * m->deep_arg.cosio;
  m->deep_arg.xnodot =
      xhdot1
      + (0.5 * temp2 * (4 - 19 * m->deep_arg.theta2)
          + 2 * temp3 * (3 - 7 * m->deep_arg.theta2)) * m->deep_arg.cosio;

  m->xnodcf          = 3.5 * m->deep_arg.betao2 * xhdot1 * m->c1;
  m->t2cof           = 1.5 * m->c1;
  m->xlcof           = 0.125 * a3ovk2 * m->deep_arg.sinio * (
      3 + 5 * m->deep_arg.cosio) / (1 + m->deep_arg.cosio);
  m->aycof           = 0.25 * a3ovk2 * m->deep_arg.sinio;
  m->x7thm1          = 7 * m->deep_arg.theta2 - 1;

  /* initialize Deep() */
  suscan_sdp4_deep_initialize(tle, m, &(m->deep_arg));
}

void
suscan_sdp4_predict(
  const struct suscan_sdp4_params *m,
  SUDOUBLE tsince,
  struct suscan_ephemeris_model_output *output)
{

  int i;
  SUDOUBLE a, axn, ayn, aynl, beta, betal, capu, cos2u, cosepw, cosik,
  cosnok, cosu, cosuk, ecose, elsq, epw, esine, pl, rdot,
  rdotk, rfdot, rfdotk, rk, sin2u, sinepw, sinik, sinnok, sinu,
  sinuk, tempe, templ, tsq, u, uk, ux, uy, uz, vx, vy, vz, xl,
  xlt, xmam, xmdf, xmx, xmy, xnoddf, xll, r, temp, tempa, temp1,
  temp2, temp3, temp4, temp5, temp6;

  SUDOUBLE xnodek, xinck;

  /* Initialize dynamic part of deep_arg */
  suscan_deep_arg_dynamic_t deep_dyn;
  suscan_sdp4_deep_initialize_dynamic(m, &deep_dyn);

  /* Update for secular gravity and atmospheric drag */
  xmdf            = m->xmo + m->deep_arg.xmdot * tsince;
  deep_dyn.omgadf = m->omegao+m->deep_arg.omgdot * tsince;
  xnoddf          = m->xnodeo+m->deep_arg.xnodot * tsince;
  tsq             = tsince * tsince;
  deep_dyn.xnode  = xnoddf+m->xnodcf * tsq;
  tempa           = 1 - m->c1 * tsince;
  tempe           = m->bstar * m->c4 * tsince;
  templ           = m->t2cof * tsq;
  deep_dyn.xn     = m->deep_arg.xnodp;

  /* Update for deep-space secular effects */
  deep_dyn.xll    = xmdf;
  deep_dyn.t      = tsince;

  suscan_sdp4_deep(m, SUSCAN_SDP4_TYPE_SECULAR, &m->deep_arg, &deep_dyn);

  xmdf            = deep_dyn.xll;
  a               = SU_POW(XKE / deep_dyn.xn,TWO_THIRD) * tempa * tempa;
  deep_dyn.em     = deep_dyn.em-tempe;
  xmam            = xmdf + m->deep_arg.xnodp * templ;

  /* Update for deep-space periodic effects */
  deep_dyn.xll    = xmam;

  suscan_sdp4_deep(m, SUSCAN_SDP4_TYPE_PERIODIC, &m->deep_arg, &deep_dyn);

  xmam            = deep_dyn.xll;
  xl              = xmam+deep_dyn.omgadf+deep_dyn.xnode;
  beta            = SU_SQRT(1 - deep_dyn.em * deep_dyn.em);
  deep_dyn.xn     = XKE / SU_POW(a, 1.5);

  /* Long period periodics */
  axn             = deep_dyn.em * SU_COS(deep_dyn.omgadf);
  temp            = 1 / (a * beta * beta);
  xll             = temp * m->xlcof * axn;
  aynl            = temp * m->aycof;
  xlt             = xl + xll;
  ayn             = deep_dyn.em * SU_SIN(deep_dyn.omgadf) + aynl;

  /* Solve Kepler's Equation */
  capu            = SU_FMOD(xlt - deep_dyn.xnode, TWO_PI);

  temp2           = capu;

  i = 0;

  do {
    SU_SINCOS(temp2, &sinepw, &cosepw);
    temp3 = axn * sinepw;
    temp4 = ayn * cosepw;
    temp5 = axn * cosepw;
    temp6 = ayn * sinepw;
    epw = (capu-temp4 + temp3 - temp2) / (1 - temp5 - temp6) + temp2;

    if (sufeq(epw, temp2, E6A))
      break;

    temp2 = epw;

  } while (i++ < 10);

  /* Short period preliminary quantities */
  ecose  = temp5 + temp6;
  esine  = temp3-temp4;
  elsq   = axn * axn + ayn * ayn;
  temp   = 1 - elsq;
  pl     = a * temp;
  r      = a * (1 - ecose);
  temp1  = 1 / r;
  rdot   = XKE * SU_SQRT(a) * esine * temp1;
  rfdot  = XKE * SU_SQRT(pl) * temp1;
  temp2  = a * temp1;
  betal  = SU_SQRT(temp);
  temp3  = 1 / (1 + betal);
  cosu   = temp2 * (cosepw-axn + ayn * esine * temp3);
  sinu   = temp2 * (sinepw-ayn-axn * esine * temp3);
  u      = SU_ATAN2(sinu, cosu);
  sin2u  = 2 * sinu * cosu;
  cos2u  = 2 * cosu * cosu-1;
  temp   = 1 / pl;
  temp1  = CK2 * temp;
  temp2  = temp1 * temp;

  /* Update for short periodics */
  rk =
      r * (1 - 1.5 * temp2 * betal * m->x3thm1)
      + 0.5 * temp1 * m->x1mth2 * cos2u;
  uk     = u - 0.25 * temp2 * m->x7thm1 * sin2u;
  xnodek = deep_dyn.xnode + 1.5 * temp2 * m->deep_arg.cosio * sin2u;
  xinck  = deep_dyn.xinc
      + 1.5 * temp2 * m->deep_arg.cosio * m->deep_arg.sinio * cos2u;
  rdotk  = rdot  - deep_dyn.xn * temp1 * m->x1mth2 * sin2u;
  rfdotk = rfdot + deep_dyn.xn * temp1 * (m->x1mth2 * cos2u + 1.5 * m->x3thm1);

  /* Orientation vectors */
  SU_SINCOS(uk,     &sinuk,  &cosuk);
  SU_SINCOS(xinck,  &sinik,  &cosik);
  SU_SINCOS(xnodek, &sinnok, &cosnok);

  xmx = -sinnok * cosik;
  xmy = cosnok * cosik;
  ux  = xmx * sinuk + cosnok * cosuk;
  uy  = xmy * sinuk + sinnok * cosuk;
  uz  = sinik * sinuk;
  vx  = xmx * cosuk-cosnok * sinuk;
  vy  = xmy * cosuk-sinnok * sinuk;
  vz  = sinik * cosuk;

  /* Position and velocity */
  output->pos[0] = rk * ux;
  output->pos[1] = rk * uy;
  output->pos[2] = rk * uz;
  output->vel[0] = rdotk * ux+rfdotk * vx;
  output->vel[1] = rdotk * uy+rfdotk * vy;
  output->vel[2] = rdotk * uz+rfdotk * vz;

  /* Phase in radians */
  SUDOUBLE phase = xlt - deep_dyn.xnode-deep_dyn.omgadf + TWO_PI;

  if (phase < 0.0)
    phase += TWO_PI;

  phase = SU_FMOD(phase, TWO_PI);
  output->phase  = phase;

  output->omgadf = deep_dyn.omgadf;
  output->xnodek = xnodek;
  output->xinck  = xinck;
}

/* *
 * Calculates the Greenwich Mean Sidereal Time
 * for an epoch specified in the format used in the NORAD two-line
 * element sets.
 * It has been adapted for dates beyond the year 1999.
 * Reference:  The 1992 Astronomical Almanac, page B6.
 * Modification to support Y2K. Valid 1957 through 2056.
  *
 * \param epoch TLE epoch
 * \param deep_arg Deep arg
 * \copyright GPLv2+
  * */
SUPRIVATE SUDOUBLE
suscan_sdp4_theta_g(SUDOUBLE epoch, suscan_deep_arg_fixed_t *deep_arg)
{
  SUDOUBLE year, day, UT, jd, TU, GMST, ThetaG;

  /* Modification to support Y2K */
  /* Valid 1957 through 2056   */

  day            = SU_MODF(epoch * 1e-3, &year) * 1e3;

  if (year < 57)
    year += 2000;
  else
    year += 1900;

  UT             = SU_MODF(day, &day);
  jd             = suscan_sdp4_year_to_jd(year) + day;
  TU             = (jd - 2451545.0) / 36525;
  GMST           = 24110.54841 + TU * (8640184.812866 + TU * (0.093104 - TU * 6.2e-6));
  GMST           = SU_FMOD(
                     GMST + SECONDS_PER_DAY * EARTH_ROTATIONS_PER_SIDERIAL_DAY * UT,
                     SECONDS_PER_DAY);
  ThetaG         = TWO_PI * GMST / SECONDS_PER_DAY;
  deep_arg->ds50 = jd - 2433281.5 + UT;
  ThetaG         = SU_FMOD(6.3003880987 * deep_arg->ds50 + 1.72944494, TWO_PI);

  return ThetaG;
}

void
suscan_sdp4_deep_initialize(
    const suscan_tle_t *tle,
    struct suscan_sdp4_params *m,
    suscan_deep_arg_fixed_t *deep_arg)
{
  SUDOUBLE a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, ainv2, aqnv,
  sgh, sini2, sh, si, day, bfact, c,
  cc, cosq, ctem, f322, zx, zy, eoc, eq,
  f220, f221, f311, f321, f330, f441, f442, f522, f523,
  f542, f543, g200, g201, g211, s1, s2, s3, s4, s5, s6, s7,
  se, g300, g310, g322, g410, g422, g520, g521, g532,
  g533, gam, sinq, sl, stem, temp, temp1, x1,
  x2, x3, x4, x5, x6, x7, x8, xmao,
  xno2, xnodce, xnoi, xpidot, z1, z11, z12, z13, z2,
  z21, z22, z23, z3, z31, z32, z33, ze, zn, zsing,
  zsinh, zsini, zcosg, zcosh, zcosi;

  /* Entrance for deep space initialization */
  m->thgr  = suscan_sdp4_theta_g(m->epoch,deep_arg);
  eq       = m->eo;
  m->xnq   = deep_arg->xnodp;
  aqnv     = 1 / deep_arg->aodp;
  m->xqncl = m->xincl;
  xmao     = m->xmo;
  xpidot   = deep_arg->omgdot+deep_arg->xnodot;

  SU_SINCOS(m->xnodeo, &sinq, &cosq);

  m->omegaq = m->omegao;

  /* Initialize lunar solar terms */
  day = deep_arg->ds50 + 18261.5;  /* Days since 1900 Jan 0.5 */

  m->preep = day;
  xnodce   = 4.5236020 - 9.2422029e-4 * day;

  SU_SINCOS(xnodce, &stem, &ctem);

  m->zcosil = 0.91375164 - 0.03568096 * ctem;
  m->zsinil = SU_SQRT(1 - m->zcosil * m->zcosil);
  m->zsinhl = 0.089683511 * stem / m->zsinil;
  m->zcoshl = SU_SQRT(1 - m->zsinhl * m->zsinhl);
  c         = 4.7199672+0.22997150 * day;
  gam       = 5.8351514+0.0019443680 * day;
  m->zmol   = SU_FMOD(c-gam, TWO_PI);
  zx        = 0.39785416 * stem / m->zsinil;
  zy        = m->zcoshl * ctem+0.91744867 * m->zsinhl * stem;
  zx        = SU_ATAN2(zx,zy);
  zx        = gam + zx - xnodce;

  SU_SINCOS(zx, &m->zsingl, &m->zcosgl);

  m->zmos = 6.2565837 + 0.017201977 * day;
  m->zmos = SU_FMOD(m->zmos, TWO_PI);

  /* Do solar terms */
  zcosg = ZCOSGS;
  zsing = ZSINGS;
  zcosi = ZCOSIS;
  zsini = ZSINIS;
  zcosh = cosq;
  zsinh = sinq;
  cc    = C1SS;
  zn    = ZNS;
  ze    = ZES;
  /* zmo = m->zmos; */
  xnoi = 1 / m->xnq;

  /* Loop breaks when Solar terms are done a second */
  /* time, after Lunar terms are initialized    */

  for (;;) {
    /* Solar terms done again after Lunar terms are done */
    a1  = zcosg * zcosh + zsing * zcosi * zsinh;
    a3  = -zsing * zcosh + zcosg * zcosi * zsinh;
    a7  = -zcosg * zsinh + zsing * zcosi * zcosh;
    a8  = zsing * zsini;
    a9  = zsing * zsinh + zcosg * zcosi * zcosh;
    a10 = zcosg * zsini;
    a2  = deep_arg->cosio * a7+deep_arg->sinio * a8;
    a4  = deep_arg->cosio * a9+deep_arg->sinio * a10;
    a5  = -deep_arg->sinio * a7+deep_arg->cosio * a8;
    a6  = -deep_arg->sinio * a9+deep_arg->cosio * a10;
    x1  = a1 * deep_arg->cosg+a2 * deep_arg->sing;
    x2  = a3 * deep_arg->cosg+a4 * deep_arg->sing;
    x3  = -a1 * deep_arg->sing+a2 * deep_arg->cosg;
    x4  = -a3 * deep_arg->sing+a4 * deep_arg->cosg;
    x5  = a5 * deep_arg->sing;
    x6  = a6 * deep_arg->sing;
    x7  = a5 * deep_arg->cosg;
    x8  = a6 * deep_arg->cosg;
    z31 = 12 * x1 * x1 - 3 * x3 * x3;
    z32 = 24 * x1 * x2 - 6 * x3 * x4;
    z33 = 12 * x2 * x2 - 3 * x4 * x4;
    z1  = 3 * (a1 * a1 + a2 * a2)+z31 * deep_arg->eosq;
    z2  = 6 * (a1 * a3 + a2 * a4)+z32 * deep_arg->eosq;
    z3  = 3 * (a3 * a3 + a4 * a4)+z33 * deep_arg->eosq;
    z11 = -6 * a1 * a5 + deep_arg->eosq * (-24 * x1 * x7-6 * x3 * x5);
    z12 =
        -6 * (a1 * a6 + a3 * a5)
        + deep_arg->eosq * (-24 * (x2 * x7 + x1 * x8)
        - 6 * (x3 * x6 + x4 * x5));
    z13 = -6 * a3 * a6 + deep_arg->eosq * (-24 * x2 * x8 - 6 * x4 * x6);
    z21 = 6 * a2 * a5+deep_arg->eosq * (24 * x1 * x5 - 6 * x3 * x7);
    z22 =
        6 * (a4 * a5+a2 * a6)
        + deep_arg->eosq * (24 * (x2 * x5 + x1 * x6)
        - 6 * (x4 * x7 + x3 * x8));
    z23 = 6 * a4 * a6 + deep_arg->eosq * (24 * x2 * x6 - 6 * x4 * x8);
    z1  = z1 + z1 + deep_arg->betao2 * z31;
    z2  = z2 + z2 + deep_arg->betao2 * z32;
    z3  = z3 + z3 + deep_arg->betao2 * z33;
    s3  = cc * xnoi;
    s2  = -0.5 * s3 / deep_arg->betao;
    s4  = s3 * deep_arg->betao;
    s1  = -15 * eq * s4;
    s5  = x1 * x3 + x2 * x4;
    s6  = x2 * x3 + x1 * x4;
    s7  = x2 * x4 - x1 * x3;
    se  = s1 * zn * s5;
    si  = s2 * zn * (z11 + z13);
    sl  = -zn * s3 * (z1 + z3 - 14 - 6 * deep_arg->eosq);
    sgh = s4 * zn * (z31 + z33 - 6);
    sh  = -zn * s2 * (z21 + z23);

    if (m->xqncl < 5.2359877e-2)
      sh = 0;

    m->ee2  = 2 * s1 * s6;
    m->e3   = 2 * s1 * s7;
    m->xi2  = 2 * s2 * z12;
    m->xi3  = 2 * s2 * (z13 - z11);
    m->xl2  = -2 * s3 * z2;
    m->xl3  = -2 * s3 * (z3 - z1);
    m->xl4  = -2 * s3 * (-21 - 9 * deep_arg->eosq) * ze;
    m->xgh2 = 2 * s4 * z32;
    m->xgh3 = 2 * s4 * (z33 - z31);
    m->xgh4 = -18 * s4 * ze;
    m->xh2  = -2 * s2 * z22;
    m->xh3  = -2 * s2 * (z23 - z21);

    if (m->lunar_terms_done)
      break;

    /* Do lunar terms */
    m->sse  = se;
    m->ssi  = si;
    m->ssl  = sl;
    m->ssh  = sh / deep_arg->sinio;
    m->ssg  = sgh - deep_arg->cosio * m->ssh;
    m->se2  = m->ee2;
    m->si2  = m->xi2;
    m->sl2  = m->xl2;
    m->sgh2 = m->xgh2;
    m->sh2  = m->xh2;
    m->se3  = m->e3;
    m->si3  = m->xi3;
    m->sl3  = m->xl3;
    m->sgh3 = m->xgh3;
    m->sh3  = m->xh3;
    m->sl4  = m->xl4;
    m->sgh4 = m->xgh4;
    zcosg   = m->zcosgl;
    zsing   = m->zsingl;
    zcosi   = m->zcosil;
    zsini   = m->zsinil;
    zcosh   = m->zcoshl * cosq+m->zsinhl * sinq;
    zsinh   = sinq * m->zcoshl-cosq * m->zsinhl;
    zn      = ZNL;
    cc      = C1L;
    ze      = ZEL;

    /* zmo = m->zmol; */
    m->lunar_terms_done = SU_TRUE;
  }

  m->sse += se;
  m->ssi += si;
  m->ssl += sl;
  m->ssg += sgh - deep_arg->cosio / deep_arg->sinio * sh;
  m->ssh += sh / deep_arg->sinio;

  /* Geopotential resonance initialization for 12 hour orbits */
  m->resonance_flag = SU_FALSE;
  m->synchronous_flag = SU_FALSE;

  if (m->xnq <= 0.0034906585 || m->xnq >= 0.0052359877) {
    if (m->xnq < 0.00826 || m->xnq > 0.00924)
      return;

    if (eq<0.5)
      return;

    m->resonance_flag = SU_TRUE;
    eoc               = eq * deep_arg->eosq;
    g201              = -0.306 - (eq - 0.64) * 0.440;

    if (eq <= 0.65) {
      g211 = 3.616 - 13.247 * eq + 16.290 * deep_arg->eosq;
      g310 = -19.302 + 117.390 * eq - 228.419 * deep_arg->eosq + 156.591 * eoc;
      g322 = -18.9068 + 109.7927 * eq-214.6334 * deep_arg->eosq + 146.5816 * eoc;
      g410 = -41.122+242.694 * eq - 471.094 * deep_arg->eosq + 313.953 * eoc;
      g422 = -146.407+841.880 * eq - 1629.014 * deep_arg->eosq + 1083.435 * eoc;
      g520 = -532.114+3017.977 * eq - 5740 * deep_arg->eosq + 3708.276 * eoc;
    } else {
      g211 = -72.099 + 331.819 * eq - 508.738 * deep_arg->eosq + 266.724 * eoc;
      g310 = -346.844 + 1582.851 * eq - 2415.925 * deep_arg->eosq + 1246.113 * eoc;
      g322 = -342.585 + 1554.908 * eq - 2366.899 * deep_arg->eosq + 1215.972 * eoc;
      g410 = -1052.797 + 4758.686 * eq - 7193.992 * deep_arg->eosq + 3651.957 * eoc;
      g422 = -3581.69 + 16178.11 * eq - 24462.77 * deep_arg->eosq + 12422.52 * eoc;

      if (eq <= 0.715)
        g520 = 1464.74 - 4664.75 * eq + 3763.64 * deep_arg->eosq;

      else
        g520 = -5149.66 + 29936.92 * eq - 54087.36 * deep_arg->eosq + 31324.56 * eoc;
    }

    if (eq < 0.7) {
      g533 = -919.2277+4988.61 * eq - 9064.77 * deep_arg->eosq + 5542.21 * eoc;
      g521 = -822.71072+4568.6173 * eq - 8491.4146 * deep_arg->eosq + 5337.524 * eoc;
      g532 = -853.666+4690.25 * eq - 8624.77 * deep_arg->eosq + 5341.4 * eoc;
    } else {
      g533 = -37995.78 + 161616.52 * eq - 229838.2 * deep_arg->eosq + 109377.94 * eoc;
      g521 = -51752.104 + 218913.95 * eq - 309468.16 * deep_arg->eosq + 146349.42 * eoc;
      g532 = -40023.88 + 170470.89 * eq - 242699.48 * deep_arg->eosq + 115605.82 * eoc;
    }

    sini2 = deep_arg->sinio * deep_arg->sinio;
    f220 = 0.75 * (1 + 2 * deep_arg->cosio+deep_arg->theta2);
    f221 = 1.5 * sini2;
    f321 =
        1.875 * deep_arg->sinio * (
            1 - 2 * deep_arg->cosio - 3 * deep_arg->theta2);
    f322 =
        -1.875 * deep_arg->sinio * (
            1 + 2 * deep_arg->cosio - 3 * deep_arg->theta2);
    f441 = 35 * sini2 * f220;
    f442 = 39.3750 * sini2 * sini2;
    f522 =
        9.84375 * deep_arg->sinio * (
            sini2 * (
                  1
                - 2 * deep_arg->cosio - 5 * deep_arg->theta2)
                + 0.33333333 * (
                    -2 + 4 * deep_arg->cosio + 6 * deep_arg->theta2));
    f523 =
        deep_arg->sinio * (
            4.92187512 * sini2 * (
                - 2
                - 4 * deep_arg->cosio
                + 10 * deep_arg->theta2)
          + 6.56250012 * (1 + 2 * deep_arg->cosio - 3 * deep_arg->theta2));
    f542 =
        29.53125 * deep_arg->sinio * (
              2
            - 8 * deep_arg->cosio+deep_arg->theta2 * (
                -12 + 8 * deep_arg->cosio + 10 * deep_arg->theta2));
    f543 =
        29.53125 * deep_arg->sinio * (
            -2 - 8 * deep_arg->cosio+deep_arg->theta2 * (
                12 + 8 * deep_arg->cosio - 10 * deep_arg->theta2));
    xno2     = m->xnq * m->xnq;
    ainv2    = aqnv * aqnv;
    temp1    = 3 * xno2 * ainv2;
    temp     = temp1 * ROOT22;
    m->d2201 = temp * f220 * g201;
    m->d2211 = temp * f221 * g211;
    temp1    = temp1 * aqnv;
    temp     = temp1 * ROOT32;
    m->d3210 = temp * f321 * g310;
    m->d3222 = temp * f322 * g322;
    temp1    = temp1 * aqnv;
    temp = 2 * temp1 * ROOT44;
    m->d4410 = temp * f441 * g410;
    m->d4422 = temp * f442 * g422;
    temp1    = temp1 * aqnv;
    temp     = temp1 * ROOT52;
    m->d5220 = temp * f522 * g520;
    m->d5232 = temp * f523 * g532;
    temp = 2 * temp1 * ROOT54;
    m->d5421 = temp * f542 * g521;
    m->d5433 = temp * f543 * g533;
    m->xlamo = xmao+m->xnodeo+m->xnodeo-m->thgr-m->thgr;
    bfact    = deep_arg->xmdot+deep_arg->xnodot+deep_arg->xnodot-THDT-THDT;
    bfact    = bfact+m->ssl+m->ssh+m->ssh;
  } else {
    m->resonance_flag = SU_TRUE;
    m->synchronous_flag = SU_TRUE;

    /* Synchronous resonance terms initialization */
    g200     = 1 + deep_arg->eosq * (-2.5 + 0.8125 * deep_arg->eosq);
    g310     = 1 + 2 * deep_arg->eosq;
    g300     = 1 + deep_arg->eosq * (-6 + 6.60937 * deep_arg->eosq);
    f220     = 0.75 * (1 + deep_arg->cosio) * (1 + deep_arg->cosio);
    f311     =
        0.9375 * deep_arg->sinio * deep_arg->sinio * (1 + 3 * deep_arg->cosio)
         -0.75 * (1 + deep_arg->cosio);
    f330     = 1 + deep_arg->cosio;
    f330     = 1.875 * f330 * f330 * f330;
    m->del1  = 3 * m->xnq * m->xnq * aqnv * aqnv;
    m->del2  = 2 * m->del1 * f220 * g200 * Q22;
    m->del3  = 3 * m->del1 * f330 * g300 * Q33 * aqnv;
    m->del1  = m->del1 * f311 * g310 * Q31 * aqnv;
    m->fasx2 = 0.13130908;
    m->fasx4 = 2.8843198;
    m->fasx6 = 0.37448087;
    m->xlamo = xmao + m->xnodeo + m->omegao - m->thgr;
    bfact    = deep_arg->xmdot + xpidot - THDT;
    bfact   += m->ssl + m->ssg + m->ssh;
  }

  m->xfact = bfact - m->xnq;

  /* Initialize integrator */
  m->stepp = 720;
  m->stepn = -720;
  m->step2 = 259200;

  return;
}

SUPRIVATE void
suscan_sdp4_deep_initialize_dynamic(
    const struct suscan_sdp4_params *m,
    suscan_deep_arg_dynamic_t *deep_dyn){
  deep_dyn->savtsn = 1E20;
  deep_dyn->loop_flag = 0;
  deep_dyn->epoch_restart_flag = 0;
  deep_dyn->xli = m->xlamo;
  deep_dyn->xni = m->xnq;
  deep_dyn->atime = 0;
}

void
suscan_sdp4_deep(
    const struct suscan_sdp4_params *m,
    int ientry,
    const suscan_deep_arg_fixed_t *deep_arg,
    suscan_deep_arg_dynamic_t *deep_dyn)
{
  /* This function is used by SDP4 to add lunar and solar */
  /* perturbation effects to deep-space orbit objects.  */

  SUDOUBLE alfdp,
  sinis, sinok, sil, betdp, dalf, cosis, cosok, dbet, dls, f2,
  f3, xnoh, pgh, ph, sel, ses, xls, sinzf, sis, sll, sls, temp,
  x2li, x2omi, xl, xldot, xnddt,
  xndot, xomi, zf, zm,
  delt = 0, ft = 0;

  switch (ientry) {
    case SUSCAN_SDP4_TYPE_SECULAR:  /* Entrance for deep space secular effects */
      deep_dyn->xll    = deep_dyn->xll+m->ssl * deep_dyn->t;
      deep_dyn->omgadf = deep_dyn->omgadf+m->ssg * deep_dyn->t;
      deep_dyn->xnode  = deep_dyn->xnode+m->ssh * deep_dyn->t;
      deep_dyn->em     = m->eo+m->sse * deep_dyn->t;
      deep_dyn->xinc   = m->xincl+m->ssi * deep_dyn->t;

      if (deep_dyn->xinc < 0) {
        deep_dyn->xinc   = -deep_dyn->xinc;
        deep_dyn->xnode  = deep_dyn->xnode + PI;
        deep_dyn->omgadf = deep_dyn->omgadf - PI;
      }

      if (!m->resonance_flag)
        return;

      do {
        if (deep_dyn->atime == 0
            || (deep_dyn->t >= 0 && deep_dyn->atime < 0)
            || ((deep_dyn->t < 0) && (deep_dyn->atime >= 0))) {
          /* Epoch restart */

          delt            = deep_dyn->t >= 0 ? m->stepp : m->stepn;
          deep_dyn->atime = 0;
          deep_dyn->xni   = m->xnq;
          deep_dyn->xli   = m->xlamo;
        } else if (SU_ABS(deep_dyn->t) >= SU_ABS(deep_dyn->atime)) {
            delt = deep_dyn->t > 0 ? m->stepp : m->stepn;
        }

        do {
          if (SU_ABS(deep_dyn->t-deep_dyn->atime)>= m->stepp) {
            deep_dyn->loop_flag = SU_TRUE;
            deep_dyn->epoch_restart_flag = SU_FALSE;
          } else {
            ft = deep_dyn->t-deep_dyn->atime;
            deep_dyn->loop_flag = 0;
          }

          if (SU_ABS(deep_dyn->t) < SU_ABS(deep_dyn->atime)) {
            delt = deep_dyn->t >= 0 ? m->stepp : m->stepn;
            deep_dyn->loop_flag = SU_TRUE;
            deep_dyn->epoch_restart_flag = SU_TRUE;
          }

          /* Dot terms calculated */
          if (m->synchronous_flag) {
            xndot =
                m->del1 * SU_SIN(deep_dyn->xli - m->fasx2)
              + m->del2 * SU_SIN(2 * (deep_dyn->xli - m->fasx4))
              + m->del3 * SU_SIN(3 * (deep_dyn->xli - m->fasx6));

            xnddt =
                m->del1 * SU_COS(deep_dyn->xli - m->fasx2)
            + 2 * m->del2 * SU_COS(2 * (deep_dyn->xli - m->fasx4))
            + 3 * m->del3 * SU_COS(3 * (deep_dyn->xli - m->fasx6));
          } else {
            xomi = m->omegaq+deep_arg->omgdot * deep_dyn->atime;
            x2omi = xomi + xomi;
            x2li = deep_dyn->xli + deep_dyn->xli;
            xndot =
                m->d2201 * SU_SIN(x2omi + deep_dyn->xli - G22)
              + m->d2211 * SU_SIN(deep_dyn->xli - G22)
              + m->d3210 * SU_SIN(xomi + deep_dyn->xli - G32)
              + m->d3222 * SU_SIN(-xomi + deep_dyn->xli - G32)
              + m->d4410 * SU_SIN(x2omi + x2li-G44)
              + m->d4422 * SU_SIN(x2li - G44)
              + m->d5220 * SU_SIN(xomi + deep_dyn->xli - G52)
              + m->d5232 * SU_SIN(-xomi + deep_dyn->xli - G52)
              + m->d5421 * SU_SIN(xomi + x2li - G54)
              + m->d5433 * SU_SIN(-xomi + x2li - G54);

            xnddt =
                m->d2201 * SU_COS(x2omi + deep_dyn->xli - G22)
               +m->d2211 * SU_COS(deep_dyn->xli - G22)
               +m->d3210 * SU_COS(xomi + deep_dyn->xli - G32)
               +m->d3222 * SU_COS(-xomi + deep_dyn->xli - G32)
               +m->d5220 * SU_COS(xomi + deep_dyn->xli - G52)
               +m->d5232 * SU_COS(-xomi + deep_dyn->xli - G52)
               +2 * (m->d4410 * SU_COS(x2omi + x2li - G44)
                    +m->d4422 * SU_COS(x2li - G44)
                    +m->d5421 * SU_COS(xomi + x2li - G54)
                    +m->d5433 * SU_COS(-xomi + x2li - G54));
          }

          xldot = deep_dyn->xni+m->xfact;
          xnddt = xnddt * xldot;

          if (deep_dyn->loop_flag) {
            deep_dyn->xli   = deep_dyn->xli + xldot * delt+xndot * m->step2;
            deep_dyn->xni   = deep_dyn->xni + xndot * delt+xnddt * m->step2;
            deep_dyn->atime = deep_dyn->atime + delt;
          }
        } while (deep_dyn->loop_flag && !deep_dyn->epoch_restart_flag);
      } while (deep_dyn->loop_flag && deep_dyn->epoch_restart_flag);

      deep_dyn->xn = deep_dyn->xni + xndot * ft+xnddt * ft * ft * 0.5;
      xl = deep_dyn->xli + xldot * ft + xndot * ft * ft * 0.5;
      temp = -deep_dyn->xnode + m->thgr + deep_dyn->t * THDT;

      deep_dyn->xll = m->synchronous_flag
          ? (xl - deep_dyn->omgadf + temp)
          : (xl + temp + temp);

      break;

    case SUSCAN_SDP4_TYPE_PERIODIC:   /* Entrance for lunar-solar periodics */
      SU_SINCOS(deep_dyn->xinc, &sinis, &cosis);
      if (SU_ABS(deep_dyn->savtsn - deep_dyn->t)>= 30) {
        deep_dyn->savtsn = deep_dyn->t;
        zm              = m->zmos + ZNS * deep_dyn->t;
        zf              = zm + 2 * ZES * SU_SIN(zm);
        sinzf           = SU_SIN(zf);
        f2              = 0.5 * sinzf * sinzf-0.25;
        f3              = -0.5 * sinzf * SU_COS(zf);
        ses             = m->se2 * f2 + m->se3 * f3;
        sis             = m->si2 * f2 + m->si3 * f3;
        sls             = m->sl2 * f2 + m->sl3 * f3 + m->sl4 * sinzf;
        deep_dyn->sghs  = m->sgh2 * f2 + m->sgh3 * f3 + m->sgh4 * sinzf;
        deep_dyn->shs   = m->sh2 * f2 + m->sh3 * f3;
        zm              = m->zmol + ZNL * deep_dyn->t;
        zf              = zm+2 * ZEL * SU_SIN(zm);
        sinzf           = SU_SIN(zf);
        f2              = 0.5 * sinzf * sinzf-0.25;
        f3              = -0.5 * sinzf * SU_COS(zf);
        sel             = m->ee2 * f2 + m->e3 * f3;
        sil             = m->xi2 * f2 + m->xi3 * f3;
        sll             = m->xl2 * f2 + m->xl3 * f3 + m->xl4 * sinzf;
        deep_dyn->sghl  = m->xgh2 * f2 + m->xgh3 * f3 + m->xgh4 * sinzf;
        deep_dyn->sh1   = m->xh2 * f2 + m->xh3 * f3;
        deep_dyn->pe    = ses + sel;
        deep_dyn->pinc  = sis + sil;
        deep_dyn->pl    = sls + sll;
      }

      pgh               = deep_dyn->sghs + deep_dyn->sghl;
      ph                = deep_dyn->shs + deep_dyn->sh1;
      deep_dyn->xinc    = deep_dyn->xinc + deep_dyn->pinc;
      deep_dyn->em      = deep_dyn->em + deep_dyn->pe;

      if (m->xqncl>= 0.2) {
        /* Apply periodics directly */
        ph               /= deep_arg->sinio;
        pgh              -= deep_arg->cosio * ph;
        deep_dyn->omgadf += pgh;
        deep_dyn->xnode  += ph;
        deep_dyn->xll    += deep_dyn->pl;
      } else {
        /* Apply periodics with Lyddane modification */
        SU_SINCOS(deep_dyn->xnode, &sinok, &cosok);
        alfdp           = sinis * sinok;
        betdp           = sinis * cosok;
        dalf            = ph * cosok + deep_dyn->pinc * cosis * sinok;
        dbet            = -ph * sinok + deep_dyn->pinc * cosis * cosok;
        alfdp          += dalf;
        betdp          += dbet;
        deep_dyn->xnode = SU_FMOD(deep_dyn->xnode, TWO_PI);
        xls             =
            deep_dyn->xll + deep_dyn->omgadf + cosis * deep_dyn->xnode;
        dls             =
            deep_dyn->pl + pgh - deep_dyn->pinc * deep_dyn->xnode * sinis;
        xls             = xls + dls;
        xnoh            = deep_dyn->xnode;
        deep_dyn->xnode = SU_ATAN2(alfdp, betdp);

        /* This is a patch to Lyddane modification */
        /* suggested by Rob Matson. */

        if (SU_ABS(xnoh-deep_dyn->xnode) > PI)
          deep_dyn->xnode += deep_dyn->xnode < xnoh ? TWO_PI : -TWO_PI;

        deep_dyn->xll     += deep_dyn->pl;
        deep_dyn->omgadf   =
              xls
            - deep_dyn->xll
            - SU_COS(deep_dyn->xinc) * deep_dyn->xnode;
      }

      break;
  }
}
