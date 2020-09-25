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

#include <sys/socket.h>

#include "remote.h"
#include "msg.h"

#ifdef bool
#  undef bool
#endif /* bool */

SUPRIVATE struct suscan_analyzer_interface *g_remote_analyzer_interface;

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
suscan_analyzer_remote_call_init(
    struct suscan_analyzer_remote_call *self,
    enum suscan_analyzer_remote_type type)
{
  memset(self, 0, sizeof(struct suscan_analyzer_remote_call));

  self->type = type;
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

/***************************** Call queueing **********************************/
struct suscan_analyzer_remote_call *
suscan_remote_analyzer_acquire_call(
    suscan_remote_analyzer_t *self,
    enum suscan_analyzer_remote_type type)
{
  SU_TRYCATCH(pthread_mutex_lock(&self->call_mutex) == 0, return NULL);

  suscan_analyzer_remote_call_init(&self->call, type);

  return &self->call;
}

SUBOOL
suscan_remote_analyzer_release_call(
    suscan_remote_analyzer_t *self,
    struct suscan_analyzer_remote_call *call)
{
  SU_TRYCATCH(call == &self->call, return SU_FALSE);

  suscan_analyzer_remote_call_finalize(&self->call);

  SU_TRYCATCH(pthread_mutex_unlock(&self->call_mutex) == 0, return SU_FALSE);

  return SU_TRUE;
}


SUBOOL
suscan_remote_analyzer_queue_call(
    suscan_remote_analyzer_t *self,
    struct suscan_analyzer_remote_call *call,
    SUBOOL is_control)
{
  grow_buf_t *buf = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(buf = calloc(1, sizeof(grow_buf_t)), goto done);
  SU_TRYCATCH(
      suscan_analyzer_remote_call_serialize(&self->call, buf),
      goto done);

  SU_TRYCATCH(suscan_mq_write(&self->pdu_queue, is_control, buf), goto done);

  ok = SU_TRUE;

done:
  if (!ok) {
    grow_buf_finalize(buf);
    free(buf);
  }
  return ok;
}

/*************************** Analyzer interface *******************************/
SUPRIVATE void suscan_remote_analyzer_dtor(void *ptr);

SUPRIVATE void
suscan_remote_analyzer_consume_pdu_queue(suscan_remote_analyzer_t *self)
{
  grow_buf_t *buffer;
  uint32_t type;

  while (suscan_mq_poll(&self->pdu_queue, &type, (void **) &buffer)) {
    grow_buf_finalize(buffer);
    free(buffer);
  }
}

void *
suscan_remote_analyzer_ctor(suscan_analyzer_t *parent, va_list ap)
{
  suscan_remote_analyzer_t *new = NULL;

  SU_TRYCATCH(new = calloc(1, sizeof(suscan_remote_analyzer_t)), goto fail);

  new->control_fd = -1;
  new->data_fd = -1;

  SU_TRYCATCH(pthread_mutex_init(&new->call_mutex, NULL) == 0, goto fail);
  new->call_mutex_initialized = SU_TRUE;

  return new;

fail:
  if (new != NULL)
    suscan_remote_analyzer_dtor(new);

  return NULL;
}

SUPRIVATE void
suscan_remote_analyzer_dtor(void *ptr)
{
  suscan_remote_analyzer_t *self = (suscan_remote_analyzer_t *) ptr;

  if (self->control_fd != -1)
    shutdown(self->control_fd, 2);

  if (self->data_fd != -1)
    shutdown(self->data_fd, 2);

  if (self->call_mutex_initialized)
    pthread_mutex_destroy(&self->call_mutex);

  suscan_remote_analyzer_consume_pdu_queue(self);

  free(self);
}

/* Source-related methods */
SUPRIVATE SUBOOL
suscan_remote_analyzer_set_frequency(void *ptr, SUFREQ freq, SUFREQ lnb)
{
  suscan_remote_analyzer_t *self = (suscan_remote_analyzer_t *) ptr;
  struct suscan_analyzer_remote_call *call = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      call = suscan_remote_analyzer_acquire_call(
          self,
          SUSCAN_ANALYZER_REMOTE_SET_FREQUENCY),
      goto done);

  call->freq = freq;
  call->lnb  = lnb;

  SU_TRYCATCH(
      suscan_remote_analyzer_queue_call(self, call, SU_TRUE),
      goto done);

  ok = SU_TRUE;

done:
  if (call != NULL)
    suscan_remote_analyzer_release_call(self, call);

  return ok;
}

SUPRIVATE SUBOOL
suscan_remote_analyzer_set_gain(void *ptr, const char *name, SUFLOAT value)
{
  suscan_remote_analyzer_t *self = (suscan_remote_analyzer_t *) ptr;
  struct suscan_analyzer_remote_call *call = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      call = suscan_remote_analyzer_acquire_call(
          self,
          SUSCAN_ANALYZER_REMOTE_SET_GAIN),
      goto done);

  /* TODO: Implement me */

  SU_TRYCATCH(
      suscan_remote_analyzer_queue_call(self, call, SU_TRUE),
      goto done);

  ok = SU_TRUE;

done:
  if (call != NULL)
    suscan_remote_analyzer_release_call(self, call);

  return ok;
}

SUPRIVATE SUBOOL
suscan_remote_analyzer_set_antenna(void *ptr, const char *name)
{
  suscan_remote_analyzer_t *self = (suscan_remote_analyzer_t *) ptr;
  struct suscan_analyzer_remote_call *call = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      call = suscan_remote_analyzer_acquire_call(
          self,
          SUSCAN_ANALYZER_REMOTE_SET_GAIN),
      goto done);

  /* TODO: Implement me */

  SU_TRYCATCH(
      suscan_remote_analyzer_queue_call(self, call, SU_TRUE),
      goto done);

  ok = SU_TRUE;

done:
  if (call != NULL)
    suscan_remote_analyzer_release_call(self, call);

  return ok;
}

SUPRIVATE SUBOOL
suscan_remote_analyzer_set_bandwidth(void *ptr, SUFLOAT value)
{
  suscan_remote_analyzer_t *self = (suscan_remote_analyzer_t *) ptr;
  struct suscan_analyzer_remote_call *call = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      call = suscan_remote_analyzer_acquire_call(
          self,
          SUSCAN_ANALYZER_REMOTE_SET_GAIN),
      goto done);

  /* TODO: Implement me */

  SU_TRYCATCH(
      suscan_remote_analyzer_queue_call(self, call, SU_TRUE),
      goto done);

  ok = SU_TRUE;

done:
  if (call != NULL)
    suscan_remote_analyzer_release_call(self, call);

  return ok;
}

SUPRIVATE SUBOOL
suscan_remote_analyzer_set_dc_remove(void *ptr, SUBOOL value)
{
  suscan_remote_analyzer_t *self = (suscan_remote_analyzer_t *) ptr;
  struct suscan_analyzer_remote_call *call = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      call = suscan_remote_analyzer_acquire_call(
          self,
          SUSCAN_ANALYZER_REMOTE_SET_GAIN),
      goto done);

  /* TODO: Implement me */

  SU_TRYCATCH(
      suscan_remote_analyzer_queue_call(self, call, SU_TRUE),
      goto done);

  ok = SU_TRUE;

done:
  if (call != NULL)
    suscan_remote_analyzer_release_call(self, call);

  return ok;
}

SUPRIVATE SUBOOL
suscan_remote_analyzer_set_iq_reverse(void *ptr, SUBOOL value)
{
  suscan_remote_analyzer_t *self = (suscan_remote_analyzer_t *) ptr;
  struct suscan_analyzer_remote_call *call = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      call = suscan_remote_analyzer_acquire_call(
          self,
          SUSCAN_ANALYZER_REMOTE_SET_GAIN),
      goto done);

  /* TODO: Implement me */

  SU_TRYCATCH(
      suscan_remote_analyzer_queue_call(self, call, SU_TRUE),
      goto done);

  ok = SU_TRUE;

done:
  if (call != NULL)
    suscan_remote_analyzer_release_call(self, call);

  return ok;

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_remote_analyzer_set_agc(void *ptr, SUBOOL value)
{
  suscan_remote_analyzer_t *self = (suscan_remote_analyzer_t *) ptr;
  struct suscan_analyzer_remote_call *call = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      call = suscan_remote_analyzer_acquire_call(
          self,
          SUSCAN_ANALYZER_REMOTE_SET_GAIN),
      goto done);

  /* TODO: Implement me */

  SU_TRYCATCH(
      suscan_remote_analyzer_queue_call(self, call, SU_TRUE),
      goto done);

  ok = SU_TRUE;

done:
  if (call != NULL)
    suscan_remote_analyzer_release_call(self, call);

  return ok;
}

SUPRIVATE SUBOOL
suscan_remote_analyzer_force_eos(void *ptr)
{
  suscan_remote_analyzer_t *self = (suscan_remote_analyzer_t *) ptr;
  struct suscan_analyzer_remote_call *call = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      call = suscan_remote_analyzer_acquire_call(
          self,
          SUSCAN_ANALYZER_REMOTE_SET_GAIN),
      goto done);

  /* TODO: Implement me */

  SU_TRYCATCH(
      suscan_remote_analyzer_queue_call(self, call, SU_TRUE),
      goto done);

  ok = SU_TRUE;

done:
  if (call != NULL)
    suscan_remote_analyzer_release_call(self, call);

  return ok;
}

SUPRIVATE SUBOOL
suscan_remote_analyzer_is_real_time(const void *ptr)
{
  return SU_TRUE;
}

SUPRIVATE unsigned int
suscan_remote_analyzer_get_samp_rate(const void *ptr)
{
  suscan_remote_analyzer_t *self = (suscan_remote_analyzer_t *) ptr;
  struct suscan_analyzer_remote_call *call = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      call = suscan_remote_analyzer_acquire_call(
          self,
          SUSCAN_ANALYZER_REMOTE_SET_GAIN),
      goto done);

  /* TODO: Implement me */

  SU_TRYCATCH(
      suscan_remote_analyzer_queue_call(self, call, SU_TRUE),
      goto done);

  ok = SU_TRUE;

done:
  if (call != NULL)
    suscan_remote_analyzer_release_call(self, call);

  return ok;
}

SUPRIVATE SUFLOAT
suscan_remote_analyzer_get_measured_samp_rate(const void *ptr)
{
  const suscan_remote_analyzer_t *self = (const suscan_remote_analyzer_t *) ptr;

  return self->measured_samp_rate;
}

SUPRIVATE struct suscan_analyzer_source_info *
suscan_remote_analyzer_get_source_info_pointer(const void *ptr)
{
  suscan_remote_analyzer_t *self = (suscan_remote_analyzer_t *) ptr;

  return &self->source_info;
}

SUPRIVATE SUBOOL
suscan_remote_analyzer_commit_source_info(void *ptr)
{
  return SU_TRUE;
}

/* Worker specific methods */
SUPRIVATE SUBOOL
suscan_remote_analyzer_set_sweep_strategy(
    void *ptr,
    enum suscan_analyzer_sweep_strategy strategy)
{
  suscan_remote_analyzer_t *self = (suscan_remote_analyzer_t *) ptr;
  struct suscan_analyzer_remote_call *call = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      call = suscan_remote_analyzer_acquire_call(
          self,
          SUSCAN_ANALYZER_REMOTE_SET_SWEEP_STRATEGY),
      goto done);

  /* TODO: Implement me */

  SU_TRYCATCH(
      suscan_remote_analyzer_queue_call(self, call, SU_TRUE),
      goto done);

  ok = SU_TRUE;

done:
  if (call != NULL)
    suscan_remote_analyzer_release_call(self, call);

  return ok;
}

SUPRIVATE SUBOOL
suscan_remote_analyzer_set_spectrum_partitioning(
    void *ptr,
    enum suscan_analyzer_spectrum_partitioning partitioning)
{
  suscan_remote_analyzer_t *self = (suscan_remote_analyzer_t *) ptr;
  struct suscan_analyzer_remote_call *call = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      call = suscan_remote_analyzer_acquire_call(
          self,
          SUSCAN_ANALYZER_REMOTE_SET_SPECTRUM_PARTITIONING),
      goto done);

  /* TODO: Implement me */

  SU_TRYCATCH(
      suscan_remote_analyzer_queue_call(self, call, SU_TRUE),
      goto done);

  ok = SU_TRUE;

done:
  if (call != NULL)
    suscan_remote_analyzer_release_call(self, call);

  return ok;
}

SUPRIVATE SUBOOL
suscan_remote_analyzer_set_hop_range(void *ptr, SUFREQ min, SUFREQ max)
{
  suscan_remote_analyzer_t *self = (suscan_remote_analyzer_t *) ptr;
  struct suscan_analyzer_remote_call *call = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      call = suscan_remote_analyzer_acquire_call(
          self,
          SUSCAN_ANALYZER_REMOTE_SET_HOP_RANGE),
      goto done);

  /* TODO: Implement me */

  SU_TRYCATCH(
      suscan_remote_analyzer_queue_call(self, call, SU_TRUE),
      goto done);

  ok = SU_TRUE;

done:
  if (call != NULL)
    suscan_remote_analyzer_release_call(self, call);

  return ok;
}

SUPRIVATE SUBOOL
suscan_remote_analyzer_set_buffering_size(
    void *ptr,
    SUSCOUNT size)
{
  suscan_remote_analyzer_t *self = (suscan_remote_analyzer_t *) ptr;
  struct suscan_analyzer_remote_call *call = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      call = suscan_remote_analyzer_acquire_call(
          self,
          SUSCAN_ANALYZER_REMOTE_SET_BUFFERING_SIZE),
      goto done);

  /* TODO: Implement me */

  SU_TRYCATCH(
      suscan_remote_analyzer_queue_call(self, call, SU_TRUE),
      goto done);

  ok = SU_TRUE;

done:
  if (call != NULL)
    suscan_remote_analyzer_release_call(self, call);

  return ok;
}

SUPRIVATE SUBOOL
suscan_remote_analyzer_write(void *ptr, uint32_t type, void *priv)
{
  suscan_remote_analyzer_t *self = (suscan_remote_analyzer_t *) ptr;
  struct suscan_analyzer_remote_call *call = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      call = suscan_remote_analyzer_acquire_call(
          self,
          SUSCAN_ANALYZER_REMOTE_MESSAGE),
      goto done);

  /* TODO: Implement me */

  SU_TRYCATCH(
      suscan_remote_analyzer_queue_call(self, call, SU_TRUE),
      goto done);

  ok = SU_TRUE;

done:
  if (call != NULL)
    suscan_remote_analyzer_release_call(self, call);

  return ok;
}

SUPRIVATE void
suscan_remote_analyzer_req_halt(void *ptr)
{
  suscan_remote_analyzer_t *self = (suscan_remote_analyzer_t *) ptr;
  struct suscan_analyzer_remote_call *call = NULL;

  SU_TRYCATCH(
      call = suscan_remote_analyzer_acquire_call(
          self,
          SUSCAN_ANALYZER_REMOTE_REQ_HALT),
      goto done);

  /* TODO: Implement me */

  SU_TRYCATCH(
      suscan_remote_analyzer_queue_call(self, call, SU_TRUE),
      goto done);

done:
  if (call != NULL)
    suscan_remote_analyzer_release_call(self, call);
}

#define SET_CALLBACK(name) iface.name = JOIN(suscan_remote_analyzer_, name)

const struct suscan_analyzer_interface *
suscan_remote_analyzer_get_interface(void)
{
  static struct suscan_analyzer_interface iface;

  if (g_remote_analyzer_interface == NULL) {
    iface.name = "remote";

    SET_CALLBACK(ctor);
    SET_CALLBACK(dtor);
    SET_CALLBACK(set_frequency);
    SET_CALLBACK(set_gain);
    SET_CALLBACK(set_antenna);
    SET_CALLBACK(set_bandwidth);
    SET_CALLBACK(set_dc_remove);
    SET_CALLBACK(set_iq_reverse);
    SET_CALLBACK(set_agc);
    SET_CALLBACK(force_eos);
    SET_CALLBACK(is_real_time);
    SET_CALLBACK(get_samp_rate);
    SET_CALLBACK(get_measured_samp_rate);
    SET_CALLBACK(get_source_info_pointer);
    SET_CALLBACK(commit_source_info);
    SET_CALLBACK(set_sweep_strategy);
    SET_CALLBACK(set_spectrum_partitioning);
    SET_CALLBACK(set_hop_range);
    SET_CALLBACK(set_buffering_size);
    SET_CALLBACK(write);
    SET_CALLBACK(req_halt);

    g_remote_analyzer_interface = &iface;
  }

  return g_remote_analyzer_interface;
}

#undef SET_CALLBACK

