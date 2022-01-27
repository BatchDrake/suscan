/*

  Copyright (C) 2017 Gonzalo José Carracedo Carballal

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
#include <unistd.h>
#include <pthread.h>
#include <stdint.h>
#include <time.h>

#define SU_LOG_DOMAIN "analyzer"

#include <sigutils/sigutils.h>
#include <sigutils/detect.h>

#include "analyzer.h"

#include "mq.h"
#include "msg.h"

#ifdef bool
#  undef bool
#endif /* bool */

/* Gain info objects */
SUSCAN_SERIALIZER_PROTO(suscan_analyzer_gain_info)
{
  SUSCAN_PACK_BOILERPLATE_START;

  SUSCAN_PACK(str,   self->name);
  SUSCAN_PACK(float, self->min);
  SUSCAN_PACK(float, self->max);
  SUSCAN_PACK(float, self->step);
  SUSCAN_PACK(float, self->value);

  SUSCAN_PACK_BOILERPLATE_END;
}

SUSCAN_DESERIALIZER_PROTO(suscan_analyzer_gain_info)
{
  SUSCAN_UNPACK_BOILERPLATE_START;

  SUSCAN_UNPACK(str,   self->name);
  SUSCAN_UNPACK(float, self->min);
  SUSCAN_UNPACK(float, self->max);
  SUSCAN_UNPACK(float, self->step);
  SUSCAN_UNPACK(float, self->value);

  SUSCAN_UNPACK_BOILERPLATE_END;
}

/* Analyzer params object */
SUSCAN_SERIALIZER_PROTO(suscan_analyzer_params)
{
  SUSCAN_PACK_BOILERPLATE_START;

  SUSCAN_PACK(int,   self->mode);
  SUSCAN_PACK(int,   self->detector_params.window);

  SUSCAN_PACK(uint,  self->detector_params.window_size);
  SUSCAN_PACK(float, self->detector_params.fc);
  SUSCAN_PACK(float, self->detector_params.alpha);
  SUSCAN_PACK(uint,  self->detector_params.decimation);
  SUSCAN_PACK(uint,  self->detector_params.samp_rate);

  SUSCAN_PACK(float, self->channel_update_int);
  SUSCAN_PACK(float, self->psd_update_int);
  SUSCAN_PACK(freq,  self->min_freq);
  SUSCAN_PACK(freq,  self->max_freq);

  SUSCAN_PACK_BOILERPLATE_END;
}

SUSCAN_DESERIALIZER_PROTO(suscan_analyzer_params)
{
  int32_t int32;
  SUSCAN_UNPACK_BOILERPLATE_START;

  SUSCAN_UNPACK(int32,  int32);
  self->mode = int32;

  SUSCAN_UNPACK(int32,  int32);
  self->detector_params.window = int32;

  SUSCAN_UNPACK(uint64, self->detector_params.window_size);
  SUSCAN_UNPACK(float,  self->detector_params.fc);
  SUSCAN_UNPACK(float,  self->detector_params.alpha);
  SUSCAN_UNPACK(uint64, self->detector_params.decimation);
  SUSCAN_UNPACK(uint64, self->detector_params.samp_rate);

  SUSCAN_UNPACK(float,  self->channel_update_int);
  SUSCAN_UNPACK(float,  self->psd_update_int);
  SUSCAN_UNPACK(freq,   self->min_freq);
  SUSCAN_UNPACK(freq,   self->max_freq);

  SUSCAN_UNPACK_BOILERPLATE_END;
}

void
suscan_analyzer_gain_info_destroy(struct suscan_analyzer_gain_info *self)
{
  if (self->name != NULL)
    free(self->name);

  free(self);
}

struct suscan_analyzer_gain_info *
suscan_analyzer_gain_info_dup(
    const struct suscan_analyzer_gain_info *old)
{
  struct suscan_analyzer_gain_info *new = NULL;

  SU_TRYCATCH(
      new = calloc(1, sizeof(struct suscan_analyzer_gain_info)),
      goto fail);

  SU_TRYCATCH(new->name = strdup(old->name), goto fail);

  new->max   = old->max;
  new->min   = old->min;
  new->step  = old->step;
  new->value = old->value;

  return new;

fail:
  if (new != NULL)
    suscan_analyzer_gain_info_destroy(new);

  return NULL;
}

struct suscan_analyzer_gain_info *
suscan_analyzer_gain_info_new(
    const struct suscan_source_gain_value *value)
{
  struct suscan_analyzer_gain_info *new = NULL;

  SU_TRYCATCH(
      new = calloc(1, sizeof(struct suscan_analyzer_gain_info)),
      goto fail);

  SU_TRYCATCH(new->name = strdup(value->desc->name), goto fail);

  new->max   = value->desc->max;
  new->min   = value->desc->min;
  new->step  = value->desc->step;
  new->value = value->val;

  return new;

fail:
  if (new != NULL)
    suscan_analyzer_gain_info_destroy(new);

  return NULL;
}

struct suscan_analyzer_gain_info *
suscan_analyzer_gain_info_new_value_only(
    const char *name,
    SUFLOAT value)
{
  struct suscan_analyzer_gain_info *new = NULL;

  SU_TRYCATCH(
      new = calloc(1, sizeof(struct suscan_analyzer_gain_info)),
      goto fail);

  SU_TRYCATCH(new->name = strdup(name), goto fail);

  new->value = value;

  return new;

fail:
  if (new != NULL)
    suscan_analyzer_gain_info_destroy(new);

  return NULL;
}

/* Helper methods */
SUSCAN_SERIALIZER_PROTO(suscan_analyzer_source_info)
{
  SUSCAN_PACK_BOILERPLATE_START;
  unsigned int i;

  SUSCAN_PACK(uint,  self->source_samp_rate);
  SUSCAN_PACK(uint,  self->effective_samp_rate);
  SUSCAN_PACK(float, self->measured_samp_rate);
  SUSCAN_PACK(freq,  self->frequency);
  SUSCAN_PACK(freq,  self->freq_min);
  SUSCAN_PACK(freq,  self->freq_max);
  SUSCAN_PACK(freq,  self->lnb);
  SUSCAN_PACK(float, self->bandwidth);
  SUSCAN_PACK(float, self->ppm);
  SUSCAN_PACK(str,   self->antenna);
  SUSCAN_PACK(bool,  self->dc_remove);
  SUSCAN_PACK(bool,  self->iq_reverse);
  SUSCAN_PACK(bool,  self->agc);

  SUSCAN_PACK(bool,   self->have_qth);
  if (self->have_qth) {
    SUSCAN_PACK(double, self->qth.lat);
    SUSCAN_PACK(double, self->qth.lon);
    SUSCAN_PACK(double, self->qth.height);
  }

  SUSCAN_PACK(uint, self->source_time.tv_sec);
  SUSCAN_PACK(uint, self->source_time.tv_usec);

  SUSCAN_PACK(bool, self->seekable);
  if (self->seekable) {
    SUSCAN_PACK(uint,  self->source_start.tv_sec);
    SUSCAN_PACK(uint,  self->source_start.tv_usec);  
    SUSCAN_PACK(uint, self->source_end.tv_sec);
    SUSCAN_PACK(uint, self->source_end.tv_usec);
  }

  SU_TRYCATCH(cbor_pack_map_start(buffer, self->gain_count) == 0, goto fail);
  for (i = 0; i < self->gain_count; ++i)
    SU_TRYCATCH(
        suscan_analyzer_gain_info_serialize(self->gain_list[i], buffer),
        goto fail);

  SU_TRYCATCH(cbor_pack_map_start(buffer, self->antenna_count) == 0, goto fail);
  for (i = 0; i < self->antenna_count; ++i)
    SUSCAN_PACK(str, self->antenna_list[i]);

  SUSCAN_PACK_BOILERPLATE_END;
}

SUSCAN_DESERIALIZER_PROTO(suscan_analyzer_source_info)
{
  SUSCAN_UNPACK_BOILERPLATE_START;
  SUBOOL end_required = SU_FALSE;
  size_t i;
  uint64_t nelem = 0;
  uint64_t tv_sec;
  uint32_t tv_usec;

  SUSCAN_UNPACK(uint64, self->source_samp_rate);
  SUSCAN_UNPACK(uint64, self->effective_samp_rate);
  SUSCAN_UNPACK(float,  self->measured_samp_rate);
  SUSCAN_UNPACK(freq,   self->frequency);
  SUSCAN_UNPACK(freq,   self->freq_min);
  SUSCAN_UNPACK(freq,   self->freq_max);
  SUSCAN_UNPACK(freq,   self->lnb);
  SUSCAN_UNPACK(float,  self->bandwidth);
  SUSCAN_UNPACK(float,  self->ppm);
  SUSCAN_UNPACK(str,    self->antenna);
  SUSCAN_UNPACK(bool,   self->dc_remove);
  SUSCAN_UNPACK(bool,   self->iq_reverse);
  SUSCAN_UNPACK(bool,   self->agc);

  SUSCAN_UNPACK(bool,   self->have_qth);
  if (self->have_qth) {
    SUSCAN_UNPACK(double, self->qth.lat);
    SUSCAN_UNPACK(double, self->qth.lon);
    SUSCAN_UNPACK(double, self->qth.height);
  }

  SUSCAN_UNPACK(uint64, tv_sec);
  SUSCAN_UNPACK(uint32, tv_usec);
  self->source_time.tv_sec  = tv_sec;
  self->source_time.tv_usec = tv_usec;

  SUSCAN_UNPACK(bool, self->seekable);
  if (self->seekable) {
    SUSCAN_UNPACK(uint64, tv_sec);
    SUSCAN_UNPACK(uint32, tv_usec);  
    self->source_start.tv_sec  = tv_sec;
    self->source_start.tv_usec = tv_usec;

    SUSCAN_UNPACK(uint64, tv_sec);
    SUSCAN_UNPACK(uint32, tv_usec);
    self->source_end.tv_sec  = tv_sec;
    self->source_end.tv_usec = tv_usec;
  }

  /* Deserialize gains */
  SU_TRYCATCH(
      cbor_unpack_map_start(buffer, &nelem, &end_required) == 0,
      goto fail);
  SU_TRYCATCH(!end_required, goto fail);

  self->gain_count = (unsigned int) nelem;

  if (self->gain_count > 0) {
    SU_TRYCATCH(
        self->gain_list = calloc(
            nelem,
            sizeof (struct suscan_analyzer_gain_info *)),
        goto fail);

    for (i = 0; i < self->gain_count; ++i) {
      SU_TRYCATCH(
          self->gain_list[i] = 
            calloc(1, sizeof (struct suscan_analyzer_gain_info)),
          goto fail);

      SU_TRYCATCH(
          suscan_analyzer_gain_info_deserialize(self->gain_list[i], buffer),
          goto fail);
    }
  } else {
    self->gain_list = NULL;
  }
  
  /* Deserialize antennas */
  SU_TRYCATCH(
      cbor_unpack_map_start(buffer, &nelem, &end_required) == 0,
      goto fail);
  SU_TRYCATCH(!end_required, goto fail);

  self->antenna_count = (unsigned int) nelem;

  if (self->antenna_count > 0) {
    SU_TRYCATCH(
      self->antenna_list = calloc(nelem, sizeof (char *)), 
      goto fail);

    for (i = 0; i < self->antenna_count; ++i)
      SUSCAN_UNPACK(str, self->antenna_list[i]);
  } else {
    self->antenna_list = NULL;
  }

  SUSCAN_UNPACK_BOILERPLATE_END;
}

void
suscan_analyzer_source_info_init(struct suscan_analyzer_source_info *self)
{
  memset(self, 0, sizeof(struct suscan_analyzer_source_info));
}

SUBOOL
suscan_analyzer_source_info_init_copy(
    struct suscan_analyzer_source_info *self,
    const struct suscan_analyzer_source_info *origin)
{
  struct suscan_analyzer_gain_info *gi = NULL;
  char *dup = NULL;
  unsigned int i;
  SUBOOL ok = SU_FALSE;

  suscan_analyzer_source_info_init(self);

  self->source_samp_rate    = origin->source_samp_rate;
  self->effective_samp_rate = origin->effective_samp_rate;
  self->measured_samp_rate  = origin->measured_samp_rate;
  self->frequency           = origin->frequency;
  self->freq_min            = origin->freq_min;
  self->freq_max            = origin->freq_max;
  self->lnb                 = origin->lnb;
  self->bandwidth           = origin->bandwidth;
  self->ppm                 = origin->ppm;
  self->source_time         = origin->source_time;
  self->seekable            = origin->seekable;

  if (self->seekable) {
    self->source_start = origin->source_start;
    self->source_end   = origin->source_end;
  }

  if (origin->antenna != NULL)
    SU_TRYCATCH(self->antenna = strdup(origin->antenna), goto done);

  self->dc_remove  = origin->dc_remove;
  self->iq_reverse = origin->iq_reverse;
  self->agc        = origin->agc;

  for (i = 0; i < origin->gain_count; ++i) {
    SU_TRYCATCH(
        gi = suscan_analyzer_gain_info_dup(origin->gain_list[i]),
        goto done);

    SU_TRYCATCH(PTR_LIST_APPEND_CHECK(self->gain, gi) != -1, goto done);
    gi = NULL;
  }

  for (i = 0; i < origin->antenna_count; ++i) {
    SU_TRYCATCH(dup = strdup(origin->antenna_list[i]), goto done);
    SU_TRYCATCH(PTR_LIST_APPEND_CHECK(self->antenna, dup) != -1, goto done);
    dup = NULL;
  }

  ok = SU_TRUE;

done:
  if (gi != NULL)
    suscan_analyzer_gain_info_destroy(gi);

  if (dup != NULL)
    free(dup);

  if (!ok)
    suscan_analyzer_source_info_finalize(self);

  return ok;
}

void
suscan_analyzer_source_info_finalize(struct suscan_analyzer_source_info *self)
{
  unsigned int i;

  if (self->antenna != NULL)
    free(self->antenna);

  for (i = 0; i < self->gain_count; ++i)
    if (self->gain_list[i] != NULL)
      suscan_analyzer_gain_info_destroy(self->gain_list[i]);

  if (self->gain_list != NULL)
    free(self->gain_list);

  for (i = 0; i < self->antenna_count; ++i)
    if (self->antenna_list[i] != NULL)
      free(self->antenna_list[i]);

  if (self->antenna_list != NULL)
    free(self->antenna_list);

  memset(self, 0, sizeof(struct suscan_analyzer_source_info));
}

void
suscan_analyzer_consume_mq(struct suscan_mq *mq)
{
  void *private;
  uint32_t type;

  while (suscan_mq_poll(mq, &type, &private))
    suscan_analyzer_dispose_message(type, private);
}

SUPRIVATE SUBOOL
suscan_analyzer_consume_mq_until_halt(struct suscan_mq *mq)
{
  void *private;
  uint32_t type;

  while (suscan_mq_poll(mq, &type, &private))
    if (type == SUSCAN_WORKER_MSG_TYPE_HALT)
      return SU_TRUE;
    else
      suscan_analyzer_dispose_message(type, private);

  return SU_FALSE;
}

SUBOOL
suscan_analyzer_halt_worker(suscan_worker_t *worker)
{
  while (worker->state == SUSCAN_WORKER_STATE_RUNNING) {
    suscan_worker_req_halt(worker);

    while (!suscan_analyzer_consume_mq_until_halt(worker->mq_out))
      suscan_mq_wait(worker->mq_out);
  }

  return suscan_worker_destroy(worker);
}

void *
suscan_analyzer_read(suscan_analyzer_t *self, uint32_t *type)
{
  return suscan_analyzer_read_timeout(self, type, NULL);
}

void *
suscan_analyzer_read_timeout(
    suscan_analyzer_t *self,
    uint32_t *type,
    const struct timeval *timeout)
{
  uint32_t msg_type;
  void *ret;

  do {
    msg_type = -1;
    ret = suscan_mq_read_timeout(self->mq_out, &msg_type, timeout);
    if (msg_type == -1)
      return NULL;

    if (suscan_analyzer_message_has_expired(self, ret, msg_type)) {
      suscan_analyzer_dispose_message(msg_type, ret);
      msg_type = -1;
      ret = NULL;
    }
  } while (ret == NULL && msg_type != SUSCAN_WORKER_MSG_TYPE_HALT);

  *type = msg_type;
  
  return ret;
}

struct suscan_analyzer_inspector_msg *
suscan_analyzer_read_inspector_msg(suscan_analyzer_t *analyzer)
{
  /* TODO: use poll and wait to wait for EOS and inspector messages */
  return suscan_mq_read_w_type(
      analyzer->mq_out,
      SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR);
}

struct suscan_analyzer_inspector_msg *
suscan_analyzer_read_inspector_msg_timeout(
    suscan_analyzer_t *analyzer,
    const struct timeval *timeout)
{
  /* TODO: use poll and wait to wait for EOS and inspector messages */
  return suscan_mq_read_w_type_timeout(
      analyzer->mq_out,
      SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR,
      timeout);
}

/*************************** Analyzer wrappers ********************************/
suscan_analyzer_t *
suscan_analyzer_new_from_interface(
    const struct suscan_analyzer_params *params,
    struct suscan_mq *mq_out,
    const struct suscan_analyzer_interface *iface,
    ...)
{
  suscan_analyzer_t *new = NULL;
  va_list ap;
  SUBOOL ok = SU_FALSE;

  va_start(ap, iface);

#ifdef DEBUG_ANALYZER_PARAMS
  suscan_analyzer_params_debug(params);
#endif /* DEBUG_ANALYZER_PARAMS */

  SU_TRYCATCH(new = calloc(1, sizeof (suscan_analyzer_t)), goto fail);

  new->params  = *params;
  new->running = SU_TRUE;
  new->mq_out  = mq_out;

  new->iface = iface;

  SU_TRYCATCH(new->impl = (iface->ctor) (new, ap), goto fail);

  ok = SU_TRUE;

fail:
  if (!ok && new != NULL) {
    suscan_analyzer_destroy(new);
    new = NULL;
  }

  va_end(ap);

  return new;
}

suscan_analyzer_t *
suscan_analyzer_new(
    const struct suscan_analyzer_params *params,
    suscan_source_config_t *config,
    struct suscan_mq *mq)
{
  const struct suscan_analyzer_interface *iface =
      suscan_local_analyzer_get_interface();

  /* TODO: Replace by a lookup method when the
   * global interface list is available */

  if (suscan_source_config_is_remote(config))
    iface = suscan_remote_analyzer_get_interface();

  return suscan_analyzer_new_from_interface(params, mq, iface, config);
}

void
suscan_analyzer_destroy(suscan_analyzer_t *self)
{
  if (self->impl != NULL) {
    (void) suscan_analyzer_force_eos(self);

    if (self->running) {
      if (!self->halt_requested) {
        suscan_analyzer_req_halt(self);

        /*
         * TODO: this cannot wait forever. Add suscan_mq_read_with_timeout
         */
        while (!suscan_analyzer_consume_mq_until_halt(self->mq_out))
          suscan_mq_wait(self->mq_out);
      }
    }

    (self->iface->dtor) (self->impl);
  }

  free(self);
}

/* Source-related methods */
SUBOOL
suscan_analyzer_set_freq(suscan_analyzer_t *self, SUFREQ freq, SUFREQ lnb)
{
  return (self->iface->set_frequency) (self->impl, freq, lnb);
}

SUBOOL
suscan_analyzer_set_gain(
    suscan_analyzer_t *self,
    const char *name,
    SUFLOAT value)
{
  return (self->iface->set_gain) (self->impl, name, value);
}


SUBOOL
suscan_analyzer_set_antenna(suscan_analyzer_t *self, const char *name)
{
  return (self->iface->set_antenna) (self->impl, name);
}

SUBOOL
suscan_analyzer_set_bw(suscan_analyzer_t *self, SUFLOAT bw)
{
  return (self->iface->set_bandwidth) (self->impl, bw);
}

SUBOOL
suscan_analyzer_set_ppm(suscan_analyzer_t *self, SUFLOAT ppm)
{
  return (self->iface->set_ppm) (self->impl, ppm);
}

SUBOOL
suscan_analyzer_set_dc_remove(suscan_analyzer_t *self, SUBOOL val)
{
  return (self->iface->set_dc_remove) (self->impl, val);
}

SUBOOL
suscan_analyzer_set_iq_reverse(suscan_analyzer_t *self, SUBOOL val)
{
  return (self->iface->set_iq_reverse) (self->impl, val);
}

SUBOOL
suscan_analyzer_set_agc(suscan_analyzer_t *self, SUBOOL val)
{
  return (self->iface->set_agc) (self->impl, val);
}

SUBOOL
suscan_analyzer_force_eos(suscan_analyzer_t *self)
{
  return (self->iface->force_eos) (self->impl);
}

SUBOOL
suscan_analyzer_commit_source_info(suscan_analyzer_t *self)
{
  return (self->iface->commit_source_info) (self->impl);
}

/* Worker-specific methods */
SUBOOL
suscan_analyzer_set_sweep_stratrgy(
    suscan_analyzer_t *self,
    enum suscan_analyzer_sweep_strategy strategy)
{
  return (self->iface->set_sweep_strategy) (self->impl, strategy);
}

SUBOOL
suscan_analyzer_set_spectrum_partitioning(
    suscan_analyzer_t *self,
    enum suscan_analyzer_spectrum_partitioning partitioning)
{
  return (self->iface->set_spectrum_partitioning) (self->impl, partitioning);
}

SUBOOL
suscan_analyzer_set_hop_range(suscan_analyzer_t *self, SUFREQ min, SUFREQ max)
{
  return (self->iface->set_hop_range) (self->impl, min, max);
}

SUBOOL
suscan_analyzer_set_buffering_size(suscan_analyzer_t *self, SUSCOUNT size)
{
  return (self->iface->set_buffering_size) (self->impl, size);
}

SUBOOL
suscan_analyzer_set_inspector_freq_overridable(
    suscan_analyzer_t *self,
    SUHANDLE handle,
    SUFREQ freq)
{
  if (self->iface->set_inspector_frequency != NULL)
    return (self->iface->set_inspector_frequency) (self->impl, handle, freq);

  return suscan_analyzer_set_inspector_freq_async(self, handle, freq, 0);
}

SUBOOL
suscan_analyzer_set_inspector_bandwidth_overridable(
    suscan_analyzer_t *self,
    SUHANDLE handle,
    SUFLOAT bw)
{
  if (self->iface->set_inspector_bandwidth != NULL)
    return (self->iface->set_inspector_bandwidth) (self->impl, handle, bw);

  return suscan_analyzer_set_inspector_bandwidth_async(self, handle, bw, 0);
}

/* Generic message write */
SUBOOL
suscan_analyzer_write(suscan_analyzer_t *self, uint32_t type, void *priv)
{
  return (self->iface->write) (self->impl, type, priv);
}

/* Request halt */
void
suscan_analyzer_req_halt(suscan_analyzer_t *self)
{
  self->halt_requested = SU_TRUE;

  (self->iface->req_halt) (self->impl);
}


