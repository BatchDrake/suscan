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

#define SU_LOG_DOMAIN "cli-tleinfo"

#include <sigutils/log.h>

#include <cli/cli.h>
#include <cli/cmds.h>
#include <sgdp4/sgdp4.h>

#define ORBIT_POINTS 5000

SUPRIVATE SUBOOL
sucli_tleinfo_save_orbit(
  sgdp4_ctx_t *ctx,
  orbit_t *orbit,
  SUDOUBLE t0,
  const char *file)
{
  FILE *outfp = NULL;
  kep_t kep;
  xyz_t pos, vel;
  SUDOUBLE delta;
  unsigned int i;

  SUBOOL ok = SU_FALSE;

  if ((outfp = fopen(file, "w")) == NULL) {
    SU_ERROR(
      "Cannot open `%s' for writing: %s\n", 
      file, 
      strerror(errno));
    goto done;
  }

  delta = 24. * 60 / (orbit->rev * ORBIT_POINTS);

  for (i = 0; i < ORBIT_POINTS; ++i) {
    sgdp4_ctx_compute(ctx, t0 + i * delta, SU_TRUE, &kep);
    kep_get_pos_vel_teme(&kep, &pos, &vel);

    fprintf(
      outfp, 
      "%10g,%10g,%10g,%10g,%10g,%10g\n",
      pos.x,
      pos.y,
      pos.z,
      vel.x,
      vel.y,
      vel.z);
  }

  ok = SU_TRUE;

done:
  if (outfp != NULL)
    fclose(outfp);
  return ok;
}

void
suscli_tleinfo_doppler(
  sgdp4_ctx_t *ctx,
  orbit_t *orbit,
  const struct timeval *tv,
  const xyz_t *site)
{
  SUDOUBLE dist, projvel, t0;
  SUDOUBLE discriminator;

  xyz_t site_pos;
  xyz_t director;
  xyz_t pos, vel;
  xyz_t pos_ecef, vel_ecef;
  xyz_t pos_site_rel;
  kep_t kep;

  t0 = orbit_minutes_from_timeval(orbit, tv);
  
  sgdp4_ctx_compute(ctx, t0, SU_TRUE, &kep);
  kep_get_pos_vel_teme(&kep, &pos, &vel);
  xyz_teme_to_ecef(
    &pos, 
    &vel, 
    time_timeval_to_julian(tv), 
    &pos_ecef, 
    &vel_ecef);

  xyz_geodetic_to_ecef(site, &site_pos);
  xyz_sub(&pos_ecef, &site_pos, &director);
  dist = XYZ_NORM(&director);

  xyz_sub(&pos_ecef, &site_pos, &pos_site_rel);
  discriminator = xyz_dotprod(&pos_site_rel, &site_pos);
  
  if (sufeq(dist, 0, 1e-8)) {
    projvel = 0;
  } else {
    xyz_mul_c(&director, 1. / dist);
    projvel = xyz_dotprod(&vel_ecef, &director);
  }

  printf(
    "Visible:           %s\n",
    discriminator < 0 
    ? "\033[1;31mNO\033[0m"
    : "\033[1;32mYES\033[0m");
  
  printf("VLOS velocity:  %+8.2lf km/s (distance = %8.2lf km)\n", projvel, dist);
}

SUBOOL
suscli_tleinfo_cb(const hashlist_t *params)
{
  orbit_t orbit = orbit_INITIALIZER;
  sgdp4_ctx_t ctx = sgdp4_ctx_INITIALIZER;

  const char *orbit_file = NULL;
  xyz_t pos, vel;
  xyz_t pos_ecef, vel_ecef;
  xyz_t latlon;
  xyz_t site;

  SUDOUBLE t_unix;
  SUDOUBLE t_epoch;
  SUDOUBLE t0;
  
  struct timeval tv_now;
  time_t epoch, now;
  kep_t kep;

  const char *file = NULL;
  SUBOOL ok = SU_FALSE;
  
  gettimeofday(&tv_now, NULL);
  now = tv_now.tv_sec;
  t_unix = now + 1e-6 * tv_now.tv_usec;

  SU_TRYCATCH(
    suscli_param_read_string(params, "file", &file, NULL), 
    goto done);

  SU_TRYCATCH(
    suscli_param_read_string(params, "orbitfile", &orbit_file, NULL), 
    goto done);

  SU_TRYCATCH(
    suscli_param_read_double(params, "lat", &site.lat, INFINITY),
    goto done);
  
  SU_TRYCATCH(
    suscli_param_read_double(params, "lon", &site.lon, INFINITY),
    goto done);

  SU_TRYCATCH(
    suscli_param_read_double(params, "alt", &site.height, 0.),
    goto done);

  if (file == NULL) {
    SU_ERROR("Please specify a TLE file with file=<path to TLE>\n");
    goto done;
  }

  if (!orbit_init_from_file(&orbit, file)) {
    SU_ERROR("Invalid TLE file\n");
    goto done;
  }

  sgdp4_ctx_init(&ctx, &orbit);

  t_epoch = orbit_epoch_to_unix(&orbit);
  epoch = (time_t) t_epoch;

  printf("Spacecraft name:   %s\n", orbit.name);
  printf("Epoch year:        %d\n", orbit.ep_year);
  printf("Epoch day:         %g\n", orbit.ep_day);
  printf("Drag term (B*):    %g\n", orbit.bstar);
  printf("Orbit inclination: %gº\n", SU_RAD2DEG(orbit.eqinc));
  printf("Ascension:         %gº\n", SU_RAD2DEG(orbit.ascn));
  printf("Eccentricity:      %g\n", orbit.ecc);
  printf("Arg. of perigee:   %gº\n", SU_RAD2DEG(orbit.argp));
  printf("Mean anomaly:      %gº\n", SU_RAD2DEG(orbit.mnan));
  printf("Mean motion:       %g rev / day\n", orbit.rev);
  printf("Period:            %g min\n", 24. * 60. / orbit.rev);
  printf("Revolution number: %ld\n", orbit.norb);
  printf(
    "Epoch (UTC):       %s", 
    asctime(gmtime(&epoch)));
  printf("Age (days):        %g\n", (t_unix - epoch) / 86400.);

  t0 = orbit_minutes_from_timeval(&orbit, &tv_now);

  printf("\n");
  printf("Local time:        %s", ctime(&now));

  sgdp4_ctx_compute(&ctx, t0, SU_TRUE, &kep);
  kep_get_pos_vel_teme(&kep, &pos, &vel);
  xyz_teme_to_ecef(
    &pos, 
    &vel, 
    time_timeval_to_julian(&tv_now),
    &pos_ecef,
    &vel_ecef);
  xyz_ecef_to_geodetic(&pos_ecef, &latlon);

  printf(
    "Pos (ECEF):        (%+8lg, %+8lg, %+8lg) [r = %g km]\n", 
    pos_ecef.x, 
    pos_ecef.y, 
    pos_ecef.z,
    XYZ_NORM(&pos_ecef));

  printf(
    "Vel (TEME):        (%+8lg, %+8lg, %+8lg) [v = %g km/s]\n", 
    vel.x, 
    vel.y, 
    vel.z,
    XYZ_NORM(&vel));

  printf(
    "Geodetic:          %+6.2lfN, %+6.2lfE (alt = %6.2lf km)\n",
    SU_RAD2DEG(latlon.lat),
    SU_RAD2DEG(latlon.lon),
    latlon.height);

  if (!isinf(site.lat) && !isinf(site.lon)) {
    site.lat = SU_DEG2RAD(site.lat);
    site.lon = SU_DEG2RAD(site.lon);
    suscli_tleinfo_doppler(&ctx, &orbit, &tv_now, &site);
  }

  if (orbit_file != NULL)
    if (!sucli_tleinfo_save_orbit(&ctx, &orbit, t0, orbit_file))
      goto done;

  ok = SU_TRUE;

done:
  orbit_finalize(&orbit);

  return ok;
}