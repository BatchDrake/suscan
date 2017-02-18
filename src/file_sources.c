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
#include <libgen.h>
#include <ctk.h>
#include "suscan.h"

SUPRIVATE su_block_t *
suscan_wav_source_ctor(const struct suscan_source_config *config)
{
  /* :( */
  return NULL;
}

SUBOOL
suscan_wav_source_init(void)
{
  struct suscan_source *source = NULL;

  if ((source = suscan_source_register(
      "WAV File",
      "WAV/PCM/AIFF sound file",
      suscan_wav_source_ctor)) == NULL)
    return SU_FALSE;

  if (!suscan_source_add_field(
      source,
      SUSCAN_FIELD_TYPE_FILE,
      SU_FALSE,
      "path",
      "File path"))
    return SU_FALSE;

  return SU_TRUE;
}

SUPRIVATE su_block_t *
suscan_iqfile_source_ctor(const struct suscan_source_config *config)
{
  /* :( */
  return NULL;
}

SUBOOL
suscan_iqfile_source_init(void)
{
  struct suscan_source *source = NULL;

  if ((source = suscan_source_register(
      "I/Q File",
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

  return SU_TRUE;
}

