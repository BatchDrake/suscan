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

#include <analyzer/impl/local.h>
#include "msg.h"

SUPRIVATE SUBOOL
suscan_inpsched_task_cb(
    struct suscan_mq *mq_out,
    void *wk_private,
    void *cb_private)
{
  suscan_inspsched_t *sched = (suscan_inspsched_t *) wk_private;
  struct suscan_inspector_task_info *task_info =
      (struct suscan_inspector_task_info *) cb_private;

  /*
   * We just process the incoming data. If we broke something,
   * mark the inspector as halted.
   */
  SU_TRYCATCH(
      suscan_inspector_sampler_loop(
          task_info->inspector,
          task_info->data,
          task_info->size,
          sched->analyzer->parent->mq_out),
      goto fail);

  /* Feed all enabled estimators */
  SU_TRYCATCH(
      suscan_inspector_estimator_loop(
          task_info->inspector,
          task_info->data,
          task_info->size,
          sched->analyzer->parent->mq_out),
      goto fail);

  /* Feed spectrum */
  SU_TRYCATCH(
      suscan_inspector_spectrum_loop(
          task_info->inspector,
          task_info->data,
          task_info->size,
          sched->analyzer->parent->mq_out),
      goto fail);

  return SU_FALSE;

fail:
  task_info->inspector->state = SUSCAN_ASYNC_STATE_HALTING;

  return SU_FALSE;
}

SUPRIVATE SUBOOL
suscan_inpsched_barrier_cb(
    struct suscan_mq *mq_out,
    void *wk_private,
    void *cb_private)
{
  suscan_inspsched_t *sched = (suscan_inspsched_t *) wk_private;

  suscan_local_analyzer_source_barrier(sched->analyzer);

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

struct suscan_inspector_task_info *
suscan_inspector_task_info_new(suscan_inspector_t *inspector)
{
  struct suscan_inspector_task_info *new = NULL;

  SU_TRYCATCH(
      new = calloc(1, sizeof(struct suscan_inspector_task_info)),
      return NULL);

  new->index = -1;
  new->inspector = inspector;

  return new;
}

SUFREQ
suscan_inspector_task_info_get_abs_freq(
  const struct suscan_inspector_task_info *task_info)
{
  suscan_analyzer_t *analyzer = 
    SUSCAN_LOCAL_ANALYZER_AS_ANALYZER(
      suscan_inspsched_get_analyzer(task_info->sched));
  unsigned int samp_rate = suscan_analyzer_get_samp_rate(analyzer);
  SUFREQ tuner_freq = 
    suscan_analyzer_get_source_info(analyzer)->frequency;
  SUFREQ channel_freq = 
    tuner_freq + SU_NORM2ABS_FREQ(
      samp_rate,
      SU_ANG2NORM_FREQ(su_specttuner_channel_get_f0(task_info->channel)));

  return channel_freq;
}

void
suscan_inspector_task_info_destroy(struct suscan_inspector_task_info *info)
{
  free(info);
}

void
suscan_inspsched_get_source_time(
  suscan_inspsched_t *sched, 
  struct timeval *tv)
{
  if (!sched->have_time) {
    suscan_local_analyzer_get_source_time(
      sched->analyzer, 
      &sched->source_time);
    sched->have_time = SU_TRUE;
  }

  *tv = sched->source_time;
}

SUBOOL
suscan_inspsched_append_task_info(
    suscan_inspsched_t *sched,
    struct suscan_inspector_task_info *info)
{
  int index;

  SU_TRYCATCH(info->index == -1, return SU_FALSE);

  SU_TRYCATCH(info->sched == NULL, return SU_FALSE);

  SU_TRYCATCH(
      (index = PTR_LIST_APPEND_CHECK(sched->task_info, info)) != -1,
      return SU_FALSE);

  info->index = index;
  info->sched = sched;

  return SU_TRUE;
}

SUBOOL
suscan_inspsched_remove_task_info(
    suscan_inspsched_t *sched,
    struct suscan_inspector_task_info *info)
{
  SU_TRYCATCH(sched == info->sched, return SU_FALSE);

  SU_TRYCATCH(info->index >= 0, return SU_FALSE);

  SU_TRYCATCH(info->index < sched->task_info_count, return SU_FALSE);

  SU_TRYCATCH(sched->task_info_list[info->index] == info, return SU_FALSE);

  sched->task_info_list[info->index] = NULL;

  info->index = -1;
  info->sched = NULL;

  return SU_TRUE;
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
  suscan_local_analyzer_source_barrier(sched->analyzer);

  /* Reset date */
  sched->have_time = SU_FALSE;

  return SU_TRUE;
}

SUBOOL
suscan_inspsched_destroy(suscan_inspsched_t *sched)
{
  unsigned int i;

  /*
   * Attempt to halt all workers. These are analyzer workers, and
   * should be halted as such.
   */
  for (i = 0; i < sched->worker_count; ++i)
    if (!suscan_analyzer_halt_worker(sched->worker_list[i])) {
      SU_ERROR("Fatal error while halting inspsched workers\n");
      return SU_FALSE;
    }

  if (sched->worker_list != NULL)
    free(sched->worker_list);

  /*
   * All workers halted, source worker must be finished by now
   * it is safe to go on with the object destruction
   */
  for (i = 0; i < sched->task_info_count; ++i)
    if (sched->task_info_list[i] != NULL)
      suscan_inspector_task_info_destroy(sched->task_info_list[i]);

  if (sched->task_info_list != NULL)
    free(sched->task_info_list);

  free(sched);

  return SU_TRUE;
}


suscan_inspsched_t *
suscan_inspsched_new(suscan_local_analyzer_t *analyzer)
{
  suscan_inspsched_t *new = NULL;
  suscan_worker_t *worker = NULL;

  unsigned int i, count;

  SU_TRYCATCH(new = calloc(1, sizeof(suscan_inspsched_t)), goto fail);

  new->analyzer = analyzer;

  count = suscan_inspsched_get_min_workers();

  for (i = 0; i < count; ++i) {
    SU_TRYCATCH(worker = suscan_worker_new(&analyzer->mq_in, new), goto fail);
    SU_TRYCATCH(PTR_LIST_APPEND_CHECK(new->worker, worker) != -1, goto fail);
    worker = NULL;
  }

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
