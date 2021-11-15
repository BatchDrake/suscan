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

#ifndef _INSPECTOR_OVERRIDABLE_H
#define _INSPECTOR_OVERRIDABLE_H

#include "factory.h"
#include <compat.h>
#include "list.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/********************** Inspector overridable request API ********************/
struct suscan_inspector_overridable_request
{
  LINKED_LIST;

  suscan_inspector_t *insp;
  SUBOOL  dead;

  SUBOOL  freq_request;
  SUFREQ  new_freq;
  
  SUBOOL  bandwidth_request;
  SUFLOAT new_bandwidth;
};

/********************* Inspector request manager API **************************/
struct suscan_inspector_request_manager {
  suscan_inspector_factory_t                  *owner;
  /* Overridable requests */
  struct suscan_inspector_overridable_request *overridable_free_list;
  struct suscan_inspector_overridable_request *overridable_alloc_list;
  pthread_mutex_t                              overridable_mutex;
  SUBOOL                                       overridable_init;
};

typedef struct suscan_inspector_request_manager 
  suscan_inspector_request_manager_t;

SUBOOL suscan_inspector_request_manager_init(
  suscan_inspector_request_manager_t *self,
  suscan_inspector_factory_t *owner);

void suscan_inspector_request_manager_finalize(
  suscan_inspector_request_manager_t *self);

/* This must be called from the master thread */
SUBOOL suscan_inspector_request_manager_commit_overridable(
  suscan_inspector_request_manager_t *self);

struct suscan_inspector_overridable_request *
suscan_inspector_request_manager_acquire_overridable(
    suscan_inspector_request_manager_t *self,
    suscan_inspector_t *insp);

void suscan_inspector_request_manager_submit_overridable(
    suscan_inspector_request_manager_t *self,
    struct suscan_inspector_overridable_request *rq);

SUBOOL suscan_inspector_request_manager_clear_requests(
  suscan_inspector_request_manager_t *self,
  suscan_inspector_t *insp);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _INSPECTOR_OVERRIDABLE_H */