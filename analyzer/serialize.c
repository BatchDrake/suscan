/*

  Copyright (C) 2020 Gonzalo José Carracedo Carballal

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation, version 3.

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

#define SU_LOG_DOMAIN "serialize"

#include "mq.h"
#include "msg.h"
#include "serialize.h"

/* Helper functions */
void
suscan_single_array_cpu_to_be(
    SUSINGLE *array,
    const SUSINGLE *orig,
    SUSCOUNT size)
{
  SUSCOUNT i;
  uint32_t *idest = (uint32_t *) array;
  const uint32_t *iorig = (const uint32_t *) orig;

  for (i = 0; i < size; ++i)
    idest[i] = cpu32_to_be(iorig[i]);
}

void
suscan_single_array_be_to_cpu(
    SUSINGLE *array,
    const SUSINGLE *orig,
    SUSCOUNT size)
{
  SUSCOUNT i;
  uint32_t *idest = (uint32_t *) array;
  const uint32_t *iorig = (const uint32_t *) orig;

  for (i = 0; i < size; ++i)
    idest[i] = be32_to_cpu_unaligned(iorig + i);
}

void
suscan_double_array_cpu_to_be(
    SUDOUBLE *array,
    const SUDOUBLE *orig,
    SUSCOUNT size)
{
  SUSCOUNT i;
  uint64_t *idest = (uint64_t *) array;
  const uint64_t *iorig = (const uint64_t *) orig;

  for (i = 0; i < size; ++i)
    idest[i] = cpu64_to_be(iorig[i]);
}

void
suscan_double_array_be_to_cpu(
    SUDOUBLE *array,
    const SUDOUBLE *orig,
    SUSCOUNT size)
{
  SUSCOUNT i;
  uint64_t *idest = (uint64_t *) array;
  const uint64_t *iorig = (const uint64_t *) orig;

  for (i = 0; i < size; ++i)
    idest[i] = be64_to_cpu_unaligned(iorig + i);
}

SUBOOL
suscan_pack_compact_single_array(
    grow_buf_t *buffer,
    const SUSINGLE *array,
    SUSCOUNT size)
{
  SUSCOUNT array_size = size * sizeof(SUSINGLE);
  SUSINGLE *dest;
  SUBOOL ok = SU_FALSE;

  SUSCAN_PACK(uint, size);

  SU_TRYCATCH(dest = cbor_alloc_blob(buffer, array_size), goto fail);

  suscan_single_array_cpu_to_be(dest, array, size);

  ok = SU_TRUE;

fail:
  return ok;
}


SUBOOL
suscan_pack_compact_double_array(
    grow_buf_t *buffer,
    const SUDOUBLE *array,
    SUSCOUNT size)
{
  SUSCOUNT array_size = size * sizeof(SUDOUBLE);
  SUDOUBLE *dest;
  SUBOOL ok = SU_FALSE;

  SUSCAN_PACK(uint, size);

  SU_TRYCATCH(dest = cbor_alloc_blob(buffer, array_size), goto fail);

  suscan_double_array_cpu_to_be(dest, array, size);

  ok = SU_TRUE;

fail:
  return ok;
}

SUBOOL
suscan_pack_compact_complex_array(
    grow_buf_t *buffer,
    const SUCOMPLEX *array,
    SUSCOUNT size)
{
  return suscan_pack_compact_float_array(
      buffer,
      (const SUFLOAT *) array,
      size << 1);
}

SUBOOL
suscan_unpack_compact_single_array(
    grow_buf_t *buffer,
    SUSINGLE **oarray,
    SUSCOUNT *osize)
{
  SUSINGLE *array = *oarray;
  SUSCOUNT array_length;
  SUSCOUNT array_size = *osize * sizeof(SUSINGLE);
  SUBOOL ok = SU_FALSE;

  SUSCAN_UNPACK(uint64, array_length);

  SU_TRYCATCH(
      cbor_unpack_blob(buffer, (void **) &array, &array_size) == 0,
      goto fail);
  SU_TRYCATCH(array_size == array_length * sizeof(SUSINGLE), goto fail);

  suscan_single_array_be_to_cpu(array, array, array_length);

  *oarray = array;
  *osize  = array_length;

  array = NULL;

  ok = SU_TRUE;

fail:
  if (array != NULL)
    free(array);

  return ok;
}

SUBOOL
suscan_unpack_compact_double_array(
    grow_buf_t *buffer,
    SUDOUBLE **oarray,
    SUSCOUNT *osize)
{
  SUDOUBLE *array = *oarray;
  SUSCOUNT array_size = *osize * sizeof(SUDOUBLE);
  SUSCOUNT array_length;
  SUBOOL ok = SU_FALSE;

  SUSCAN_UNPACK(uint64, array_length);

  SU_TRYCATCH(
      cbor_unpack_blob(buffer, (void **) &array, &array_size) == 0,
      goto fail);

  SU_TRYCATCH(array_size == array_length * sizeof(SUDOUBLE), goto fail);

  suscan_double_array_be_to_cpu(array, array, array_length);

  *oarray = array;
  *osize  = array_length;

  array = NULL;

  ok = SU_TRUE;

fail:
  if (array != NULL)
    free(array);

  return ok;
}

SUBOOL
suscan_unpack_compact_complex_array(
    grow_buf_t *buffer,
    SUCOMPLEX **array,
    SUSCOUNT *size)
{
  int ret;
  SUSCOUNT fake_size = *size << 1;

  if (!suscan_unpack_compact_float_array(
      buffer,
      (SUFLOAT **) array,
      &fake_size)) {
    SU_ERROR("Failed to unpack float components of complex array\n");
    return SU_FALSE;
  }

  /*
   * Size must be an even number. If it is not the case, something
   * went very wrong.
   */
  if (fake_size & 1) {
    free(*array);
    *array = NULL;
    *size  = 0;

    SU_ERROR(
        "Complex array: asked for %d floats, but %d received?\n",
        *size << 1,
        fake_size);

    return SU_FALSE;
  }

  *size = fake_size >> 1;

  return SU_TRUE;
}

