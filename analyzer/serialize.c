/*

  Copyright (C) 2020 Gonzalo José Carracedo Carballal

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

  if ((ret = suscan_unpack_compact_float_array(
      buffer,
      (SUFLOAT **) array,
      &fake_size)) != 0)
    return SU_FALSE;

  /*
   * Size must be an even number. If it is not the case, something
   * went very wrong.
   */
  if (fake_size & 1) {
    free(*array);
    *array = NULL;
    *size  = 0;

    return SU_FALSE;
  }

  *size = fake_size >> 1;

  return SU_TRUE;
}

/* suscan_analyzer_status_msg */
SUSCAN_SERIALIZER_PROTO(suscan_analyzer_status_msg)
{
  SUSCAN_PACK_BOILERPLATE_START;

  SUSCAN_PACK(int, self->code);
  SUSCAN_PACK(str, self->err_msg);

  SUSCAN_PACK_BOILERPLATE_END;
}

SUSCAN_DESERIALIZER_PROTO(suscan_analyzer_status_msg)
{
  SUSCAN_UNPACK_BOILERPLATE_START;

  SUSCAN_UNPACK(int32, self->code);
  SUSCAN_UNPACK(str, self->err_msg);

  UNPACK_BOILERPLATE_END;
}

/* suscan_analyzer_throttle_msg */
SUSCAN_SERIALIZER_PROTO(suscan_analyzer_throttle_msg)
{
  SUSCAN_PACK_BOILERPLATE_START;

  SUSCAN_PACK(uint, self->samp_rate);

  SUSCAN_PACK_BOILERPLATE_END;
}

SUSCAN_DESERIALIZER_PROTO(suscan_analyzer_throttle_msg)
{
  SUSCAN_UNPACK_BOILERPLATE_START;

  SUSCAN_UNPACK(uint64, self->samp_rate);

  UNPACK_BOILERPLATE_END;
}

/* suscan_analyzer_psd_msg */
SUSCAN_SERIALIZER_PROTO(suscan_analyzer_psd_msg)
{
  SUSCAN_PACK_BOILERPLATE_START;

  SUSCAN_PACK(int,   self->fc);
  SUSCAN_PACK(uint,  self->inspector_id);
  SUSCAN_PACK(float, self->samp_rate);
  SUSCAN_PACK(float, self->N0);

  SU_TRYCATCH(
      suscan_pack_compact_single_array(
          buffer,
          self->psd_data,
          self->psd_size),
      goto fail);

  SUSCAN_PACK_BOILERPLATE_END;
}

SUSCAN_DESERIALIZER_PROTO(suscan_analyzer_psd_msg)
{
  SUSCAN_UNPACK_BOILERPLATE_START;

  SUSCAN_UNPACK(int64,  self->fc);
  SUSCAN_UNPACK(uint32, self->inspector_id);
  SUSCAN_UNPACK(float,  self->samp_rate);
  SUSCAN_UNPACK(float,  self->N0);

  SU_TRYCATCH(
      suscan_unpack_compact_single_array(
          buffer,
          &self->psd_data,
          &self->psd_size),
      goto fail);

  UNPACK_BOILERPLATE_END;
}

/* Channel sample batch */
SUSCAN_SERIALIZER_PROTO(suscan_analyzer_sample_batch_msg)
{
  SUSCAN_PACK_BOILERPLATE_START;

  SUSCAN_PACK(int, self->inspector_id);
  SU_TRYCATCH(
      suscan_pack_compact_complex_array(
          buffer,
          self->samples,
          self->sample_count),
      goto fail);

  SUSCAN_PACK_BOILERPLATE_END;
}

SUSCAN_DESERIALIZER_PROTO(suscan_analyzer_sample_batch_msg)
{
  SUSCAN_UNPACK_BOILERPLATE_START;

  SUSCAN_UNPACK(uint32, self->inspector_id);
  SU_TRYCATCH(
      suscan_unpack_compact_complex_array(
          buffer,
          &self->samples,
          &self->sample_count),
      goto fail);

  UNPACK_BOILERPLATE_END;
}
