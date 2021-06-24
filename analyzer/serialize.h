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

#ifndef _SUSCAN_SERIALIZE_H
#define _SUSCAN_SERIALIZE_H

#include <util/cbor.h>

#ifdef _SU_SINGLE_PRECISION
#  define suscan_pack_compact_float_array   suscan_pack_compact_single_array
#  define suscan_unpack_compact_float_array suscan_unpack_compact_single_array
#else
#  define suscan_pack_compact_float_array   suscan_pack_compact_double_array
#  define suscan_unpack_compact_float_array suscan_unpack_compact_double_array
#endif /* _SU_SINGLE_PRECISION */

#define SUSCAN_SERIALIZER_PROTO(structname)            \
SUBOOL                                                 \
JOIN(structname, _serialize)(                          \
    const struct structname *self,                     \
    grow_buf_t *buffer)                                \

#define SUSCAN_DESERIALIZER_PROTO(structname)          \
SUBOOL                                                 \
JOIN(structname, _deserialize)(                        \
    struct structname *self,                           \
    grow_buf_t *buffer)                                \

#define SUSCAN_SERIALIZABLE(structname)                \
    struct structname;                                 \
    SUSCAN_SERIALIZER_PROTO(structname);               \
    SUSCAN_DESERIALIZER_PROTO(structname);             \
    struct structname

#define SUSCAN_PACK_BOILERPLATE_START                  \
  SUBOOL ok = SU_FALSE;

#define SUSCAN_PACK_BOILERPLATE_END                    \
  ok = SU_TRUE;                                        \
                                                       \
fail:                                                  \
  return ok

#define SUSCAN_UNPACK_BOILERPLATE_START                \
  size_t _ptr = grow_buf_ptr(buffer);                  \
  SUBOOL ok = SU_FALSE;

#define SUSCAN_UNPACK_BOILERPLATE_FINALLY              \
    ok = SU_TRUE;                                      \
                                                       \
fail:                                                  \
  if (!ok)                                             \
    grow_buf_seek(buffer, _ptr, SEEK_SET);             \
                                                       \

#define SUSCAN_UNPACK_BOILERPLATE_RETURN               \
  return ok;

#define SUSCAN_UNPACK_BOILERPLATE_END                  \
    SUSCAN_UNPACK_BOILERPLATE_FINALLY                  \
    SUSCAN_UNPACK_BOILERPLATE_RETURN                   \

#define SUSCAN_PACK(t, v, arg...)                      \
    SU_TRYCATCH(                                       \
        JOIN(cbor_pack_, t)(buffer, v, ##arg) == 0,    \
        goto fail)

#define SUSCAN_UNPACK(t, v, arg...)                    \
    SU_TRYCATCH(                                       \
        JOIN(cbor_unpack_, t)(buffer, &v, ##arg) == 0, \
        goto fail)

void suscan_single_array_cpu_to_be(
    SUSINGLE *array,
    const SUSINGLE *orig,
    SUSCOUNT size);

void suscan_single_array_be_to_cpu(
    SUSINGLE *array,
    const SUSINGLE *orig,
    SUSCOUNT size);

void suscan_double_array_cpu_to_be(
    SUDOUBLE *array,
    const SUDOUBLE *orig,
    SUSCOUNT size);

void suscan_double_array_be_to_cpu(
    SUDOUBLE *array,
    const SUDOUBLE *orig,
    SUSCOUNT size);

SUBOOL suscan_pack_compact_single_array(
    grow_buf_t *buffer,
    const SUSINGLE *array,
    SUSCOUNT size);

SUBOOL suscan_pack_compact_double_array(
    grow_buf_t *buffer,
    const SUDOUBLE *array,
    SUSCOUNT size);

SUBOOL suscan_pack_compact_complex_array(
    grow_buf_t *buffer,
    const SUCOMPLEX *array,
    SUSCOUNT size);

SUBOOL
suscan_unpack_compact_single_array(
    grow_buf_t *buffer,
    SUSINGLE **oarray,
    SUSCOUNT *osize);

SUBOOL
suscan_unpack_compact_double_array(
    grow_buf_t *buffer,
    SUDOUBLE **oarray,
    SUSCOUNT *osize);

SUBOOL suscan_unpack_compact_complex_array(
    grow_buf_t *buffer,
    SUCOMPLEX **array,
    SUSCOUNT *size);

#endif /* _SUSCAN_SERIALIZE_H */
