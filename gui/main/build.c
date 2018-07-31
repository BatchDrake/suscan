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

#define SU_LOG_DOMAIN "gui-build"

#include <confdb.h>
#include "modemctl.h"
#include "gui.h"

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

SUPRIVATE void
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

SUBOOL
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

  SU_TRYCATCH(
      gui->throttleSampRateSpinButton =
          GTK_SPIN_BUTTON(gtk_builder_get_object(
              gui->builder,
              "sbThrottleSampRate")),
          return SU_FALSE);

  SU_TRYCATCH(
      gui->throttleOverrideCheckButton =
          GTK_CHECK_BUTTON(gtk_builder_get_object(
              gui->builder,
              "cbThrottleOverride")),
          return SU_FALSE);

  SU_TRYCATCH(
      gui->settingsViewStack =
          GTK_STACK(gtk_builder_get_object(
              gui->builder,
              "sSettingsView")),
          return SU_FALSE);

  SU_TRYCATCH(
      gui->channelDiscoveryFrame =
          GTK_FRAME(gtk_builder_get_object(
              gui->builder,
              "fChannelDiscovery")),
          return SU_FALSE);

  SU_TRYCATCH(
      gui->colorsFrame =
          GTK_FRAME(gtk_builder_get_object(
              gui->builder,
              "fColors")),
          return SU_FALSE);


  SU_TRYCATCH(
      gui->settingsViewStack =
          GTK_STACK(gtk_builder_get_object(
              gui->builder,
              "sSettingsView")),
          return SU_FALSE);

  SU_TRYCATCH(
      gui->settingsSelectorListBox =
          GTK_LIST_BOX(gtk_builder_get_object(
              gui->builder,
              "lbSettingsSelector")),
          return SU_FALSE);

  SU_TRYCATCH(
      gui->profileNameDialog =
          GTK_DIALOG(gtk_builder_get_object(
              gui->builder,
              "dlProfileName")),
          return SU_FALSE);

  SU_TRYCATCH(
      gui->profileNameEntry =
          GTK_ENTRY(gtk_builder_get_object(
              gui->builder,
              "eProfileName")),
          return SU_FALSE);

  SU_TRYCATCH(
      gui->profilesMenu =
          GTK_MENU(gtk_builder_get_object(
              gui->builder,
              "mProfiles")),
          return SU_FALSE);

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
