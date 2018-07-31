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

#define SU_LOG_DOMAIN "inspector-gui-callbacks"

#include <sigutils/agc.h>
#include <codec/codec.h>

#include "gui.h"
#include "inspector.h"

void
suscan_gui_inspector_serialize_cb(GtkWidget *widget, gpointer data)
{
  suscan_gui_inspector_t *inspector = (suscan_gui_inspector_t *) data;
  suscan_gui_t *gui = suscan_gui_symsrc_get_gui((suscan_gui_symsrc_t *) data);
  suscan_object_t *object = NULL;
  const char *name;

  name = suscan_gui_prompt(
      gui,
      "Save inspector",
      "Enter inspector name",
      "");

  if (name != NULL) {
    SU_TRYCATCH(
        object = suscan_gui_inspector_serialize(inspector),
        goto done);

    SU_TRYCATCH(
        suscan_object_set_field_value(object, "label", name),
        goto done);

    /* Save inspector */
    SU_TRYCATCH(
        suscan_config_context_put(gui->inspectors_ctx, object),
        goto done);

    object = NULL;
  }

done:
  if (object != NULL)
    suscan_object_destroy(object);
}
