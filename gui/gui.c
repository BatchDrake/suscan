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

PTR_LIST_EXTERN(struct suscan_source, source); /* Declared in source.c */

void
suscan_gui_msgbox(
    struct suscan_gui *gui,
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
    dialog = gtk_message_dialog_new (gui->main,
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
suscan_gui_destroy(struct suscan_gui *gui)
{
  free(gui);
}

void
suscan_gui_source_config_destroy(struct suscan_gui_source_config *config)
{
  unsigned int i;

  if (config->config != NULL)
    suscan_source_config_destroy(config->config);

  if (config->grid != NULL)
    gtk_widget_destroy(GTK_WIDGET(config->grid));

  for (i = 0; i < config->widget_count; ++i)
    gtk_widget_destroy(config->widget_list[i]);

  if (config->widget_list != NULL)
    free(config->widget_list);

  free(config);
}

GtkWidget *
suscan_gui_source_config_to_widget(
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

SUBOOL
suscan_gui_source_config_parse(struct suscan_gui_source_config *config)
{
  unsigned int i;
  uint64_t int_val;
  SUFLOAT float_val;
  const gchar *text = NULL;
  gchar *alloc = NULL;
  SUBOOL ok = SU_FALSE;

  for (i = 0; i < config->source->field_count; ++i) {
    switch (config->source->field_list[i]->type) {
      case SUSCAN_FIELD_TYPE_STRING:
        SU_TRYCATCH(
            text = gtk_entry_get_text(GTK_ENTRY(config->widget_list[i])),
            goto done);

        SU_TRYCATCH(
            suscan_source_config_set_string(
                config->config,
                config->source->field_list[i]->name,
                text),
            goto done);
        break;

      case SUSCAN_FIELD_TYPE_INTEGER:
        SU_TRYCATCH(
            text = gtk_entry_get_text(GTK_ENTRY(config->widget_list[i])),
            goto done);

        if (sscanf(text, "%lli", &int_val) < 1)
          return SU_FALSE;

        SU_TRYCATCH(
            suscan_source_config_set_integer(
                config->config,
                config->source->field_list[i]->name,
                int_val),
                goto done);

        break;

      case SUSCAN_FIELD_TYPE_FLOAT:
        SU_TRYCATCH(
            text = gtk_entry_get_text(GTK_ENTRY(config->widget_list[i])),
            goto done);

        if (sscanf(text, SUFLOAT_FMT, &float_val) < 1)
          return SU_FALSE;

        SU_TRYCATCH(
            suscan_source_config_set_float(
                config->config,
                config->source->field_list[i]->name,
                float_val),
            goto done);

        break;

      case SUSCAN_FIELD_TYPE_BOOLEAN:
        SU_TRYCATCH(
            suscan_source_config_set_bool(
                config->config,
                config->source->field_list[i]->name,
                gtk_toggle_button_get_active(
                    GTK_TOGGLE_BUTTON(config->widget_list[i]))),
            goto done);

        break;

      case SUSCAN_FIELD_TYPE_FILE:
        SU_TRYCATCH(
            alloc = gtk_file_chooser_get_filename(
                GTK_FILE_CHOOSER(config->widget_list[i])),
            goto done);

        text = alloc;

        SU_TRYCATCH(
            suscan_source_config_set_file(
                config->config,
                config->source->field_list[i]->name,
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

struct suscan_gui_source_config *
suscan_gui_source_config_new(struct suscan_source *source)
{
  struct suscan_gui_source_config *new = NULL;
  GtkWidget *widget = NULL;
  GtkWidget *label = NULL;
  unsigned int i;

  SU_TRYCATCH(
      new = calloc(1, sizeof(struct suscan_gui_source_config)),
      goto fail);

  new->source = source;

  SU_TRYCATCH(new->config = suscan_source_config_new(source), goto fail);

  SU_TRYCATCH(new->grid = GTK_GRID(gtk_grid_new()), goto fail);

  g_object_ref(G_OBJECT(new->grid));

  gtk_grid_insert_column(new->grid, 0);
  gtk_grid_insert_column(new->grid, 1);
  gtk_widget_set_hexpand(GTK_WIDGET(new->grid), TRUE);

  for (i = 0; i < new->config->source->field_count; ++i) {
    SU_TRYCATCH(
        widget = suscan_gui_source_config_to_widget(
            source->field_list[i],
            new->config->values[i]),
        goto fail);

    SU_TRYCATCH(PTR_LIST_APPEND_CHECK(new->widget, widget) != -1, goto fail);

    /* Arrange widget in grid */
    gtk_grid_insert_row(new->grid, i);

    if (source->field_list[i]->type != SUSCAN_FIELD_TYPE_BOOLEAN) {
      SU_TRYCATCH(
          label = gtk_label_new(source->field_list[i]->desc),
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
    suscan_gui_source_config_destroy(new);

  if (widget != NULL)
    gtk_widget_destroy(widget);

  if (label != NULL)
    gtk_widget_destroy(label);

  return new;
}

SUBOOL
suscan_gui_populate_source_list(struct suscan_gui *gui)
{
  unsigned int i;
  GtkTreeIter new_element;
  struct suscan_gui_source_config *config;

  for (i = 0; i < source_count; ++i) {
    SU_TRYCATCH(
        config = suscan_gui_source_config_new(source_list[i]),
        return SU_FALSE);
    gtk_list_store_append(
        gui->sourceListStore,
        &new_element);
    gtk_list_store_set(
        gui->sourceListStore,
        &new_element,
        0, source_list[i]->desc,
        1, config,
        -1);
  }

  return SU_TRUE;
}

SUPRIVATE void
suscan_gui_double_data_func(
    GtkTreeViewColumn *tree_column,
    GtkCellRenderer *cell,
    GtkTreeModel *model,
    GtkTreeIter *iter,
    gpointer data)
{
  const char *fmt = data;
  gdouble double_val;
  GValue val = G_VALUE_INIT;
  char as_string[32];

  gtk_tree_model_get_value(model, iter, (long) data, &val);

  double_val = g_value_get_double(&val);

  snprintf(as_string, sizeof(as_string), "%.1lf", double_val);

  g_object_set(G_OBJECT(cell), "text", as_string, NULL);

  g_value_unset(&val);
}

void
suscan_setup_column_formats(struct suscan_gui *gui)
{
  gtk_tree_view_column_set_cell_data_func(
      gui->centerFrequencyCol,
      GTK_CELL_RENDERER(gui->centerFrequencyCellRenderer),
      suscan_gui_double_data_func,
      (gpointer *) 0,
      NULL);

  gtk_tree_view_column_set_cell_data_func(
      gui->snrCol,
      GTK_CELL_RENDERER(gui->snrCellRenderer),
      suscan_gui_double_data_func,
      (gpointer *) 1,
      NULL);

  gtk_tree_view_column_set_cell_data_func(
      gui->signalLevelCol,
      GTK_CELL_RENDERER(gui->signalLevelCellRenderer),
      suscan_gui_double_data_func,
      (gpointer *) 2,
      NULL);

  gtk_tree_view_column_set_cell_data_func(
      gui->noiseLevelCol,
      GTK_CELL_RENDERER(gui->noiseLevelCellRenderer),
      suscan_gui_double_data_func,
      (gpointer *) 3,
      NULL);

  gtk_tree_view_column_set_cell_data_func(
      gui->bandwidthCol,
      GTK_CELL_RENDERER(gui->bandwidthCellRenderer),
      suscan_gui_double_data_func,
      (gpointer *) 4,
      NULL);

}

SUBOOL
suscan_gui_load_all_widgets(struct suscan_gui *gui)
{
  SU_TRYCATCH(
      gui->main = GTK_WINDOW(gtk_builder_get_object(gui->builder, "wMain")),
      return SU_FALSE);

  SU_TRYCATCH(
      gui->headerBar = GTK_HEADER_BAR(
          gtk_builder_get_object(gui->builder, "HeaderBar")),
      return SU_FALSE);

  gtk_window_set_titlebar(gui->main, GTK_WIDGET(gui->headerBar));

  SU_TRYCATCH(
      gui->sourceListStore = GTK_LIST_STORE(
          gtk_builder_get_object(gui->builder, "lsSourceListStore")),
      return SU_FALSE);

  SU_TRYCATCH(
      gui->settingsDialog = GTK_DIALOG(
          gtk_builder_get_object(gui->builder, "dlSettings")),
      return SU_FALSE);

  SU_TRYCATCH(
      gui->aboutDialog = GTK_DIALOG(
          gtk_builder_get_object(gui->builder, "dlAbout")),
      return SU_FALSE);

  SU_TRYCATCH(
      gui->sourceCombo = GTK_COMBO_BOX(
          gtk_builder_get_object(gui->builder, "cmSourceSelect")),
      return SU_FALSE);

  SU_TRYCATCH(
      gui->sourceAlignment =
          gtk_builder_get_object(gui->builder, "alSourceParams"),
      return SU_FALSE);

  SU_TRYCATCH(
      gui->toggleConnect =
          GTK_BUTTON(gtk_builder_get_object(gui->builder, "bToggleConnect")),
      return SU_FALSE);

  SU_TRYCATCH(
      gui->freqLabels[0] =
          GTK_LABEL(gtk_builder_get_object(
              gui->builder,
              "lMainViewsSummaryFreq0")),
      return SU_FALSE);

  SU_TRYCATCH(
      gui->freqLabels[1] =
          GTK_LABEL(gtk_builder_get_object(
              gui->builder,
              "lMainViewsSummaryFreq1")),
      return SU_FALSE);

  SU_TRYCATCH(
      gui->freqLabels[2] =
          GTK_LABEL(gtk_builder_get_object(
              gui->builder,
              "lMainViewsSummaryFreq2")),
      return SU_FALSE);

  SU_TRYCATCH(
      gui->freqLabels[3] =
          GTK_LABEL(gtk_builder_get_object(
              gui->builder,
              "lMainViewsSummaryFreq3")),
      return SU_FALSE);

  SU_TRYCATCH(
      gui->freqLabels[4] =
          GTK_LABEL(gtk_builder_get_object(
              gui->builder,
              "lMainViewsSummaryFreq4")),
      return SU_FALSE);

  SU_TRYCATCH(
      gui->freqLabels[5] =
          GTK_LABEL(gtk_builder_get_object(
              gui->builder,
              "lMainViewsSummaryFreq5")),
      return SU_FALSE);

  SU_TRYCATCH(
      gui->freqLabels[6] =
          GTK_LABEL(gtk_builder_get_object(
              gui->builder,
              "lMainViewsSummaryFreq6")),
      return SU_FALSE);

  SU_TRYCATCH(
      gui->freqLabels[7] =
          GTK_LABEL(gtk_builder_get_object(
              gui->builder,
              "lMainViewsSummaryFreq7")),
      return SU_FALSE);

  SU_TRYCATCH(
      gui->freqLabels[8] =
          GTK_LABEL(gtk_builder_get_object(
              gui->builder,
              "lMainViewsSummaryFreq8")),
      return SU_FALSE);

  SU_TRYCATCH(
      gui->freqLabels[9] =
          GTK_LABEL(gtk_builder_get_object(
              gui->builder,
              "lMainViewsSummaryFreq9")),
      return SU_FALSE);

  SU_TRYCATCH(
      gui->preferencesButton =
          GTK_BUTTON(gtk_builder_get_object(
              gui->builder,
              "bPreferences")),
      return SU_FALSE);

  SU_TRYCATCH(
      gui->cpuLabel =
          GTK_LABEL(gtk_builder_get_object(
              gui->builder,
              "lCpu")),
      return SU_FALSE);

  SU_TRYCATCH(
      gui->cpuLevelBar =
          GTK_LEVEL_BAR(gtk_builder_get_object(
              gui->builder,
              "lbCpu")),
      return SU_FALSE);

  SU_TRYCATCH(
      gui->n0Label =
          GTK_LABEL(gtk_builder_get_object(
              gui->builder,
              "lN0")),
      return SU_FALSE);

  SU_TRYCATCH(
      gui->n0LevelBar =
          GTK_LEVEL_BAR(gtk_builder_get_object(
              gui->builder,
              "lbN0")),
      return SU_FALSE);

  SU_TRYCATCH(
      gui->channelListStore =
          GTK_LIST_STORE(gtk_builder_get_object(
              gui->builder,
              "lsMainChannelListStore")),
      return SU_FALSE);

  SU_TRYCATCH(
      gui->centerFrequencyCol =
          GTK_TREE_VIEW_COLUMN(gtk_builder_get_object(
              gui->builder,
              "cCenterFrequency")),
      return SU_FALSE);

  SU_TRYCATCH(
      gui->snrCol =
          GTK_TREE_VIEW_COLUMN(gtk_builder_get_object(
              gui->builder,
              "cSNR")),
      return SU_FALSE);

  SU_TRYCATCH(
      gui->signalLevelCol =
          GTK_TREE_VIEW_COLUMN(gtk_builder_get_object(
              gui->builder,
              "cSignalLevel")),
      return SU_FALSE);

  SU_TRYCATCH(
      gui->noiseLevelCol =
          GTK_TREE_VIEW_COLUMN(gtk_builder_get_object(
              gui->builder,
              "cNoiseLevel")),
      return SU_FALSE);

  SU_TRYCATCH(
      gui->bandwidthCol =
          GTK_TREE_VIEW_COLUMN(gtk_builder_get_object(
              gui->builder,
              "cBandwidth")),
      return SU_FALSE);

  SU_TRYCATCH(
      gui->centerFrequencyCellRenderer =
          GTK_CELL_RENDERER_TEXT(gtk_builder_get_object(
              gui->builder,
              "crCenterFrequency")),
      return SU_FALSE);

  SU_TRYCATCH(
      gui->snrCellRenderer =
          GTK_CELL_RENDERER_TEXT(gtk_builder_get_object(
              gui->builder,
              "crSNR")),
      return SU_FALSE);

  SU_TRYCATCH(
      gui->signalLevelCellRenderer =
          GTK_CELL_RENDERER_TEXT(gtk_builder_get_object(
              gui->builder,
              "crSignalLevel")),
      return SU_FALSE);

  SU_TRYCATCH(
      gui->noiseLevelCellRenderer =
          GTK_CELL_RENDERER_TEXT(gtk_builder_get_object(
              gui->builder,
              "crNoiseLevel")),
      return SU_FALSE);

  SU_TRYCATCH(
      gui->bandwidthCellRenderer =
          GTK_CELL_RENDERER_TEXT(gtk_builder_get_object(
              gui->builder,
              "crBandwidth")),
      return SU_FALSE);

  SU_TRYCATCH(
      gui->spectrumSampleRate =
          GTK_LABEL(gtk_builder_get_object(
              gui->builder,
              "lSpectrumSampleRate")),
      return SU_FALSE);

  SU_TRYCATCH(
      gui->spectrumDbsPerDivLabel =
          GTK_LABEL(gtk_builder_get_object(
              gui->builder,
              "lSpectrumDbsPerDiv")),
      return SU_FALSE);

  SU_TRYCATCH(
      gui->spectrumRefLevelLabel =
          GTK_LABEL(gtk_builder_get_object(
              gui->builder,
              "lSpectrumRefLevel")),
          return SU_FALSE);

  SU_TRYCATCH(
      gui->spectrumFreqScaleLabel =
          GTK_LABEL(gtk_builder_get_object(
              gui->builder,
              "lSpectrumFreqScale")),
          return SU_FALSE);

  SU_TRYCATCH(
      gui->spectrumFreqOffsetLabel =
          GTK_LABEL(gtk_builder_get_object(
            gui->builder,
            "lSpectrumFreqOffset")),
          return SU_FALSE);

  SU_TRYCATCH(
      gui->spectrumShowChannelsCheck =
          GTK_CHECK_BUTTON(gtk_builder_get_object(
            gui->builder,
            "chShowChannels")),
          return SU_FALSE);

  SU_TRYCATCH(
      gui->channelMenu =
          GTK_MENU(gtk_builder_get_object(
            gui->builder,
            "mChannel")),
          return SU_FALSE);

  SU_TRYCATCH(
      gui->channelHeaderMenuItem =
          GTK_MENU_ITEM(gtk_builder_get_object(
            gui->builder,
            "miChannelHeader")),
          return SU_FALSE);

  SU_TRYCATCH(
      gui->openInspectorMenuItem =
          GTK_MENU_ITEM(gtk_builder_get_object(
            gui->builder,
            "miOpenInspector")),
          return SU_FALSE);

  SU_TRYCATCH(
      gui->analyzerViewsNotebook =
          GTK_NOTEBOOK(gtk_builder_get_object(
            gui->builder,
            "nbAnalyzerViews")),
          return SU_FALSE);

  SU_TRYCATCH(
      gui->logMessagesListStore =
          GTK_LIST_STORE(gtk_builder_get_object(
            gui->builder,
            "lsLogMessages")),
          return SU_FALSE);

  SU_TRYCATCH(
      gui->logMessagesTreeView =
          GTK_TREE_VIEW(gtk_builder_get_object(
              gui->builder,
              "tvLogMessages")),
          return SU_FALSE);



  suscan_gui_populate_source_list(gui);

  suscan_setup_column_formats(gui);

  gtk_combo_box_set_active(gui->sourceCombo, 0);

  return SU_TRUE;
}

/************************ Inspector handling methods *************************/
SUBOOL
suscan_gui_remove_inspector(
    struct suscan_gui *gui,
    struct suscan_gui_inspector *insp)
{
  unsigned int i;

  if (insp->index < 0 || insp->index >= gui->inspector_count)
    return SU_FALSE;

  SU_TRYCATCH(gui->inspector_list[insp->index] == insp, return SU_FALSE);
  gtk_notebook_remove_page(gui->analyzerViewsNotebook, insp->page);
  /*
   * Substract one from all pages greater than insp->page. We do this
   * because GTK sucks at handling notebook pages
   */
  for (i = 0; i < gui->inspector_count; ++i)
    if (gui->inspector_list[i] != NULL)
      if (gui->inspector_list[i]->page > insp->page)
        --gui->inspector_list[i]->page;

  gui->inspector_list[insp->index] = NULL;
  insp->index = -1;
  insp->page = -1;
  insp->gui = NULL;
  return SU_TRUE;
}

SUBOOL
suscan_gui_add_inspector(
    struct suscan_gui *gui,
    struct suscan_gui_inspector *insp)
{
  struct suscan_inspector_params params = suscan_inspector_params_INITIALIZER;
  SUBOOL inspector_added = SU_FALSE;

  SU_TRYCATCH(
      (insp->index = PTR_LIST_APPEND_CHECK(gui->inspector, insp)) != -1,
      goto fail);

  inspector_added = SU_TRUE;
  insp->gui = gui;

  SU_TRYCATCH(
      (insp->page = gtk_notebook_append_page_menu(
          gui->analyzerViewsNotebook,
          GTK_WIDGET(insp->channelInspectorGrid),
          GTK_WIDGET(insp->pageLabelEventBox),
          NULL)) >= 0,
      goto fail);

  gtk_notebook_set_current_page(gui->analyzerViewsNotebook, insp->page);

  /*
   * Page added. Set initial params. Interface will be unlocked as soon
   * as we received the response of this message
   */
  params.inspector_id = insp->index;
  insp->params = params;

  SU_TRYCATCH(
      suscan_inspector_set_params_async(
          gui->analyzer,
          insp->inshnd,
          &params,
          rand()),
      goto fail);

  return TRUE;

fail:
  if (inspector_added)
    (void) suscan_gui_remove_inspector(gui, insp);

  return FALSE;
}

struct suscan_gui_inspector *
suscan_gui_get_inspector(const struct suscan_gui *gui, uint32_t inspector_id)
{
  if (inspector_id >= gui->inspector_count)
    return NULL;

  return gui->inspector_list[inspector_id];
}

SUPRIVATE void
suscan_quit_cb(GtkWidget *obj, gpointer u_data)
{
  gtk_main_quit();
}

struct suscan_gui *
suscan_gui_new(int argc, char **argv)
{
  struct suscan_gui *gui = NULL;
  GError *err;

  gtk_init(&argc, &argv);

  SU_TRYCATCH(gui = calloc(1, sizeof(struct suscan_gui)), goto fail);

  SU_TRYCATCH(
      gui->builder = gtk_builder_new_from_file(PKGDATADIR "/gui/main.glade"),
      goto fail);

  gtk_builder_connect_signals(gui->builder, gui);

  SU_TRYCATCH(suscan_gui_load_all_widgets(gui), goto fail);

  suscan_gui_spectrum_init(&gui->main_spectrum);

  g_signal_connect(
      GTK_WIDGET(gui->main),
      "destroy",
      G_CALLBACK(suscan_quit_cb),
      NULL);

  return gui;

fail:
  if (gui != NULL)
    suscan_gui_destroy(gui);

  return NULL;
}

void
suscan_gui_set_freq(struct suscan_gui *gui, uint64_t freq)
{
  unsigned int i;
  char string[3];

  for (i = 0; i < 10; ++i) {
    if (i == 9)
      snprintf(string, 3, "%d,", freq % 10);
    else if (i != 0 && (i % 3) == 0)
      snprintf(string, 3, "%d.", freq % 10);
    else
      snprintf(string, 3, "%d", freq % 10);

    gtk_label_set_text(gui->freqLabels[i], string);

    freq /= 10;
  }
}

void
suscan_gui_set_config(
    struct suscan_gui *gui,
    struct suscan_gui_source_config *config)
{
  gui->selected_config = config;

  struct suscan_field_value *val;

  if (config == NULL) {
    gtk_header_bar_set_subtitle(gui->headerBar, "No source selected");
    gtk_widget_set_sensitive(GTK_WIDGET(gui->toggleConnect), FALSE);
  } else {
    gtk_header_bar_set_subtitle(gui->headerBar, config->source->desc);
    gtk_widget_set_sensitive(GTK_WIDGET(gui->toggleConnect), TRUE);

    if ((val = suscan_source_config_get_value(config->config, "fc")) != NULL) {
      if (val->field->type == SUSCAN_FIELD_TYPE_INTEGER)
        suscan_gui_set_freq(gui, val->as_int);
      else if (val->field->type == SUSCAN_FIELD_TYPE_FLOAT)
        suscan_gui_set_freq(gui, val->as_float);
    }
  }
}

void
suscan_gui_disable_all_inspectors(struct suscan_gui *gui)
{
  unsigned int i;

  for (i = 0; i < gui->inspector_count; ++i)
    if (gui->inspector_list[i] != NULL)
      suscan_gui_inspector_disable(gui->inspector_list[i]);
}

SUBOOL
suscan_gui_start(
    int argc,
    char **argv,
    struct suscan_source_config **config_list,
    unsigned int config_count)
{
  struct suscan_gui *gui = NULL;

  SU_TRYCATCH(gui = suscan_gui_new(argc, argv), return SU_FALSE);

  gtk_widget_show(GTK_WIDGET(gui->main));

  gtk_window_set_title(gui->main, "SUScan by BatchDrake");

  suscan_gui_setup_logging(gui);

  SU_INFO("SUScan GTK interface initialized\n");

  gtk_main();

  return SU_TRUE;

fail:
  if (gui != NULL)
    suscan_gui_destroy(gui);

  return SU_FALSE;
}

