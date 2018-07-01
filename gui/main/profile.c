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

#define SU_LOG_DOMAIN "gui"

#include <confdb.h>
#include "modemctl.h"
#include "gui.h"

/**************************** Append profile GUIs  ***************************/

/* We can do this as this function is only called from the GUI thread */
SUPRIVATE const gchar *
suscan_gui_get_profile_name(suscan_gui_profile_t *profile)
{
  static char namebuf[32];

  snprintf(namebuf, sizeof(namebuf), "prof-0x%016lx", (unsigned long) profile);

  return namebuf;
}

SUPRIVATE void
suscan_gui_select_profile(GtkWidget *widget, gpointer data)
{
  suscan_gui_profile_t *profile = (suscan_gui_profile_t *) data;
  suscan_gui_t *gui = suscan_gui_profile_get_gui(profile);

  gtk_stack_set_visible_child(
      gui->settingsViewStack,
      suscan_gui_profile_get_root(profile));
}

SUPRIVATE void
suscan_gui_add_profile_widgets(
    suscan_gui_t *gui,
    suscan_gui_profile_t *profile)
{
  GtkWidget *widget;

  widget = gtk_list_box_row_new();

  gtk_container_add(
      GTK_CONTAINER(widget),
      suscan_gui_profile_get_selector(profile));
  gtk_list_box_insert(gui->settingsSelectorListBox, widget, -1);
  gtk_widget_show(widget);

  gtk_widget_set_size_request(widget, 100, 50);

  g_signal_connect(
      widget,
      "activate",
      G_CALLBACK(suscan_gui_select_profile),
      profile);

  widget = suscan_gui_profile_get_root(profile);
  gtk_stack_add_named(
      gui->settingsViewStack,
      widget,
      suscan_gui_get_profile_name(profile));
  gtk_widget_show(widget);
}

SUPRIVATE SUBOOL
suscan_gui_append_profile(suscan_source_config_t *cfg, void *private)
{
  suscan_gui_t *gui = (suscan_gui_t *) private;
  suscan_gui_profile_t *profile = NULL;

  SU_TRYCATCH(profile = suscan_gui_profile_new(cfg), goto fail);

  SU_TRYCATCH(PTR_LIST_APPEND_CHECK(gui->profile, profile) != -1, goto fail);

  suscan_gui_profile_set_gui(profile, gui);

  suscan_gui_add_profile_widgets(gui, profile);

  return SU_TRUE;

fail:
  if (profile != NULL)
    suscan_gui_profile_destroy(profile);

  return SU_FALSE;
}

SUBOOL
suscan_gui_load_profiles(suscan_gui_t *gui)
{
  SU_TRYCATCH(
      suscan_source_config_walk(suscan_gui_append_profile, gui),
      return SU_FALSE);

  return SU_TRUE;
}
