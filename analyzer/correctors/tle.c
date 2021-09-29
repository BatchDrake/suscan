/*

  Copyright (C) 2017 Gonzalo José Carracedo Carballal

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

#define SU_LOG_DOMAIN "tle-corrector"

#include <sigutils/log.h>

#include "corrector.h"
#include <sgdp4/sgdp4.h>

#define SPEED_OF_LIGHT_KM_S 299792.458

struct suscan_tle_corrector {
  sgdp4_ctx_t ctx;
  orbit_t     orbit;
  xyz_t       site;
};

typedef struct suscan_tle_corrector suscan_tle_corrector_t;

void
suscan_tle_corrector_destroy(
  suscan_tle_corrector_t *self)
{
  orbit_finalize(&self->orbit);
}

suscan_tle_corrector_t *
suscan_tle_corrector_new_from_file(const char *path, const xyz_t *site)
{
  suscan_tle_corrector_t *new = NULL;
  
  SU_TRYCATCH(new = calloc(1, sizeof(suscan_tle_corrector_t)), goto fail);

  new->site = *site;

  if (!orbit_init_from_file(&new->orbit, path)) {
    SU_ERROR("Invalid TLE file\n");
    goto fail;
  }

  SU_TRYCATCH(
    sgdp4_ctx_init(&new->ctx, &new->orbit) != SGDP4_ERROR,
    goto fail);

  return new;

fail:
  suscan_tle_corrector_destroy(new);
  return NULL;
}

SUBOOL
suscan_tle_corrector_correct_freq(
  suscan_tle_corrector_t *self,
  const struct timeval *tv,
  SUFREQ freq,
  SUFREQ *outfreq)
{
  SUDOUBLE dist, delta_v;
  SUDOUBLE discriminator;
  SUDOUBLE mins = orbit_minutes_from_timeval(&self->orbit, tv);
  xyz_t site_pos;
  xyz_t director;
  xyz_t pos, vel;
  xyz_t pos_ecef, vel_ecef;
  xyz_t pos_site_rel;
  kep_t kep;

  sgdp4_ctx_compute(&self->ctx, mins, SU_TRUE, &kep);

  kep_get_pos_vel_teme(&kep, &pos, &vel);
  xyz_teme_to_ecef(
    &pos, 
    &vel, 
    time_timeval_to_julian(tv), 
    &pos_ecef, 
    &vel_ecef);

  xyz_geodetic_to_ecef(&self->site, &site_pos);
  xyz_sub(&pos_ecef, &site_pos, &director);
  dist = XYZ_NORM(&director);

  xyz_sub(&pos_ecef, &site_pos, &pos_site_rel);
  discriminator = xyz_dotprod(&pos_site_rel, &site_pos);
  
  if (sufeq(dist, 0, 1e-8)) {
    delta_v = 0;
  } else {
    xyz_mul_c(&director, 1. / dist);
    delta_v = xyz_dotprod(&vel_ecef, &director);
  }

  *outfreq = delta_v / SPEED_OF_LIGHT_KM_S * freq;

  return discriminator > 0;
}

SUPRIVATE void *
suscan_tle_corrector_ctor(va_list ap)
{

}

SUPRIVATE void
suscan_tle_corrector_dtor(void *userdata)
{

}

SUPRIVATE SUBOOL 
suscan_tle_corrector_applicable(
    void *userdata, 
    const struct timeval *source_time)
{
  
}

SUPRIVATE SUFLOAT
suscan_tle_corrector_get_correction (
    void *userdata,
    const struct timeval *source_time,
    SUFREQ abs_freq)
{

}
