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

#define SU_LOG_DOMAIN "main"

#include "gui.h"

gint
suscan_settings_dialog_run(suscan_gui_t *gui)
{
  gint response;

  gtk_dialog_set_default_response(gui->settingsDialog, 0);
  response = gtk_dialog_run(gui->settingsDialog);
  gtk_widget_hide(GTK_WIDGET(gui->settingsDialog));

  return response;
}

void
suscan_on_about(GtkWidget *widget, gpointer data)
{
  suscan_gui_t *gui = (suscan_gui_t *) data;

  (void) gtk_dialog_run(gui->aboutDialog);
  gtk_widget_hide(GTK_WIDGET(gui->aboutDialog));
}


void
suscan_on_settings(GtkWidget *widget, gpointer data)
{
  suscan_gui_t *gui = (suscan_gui_t *) data;
  struct suscan_gui_src_ui *config;
  gint response;

  for (;;) {
    response = suscan_settings_dialog_run(gui);

    if (response == 0) { /* Okay pressed */
      config = suscan_gui_get_selected_src_ui(gui);

      if (!suscan_gui_analyzer_params_from_dialog(gui)) {
        suscan_error(
            gui,
            "Analyzer params",
            "Invalid values passed to analyzer parameters (see log)");
        continue;
      }

      if (gui->state == SUSCAN_GUI_STATE_RUNNING)
        if (!suscan_analyzer_set_params_async(
            gui->analyzer,
            &gui->analyzer_params,
            0))
          suscan_error(
              gui,
              "Analyzer params",
              "Failed to send parameters to analyzer thread");

      if (gtk_widget_get_sensitive(GTK_WIDGET(gui->sourceGrid))) {
        if (!suscan_gui_src_ui_from_dialog(config)) {
          suscan_error(
              gui,
              "Parameter validation",
              "Invalid values passed to source parameters (see log)");
          continue;
        }

        suscan_gui_set_src_ui(gui, config);
      }
    }

    break;
  }
}

void
suscan_on_toggle_connect(GtkWidget *widget, gpointer data)
{
  suscan_gui_t *gui = (suscan_gui_t *) data;

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

void
suscan_on_open_inspector(GtkWidget *widget, gpointer data)
{
  suscan_gui_t *gui = (suscan_gui_t *) data;

  /* Send open message. We will open new tab on response */
  SU_TRYCATCH(
      suscan_analyzer_open_async(
          gui->analyzer,
          &gui->selected_channel,
          rand()),
      return);
}

struct suscan_gui_src_ui *
suscan_gui_get_selected_src_ui(const suscan_gui_t *gui)
{
  struct suscan_gui_src_ui *ui;
  GtkTreeIter iter;
  GtkTreeModel *model;
  GValue val = G_VALUE_INIT;

  model = gtk_combo_box_get_model(gui->sourceCombo);
  gtk_combo_box_get_active_iter(gui->sourceCombo, &iter);
  gtk_tree_model_get_value(model, &iter, 1, &val);

  ui = (struct suscan_gui_src_ui *) g_value_get_pointer(&val);

  g_value_unset(&val);

  return ui;
}

SUBOOL
suscan_gui_set_selected_src_ui(
    suscan_gui_t *gui,
    const struct suscan_gui_src_ui *new_ui)
{
  GtkTreeIter iter;
  const struct suscan_gui_src_ui *ui;
  gboolean ok;

  ok = gtk_tree_model_get_iter_first(
      GTK_TREE_MODEL(gui->sourceListStore),
      &iter);

  while (ok) {
    gtk_tree_model_get(
        GTK_TREE_MODEL(gui->sourceListStore),
        &iter,
        1,
        &ui,
        -1);

    if (ui == new_ui) {
      gtk_combo_box_set_active_iter(gui->sourceCombo, &iter);
      return SU_TRUE;
    }

    ok = gtk_tree_model_iter_next(GTK_TREE_MODEL(gui->sourceListStore), &iter);
  }

  return SU_FALSE;
}

void
suscan_on_source_changed(GtkWidget *widget, gpointer *data)
{
  suscan_gui_t *gui = (suscan_gui_t *) data;
  struct suscan_gui_src_ui *config;
  GList *list;
  GtkWidget *cfgui = NULL;

  config = suscan_gui_get_selected_src_ui(gui);

  list = gtk_container_get_children(GTK_CONTAINER(gui->sourceAlignment));

  if (list != NULL) {
    cfgui = list->data;

    g_list_free(list);
  }

  if (cfgui != NULL)
    gtk_container_remove(
        GTK_CONTAINER(gui->sourceAlignment),
        GTK_WIDGET(cfgui));

  cfgui = suscan_gui_cfgui_get_root(config->cfgui);

  gtk_container_add(GTK_CONTAINER(gui->sourceAlignment), cfgui);

  gtk_widget_show(cfgui);

  gtk_window_resize(GTK_WINDOW(gui->settingsDialog), 1, 1);
}
