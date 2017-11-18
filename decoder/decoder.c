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
#include "decoder.h"

PTR_LIST_CONST(struct suscan_decoder_desc, desc);

struct suscan_decoder_desc *
suscan_decoder_register(
    const char *desc,
    su_codec_t *(*ctor) (unsigned int, suscan_config_t *))
{
  struct suscan_decoder_desc *new = NULL;
  suscan_config_desc_t *config_desc = NULL;
  char *desc_dup = NULL;

  SU_TRYCATCH(desc_dup = strdup(desc), goto fail);

  SU_TRYCATCH(new = malloc(sizeof(struct suscan_decoder_desc)), goto fail);

  SU_TRYCATCH(config_desc = suscan_config_desc_new(), goto fail);

  /* Common parameters may be added to config_desc here */

  new->desc = desc_dup;
  new->config_desc = config_desc;
  new->ctor = ctor;

  SU_TRYCATCH(PTR_LIST_APPEND_CHECK(desc, new) != -1, goto fail);

  return new;

fail:
  if (desc_dup != NULL)
    free(desc_dup);

  if (new != NULL)
    free(new);

  if (config_desc != NULL)
    suscan_config_desc_destroy(config_desc);

  return NULL;
}

void
suscan_decoder_desc_get_list(
    struct suscan_decoder_desc *const **list,
    unsigned int *count)
{
  *list = desc_list;
  *count = desc_count;
}

suscan_config_t *
suscan_decoder_make_config(const struct suscan_decoder_desc *desc)
{
  return suscan_config_new(desc->config_desc);
}

su_codec_t *
suscan_decoder_make_codec(
    const struct suscan_decoder_desc *desc,
    unsigned int bits,
    suscan_config_t *config)
{
  return (desc->ctor) (bits, config);
}

SUBOOL
suscan_decoder_register_builtin(void)
{
  SU_TRYCATCH(suscan_decoder_diff_register(), return SU_FALSE);

  return SU_TRUE;
}
