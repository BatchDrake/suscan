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

#define SU_LOG_DOMAIN "spectsrc"

#include "spectsrc.h"

PTR_LIST_CONST(struct suscan_spectsrc_class, spectsrc_class);

const struct suscan_spectsrc_class *
suscan_spectsrc_class_lookup(const char *name)
{
  unsigned int i;

  for (i = 0; i < spectsrc_class_count; ++i)
    if (strcmp(spectsrc_class_list[i]->name, name) == 0)
      return spectsrc_class_list[i];

  return NULL;
}

SUBOOL
suscan_spectsrc_class_register(const struct suscan_spectsrc_class *class)
{
  SU_TRYCATCH(class->name    != NULL, return SU_FALSE);
  SU_TRYCATCH(class->desc    != NULL, return SU_FALSE);
  SU_TRYCATCH(class->compute != NULL, return SU_FALSE);
  SU_TRYCATCH(class->ctor    != NULL, return SU_FALSE);
  SU_TRYCATCH(class->dtor    != NULL, return SU_FALSE);

  SU_TRYCATCH(
      suscan_spectsrc_class_lookup(class->name) == NULL,
      return SU_FALSE);

  SU_TRYCATCH(
      PTR_LIST_APPEND_CHECK(spectsrc_class, (void *) class) != -1,
      return SU_FALSE);

  return SU_TRUE;
}

suscan_spectsrc_t *
suscan_spectsrc_new(
    const struct suscan_spectsrc_class *class,
    SUSCOUNT fs,
    SUSCOUNT size)
{
  suscan_spectsrc_t *new = NULL;

  SU_TRYCATCH(new = calloc(1, sizeof(suscan_spectsrc_t)), goto fail);

  new->class = class;

  SU_TRYCATCH(new->private = (class->ctor) (fs, size), goto fail);

  return new;

fail:
  if (new != NULL)
    suscan_spectsrc_destroy(new);

  return NULL;
}

SUBOOL
suscan_spectsrc_compute(
    suscan_spectsrc_t *src,
    const SUCOMPLEX *data,
    SUFLOAT *result)
{
  return (src->class->compute) (src->private, data, result);
}

void
suscan_spectsrc_destroy(suscan_spectsrc_t *spectsrc)
{
  if (spectsrc != NULL)
    (spectsrc->class->dtor) (spectsrc->private);

  free(spectsrc);
}

SUBOOL
suscan_init_spectsrcs(void)
{

  return SU_TRUE;
}
