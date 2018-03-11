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

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>

#include "source.h"
#include "xsig.h"

#ifdef _SU_SINGLE_PRECISION
#  define sf_read sf_read_float
#else
#  define sf_read sf_read_double
#endif

SUPRIVATE SUBOOL xsig_source_block_class_registered = SU_FALSE;

SUPRIVATE void
xsig_source_params_finalize(struct xsig_source_params *params)
{
  if (params->file != NULL)
    free((void *) params->file);
}

SUPRIVATE SUBOOL
xsig_source_params_copy(
    struct xsig_source_params *dest,
    const struct xsig_source_params *orig)
{
  memset(dest, 0, sizeof (struct xsig_source_params));

  if ((dest->file = strdup(orig->file)) == NULL)
    goto fail;

  dest->window_size = orig->window_size;
  dest->onacquire = orig->onacquire;
  dest->private = orig->private;
  dest->loop = orig->loop;

  return SU_TRUE;

fail:
  xsig_source_params_finalize(dest);

  return SU_FALSE;
}

void
xsig_source_destroy(struct xsig_source *source)
{
  xsig_source_params_finalize(&source->params);

  if (source->sf != NULL)
    sf_close(source->sf);

  if (source->as_complex != NULL)
    free(source->as_complex);

  free(source);
}

struct xsig_source *
xsig_source_new(const struct xsig_source_params *params)
{
  struct xsig_source *new = NULL;

  if ((new = calloc(1, sizeof (struct xsig_source))) == NULL)
    goto fail;

  if (params->raw_iq) {
    new->info.format = SF_FORMAT_RAW | SF_FORMAT_FLOAT | SF_ENDIAN_LITTLE;
    new->info.channels = 2;
    new->info.samplerate = params->samp_rate;
  }

  if (!xsig_source_params_copy(&new->params, params)) {
    SU_ERROR("failed to copy source params\n");
    goto fail;
  }

  if ((new->sf = sf_open(params->file, SFM_READ, &new->info)) == NULL) {
    SU_ERROR(
        "failed to open `%s': error %s\n",
        params->file,
        sf_strerror(NULL));
    goto fail;
  }

  /* These are used to expose block properties */
  new->samp_rate = new->info.samplerate;
  new->fc = params->fc;

  if ((new->as_complex = malloc(params->window_size * sizeof(SUCOMPLEX)))
      == NULL) {
    SU_ERROR("cannot allocate memory for read window\n");
    goto fail;
  }

  return new;

fail:
  if (new != NULL)
    xsig_source_destroy(new);

  return NULL;
}

/* I'm doing this only because I want to save a buffer */
SUPRIVATE void
xsig_source_complete_acquire(struct xsig_source *source)
{
  if (source->params.onacquire != NULL)
    (source->params.onacquire)(source, source->params.private);
}

SUBOOL
xsig_source_acquire(struct xsig_source *source)
{
  unsigned int size;
  unsigned int real_count;
  int got;
  int i;

  real_count = source->params.window_size * source->info.channels;

  do {
    got = sf_read(source->sf, source->as_real, real_count);

    if (got == 0) {
      if (!source->params.loop)
        return SU_FALSE; /* End of file reached and looping disabled, stop */
      else if (sf_seek(source->sf, 0, SEEK_SET) == -1)
        return SU_FALSE; /* Seek failed, return */
    }
  } while (got == 0);

  /*
   * One channel only: convert everything to complex. Conversion
   * can be performed in-place
   */
  if (source->info.channels == 1)
    for (i = got - 1; i >= 0; --i)
      source->as_complex[i] = source->as_real[i];

  source->avail = got / source->info.channels;

  xsig_source_complete_acquire(source);

  return SU_TRUE;
}

/* Source as block */
SUPRIVATE SUBOOL
xsig_source_block_ctor(struct sigutils_block *block, void **private, va_list ap)
{
  SUBOOL ok = SU_FALSE;
  struct xsig_source *source = NULL;
  const struct xsig_source_params *params;

  params = va_arg(ap, const struct xsig_source_params *);

  if ((source = xsig_source_new(params)) == NULL) {
    SU_ERROR("Failed to initialize signal source\n");
    goto done;
  }

  ok = SU_TRUE;

  ok = ok && su_block_set_property_ref(
      block,
      SU_PROPERTY_TYPE_INTEGER,
      "samp_rate",
      &source->samp_rate);

  ok = ok && su_block_set_property_ref(
      block,
      SU_PROPERTY_TYPE_INTEGER,
      "fc",
      &source->fc);

done:
  if (!ok) {
    if (source != NULL)
      xsig_source_destroy(source);
  }
  else
    *private = source;

  return ok;
}

SUPRIVATE void
xsig_source_block_dtor(void *private)
{
  struct xsig_source *source = (struct xsig_source *) private;

  xsig_source_destroy(source);
}

SUPRIVATE SUSDIFF
xsig_source_block_acquire(
    void *private,
    su_stream_t *out,
    unsigned int port_id,
    su_block_port_t *in)
{
  SUCOMPLEX *start;
  SUSDIFF size;
  SUSDIFF i;
  SUSDIFF ptr;
  struct xsig_source *source = (struct xsig_source *) private;

  size = su_stream_get_contiguous(out, &start, out->size);

  /* Ensure we can deliver something */
  if (source->avail == 0)
    if (!xsig_source_acquire(source))
      return SU_BLOCK_PORT_READ_END_OF_STREAM;

  if (size > source->avail)
    size = source->avail;

  ptr = source->params.window_size - source->avail;

  memcpy(start, source->as_complex + ptr, size * sizeof (SUCOMPLEX));

  /* Advance in stream */
  if (su_stream_advance_contiguous(out, size) != size) {
    SU_ERROR("Unexpected size after su_stream_advance_contiguous\n");
    return -1;
  }

  /* Mark these samples as consumed */
  source->avail -= size;

  /*
   * Finish acquisition process by updating FFT. This will alter the contents
   * of the window buffer and should not be used anymore until the next
   * window is read.
   */
  if (source->avail == 0)
    xsig_source_complete_acquire(source);

  return size;
}

SUPRIVATE struct sigutils_block_class xsig_source_block_class = {
    "xsig_source", /* name */
    0,     /* in_size */
    1,     /* out_size */
    xsig_source_block_ctor,    /* constructor */
    xsig_source_block_dtor,    /* destructor */
    xsig_source_block_acquire  /* acquire */
};

SUPRIVATE SUBOOL
xsig_source_assert_block_class(void)
{
  if (!xsig_source_block_class_registered) {
    if (!su_block_class_register(&xsig_source_block_class)) {
      SU_ERROR("Failed to initialize xsig source block class\n");
      return SU_FALSE;
    }

    xsig_source_block_class_registered = SU_TRUE;
  }

  return SU_TRUE;
}

su_block_t *
xsig_source_create_block(const struct xsig_source_params *params)
{
  su_block_t *block = NULL;

  if (!xsig_source_assert_block_class()) {
    SU_ERROR("cannot assert xsig source block class\n");
    return NULL;
  }

  if ((block = su_block_new("xsig_source", params)) == NULL) {
    SU_ERROR("cannot initialize signal source block\n");
    return NULL;
  }

  return block;
}

SUPRIVATE su_block_t *
suscan_wav_source_ctor(const struct suscan_source_config *config)
{
  struct xsig_source_params params;
  struct suscan_field_value *value;

  if ((value = suscan_source_config_get_value(config, "path")) == NULL)
    return NULL;
  params.file = value->as_string;

  if ((value = suscan_source_config_get_value(config, "fc")) == NULL)
    return NULL;
  params.fc = value->as_int; /* defaults to 0 */

  if ((value = suscan_source_config_get_value(config, "loop")) == NULL)
    return NULL;
  params.loop = value->as_bool; /* defaults to false */

  params.onacquire = NULL;
  params.private = NULL;
  params.window_size = 512;
  params.onacquire = NULL;
  params.raw_iq = SU_FALSE;

  return xsig_source_create_block(&params);
}

SUBOOL
suscan_wav_source_init(void)
{
  struct suscan_source *source = NULL;

  if ((source = suscan_source_register(
      "wavfile",
      "WAV/PCM/AIFF sound file",
      suscan_wav_source_ctor)) == NULL)
    return SU_FALSE;

  source->real_samp = SU_TRUE;

  if (!suscan_source_add_field(
      source,
      SUSCAN_FIELD_TYPE_FILE,
      SU_FALSE,
      "path",
      "File path"))
    return SU_FALSE;

  if (!suscan_source_add_field(
      source,
      SUSCAN_FIELD_TYPE_INTEGER,
      SU_TRUE,
      "fc",
      "Center frequency"))
    return SU_FALSE;

  if (!suscan_source_add_field(
      source,
      SUSCAN_FIELD_TYPE_BOOLEAN,
      SU_TRUE,
      "loop",
      "Loop"))
    return SU_FALSE;

  return SU_TRUE;
}

SUPRIVATE su_block_t *
suscan_iqfile_source_ctor(const struct suscan_source_config *config)
{
  struct xsig_source_params params;
  struct suscan_field_value *value;

  if ((value = suscan_source_config_get_value(config, "path")) == NULL)
    return NULL;
  params.file = value->as_string;

  if ((value = suscan_source_config_get_value(config, "fs")) == NULL)
    return NULL;
  params.samp_rate = value->as_int;

  if ((value = suscan_source_config_get_value(config, "fc")) == NULL)
    return NULL;
  params.fc = value->as_int; /* defaults to 0 */

  if ((value = suscan_source_config_get_value(config, "loop")) == NULL)
    return NULL;
  params.loop = value->as_bool; /* defaults to false */

  params.onacquire = NULL;
  params.private = NULL;
  params.window_size = 512;
  params.onacquire = NULL;
  params.raw_iq = SU_TRUE;

  return xsig_source_create_block(&params);
}

SUBOOL
suscan_iqfile_source_init(void)
{
  struct suscan_source *source = NULL;

  if ((source = suscan_source_register(
      "iqfile",
      "GQRX's I/Q recording",
      suscan_iqfile_source_ctor)) == NULL)
    return SU_FALSE;

  if (!suscan_source_add_field(
      source,
      SUSCAN_FIELD_TYPE_FILE,
      SU_FALSE,
      "path",
      "File path"))
    return SU_FALSE;

  if (!suscan_source_add_field(
      source,
      SUSCAN_FIELD_TYPE_INTEGER,
      SU_FALSE,
      "fs",
      "Sampling frequency"))
    return SU_FALSE;

  if (!suscan_source_add_field(
      source,
      SUSCAN_FIELD_TYPE_INTEGER,
      SU_TRUE,
      "fc",
      "Center frequency"))
    return SU_FALSE;

  if (!suscan_source_add_field(
      source,
      SUSCAN_FIELD_TYPE_BOOLEAN,
      SU_TRUE,
      "loop",
      "Loop"))
    return SU_FALSE;

  return SU_TRUE;
}

