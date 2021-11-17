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

#define SU_LOG_DOMAIN "insp-factory"

#include "factory.h"

#include <sigutils/sigutils.h>

PTR_LIST(
  const struct suscan_inspector_factory_class,
  factory_class);

SUBOOL
suscan_inspector_factory_class_register(
  const struct suscan_inspector_factory_class *class)
{
  if (suscan_inspector_factory_class_lookup(class->name) != NULL) {
    SU_ERROR("Attempting to register inspector class `%s'\n", class->name);
    return SU_FALSE;
  }

  return PTR_LIST_APPEND_CHECK(factory_class, (void *) class) != -1;
}

const struct suscan_inspector_factory_class *
  suscan_inspector_factory_class_lookup(const char *name)
{
  unsigned int i;

  for (i = 0; i < factory_class_count; ++i)
    if (strcmp(factory_class_list[i]->name, name) == 0)
      return factory_class_list[i];

  return NULL;
}

/***************************** Factory API ***********************************/
SUPRIVATE void 
suscan_inspector_factory_cleanup_unsafe(suscan_inspector_factory_t *self)
{
  unsigned int i;

  for (i = 0; i < self->inspector_count; ++i)
    if (self->inspector_list[i] != NULL
        && self->inspector_list[i]->state == SUSCAN_ASYNC_STATE_HALTED) {
      SU_DEREF(self->inspector_list[i], factory);
      self->inspector_list[i] = NULL;
    }
}

void
suscan_inspector_factory_destroy(suscan_inspector_factory_t *self)
{
  unsigned int i;

  suscan_inspector_factory_cleanup_unsafe(self);

  for (i = 0; i < self->inspector_count; ++i)
    if (self->inspector_list[i] != NULL) {
      /* 
       * Before dereferencing an inspector from here, we
       * have to make sure everything was closed appropriately
       */
      if (self->inspector_list[i]->factory_userdata != NULL)
        (self->iface->close) (
          self->userdata, 
          self->inspector_list[i]->factory_userdata);
#ifdef SUSCAN_REFCOUNT_DEBUG
      suscan_refcount_debug(&self->inspector_list[i]->SUSCAN_REFCNT_FIELD);
#endif /* SUSCAN_REFCOUNG_DEBUG */
      SU_DEREF(self->inspector_list[i], factory);
    }
  
  if (self->inspector_list != NULL)
    free(self->inspector_list);

  if (self->userdata != NULL)
    (self->iface->dtor) (self->userdata);

  if (self->sched != NULL)
    suscan_inspsched_destroy(self->sched);

  if (self->inspector_list_init)
    pthread_mutex_destroy(&self->inspector_list_mutex);

  free(self);
}

suscan_inspector_factory_t *
suscan_inspector_factory_new(const char *name, ...)
{
  suscan_inspector_factory_t *new = NULL;
  pthread_mutexattr_t attr;
  const struct suscan_inspector_factory_class *class = NULL;
  va_list ap;
  SUBOOL ok = SU_FALSE;
  va_start(ap, name);

  if ((class = suscan_inspector_factory_class_lookup(name)) == NULL) {
    SU_ERROR("No such inspector class `%s'\n", name);
    goto done;
  }

  SU_TRYCATCH(
    new = calloc(1, sizeof(suscan_inspector_factory_t)), 
    goto done);

  new->iface = class;

  if ((new->userdata = (new->iface->ctor(new, ap))) == NULL)
    goto done;

  if (new->mq_out == NULL) {
    SU_ERROR("Constructor did not set an output message queue\n");
    goto done;
  }

  if (new->mq_ctl == NULL) {
    SU_ERROR("Constructor did not set a control message queue\n");
    goto done;
  }

  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  SU_TRYCATCH(
    pthread_mutex_init(&new->inspector_list_mutex, &attr) == 0, 
    goto done);
  new->inspector_list_init = SU_TRUE;

  SU_TRYCATCH(
    new->sched = suscan_inspsched_new(new->mq_ctl),
    goto done);

  ok = SU_TRUE;

done:
  va_end(ap);

  if (!ok) {
    if (new != NULL)
      suscan_inspector_factory_destroy(new);
    new = NULL;
  }

  return new;
}

SUPRIVATE void
suscan_inspector_factory_update_frequency_corrections(
  suscan_inspector_factory_t *self,
  suscan_inspector_t *insp)
{
  struct timeval source_time;
  SUFREQ freq;
  SUFLOAT delta_f;

  suscan_inspector_factory_get_time(self, &source_time);
  freq = suscan_inspector_factory_get_inspector_freq(self, insp);

  if (suscan_inspector_get_correction(insp, &source_time, freq, &delta_f))
    suscan_inspector_factory_set_inspector_freq_correction(
      self, 
      insp,
      delta_f);

  /* Deliver pending report */
  (void) suscan_inspector_deliver_report(insp, &source_time, freq);
}

/*
 * Closure route 1: Data arrives to a inspector being halted
 */
SUBOOL
suscan_inspector_factory_feed(
  suscan_inspector_factory_t *self,
  suscan_inspector_t *insp,
  const SUCOMPLEX *data,
  SUSCOUNT size)
{
  struct suscan_inspector_task_info *info = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(insp->state != SUSCAN_ASYNC_STATE_HALTED, goto done);

  /* Make sure the inspector is not in HALTING state. */
  if (insp->state == SUSCAN_ASYNC_STATE_HALTING) {
    (void) (self->iface->close) (self->userdata, insp->factory_userdata);
    insp->factory_userdata = NULL;
    insp->state = SUSCAN_ASYNC_STATE_HALTED; 
    
    /* Yes, that's it */
    ok = SU_TRUE;
    goto done;
  }

  /* Step 1: update frequency corrections for this inspector */
  suscan_inspector_factory_update_frequency_corrections(self, insp);

  /* Step 2: allocate task info and queue task */
  SU_TRYCATCH(
    info = suscan_inspsched_acquire_task_info(self->sched, insp), 
    goto done);

  info->data      = data;
  info->size      = size;
  info->inspector = insp;

  SU_TRYCATCH(suscan_inspsched_queue_task(self->sched, info), goto done);
  info = NULL;

  ok = SU_TRUE;

done:
  if (info != NULL)
    suscan_inspsched_return_task_info(self->sched, info);

  return ok;  
}

SUBOOL
suscan_inspector_factory_force_sync(suscan_inspector_factory_t *self)
{
  return suscan_inspsched_sync(self->sched);
}

/*
 * TODO: This is not enough to halt an inspector, as overridable
 * requests may keep references to it. Remember to call
 * suscan_inspector_request_manager_clear_requests
 */
SUBOOL
suscan_inspector_factory_halt_inspector(
  suscan_inspector_factory_t *self,
  suscan_inspector_t *insp)
{
  SUBOOL mutex_acquired = SU_FALSE;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(pthread_mutex_lock(&self->inspector_list_mutex) == 0, goto done);
  mutex_acquired = SU_TRUE;

  if (insp->state == SUSCAN_ASYNC_STATE_RUNNING)
    insp->state = SUSCAN_ASYNC_STATE_HALTING;

  (void) pthread_mutex_unlock(&self->inspector_list_mutex);
  mutex_acquired = SU_FALSE;

  ok = SU_TRUE;

done:
  if (mutex_acquired)
    (void) pthread_mutex_unlock(&self->inspector_list_mutex);
  
  return ok;
}

SUBOOL
suscan_inspector_factory_walk_inspectors(
  suscan_inspector_factory_t *self,
  SUBOOL (*callback) (
    void *userdata,
    struct suscan_inspector *insp),
  void *userdata)
{
  unsigned int i;
  SUBOOL mutex_acquired = SU_FALSE;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(pthread_mutex_lock(&self->inspector_list_mutex) == 0, goto done);
  mutex_acquired = SU_TRUE;

  for (i = 0; i < self->inspector_count; ++i) {
    if (self->inspector_list[i] != NULL) {
      SU_TRYCATCH(
        suscan_inspector_walk_inspectors(
          self->inspector_list[i],
          callback,
          userdata),
      goto done);

      SU_TRYCATCH(
        (callback) (userdata, self->inspector_list[i]),
        goto done);
    }
  }

  ok = SU_TRUE;

done:
  if (mutex_acquired)
    (void) pthread_mutex_unlock(&self->inspector_list_mutex);
  
  return ok;
}

/*
 * open abstracts the procedure of opening a new inspector. It can imply
 * opening a specttuner channel, or asking some remote device to perform
 * in-device channelization and deliver already decimated samples.
 */
suscan_inspector_t *
suscan_inspector_factory_open(suscan_inspector_factory_t *self, ...)
{
  suscan_inspector_t *new = NULL;
  const char *class;
  void *userdata = NULL;
  struct suscan_inspector_sampling_info samp_info;
  SUHANDLE index = -1;
  SUBOOL mutex_acquired = SU_FALSE;
  va_list ap;
  SUBOOL ok = SU_FALSE;

  va_start(ap, self);

  /* Allocate userdata for this new inspector */
  if ((userdata = (self->iface->open) (self->userdata, &class, &samp_info, ap)) == NULL)
    goto done;

  /* 
   * Okay, whatever had to happen to open this inspector went well, time to
   * create and register the inspector.
   */

  SU_TRYCATCH(
      new = suscan_inspector_new(
        self,         /* This factory */
        class,        /* Inspector class */
        &samp_info,   /* Sampling info, as determined by open */
        self->mq_out, /* Output message queue */
        self->mq_ctl, /* Control message queue */
        userdata),    /* Per-inspector factory data */
      goto done);


  SU_TRYCATCH(pthread_mutex_lock(&self->inspector_list_mutex) == 0, goto done);
  mutex_acquired = SU_TRUE;

  /* vvvvvvvvvvvvvvvvvvvvvvvv inspector_list lock vvvvvvvvvvvvvvvvvvvvvvvvvv */
  suscan_inspector_factory_cleanup_unsafe(self);

  SU_TRYCATCH(
    (index = PTR_LIST_APPEND_CHECK(self->inspector, new)) != -1,
    goto done);
  SU_REF(new, factory);

  new->handle = -1;
  
  /* ^^^^^^^^^^^^^^^^^^^^^^^ inspector_list lock ^^^^^^^^^^^^^^^^^^^^^^^^^^^ */
  (void) pthread_mutex_unlock(&self->inspector_list_mutex);
  mutex_acquired = SU_FALSE;

  /* Registration done. Report inspector object to implementation and return */
  (self->iface->bind) (self->userdata, userdata, new);
  userdata = NULL;

  /* After a successful bind, we can say the inspectir is now running */
  new->state = SUSCAN_ASYNC_STATE_RUNNING;

  ok = SU_TRUE;

done:
  if (mutex_acquired)
    (void) pthread_mutex_unlock(&self->inspector_list_mutex);

  va_end(ap);

  if (!ok) {
    if (new != NULL && index != -1) {
      suscan_inspector_destroy(new);
      new = NULL;
    }
  }

  if (userdata != NULL)
    (self->iface->close) (self->userdata, userdata);

  return new;
}
