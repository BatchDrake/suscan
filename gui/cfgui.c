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

#define SU_LOG_DOMAIN "cfgui"

#include "gui.h"

SUPRIVATE GtkWidget *
suscan_field_to_widget(
    const struct suscan_field *field,
    struct suscan_field_value *value)
{
  GtkWidget *widget = NULL;
  char text[64];

  switch (field->type) {
    case SUSCAN_FIELD_TYPE_STRING:
      SU_TRYCATCH(widget = gtk_entry_new(), goto done);
      gtk_entry_set_text(
          GTK_ENTRY(widget),
          value->as_string);
      break;

    case SUSCAN_FIELD_TYPE_FILE:
      SU_TRYCATCH(
          widget = gtk_file_chooser_button_new(
              "Browse...",
              GTK_FILE_CHOOSER_ACTION_OPEN),
          goto done);

      if (strlen(value->as_string) > 0)
        gtk_file_chooser_set_filename(
            GTK_FILE_CHOOSER(widget),
            value->as_string);
      break;

    case SUSCAN_FIELD_TYPE_BOOLEAN:
      SU_TRYCATCH(
          widget = gtk_check_button_new_with_label(field->desc),
          goto done);

      gtk_toggle_button_set_active(
          GTK_TOGGLE_BUTTON(widget),
          value->as_bool);
      break;

    case SUSCAN_FIELD_TYPE_INTEGER:
      SU_TRYCATCH(widget = gtk_entry_new(), goto done);
      gtk_entry_set_input_purpose(
          GTK_ENTRY(widget),
          GTK_INPUT_PURPOSE_DIGITS);

      snprintf(text, sizeof(text), "%lli", value->as_int);
      text[sizeof(text) - 1] = 0;
      gtk_entry_set_text(GTK_ENTRY(widget), text);

      break;

    case SUSCAN_FIELD_TYPE_FLOAT:
      SU_TRYCATCH(widget = gtk_entry_new(), goto done);
      gtk_entry_set_input_purpose(
          GTK_ENTRY(widget),
          GTK_INPUT_PURPOSE_NUMBER);

      snprintf(text, sizeof(text), "%lg", value->as_float);
      text[sizeof(text) - 1] = 0;
      gtk_entry_set_text(GTK_ENTRY(widget), text);

      break;
  }

done:
  if (widget != NULL)
    g_object_ref(G_OBJECT(widget));

  return widget;
}

GtkWidget *
suscan_gui_cfgui_get_root(const struct suscan_gui_cfgui *ui)
{
  return GTK_WIDGET(ui->grid);
}

SUBOOL
suscan_gui_cfgui_parse(struct suscan_gui_cfgui *ui)
{
  unsigned int i;
  uint64_t int_val;
  SUFLOAT float_val;
  const gchar *text = NULL;
  gchar *alloc = NULL;
  SUBOOL ok = SU_FALSE;

  for (i = 0; i < ui->config->desc->field_count; ++i) {
    switch (ui->config->desc->field_list[i]->type) {
      case SUSCAN_FIELD_TYPE_STRING:
        SU_TRYCATCH(
            text = gtk_entry_get_text(GTK_ENTRY(ui->widget_list[i])),
            goto done);

        SU_TRYCATCH(
            suscan_config_set_string(
                ui->config,
                ui->config->desc->field_list[i]->name,
                text),
            goto done);
        break;

      case SUSCAN_FIELD_TYPE_INTEGER:
        SU_TRYCATCH(
            text = gtk_entry_get_text(GTK_ENTRY(ui->widget_list[i])),
            goto done);

        if (sscanf(text, "%lli", &int_val) < 1)
          return SU_FALSE;

        SU_TRYCATCH(
            suscan_config_set_integer(
                ui->config,
                ui->config->desc->field_list[i]->name,
                int_val),
                goto done);

        break;

      case SUSCAN_FIELD_TYPE_FLOAT:
        SU_TRYCATCH(
            text = gtk_entry_get_text(GTK_ENTRY(ui->widget_list[i])),
            goto done);

        if (sscanf(text, SUFLOAT_FMT, &float_val) < 1)
          return SU_FALSE;

        SU_TRYCATCH(
            suscan_config_set_float(
                ui->config,
                ui->config->desc->field_list[i]->name,
                float_val),
            goto done);

        break;

      case SUSCAN_FIELD_TYPE_BOOLEAN:
        SU_TRYCATCH(
            suscan_config_set_bool(
                ui->config,
                ui->config->desc->field_list[i]->name,
                gtk_toggle_button_get_active(
                    GTK_TOGGLE_BUTTON(ui->widget_list[i]))),
            goto done);

        break;

      case SUSCAN_FIELD_TYPE_FILE:
        SU_TRYCATCH(
            alloc = gtk_file_chooser_get_filename(
                GTK_FILE_CHOOSER(ui->widget_list[i])),
            goto done);

        text = alloc;

        SU_TRYCATCH(
            suscan_config_set_file(
                ui->config,
                ui->config->desc->field_list[i]->name,
                text),
            goto done);
        break;
    }
  }

  ok = SU_TRUE;

done:
  if (alloc != NULL)
    g_free(alloc);

  return ok;
}

void
suscan_gui_cfgui_dump(struct suscan_gui_cfgui *ui)
{
  unsigned int i;
  char textbuf[32];
  const char *str;

  for (i = 0; i < ui->config->desc->field_count; ++i) {
    switch (ui->config->desc->field_list[i]->type) {
      case SUSCAN_FIELD_TYPE_STRING:
        if ((str = ui->config->values[i]->as_string) == NULL)
          str = "";

        gtk_entry_set_text(GTK_ENTRY(ui->widget_list[i]), str);
        break;

      case SUSCAN_FIELD_TYPE_INTEGER:
        suscan_gui_text_entry_set_integer(
            GTK_ENTRY(ui->widget_list[i]),
            ui->config->values[i]->as_int);
        break;

      case SUSCAN_FIELD_TYPE_FLOAT:
        suscan_gui_text_entry_set_float(
            GTK_ENTRY(ui->widget_list[i]),
            ui->config->values[i]->as_float);
        break;

      case SUSCAN_FIELD_TYPE_BOOLEAN:
        gtk_toggle_button_set_active(
            GTK_TOGGLE_BUTTON(ui->widget_list[i]),
            ui->config->values[i]->as_bool);
        break;

      case SUSCAN_FIELD_TYPE_FILE:
        if (ui->config->values[i]->as_string != NULL) {
          gtk_file_chooser_set_filename(
              GTK_FILE_CHOOSER(ui->widget_list[i]),
              ui->config->values[i]->as_string);
        }
        break;
    }
  }
}

void
suscan_gui_cfgui_destroy(struct suscan_gui_cfgui *ui)
{
  unsigned int i;

  if (ui->grid != NULL)
    gtk_widget_destroy(GTK_WIDGET(ui->grid));

  for (i = 0; i < ui->widget_count; ++i)
    gtk_widget_destroy(ui->widget_list[i]);

  if (ui->widget_list != NULL)
    free(ui->widget_list);

  free(ui);
}

struct suscan_gui_cfgui *
suscan_gui_cfgui_new(suscan_config_t *config)
{
  struct suscan_gui_cfgui *new = NULL;
  GtkWidget *widget = NULL;
  GtkWidget *label = NULL;
  unsigned int i;

  SU_TRYCATCH(
      new = calloc(1, sizeof(struct suscan_gui_cfgui)),
      goto fail);

  SU_TRYCATCH(new->grid = GTK_GRID(gtk_grid_new()), goto fail);

  new->config = config; /* Borrow */

  g_object_ref(G_OBJECT(new->grid));

  gtk_grid_insert_column(new->grid, 0);
  gtk_grid_insert_column(new->grid, 1);
  gtk_widget_set_hexpand(GTK_WIDGET(new->grid), TRUE);

  for (i = 0; i < config->desc->field_count; ++i) {
    SU_TRYCATCH(
        widget = suscan_field_to_widget(
            config->desc->field_list[i],
            new->config->values[i]),
        goto fail);

    SU_TRYCATCH(PTR_LIST_APPEND_CHECK(new->widget, widget) != -1, goto fail);

    /* Arrange widget in grid */
    gtk_grid_insert_row(new->grid, i);

    if (config->desc->field_list[i]->type != SUSCAN_FIELD_TYPE_BOOLEAN) {
      SU_TRYCATCH(
          label = gtk_label_new(config->desc->field_list[i]->desc),
          goto fail);
      gtk_label_set_xalign(GTK_LABEL(label), 0);
    }

    if (label != NULL) {
      gtk_grid_attach(new->grid, label, 0, i, 1, 1);
      gtk_grid_attach(new->grid, widget, 1, i, 1, 1);
      gtk_widget_set_margin_start(label, 4);
      gtk_widget_set_margin_end(label, 4);
      gtk_widget_set_margin_bottom(label, 4);
      gtk_widget_show(label);
      label = NULL; /* Drop ownership */
    } else {
      gtk_grid_attach(new->grid, widget, 0, i, 2, 1);
    }

    gtk_widget_set_margin_start(widget, 4);
    gtk_widget_set_margin_end(widget, 4);
    gtk_widget_set_margin_bottom(widget, 4);

    gtk_widget_set_hexpand(widget, TRUE);
    gtk_widget_show(widget);
    widget = NULL; /* Drop ownership */
  }

  return new;

fail:
  if (new != NULL)
    suscan_gui_cfgui_destroy(new);

  if (widget != NULL)
    gtk_widget_destroy(widget);

  if (label != NULL)
    gtk_widget_destroy(label);

  return NULL;
}
