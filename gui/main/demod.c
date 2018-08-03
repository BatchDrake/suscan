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

#include <string.h>
#include <ctype.h>

#define SU_LOG_DOMAIN "gui-demod"

#include <confdb.h>
#include "gui.h"

/********************* Handle demodulator list *******************************/
suscan_object_t *
suscan_gui_demod_lookup(const suscan_gui_t *gui, const char *name)
{
  unsigned int i, count;
  suscan_object_t *object;
  const char *label;

  count = suscan_object_set_get_count(gui->demod_obj);

  for (i = 0; i < count; ++i) {
    object = suscan_object_set_get(gui->demod_obj, i);
    if (object != NULL) {
      label = suscan_object_get_field_value(object, "label");
      if (label != NULL && strcmp(label, name) == 0)
        return object;
    }
  }

  return NULL;
}

SUBOOL
suscan_gui_demod_append(
    suscan_gui_t *gui,
    const char *name,
    suscan_object_t *object)
{
  SU_TRYCATCH(suscan_gui_demod_lookup(gui, name) == NULL, return SU_FALSE);

  SU_TRYCATCH(
      suscan_object_set_field_value(object, "label", name),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_context_put(gui->demod_ctx, object),
      return SU_FALSE);

  suscan_gui_demod_refresh_list_store(gui);

  return SU_TRUE;
}

SUBOOL
suscan_gui_demod_remove(suscan_gui_t *gui, suscan_object_t *obj)
{
  if (suscan_config_context_remove(gui->demod_ctx, obj)) {
    suscan_gui_demod_refresh_list_store(gui);
    return SU_TRUE;
  }

  return SU_FALSE;
}

/************************ UI interaction ****************************/
const suscan_object_t *
suscan_gui_ask_for_demod(suscan_gui_t *gui)
{
  int result;
  GtkTreePath *path;

  gui->selected_demod = NULL;

  /* Select first */
  path = gtk_tree_path_new_from_indices(0, -1);
  gtk_tree_selection_select_path(
      gtk_tree_view_get_selection(gui->selectDemodTreeView),
      path);
  gtk_tree_path_free(path);

  gtk_widget_show(GTK_WIDGET(gui->chooseDemodulatorDialog));

  result = gtk_dialog_run(gui->chooseDemodulatorDialog);

  gtk_widget_hide(GTK_WIDGET(gui->chooseDemodulatorDialog));

  if (result)
    return gui->selected_demod;

  return NULL;
}

void
suscan_gui_demod_refresh_list_store(suscan_gui_t *gui)
{
  unsigned int i, count;
  const suscan_object_t *object, *params;
  GtkTreeIter new_element;
  char *ptr;
  const char *value;
  char class[10];
  char baudrate[32];
  unsigned int j;

  gtk_list_store_clear(gui->demodulatorsListStore);

  count = suscan_object_set_get_count(gui->demod_obj);

  baudrate[sizeof(baudrate) - 1] = '\0';
  class[sizeof(class) - 1] = '\0';

  for (i = 0; i < count; ++i) {
    object = suscan_object_set_get(gui->demod_obj, i);
    if (object != NULL
        && (params = suscan_object_get_field(object, "demod_params")) != NULL) {

      if ((value = suscan_object_get_field_value(params, "clock.baud")) == NULL)
        continue;

      strncpy(baudrate, value, sizeof(baudrate) - 1);
      if ((ptr = strchr(baudrate, '.')) != NULL)
        *ptr = '\0';

      if ((value = suscan_object_get_field_value(object, "class")) == NULL)
        continue;

      strncpy(class, value, sizeof(class) - 1);
      for (j = 0; j < strlen(class); ++j)
        class[j] = toupper(class[j]);

      gtk_list_store_append(gui->demodulatorsListStore, &new_element);
      gtk_list_store_set(
          gui->demodulatorsListStore,
          &new_element,
          0, suscan_object_get_field_value(object, "label"),
          1, class,
          2, baudrate,
          3, object,
          -1);
    }
  }
}

