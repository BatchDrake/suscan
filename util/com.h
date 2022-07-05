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

#ifndef _UTIL_COM_H
#define _UTIL_COM_H

#include <sigutils/types.h>
#include <pthread.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef void (*suscan_generic_dtor_t) (void *);

struct suscan_refcount {
  pthread_mutex_t mutex;
  SUBOOL init;
  
  unsigned int counter;
  suscan_generic_dtor_t dtor;
  void *owner;
#ifdef SUSCAN_REFCOUNT_DEBUG
  PTR_LIST(char, ref);
#endif /* SUSCAN_REFCOUNT_DEBUG */
};

typedef struct suscan_refcount suscan_refcount_t;

#define SUSCAN_REFCNT_FIELD refcnt
#define SUSCAN_REFCOUNT suscan_refcount_t SUSCAN_REFCNT_FIELD

#ifdef SUSCAN_REFCOUNT_DEBUG
SUINLINE void
suscan_refcount_append_ref(suscan_refcount_t *ref, const char *name)
{
  char *dup = NULL;
  SUBOOL mutex_acquired = SU_FALSE;
  
  if (pthread_mutex_lock(&ref->mutex) != 0)
    goto done;
  mutex_acquired = SU_TRUE;
  
#ifdef SUSCAN_REFCOUNT_PRINT_REFERENCES
  fprintf(
    stderr, 
    "%p: append ref `%s' (%d to %d)\n", 
    ref->owner, 
    name, 
    ref->counter - 1,
    ref->counter);
#endif /* SUSCAN_REFCOUNT_PRINT_REFERENCES */

  if ((dup = strdup(name)) == NULL)
    goto done;

  if (PTR_LIST_APPEND_CHECK(ref->ref, dup) == -1)
    goto done;

  dup = NULL;

done:
  if (dup != NULL)
    free(dup);
  
  if (mutex_acquired)
    pthread_mutex_unlock(&ref->mutex);  
}

SUINLINE void
suscan_refcount_remove_ref(suscan_refcount_t *ref, const char *name)
{
  unsigned int i;
  SUBOOL mutex_acquired = SU_FALSE;

  if (pthread_mutex_lock(&ref->mutex) != 0)
    goto done;
  mutex_acquired = SU_TRUE;
  
#ifdef SUSCAN_REFCOUNT_PRINT_REFERENCES
  fprintf(
    stderr, 
    "%p: remove ref `%s' (%d to %d)\n", 
    ref->owner, 
    name, 
    ref->counter,
    ref->counter - 1);
#endif /* SUSCAN_REFCOUNT_PRINT_REFERENCES */

  for (i = 0; i < ref->ref_count; ++i) {
    if (ref->ref_list[i] != NULL
        && strcmp(ref->ref_list[i], name) == 0) {
      free(ref->ref_list[i]);
      ref->ref_list[i] = NULL;
      break;
    }
  }

done:
  if (mutex_acquired)
    pthread_mutex_unlock(&ref->mutex);    
}

#endif /* SUSCAN_REFCOUNT_DEBUG */

SUINLINE SUBOOL
suscan_refcount_inc(suscan_refcount_t *ref)
{
  if (pthread_mutex_lock(&ref->mutex) != 0)
    return SU_FALSE;

  ++ref->counter;

  pthread_mutex_unlock(&ref->mutex);

  return SU_TRUE;
}

SUINLINE SUBOOL
suscan_refcount_dec(suscan_refcount_t *ref)
{
  if (pthread_mutex_lock(&ref->mutex) != 0)
    return SU_FALSE;
  
  --ref->counter;
  
  pthread_mutex_unlock(&ref->mutex);

  if (ref->counter == 0) {
#ifdef SUSCAN_REFCOUNT_DEBUG
    fprintf(stderr, "%p: destructor called\n", ref->owner);
#endif /* SUSCAN_RECOUNT_DEBUG */
    (ref->dtor)(ref->owner);
  }

  return SU_TRUE;
}

#ifdef SUSCAN_REFCOUNT_DEBUG
#  define SU_REF(ptr, context)                             \
do {                                                       \
  if (suscan_refcount_inc(&(ptr)->SUSCAN_REFCNT_FIELD))    \
    suscan_refcount_append_ref(                            \
      &(ptr)->SUSCAN_REFCNT_FIELD,                         \
      STRINGIFY(context));                                 \
} while (0)

#define SU_DEREF(ptr, context)                             \
do {                                                       \
  suscan_refcount_remove_ref(                              \
    &(ptr)->SUSCAN_REFCNT_FIELD,                           \
    STRINGIFY(context));                                   \
  (void) suscan_refcount_dec(&(ptr)->SUSCAN_REFCNT_FIELD); \
} while (0)
#else  /* SUSCAN_REFCOUNT_DEBUG */
#  define SU_REF(ptr, context)                             \
  (void) suscan_refcount_inc(&(ptr)->SUSCAN_REFCNT_FIELD); \

#  define SU_DEREF(ptr, context)                           \
  (void) suscan_refcount_dec(&(ptr)->SUSCAN_REFCNT_FIELD)
#endif /* SUSCAN_REFCOUNT_DEBUG */

#define SUSCAN_INIT_REFCOUNT(clsname, ptr)                 \
  suscan_refcount_init(                                    \
    &(ptr)->SUSCAN_REFCNT_FIELD,                           \
    (void (*) (void *)) JOIN(clsname, _destroy))

#define SUSCAN_FINALIZE_REFCOUNT(ptr)                      \
  suscan_refcount_finalize(&(ptr)->SUSCAN_REFCNT_FIELD)

SUBOOL suscan_refcount_init(suscan_refcount_t *self, suscan_generic_dtor_t dtor);
void   suscan_refcount_finalize(suscan_refcount_t *self);
void   suscan_refcount_debug(const suscan_refcount_t *self);
#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _UTIL_COM_H */
