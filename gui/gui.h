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

#ifndef _GUI_GUI_H
#define _GUI_GUI_H

#include <sigutils/sigutils.h>
#include <suscan.h>
#include <gtk/gtk.h>

#define SUSCAN_GUI_HORIZONTAL_DIVS 20
#define SUSCAN_GUI_VERTICAL_DIVS   10

#define SUSCAN_GUI_SPECTRUM_DX (1. / SUSCAN_GUI_HORIZONTAL_DIVS)
#define SUSCAN_GUI_SPECTRUM_DY (1. / SUSCAN_GUI_VERTICAL_DIVS)

#define SUSCAN_GUI_SPECTRUM_LEFT_PADDING 30
#define SUSCAN_GUI_SPECTRUM_TOP_PADDING 5

#define SUSCAN_GUI_SPECTRUM_RIGHT_PADDING 5
#define SUSCAN_GUI_SPECTRUM_BOTTOM_PADDING 30

#define SUSCAN_SPECTRUM_TO_SCR_X(s, x)          \
  (((s)->width                                  \
      - SUSCAN_GUI_SPECTRUM_LEFT_PADDING        \
      - SUSCAN_GUI_SPECTRUM_RIGHT_PADDING)      \
    * (x)                                       \
      + SUSCAN_GUI_SPECTRUM_LEFT_PADDING)

#define SUSCAN_SPECTRUM_TO_SCR_Y(s, y)          \
  (((s)->height                                 \
      - SUSCAN_GUI_SPECTRUM_TOP_PADDING         \
      - SUSCAN_GUI_SPECTRUM_BOTTOM_PADDING)     \
    * (y)                                       \
      + SUSCAN_GUI_SPECTRUM_TOP_PADDING)

#define SUSCAN_SPECTRUM_TO_SCR(s, x, y)         \
  SUSCAN_SPECTRUM_TO_SCR_X(s, x), SUSCAN_SPECTRUM_TO_SCR_Y(s, y)

#ifndef PKGDATADIR
#define PKGDATADIR "/usr"
#endif

struct suscan_gui_source_config {
  struct suscan_source *source;
  struct suscan_source_config *config;
  PTR_LIST(GtkWidget, widget);
  GtkGrid *grid;
};

enum suscan_gui_state {
  SUSCAN_GUI_STATE_STOPPED,
  SUSCAN_GUI_STATE_RUNNING,
  SUSCAN_GUI_STATE_STOPPING
};

struct suscan_gui_spectrum {
  SUFLOAT db_per_div;
  cairo_surface_t *surface;
  unsigned width;
  unsigned height;

  uint64_t fc;
  SUFLOAT *psd_data;
  SUSCOUNT psd_size;
  SUSCOUNT samp_rate;
  SUFLOAT  N0;

  /* Current channel list */
  PTR_LIST(struct sigutils_channel, channel);
};

struct suscan_gui {
  /* Widgets */
  GtkBuilder *builder;
  GtkWindow *main;
  GtkButton *toggleConnect;
  GtkButton *preferencesButton;
  GtkListStore *sourceListStore;
  GtkListStore *channelListStore;
  GtkDialog *settingsDialog;
  GtkDialog *aboutDialog;
  GtkComboBox *sourceCombo;
  GtkHeaderBar *headerBar;
  GtkMenuBar *menuBar;
  GtkLabel *freqLabels[10];
  GObject *sourceAlignment;

  GtkTreeViewColumn *centerFrequencyCol;
  GtkTreeViewColumn *snrCol;
  GtkTreeViewColumn *signalLevelCol;
  GtkTreeViewColumn *noiseLevelCol;
  GtkTreeViewColumn *bandwidthCol;

  GtkCellRendererText *centerFrequencyCellRenderer;
  GtkCellRendererText *snrCellRenderer;
  GtkCellRendererText *signalLevelCellRenderer;
  GtkCellRendererText *noiseLevelCellRenderer;
  GtkCellRendererText *bandwidthCellRenderer;

  GtkLevelBar *cpuLevelBar;
  GtkLabel *cpuLabel;

  GtkLevelBar *n0LevelBar;
  GtkLabel *n0Label;

  GtkDrawingArea *spectrumArea;

  struct suscan_gui_source_config *selected_config;

  /* GUI state */
  enum suscan_gui_state state;

  /* Analyzer integration */
  suscan_analyzer_t *analyzer;
  struct suscan_mq mq_out;
  GThread *async_thread;

  /* Main spectrum */
  struct suscan_gui_spectrum main_spectrum;
};

struct suscan_gui *suscan_gui_new(int argc, char **argv);

SUBOOL suscan_gui_start(
    int argc,
    char **argv,
    struct suscan_source_config **config_list,
    unsigned int config_count);

void suscan_gui_msgbox(
    struct suscan_gui *gui,
    GtkMessageType type,
    const char *title,
    const char *fmt,
    ...);

struct suscan_gui_source_config *suscan_gui_get_selected_source(
    struct suscan_gui *gui);

SUBOOL suscan_gui_source_config_parse(struct suscan_gui_source_config *config);

void suscan_gui_set_config(
    struct suscan_gui *gui,
    struct suscan_gui_source_config *config);

void suscan_gui_update_state(
    struct suscan_gui *gui,
    enum suscan_gui_state state);

SUBOOL suscan_gui_connect(struct suscan_gui *gui);
void suscan_gui_disconnect(struct suscan_gui *gui);

void suscan_gui_spectrum_update(
    struct suscan_gui_spectrum *spectrum,
    struct suscan_analyzer_psd_msg *msg);

void suscan_gui_spectrum_update_channels(
    struct suscan_gui_spectrum *spectrum,
    struct sigutils_channel **channel_list,
    unsigned int channel_count);

#define suscan_error(gui, title, fmt, arg...) \
    suscan_gui_msgbox(gui, GTK_MESSAGE_ERROR, title, fmt, ##arg)

#define suscan_warning(gui, title, fmt, arg...) \
    suscan_gui_msgbox(gui, GTK_MESSAGE_WARNING, title, fmt, ##arg)

#endif /* _GUI_GUI_H */
