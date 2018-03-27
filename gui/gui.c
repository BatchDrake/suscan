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

#define SU_LOG_DOMAIN "gui"

#include "modemctl.h"
#include "gui.h"

PTR_LIST_EXTERN(struct suscan_source, source); /* Declared in source.c */

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
suscan_gui_destroy(suscan_gui_t *gui)
{
  unsigned int i;

  for (i = 0; i < gui->action_count; ++i)
    if (gui->action_list[i] != NULL)
      free(gui->action_list[i]);

  if (gui->action_list != NULL)
    free(gui->action_list);

  for (i = 0; i < gui->inspector_count; ++i)
    if (gui->inspector_list[i] != NULL)
      suscan_gui_inspector_destroy(gui->inspector_list[i]);

  if (gui->inspector_list != NULL)
    free(gui->inspector_list);

  for (i = 0; i < gui->recent_count; ++i)
    if (gui->recent_list[i] != NULL)
      suscan_gui_recent_destroy(gui->recent_list[i]);

  if (gui->recent_list != NULL)
    free(gui->recent_list);

  if (gui->builder != NULL)
    g_object_unref(gui->builder);

  if (gui->analyzer != NULL)
    suscan_analyzer_destroy(gui->analyzer);

  suscan_mq_finalize(&gui->mq_out);

  free(gui);
}

/************************* Analyzer parameter dialog ************************/
void
suscan_gui_text_entry_set_float(GtkEntry *entry, SUFLOAT value)
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

void
suscan_gui_analyzer_params_to_dialog(suscan_gui_t *gui)
{
  suscan_gui_text_entry_set_float(
      gui->alphaEntry,
      gui->analyzer_params.detector_params.alpha);
  suscan_gui_text_entry_set_float(
      gui->betaEntry,
      gui->analyzer_params.detector_params.beta);
  suscan_gui_text_entry_set_float(
      gui->gammaEntry,
      gui->analyzer_params.detector_params.gamma);
  suscan_gui_text_entry_set_float(
      gui->snrEntry,
      SU_POWER_DB(gui->analyzer_params.detector_params.snr));
  suscan_gui_text_entry_set_scount(
      gui->bufSizeEntry,
      gui->analyzer_params.detector_params.window_size);

  switch (gui->analyzer_params.detector_params.window) {
    case SU_CHANNEL_DETECTOR_WINDOW_NONE:
      gtk_toggle_button_set_active(
          GTK_TOGGLE_BUTTON(gui->rectangularWindowButton),
          TRUE);
      break;

    case SU_CHANNEL_DETECTOR_WINDOW_HAMMING:
      gtk_toggle_button_set_active(
          GTK_TOGGLE_BUTTON(gui->hammingWindowButton),
          TRUE);
      break;

    case SU_CHANNEL_DETECTOR_WINDOW_HANN:
      gtk_toggle_button_set_active(
          GTK_TOGGLE_BUTTON(gui->hannWindowButton),
          TRUE);
      break;

    case SU_CHANNEL_DETECTOR_WINDOW_FLAT_TOP:
      gtk_toggle_button_set_active(
          GTK_TOGGLE_BUTTON(gui->flatTopWindowButton),
          TRUE);
      break;

    case SU_CHANNEL_DETECTOR_WINDOW_BLACKMANN_HARRIS:
      gtk_toggle_button_set_active(
          GTK_TOGGLE_BUTTON(gui->blackmannHarrisWindowButton),
          TRUE);
      break;
  }

  suscan_gui_text_entry_set_float(
      gui->psdIntervalEntry,
      gui->analyzer_params.psd_update_int * 1e3);
  suscan_gui_text_entry_set_float(
      gui->chIntervalEntry,
      gui->analyzer_params.channel_update_int * 1e3);
}

SUPRIVATE SUBOOL
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

SUPRIVATE SUBOOL
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

void
suscan_gui_settings_to_dialog(suscan_gui_t *gui)
{
  gtk_color_chooser_set_rgba(
      GTK_COLOR_CHOOSER(gui->paFgColorButton),
      &gui->settings.pa_fg);

  gtk_color_chooser_set_rgba(
      GTK_COLOR_CHOOSER(gui->paBgColorButton),
      &gui->settings.pa_bg);

  gtk_color_chooser_set_rgba(
      GTK_COLOR_CHOOSER(gui->paAxesColorButton),
      &gui->settings.pa_axes);

  gtk_color_chooser_set_rgba(
      GTK_COLOR_CHOOSER(gui->paTextColorButton),
      &gui->settings.pa_text);

  gtk_color_chooser_set_rgba(
      GTK_COLOR_CHOOSER(gui->inspFgColorButton),
      &gui->settings.insp_fg);

  gtk_color_chooser_set_rgba(
      GTK_COLOR_CHOOSER(gui->inspBgColorButton),
      &gui->settings.insp_bg);

  gtk_color_chooser_set_rgba(
      GTK_COLOR_CHOOSER(gui->inspAxesColorButton),
      &gui->settings.pa_axes);

  gtk_color_chooser_set_rgba(
      GTK_COLOR_CHOOSER(gui->inspTextColorButton),
      &gui->settings.insp_text);

  gtk_color_chooser_set_rgba(
      GTK_COLOR_CHOOSER(gui->lcdFgColorButton),
      &gui->settings.lcd_fg);

  gtk_color_chooser_set_rgba(
      GTK_COLOR_CHOOSER(gui->lcdBgColorButton),
      &gui->settings.lcd_bg);
}

void
suscan_gui_settings_from_dialog(suscan_gui_t *gui)
{
  gtk_color_chooser_get_rgba(
      GTK_COLOR_CHOOSER(gui->paFgColorButton),
      &gui->settings.pa_fg);

  gtk_color_chooser_get_rgba(
      GTK_COLOR_CHOOSER(gui->paBgColorButton),
      &gui->settings.pa_bg);

  gtk_color_chooser_get_rgba(
      GTK_COLOR_CHOOSER(gui->paAxesColorButton),
      &gui->settings.pa_axes);

  gtk_color_chooser_get_rgba(
      GTK_COLOR_CHOOSER(gui->paTextColorButton),
      &gui->settings.pa_text);

  gtk_color_chooser_get_rgba(
      GTK_COLOR_CHOOSER(gui->inspFgColorButton),
      &gui->settings.insp_fg);

  gtk_color_chooser_get_rgba(
      GTK_COLOR_CHOOSER(gui->inspBgColorButton),
      &gui->settings.insp_bg);

  gtk_color_chooser_get_rgba(
      GTK_COLOR_CHOOSER(gui->inspAxesColorButton),
      &gui->settings.insp_axes);

  gtk_color_chooser_get_rgba(
      GTK_COLOR_CHOOSER(gui->inspTextColorButton),
      &gui->settings.insp_text);

  gtk_color_chooser_get_rgba(
      GTK_COLOR_CHOOSER(gui->lcdFgColorButton),
      &gui->settings.lcd_fg);

  gtk_color_chooser_get_rgba(
      GTK_COLOR_CHOOSER(gui->lcdBgColorButton),
      &gui->settings.lcd_bg);

  suscan_gui_apply_settings(gui);
}

SUBOOL
suscan_gui_analyzer_params_from_dialog(suscan_gui_t *gui)
{
  struct suscan_analyzer_params params = gui->analyzer_params;
  SUFLOAT snr;
  SUBOOL ok = SU_FALSE;

  if (!suscan_gui_text_entry_get_float(
      gui->alphaEntry,
      &params.detector_params.alpha)) {
    SU_ERROR("Invalid value for detector's spectrum averaging factor\n");
    goto done;
  }

  if (!suscan_gui_text_entry_get_float(
      gui->betaEntry,
      &params.detector_params.beta)) {
    SU_ERROR("Invalid value for detector's signal level averaging factor\n");
    goto done;
  }

  if (!suscan_gui_text_entry_get_float(
      gui->gammaEntry,
      &params.detector_params.gamma)) {
    SU_ERROR("Invalid value for detector's noise level averaging factor\n");
    goto done;
  }

  if (!suscan_gui_text_entry_get_float(
      gui->snrEntry,
      &snr)) {
    SU_ERROR("Invalid value for detector's spectrum averaging factor\n");
    goto done;
  }

  params.detector_params.snr = SU_POWER_MAG(snr);

  if (gtk_toggle_button_get_active(
      GTK_TOGGLE_BUTTON(gui->rectangularWindowButton)))
      params.detector_params.window = SU_CHANNEL_DETECTOR_WINDOW_NONE;
  else if (gtk_toggle_button_get_active(
      GTK_TOGGLE_BUTTON(gui->hammingWindowButton)))
    params.detector_params.window = SU_CHANNEL_DETECTOR_WINDOW_HAMMING;
  else if (gtk_toggle_button_get_active(
      GTK_TOGGLE_BUTTON(gui->hannWindowButton)))
    params.detector_params.window = SU_CHANNEL_DETECTOR_WINDOW_HANN;
  else if (gtk_toggle_button_get_active(
      GTK_TOGGLE_BUTTON(gui->flatTopWindowButton)))
    params.detector_params.window = SU_CHANNEL_DETECTOR_WINDOW_FLAT_TOP;
  else if (gtk_toggle_button_get_active(
      GTK_TOGGLE_BUTTON(gui->blackmannHarrisWindowButton)))
      params.detector_params.window = SU_CHANNEL_DETECTOR_WINDOW_BLACKMANN_HARRIS;

  if (!suscan_gui_text_entry_get_scount(
      gui->bufSizeEntry,
      &params.detector_params.window_size)) {
    SU_ERROR("Invalid value for detector's FFT size\n");
    goto done;
  }

  if (!suscan_gui_text_entry_get_float(
      gui->psdIntervalEntry,
      &params.psd_update_int)) {
    SU_ERROR("Invalid value for PSD update interval\n");
    goto done;
  }

  params.psd_update_int *= 1e-3;

  if (!suscan_gui_text_entry_get_float(
      gui->chIntervalEntry,
      &params.channel_update_int)) {
    SU_ERROR("Invalid value for channel update interval\n");
    goto done;
  }

  params.channel_update_int *= 1e-3;

  gui->analyzer_params = params;
  ok = SU_TRUE;

done:
  suscan_gui_analyzer_params_to_dialog(gui);

  return ok;
}

/*************************** Source dialog methods **************************/
void
suscan_gui_source_config_destroy(struct suscan_gui_src_ui *config)
{
  if (config->config != NULL)
    suscan_source_config_destroy(config->config);

  if (config->cfgui)
    suscan_gui_cfgui_destroy(config->cfgui);

  free(config);
}

void
suscan_gui_src_ui_to_dialog(const struct suscan_gui_src_ui *ui)
{
  suscan_gui_cfgui_dump(ui->cfgui);
}

SUBOOL
suscan_gui_src_ui_from_dialog(struct suscan_gui_src_ui *ui)
{
  return suscan_gui_cfgui_parse(ui->cfgui);
}

struct suscan_gui_src_ui *
suscan_gui_source_config_new(struct suscan_source *source)
{
  struct suscan_gui_src_ui *new = NULL;


  SU_TRYCATCH(
      new = calloc(1, sizeof(struct suscan_gui_src_ui)),
      goto fail);

  new->source = source;

  SU_TRYCATCH(new->config = suscan_source_config_new(source), goto fail);

  SU_TRYCATCH(
      new->cfgui = suscan_gui_cfgui_new(new->config->config),
      goto fail);

  return new;

fail:
  if (new != NULL)
    suscan_gui_source_config_destroy(new);

  return NULL;
}

SUBOOL
suscan_gui_populate_source_list(suscan_gui_t *gui)
{
  unsigned int i;
  GtkTreeIter new_element;
  struct suscan_gui_src_ui *config;

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

struct suscan_gui_src_ui *
suscan_gui_lookup_source_config(
    const suscan_gui_t *gui,
    const struct suscan_source *src)
{
  GtkTreeIter iter;
  struct suscan_gui_src_ui *config;
  gboolean ok;

  ok = gtk_tree_model_get_iter_first(
      GTK_TREE_MODEL(gui->sourceListStore),
      &iter);

  while (ok) {
    gtk_tree_model_get(
        GTK_TREE_MODEL(gui->sourceListStore),
        &iter,
        1,
        &config,
        -1);

    if (config->source == src)
      return config;

    ok = gtk_tree_model_iter_next(GTK_TREE_MODEL(gui->sourceListStore), &iter);
  }

  return NULL;
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
  gsufloat double_val;
  GValue val = G_VALUE_INIT;
  char as_string[32];

  gtk_tree_model_get_value(model, iter, (long) data, &val);

  double_val = g_value_get_double(&val);

  snprintf(as_string, sizeof(as_string), "%.1lf", double_val);

  g_object_set(G_OBJECT(cell), "text", as_string, NULL);

  g_value_unset(&val);
}

void
suscan_setup_column_formats(suscan_gui_t *gui)
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

SUPRIVATE void
suscan_gui_on_open_inspector(
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
          rand()),
      return);
}

SUPRIVATE SUBOOL
suscan_gui_add_inspector_action(
    suscan_gui_t *gui,
    const struct suscan_inspector_interface *insp_iface)
{
  char *action_text = NULL;
  struct suscan_gui_spectrum_action *action = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(action_text = strbuild("Open %s", insp_iface->desc), goto done);

  SU_TRYCATCH(
      action = calloc(1, sizeof(struct suscan_gui_spectrum_action)),
      goto done);

  action->gui = gui;
  action->insp_iface = insp_iface;

  SU_TRYCATCH(PTR_LIST_APPEND_CHECK(gui->action, action) != -1, goto done);

  (void) sugtk_spectrum_add_menu_action(
      gui->spectrum,
      action_text,
      suscan_gui_on_open_inspector,
      action);

  action = NULL;

  ok = SU_TRUE;

done:
  if (action_text != NULL)
    free(action_text);

  if (action != NULL)
    free(action);

  return ok;
}

SUPRIVATE SUBOOL
suscan_gui_add_all_inspector_actions(suscan_gui_t *gui)
{
  const struct suscan_inspector_interface **iface_list;
  unsigned int iface_count;
  unsigned int i;

  suscan_inspector_interface_get_list(&iface_list, &iface_count);

  for (i = 0; i < iface_count; ++i)
    SU_TRYCATCH(
        suscan_gui_add_inspector_action(gui, iface_list[i]),
        return SU_FALSE);

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_gui_load_all_widgets(suscan_gui_t *gui)
{
  SU_TRYCATCH(
      gui->main = GTK_WINDOW(gtk_builder_get_object(gui->builder, "wMain")),
      return SU_FALSE);

  SU_TRYCATCH(
      gui->headerBar = GTK_HEADER_BAR(
          gtk_builder_get_object(gui->builder, "HeaderBar")),
      return SU_FALSE);

  SU_TRYCATCH(
      gui->spectrumGrid = GTK_GRID(
          gtk_builder_get_object(gui->builder, "grSpectrum")),
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
      gui->sourceGrid = GTK_GRID(
          gtk_builder_get_object(gui->builder, "grSourceGrid")),
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
      gui->spectrumSampleRateLabel =
          GTK_LABEL(gtk_builder_get_object(
              gui->builder,
              "lSpectrumSampleRate")),
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

  SU_TRYCATCH(
      gui->recentMenu =
          GTK_MENU(gtk_builder_get_object(
              gui->builder,
              "mRecents")),
          return SU_FALSE);

  SU_TRYCATCH(
      gui->emptyMenuItem =
          GTK_MENU_ITEM(gtk_builder_get_object(
              gui->builder,
              "miEmpty")),
          return SU_FALSE);

  SU_TRYCATCH(
      gui->overlayChannelToggleButton =
          GTK_TOGGLE_BUTTON(gtk_builder_get_object(
              gui->builder,
              "tbOverlayChannels")),
          return SU_FALSE);

  SU_TRYCATCH(
      gui->autoGainToggleButton =
          GTK_TOGGLE_BUTTON(gtk_builder_get_object(
              gui->builder,
              "tbAutoGain")),
          return SU_FALSE);

  SU_TRYCATCH(
      gui->gainScale =
          GTK_SCALE(gtk_builder_get_object(
              gui->builder,
              "sbRefLevel")),
          return SU_FALSE);

  SU_TRYCATCH(
      gui->rangeScale =
          GTK_SCALE(gtk_builder_get_object(
              gui->builder,
              "sbRange")),
          return SU_FALSE);

  SU_TRYCATCH(
      gui->panadapterScale =
          GTK_SCALE(gtk_builder_get_object(
              gui->builder,
              "sbPanadapter")),
          return SU_FALSE);

  SU_TRYCATCH(
      gui->alphaEntry =
          GTK_ENTRY(gtk_builder_get_object(
              gui->builder,
              "eAnalyzerAlpha")),
          return SU_FALSE);

  SU_TRYCATCH(
      gui->betaEntry =
          GTK_ENTRY(gtk_builder_get_object(
              gui->builder,
              "eAnalyzerBeta")),
          return SU_FALSE);

  SU_TRYCATCH(
      gui->gammaEntry =
          GTK_ENTRY(gtk_builder_get_object(
              gui->builder,
              "eAnalyzerGamma")),
          return SU_FALSE);

  SU_TRYCATCH(
      gui->snrEntry =
          GTK_ENTRY(gtk_builder_get_object(
              gui->builder,
              "eSNR")),
          return SU_FALSE);

  SU_TRYCATCH(
      gui->bufSizeEntry =
          GTK_ENTRY(gtk_builder_get_object(
              gui->builder,
              "eBufferSize")),
          return SU_FALSE);

  SU_TRYCATCH(
      gui->chIntervalEntry =
          GTK_ENTRY(gtk_builder_get_object(
              gui->builder,
              "eChInterval")),
          return SU_FALSE);

  SU_TRYCATCH(
      gui->psdIntervalEntry =
          GTK_ENTRY(gtk_builder_get_object(
              gui->builder,
              "ePSDInterval")),
          return SU_FALSE);

  SU_TRYCATCH(
      gui->rectangularWindowButton =
          GTK_RADIO_BUTTON(gtk_builder_get_object(
              gui->builder,
              "rbWinFuncRectangular")),
          return SU_FALSE);

  SU_TRYCATCH(
      gui->hammingWindowButton =
          GTK_RADIO_BUTTON(gtk_builder_get_object(
              gui->builder,
              "rbWinFuncHamming")),
          return SU_FALSE);

  SU_TRYCATCH(
      gui->hannWindowButton =
          GTK_RADIO_BUTTON(gtk_builder_get_object(
              gui->builder,
              "rbWinFuncHann")),
          return SU_FALSE);

  SU_TRYCATCH(
      gui->blackmannHarrisWindowButton =
          GTK_RADIO_BUTTON(gtk_builder_get_object(
              gui->builder,
              "rbWinFuncBlackmannHarris")),
          return SU_FALSE);

  SU_TRYCATCH(
      gui->flatTopWindowButton =
          GTK_RADIO_BUTTON(gtk_builder_get_object(
              gui->builder,
              "rbWinFuncFlatTop")),
          return SU_FALSE);

  SU_TRYCATCH(
      gui->titleLabel =
          GTK_LABEL(gtk_builder_get_object(
              gui->builder,
              "lTitle")),
          return SU_FALSE);

  SU_TRYCATCH(
      gui->subTitleLabel =
          GTK_LABEL(gtk_builder_get_object(
              gui->builder,
              "lSubTitle")),
          return SU_FALSE);

  SU_TRYCATCH(
      gui->symToolNotebook =
          GTK_NOTEBOOK(gtk_builder_get_object(
              gui->builder,
              "nbSymTool")),
          return SU_FALSE);

  SU_TRYCATCH(
      gui->freqBox =
          GTK_BOX(gtk_builder_get_object(
              gui->builder,
              "bFreq")),
          return SU_FALSE);

  /* Settings dialog widgets */
  SU_TRYCATCH(
      gui->paFgColorButton =
          GTK_COLOR_BUTTON(gtk_builder_get_object(
              gui->builder,
              "cbPaFg")),
          return SU_FALSE);

  SU_TRYCATCH(
      gui->paBgColorButton =
          GTK_COLOR_BUTTON(gtk_builder_get_object(
              gui->builder,
              "cbPaBg")),
          return SU_FALSE);

  SU_TRYCATCH(
      gui->paTextColorButton =
          GTK_COLOR_BUTTON(gtk_builder_get_object(
              gui->builder,
              "cbPaText")),
          return SU_FALSE);

  SU_TRYCATCH(
      gui->paAxesColorButton =
          GTK_COLOR_BUTTON(gtk_builder_get_object(
              gui->builder,
              "cbPaAxes")),
          return SU_FALSE);

  SU_TRYCATCH(
      gui->inspFgColorButton =
          GTK_COLOR_BUTTON(gtk_builder_get_object(
              gui->builder,
              "cbInspFg")),
          return SU_FALSE);

  SU_TRYCATCH(
      gui->inspBgColorButton =
          GTK_COLOR_BUTTON(gtk_builder_get_object(
              gui->builder,
              "cbInspBg")),
          return SU_FALSE);

  SU_TRYCATCH(
      gui->inspTextColorButton =
          GTK_COLOR_BUTTON(gtk_builder_get_object(
              gui->builder,
              "cbInspText")),
          return SU_FALSE);

  SU_TRYCATCH(
      gui->inspAxesColorButton =
          GTK_COLOR_BUTTON(gtk_builder_get_object(
              gui->builder,
              "cbInspAxes")),
          return SU_FALSE);

  SU_TRYCATCH(
      gui->lcdFgColorButton =
          GTK_COLOR_BUTTON(gtk_builder_get_object(
              gui->builder,
              "cbLcdFg")),
          return SU_FALSE);

  SU_TRYCATCH(
      gui->lcdBgColorButton =
          GTK_COLOR_BUTTON(gtk_builder_get_object(
              gui->builder,
              "cbLcdBg")),
          return SU_FALSE);

  suscan_gui_populate_source_list(gui);

  suscan_setup_column_formats(gui);

  gtk_combo_box_set_active(gui->sourceCombo, 0);

  /* Update preferences */
  suscan_gui_analyzer_params_to_dialog(gui);
  suscan_gui_settings_to_dialog(gui);

  /* Add spectrum view */
  gui->spectrum = SUGTK_SPECTRUM(sugtk_spectrum_new());
  sugtk_spectrum_set_smooth_N0(gui->spectrum, TRUE);

  SU_TRYCATCH(suscan_gui_add_all_inspector_actions(gui), return SU_FALSE);

  gtk_grid_attach(gui->spectrumGrid, GTK_WIDGET(gui->spectrum), 0, 0, 1, 1);

  gtk_widget_set_hexpand(GTK_WIDGET(gui->spectrum), TRUE);
  gtk_widget_set_vexpand(GTK_WIDGET(gui->spectrum), TRUE);

  gtk_widget_show(GTK_WIDGET(gui->spectrum));

  sugtk_spectrum_set_mode(gui->spectrum, SUGTK_SPECTRUM_MODE_BOTH);
  sugtk_spectrum_set_show_channels(gui->spectrum, TRUE);

  /* Update GUI on spectrum state */
  gui->updating_settings = SU_TRUE;
  gtk_toggle_button_set_active(
      gui->overlayChannelToggleButton,
      sugtk_spectrum_get_show_channels(gui->spectrum));
  gui->updating_settings = SU_FALSE;

  gtk_toggle_button_set_active(
      gui->autoGainToggleButton,
      sugtk_spectrum_get_auto_level(gui->spectrum));

  /* Add frequency LCD */
  gui->freqLcd = SUGTK_LCD(sugtk_lcd_new());
  gtk_box_pack_start(gui->freqBox, GTK_WIDGET(gui->freqLcd), TRUE, TRUE, 0);
  gtk_widget_show(GTK_WIDGET(gui->freqLcd));

  return SU_TRUE;
}

/************************ Inspector handling methods *************************/
SUBOOL
suscan_gui_remove_inspector(
    suscan_gui_t *gui,
    suscan_gui_inspector_t *insp)
{
  gint num;
  int index = insp->index;
  if (index < 0 || index >= gui->inspector_count)
    return SU_FALSE;

  SU_TRYCATCH(gui->inspector_list[index] == insp, return SU_FALSE);

  SU_TRYCATCH(
      (num = gtk_notebook_page_num(
          gui->analyzerViewsNotebook,
          GTK_WIDGET(insp->channelInspectorGrid))) != -1,
      return SU_FALSE);

  gtk_notebook_remove_page(gui->analyzerViewsNotebook, num);

  gui->inspector_list[index] = NULL;

  return SU_TRUE;
}

SUBOOL
suscan_gui_add_inspector(
    suscan_gui_t *gui,
    suscan_gui_inspector_t *insp)
{
  gint page;
  SUBOOL inspector_added = SU_FALSE;

  SU_TRYCATCH(
      (insp->index = PTR_LIST_APPEND_CHECK(gui->inspector, insp)) != -1,
      goto fail);

  inspector_added = SU_TRUE;
  insp->_parent.gui = gui;

  SU_TRYCATCH(
      (page = gtk_notebook_append_page_menu(
          gui->analyzerViewsNotebook,
          GTK_WIDGET(insp->channelInspectorGrid),
          GTK_WIDGET(insp->pageLabelEventBox),
          NULL)) >= 0,
      goto fail);

  gtk_notebook_set_tab_reorderable(
      gui->analyzerViewsNotebook,
      GTK_WIDGET(insp->pageLabelEventBox),
      TRUE);

  gtk_notebook_set_current_page(gui->analyzerViewsNotebook, page);

  return TRUE;

fail:
  if (inspector_added)
    (void) suscan_gui_remove_inspector(gui, insp);

  return FALSE;
}

suscan_gui_inspector_t *
suscan_gui_get_inspector(const suscan_gui_t *gui, uint32_t inspector_id)
{
  if (inspector_id >= gui->inspector_count)
    return NULL;

  return gui->inspector_list[inspector_id];
}

/*************************** Symbol tool handling ****************************/
SUBOOL
suscan_gui_remove_symtool(
    suscan_gui_t *gui,
    suscan_gui_symtool_t *symtool)
{
  gint num;
  int index = symtool->index;
  if (index < 0 || index >= gui->symtool_count)
    return SU_FALSE;

  SU_TRYCATCH(gui->symtool_list[index] == symtool, return SU_FALSE);

  SU_TRYCATCH(
      (num = gtk_notebook_page_num(
          gui->symToolNotebook,
          suscan_gui_symtool_get_root(symtool))) != -1,
      return SU_FALSE);

  gtk_notebook_remove_page(gui->symToolNotebook, num);

  gui->symtool_list[index] = NULL;

  return SU_TRUE;
}

SUBOOL
suscan_gui_add_symtool(
    suscan_gui_t *gui,
    suscan_gui_symtool_t *symtool)
{
  gint page;
  SUBOOL symtool_added = SU_FALSE;

  SU_TRYCATCH(
      (symtool->index = PTR_LIST_APPEND_CHECK(gui->symtool, symtool)) != -1,
      goto fail);

  symtool_added = SU_TRUE;
  symtool->_parent.gui = gui;

  SU_TRYCATCH(
      (page = gtk_notebook_append_page_menu(
          gui->symToolNotebook,
          suscan_gui_symtool_get_root(symtool),
          suscan_gui_symtool_get_label(symtool),
          NULL)) >= 0,
      goto fail);

  gtk_notebook_set_tab_reorderable(
      gui->symToolNotebook,
      suscan_gui_symtool_get_label(symtool),
      TRUE);

  gtk_notebook_set_current_page(gui->symToolNotebook, page);

  return TRUE;

fail:
  if (symtool_added)
    (void) suscan_gui_remove_symtool(gui, symtool);

  return FALSE;
}

suscan_gui_symtool_t *
suscan_gui_get_symtool(const suscan_gui_t *gui, uint32_t symtool_id)
{
  if (symtool_id >= gui->symtool_count)
    return NULL;

  return gui->symtool_list[symtool_id];
}

/**************************** Generic GUI methods ****************************/
SUPRIVATE void
suscan_quit_cb(GtkWidget *obj, gpointer data)
{
  suscan_gui_t *gui = (suscan_gui_t *) data;

  suscan_gui_quit(gui);
}

void
suscan_gui_apply_settings_on_inspector(
    suscan_gui_t *gui,
    suscan_gui_inspector_t *insp)
{
  sugtk_spectrum_set_fg_color(insp->spectrum, gui->settings.insp_fg);
  sugtk_spectrum_set_bg_color(insp->spectrum, gui->settings.insp_bg);
  sugtk_spectrum_set_text_color(insp->spectrum, gui->settings.insp_text);
  sugtk_spectrum_set_axes_color(insp->spectrum, gui->settings.insp_axes);

  sugtk_constellation_set_fg_color(insp->constellation, gui->settings.insp_fg);
  sugtk_constellation_set_bg_color(insp->constellation, gui->settings.insp_bg);
  sugtk_constellation_set_axes_color(insp->constellation, gui->settings.insp_axes);

  sugtk_waveform_set_fg_color(insp->phasePlot, gui->settings.insp_fg);
  sugtk_waveform_set_bg_color(insp->phasePlot, gui->settings.insp_bg);
  sugtk_waveform_set_axes_color(insp->phasePlot, gui->settings.insp_axes);

  sugtk_histogram_set_fg_color(insp->histogram, gui->settings.insp_fg);
  sugtk_histogram_set_bg_color(insp->histogram, gui->settings.insp_bg);
  sugtk_histogram_set_axes_color(insp->histogram, gui->settings.insp_axes);
}

void
suscan_gui_apply_settings(suscan_gui_t *gui)
{
  unsigned int i;

  sugtk_spectrum_set_fg_color(gui->spectrum, gui->settings.pa_fg);
  sugtk_spectrum_set_bg_color(gui->spectrum, gui->settings.pa_bg);
  sugtk_spectrum_set_text_color(gui->spectrum, gui->settings.pa_text);
  sugtk_spectrum_set_axes_color(gui->spectrum, gui->settings.pa_axes);

  sugtk_lcd_set_fg_color(gui->freqLcd, gui->settings.lcd_fg);
  sugtk_lcd_set_bg_color(gui->freqLcd, gui->settings.lcd_bg);

  for (i = 0; i < gui->inspector_count; ++i)
    if (gui->inspector_list[i] != NULL)
      suscan_gui_apply_settings_on_inspector(gui, gui->inspector_list[i]);
}

suscan_gui_t *
suscan_gui_new(int argc, char **argv)
{
  suscan_gui_t *gui = NULL;
  GtkCssProvider *provider;
  GError *err = NULL;

  gtk_init(&argc, &argv);

  provider = gtk_css_provider_new();

  SU_TRYCATCH(
      gtk_css_provider_load_from_path(
          provider,
          PKGDATADIR "/gui/ui.css",
          &err),
      g_prefix_error(&err, "Cannot parse CSS"); goto fail);

  gtk_style_context_add_provider_for_screen(
      gdk_screen_get_default(),
      GTK_STYLE_PROVIDER(provider),
      GTK_STYLE_PROVIDER_PRIORITY_USER);

  SU_TRYCATCH(gui = calloc(1, sizeof(suscan_gui_t)), goto fail);

  SU_TRYCATCH(
      gui->g_settings = g_settings_new(SUSCAN_GUI_SETTINGS_ID),
      goto fail);

  SU_TRYCATCH(
      gui->builder = gtk_builder_new_from_file(PKGDATADIR "/gui/main.glade"),
      goto fail);

  gtk_builder_connect_signals(gui->builder, gui);

  suscan_gui_load_settings(gui);

  SU_TRYCATCH(suscan_gui_load_all_widgets(gui), goto fail);

  suscan_gui_apply_settings(gui);

  g_signal_connect(
      GTK_WIDGET(gui->main),
      "destroy",
      G_CALLBACK(suscan_quit_cb),
      gui);

  suscan_gui_retrieve_recent(gui);

  return gui;

fail:
  if (gui != NULL)
    suscan_gui_destroy(gui);

  return NULL;
}

void
suscan_gui_set_freq(suscan_gui_t *gui, uint64_t freq)
{
  sugtk_lcd_set_value(gui->freqLcd, freq);
}

SUBOOL
suscan_gui_set_title(suscan_gui_t *gui, const char *title)
{
  char *full_title = NULL;

  SU_TRYCATCH(full_title = strbuild("%s - Suscan", title), return SU_FALSE);

  gtk_label_set_text(gui->titleLabel, title);

  gtk_window_set_title(gui->main, full_title);

  free(full_title);

  return SU_TRUE;
}

void
suscan_gui_set_src_ui(
    suscan_gui_t *gui,
    struct suscan_gui_src_ui *ui)
{
  struct suscan_field_value *val;

  if (ui == NULL) {
    (void) suscan_gui_set_title(gui, "No source selected");
    gtk_widget_set_sensitive(GTK_WIDGET(gui->toggleConnect), FALSE);
    gui->analyzer_source_config = NULL;
  } else {
    (void) suscan_gui_set_title(gui, ui->source->desc);
    gtk_widget_set_sensitive(GTK_WIDGET(gui->toggleConnect), TRUE);
    gui->analyzer_source_config = ui->config;

    if ((val = suscan_source_config_get_value(ui->config, "fc")) != NULL) {
      if (val->field->type == SUSCAN_FIELD_TYPE_INTEGER)
        suscan_gui_set_freq(gui, val->as_int);
      else if (val->field->type == SUSCAN_FIELD_TYPE_FLOAT)
        suscan_gui_set_freq(gui, val->as_float);
    }
  }
}

void
suscan_gui_detach_all_inspectors(suscan_gui_t *gui)
{
  unsigned int i;

  for (i = 0; i < gui->inspector_count; ++i)
    if (gui->inspector_list[i] != NULL)
      suscan_gui_inspector_detach(gui->inspector_list[i]);
}

SUBOOL
suscan_gui_helper_preload(void)
{
  SU_TRYCATCH(suscan_gui_modemctl_agc_init(), return SU_FALSE);
  SU_TRYCATCH(suscan_gui_modemctl_afc_init(), return SU_FALSE);
  SU_TRYCATCH(suscan_gui_modemctl_mf_init(), return SU_FALSE);
  SU_TRYCATCH(suscan_gui_modemctl_equalizer_init(), return SU_FALSE);
  SU_TRYCATCH(suscan_gui_modemctl_clock_init(), return SU_FALSE);

  return SU_TRUE;
}

SUBOOL
suscan_gui_start(
    int argc,
    char **argv,
    struct suscan_source_config **config_list,
    unsigned int config_count)
{
  suscan_gui_t *gui = NULL;

  SU_TRYCATCH(suscan_gui_helper_preload(), goto fail);

  SU_TRYCATCH(gui = suscan_gui_new(argc, argv), goto fail);

  gtk_widget_show(GTK_WIDGET(gui->main));

  suscan_gui_set_title(gui, "No source selected");

  suscan_gui_setup_logging(gui);

  SU_INFO("SUScan GTK interface initialized\n");

  gtk_main();

  return SU_TRUE;

fail:
  if (gui != NULL)
    suscan_gui_destroy(gui);

  return SU_FALSE;
}

