/*

  Copyright (C) 2018 Gonzalo José Carracedo Carballal

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

#ifndef _INSPSCHED_H
#define _INSPSCHED_H

#include <util.h>
#include <sigutils/specttuner.h>

#include "worker.h"

struct suscan_inspector;
struct suscan_inspsched;

struct suscan_inspector_task_info {
  int index; /* Back reference to task_info list */
  struct suscan_inspsched *sched; /* BORROWED: Scheduler owning this task_info */
  struct suscan_inspector *inspector;  /* BORROWED: Inspector to feed */
  const su_specttuner_channel_t *channel; /* BORROWED: Channel */
  const SUCOMPLEX *data;
  SUSCOUNT size;
};

struct suscan_analyzer;

struct suscan_inspsched {
  struct suscan_analyzer *analyzer;

  /* Inspector task info */
  PTR_LIST(struct suscan_inspector_task_info, task_info);

  /* Worker pool */
  PTR_LIST(suscan_worker_t, worker);
  unsigned int last_worker; /* Used as rotatory index */
};

typedef struct suscan_inspsched suscan_inspsched_t;

SUINLINE unsigned int
suscan_inspsched_get_num_workers(const suscan_inspsched_t *sched)
{
  return sched->worker_count;
}

SUINLINE struct suscan_analyzer *
suscan_inspsched_get_analyzer(const suscan_inspsched_t *sched)
{
  return sched->analyzer;
}

void suscan_inspector_task_info_destroy(
    struct suscan_inspector_task_info *info);

struct suscan_inspector_task_info *suscan_inspector_task_info_new(
    struct suscan_inspector *inspector);

SUBOOL suscan_inspsched_append_task_info(
    suscan_inspsched_t *sched,
    struct suscan_inspector_task_info *info);

SUBOOL suscan_inspsched_remove_task_info(
    suscan_inspsched_t *sched,
    struct suscan_inspector_task_info *info);

SUBOOL suscan_inspsched_queue_task(
    suscan_inspsched_t *sched,
    struct suscan_inspector_task_info *task_info);

SUBOOL suscan_inspsched_sync(suscan_inspsched_t *sched);

suscan_inspsched_t *suscan_inspsched_new(struct suscan_analyzer *analyzer);

SUBOOL suscan_inspsched_destroy(suscan_inspsched_t *sched);

#endif /* _INSPSCHED_H */
