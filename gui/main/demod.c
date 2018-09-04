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

  suscan_gui_demod_refresh_ui(gui);

  return SU_TRUE;
}

SUBOOL
suscan_gui_demod_remove(suscan_gui_t *gui, suscan_object_t *obj)
{
  if (suscan_config_context_remove(gui->demod_ctx, obj)) {
    suscan_gui_demod_refresh_ui(gui);
    return SU_TRUE;
  }

  return SU_FALSE;
}

/********************** Demodulator properties *************************/
SUPRIVATE SUBOOL
suscan_gui_demod_properties_refresh(
    suscan_gui_t *gui,
    const suscan_object_t *obj)
{
  unsigned int i, count;
   const suscan_object_t *params, *entry;
   GtkTreeIter new_element;

   SU_TRYCATCH(
       params = suscan_object_get_field(obj, "demod_params"),
       return SU_FALSE);

   gtk_list_store_clear(gui->demodPropertiesListStore);

   count = suscan_object_field_count(params);

   for (i = 0; i < count; ++i) {
     if ((entry = suscan_object_get_field_by_index(params, i)) != NULL) {
       gtk_list_store_append(gui->demodPropertiesListStore, &new_element);
       gtk_list_store_set(
           gui->demodPropertiesListStore,
           &new_element,
           0, suscan_object_get_name(entry),
           1, suscan_object_get_value(entry),
           -1);
     }
   }

   return SU_TRUE;
}

const char *
suscan_gui_show_demod_properties(suscan_gui_t *gui, const suscan_object_t *obj)
{
  const char *new_name = NULL;
  const char *label, *class;
  int result;

  if (!suscan_gui_demod_properties_refresh(gui, obj)) {
    suscan_error(
        gui,
        "Cannot show properties",
        "Somehow this demodulator is not properly initialized. Properties "
        "are not available");
    goto done;
  }

  if ((class = suscan_object_get_field_value(obj, "class")) == NULL)
    class = "<no class>";
  gtk_label_set_text(gui->demodClassLabel, class);

  if ((label = suscan_object_get_field_value(obj, "label")) == NULL)
    label = "";
  gtk_entry_set_text(gui->demodNameEntry, label);

  gtk_widget_show(GTK_WIDGET(gui->demodPropertiesDialog));

  result = gtk_dialog_run(gui->demodPropertiesDialog);

  gtk_widget_hide(GTK_WIDGET(gui->demodPropertiesDialog));

  if (result && strcmp(gtk_entry_get_text(gui->demodNameEntry), label) != 0)
    new_name = gtk_entry_get_text(gui->demodNameEntry);

done:
  return new_name;
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

SUPRIVATE void
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

SUPRIVATE void
suscan_gui_on_inspect_as(
    SuGtkSpectrum *spect,
    gsufloat freq,
    const struct sigutils_channel *channel,
    gpointer data)
{
  struct suscan_gui_spectrum_action *action =
      (struct suscan_gui_spectrum_action *) data;

  /* Send open message. We will open new tab on response */

  SU_TRYCATCH(
      suscan_analyzer_open_async(
          action->gui->analyzer,
          action->insp_iface->name,
          channel,
          action->index),
      return);
}

SUPRIVATE void
suscan_gui_demod_refresh_menus(suscan_gui_t *gui)
{
  unsigned int i, count;
  GtkMenu *menu = NULL;
  suscan_object_t *object;
  const char *label = NULL;
  const char *class = NULL;
  const struct suscan_inspector_interface *iface;
  struct suscan_gui_spectrum_action *action = NULL;

  count = suscan_object_set_get_count(gui->demod_obj);

  /* Clear this menu */
  gtk_menu_item_set_submenu(gui->demodMenuItem, NULL);

  for (i = 0; i < count; ++i) {
    object = suscan_object_set_get(gui->demod_obj, i);

    if (object != NULL
        && (label = suscan_object_get_field_value(object, "label")) != NULL
        && (class = suscan_object_get_field_value(object, "class")) != NULL
        && (iface = suscan_inspector_interface_lookup(class)) != NULL) {
      if (menu == NULL)
        menu = GTK_MENU(gtk_menu_new());

      SU_TRYCATCH(
          action = suscan_gui_assert_spectrum_action(gui, iface, object),
          continue);

      (void) sugtk_spectrum_add_action_to_menu(
          gui->spectrum,
          GTK_MENU_SHELL(menu),
          label,
          suscan_gui_on_inspect_as,
          action);
    }
  }

  /* This is intentional */
  gtk_menu_item_set_submenu(gui->demodMenuItem, GTK_WIDGET(menu));
  gtk_widget_set_sensitive(GTK_WIDGET(gui->demodMenuItem), menu != NULL);
}

SUPRIVATE void
suscan_gui_demod_refresh_settings_page(suscan_gui_t *gui)
{
  GtkTreePath *path;
  SUBOOL sensitive;

  path = gtk_tree_path_new_from_indices(0, -1);
  gtk_tree_selection_select_path(
      gtk_tree_view_get_selection(gui->demodListTreeView),
      path);
  gtk_tree_path_free(path);

  sensitive = gtk_tree_model_iter_n_children(
      GTK_TREE_MODEL(gui->demodulatorsListStore),
      NULL) != 0;

  gtk_widget_set_sensitive(GTK_WIDGET(gui->demodPropertiesButton), sensitive);
  gtk_widget_set_sensitive(GTK_WIDGET(gui->demodRemoveButton), sensitive);
}

void
suscan_gui_demod_refresh_ui(suscan_gui_t *gui)
{
  suscan_gui_demod_refresh_list_store(gui);
  suscan_gui_demod_refresh_menus(gui);
  suscan_gui_demod_refresh_settings_page(gui);
}
