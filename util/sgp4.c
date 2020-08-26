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

/*  Explicitly disable single precision here  */
#if defined(_SU_SINGLE_PRECISION)
#  undef _SU_SINGLE_PRECISION
#endif /*  defined(_SU_SINGLE_PRECISION)  */

#include "sgp4.h"
#include "sgdp4defs.h"

void
suscan_sgp4_params_init(
  const suscan_tle_t  * tle,
  struct suscan_sgp4_params  * m)
{
  SUDOUBLE  x1m5th, xhdot1, a1, a3ovk2, ao, betao, betao2, c1sq, c2, c3,
  coef, coef1, del1, delo, eeta, eosq, etasq, perigee, pinvsq, psisq, qoms24,
  s4, temp, temp1, temp2, temp3, theta2, theta4, tsi;

  m->simple_flag = SU_FALSE;

  m->bstar  = tle->bstar_drag_term / AE;
  m->eo     = tle->eccentricity;
  m->xno    = tle->mean_motion * TWO_PI / MINUTES_PER_DAY;

  m->xincl  = SU_DEG2RAD(tle->inclination);
  m->xnodeo = SU_DEG2RAD(tle->right_ascension);
  m->omegao = SU_DEG2RAD(tle->argument_of_perigee);
  m->xmo    = SU_DEG2RAD(tle->mean_anomaly);

  a1        = SU_POW(XKE/m->xno, TWO_THIRD);
  m->cosio  = SU_COS(m->xincl);
  theta2    = m->cosio * m->cosio;
  m->x3thm1 = 3 * theta2 - 1.0;
  eosq      = m->eo * m->eo;
  betao2    = 1.0-eosq;
  betao     = SU_SQRT(betao2);
  del1      = 1.5 * CK2 * m->x3thm1 / (a1  * a1 * betao * betao2);

  ao        = a1 * (
              1.0
              - del1 * (
                  0.5 * TWO_THIRD + del1 * (1.0 + 134.0 / 81.0 * del1)));

  delo     = 1.5 * CK2 * m->x3thm1 / (ao * ao * betao * betao2);
  m->xnodp = m->xno / (1.0 + delo);
  m->aodp  = ao / (1.0 - delo);

  /*  For perigee less than 220 kilometers, the "simple"    */
  /*  flag is set and the equations are truncated to linear   */
  /*  variation in sqrt a and quadratic variation in mean   */
  /*  anomaly.  Also, the c3 term, the delta omega term, and  */
  /*  the delta m term are dropped.               */

  m->simple_flag  =
      (m->aodp * (1 - m->eo) / AE) < (220 / EARTH_RADIUS_KM_WGS84 + AE);

  /*  For perigees below 156 km, the     */
  /*  values of s and qoms2t are altered.  */

  perigee = (m->aodp * (1 - m->eo) - AE) * EARTH_RADIUS_KM_WGS84;

  if (perigee < 156.0) {
    s4     = perigee <= 98.0 ? 20 : (perigee - 78.0);
    qoms24 = SU_POW((120 - s4) * AE / EARTH_RADIUS_KM_WGS84, 4);
    s4     = s4 / EARTH_RADIUS_KM_WGS84 + AE;
  } else {
    s4     = S_DENSITY_PARAM;
    qoms24 = QOMS2T;
  }

  pinvsq    = 1 / (m->aodp * m->aodp * betao2 * betao2);
  tsi       = 1 / m->aodp-s4;
  m->eta    = m->aodp * m->eo * tsi;
  etasq     = m->eta * m->eta;
  eeta      = m->eo * m->eta;
  psisq     = SU_ABS(1 - etasq);
  coef      = qoms24 * SU_SQR(tsi) * SU_SQR(tsi);
  coef1     = coef * SU_POW(psisq, -3.5);
  c2        = coef1 * m->xnodp * (
      m->aodp * (1 + 1.5 * etasq + eeta * (4 + etasq))
      + 0.75 * CK2 * tsi / psisq * m->x3thm1 * (8 + 3 * etasq * (8 + etasq)));
  m->c1     = m->bstar * c2;
  m->sinio  = SU_SIN(m->xincl);
  a3ovk2    = -J3_HARMONIC_WGS72 / CK2 * SU_CUBE(AE);
  c3        = coef * tsi * a3ovk2 * m->xnodp * AE * m->sinio / m->eo;
  m->x1mth2 = 1-theta2;

  m->c4 = 2 * m->xnodp * coef1 * m->aodp * betao2 * (
      m->eta * (2 + 0.5 * etasq)
      + m->eo * (0.5 + 2 * etasq)
      - 2 * CK2 * tsi/(m->aodp * psisq) * (
          -3 * m->x3thm1 * (
              1
              - 2 * eeta
              + etasq * (1.5-0.5 * eeta))
          + 0.75 * m->x1mth2 * (2 * etasq-eeta * (
              1 + etasq)) * SU_COS(2 * m->omegao)));

  m->c5 = 2 * coef1 * m->aodp * betao2 * (
      1 + 2.75 * (etasq + eeta) + eeta * etasq);

  theta4 = theta2 * theta2;
  temp1  = 3 * CK2 * pinvsq * m->xnodp;
  temp2  = temp1 * CK2 * pinvsq;
  temp3  = 1.25 * CK4 * pinvsq * pinvsq * m->xnodp;

  m->xmdot =
      m->xnodp
      + 0.5 * temp1 * betao * m->x3thm1
      + 0.0625 * temp2 * betao * (13 - 78 * theta2 + 137 * theta4);

  x1m5th = 1 - 5 * theta2;

  m->omgdot =
      -0.5 * temp1 * x1m5th
      + 0.0625 * temp2 * (7 - 114 * theta2 + 395 * theta4)
      + temp3 * (3-36 * theta2 + 49 * theta4);

  xhdot1    = -temp1 * m->cosio;
  m->xnodot = xhdot1 + (
      0.5 * temp2 * (4-19 * theta2)
      + 2 * temp3 * (3-7 * theta2)) * m->cosio;
  m->omgcof = m->bstar * c3 * SU_COS(m->omegao);
  m->xmcof  = -TWO_THIRD * coef * m->bstar * AE/eeta;
  m->xnodcf = 3.5 * betao2 * xhdot1 * m->c1;
  m->t2cof  = 1.5 * m->c1;
  m->xlcof  = 0.125 * a3ovk2 * m->sinio * (3 + 5 * m->cosio)/(1 + m->cosio);
  m->aycof  = 0.25 * a3ovk2 * m->sinio;
  m->delmo  = SU_POW(1 + m->eta * SU_COS(m->xmo), 3);
  m->sinmo  = SU_SIN(m->xmo);
  m->x7thm1 = 7 * theta2-1;

  if (!m->simple_flag) {
    c1sq     = m->c1 * m->c1;
    m->d2    = 4 * m->aodp * tsi * c1sq;
    temp     = m->d2 * tsi * m->c1 / 3;
    m->d3    = (17 * m->aodp + s4) * temp;
    m->d4    = 0.5 * temp * m->aodp * tsi * (221 * m->aodp + 31 * s4) * m->c1;
    m->t3cof = m->d2 + 2 * c1sq;
    m->t4cof = 0.25 * (3 * m->d3 + m->c1 * (12 * m->d2 + 10 * c1sq));
    m->t5cof = 0.2 * (3 * m->d4 + 12 * m->c1 * m->d3 + 6 * m->d2 * m->d2 + 15 * c1sq * (2 * m->d2 + c1sq));
  }
}

void
suscan_sgp4_predict(
    const struct suscan_sgp4_params *m,
    SUDOUBLE tsince,
    struct suscan_ephemeris_model_output *output)
{
  SUDOUBLE cosuk, sinuk, rfdotk, vx, vy, vz, ux, uy, uz, xmy, xmx, cosnok,
  sinnok, cosik, sinik, rdotk, xinck, xnodek, uk, rk, cos2u, sin2u,
  u, sinu, cosu, betal, rfdot, rdot, r, pl, elsq, esine, ecose, epw,
  cosepw, tfour, sinepw, capu, ayn, xlt, aynl, xll,
  axn, xn, beta, xl, e, a, tcube, delm, delomg, templ, tempe, tempa,
  xnode, tsq, xmp, omega, xnoddf, omgadf, xmdf, temp, temp1, temp2,
  temp3, temp4, temp5, temp6;

  int i;

  /*  Update for secular gravity and atmospheric drag.  */
  xmdf   = m->xmo + m->xmdot * tsince;
  omgadf = m->omegao + m->omgdot * tsince;
  xnoddf = m->xnodeo + m->xnodot * tsince;
  omega  = omgadf;
  xmp    = xmdf;
  tsq    = tsince * tsince;
  xnode  = xnoddf + m->xnodcf * tsq;
  tempa  = 1-m->c1 * tsince;
  tempe  = m->bstar * m->c4 * tsince;
  templ  = m->t2cof * tsq;

  if (!m->simple_flag) {
    delomg = m->omgcof * tsince;
    delm   = m->xmcof * (SU_POW(1 + m->eta * SU_COS(xmdf), 3) - m->delmo);
    temp   = delomg + delm;
    xmp    = xmdf + temp;
    omega  = omgadf-temp;
    tcube  = tsq * tsince;
    tfour  = tsince * tcube;
    tempa  = tempa-m->d2 * tsq-m->d3 * tcube-m->d4 * tfour;
    tempe  = tempe + m->bstar * m->c5 * (SU_SIN(xmp) - m->sinmo);
    templ  = templ + m->t3cof * tcube + tfour * (m->t4cof + tsince * m->t5cof);
  }

  a    = m->aodp * SU_SQR(tempa);
  e    = m->eo-tempe;
  xl   = xmp + omega + xnode + m->xnodp * templ;
  beta = SU_SQRT(1-e * e);
  xn   = XKE / SU_POW(a,1.5);

  /*  Long period periodics  */
  axn   = e * SU_COS(omega);
  temp  = 1 / (a * beta * beta);
  xll   = temp * m->xlcof * axn;
  aynl  = temp * m->aycof;
  xlt   = xl + xll;
  ayn   = e * SU_SIN(omega) + aynl;

  /*  Solve Kepler's Equation  */
  capu  = SU_FMOD(xlt - xnode, TWO_PI);
  temp2 = capu;
  i = 0;

  do {
    sinepw = SU_SIN(temp2);
    cosepw = SU_COS(temp2);
    temp3  = axn * sinepw;
    temp4  = ayn * cosepw;
    temp5  = axn * cosepw;
    temp6  = ayn * sinepw;
    epw    = (capu-temp4 + temp3-temp2) / (1 - temp5-temp6) + temp2;

    if (sufeq(epw, temp2, E6A))
      break;

    temp2 = epw;
  } while (i++ < 10);

  /*  Short period preliminary quantities  */
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

  /*  Update for short periodics  */
  rk =
      r * (1 - 1.5 * temp2 * betal * m->x3thm1)
      + 0.5 * temp1 * m->x1mth2 * cos2u;
  uk     = u - 0.25 * temp2 * m->x7thm1 * sin2u;
  xnodek = xnode + 1.5 * temp2 * m->cosio * sin2u;
  xinck  = m->xincl + 1.5 * temp2 * m->cosio * m->sinio * cos2u;
  rdotk  = rdot-xn * temp1 * m->x1mth2 * sin2u;
  rfdotk = rfdot + xn * temp1 * (m->x1mth2 * cos2u + 1.5 * m->x3thm1);

  /*  Orientation vectors  */
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

  /*  Position and velocity  */
  output->pos[0] = rk * ux;
  output->pos[1] = rk * uy;
  output->pos[2] = rk * uz;

  output->vel[0] = rdotk * ux + rfdotk * vx;
  output->vel[1] = rdotk * uy + rfdotk * vy;
  output->vel[2] = rdotk * uz + rfdotk * vz;

  /*  Phase in radians  */
  output->phase  = xlt-xnode-omgadf + TWO_PI;

  if (output->phase<0.0)
    output->phase  +=  TWO_PI;

  output->phase  = SU_FMOD(output->phase, TWO_PI);

  output->xinck  = xinck;
  output->omgadf = omgadf;
  output->xnodek = xnodek;
}
