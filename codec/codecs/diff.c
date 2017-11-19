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

#define SU_LOG_DOMAIN "diff-codec"

#include <sigutils/log.h>
#include <codec.h>

SUPRIVATE struct suscan_codec_class class;

SUPRIVATE SUBOOL
suscan_codec_diff_ctor(
    void **private,
    struct suscan_codec *codec,
    unsigned int bits_per_symbol,
    const suscan_config_t *config,
    enum su_codec_direction direction)
{
  struct suscan_field_value *value = NULL;
  su_codec_t *sucodec;

  /* This should always work */
  SU_TRYCATCH(
      value = suscan_config_get_value(config, "sign"),
      return SU_FALSE);

  SU_TRYCATCH(
      sucodec = su_codec_new("diff", bits_per_symbol, value->as_bool),
      return SU_FALSE);

  if (direction == SUSCAN_CODEC_DIRECTION_FORWARDS)
    su_codec_set_direction(sucodec, SU_CODEC_DIRECTION_FORWARDS);
  else
    su_codec_set_direction(sucodec, SU_CODEC_DIRECTION_BACKWARDS);

  *private = sucodec;

  return SU_TRUE;
}

SUSCOUNT
suscan_codec_diff_process(
    void *private,
    grow_buf_t *result, /* Out */
    struct suscan_codec_progress *progress, /* Out */
    SUBITS *data,
    SUSCOUNT len)
{
  su_codec_t *codec = (su_codec_t *) private;
  SUSYMBOL ret;
  SUSCOUNT processed;
  SUBITS c;

  processed = len;

  while (len--) {
    ret = su_codec_feed(codec, SU_TOSYM(*data++));

    if (SU_ISSYM(ret)) {
      c = SU_FROMSYM(ret);
      SU_TRYCATCH(grow_buf_append(result, &c, sizeof(SUBITS)), return -1);
    }
  }

  return processed;
}

void
suscan_codec_diff_dtor(void *private)
{
  su_codec_destroy((su_codec_t *) private);
}

SUBOOL
suscan_codec_class_diff_register(void)
{
  SU_TRYCATCH(
      class.config_desc = suscan_config_desc_new(),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_desc_add_field(
          class.config_desc,
          SUSCAN_FIELD_TYPE_BOOLEAN,
          SU_FALSE,
          "sign",
          "Invert difference sign"),
      return SU_FALSE);

  class.desc = "Generic differential codec";
  class.directions = SUSCAN_CODEC_CODEC_BOTH;

  class.ctor    = suscan_codec_diff_ctor;
  class.process = suscan_codec_diff_process;
  class.dtor    = suscan_codec_diff_dtor;

  SU_TRYCATCH(
      suscan_codec_class_register(&class),
      return SU_FALSE);

  return SU_TRUE;
}
