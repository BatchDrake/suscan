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

#ifndef _ESTIMATOR_H
#define _ESTIMATOR_H

#include <sigutils/sigutils.h>

struct suscan_estimator_class {
  const char *name;
  const char *desc;
  const char *field;

  void * (*ctor) (SUSCOUNT fs);

  SUBOOL (*feed) (void *private, const SUCOMPLEX *samples, SUSCOUNT size);

  SUBOOL (*read) (const void *private, SUFLOAT *out);

  void (*dtor) (void *private);
};

struct suscan_estimator {
  const struct suscan_estimator_class *class;
  void *private;
  SUBOOL enabled;
};

typedef struct suscan_estimator suscan_estimator_t;

const struct suscan_estimator_class *suscan_estimator_class_lookup(
    const char *name);

SUINLINE SUBOOL
suscan_estimator_is_enabled(const suscan_estimator_t *estimator)
{
  return estimator->enabled;
}

SUINLINE void
suscan_estimator_set_enabled(suscan_estimator_t *estimator, SUBOOL state)
{
  estimator->enabled = state;
}

SUBOOL suscan_estimator_class_register(
    const struct suscan_estimator_class *class);

suscan_estimator_t *suscan_estimator_new(
    const struct suscan_estimator_class *class,
    SUSCOUNT fs);

SUBOOL suscan_estimator_feed(
    suscan_estimator_t *estimator,
    const SUCOMPLEX *samples,
    SUSCOUNT size);

SUBOOL suscan_estimator_read(
    const suscan_estimator_t *estimator,
    SUFLOAT *out);

void suscan_estimator_destroy(suscan_estimator_t *estimator);

/******************** Builtin channel estimators *****************************/
SUBOOL suscan_estimator_fac_register(void);
SUBOOL suscan_estimator_nonlinear_register(void);

SUBOOL suscan_init_estimators(void);

#endif /* _ANALYZER_ESTIMATOR_H */
