/*

  Copyright (C) 2021 Gonzalo José Carracedo Carballal

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

#define SU_LOG_DOMAIN "sgdp4-predict"
#define _DEFAULT_SOURCE

#include "sgdp4.h"
#include <sigutils/log.h>
#include <util/compat-time.h>

/*
 * This strategy was inspired by Gpredict, which provides a way to calculate
 * the start and end times of a pass.
 */

SUBOOL
sgdp4_prediction_update(
  sgdp4_prediction_t *self, 
  const struct timeval *tv)
{
  SUDOUBLE mins;
  xyz_t pos, vel, sat_geo;
  kep_t kep;

  if (!self->init 
  || self->tv.tv_sec != tv->tv_sec 
  || self->tv.tv_usec != tv->tv_usec) {
    mins = orbit_minutes_from_timeval(&self->orbit, tv);
  
    if (sgdp4_ctx_compute(&self->ctx, mins, SU_TRUE, &kep) == SGDP4_ERROR)
      return SU_FALSE;

    kep_get_pos_vel_teme(&kep, &pos, &vel);

    xyz_teme_to_ecef(
      &pos, 
      &vel, 
      time_timeval_to_julian(tv), 
      &self->pos_ecef, 
      &self->vel_ecef);

    xyz_ecef_to_razel(
      &self->pos_ecef, 
      &self->vel_ecef, 
      &self->site,
      &self->pos_azel,
      &self->vel_azel);

    xyz_ecef_to_geodetic(&self->pos_ecef, &sat_geo);
      
    self->alt = sat_geo.height;

    /* Done */
    self->init     = SU_TRUE;
    self->tv       = *tv;
  }

  return SU_TRUE;
}

SUPRIVATE SUBOOL
orbit_is_geo(const orbit_t *orbit)
{
  /* 
   * 1.0027 is actually the number of sidereal days per sidereal year,
   * this is because the number of revolutions per day is provided wrt
   * the inertial frame.
   */
  return sufeq(orbit->rev, 1.0027, 2e-4);
}

/*
 * Gpredict's original strategy was to assume a satellite was decayed
 * if:
 *                      16.666 - rev
 * sat_epoch(days) + ------------------ < now (days)
 *                      10 * |drag|
 * 
 * Where drag should have units of (rev / day^2), and it is calculated as:
 *          drev / dt
 * drag = --------------
 *            2 * pi 
 */

SUPRIVATE SUBOOL
orbit_is_decayed(const orbit_t *orbit, const struct timeval *tv)
{
  struct timeval epoch, diff;
  SUDOUBLE elapsed;
  SUDOUBLE max;

  orbit_epoch_to_timeval(orbit, &epoch);
  timersub(tv, &epoch, &diff);

  elapsed = diff.tv_sec + 1e-6 * diff.tv_usec;
  max = 2. * PI * 86400. * (16.666666 - orbit->rev) / (10 * orbit->d2revdt2);

  return elapsed > max;

}

SUPRIVATE SUBOOL
sgdp4_prediction_has_aos(const sgdp4_prediction_t *self)
{
  SUDOUBLE lin, sma, apogee;
  SUDOUBLE maxlat;

  if (orbit_is_geo(&self->orbit) 
      || orbit_is_decayed(&self->orbit, &self->tv)
      || self->orbit.rev == 0.0)
    return SU_FALSE;

  lin = self->orbit.eqinc;
  if (lin >= .5 * PI)
    lin = PI - lin;
  
  /* 
   * Near the poles, many near low-inclination satellites
   * orbit below the horizon. Verify this case.
   */
  sma    = 331.25 * pow(1440.0 / self->orbit.rev, 2. / 3.);
  apogee = sma * (1. + self->orbit.ecc) - EQRAD;

  maxlat = acos(EQRAD / (apogee + EQRAD)) + lin;

  return self->site.lat < fabs(maxlat);
}

SUPRIVATE SUDOUBLE
timeval_elapsed(const struct timeval *a, const struct timeval *b)
{
  struct timeval diff;
  timersub(a, b, &diff);

  return diff.tv_sec + 1e-6 * diff.tv_usec;
}

SUPRIVATE void
timeval_add_double(struct timeval *a, SUDOUBLE ddelta)
{
  struct timeval delta;
  
  if (ddelta < 0) {
    ddelta = -ddelta;
    delta.tv_sec  = floor(ddelta);
    delta.tv_usec = (ddelta - delta.tv_sec) * 1e6;
    timersub(a, &delta, a);
  } else {
    delta.tv_sec  = floor(ddelta);
    delta.tv_usec = (ddelta - delta.tv_sec) * 1e6;
    timeradd(a, &delta, a);
  }
}

SUBOOL
sgdp4_prediction_find_aos(
  sgdp4_prediction_t *self, 
  const struct timeval *tv, 
  SUDOUBLE window, /* In seconds */
  struct timeval *aos)
{
  SUDOUBLE delta_t;
  struct timeval t = *tv;

  sgdp4_prediction_update(self, tv);

  if (!sgdp4_prediction_has_aos(self))
    return SU_FALSE;

  if (self->pos_azel.elevation > 0.0) {
    if (!sgdp4_prediction_find_los(self, tv, window, &t))
      return SU_FALSE;

    t.tv_sec += 1440; /* 20 min */
  }

  sgdp4_prediction_update(self, &t);

  /* Coarse search of the AOS */
  while (self->pos_azel.elevation < -0.015
    && (window <= 0 || timeval_elapsed(&t, tv) < window)) {
    delta_t = -30 * (
      SU_RAD2DEG(self->pos_azel.elevation) * (self->alt / 8400. + .46) - 2.0);

    timeval_add_double(&t, delta_t);
    sgdp4_prediction_update(self, &t);
  }

  /* Fine grained search of AOS */
  while (window <= 0 || timeval_elapsed(&t, tv) < window) {
    if (sufeq(self->pos_azel.elevation, 0, 8.7e-5)) {
      *aos = t;
      break;
    }

    timeval_add_double(
      &t,
      -.163
        * SU_RAD2DEG(self->pos_azel.elevation) 
        * sqrt(self->alt));
    sgdp4_prediction_update(self, &t);
  }

  return SU_TRUE;
}

SUBOOL
sgdp4_prediction_find_los(
  sgdp4_prediction_t *self, 
  const struct timeval *tv, 
  SUDOUBLE delta_t, /* In seconds */
  struct timeval *los)
{
  struct timeval t = *tv;
  SUDOUBLE tmpel;

  sgdp4_prediction_update(self, tv);

  if (!sgdp4_prediction_has_aos(self))
    return SU_FALSE;

  if (self->pos_azel.elevation < 0.0) {
    if (!sgdp4_prediction_find_aos(self, tv, delta_t, &t))
      return SU_FALSE;

    t.tv_sec += 90; /* 1.5 min */
  }

  sgdp4_prediction_update(self, &t);

  /* Coarse search of the LOS */
  while (self->pos_azel.elevation < -0.015
    && (delta_t <= 0 || timeval_elapsed(&t, tv) < delta_t)) {
    timeval_add_double(
      &t,
      3.456 
        * cos(self->pos_azel.elevation - .017) 
        * sqrt(self->alt));
    sgdp4_prediction_update(self, &t);
  }

  /* Fine grained search of LOS */
  while (delta_t <= 0 || timeval_elapsed(&t, tv) < delta_t) {
    timeval_add_double(
      &t,
      .1719 
        * SU_RAD2DEG(self->pos_azel.elevation) 
        * sqrt(self->alt));
    sgdp4_prediction_update(self, &t);

    if (sufeq(self->pos_azel.elevation, 0, 8.7e-5)) {
      tmpel = self->pos_azel.elevation;
      timeval_add_double(&t, -1);
      sgdp4_prediction_update(self, &t);

      if (self->pos_azel.elevation > tmpel) {
        *los = t;
        break;
      }
    }
  }

  return SU_TRUE;
}

void 
sgdp4_prediction_get_azel(
  const sgdp4_prediction_t *self, 
  xyz_t *azel)
{
  *azel = self->pos_azel;
}

void 
sgdp4_prediction_get_ecef(
  const sgdp4_prediction_t *self, 
  xyz_t *ecef)
{
  *ecef = self->pos_ecef;
}

void 
sgdp4_prediction_get_vel_azel(
  const sgdp4_prediction_t *self, 
  xyz_t *v_azel)
{
  *v_azel = self->vel_azel;
}

void
sgdp4_prediction_finalize(sgdp4_prediction_t *self)
{
  if (self->orbit.name != NULL)
    free(self->orbit.name);
}

SUBOOL
sgdp4_prediction_init(
  sgdp4_prediction_t *self, 
  const orbit_t *orbit,
  const xyz_t *geo)
{
  int ret;
  SUBOOL ok = SU_FALSE;

  memset(self, 0, sizeof(sgdp4_prediction_t));
  
  self->orbit = *orbit;
  self->site  = *geo;
  
  gettimeofday(&self->tv, NULL);

  if (orbit->name != NULL)
    SU_TRYCATCH(self->orbit.name = strdup(orbit->name), goto done);

  ret = sgdp4_ctx_init(&self->ctx, &self->orbit);
  
  if (ret == SGDP4_ERROR) {
    SU_ERROR("SGDP4 initialization error\n");
    goto done;
  }

  if (ret == SGDP4_NOT_INIT) {
    SU_ERROR("SGDP4 not initialized\n");
    goto done;

  }
  ok = SU_TRUE;

done:
  if (!ok)
    sgdp4_prediction_finalize(self);

  return ok;
}