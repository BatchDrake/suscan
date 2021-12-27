/*

  Copyright (C) 2018 Gonzalo José Carracedo Carballal

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

#define SU_LOG_DOMAIN "inspsched"

#include <sigutils/log.h>
#include <unistd.h>

#include "inspsched.h"

#include <compat.h>
#include "msg.h"

/*************************** Task Info API ***************************/
SUPRIVATE struct suscan_inspector_task_info *
suscan_inspector_task_info_new(suscan_inspector_t *inspector)
{
  struct suscan_inspector_task_info *new = NULL;

  SU_TRYCATCH(
      new = calloc(1, sizeof(struct suscan_inspector_task_info)),
      return NULL);

  new->inspector = inspector;

  return new;
}

SUPRIVATE void
suscan_inspector_task_info_destroy(struct suscan_inspector_task_info *info)
{
  free(info);
}

struct suscan_inspector_task_info *
suscan_inspsched_acquire_task_info(
  suscan_inspsched_t *self,
  suscan_inspector_t *insp)
{
  struct suscan_inspector_task_info *result = NULL, *allocd = NULL;
  SUBOOL mutex_acquired = SU_FALSE;

  SU_TRYCATCH(pthread_mutex_lock(&self->task_mutex) == 0, goto done);
  mutex_acquired = SU_TRUE;

  if (list_is_empty(AS_LIST(self->task_free_list))) {
    (void) pthread_mutex_unlock(&self->task_mutex);
    mutex_acquired = SU_FALSE;

    SU_TRYCATCH(
      allocd = suscan_inspector_task_info_new(insp),
      goto done);

    allocd->sched = self;

    SU_TRYCATCH(pthread_mutex_lock(&self->task_mutex) == 0, goto done);
    mutex_acquired = SU_TRUE;

    result = allocd;
    allocd = NULL;
  } else {
    result = list_get_head(AS_LIST(self->task_free_list));
    list_remove_element(AS_LIST(self->task_free_list), result);

    result->inspector = insp;
  }

  /* One way or the other, a new task info was created. Put it into the
     alloc list and increase refcounter */
  
  list_insert_head(AS_LIST(self->task_alloc_list), result);

  SU_REF(insp, task_info);

done:
  if (mutex_acquired)
    (void) pthread_mutex_unlock(&self->task_mutex);

  if (allocd != NULL)
    suscan_inspector_task_info_destroy(allocd);

  return result;
}

void
suscan_inspsched_return_task_info(
  suscan_inspsched_t *self,
  struct suscan_inspector_task_info *task_info)
{
  SUBOOL mutex_acquired = SU_FALSE;

  SU_TRYCATCH(pthread_mutex_lock(&self->task_mutex) == 0, goto done);
  mutex_acquired = SU_TRUE;

  SU_DEREF(task_info->inspector, task_info);

  /* Remove from alloc list */
  list_remove_element(AS_LIST(self->task_alloc_list), task_info);

  /* And insert in free list */
  list_insert_head(AS_LIST(self->task_free_list), task_info);

  task_info = NULL;

done:
  if (mutex_acquired)
    (void) pthread_mutex_unlock(&self->task_mutex);

  if (task_info == NULL)
    suscan_inspector_task_info_destroy(task_info);
}


/****************************** Inspsched API ****************************/
SUPRIVATE SUBOOL
suscan_inpsched_task_cb(
    struct suscan_mq *mq_out,
    void *wk_private,
    void *cb_private)
{
  suscan_inspsched_t *sched = (suscan_inspsched_t *) wk_private;
  struct suscan_inspector_task_info *task_info =
      (struct suscan_inspector_task_info *) cb_private;
  SUBOOL ok = SU_FALSE;

  /* Feed all enabled estimators */
  SU_TRYCATCH(
      suscan_inspector_estimator_loop(
          task_info->inspector,
          task_info->data,
          task_info->size),
      goto fail);

  /* Feed spectrum */
  SU_TRYCATCH(
      suscan_inspector_spectrum_loop(
          task_info->inspector,
          task_info->data,
          task_info->size),
      goto fail);

  /*
   * We just process the incoming data. If we broke something,
   * mark the inspector as halted.
   */
  SU_TRYCATCH(
      suscan_inspector_sampler_loop(
          task_info->inspector,
          task_info->data,
          task_info->size),
      goto fail);

  ok = SU_TRUE;

fail:
  if (!ok)
    task_info->inspector->state = SUSCAN_ASYNC_STATE_HALTING;

  suscan_inspsched_return_task_info(sched, task_info);

  return SU_FALSE;
}

/* TODO: Move to factory implementation */
SUPRIVATE SUBOOL
suscan_inpsched_barrier_cb(
    struct suscan_mq *mq_out,
    void *wk_private,
    void *cb_private)
{
  suscan_inspsched_t *sched = (suscan_inspsched_t *) wk_private;

  pthread_barrier_wait(&sched->barrier);

  return SU_FALSE;
}

SUPRIVATE unsigned int
suscan_inspsched_get_min_workers(void)
{
  long count;

  if ((count = sysconf(_SC_NPROCESSORS_ONLN)) < 2)
    count = 2;

  return count - 1;
}

SUBOOL
suscan_inspsched_queue_task(
    suscan_inspsched_t *sched,
    struct suscan_inspector_task_info *task_info)
{
  /* Process new samples */
  SU_TRYCATCH(
      suscan_worker_push(
          sched->worker_list[sched->last_worker],
          suscan_inpsched_task_cb,
          task_info),
      return SU_FALSE);

  if (++sched->last_worker == sched->worker_count)
    sched->last_worker = 0;

  return SU_TRUE;
}

SUBOOL
suscan_inspsched_sync(suscan_inspsched_t *sched)
{
  unsigned int i;

  /* Queue barriers */
  for (i = 0; i < sched->worker_count; ++i)
    SU_TRYCATCH(
        suscan_worker_push(
            sched->worker_list[i],
            suscan_inpsched_barrier_cb,
            NULL),
        return SU_FALSE);

  /* Wait for all threads */
  pthread_barrier_wait(&sched->barrier);

  /* Reset date */
  sched->have_time = SU_FALSE;

  return SU_TRUE;
}

SUBOOL
suscan_inspsched_destroy(suscan_inspsched_t *self)
{
  unsigned int i;
  struct suscan_inspector_task_info *info, *tmp;

  /*
   * Attempt to halt all workers. These are analyzer workers, and
   * should be halted as such.
   */
  for (i = 0; i < self->worker_count; ++i)
    if (!suscan_analyzer_halt_worker(self->worker_list[i])) {
      SU_ERROR("Fatal error while halting inspsched workers\n");
      return SU_FALSE;
    }

  if (self->worker_list != NULL)
    free(self->worker_list);

  /*
   * All workers halted, source worker must be finished by now
   * it is safe to go on with the object destruction. We basically
   * traverse the freelist and perform a free. In the alloc list
   * are all task infos that have been left unprocessed.
   */

  FOR_EACH_SAFE(info, tmp, self->task_free_list)
    suscan_inspector_task_info_destroy(info);

  FOR_EACH_SAFE(info, tmp, self->task_alloc_list) {
    SU_DEREF(info->inspector, task_info);
    suscan_inspector_task_info_destroy(info);
  }

  if (self->task_init)
    pthread_mutex_destroy(&self->task_mutex);

  if (self->barrier_init)
    pthread_barrier_destroy(&self->barrier);

  free(self);

  return SU_TRUE;
}


suscan_inspsched_t *
suscan_inspsched_new(struct suscan_mq *ctl_mq)
{
  suscan_inspsched_t *new = NULL;
  suscan_worker_t *worker = NULL;

  unsigned int i, count;

  SU_TRYCATCH(new = calloc(1, sizeof(suscan_inspsched_t)), goto fail);

  new->ctl_mq = ctl_mq;
  
  count = suscan_inspsched_get_min_workers();

  for (i = 0; i < count; ++i) {
    SU_TRYCATCH(
      worker = suscan_worker_new_ex("inspsched-worker", new->ctl_mq, new), 
      goto fail);
    SU_TRYCATCH(PTR_LIST_APPEND_CHECK(new->worker, worker) != -1, goto fail);
    worker = NULL;
  }

  SU_TRYCATCH(
    pthread_mutex_init(&new->task_mutex, NULL) == 0,
    goto fail);
  new->task_init = SU_TRUE;

  SU_TRYCATCH(
    pthread_barrier_init(&new->barrier, NULL, new->worker_count + 1) == 0,
    goto fail);
  new->barrier_init = SU_TRUE;

  return new;

fail:
  /*
   * We can call worker_halt because it is empty and no messages will be
   * emitted from any callback.
   */
  if (worker != NULL)
    suscan_worker_halt(worker);

  if (new != NULL)
    suscan_inspsched_destroy(new);

  return NULL;
}
