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

#ifndef _SUSCAN_SERIALIZE_H
#define _SUSCAN_SERIALIZE_H

#include <util/cbor.h>

#define SUSCAN_SERIALIZER_PROTO(structname)     \
SUBOOL                                          \
JOIN(structname, _serialize)(                   \
    const struct structname *self,              \
    grow_buf_t *buffer)                         \

#define SUSCAN_DESERIALIZER_PROTO(structname)   \
SUBOOL                                          \
JOIN(structname, _deserialize)(                 \
    struct structname *self,                    \
    grow_buf_t *buffer)                         \

#define SUSCAN_SERIALIZABLE(structname)         \
    struct structname;                          \
    SUSCAN_SERIALIZER_PROTO(structname);        \
    SUSCAN_DESERIALIZER_PROTO(structname);      \
    struct structname

#define SUSCAN_PACK_BOILERPLATE_START           \
  SUBOOL ok = SU_FALSE;

#define SUSCAN_PACK_BOILERPLATE_END             \
  ok = SU_TRUE;                                 \
                                                \
fail:                                           \
  return ok

#define SUSCAN_UNPACK_BOILERPLATE_START         \
  size_t ptr = grow_buf_ptr(buffer);            \
  SUBOOL ok = SU_FALSE;

#define UNPACK_BOILERPLATE_END                  \
    ok = SU_TRUE;                               \
                                                \
fail:                                           \
  if (!ok)                                      \
    grow_buf_seek(buffer, ptr, SEEK_SET);       \
                                                \
  return ok;

#define SUSCAN_PACK(t, v)                       \
    SU_TRYCATCH(                                \
        JOIN(cbor_pack_, t)(buffer, v) == 0,    \
        goto fail)

#define SUSCAN_UNPACK(t, v)                     \
    SU_TRYCATCH(                                \
        JOIN(cbor_unpack_, t)(buffer, &v) == 0, \
        goto fail)

#endif /* _SUSCAN_SERIALIZE_H */
