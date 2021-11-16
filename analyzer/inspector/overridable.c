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

#include "overridable.h"

SUPRIVATE void
suscan_inspector_overridable_request_destroy(
  struct suscan_inspector_overridable_request *self)
{
  free(self);
}

SUPRIVATE struct suscan_inspector_overridable_request *
suscan_inspector_overridable_request_new(suscan_inspector_t *insp)
{
  struct suscan_inspector_overridable_request *new = NULL;

  SU_TRYCATCH(
      new = calloc(1, sizeof(struct suscan_inspector_overridable_request)),
      goto done);

  new->insp = insp;

  return new;

done:
  if (new != NULL)
    suscan_inspector_overridable_request_destroy(new);

  return NULL;
}

SUBOOL
suscan_inspector_request_manager_init(suscan_inspector_request_manager_t *self)
{
  SUBOOL ok = SU_FALSE;

  memset(self, 0, sizeof (suscan_inspector_request_manager_t));

  SU_TRYCATCH(
    pthread_mutex_init(&self->overridable_mutex, NULL) == 0, 
    goto done);
  self->overridable_init = SU_TRUE;

  ok = SU_TRUE;

done:
  if (!ok)
    suscan_inspector_request_manager_finalize(self);

  return ok;
}

void
suscan_inspector_request_manager_finalize(
  suscan_inspector_request_manager_t *self)
{
  struct suscan_inspector_overridable_request *req, *tmp;

  /* Overridable requests in the free pool */
  FOR_EACH_SAFE(req, tmp, self->overridable_free_list)
    suscan_inspector_overridable_request_destroy(req);
  

  /* Overridable requests that have not been processed */
  FOR_EACH_SAFE(req, tmp, self->overridable_alloc_list) {
    SU_DEREF(req->insp, overridable);
    suscan_inspector_overridable_request_destroy(req);
  }

  if (self->overridable_init)
    (void) pthread_mutex_destroy(&self->overridable_mutex);
}

SUPRIVATE void
suscan_inspector_request_manager_return_overridable_unsafe(
  suscan_inspector_request_manager_t *self,
  struct suscan_inspector_overridable_request *request)
{
  SU_DEREF(request->insp, overridable);

  /* Reset userdata */
  suscan_inspector_set_userdata(request->insp, NULL);
  request->insp = NULL;

  /* Remove from alloc list */
  list_remove_element(AS_LIST(self->overridable_alloc_list), request);

  /* And insert in free list */
  list_insert_head(AS_LIST(self->overridable_free_list), request);
}

/* This must be called from the master thread */
SUBOOL
suscan_inspector_request_manager_commit_overridable(
  suscan_inspector_request_manager_t *self)
{
  struct suscan_inspector_overridable_request *this, *tmp;
  SUBOOL mutex_acquired = SU_FALSE;
  SUBOOL ok = SU_FALSE;

  if (!list_is_empty(AS_LIST(self->overridable_alloc_list))) {
    SU_TRYCATCH(pthread_mutex_lock(&self->overridable_mutex) == 0, goto done);
    mutex_acquired = SU_TRUE;

    /* vvvvvvvvvvvvvvvvvvvv overridable mutex acquired vvvvvvvvvvvvvvvvvvvv */
    FOR_EACH_SAFE(this, tmp, self->overridable_alloc_list) {
      if (!this->dead) {
        /* Acknowledged */
        suscan_inspector_set_userdata(this->insp, NULL);

        /* Parse this request */
        if (this->freq_request) {
          SU_TRYCATCH(
            suscan_inspector_factory_set_inspector_freq(
              suscan_inspector_get_factory(this->insp),
              this->insp,
              this->new_freq),
            goto done);
        }

        if (this->bandwidth_request) {
          /* Set bandwidth request */
          SU_TRYCATCH(
            suscan_inspector_factory_set_inspector_bandwidth(
              suscan_inspector_get_factory(this->insp),
              this->insp,
              this->new_bandwidth),
            goto done);

          SU_TRYCATCH(
              suscan_inspector_notify_bandwidth(this->insp, this->new_bandwidth),
              goto done);
        }
      }

      suscan_inspector_request_manager_return_overridable_unsafe(
        self,
        this);
    }
    /* ^^^^^^^^^^^^^^^^^^^^ overridable mutex acquired ^^^^^^^^^^^^^^^^^^^^^^ */
    (void) pthread_mutex_unlock(&self->overridable_mutex);
    mutex_acquired = SU_FALSE;
  }

  ok = SU_TRUE;

done:
  if (mutex_acquired)
    (void) pthread_mutex_unlock(&self->overridable_mutex);

  return ok;
}

SUBOOL
suscan_inspector_request_manager_clear_requests(
  suscan_inspector_request_manager_t *self,
  suscan_inspector_t *insp)
{
  struct suscan_inspector_overridable_request *req = NULL;
  SUBOOL list_locked = SU_FALSE;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(pthread_mutex_lock(&self->overridable_mutex) == 0, goto done);
  list_locked = SU_TRUE;

  if ((req = suscan_inspector_get_userdata(insp)) != NULL)
    suscan_inspector_request_manager_return_overridable_unsafe(self, req);

  ok = SU_TRUE;

done:
  if (list_locked)
    (void) pthread_mutex_unlock(&self->overridable_mutex);

  return ok;
}

/*
 * NOTE: THIS LEAVES THE LIST LOCKED!
 */
struct suscan_inspector_overridable_request *
suscan_inspector_request_manager_acquire_overridable(
    suscan_inspector_request_manager_t *self,
    suscan_inspector_t *insp)
{
  struct suscan_inspector_overridable_request *req = NULL;
  struct suscan_inspector_overridable_request *own_req = NULL;
  SUBOOL mutex_acquired = SU_FALSE;

  SU_TRYCATCH(pthread_mutex_lock(&self->overridable_mutex) == 0, goto done);
  mutex_acquired = SU_TRUE;

  SU_TRYCATCH(insp->state == SUSCAN_ASYNC_STATE_RUNNING, goto done);

  if ((req = suscan_inspector_get_userdata(insp)) == NULL) {
    /* No userdata in inspector. Release mutex, create object and lock again. */

    if (list_is_empty(AS_LIST(self->overridable_free_list))) {
      /* Freelist is empty: allocate new element */
      (void) pthread_mutex_unlock(&self->overridable_mutex);
      mutex_acquired = SU_FALSE;
    
      SU_TRYCATCH(
          own_req = suscan_inspector_overridable_request_new(insp),
          goto done);

      SU_TRYCATCH(
        pthread_mutex_lock(&self->overridable_mutex) == 0,
        goto done);
      mutex_acquired = SU_TRUE;

      req     = own_req;
      own_req = NULL;
    } else {
      /* Take head from freelist */
      req = list_get_head(AS_LIST(self->overridable_free_list));
      list_remove_element(AS_LIST(self->overridable_free_list), req);
      req->insp = insp;
    }
  }

done:
  if (req == NULL && mutex_acquired)
    (void) pthread_mutex_unlock(&self->overridable_mutex);

  if (own_req != NULL)
    suscan_inspector_overridable_request_destroy(own_req);

  return req;
}

/*
 * These are the only two possible unlockers.
 */
void
suscan_inspector_request_manager_discard_overridable(
  suscan_inspector_request_manager_t *self,
  struct suscan_inspector_overridable_request *request)
{
  /* Discarded. Back to freelist */
  list_insert_head(AS_LIST(self->overridable_free_list), request);

  (void) pthread_mutex_unlock(&self->overridable_mutex);
}


void
suscan_inspector_request_manager_submit_overridable(
    suscan_inspector_request_manager_t *self,
    struct suscan_inspector_overridable_request *req)
{
  struct suscan_inspector_overridable_request *old_req;

  old_req = suscan_inspector_get_userdata(req->insp);

  if (old_req == NULL) {
    /* This request is now in the alloc list */
    list_insert_head(AS_LIST(self->overridable_alloc_list), req);

    SU_REF(req->insp, overridable);
    
    suscan_inspector_set_userdata(req->insp, req);
  }

  (void) pthread_mutex_unlock(&self->overridable_mutex);
}
