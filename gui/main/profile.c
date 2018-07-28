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

/**************************** Profile selection ******************************/
SUBOOL
suscan_gui_select_profile(suscan_gui_t *gui, suscan_gui_profile_t *profile)
{
  const suscan_source_config_t *config = NULL;

  /* profile can be NULL */
  gui->active_profile = profile;

  if (profile == NULL) {
    SU_INFO("No profile selected\n");
  } else {
    config = suscan_gui_profile_get_source_config(profile);
    SU_INFO("Profile selected: %s\n", suscan_source_config_get_label(config));
  }

  suscan_gui_update_state(gui, gui->state);

  return SU_FALSE;
}

/************************** Profile selection menu ***************************/
void
suscan_gui_clear_profile_menu(suscan_gui_t *gui)
{
  unsigned int i;

  for (i = 0; i < gui->profileRadioButton_count; ++i)
    if (gui->profileRadioButton_list[i] != NULL)
      gtk_widget_destroy(GTK_WIDGET(gui->profileRadioButton_list[i]));

  if (gui->profileRadioButton_list != NULL)
    free(gui->profileRadioButton_list);

  gui->profileRadioButton_list = NULL;
  gui->profileRadioButton_count = 0;
}

SUPRIVATE void
suscan_gui_on_set_active_profile(GtkWidget *widget, gpointer data)
{
  suscan_gui_profile_t *profile = (suscan_gui_profile_t *) data;
  suscan_gui_t *gui = suscan_gui_profile_get_gui(profile);

  if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget)))
    (void) suscan_gui_select_profile(gui, profile);
}

SUPRIVATE SUBOOL
suscan_gui_update_profile_menu(suscan_gui_t *gui)
{
  unsigned int i;
  GtkRadioMenuItem *item = NULL;
  const suscan_source_config_t *config;
  GSList *group = NULL;
  suscan_gui_clear_profile_menu(gui);

  for (i = 0; i < gui->profile_count; ++i) {
    if (gui->profile_list[i] != NULL) {
      config = suscan_gui_profile_get_source_config(gui->profile_list[i]);
      item = GTK_RADIO_MENU_ITEM(gtk_radio_menu_item_new_with_label(
          group,
          suscan_source_config_get_label(config)));
      group = gtk_radio_menu_item_get_group(item);

      SU_TRYCATCH(
          PTR_LIST_APPEND_CHECK(gui->profileRadioButton, item) != -1,
          goto fail);

      gtk_menu_shell_append(
          GTK_MENU_SHELL(gui->profilesMenu),
          GTK_WIDGET(item));

      gtk_check_menu_item_set_active(
          GTK_CHECK_MENU_ITEM(item),
          gui->active_profile == gui->profile_list[i]);

      gtk_widget_show(GTK_WIDGET(item));

      g_signal_connect(
          item,
          "toggled",
          G_CALLBACK(suscan_gui_on_set_active_profile),
          gui->profile_list[i]);

      item = NULL;
    }
  }

  return SU_TRUE;

fail:
  if (item != NULL)
    gtk_widget_destroy(GTK_WIDGET(item));

  return SU_FALSE;
}

/**************************** Append profile GUIs  ***************************/
SUPRIVATE SUBOOL suscan_gui_append_profile(
    suscan_gui_t *gui,
    suscan_source_config_t *cfg);

SUPRIVATE SUBOOL suscan_gui_remove_profile(
    suscan_gui_t *gui,
    suscan_gui_profile_t *profile);

/* We can do this as this function is only called from the GUI thread */
SUPRIVATE const gchar *
suscan_gui_get_profile_name(suscan_gui_profile_t *profile)
{
  static char namebuf[32];

  snprintf(namebuf, sizeof(namebuf), "prof-0x%016lx", (unsigned long) profile);

  return namebuf;
}

SUPRIVATE SUBOOL
suscan_gui_on_rename_profile(suscan_gui_profile_t *profile, void *private)
{
  suscan_gui_t *gui = (suscan_gui_t *) private;
  const char *new_name;
  const char *original;

  original = suscan_source_config_get_label(
      suscan_gui_profile_get_source_config(profile));

  while ((new_name = suscan_gui_ask_for_profile_name(
      gui,
      "Rename profile",
      original)) != NULL && strcmp(new_name, original) != 0) {
    if (suscan_source_config_lookup(new_name) != NULL) {
      suscan_error(
          gui,
          "Profile name already in use", "Profile name `%s' is already in use. "
          "Please pick a different one.",
          new_name);
      continue;
    }

    SU_TRYCATCH(suscan_gui_profile_rename(profile, new_name), return SU_FALSE);
    SU_TRYCATCH(suscan_gui_update_profile_menu(gui), return SU_FALSE);

    break;
  }

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_gui_on_duplicate_profile(suscan_gui_profile_t *profile, void *private)
{
  suscan_gui_t *gui = (suscan_gui_t *) private;
  suscan_source_config_t *config;
  char *new_name = NULL;
  suscan_source_config_t *new_config = NULL;
  SUBOOL ok = SU_FALSE;

  config = suscan_gui_profile_get_source_config(profile);

  SU_TRYCATCH(
      new_name = suscan_gui_profile_helper_suggest_label(
          suscan_source_config_get_label(config)),
      goto done);

  SU_TRYCATCH(new_config = suscan_source_config_clone(config), goto done);

  SU_TRYCATCH(suscan_source_config_set_label(new_config, new_name), goto done);

  free(new_name);
  new_name = NULL;

  SU_TRYCATCH(suscan_source_config_register(new_config), goto done);

  config = new_config;
  new_config = NULL;

  SU_TRYCATCH(suscan_gui_append_profile(gui, config), goto done);

  ok = SU_TRUE;

done:
  if (new_name != NULL)
    free(new_name);

  if (new_config != NULL)
    suscan_source_config_destroy(new_config);

  return ok;
}

SUPRIVATE SUBOOL
suscan_gui_on_remove_profile(suscan_gui_profile_t *profile, void *private)
{
  suscan_gui_t *gui = (suscan_gui_t *) private;
  suscan_source_config_t *config;
  GtkWidget *parent;

  /*
   * Removal of a profile is delicate, and we must ensure that all references
   * to its source config are removed before destroying it..
   */

  /* Step 1: Remove from gui profile list */
  SU_TRYCATCH(suscan_gui_remove_profile(gui, profile), return SU_FALSE);

  /* Step 2: Destroy root widget and selector. */
  parent = gtk_widget_get_parent(suscan_gui_profile_get_selector(profile));
  gtk_widget_destroy(suscan_gui_profile_get_selector(profile));
  gtk_widget_destroy(suscan_gui_profile_get_root(profile));
  gtk_widget_destroy(parent); /* This is the ListBoxRow */

  /* Step 3: Remove configuration from global config list */
  config = suscan_gui_profile_get_source_config(profile);
  SU_TRYCATCH(suscan_source_config_unregister(config), return SU_FALSE);

  /* Step 4: Destroy profile object. */
  suscan_gui_profile_destroy(profile);

  return SU_TRUE;
}

SUPRIVATE void
suscan_gui_on_select_profile(GtkWidget *widget, gpointer data)
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
      G_CALLBACK(suscan_gui_on_select_profile),
      profile);

  widget = suscan_gui_profile_get_root(profile);
  gtk_stack_add_named(
      gui->settingsViewStack,
      widget,
      suscan_gui_get_profile_name(profile));
  gtk_widget_show(widget);
}

SUPRIVATE SUBOOL
suscan_gui_remove_profile(suscan_gui_t *gui, suscan_gui_profile_t *profile)
{
  unsigned int i;

  /* Current profile was destroyed, set active profile to NULL */
  if (gui->active_profile == profile)
    suscan_gui_select_profile(gui, NULL);

  for (i = 0; i < gui->profile_count; ++i)
    if (gui->profile_list[i] == profile) {
      gui->profile_list[i] = NULL;
      SU_TRYCATCH(suscan_gui_update_profile_menu(gui), return SU_FALSE);
      return SU_TRUE;
    }

  return SU_FALSE;
}

SUPRIVATE SUBOOL
suscan_gui_append_profile(suscan_gui_t *gui, suscan_source_config_t *cfg)
{
  suscan_gui_profile_t *profile = NULL;
  struct suscan_gui_profile_listeners listeners =
      suscan_gui_profile_listeners_INITIALIZER;

  SU_TRYCATCH(profile = suscan_gui_profile_new(cfg), goto fail);

  SU_TRYCATCH(PTR_LIST_APPEND_CHECK(gui->profile, profile) != -1, goto fail);

  suscan_gui_profile_set_gui(profile, gui);

  suscan_gui_add_profile_widgets(gui, profile);

  listeners.private = gui;
  listeners.on_rename = suscan_gui_on_rename_profile;
  listeners.on_duplicate = suscan_gui_on_duplicate_profile;
  listeners.on_remove = suscan_gui_on_remove_profile;

  suscan_gui_profile_set_listeners(profile, &listeners);

  profile = NULL;

  SU_TRYCATCH(suscan_gui_update_profile_menu(gui), goto fail);

  return SU_TRUE;

fail:
  if (profile != NULL)
    suscan_gui_profile_destroy(profile);

  return SU_FALSE;
}

SUPRIVATE SUBOOL
suscan_gui_append_on_profile(suscan_source_config_t *cfg, void *private)
{
  return suscan_gui_append_profile((suscan_gui_t *) private, cfg);
}

SUBOOL
suscan_gui_create_profile(suscan_gui_t *gui, const char *name)
{
  suscan_source_config_t *config = NULL;

  SU_TRYCATCH(
      config = suscan_source_config_new(
          SUSCAN_SOURCE_TYPE_SDR,
          SUSCAN_SOURCE_FORMAT_AUTO),
      goto fail);

  SU_TRYCATCH(suscan_source_config_set_label(config, name), goto fail);

  SU_TRYCATCH(suscan_source_config_register(config), goto fail);

  (void) suscan_gui_append_profile(gui, config);

  return SU_TRUE;

fail:
  if (config != NULL)
    suscan_source_config_destroy(config);

  return SU_FALSE;
}

void
suscan_gui_show_profile(suscan_gui_t *gui, suscan_gui_profile_t *profile)
{
  GtkWidget *parent;

  parent = gtk_widget_get_parent(suscan_gui_profile_get_selector(profile));

  if (parent != NULL)
    g_signal_emit_by_name(parent, "activate", 0, NULL);
}

void
suscan_gui_reset_all_profiles(suscan_gui_t *gui)
{
  unsigned int i;

  for (i = 0; i < gui->profile_count; ++i)
    if (gui->profile_list[i] != NULL) {
      if (suscan_gui_profile_has_changed(gui->profile_list[i])) {
        (void) suscan_gui_profile_refresh_gui(gui->profile_list[i]);
        suscan_gui_profile_reset_changed(gui->profile_list[i]);
      }
    }
}

SUBOOL
suscan_gui_parse_all_changed_profiles(suscan_gui_t *gui)
{
  unsigned int i;

  for (i = 0; i < gui->profile_count; ++i)
    if (gui->profile_list[i] != NULL)
      if (suscan_gui_profile_has_changed(gui->profile_list[i])) {
        if (!suscan_gui_profile_refresh_config(gui->profile_list[i])) {
          suscan_error(
              gui,
              "Failed to save profile",
              "Profile configuration has errors. Please review it and "
              "save it again, or discard changes.");

          suscan_gui_show_profile(gui, gui->profile_list[i]);

          return SU_FALSE;
        } else {
          /* Reset changed */
          suscan_gui_profile_reset_changed(gui->profile_list[i]);
        }
      }

  return SU_TRUE;
}

SUBOOL
suscan_gui_load_profiles(suscan_gui_t *gui)
{
  if (suscan_source_device_get_count() == 0) {
    suscan_warning(
        gui,
        "No SDR devices available",
        "No SDR devices have been found! However, you will still be able to "
        "work with file-like signal sources.\n\n"
        "If you think this is an error, please "
        "verify that all required SoapySDR modules have been properly "
        "installed.");
  }

  SU_TRYCATCH(
      suscan_source_config_walk(suscan_gui_append_on_profile, gui),
      return SU_FALSE);

  return SU_TRUE;
}
