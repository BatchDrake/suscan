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

#include "gui.h"

void
suscan_gui_recent_destroy(struct suscan_gui_recent *recent)
{
  if (recent->config != NULL)
    suscan_source_config_destroy(recent->config);

  if (recent->conf_string != NULL)
    free(recent->conf_string);

  free(recent);
}

struct suscan_gui_recent *
suscan_gui_recent_new(
    struct suscan_gui *gui,
    char *conf_string)
{
  struct suscan_gui_recent *new = NULL;

  SU_TRYCATCH(
      new = calloc(1, sizeof(struct suscan_gui_recent)),
      goto fail);

  SU_TRYCATCH(
      new->config = suscan_source_string_to_config(conf_string),
      goto fail);

  new->gui = gui;
  new->conf_string = conf_string;

  /* Dirty hack to pass configurations as strings */
  new->as_gui_config.source = new->config->source;
  new->as_gui_config.config = new->config;
  return new;

fail:
  if (new != NULL)
    suscan_gui_recent_destroy(new);

  return NULL;
}


void
suscan_gui_on_open_recent(GtkWidget *widget, gpointer *data)
{
  struct suscan_gui_recent *recent = (struct suscan_gui_recent *) data;

  suscan_gui_set_config(recent->gui, &recent->as_gui_config);
}

SUPRIVATE void
suscan_gui_update_recent_menu(struct suscan_gui *gui)
{
  GList *children, *iter;
  GtkWidget *item;
  unsigned int i;

  children = gtk_container_get_children(GTK_CONTAINER(gui->recentMenu));
  for (iter = children; iter != NULL; iter = g_list_next(iter))
    gtk_container_remove(
        GTK_CONTAINER(gui->recentMenu),
        GTK_WIDGET(iter->data)); /* GtkBuilder holds a ref to emptyMenuItem */
  g_list_free(children);

  if (gui->recent_count == 0) {
    /* Add empty menu item */
    gtk_container_add(
        GTK_CONTAINER(gui->recentMenu),
        GTK_WIDGET(gui->emptyMenuItem));
  } else {
    for (i = 0; i < gui->recent_count; ++i) {
      /* Create new entries */
      item = gtk_menu_item_new_with_label(gui->recent_list[i]->conf_string);
      gtk_widget_show(item);
      gtk_menu_shell_append(
          GTK_MENU_SHELL(gui->recentMenu),
          item);

      /* Set this recent entry to the new item */
      g_signal_connect(
          G_OBJECT(item),
          "activate",
          G_CALLBACK(suscan_gui_on_open_recent),
          gui->recent_list[i]);
    }
  }
}

SUBOOL
suscan_gui_append_recent(
    struct suscan_gui *gui,
    const struct suscan_source_config *config)
{
  SUBOOL ok = SU_FALSE;
  char *conf_string = NULL;
  unsigned int i;
  struct suscan_gui_recent *tmp;
  struct suscan_gui_recent *recent;

  SU_TRYCATCH(conf_string = suscan_source_config_to_string(config), goto done);

  /* Locate this recent */
  for (i = 0; i < gui->recent_count; ++i)
    if (strcmp(gui->recent_list[i]->conf_string, conf_string) == 0)
      break;

  if (i > 0 || gui->recent_count == 0) {
    /* Not found? Create a new entry */
    if (i == gui->recent_count) {
      SU_TRYCATCH(
          recent = suscan_gui_recent_new(gui, conf_string),
          goto done);
      conf_string = NULL;
      SU_TRYCATCH(
          PTR_LIST_APPEND_CHECK(gui->recent, recent) != -1,
          goto done);
      recent = NULL;
    }

    /* Move recent to the beginning */
    tmp = gui->recent_list[0];
    gui->recent_list[0] = gui->recent_list[i];
    gui->recent_list[i] = tmp;

    /* Update menu items */
    suscan_gui_update_recent_menu(gui);
  }

  ok = SU_TRUE;

done:
  if (conf_string != NULL)
    free(conf_string);

  if (recent != NULL)
    suscan_gui_recent_destroy(recent);

  return ok;
}
