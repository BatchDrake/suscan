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

#define SU_LOG_DOMAIN "callbacks"

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
  gint response;

  for (;;) {
    response = suscan_settings_dialog_run(gui);

    if (response == 0) {
      /* We try first with these */
      if (!suscan_gui_parse_all_changed_profiles(gui))
        continue;

      /* We load these always */
      suscan_gui_settings_from_dialog(gui);

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
    } else if (response == 1) {
      suscan_gui_reset_all_profiles(gui);
    }

    break;
  }
}

void
suscan_on_activate_channel_discovery_settings(GtkListBoxRow *row, gpointer data)
{
  suscan_gui_t *gui = (suscan_gui_t *) data;

  gtk_stack_set_visible_child(
      gui->settingsViewStack,
      GTK_WIDGET(gui->channelDiscoveryFrame));
}

void
suscan_on_activate_color_settings(GtkListBoxRow *row, gpointer data)
{
  suscan_gui_t *gui = (suscan_gui_t *) data;

  gtk_stack_set_visible_child(
        gui->settingsViewStack,
        GTK_WIDGET(gui->colorsFrame));
}

void
suscan_on_activate_source_settings(GtkListBoxRow *row, gpointer data)
{
  suscan_gui_t *gui = (suscan_gui_t *) data;

  /* TODO: */
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
suscan_spectrum_on_center(GtkWidget *widget, gpointer data)
{
  suscan_gui_t *gui = (suscan_gui_t *) data;

  sugtk_spectrum_set_freq_offset(gui->spectrum, 0);
}

void
suscan_spectrum_on_settings_changed(GtkWidget *widget, gpointer data)
{
  suscan_gui_t *gui = (suscan_gui_t *) data;
  gboolean auto_level, prev_auto_level;

  if (!gui->updating_settings) {
    gui->updating_settings = SU_TRUE;

    sugtk_spectrum_set_show_channels(
        gui->spectrum,
        gtk_toggle_button_get_active(gui->overlayChannelToggleButton));

    prev_auto_level = sugtk_spectrum_get_auto_level(gui->spectrum);

    sugtk_spectrum_set_auto_level(
        gui->spectrum,
        gtk_toggle_button_get_active(gui->autoGainToggleButton));

    auto_level = sugtk_spectrum_get_auto_level(gui->spectrum);

    if (sugtk_spectrum_get_s_wf_ratio(gui->spectrum) !=
        gtk_range_get_value(GTK_RANGE(gui->panadapterScale)))
      sugtk_spectrum_set_s_wf_ratio(
          gui->spectrum,
          gtk_range_get_value(GTK_RANGE(gui->panadapterScale)));

    if (!auto_level) {
      if (prev_auto_level) {
        gtk_range_set_value(
            GTK_RANGE(gui->gainScale),
            sugtk_spectrum_get_ref_level(gui->spectrum));
        gtk_range_set_value(
            GTK_RANGE(gui->rangeScale),
            sugtk_spectrum_get_dbs_per_div(gui->spectrum));
      } else {
        sugtk_spectrum_set_ref_level(
            gui->spectrum,
            gtk_range_get_value(GTK_RANGE(gui->gainScale)));
        sugtk_spectrum_set_dbs_per_div(
            gui->spectrum,
            gtk_range_get_value(GTK_RANGE(gui->rangeScale)));
      }
    }

    gtk_widget_set_sensitive(
        GTK_WIDGET(gui->gainScale),
        !sugtk_spectrum_get_auto_level(gui->spectrum));
    gtk_widget_set_sensitive(
        GTK_WIDGET(gui->rangeScale),
        !sugtk_spectrum_get_auto_level(gui->spectrum));

    gui->updating_settings = SU_FALSE;
  }
}

void
suscan_gui_on_throttle_override(GtkWidget *widget, gpointer data)
{
  suscan_gui_t *gui = (suscan_gui_t *) data;
  gboolean overriden;

  overriden = gtk_toggle_button_get_active(
      GTK_TOGGLE_BUTTON(gui->throttleOverrideCheckButton));

  gtk_widget_set_sensitive(
      GTK_WIDGET(gui->throttleSampRateSpinButton),
      overriden);

  if (gui->analyzer != NULL) {
    if (overriden)
      suscan_analyzer_set_throttle_async(
          gui->analyzer,
          gtk_spin_button_get_value(gui->throttleSampRateSpinButton),
          rand());
    else
      suscan_analyzer_set_throttle_async(
          gui->analyzer,
          0,
          rand());
  }
}

void
suscan_gui_on_size_allocate(
    GtkWidget *widget,
    GtkAllocation *allocation,
    gpointer data)
{
  if (allocation->width > SUSCAN_GUI_SPECTRUM_PANEL_WIDTH) {
    gtk_paned_set_position(
        GTK_PANED(widget),
        allocation->width - SUSCAN_GUI_SPECTRUM_PANEL_WIDTH);
  }
}

void
suscan_gui_pass_row_selection(
    GtkListBox *box,
    GtkListBoxRow *row,
    gpointer data)
{
  g_signal_emit_by_name(row, "activate", 0, NULL);
}
