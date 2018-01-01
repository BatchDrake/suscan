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

#define SU_LOG_DOMAIN "estimator"

#include "estimator.h"

PTR_LIST_CONST(struct suscan_estimator_class, estimator_class);

const struct suscan_estimator_class *
suscan_estimator_class_lookup(const char *name)
{
  unsigned int i;

  for (i = 0; i < estimator_class_count; ++i)
    if (strcmp(estimator_class_list[i]->name, name) == 0)
      return estimator_class_list[i];

  return NULL;
}

SUBOOL
suscan_estimator_class_register(const struct suscan_estimator_class *class)
{
  SU_TRYCATCH(class->name  != NULL, return SU_FALSE);
  SU_TRYCATCH(class->desc  != NULL, return SU_FALSE);
  SU_TRYCATCH(class->field != NULL, return SU_FALSE);
  SU_TRYCATCH(class->ctor  != NULL, return SU_FALSE);
  SU_TRYCATCH(class->dtor  != NULL, return SU_FALSE);
  SU_TRYCATCH(class->read  != NULL, return SU_FALSE);
  SU_TRYCATCH(class->feed  != NULL, return SU_FALSE);

  SU_TRYCATCH(
      suscan_estimator_class_lookup(class->name) == NULL,
      return SU_FALSE);

  SU_TRYCATCH(
      PTR_LIST_APPEND_CHECK(estimator_class, (void *) class) != -1,
      return SU_FALSE);

  return SU_TRUE;
}

suscan_estimator_t *
suscan_estimator_new(
    const struct suscan_estimator_class *class,
    SUSCOUNT fs,
    const struct sigutils_channel *channel)
{
  suscan_estimator_t *new = NULL;

  SU_TRYCATCH(new = calloc(1, sizeof(suscan_estimator_t)), goto fail);

  new->class = class;

  SU_TRYCATCH(new->private = (class->ctor) (fs, channel), goto fail);

  return new;

fail:
  if (new != NULL)
    suscan_estimator_destroy(new);

  return NULL;
}

SUBOOL
suscan_estimator_feed(
    suscan_estimator_t *estimator,
    const SUCOMPLEX *samples,
    SUSCOUNT size)
{
  return (estimator->class->feed) (estimator->private, samples, size);
}

SUBOOL
suscan_estimator_read(const suscan_estimator_t *estimator, SUFLOAT *out)
{
  return (estimator->class->read) (estimator->private, out);
}

void
suscan_estimator_destroy(suscan_estimator_t *estimator)
{
  if (estimator != NULL)
    (estimator->class->dtor) (estimator->private);

  free(estimator);
}
