/*

  Copyright (C) 2017 Gonzalo José Carracedo Carballal

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

#include <string.h>
#include <time.h>

#define SU_LOG_DOMAIN "inspector-gui-serialize"

#include <sigutils/agc.h>
#include <codec/codec.h>

#include "gui.h"
#include "inspector.h"

suscan_object_t *
suscan_gui_inspector_serialize(const suscan_gui_inspector_t *inspector)
{
  suscan_object_t *new = NULL;
  suscan_object_t *field = NULL;

  SU_TRYCATCH(new = suscan_object_new(SUSCAN_OBJECT_TYPE_OBJECT), goto fail);

  SU_TRYCATCH(suscan_object_set_class(new, "inspector"), goto fail);

  SU_TRYCATCH(
      suscan_object_set_field_value(new, "class", inspector->class),
      goto fail);

  SU_TRYCATCH(field = suscan_config_to_object(inspector->config), goto fail);

  SU_TRYCATCH(
      suscan_object_set_field(new, "demod_params", field),
      goto fail);

  field = NULL;

  return new;

fail:
  if (new != NULL)
    suscan_object_destroy(new);

  if (field != NULL)
    suscan_object_destroy(field);

  return NULL;
}
