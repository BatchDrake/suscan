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

#define SU_LOG_DOMAIN "diff-decoder"

#include <sigutils/log.h>
#include "decoder.h"

SUPRIVATE su_codec_t *
suscan_decoder_diff_make_codec(unsigned int bits, suscan_config_t *config)
{
  struct suscan_field_value *value = NULL;

  /* This should always work */
  SU_TRYCATCH(
      value = suscan_config_get_value(config, "sign"),
      return NULL);

  return su_codec_new("diff", bits, value->as_bool);
}

SUBOOL
suscan_decoder_diff_register(void)
{
  struct suscan_decoder_desc *desc;

  SU_TRYCATCH(
      desc = suscan_decoder_register(
          "Generic differential codec",
          suscan_decoder_diff_make_codec),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_desc_add_field(
          desc->config_desc,
          SUSCAN_FIELD_TYPE_BOOLEAN,
          SU_FALSE,
          "sign",
          "Invert difference sign"),
      return SU_FALSE);

  return SU_TRUE;
}
