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

SUPRIVATE struct suscan_codec_class diff_class;
SUPRIVATE struct suscan_codec_class pim_dpsk_class;

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

  if (codec->classptr == &pim_dpsk_class && bits_per_symbol < 2) {
    SU_ERROR(
        "This decoder cannot be created for less than 2 bits per symbol\n");
    return SU_FALSE;
  }
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

  if (codec->classptr == &pim_dpsk_class)
    codec->output_bits_per_symbol -= 1;
  *private = sucodec;

  return SU_TRUE;
}

SUSDIFF
suscan_codec_diff_process(
    void *private,
    struct suscan_codec *codec,
    grow_buf_t *result, /* Out */
    struct suscan_codec_progress *progress, /* Out */
    const SUBITS *data,
    SUSCOUNT len)
{
  su_codec_t *sucodec = (su_codec_t *) private;
  SUSYMBOL ret;
  SUSCOUNT processed;
  SUBITS c;
  SUBOOL divide = codec->classptr == &pim_dpsk_class;

  processed = len;

  while (len--) {
    ret = su_codec_feed(sucodec, SU_TOSYM(*data++));

    if (SU_ISSYM(ret)) {
      c = SU_FROMSYM(ret);

      /* For pi/m-DmPSK, we discard one bit */
      if (divide)
        c >>= 1;

      SU_TRYCATCH(
          grow_buf_append(result, &c, sizeof(SUBITS)) != -1,
          return -1);
    }
  }

  /* Update always */
  progress->updated = SU_TRUE;

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
  /* Generic differential decoder */
  SU_TRYCATCH(
      diff_class.config_desc = suscan_config_desc_new(),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_desc_add_field(
          diff_class.config_desc,
          SUSCAN_FIELD_TYPE_BOOLEAN,
          SU_FALSE,
          "sign",
          "Invert difference sign"),
      return SU_FALSE);

  diff_class.desc = "Generic differential codec";
  diff_class.directions = SUSCAN_CODEC_DIRECTION_BOTH;

  diff_class.ctor    = suscan_codec_diff_ctor;
  diff_class.process = suscan_codec_diff_process;
  diff_class.dtor    = suscan_codec_diff_dtor;

  SU_TRYCATCH(
      suscan_codec_class_register(&diff_class),
      return SU_FALSE);

  /* Pi/m-mPSK decoder */
  SU_TRYCATCH(
      pim_dpsk_class.config_desc = suscan_config_desc_new(),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_desc_add_field(
          pim_dpsk_class.config_desc,
          SUSCAN_FIELD_TYPE_BOOLEAN,
          SU_FALSE,
          "sign",
          "Invert difference sign"),
      return SU_FALSE);

  pim_dpsk_class.desc = "π/m-mPSK differential decoder";
  pim_dpsk_class.directions = SUSCAN_CODEC_DIRECTION_BACKWARDS;

  pim_dpsk_class.ctor    = suscan_codec_diff_ctor;
  pim_dpsk_class.process = suscan_codec_diff_process;
  pim_dpsk_class.dtor    = suscan_codec_diff_dtor;

  SU_TRYCATCH(
      suscan_codec_class_register(&pim_dpsk_class),
      return SU_FALSE);

  return SU_TRUE;
}
