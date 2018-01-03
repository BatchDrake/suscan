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
#include <sigutils/taps.h>

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
  SU_TRYCATCH(class->preproc != NULL, return SU_FALSE);
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

SUINLINE SUBOOL
suscan_spectsrc_init_window_func(suscan_spectsrc_t *src)
{
  unsigned int i;

  for (i = 0; i < src->window_size; ++i)
    src->window_func[i] = 1;

  switch (src->window_type) {
    case SU_CHANNEL_DETECTOR_WINDOW_NONE:
      /* Do nothing. */
      break;

    case SU_CHANNEL_DETECTOR_WINDOW_HAMMING:
      su_taps_apply_hamming_complex(
          src->window_func,
          src->window_size);
      break;

    case SU_CHANNEL_DETECTOR_WINDOW_HANN:
      su_taps_apply_hann_complex(
          src->window_func,
          src->window_size);
      break;

    case SU_CHANNEL_DETECTOR_WINDOW_FLAT_TOP:
      su_taps_apply_flat_top_complex(
          src->window_func,
          src->window_size);
      break;

    case SU_CHANNEL_DETECTOR_WINDOW_BLACKMANN_HARRIS:
      su_taps_apply_blackmann_harris_complex(
          src->window_func,
          src->window_size);
      break;

    default:
      SU_WARNING("Unsupported window function %d\n", src->window_type);
      return SU_FALSE;
  }

  return SU_TRUE;
}

suscan_spectsrc_t *
suscan_spectsrc_new(
    const struct suscan_spectsrc_class *class,
    SUSCOUNT size,
    enum sigutils_channel_detector_window window_type)
{
  suscan_spectsrc_t *new = NULL;
  unsigned int i;

  SU_TRYCATCH(new = calloc(1, sizeof(suscan_spectsrc_t)), goto fail);

  new->class = class;
  new->window_type = window_type;
  new->window_size = size;

  if (window_type != SU_CHANNEL_DETECTOR_WINDOW_NONE) {
    SU_TRYCATCH(
        new->window_func = malloc(size * sizeof(SUCOMPLEX)),
        goto fail);
    SU_TRYCATCH(
        suscan_spectsrc_init_window_func(new),
        goto fail);
  }

  SU_TRYCATCH(
      new->window_buffer = fftw_malloc(size * sizeof(SU_FFTW(_complex))),
      goto fail);

  SU_TRYCATCH(
      new->private = (class->ctor) (new),
      goto fail);

  SU_TRYCATCH(
      (new->fft_plan = SU_FFTW(_plan_dft_1d)(
          new->window_size,
          new->window_buffer,
          new->window_buffer,
          FFTW_FORWARD,
          FFTW_ESTIMATE)),
      goto fail);

  return new;

fail:
  if (new != NULL)
    suscan_spectsrc_destroy(new);

  return NULL;
}

SUBOOL
suscan_spectsrc_drop(suscan_spectsrc_t *src)
{
  SU_TRYCATCH(src->window_ptr == src->window_size, return SU_FALSE);

  src->window_ptr = 0;

  return SU_TRUE;
}

SUBOOL
suscan_spectsrc_calculate(suscan_spectsrc_t *src, SUFLOAT *result)
{
  unsigned int i;

  SU_TRYCATCH(src->window_ptr == src->window_size, return SU_FALSE);

  src->window_ptr = 0;

  if (src->class->preproc != NULL) {
    SU_TRYCATCH(
        (src->class->preproc) (
            src,
            src->private,
            src->window_buffer,
            src->window_size),
        return SU_FALSE);
  }

  /* Apply window function first */
  for (i = 0; i < src->window_size; ++i)
    src->window_buffer[i] *= src->window_func[i];

  /* Apply FFT */
  fftw_execute(src->fft_plan);

  /* Apply postprocessing */
  SU_TRYCATCH(
      (src->class->postproc) (
          src,
          src->private,
          src->window_buffer,
          src->window_size),
      return SU_FALSE);

  /* Convert to absolute value */
  for (i = 0; i < src->window_size; ++i)
    result[i] = SU_C_ABS(src->window_buffer[i]);

  return SU_TRUE;
}

SUSCOUNT
suscan_spectsrc_feed(
    suscan_spectsrc_t *spectsrc,
    const SUCOMPLEX *data,
    SUSCOUNT size)
{
  SUSCOUNT avail = spectsrc->window_size - spectsrc->window_ptr;

  if (size > avail)
    size = avail;

  memcpy(spectsrc->window_buffer + spectsrc->window_ptr, data, size);

  spectsrc->window_ptr += size;

  return size;
}

void
suscan_spectsrc_destroy(suscan_spectsrc_t *spectsrc)
{
  if (spectsrc != NULL)
    (spectsrc->class->dtor) (spectsrc->private);

  if (spectsrc->fft_plan != NULL)
    fftw_destroy_plan(spectsrc->fft_plan);

  if (spectsrc->window_func != NULL)
    free(spectsrc->window_func);

  if (spectsrc->window_buffer != NULL)
    fftw_free(spectsrc->window_buffer);

  free(spectsrc);
}

SUBOOL
suscan_init_spectsrcs(void)
{
  SU_TRYCATCH(suscan_spectsrc_psd_register(), return SU_FALSE);
  SU_TRYCATCH(suscan_spectsrc_cyclo_register(), return SU_FALSE);

  return SU_TRUE;
}
