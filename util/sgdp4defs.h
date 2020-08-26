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

#ifndef _UTIL_SGDP4DEFS_H
#define _UTIL_SGDP4DEFS_H

/* Geosynchronous satellite parameters */
#define GEOSYNCHRONOUS_LOWER_MEAN_MOTION 0.9
#define GEOSYNCHRONOUS_UPPER_MEAN_MOTION 1.1
#define GEOSYNCHRONOUS_ECCENTRICITY_THRESHOLD 0.2
#define GEOSYNCHRONOUS_INCLINATION_THRESHOLD_DEGREES 70

/* Mathematic shorthands */
#define PI_HALF       (.5 * PI)
#define TWO_PI        (2 * PI)
#define THREE_PI_HALF (1.5 * PI)
#define TWO_THIRD     6.6666666666666666e-1

/* Time definitions */
#define MINUTES_PER_DAY 1.44E3
#define SECONDS_PER_DAY 8.6400E4
#define JULIAN_TIME_DIFF 2444238.5

/* Physical properties */
#define J3_HARMONIC_WGS72                      -2.53881E-6
#define EARTH_RADIUS_KM_WGS84                   6.378137e3
#define FLATTENING_FACTOR                       3.35281066474748e-3
#define EARTH_ROTATIONS_PER_SIDERIAL_DAY        1.00273790934
#define SOLAR_RADIUS_KM                         6.96000e5
#define ASTRONOMICAL_UNIT_KM                    1.49597870691e8
#define NAUTICAL_TWILIGHT_SUN_ELEVATION       -12.0
#define SPEED_OF_LIGHT                  299792458.0
#define EARTH_ANGULAR_VELOCITY                  7.292115e-5

/* Iteration constants */
#define AOSLOS_HORIZON_THRESHOLD    0.3
#define MAXELE_TIME_EQUALITY_THRESHOLD  FLT_EPSILON
#define MAXELE_MAX_NUM_ITERATIONS   10000

/* General spacetrack report #3 constants */
#define XKE             7.43669161e-2
#define CK2             5.413079e-4
#define E6A             1.0e-6
#define AE              1.0
#define CK4             6.209887e-7
#define S_DENSITY_PARAM 1.012229
#define QOMS2T          1.880279e-09

/* Constants in deep space subroutines. Not defined in spacetrack report #3. */

#define ZNS     1.19459e-5
#define C1SS    2.9864797e-6
#define ZES     1.675e-2
#define ZNL     1.5835218e-4
#define C1L     4.7968065e-7
#define ZEL     5.490e-2
#define ZCOSIS  9.1744867e-1
#define ZSINIS  3.9785416e-1
#define ZSINGS -9.8088458e-1
#define ZCOSGS  1.945905e-1
#define Q22     1.7891679e-6
#define Q31     2.1460748e-6
#define Q33     2.2123015e-7
#define G22     5.7686396
#define G32     9.5240898e-1
#define G44     1.8014998
#define G52     1.0508330
#define G54     4.4108898
#define ROOT22  1.7891679e-6
#define ROOT32  3.7393792e-7
#define ROOT44  7.3636953e-9
#define ROOT52  1.1428639e-7
#define ROOT54  2.1765803e-9
#define THDT    4.3752691e-3


#endif /* _UTIL_SGDP4DEFS_H */
