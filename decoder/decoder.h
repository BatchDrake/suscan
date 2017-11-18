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

#ifndef _DECODER_DECODER_H
#define _DECODER_DECODER_H

#include <sigutils/sigutils.h>

#include <cfg.h>

struct suscan_decoder_desc {
  const char *desc;
  suscan_config_desc_t *config_desc;
  su_encoder_t *(*ctor) (unsigned int bits, suscan_config_t *);
};

struct suscan_decoder_desc *suscan_decoder_register(
    const char *desc,
    su_encoder_t *(*ctor) (unsigned int, suscan_config_t *));

void suscan_decoder_desc_get_list(
    struct suscan_decoder_desc *const **list,
    unsigned int *count);

suscan_config_t *suscan_decoder_make_config(
    const struct suscan_decoder_desc *desc);

su_encoder_t *suscan_decoder_make_encoder(
    const struct suscan_decoder_desc *desc,
    unsigned int bits,
    suscan_config_t *config);

SUBOOL suscan_decoder_register_builtin(void);

/* Builtin decoders */
SUBOOL suscan_decoder_diff_register(void);

#endif /* _DECODER_DECODER_H */
