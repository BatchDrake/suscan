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

#define SU_LOG_DOMAIN "suscan-com"

#include <sigutils/log.h>
#include "com.h"

void
suscan_refcount_debug(const suscan_refcount_t *self)
{
#ifdef SUSCAN_REFCOUNT_DEBUG
  unsigned int i;
#endif /* SUSCAN_REFCOUNT_DEBUG */

  fprintf(stderr, "%p: %u outstanding references\n", self->owner, self->counter);

#ifdef SUSCAN_REFCOUNT_DEBUG
  for (i = 0; i < self->ref_count; ++i)
    if (self->ref_list[i] != NULL)
      fprintf(stderr, "  [0x%02d] %s\n", i, self->ref_list[i]);
  fprintf(stderr, "\n");
#endif /* SUSCAN_REFCOUNT_DEBUG */
}

SUBOOL
suscan_refcount_init(
  suscan_refcount_t *self,
  suscan_generic_dtor_t dtor)
{
  SUBOOL ok = SU_FALSE;

  memset(self, 0, sizeof(suscan_refcount_t));

  SU_TRYCATCH(
    pthread_mutex_init(&self->mutex, NULL) == 0,
    goto done);

  /* We assume the refcounter is the first field */

  self->init  = SU_TRUE;
  self->dtor  = dtor;
  self->owner = self;

  ok = SU_TRUE;

done:
  return ok;
}

void
suscan_refcount_finalize(suscan_refcount_t *self)
{
#ifdef SUSCAN_REFCOUNT_DEBUG
  unsigned int i;

  for (i = 0; i < self->ref_count; ++i)
    if (self->ref_list[i] != NULL)
      free(self->ref_list[i]);

  if (self->ref_list != NULL)
    free(self->ref_list);
#endif /* SUSCAN_REFCOUNT_DEBUG */

  if (self->init)
    pthread_mutex_destroy(&self->mutex);
}
