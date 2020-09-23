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
  SUSCOUNT i;

  SUSCAN_PACK(int, self->fc);
  SUSCAN_PACK(uint, self->inspector_id);
  SUSCAN_PACK(float, self->samp_rate);
  SUSCAN_PACK(float, self->N0);

  SU_TRYCATCH(cbor_pack_array_start(buffer, self->psd_size) == 0, goto fail);
  for (i = 0; i < self->psd_size; ++i)
    SUSCAN_PACK(float, self->psd_data[i]);

  SUSCAN_PACK_BOILERPLATE_END;
}

SUSCAN_DESERIALIZER_PROTO(suscan_analyzer_psd_msg)
{
  SUSCAN_UNPACK_BOILERPLATE_START;
  SUSCOUNT i;
  uint64_t new_size;
  SUFLOAT *tmp;
  SUBOOL end;

  SUSCAN_UNPACK(int64, self->fc);
  SUSCAN_UNPACK(uint32, self->inspector_id);
  SUSCAN_UNPACK(float, self->samp_rate);
  SUSCAN_UNPACK(float, self->N0);

  SU_TRYCATCH(cbor_unpack_array_start(buffer, &new_size, &end) == 0, goto fail);
  SU_TRYCATCH(!end, goto fail);
  if (self->psd_size != new_size) {
    SU_TRYCATCH(
        tmp = realloc(self->psd_data, new_size * sizeof(SUFLOAT)),
        goto fail);

    self->psd_data = tmp;
  }

  for (i = 0; i < self->psd_size; ++i)
    SUSCAN_UNPACK(float, self->psd_data[i]);

  UNPACK_BOILERPLATE_END;
}
