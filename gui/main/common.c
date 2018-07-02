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

#define SU_LOG_DOMAIN "gui-common"

#include <confdb.h>
#include "modemctl.h"
#include "gui.h"

void
suscan_gui_msgbox(
    suscan_gui_t *gui,
    GtkMessageType type,
    const char *title,
    const char *fmt,
    ...)
{
  va_list ap;
  char *message;
  GtkWidget *dialog;

  va_start(ap, fmt);

  if ((message = vstrbuild(fmt, ap)) != NULL) {
    dialog = gtk_message_dialog_new(
        gui->main,
        GTK_DIALOG_DESTROY_WITH_PARENT,
        type,
        GTK_BUTTONS_CLOSE,
        "%s",
        message);

    gtk_window_set_title(GTK_WINDOW(dialog), title);

    gtk_dialog_run(GTK_DIALOG(dialog));

    gtk_widget_destroy(dialog);

    free(message);
  }

  va_end(ap);
}

void
suscan_gui_text_entry_set_float(GtkEntry *entry, SUFLOAT value)
{
  char buffer[30];

  buffer[29] = '\0';

  snprintf(buffer, 29, "%g", value);

  gtk_entry_set_text(entry, buffer);
}

void
suscan_gui_text_entry_set_freq(GtkEntry *entry, SUFREQ value)
{
  char buffer[30];

  buffer[29] = '\0';

  snprintf(buffer, 29, "%lg", value);

  gtk_entry_set_text(entry, buffer);
}

void
suscan_gui_text_entry_set_scount(GtkEntry *entry, SUSCOUNT value)
{
  char buffer[30];

  buffer[29] = '\0';

  snprintf(buffer, 29, "%lu", value);

  gtk_entry_set_text(entry, buffer);
}

void
suscan_gui_text_entry_set_integer(GtkEntry *entry, int64_t value)
{
  char buffer[30];

  buffer[29] = '\0';

  snprintf(buffer, 29, "%lli", value);

  gtk_entry_set_text(entry, buffer);
}

SUBOOL
suscan_gui_text_entry_get_float(GtkEntry *entry, SUFLOAT *result)
{
  const gchar *text = NULL;

  SU_TRYCATCH(
      text = gtk_entry_get_text(entry),
      return FALSE);

  if (sscanf(text, SUFLOAT_SCANF_FMT, result) < 1)
    return FALSE;

  return TRUE;
}

SUBOOL
suscan_gui_text_entry_get_freq(GtkEntry *entry, SUFREQ *result)
{
  const gchar *text = NULL;

  SU_TRYCATCH(
      text = gtk_entry_get_text(entry),
      return FALSE);

  if (sscanf(text, "%lg", result) < 1)
    return FALSE;

  return TRUE;
}

SUBOOL
suscan_gui_text_entry_get_scount(GtkEntry *entry, SUSCOUNT *result)
{
  const gchar *text = NULL;

  SU_TRYCATCH(
      text = gtk_entry_get_text(entry),
      return FALSE);

  if (sscanf(text, "%lu", result) < 1)
    return FALSE;

  return TRUE;
}


SUBOOL
suscan_gui_text_entry_get_integer(GtkEntry *entry, int64_t *result)
{
  const gchar *text = NULL;

  SU_TRYCATCH(
      text = gtk_entry_get_text(entry),
      return FALSE);

  if (sscanf(text, "%lli", result) < 1)
    return FALSE;

  return TRUE;
}
