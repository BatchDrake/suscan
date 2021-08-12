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

#ifndef _SPECTSRC_H
#define _SPECTSRC_H

#include <sigutils/sigutils.h>
#include <sigutils/detect.h>
#include <sigutils/smoothpsd.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct suscan_spectsrc;

struct suscan_spectsrc_class {
  const char *name;
  const char *desc;

  void * (*ctor) (struct suscan_spectsrc *src);

  SUBOOL (*preproc)  (
      struct suscan_spectsrc *src,
      void *privdata,
      SUCOMPLEX *buffer,
      SUSCOUNT size);

  void (*dtor) (void *privdata);
};

const struct suscan_spectsrc_class *suscan_spectsrc_class_lookup(
    const char *name);

SUBOOL suscan_spectsrc_class_register(
    const struct suscan_spectsrc_class *classdef);

struct suscan_spectsrc {
  const struct suscan_spectsrc_class *classptr;
  void *privdata;

  SUSCOUNT        buffer_size;
  SUCOMPLEX      *buffer;
  su_smoothpsd_t *smooth_psd;

  SUBOOL (*on_spectrum) (void *userdata, const SUFLOAT *data, SUSCOUNT size);
  void *userdata;
};

typedef struct suscan_spectsrc suscan_spectsrc_t;

suscan_spectsrc_t *suscan_spectsrc_new(
    const struct suscan_spectsrc_class *classdef,
    SUFLOAT  samp_rate,
    SUFLOAT  spectrum_rate,
    SUSCOUNT size,
    enum sigutils_channel_detector_window window_type,
    SUBOOL (*on_spectrum) (void *userdata, const SUFLOAT *data, SUSCOUNT size),
    void *userdata);

SUSCOUNT suscan_spectsrc_feed(
    suscan_spectsrc_t *src,
    const SUCOMPLEX *data,
    SUSCOUNT size);

void suscan_spectsrc_destroy(suscan_spectsrc_t *src);

SUBOOL suscan_spectsrc_psd_register(void);
SUBOOL suscan_spectsrc_cyclo_register(void);
SUBOOL suscan_spectsrc_fmcyclo_register(void);
SUBOOL suscan_spectsrc_fmspect_register(void);
SUBOOL suscan_spectsrc_pmspect_register(void);
SUBOOL suscan_spectsrc_timediff_register(void);

SUBOOL suscan_spectsrc_exp_2_register(void);
SUBOOL suscan_spectsrc_exp_4_register(void);
SUBOOL suscan_spectsrc_exp_8_register(void);

SUBOOL suscan_init_spectsrcs(void);

SUBOOL suscan_spectsrcs_initialized(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _SPECTSRC_H */
