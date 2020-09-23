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
#include <util/cbor.h>

#define PACK_BOILERPLATE_START                  \
  SUBOOL ok = SU_FALSE;

#define PACK_BOILERPLATE_END                    \
  ok = SU_TRUE;                                 \
                                                \
fail:                                           \
  return ok

#define UNPACK_BOILERPLATE_START                \
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

#define PACK(t, v)                              \
    SU_TRYCATCH(                                \
        JOIN(cbor_pack_, t)(buffer, v) == 0,    \
        goto fail)

#define UNPACK(t, v)                            \
    SU_TRYCATCH(                                \
        JOIN(cbor_unpack_, t)(buffer, &v) == 0, \
        goto fail)

/* suscan_analyzer_status_msg */
SUSCAN_SERIALIZER_PROTO(suscan_analyzer_status_msg)
{
  PACK_BOILERPLATE_START;

  PACK(int, self->code);
  PACK(str, self->err_msg);

  PACK_BOILERPLATE_END;
}

SUSCAN_DESERIALIZER_PROTO(suscan_analyzer_status_msg)
{
  UNPACK_BOILERPLATE_START;

  UNPACK(int32, self->code);
  UNPACK(str, self->err_msg);

  UNPACK_BOILERPLATE_END;
}

/* suscan_analyzer_throttle_msg */
SUSCAN_SERIALIZER_PROTO(suscan_analyzer_throttle_msg)
{
  PACK_BOILERPLATE_START;

  PACK(uint, self->samp_rate);

  PACK_BOILERPLATE_END;
}

SUSCAN_DESERIALIZER_PROTO(suscan_analyzer_throttle_msg)
{
  UNPACK_BOILERPLATE_START;

  UNPACK(uint64, self->samp_rate);

  UNPACK_BOILERPLATE_END;
}

/* suscan_analyzer_psd_msg */
SUSCAN_SERIALIZER_PROTO(suscan_analyzer_psd_msg)
{
  PACK_BOILERPLATE_START;
  SUSCOUNT i;

  PACK(int, self->fc);
  PACK(uint, self->inspector_id);
  PACK(float, self->samp_rate);
  PACK(float, self->N0);

  SU_TRYCATCH(cbor_pack_array_start(buffer, self->psd_size) == 0, goto fail);
  for (i = 0; i < self->psd_size; ++i)
    PACK(float, self->psd_data[i]);

  PACK_BOILERPLATE_END;
}

SUSCAN_DESERIALIZER_PROTO(suscan_analyzer_psd_msg)
{
  UNPACK_BOILERPLATE_START;
  SUSCOUNT i;
  uint64_t new_size;
  SUFLOAT *tmp;
  SUBOOL end;

  UNPACK(int64, self->fc);
  UNPACK(uint32, self->inspector_id);
  UNPACK(float, self->samp_rate);
  UNPACK(float, self->N0);

  SU_TRYCATCH(cbor_unpack_array_start(buffer, &new_size, &end) == 0, goto fail);
  SU_TRYCATCH(!end, goto fail);
  if (self->psd_size != new_size) {
    SU_TRYCATCH(
        tmp = realloc(self->psd_data, new_size * sizeof(SUFLOAT)),
        goto fail);

    self->psd_data = tmp;
  }

  for (i = 0; i < self->psd_size; ++i)
    UNPACK(float, self->psd_data[i]);

  UNPACK_BOILERPLATE_END;
}
