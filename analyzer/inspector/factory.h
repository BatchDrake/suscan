/*

  Copyright (C) 2021 Gonzalo José Carracedo Carballal

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

#ifndef _INSPECTOR_FACTORY_H
#define _INSPECTOR_FACTORY_H

#include "inspector.h"
#include "inspsched.h"
#include "mq.h"

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct suscan_inspector_factory;

/* TODO: Use hashtables */

struct suscan_inspector_factory_class {
  const char *name;
  void *(*ctor)(struct suscan_inspector_factory *, va_list);

  void (*get_time) (void *, struct timeval *tv);
  
  /* Inspector handling */
  /* Called by open (register handle). */
  void *(*open) (void *, const char **, struct suscan_inspector_sampling_info *, va_list);

  /* Called by open as well */
  void (*bind) (void *, void *, suscan_inspector_t *);

  /* Called by cleanup before destroy (remove handle) */
  void  (*close) (void *, void *);

  /* Called after feeding every inspector */
  void  (*free_buf) (void *, void *, SUCOMPLEX *, SUSCOUNT);

  /* Set absolute bandwidth */
  SUBOOL (*set_bandwidth) (void *, void *, SUFLOAT);

  /* Set absolute frequency */
  SUBOOL (*set_frequency) (void *, void *, SUFREQ);
  
  /* Set domain */
  SUBOOL (*set_domain) (void *, void *, SUBOOL);

  SUFREQ (*get_abs_freq) (void *, void *);
  SUBOOL (*set_freq_correction) (void *, void *, SUFLOAT);
  
  void (*dtor)(void *);
};

SUBOOL suscan_inspector_factory_class_register(
  const struct suscan_inspector_factory_class *);

const struct suscan_inspector_factory_class *
  suscan_inspector_factory_class_lookup(const char *name);

/*************************** Inspector Factory API ***************************/
struct suscan_inspector_factory {
  const struct suscan_inspector_factory_class *iface;
  void *userdata;

  struct suscan_mq *mq_out;
  struct suscan_mq *mq_ctl;

  struct suscan_inspector_task_info *task_info_pool;
  
  PTR_LIST(suscan_inspector_t, inspector); /* This list owns inspectors */
  pthread_mutex_t     inspector_list_mutex; /* Inspector list lock */
  SUBOOL              inspector_list_init;
  
  suscan_inspsched_t *sched;   /* Inspector scheduler */
};

typedef struct suscan_inspector_factory suscan_inspector_factory_t;

SUINLINE void
suscan_inspector_factory_set_mq_out(
  suscan_inspector_factory_t *self,
  struct suscan_mq *mq)
{
  self->mq_out = mq;
}

SUINLINE void
suscan_inspector_factory_set_mq_ctl(
  suscan_inspector_factory_t *self,
  struct suscan_mq *mq)
{
  self->mq_ctl = mq;
}

SUINLINE void
suscan_inspector_factory_get_time(
  const suscan_inspector_factory_t *self,
  struct timeval *tv)
{
  (self->iface->get_time) (self->userdata, tv);
}

SUINLINE SUFREQ
suscan_inspector_factory_get_inspector_freq(
  const suscan_inspector_factory_t *self,
  const suscan_inspector_t *insp)
{
  return (self->iface->get_abs_freq) (self->userdata, insp->factory_userdata);
}

SUINLINE SUBOOL
suscan_inspector_factory_set_inspector_freq(
  suscan_inspector_factory_t *self,
  suscan_inspector_t *insp,
  SUFREQ freq)
{
  return (self->iface->set_frequency) (
    self->userdata,
    insp->factory_userdata,
    freq);
}

SUINLINE SUBOOL
suscan_inspector_factory_set_inspector_domain(
  suscan_inspector_factory_t *self,
  suscan_inspector_t *insp,
  SUBOOL is_freq)
{
  return (self->iface->set_domain) (
    self->userdata,
    insp->factory_userdata,
    is_freq);
}

SUINLINE SUBOOL
suscan_inspector_factory_set_inspector_bandwidth(
  suscan_inspector_factory_t *self,
  suscan_inspector_t *insp,
  SUFLOAT bandwidth)
{
  return (self->iface->set_bandwidth) (
    self->userdata,
    insp->factory_userdata,
    bandwidth);
}

SUINLINE SUBOOL
suscan_inspector_factory_set_inspector_freq_correction(
  suscan_inspector_factory_t *self,
  suscan_inspector_t *insp,
  SUFLOAT correction)
{
  return (self->iface->set_freq_correction) (
    self->userdata, 
    insp->factory_userdata, 
    correction);
}
suscan_inspector_factory_t *suscan_inspector_factory_new(
  const char *name,
  ...);

suscan_inspector_t *suscan_inspector_factory_open(
  suscan_inspector_factory_t *self,
  ...);

SUBOOL suscan_inspector_factory_walk_inspectors(
  suscan_inspector_factory_t *self,
  SUBOOL (*callback) ( /* Traverse function */
    void *userdata,
    struct suscan_inspector *insp),
  void *userdata);

/* Decrease reference counter */
void suscan_inspector_factory_release_inspector(
  suscan_inspector_factory_t *self,
  suscan_inspector_t *insp);
  
SUBOOL suscan_inspector_factory_feed(
  suscan_inspector_factory_t *self,
  suscan_inspector_t *insp,
  const SUCOMPLEX *data,
  SUSCOUNT size);

SUBOOL suscan_inspector_factory_notify_freq(
  suscan_inspector_factory_t *self,
  suscan_inspector_t *insp,
  SUFLOAT prev_f0,
  SUFLOAT next_f0);

SUBOOL suscan_inspector_factory_force_sync(suscan_inspector_factory_t *self);

SUBOOL suscan_inspector_factory_halt_inspector(
  suscan_inspector_factory_t *self,
  suscan_inspector_t *insp);

/* Close all halted inspectors */
void suscan_inspector_factory_cleanup(
  suscan_inspector_factory_t *self);

void suscan_inspector_factory_destroy(suscan_inspector_factory_t *self);
  
#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* _INSPECTOR_FACTORY_H */
