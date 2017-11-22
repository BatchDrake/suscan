/*

  Copyright (C) 2016 Gonzalo José Carracedo Carballal

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

#define SU_LOG_DOMAIN "decoder"

#include <sigutils/log.h>
#include "codec.h"

PTR_LIST_CONST(SUPRIVATE struct suscan_codec_class, codec_class);

SUBOOL
suscan_codec_class_register(
    const struct suscan_codec_class *class)
{
  SU_TRYCATCH(class->desc != NULL, return SU_FALSE);

  SU_TRYCATCH(class->directions & SUSCAN_CODEC_DIRECTION_BOTH, return SU_FALSE);

  SU_TRYCATCH(class->config_desc != NULL, return SU_FALSE);

  SU_TRYCATCH(class->process != NULL, return SU_FALSE);

  SU_TRYCATCH(
      PTR_LIST_APPEND_CHECK(codec_class, (void *) class) != -1,
      return SU_FALSE);

  return SU_TRUE;
}

void
suscan_codec_class_get_list(
    struct suscan_codec_class *const **list,
    unsigned int *count)
{
  *list = codec_class_list;
  *count = codec_class_count;
}

suscan_config_t *
suscan_codec_class_make_config(const struct suscan_codec_class *class)
{
  return suscan_config_new(class->config_desc);
}

unsigned int
suscan_codec_get_input_bits_per_symbol(const suscan_codec_t *codec)
{
  return codec->bits_per_symbol;
}

unsigned int
suscan_codec_get_output_bits_per_symbol(const suscan_codec_t *codec)
{
  return codec->output_bits_per_symbol;
}

void
suscan_codec_destroy(suscan_codec_t *codec)
{
  if (codec->class->ctor != NULL && codec->class->dtor != NULL)
    (codec->class->dtor) (codec->private);

  free(codec);
}

suscan_codec_t *
suscan_codec_class_make_codec(
    const struct suscan_codec_class *class,
    unsigned int bits_per_symbol,
    const suscan_config_t *config,
    enum su_codec_direction direction)
{
  suscan_codec_t *new = NULL;

  SU_TRYCATCH(new = calloc(1, sizeof(suscan_codec_t)), goto fail);

  new->bits_per_symbol = bits_per_symbol;
  new->output_bits_per_symbol = bits_per_symbol;
  new->class = class;

  if (class->ctor != NULL)
    SU_TRYCATCH(
        (class->ctor) (
            &new->private,
            new,
            bits_per_symbol,
            config,
            direction),
        new->class = NULL;
        goto fail);

  return new;

fail:
  if (new != NULL)
    suscan_codec_destroy(new);

  return NULL;
}

SUSDIFF
suscan_codec_feed(
    suscan_codec_t *codec,
    grow_buf_t *result, /* Out */
    struct suscan_codec_progress *progress, /* Out */
    SUBITS *data,
    SUSCOUNT len)
{
  struct suscan_codec_progress ignored;

  if (progress == NULL)
    progress = &ignored;

  progress->updated = SU_FALSE;

  return (codec->class->process) (
      codec->private,
      codec,
      result,
      progress,
      data,
      len);
}

SUBOOL
suscan_codec_class_register_builtin(void)
{
  SU_TRYCATCH(suscan_codec_class_diff_register(), return SU_FALSE);

  return SU_TRUE;
}
