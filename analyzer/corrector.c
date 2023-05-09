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

#define SU_LOG_DOMAIN "freq-corrector"

#include <sigutils/util/util.h>
#include <sigutils/log.h>

#include "corrector.h"
#include <string.h>

PTR_LIST(
  SUPRIVATE const struct suscan_frequency_corrector_class, 
  corrector_class);

SUBOOL 
suscan_frequency_corrector_class_register(
  const struct suscan_frequency_corrector_class *classdef)
{
  SUBOOL ok = SU_FALSE;

  if (suscan_frequency_corrector_class_lookup(classdef->name) != NULL) {
    SU_ERROR(
      "Frequency corrector class `%s' already registered\n", 
      classdef->name);
    goto done;
  }

  SU_TRYC(PTR_LIST_APPEND_CHECK(corrector_class, (void *) classdef));

  ok = SU_TRUE;

done:
  return ok;
}

const struct suscan_frequency_corrector_class *
suscan_frequency_corrector_class_lookup(const char *name)
{
  unsigned int i;

  for (i = 0; i < corrector_class_count; ++i)
    if (strcmp(corrector_class_list[i]->name, name) == 0)
      return corrector_class_list[i];

  return NULL;
}

suscan_frequency_corrector_t *
suscan_frequency_corrector_new(const char *name, ...)
{
  suscan_frequency_corrector_t *new = NULL;
  const struct suscan_frequency_corrector_class *iface;
  va_list ap;
  SUBOOL ok = SU_FALSE;

  va_start(ap, name);

  if ((iface = suscan_frequency_corrector_class_lookup(name)) == NULL) {
    SU_ERROR("No such corrector class `%s'\n", name);
    goto done;
  }
  
  SU_TRYCATCH(
    new = calloc(1, sizeof(suscan_frequency_corrector_t)), 
    goto done);

  new->iface = iface;

  SU_TRYCATCH(new->userdata = (new->iface->ctor) (ap), goto done);

  ok = SU_TRUE;

done:
  va_end(ap);

  if (!ok) {
    suscan_frequency_corrector_destroy(new);
    new = NULL;
  }

  return new;
}

void 
suscan_frequency_corrector_destroy(suscan_frequency_corrector_t *self)
{
  if (self->userdata != NULL)
    (self->iface->dtor) (self->userdata);
  
  free(self);
}
