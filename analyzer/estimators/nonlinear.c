/*

  Copyright (C) 2017 Gonzalo José Carracedo Carballal

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

#include <string.h>

#define SU_LOG_DOMAIN "nonlinear-estimator"

#include <sigutils/detect.h>
#include "source.h"
#include "estimator.h"

SUPRIVATE void *
suscan_estimator_nonlinear_ctor(SUSCOUNT fs)
{
  struct sigutils_channel_detector_params cd_params =
      sigutils_channel_detector_params_INITIALIZER;

  cd_params.samp_rate = fs;
  cd_params.window_size = SUSCAN_SOURCE_DEFAULT_BUFSIZ;
  cd_params.tune = SU_FALSE; /* Estimators expect baseband signals */
  cd_params.mode = SU_CHANNEL_DETECTOR_MODE_NONLINEAR_DIFF;

  return su_channel_detector_new(&cd_params);
}

SUPRIVATE SUBOOL
suscan_estimator_nonlinear_feed(
    void *private,
    const SUCOMPLEX *x,
    SUSCOUNT size)
{
  SU_TRYCATCH(
      su_channel_detector_feed_bulk(
          (su_channel_detector_t *) private,
          x,
          size) == size,
      return SU_FALSE);

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_estimator_nonlinear_read(const void *private, SUFLOAT *out)
{
  *out = su_channel_detector_get_baud((const su_channel_detector_t *) private);

  return SU_TRUE;
}

SUPRIVATE void
suscan_estimator_nonlinear_dtor(void *private)
{
  su_channel_detector_destroy((su_channel_detector_t *) private);
}

SUBOOL
suscan_estimator_nonlinear_register(void)
{
  static struct suscan_estimator_class class = {
      .name  = "baud-nonlinear",
      .desc  = "Non-linear baud estimator",
      .field = "clock.baud",
      .ctor  = suscan_estimator_nonlinear_ctor,
      .feed  = suscan_estimator_nonlinear_feed,
      .read  = suscan_estimator_nonlinear_read,
      .dtor  = suscan_estimator_nonlinear_dtor
  };

  SU_TRYCATCH(suscan_estimator_class_register(&class), return SU_FALSE);

  return SU_TRUE;
}
