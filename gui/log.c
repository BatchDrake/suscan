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

#include <ctype.h>
#include <string.h>
#include "gui.h"

struct suscan_log_message_envelope {
  suscan_gui_t *gui;
  struct sigutils_log_message *msg;
};

void
suscan_log_message_envelope_destroy(struct suscan_log_message_envelope *env)
{
  if (env->msg != NULL)
    sigutils_log_message_destroy(env->msg);

  free(env);
}

struct suscan_log_message_envelope *
suscan_log_message_envelope_new(
    suscan_gui_t *gui,
    const struct sigutils_log_message *msg)
{
  struct suscan_log_message_envelope *new = NULL;

  if ((new = calloc(1, sizeof (struct suscan_log_message_envelope))) == NULL)
    goto fail;

  if ((new->msg = sigutils_log_message_dup(msg)) == NULL)
    goto fail;

  new->gui = gui;

  return new;

fail:
  if (new != NULL)
    suscan_log_message_envelope_destroy(new);

  return NULL;
}

SUPRIVATE gboolean
suscan_log_new_message_cb(gpointer user_data)
{
  struct suscan_log_message_envelope *env =
      (struct suscan_log_message_envelope *) user_data;
  const char *icon_name;
  GdkPixbuf *pixbuf = NULL;
  GtkIconTheme *theme = NULL;
  GtkTreeModel *model = NULL;
  GtkTreePath *path = NULL;
  GtkTreeIter new_element;
  char *msg_dup = NULL;
  int i;
  time_t date;
  char str_date[32];

  if ((msg_dup = strdup(env->msg->message)) == NULL)
    goto done;

  i = strlen(msg_dup) - 1;
  while (i >= 0 && isspace(msg_dup[i]))
    msg_dup[i--] = '\0';

  switch (env->msg->severity) {
    case SU_LOG_SEVERITY_CRITICAL:
      icon_name = "dialog-cancel";
      break;

    case SU_LOG_SEVERITY_ERROR:
      icon_name = "dialog-error";
      break;

    case SU_LOG_SEVERITY_WARNING:
      icon_name = "dialog-warning";
      break;

    case SU_LOG_SEVERITY_INFO:
      icon_name = "dialog-information";
      break;

    case SU_LOG_SEVERITY_DEBUG:
      icon_name = "document-properties";
      break;

    default:
      icon_name = "dialog-question";
  }

  if ((theme = gtk_icon_theme_get_default()) == NULL)
    goto done;

  if ((pixbuf = gtk_icon_theme_load_icon(
      theme,
      icon_name,
      16,
      GTK_ICON_LOOKUP_FORCE_SIZE,
      NULL)) == NULL)
    goto done;

  date = env->msg->time.tv_sec;
  strncpy(str_date, ctime(&date), sizeof (str_date));
  str_date[strlen(str_date) - 1] = '\0'; /* Remove \n */

  gtk_list_store_append(
      env->gui->logMessagesListStore,
      &new_element);
  gtk_list_store_set(
      env->gui->logMessagesListStore,
      &new_element,
      0, str_date,
      1, pixbuf,
      2, env->msg->domain,
      3, msg_dup,
      -1);

  model = gtk_tree_view_get_model(env->gui->logMessagesTreeView);
  if ((path = gtk_tree_model_get_path(model, &new_element)) == NULL)
    goto done;

  gtk_tree_view_scroll_to_cell(
      env->gui->logMessagesTreeView,
      path,
      NULL,
      FALSE,
      0.0,
      0.0);

done:
  if (path != NULL)
    gtk_tree_path_free(path);

  if (msg_dup != NULL)
    free(msg_dup);

  g_object_unref(G_OBJECT(pixbuf));

  suscan_log_message_envelope_destroy(env);

  return G_SOURCE_REMOVE;
}

SUPRIVATE void
suscan_gui_log_func(void *private, const struct sigutils_log_message *logmsg)
{
  struct suscan_log_message_envelope *env;
  suscan_gui_t *gui = (suscan_gui_t *) private;

  if ((env = suscan_log_message_envelope_new(gui, logmsg)) != NULL)
    g_idle_add(suscan_log_new_message_cb, env);
}

void
suscan_gui_setup_logging(suscan_gui_t *gui)
{
  struct sigutils_log_config config = sigutils_log_config_INITIALIZER;

  /* Log messages will be emitted from the same thread, always */
  config.exclusive = SU_FALSE;
  config.log_func = suscan_gui_log_func;
  config.private = gui;

  su_log_init(&config);
}
