/*

  Copyright (C) 2017 Gonzalo José Carracedo Carballal

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

/* Gain info objects */
void
suscan_analyzer_gain_info_destroy(struct suscan_analyzer_gain_info *self)
{
  if (self->name != NULL)
    free(self->name);

  free(self);
}

struct suscan_analyzer_gain_info *
suscan_analyzer_gain_info_new(const char *name, SUFLOAT value)
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
suscan_analyzer_read(suscan_analyzer_t *analyzer, uint32_t *type)
{
  return suscan_mq_read(analyzer->mq_out, type);
}

void *
suscan_analyzer_read_timeout(
    suscan_analyzer_t *analyzer,
    uint32_t *type,
    const struct timeval *timeout)
{
  return suscan_mq_read_timeout(analyzer->mq_out, type, timeout);
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
  return suscan_analyzer_new_from_interface(
      params,
      mq,
      suscan_local_analyzer_get_interface(),
      config);
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


