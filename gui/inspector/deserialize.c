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

#define SU_LOG_DOMAIN "inspector-gui-deserialize"

#include <sigutils/agc.h>
#include <codec/codec.h>

#include "gui.h"
#include "inspector.h"

SUBOOL
suscan_gui_inspector_deserialize(
    suscan_gui_inspector_t *inspector,
    const suscan_object_t *object)
{
  const char *class;
  const char *label;
  const suscan_object_t *params;

  if (inspector->index == -1) {
    SU_ERROR("Inspector is not associated\n");
    goto fail;
  }

  SU_TRYCATCH(
      class = suscan_object_get_field_value(object, "class"),
      goto fail);

  if ((label = suscan_object_get_field_value(object, "label")) == NULL)
    label = "Unnamed demodulator";

  if (strcmp(class, inspector->class) != 0) {
    SU_ERROR(
        "Incompatible class for inspector (configuration is %d, but inspector is %s)\n",
        class,
        inspector->class);
    goto fail;
  }

  SU_TRYCATCH(
      params = suscan_object_get_field(object, "demod_params"),
      goto fail);

  SU_TRYCATCH(suscan_object_to_config(inspector->config, params), goto fail);

  SU_TRYCATCH(suscan_gui_inspector_commit_config(inspector), goto fail);

  SU_TRYCATCH(suscan_gui_inspector_refresh_on_config(inspector), goto fail);

  return SU_TRUE;

fail:

  return SU_FALSE;
}
