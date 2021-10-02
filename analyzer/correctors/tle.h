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

#ifndef _ANALYZER_CORRECTORS_TLE_H
#define _ANALYZER_CORRECTORS_TLE_H

#include <sgdp4/sgdp4-types.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

enum suscan_tle_corrector_mode {
  SUSCAN_TLE_CORRECTOR_MODE_FILE,
  SUSCAN_TLE_CORRECTOR_MODE_STRING,
  SUSCAN_TLE_CORRECTOR_MODE_ORBIT
};

struct suscan_tle_corrector {
  sgdp4_ctx_t ctx;
  orbit_t     orbit;
  xyz_t       site;
};

typedef struct suscan_tle_corrector suscan_tle_corrector_t;

void suscan_tle_corrector_destroy(suscan_tle_corrector_t *self);

suscan_tle_corrector_t *suscan_tle_corrector_new_from_file(
  const char *path,
  const xyz_t *site);

suscan_tle_corrector_t *suscan_tle_corrector_new(
  const char *string,
  const xyz_t *site);

suscan_tle_corrector_t *suscan_tle_corrector_new_from_orbit(
  const orbit_t *orbit,
  const xyz_t *site);

SUBOOL suscan_tle_corrector_correct_freq(
  suscan_tle_corrector_t *self,
  const struct timeval *tv,
  SUFREQ freq,
  SUFLOAT *delta_freq);

/* Helper function to populate reports */
SUBOOL suscan_frequency_corrector_tle_get_report(
  suscan_frequency_corrector_t *fc,
  const struct timeval *tv,
  SUFREQ freq,
  struct suscan_orbit_report *report);

SUBOOL suscan_tle_corrector_init(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _ANALYZER_CORRECTORS_TLE_H */
