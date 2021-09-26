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

#ifndef _CORRECTOR_H
#define _CORRECTOR_H

#include <util.h>
#include <sigutils/types.h>
#include <stdarg.h>

struct suscan_frequency_corrector_class {
  const char *name;

  void   *(*ctor) (va_list);
  void    (*dtor) (void *userdata);
  SUBOOL  (*applicable) (
    void *userdata, 
    const struct timeval *source_time);

  SUFLOAT (*get_correction) (
    void *userdata,
    const struct timeval *source_time,
    SUFREQ abs_freq);  /* In Hz */
};

SUBOOL suscan_frequency_corrector_class_register(
  const struct suscan_frequency_corrector_class *classdef);

const struct suscan_frequency_corrector_class *
suscan_frequency_corrector_class_lookup(const char *name);

struct suscan_frequency_corrector {
  const struct suscan_frequency_corrector_class *iface;
  void *userdata;
};

typedef struct suscan_frequency_corrector suscan_frequency_corrector_t;

suscan_frequency_corrector_t *suscan_frequency_corrector_new(
  const char *name,
  ...);

void suscan_frequency_corrector_destroy(suscan_frequency_corrector_t *self);

SUINLINE SUBOOL
suscan_frequency_corrector_is_applicable(
  suscan_frequency_corrector_t *self,
  const struct timeval *source_time)
{
  if (self->iface->applicable == NULL)
    return SU_TRUE;
  
  return (self->iface->applicable) (self->userdata, source_time);
}

SUINLINE SUFLOAT
suscan_frequency_corrector_get_correction(
  suscan_frequency_corrector_t *self,
  const struct timeval *source_time,
  SUFREQ abs_freq)
{
  return (self->iface->get_correction) (self->userdata, source_time, abs_freq);
}

#endif /* _CORRECTOR_H */
