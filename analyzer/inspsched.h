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

#ifndef _INSPSCHED_H
#define _INSPSCHED_H

#include <sigutils/util/util.h>
#include <sigutils/specttuner.h>

#define _COMPAT_BARRIERS

#include <compat.h>
#include "worker.h"
#include "list.h"

struct suscan_inspector;
struct suscan_inspsched;
struct suscan_inspector_factory;

enum suscan_inspector_task_info_type {
  SUSCAN_INSPECTOR_TASK_INFO_TYPE_SAMPLES,
  SUSCAN_INSPECTOR_TASK_INFO_TYPE_NEW_FREQ
};

struct suscan_inspector_task_info {
  LINKED_LIST;

  struct suscan_inspsched *sched;
  struct suscan_inspector *inspector;
  enum suscan_inspector_task_info_type type;

  struct {
    const SUCOMPLEX *data;
    SUSCOUNT size;
  } samples;
  
  struct {
    SUFREQ old_f0;
    SUFREQ new_f0;
  } new_freq;
};

struct suscan_local_analyzer;

struct suscan_inspsched {
  struct suscan_mq *ctl_mq;

  struct suscan_mq  mq_out;
  SUBOOL            mq_out_init;
  
  SUBOOL have_time;

  pthread_mutex_t                    task_mutex;
  SUBOOL                             task_init;
  struct suscan_inspector_task_info *task_free_list;
  struct suscan_inspector_task_info *task_alloc_list;

  /* Worker pool */
  PTR_LIST(suscan_worker_t, worker);
  unsigned int last_worker; /* Used as rotatory index */
  pthread_barrier_t  barrier; /* Inspector barrier */
  SUBOOL barrier_init;
};

typedef struct suscan_inspsched suscan_inspsched_t;

SUINLINE unsigned int
suscan_inspsched_get_num_workers(const suscan_inspsched_t *sched)
{
  return sched->worker_count;
}

struct suscan_inspector_task_info *suscan_inspsched_acquire_task_info(
  suscan_inspsched_t *self,
  struct suscan_inspector *insp);

void suscan_inspsched_return_task_info(
  suscan_inspsched_t *self,
  struct suscan_inspector_task_info *task_info);

SUBOOL suscan_inspsched_queue_task(
    suscan_inspsched_t *sched,
    struct suscan_inspector_task_info *task_info);

SUBOOL suscan_inspsched_sync(suscan_inspsched_t *sched);

/*
 * ctl_mq: where worker messages go (i.e. halt messages)
 * insp_mq: where inspector result messages go (i.e. stuff forwarder to the user)
 */
suscan_inspsched_t *suscan_inspsched_new(struct suscan_mq *ctl_mq);

SUBOOL suscan_inspsched_destroy(suscan_inspsched_t *sched);

#endif /* _INSPSCHED_H */
