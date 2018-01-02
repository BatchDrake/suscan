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

#ifndef _SPECTSRC_H
#define _SPECTSRC_H

#include <sigutils/sigutils.h>
#include <sigutils/detect.h>

struct suscan_spectsrc_class {
  const char *name;
  const char *desc;
  void * (*ctor) (SUSCOUNT size);
  SUBOOL (*compute) (void *private, const SUCOMPLEX *data, SUFLOAT *result);
  void   (*dtor) (void *private);
};

const struct suscan_spectsrc_class *suscan_spectsrc_class_lookup(
    const char *name);

SUBOOL suscan_spectsrc_class_register(
    const struct suscan_spectsrc_class *class);

struct suscan_spectsrc {
  const struct suscan_spectsrc_class *class;
  void *private;

  enum sigutils_channel_detector_window window_type;
  SUCOMPLEX *window_func;
  SUCOMPLEX *window_buffer;
  SUSCOUNT   window_size;
};

typedef struct suscan_spectsrc suscan_spectsrc_t;

suscan_spectsrc_t *suscan_spectsrc_new(
    const struct suscan_spectsrc_class *class,
    SUSCOUNT size,
    enum sigutils_channel_detector_window window_type);

SUBOOL suscan_spectsrc_compute(
    suscan_spectsrc_t *src,
    const SUCOMPLEX *data,
    SUFLOAT *result);

void suscan_spectsrc_destroy(suscan_spectsrc_t *src);

#endif /* _SPECTSRC_H */
