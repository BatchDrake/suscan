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

#define SU_LOG_DOMAIN "tle-corrector"

#include <sigutils/log.h>

#include "corrector.h"
#include "tle.h"
#include <sgdp4/sgdp4.h>

SUPRIVATE struct suscan_frequency_corrector_class g_tle_corrector_class;

void
suscan_tle_corrector_destroy(suscan_tle_corrector_t *self)
{
  sgdp4_prediction_finalize(&self->prediction);
}

suscan_tle_corrector_t *
suscan_tle_corrector_new_from_file(const char *path, const xyz_t *site)
{
  suscan_tle_corrector_t *new = NULL;
  orbit_t orbit = orbit_INITIALIZER;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(new = calloc(1, sizeof(suscan_tle_corrector_t)), goto done);

  SU_TRYCATCH(orbit_init_from_file(&orbit, path), goto done);

  SU_TRYCATCH(
    sgdp4_prediction_init(&new->prediction, &orbit, site),
    goto done);

  ok = SU_TRUE;

done:
  orbit_finalize(&orbit);

  if (!ok) {
    if (new != NULL)
      suscan_tle_corrector_destroy(new);
  }

  return new;
}

suscan_tle_corrector_t *
suscan_tle_corrector_new(const char *string, const xyz_t *site)
{
  suscan_tle_corrector_t *new = NULL;
  orbit_t orbit = orbit_INITIALIZER;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(new = calloc(1, sizeof(suscan_tle_corrector_t)), goto done);

  SU_TRYCATCH(
    orbit_init_from_data(&orbit, string, strlen(string)), 
    goto done);

  SU_TRYCATCH(
    sgdp4_prediction_init(&new->prediction, &orbit, site),
    goto done);

  ok = SU_TRUE;

done:
  orbit_finalize(&orbit);

  if (!ok) {
    if (new != NULL)
      suscan_tle_corrector_destroy(new);
  }

  return new;
}

suscan_tle_corrector_t *
suscan_tle_corrector_new_from_orbit(const orbit_t *orbit, const xyz_t *site)
{
  suscan_tle_corrector_t *new = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(new = calloc(1, sizeof(suscan_tle_corrector_t)), goto done);

  SU_TRYCATCH(
    sgdp4_prediction_init(&new->prediction, orbit, site),
    goto done);

  ok = SU_TRUE;

done:
  if (!ok) {
    if (new != NULL)
      suscan_tle_corrector_destroy(new);
  }

  return new;
}

SUBOOL
suscan_tle_corrector_visible(
  suscan_tle_corrector_t *self,
  const struct timeval *tv)
{
  xyz_t azel;

  sgdp4_prediction_update(&self->prediction, tv);
  sgdp4_prediction_get_azel(&self->prediction, &azel);

  return azel.elevation >= 0;
}

SUBOOL
suscan_frequency_corrector_tle_get_report(
  suscan_frequency_corrector_t *fc,
  const struct timeval *tv,
  SUFREQ freq,
  struct suscan_orbit_report *report)
{
  suscan_tle_corrector_t *self;
  xyz_t pos_azel, vel_azel;

  if (suscan_frequency_corrector_get_class(fc) != &g_tle_corrector_class)
    return SU_FALSE;

  self = suscan_frequency_corrector_get_userdata(fc);

  sgdp4_prediction_update(&self->prediction, tv);
  sgdp4_prediction_get_azel(&self->prediction, &pos_azel);
  sgdp4_prediction_get_vel_azel(&self->prediction, &vel_azel);

  report->freq_corr = vel_azel.distance / SPEED_OF_LIGHT_KM_S * freq;
  report->rx_time   = *tv;
  report->vlos_vel  = vel_azel.distance;
  report->satpos    = pos_azel;

  return SU_TRUE;
}

/* TODO: Use correction reports? */
SUBOOL
suscan_tle_corrector_correct_freq(
  suscan_tle_corrector_t *self,
  const struct timeval *tv,
  SUFREQ freq,
  SUFLOAT *delta_freq)
{
  xyz_t pos_azel, vel_azel;

  sgdp4_prediction_update(&self->prediction, tv);
  sgdp4_prediction_get_azel(&self->prediction, &pos_azel);
  sgdp4_prediction_get_vel_azel(&self->prediction, &vel_azel);
  
  *delta_freq = -vel_azel.distance / SPEED_OF_LIGHT_KM_S * freq;

  return SU_TRUE;
}

SUPRIVATE void *
suscan_tle_corrector_ctor(va_list ap)
{
  void *userdata = NULL;
  enum suscan_tle_corrector_mode mode;
  const xyz_t *xyz;

  mode = va_arg(ap, enum suscan_tle_corrector_mode);
  xyz  = va_arg(ap, const xyz_t *);

  switch (mode) {
    case SUSCAN_TLE_CORRECTOR_MODE_FILE:
      SU_TRYCATCH(
        userdata = suscan_tle_corrector_new_from_file(
          va_arg(ap, const char *),
          xyz),
        goto done);
      break;

    case SUSCAN_TLE_CORRECTOR_MODE_STRING:
      SU_TRYCATCH(
        userdata = suscan_tle_corrector_new(va_arg(ap, const char *), xyz),
        goto done);
      break;

    case SUSCAN_TLE_CORRECTOR_MODE_ORBIT:
      SU_TRYCATCH(
        userdata = suscan_tle_corrector_new_from_orbit(
          va_arg(ap, const orbit_t *),
          xyz),
        goto done);
      break;

    default:
      SU_ERROR("Invalid corrector mode %d\n", mode);
  }

done:
  return userdata;
}

SUPRIVATE void
suscan_tle_corrector_dtor(void *userdata)
{
  suscan_tle_corrector_destroy(userdata);
}

SUPRIVATE SUBOOL 
suscan_tle_corrector_applicable(
    void *userdata, 
    const struct timeval *source_time)
{
  return SU_TRUE;
}

SUPRIVATE SUFLOAT
suscan_tle_corrector_get_correction(
    void *userdata,
    const struct timeval *source_time,
    SUFREQ abs_freq)
{
  SUFLOAT delta_freq = 0;

  if (!suscan_tle_corrector_correct_freq(
    userdata,
    source_time,
    abs_freq,
    &delta_freq))
      return 0;

  return delta_freq;
}

SUBOOL 
suscan_tle_corrector_init(void)
{
  SUBOOL ok = SU_FALSE;

  g_tle_corrector_class.name = "tle";
  g_tle_corrector_class.ctor = suscan_tle_corrector_ctor;
  g_tle_corrector_class.dtor = suscan_tle_corrector_dtor;
  g_tle_corrector_class.applicable = suscan_tle_corrector_applicable;
  g_tle_corrector_class.get_correction = suscan_tle_corrector_get_correction;

  SU_TRYCATCH(suscan_frequency_corrector_class_register(&g_tle_corrector_class), goto done);

  ok = SU_TRUE;

done:
  return ok;
}