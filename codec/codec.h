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

#ifndef _CODEC_CODEC_H
#define _CODEC_CODEC_H

#include <sigutils/sigutils.h>

#include <cfg.h>

/* Process error codes are detailed in suscan_codec_progress */

#define SUSCAN_PROCESS_CODE_NO_DATA  0 /* No data (yet) */
#define SUSCAN_PROCESS_CODE_ERROR   -1 /* Something went wrong */
#define SUSCAN_PROCESS_CODE_EOS     -2 /* End of stream */
#define SUSCAN_PROCESS_CODE_MIN     SUSCAN_PROCESS_CODE_EOS

#define SUSCAN_CODEC_DIRECTION_FORWARDS  1
#define SUSCAN_CODEC_DIRECTION_BACKWARDS 2
#define SUSCAN_CODEC_DIRECTION_BOTH \
  (SUSCAN_CODEC_DIRECTION_FORWARDS | SUSCAN_CODEC_DIRECTION_BACKWARDS)

#define SUSCAN_CODEC_PROGRESS_UNDEFINED -1

struct suscan_codec_progress {
  SUBOOL  updated;
  SUFLOAT progress;
  char *message;
};

struct suscan_codec;

struct suscan_codec_class {
  const char *desc;
  suscan_config_desc_t *config_desc;
  unsigned int directions;
  SUBOOL (*ctor) (
      void **private,
      struct suscan_codec *codec,
      unsigned int bits_per_symbol,
      const suscan_config_t *config,
      enum su_codec_direction direction);
  SUSDIFF (*process) (
      void *private,
      struct suscan_codec *codec,
      grow_buf_t *result, /* Out */
      struct suscan_codec_progress *progress, /* Out */
      const SUBITS *data,
      SUSCOUNT len);
  void (*dtor) (void *private);
};

struct suscan_codec {
  const struct suscan_codec_class *class;
  unsigned int bits_per_symbol;
  unsigned int output_bits_per_symbol;
  void *private;
};

typedef struct suscan_codec suscan_codec_t;

SUBOOL suscan_codec_class_register(const struct suscan_codec_class *class);

void suscan_codec_class_get_list(
    const struct suscan_codec_class ***list,
    unsigned int *count);

suscan_config_t *suscan_codec_class_make_config(
    const struct suscan_codec_class *class);

void suscan_codec_destroy(suscan_codec_t *codec);

unsigned int suscan_codec_get_input_bits_per_symbol(
    const suscan_codec_t *codec);

unsigned int suscan_codec_get_output_bits_per_symbol(
    const suscan_codec_t *codec);

suscan_codec_t *suscan_codec_class_make_codec(
    const struct suscan_codec_class *class,
    unsigned int bits_per_symbol,
    const suscan_config_t *config,
    enum su_codec_direction direction);

SUSDIFF suscan_codec_feed(
    suscan_codec_t *codec,
    grow_buf_t *result, /* Out */
    struct suscan_codec_progress *progress, /* Out */
    const SUBITS *data,
    SUSCOUNT len);

SUBOOL suscan_codec_class_register_builtin(void);

/* Builtin codecs */
SUBOOL suscan_codec_class_diff_register(void);

#endif /* _CODEC_CODEC_H */
