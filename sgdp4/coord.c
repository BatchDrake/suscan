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

#define SU_LOG_DOMAIN "sgdp4-coord"

#include "sgdp4.h"
#include <sigutils/log.h>

#ifndef _SGDP4_LEAP_SECONDS
#  define _SGDP4_LEAP_SECONDS 23
#endif /* _SGDP4_LEAP_SECONDS */

/*
 * https://github.com/Spacecraft-Code/Vallado/blob/master/Matlab/teme2ecef.m
 * Units are in km and km/s
 * 
 * C implementation was inspired by the following files:
 * 
 * https://github.com/Hopperpop/Sgp4-Library/blob/master/src/sgp4coord.cpp
 * https://github.com/Hopperpop/Sgp4-Library/blob/master/src/sgp4unit.cpp
 * 
 * Under MIT license by Hopperpop (2016)
 */


SUPRIVATE SUDOUBLE  
gstime(SUDOUBLE jdut1)
{
  SUDOUBLE gst, ut1;

  ut1 = (jdut1 - 2451545.0) / 36525.0;
  gst = 
    ((-6.2e-6 * ut1 + 0.093104) 
    * ut1 + (876600.0 * 3600 + 8640184.812866))
    * ut1 + 67310.54841;  /* seconds */

  /* A day is worth 1 / 240 of a year */
  gst = fmod(SU_DEG2RAD(gst) / 240.0, 2 * PI);

  if (gst < 0.0)
    gst += 2 * PI;

  return gst;
}

SUPRIVATE void 
polarm(SUDOUBLE jdut1, SUDOUBLE pm[3][3])
{
  SUDOUBLE MJD; /* Modified julian date */
  SUDOUBLE A, C;
  SUDOUBLE xp, yp; /* Polar motion coefficients */
    
  /* See IERS Bulletin - A (Vol. XXVIII No. 030) */
  /* Polar motion is modelled after two major contributions:
   * Chandler Wobble and an anuall oscillation. Explanation
   * of these are in https://www.iers.org/IERS/EN/Science/EarthRotation/PolarMotion.html 
   */

  MJD = jdut1 - 2400000.5;
  A = 2 * PI * (MJD - 57226) / 365.25; /* Annual oscillation */
  C = 2 * PI * (MJD - 57226) / 435;    /* Chandler wobble */
    
  xp = (0.1033 + 0.0494 * cos(A) + 0.0482 * sin(A) + 0.0297 * cos(C) + 0.0307 * sin(C)) * 4.84813681e-6;
  yp = (0.3498 + 0.0441 * cos(A) - 0.0393 * sin(A) + 0.0307 * cos(C) - 0.0297 * sin(C)) * 4.84813681e-6;
    
  pm[0][0] = cos(xp);
  pm[0][1] = 0.0;
  pm[0][2] = -sin(xp);
  pm[1][0] = sin(xp) * sin(yp);
  pm[1][1] = cos(yp);
  pm[1][2] = cos(xp) * sin(yp);
  pm[2][0] = sin(xp) * cos(yp);
  pm[2][1] = -sin(yp);
  pm[2][2] = cos(xp) * cos(yp);
}

/* Refer to https://github.com/Spacecraft-Code/Vallado/blob/master/Matlab/teme2ecef.m */
void 
xyz_teme_to_ecef(
  const xyz_t *pos,
  const xyz_t *vel,
  SUDOUBLE jdut1, 
  xyz_t *ecef_pos,
  xyz_t *ecef_vel)
{
  xyz_t rpef, vpef;
  xyz_t omegaearth;
  SUDOUBLE gmst;
  SUDOUBLE st[3][3];
  SUDOUBLE pm[3][3];
  
  /* gmst= gstime( jdut1 ); */
  gmst = gstime(jdut1 + 23 / (3600. * 24.));
  
  omegaearth.x = 0.0;
  omegaearth.y = 0.0;
  omegaearth.z = 7.29211514670698e-05 * (1.0  - 0.0015563/86400.0);
    
  st[0][0] = cos(gmst);
  st[0][1] = -sin(gmst);
  st[0][2] = 0.0;
  st[1][0] = sin(gmst);
  st[1][1] = cos(gmst);
  st[1][2] = 0.0;
  st[2][0] = 0.0;
  st[2][1] = 0.0;
  st[2][2] = 1.0;

  /* [pm] = polarm(xp,yp,ttt,'80'); */
  polarm(jdut1, pm);

  /* Convert position */
  if (pos != NULL) {
    XYZ_MATMUL(&rpef, st, pos);
    XYZ_MATMUL(ecef_pos, pm, &rpef);
  }

  /* Convert velocity */
  if (vel != NULL) {
    XYZ_MATMUL(&vpef, st, vel);

    vpef.x -= omegaearth.y * rpef.z - omegaearth.z * rpef.y;
    vpef.y -= omegaearth.z * rpef.x - omegaearth.x * rpef.z;
    vpef.z -= omegaearth.x * rpef.y - omegaearth.y * rpef.x;
      
    XYZ_MATMUL(ecef_vel, pm, &vpef);
  }
}

/*
 * https://github.com/Spacecraft-Code/Vallado/blob/master/Matlab/ijk2ll.m
 */
#define XYZ_TOL   1e-8
#define EARTHECC2 .006694385000 /* Eccentricity of Earth^2 */

void 
xyz_ecef_to_geodetic(const xyz_t *pos, xyz_t *geo)
{
  SUDOUBLE r, temp, rtasc;
  SUDOUBLE delta_prev, sint, c = 0;
  unsigned int i = 0;

  r = XYZ_NORM(pos);

  /* Compute longitude */
  temp = sqrt(pos->x * pos->x + pos->y * pos->y);
 
  if (sufeq(temp, 0, XYZ_TOL))
    rtasc = .5 * PI * SU_SGN(pos->z);
  else
    rtasc = atan2(pos->y, pos->x);
    
  geo->lon = rtasc;
    
  if (fabs(geo->lon) >= PI)
    geo->lon -= SU_SGN(geo->lon) * 2 * PI;
  
  /* Compute latitude */
  geo->lat   = asin(pos->z / r);
  delta_prev = geo->lat + 10.0;
    
  while (!sufeq(geo->lat, delta_prev, XYZ_TOL) && (i < 10)) {
    delta_prev = geo->lat;
    sint       = sin(geo->lat);
    c          = EQRAD / sqrt(1.0 - EARTHECC2 * sint * sint);
    geo->lat   = atan2(pos->z + c * EARTHECC2 * sint, temp);
    ++i;
  }
    
  /* Compute height taking eccentricity into account */
  if (.5 * PI - fabs(geo->lat) > PI / 180.0)
    geo->height = (temp / cos(geo->lat)) - c;
  else
    geo->height = pos->z / sin(geo->lat) - c * (1.0 - EARTHECC2);
}

SUDOUBLE 
time_unix_to_julian(SUDOUBLE timestamp)
{
   return (timestamp / 86400.0) + 2440587.5;
}

SUDOUBLE 
time_julian_to_unix(SUDOUBLE jd)
{
	return floor((jd - 2440587.5) * 86400.0 + 0.5);
}

SUDOUBLE 
time_timeval_to_julian(const struct timeval *tv)
{
   return (tv->tv_sec / 86400.0 + tv->tv_usec / 86400.0e6) + 2440587.5;
}
