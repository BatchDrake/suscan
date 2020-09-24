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

#define SU_LOG_DOMAIN "remote-analyzer"

#include "remote.h"
#include "msg.h"

#ifdef bool
#  undef bool
#endif /* bool */

SUSCAN_SERIALIZER_PROTO(suscan_analyzer_remote_call)
{
  SUSCAN_PACK_BOILERPLATE_START;

  SUSCAN_PACK(uint, self->type);

  switch (self->type) {
    case SUSCAN_ANALYZER_REMOTE_AUTH_INFO:
    case SUSCAN_ANALYZER_REMOTE_SOURCE_INFO:
      SU_TRYCATCH(
          suscan_analyzer_source_info_serialize(&self->source_info, buffer),
          goto fail);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_FREQUENCY:
      SUSCAN_PACK(freq, self->freq);
      SUSCAN_PACK(freq, self->lnb);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_GAIN:
      SUSCAN_PACK(str, self->gain.name);
      SUSCAN_PACK(float, self->gain.value);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_ANTENNA:
      SUSCAN_PACK(str, self->antenna);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_BANDWIDTH:
      SUSCAN_PACK(float, self->bandwidth);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_DC_REMOVE:
      SUSCAN_PACK(bool, self->dc_remove);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_IQ_REVERSE:
      SUSCAN_PACK(bool, self->iq_reverse);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_AGC:
      SUSCAN_PACK(bool, self->agc);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_FORCE_EOS:
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_SWEEP_STRATEGY:
      SUSCAN_PACK(uint, self->sweep_strategy);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_SPECTRUM_PARTITIONING:
      SUSCAN_PACK(uint, self->spectrum_partitioning);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_HOP_RANGE:
      SUSCAN_PACK(freq, self->hop_range.min);
      SUSCAN_PACK(freq, self->hop_range.max);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_BUFFERING_SIZE:
      SUSCAN_PACK(uint, self->buffering_size);
      break;

    case SUSCAN_ANALYZER_REMOTE_MESSAGE:
      SU_TRYCATCH(
          suscan_analyzer_msg_serialize(self->msg.type, self->msg.ptr, buffer),
          goto fail);
      break;

    case SUSCAN_ANALYZER_REMOTE_REQ_HALT:
      break;

    default:
      SU_ERROR("Invalid remote call `%d'\n", self->type);
      break;
  }

  SUSCAN_PACK_BOILERPLATE_END;
}

SUSCAN_DESERIALIZER_PROTO(suscan_analyzer_remote_call)
{
  SUSCAN_UNPACK_BOILERPLATE_START;

  SUSCAN_UNPACK(uint32, self->type);

  switch (self->type) {
    case SUSCAN_ANALYZER_REMOTE_AUTH_INFO:
    case SUSCAN_ANALYZER_REMOTE_SOURCE_INFO:
      SU_TRYCATCH(
          suscan_analyzer_source_info_serialize(&self->source_info, buffer),
          goto fail);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_FREQUENCY:
      SUSCAN_UNPACK(freq, self->freq);
      SUSCAN_UNPACK(freq, self->lnb);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_GAIN:
      SUSCAN_UNPACK(str,   self->gain.name);
      SUSCAN_UNPACK(float, self->gain.value);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_ANTENNA:
      SUSCAN_UNPACK(str, self->antenna);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_BANDWIDTH:
      SUSCAN_UNPACK(float, self->bandwidth);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_DC_REMOVE:
      SUSCAN_UNPACK(bool, self->dc_remove);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_IQ_REVERSE:
      SUSCAN_UNPACK(bool, self->iq_reverse);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_AGC:
      SUSCAN_UNPACK(bool, self->agc);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_FORCE_EOS:
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_SWEEP_STRATEGY:
      SUSCAN_UNPACK(uint32, self->sweep_strategy);
      SU_TRYCATCH(self->sweep_strategy < 2, goto fail);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_SPECTRUM_PARTITIONING:
      SUSCAN_UNPACK(uint32, self->spectrum_partitioning);
      SU_TRYCATCH(self->spectrum_partitioning < 2, goto fail);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_HOP_RANGE:
      SUSCAN_UNPACK(freq, self->hop_range.min);
      SUSCAN_UNPACK(freq, self->hop_range.max);

      SU_TRYCATCH(self->hop_range.min < self->hop_range.max, goto fail);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_BUFFERING_SIZE:
      SUSCAN_UNPACK(uint32, self->buffering_size);
      break;

    case SUSCAN_ANALYZER_REMOTE_MESSAGE:
      SU_TRYCATCH(
          suscan_analyzer_msg_deserialize(
              &self->msg.type,
              &self->msg.ptr, buffer),
          goto fail);
      break;

    case SUSCAN_ANALYZER_REMOTE_REQ_HALT:
      break;

    default:
      SU_ERROR("Invalid remote call `%d'\n", self->type);
      break;
  }

  SUSCAN_UNPACK_BOILERPLATE_END;
}

void
suscan_analyzer_remote_call_init(struct suscan_analyzer_remote_call *self)
{
  memset(self, 0, sizeof(struct suscan_analyzer_remote_call));
}

void
suscan_analyzer_remote_call_finalize(struct suscan_analyzer_remote_call *self)
{
  switch (self->type) {
    case SUSCAN_ANALYZER_REMOTE_SET_GAIN:
      if (self->gain.name != NULL)
        free(self->gain.name);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_ANTENNA:
      if (self->antenna != NULL)
        free(self->antenna);
      break;

    case SUSCAN_ANALYZER_REMOTE_SOURCE_INFO:
      suscan_analyzer_source_info_finalize(&self->source_info);
      break;

    case SUSCAN_ANALYZER_REMOTE_MESSAGE:
      suscan_analyzer_dispose_message(self->msg.type, self->msg.ptr);
      break;
  }
}
