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
SUSCAN_SERIALIZER_PROTO(suscan_source_gain_info)
{
  SUSCAN_PACK_BOILERPLATE_START;

  SUSCAN_PACK(str,   self->name);
  SUSCAN_PACK(float, self->min);
  SUSCAN_PACK(float, self->max);
  SUSCAN_PACK(float, self->step);
  SUSCAN_PACK(float, self->value);

  SUSCAN_PACK_BOILERPLATE_END;
}

SUSCAN_DESERIALIZER_PROTO(suscan_source_gain_info)
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
  int32_t int32 = 0;
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
  /* We resue this timeout as a sane value. */
  struct timespec ts = 
    {
      SUSCAN_WORKER_DESTROY_TIMEOUT_MS / 1000,
      (SUSCAN_WORKER_DESTROY_TIMEOUT_MS * 1000000ull) % 1000000000
    };

  while (worker->state == SUSCAN_WORKER_STATE_RUNNING) {
    suscan_worker_req_halt(worker);

    while (!suscan_analyzer_consume_mq_until_halt(worker->mq_out))
      if (suscan_mq_timedwait(worker->mq_out, &ts)) {
        if (worker->state == SUSCAN_WORKER_STATE_RUNNING) {
          SU_ERROR(
            "Worker destruction took more than %d ms. Aborted.\n",
            SUSCAN_WORKER_DESTROY_TIMEOUT_MS);
          return SU_FALSE;
        }
      }
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
  int errno_val = errno;

  do {
    msg_type = SUSCAN_ANALYZER_MESSAGE_TYPE_INVALID;
    ret = suscan_mq_read_timeout(self->mq_out, &msg_type, timeout);
    if (msg_type == SUSCAN_ANALYZER_MESSAGE_TYPE_INVALID) {
      errno_val = ETIMEDOUT;
      break;
    }

    /* 
     * Network-based analyzers may cause remote messages to be expired
     * before they are actually delivered to the GUI. We just disable
     * flow control in this case and let the consumer handle that.. 
     */
    if (suscan_analyzer_is_local(self)) {
      if (suscan_analyzer_message_has_expired(self, ret, msg_type)) {
        suscan_analyzer_dispose_message(msg_type, ret);
        ret = NULL;
      }
    }
  } while (ret == NULL && msg_type != SUSCAN_WORKER_MSG_TYPE_HALT);

  *type = msg_type;
  errno = errno_val;

  return ret;
}

SUBOOL
suscan_analyzer_wait_until_ready(
  suscan_analyzer_t *self,
  struct timeval *timeout)
{
  uint32_t type;
  void *msg;
  SUBOOL ready = SU_FALSE;
  SUBOOL do_break = SU_FALSE;

  do {
    msg = suscan_analyzer_read_timeout(self, &type, timeout);
    if ( type == SUSCAN_ANALYZER_MESSAGE_TYPE_INVALID
      || type == SUSCAN_ANALYZER_MESSAGE_TYPE_EOS
      || type == SUSCAN_ANALYZER_MESSAGE_TYPE_READ_ERROR
      || type == SUSCAN_WORKER_MSG_TYPE_HALT)
      do_break = SU_TRUE;

    ready = type == SUSCAN_ANALYZER_MESSAGE_TYPE_SOURCE_INFO;
    
    suscan_analyzer_dispose_message(type, msg);
  } while (!do_break && !ready);

  return ready;
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

SUPRIVATE SUBOOL
suscan_analyzer_test_permissions(const suscan_analyzer_t *self, uint64_t perm)
{
  const struct suscan_source_info *info = 
    suscan_analyzer_get_source_info(self);

  return (info->permissions & perm) == perm;
}

#define CHECK_PERMISSION(self, perm)                                 \
  if (!suscan_analyzer_test_permissions(self, perm)) {               \
    SU_WARNING("Action `%s' not allowed by analyzer\n", __FUNCTION__); \
    return SU_FALSE;                                                 \
  }

/* Source-related methods */
SUBOOL
suscan_analyzer_set_freq(suscan_analyzer_t *self, SUFREQ freq, SUFREQ lnb)
{
  CHECK_PERMISSION(self, SUSCAN_ANALYZER_PERM_SET_FREQ);

  return (self->iface->set_frequency) (self->impl, freq, lnb);
}

SUBOOL
suscan_analyzer_set_gain(
    suscan_analyzer_t *self,
    const char *name,
    SUFLOAT value)
{
  CHECK_PERMISSION(self, SUSCAN_ANALYZER_PERM_SET_GAIN);

  return (self->iface->set_gain) (self->impl, name, value);
}

SUBOOL
suscan_analyzer_set_antenna(suscan_analyzer_t *self, const char *name)
{
  CHECK_PERMISSION(self, SUSCAN_ANALYZER_PERM_SET_ANTENNA);

  return (self->iface->set_antenna) (self->impl, name);
}

SUBOOL
suscan_analyzer_set_bw(suscan_analyzer_t *self, SUFLOAT bw)
{
  CHECK_PERMISSION(self, SUSCAN_ANALYZER_PERM_SET_BW);

  return (self->iface->set_bandwidth) (self->impl, bw);
}

SUBOOL
suscan_analyzer_set_ppm(suscan_analyzer_t *self, SUFLOAT ppm)
{
  CHECK_PERMISSION(self, SUSCAN_ANALYZER_PERM_SET_PPM);

  return (self->iface->set_ppm) (self->impl, ppm);
}

SUBOOL
suscan_analyzer_set_dc_remove(suscan_analyzer_t *self, SUBOOL val)
{
  CHECK_PERMISSION(self, SUSCAN_ANALYZER_PERM_SET_DC_REMOVE);

  return (self->iface->set_dc_remove) (self->impl, val);
}

SUBOOL
suscan_analyzer_set_iq_reverse(suscan_analyzer_t *self, SUBOOL val)
{
  CHECK_PERMISSION(self, SUSCAN_ANALYZER_PERM_SET_IQ_REVERSE);

  return (self->iface->set_iq_reverse) (self->impl, val);
}

SUBOOL
suscan_analyzer_set_agc(suscan_analyzer_t *self, SUBOOL val)
{
  CHECK_PERMISSION(self, SUSCAN_ANALYZER_PERM_SET_AGC);

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

SUBOOL
suscan_analyzer_supports_baseband_filtering(suscan_analyzer_t *analyzer)
{
  return analyzer->iface->register_baseband_filter != NULL;
}

SUBOOL
suscan_analyzer_register_baseband_filter_with_prio(
    suscan_analyzer_t *self,
    suscan_analyzer_baseband_filter_func_t func,
    void *privdata,
    int64_t prio)
{
  if (!suscan_analyzer_supports_baseband_filtering(self)) {
    SU_ERROR("This type of analyzer object does not support custom baseband filtering\n");
    return SU_FALSE;
  }

  CHECK_PERMISSION(self, SUSCAN_ANALYZER_PERM_SET_BB_FILTER);

  return (self->iface->register_baseband_filter) (
    self->impl,
    func,
    privdata,
    prio);
}


SUBOOL
suscan_analyzer_register_baseband_filter(
    suscan_analyzer_t *self,
    suscan_analyzer_baseband_filter_func_t func,
    void *privdata)
{
  if (!suscan_analyzer_supports_baseband_filtering(self)) {
    SU_ERROR("This type of analyzer object does not support custom baseband filtering\n");
    return SU_FALSE;
  }

  CHECK_PERMISSION(self, SUSCAN_ANALYZER_PERM_SET_BB_FILTER);

  return (self->iface->register_baseband_filter) (
    self->impl,
    func,
    privdata,
    SUSCAN_ANALYZER_BBFILT_PRIO_DEFAULT);
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


