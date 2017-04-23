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


#include "gui.h"

gint
suscan_settings_dialog_run(struct suscan_gui *gui)
{
  gint response;

  gtk_dialog_set_default_response(gui->settingsDialog, 0);
  response = gtk_dialog_run(gui->settingsDialog);
  gtk_widget_hide(GTK_WIDGET(gui->settingsDialog));

  return response;
}

void
suscan_on_settings(GtkWidget *widget, gpointer *data)
{
  struct suscan_gui *gui = (struct suscan_gui *) data;
  struct suscan_gui_source_config *config;
  gint response;

  for (;;) {
    response = suscan_settings_dialog_run(gui);

    if (response == 0) { /* Okay pressed */
      config = suscan_gui_get_selected_source(gui);

      if (!suscan_gui_source_config_parse(config)) {
        suscan_error(
            gui,
            "Parameter validation",
            "Invalid values passed to source parameters");
        continue;
      }

      suscan_gui_set_config(gui, config);
    }

    break;
  }
}

void
suscan_on_toggle_connect(GtkWidget *widget, gpointer *data)
{
  struct suscan_gui *gui = (struct suscan_gui *) data;

  switch (gui->state) {
    case SUSCAN_GUI_STATE_STOPPED:
      if (!suscan_gui_connect(gui)) {
        suscan_error(
            gui,
            "Connect to source",
            "Failed to start source. Please verify source parameters and"
            "see log messages for details");
      }
      break;

    case SUSCAN_GUI_STATE_RUNNING:
      suscan_gui_disconnect(gui);
      break;

    default:
      suscan_error(gui, "Error", "Impossiburu!");
  }
}

struct suscan_gui_source_config *
suscan_gui_get_selected_source(struct suscan_gui *gui)
{
  struct suscan_gui_source_config *config;
  GtkTreeIter iter;
  GtkTreeModel *model;
  GValue val = G_VALUE_INIT;

  model = gtk_combo_box_get_model(gui->sourceCombo);
  gtk_combo_box_get_active_iter(gui->sourceCombo, &iter);
  gtk_tree_model_get_value(model, &iter, 1, &val);

  config = (struct suscan_gui_source_config *) g_value_get_pointer(&val);

  g_value_unset(&val);

  return config;
}

void
suscan_on_source_changed(GtkWidget *widget, gpointer *data)
{
  struct suscan_gui *gui = (struct suscan_gui *) data;
  struct suscan_gui_source_config *config;
  GList *list;
  GtkWidget *prev = NULL;

  config = suscan_gui_get_selected_source(gui);

  list = gtk_container_get_children(GTK_CONTAINER(gui->sourceAlignment));

  if (list != NULL) {
    prev = list->data;

    g_list_free(list);
  }

  if (prev != NULL)
    gtk_container_remove(GTK_CONTAINER(gui->sourceAlignment), GTK_WIDGET(prev));

  gtk_container_add(
      GTK_CONTAINER(gui->sourceAlignment),
      GTK_WIDGET(config->grid));

  gtk_widget_show(GTK_WIDGET(config->grid));

  gtk_window_resize(GTK_WINDOW(gui->settingsDialog), 1, 1);
}
