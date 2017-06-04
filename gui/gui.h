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

#ifndef PKGDATADIR
#define PKGDATADIR "/usr"
#endif

#define SUSCAN_GUI_SETTINGS_ID "org.actinid.SUScan"
#define SUSCAN_GUI_MAX_CHANNELS 10

struct suscan_gui_source_config {
  const struct suscan_source *source;
  struct suscan_source_config *config;
  PTR_LIST(GtkWidget, widget);
  GtkGrid *grid;
};

enum suscan_gui_state {
  SUSCAN_GUI_STATE_STOPPED,
  SUSCAN_GUI_STATE_RUNNING,
  SUSCAN_GUI_STATE_STOPPING,
  SUSCAN_GUI_STATE_QUITTING
};

#define SUSCAN_GUI_SPECTRUM_FREQ_OFFSET_DEFAULT 0
#define SUSCAN_GUI_SPECTRUM_FREQ_SCALE_DEFAULT  1
#define SUSCAN_GUI_SPECTRUM_DBS_PER_DIV_DEFAULT 10
#define SUSCAN_GUI_SPECTRUM_REF_LEVEL_DEFAULT   0
#define SUSCAN_GUI_SPECTRUM_WATERFALL_AGC_ALPHA .1

enum suscan_gui_spectrum_param {
  SUSCAN_GUI_SPECTRUM_PARAM_FREQ_OFFSET,
  SUSCAN_GUI_SPECTRUM_PARAM_FREQ_SCALE,
  SUSCAN_GUI_SPECTRUM_PARAM_REF_LEVEL,
  SUSCAN_GUI_SPECTRUM_PARAM_DBS_PER_DIV,
};

enum suscan_gui_spectrum_mode {
  SUSCAN_GUI_SPECTRUM_MODE_SPECTROGRAM,
  SUSCAN_GUI_SPECTRUM_MODE_WATERFALL,
};

struct suscan_gui_spectrum {
  enum suscan_gui_spectrum_mode mode;
  unsigned width;
  unsigned height;
  int g_width;
  int g_height;

  uint64_t fc;
  SUFLOAT *psd_data;
  SUSCOUNT psd_size;
  SUSCOUNT samp_rate;
  SUFLOAT  N0;
  SUSCOUNT updates; /* Number of spectrum updates */
  SUSCOUNT last_update; /* Last update in which waterfall has been repainted */
  SUBOOL   auto_level; /* Automatic reference level */

  /* Representation properties */
  SUBOOL  show_channels; /* Defaults to TRUE */
  SUFLOAT freq_offset; /* Defaults to 0 */
  SUFLOAT freq_scale;  /* Defaults to 1 */
  SUFLOAT dbs_per_div; /* Defaults to 10 */
  SUFLOAT ref_level;   /* Defaults to 0 */
  SUFLOAT last_max;

  /* Waterfall members */
  SUBOOL flip;
  cairo_surface_t *wf_surf[2];
  SUFLOAT last_freq_offset;

  /* Scroll and motion state */
  gdouble last_x;
  gdouble last_y;

  gdouble  prev_ev_x;
  SUBOOL   dragging;
  SUFLOAT  original_freq_offset;
  SUFLOAT  original_ref_level;

  /* Current channel selection state */
  SUBOOL selecting;
  struct sigutils_channel selection;

  /* Current channel list */
  PTR_LIST(struct sigutils_channel, channel);
};


struct suscan_gui_recent {
  struct suscan_gui *gui;
  char *conf_string;
  struct suscan_source_config *config;
  struct suscan_gui_source_config as_gui_config;
};

struct suscan_gui {
  /* Application settings */
  GSettings *settings;

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
  GtkMenu *recentMenu;
  GtkMenuItem *emptyMenuItem;

  GtkRadioMenuItem    *spectrogramMenuItem;
  GtkRadioMenuItem    *waterfallMenuItem;
  GtkToggleToolButton *overlayChannelToggleButton;
  GtkToggleToolButton *autoGainToggleButton;
  GtkAdjustment       *gainAdjustment;

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

  GtkLabel *spectrumSampleRate;
  GtkLabel *spectrumDbsPerDivLabel;
  GtkLabel *spectrumRefLevelLabel;
  GtkLabel *spectrumFreqScaleLabel;
  GtkLabel *spectrumFreqOffsetLabel;

  GtkMenu *channelMenu;
  GtkMenuItem *channelHeaderMenuItem;
  GtkMenuItem *openInspectorMenuItem;

  GtkNotebook *analyzerViewsNotebook;

  GtkTreeView *logMessagesTreeView;
  GtkListStore *logMessagesListStore;

  /* FIXME: this should be suscan_source_config, not suscan_gui_source_config */
  struct suscan_gui_source_config *selected_config;

  /* GUI state */
  enum suscan_gui_state state;

  /* Analyzer integration */
  suscan_analyzer_t *analyzer;
  struct suscan_mq mq_out;
  GThread *async_thread;

  /* Main spectrum */
  SUSCOUNT current_samp_rate;
  struct sigutils_channel selected_channel;
  struct suscan_gui_spectrum main_spectrum;

  /* Keep list of inspector tabs */
  PTR_LIST(struct suscan_gui_inspector, inspector);

  /* Keep a list of the last configurations used */
  PTR_LIST(struct suscan_gui_recent, recent);
};

#define SUSCAN_GUI_CONSTELLATION_HISTORY 200

struct suscan_gui_constellation {
  cairo_surface_t *surface;
  unsigned width;
  unsigned height;

  SUCOMPLEX phase;
  SUCOMPLEX history[SUSCAN_GUI_CONSTELLATION_HISTORY];
  unsigned int p;
};

struct suscan_gui_inspector {
  int index; /* Back reference */
  SUHANDLE inshnd; /* Inspector handle (relative to current analyzer) */
  SUBOOL dead; /* Owner analyzer has been destroyed */
  SUBOOL recording; /* Symbol recorder enabled */
  SUBOOL autoscroll;
  struct suscan_gui *gui; /* Parent GUI */
  struct suscan_gui_constellation constellation; /* Constellation graph */
  struct suscan_gui_spectrum spectrum; /* Spectrum graph */
  struct suscan_inspector_params params; /* Inspector params */
  char *symbol_text_buffer;
  size_t symbol_text_buffer_size;

  /* Widgets */
  GtkBuilder  *builder;
  GtkEventBox *pageLabelEventBox;
  GtkLabel    *pageLabel;
  GtkGrid     *channelInspectorGrid;
  GtkToggleButton *autoScrollToggleButton;

  /* Gain control widgets */
  GtkRadioButton *automaticGainRadioButton;
  GtkRadioButton *manualGainRadioButton;
  GtkAlignment   *gainManualAlignment;
  GtkEntry       *gainEntry;
  GtkScale       *gainFineTuneScale;

  /* Carrier control alignment */
  GtkRadioButton *costas2RadioButton;
  GtkRadioButton *costas4RadioButton;
  GtkRadioButton *costas8RadioButton;
  GtkRadioButton *manualRadioButton;
  GtkAlignment   *carrierManualAlignment;
  GtkEntry       *carrierOffsetEntry;
  GtkScale       *fineTuneScale;
  GtkScale       *phaseScale;

  /* Clock control widgets */
  GtkRadioButton *clockGardnerRadioButton;
  GtkRadioButton *clockManualRadioButton;
  GtkRadioButton *clockDisableButton;
  GtkAlignment   *clockGardnerAlignment;
  GtkEntry       *gardnerAlphaEntry;
  GtkCheckButton *gardnerEnableBetaCheckButton;
  GtkEntry       *gardnerBetaEntry;
  GtkAlignment   *clockManualAlignment;
  GtkEntry       *baudRateEntry;
  GtkScale       *symbolPhaseScale;
  GtkButton      *setBaudRateButton;
  GtkButton      *detectBaudRateFACButton;
  GtkButton      *detectBaudRateNLNButton;
  GtkScale       *fineBaudScale;

  /* Matched filter widgets */
  GtkRadioButton *matchedFilterBypassRadioButton;
  GtkRadioButton *matchedFilterRRCRadioButton;
  GtkAlignment   *rootRaisedCosineAlignment;
  GtkScale       *rollOffScale;

  /* Spectrum source widgets */
  GtkRadioButton *powerSpectrumRadioButton;
  GtkRadioButton *cycloSpectrumRadioButton;
  GtkRadioButton *noSpectrumRadioButton;

  /* Symbol recorder widgets */
  GtkTextView    *symbolTextView;
  GtkTextBuffer  *symbolTextBuffer;

  struct sigutils_channel channel;
};

void suscan_gui_destroy(struct suscan_gui *gui);

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

void suscan_gui_setup_logging(struct suscan_gui *gui);

struct suscan_gui_source_config *suscan_gui_get_selected_source(
    struct suscan_gui *gui);

SUBOOL suscan_gui_source_config_parse(struct suscan_gui_source_config *config);

void suscan_gui_set_config(
    struct suscan_gui *gui,
    struct suscan_gui_source_config *config);

void suscan_gui_update_state(
    struct suscan_gui *gui,
    enum suscan_gui_state state);

void suscan_gui_detach_all_inspectors(struct suscan_gui *gui);

SUBOOL suscan_gui_connect(struct suscan_gui *gui);
void suscan_gui_disconnect(struct suscan_gui *gui);
void suscan_gui_quit(struct suscan_gui *gui);

/* Spectrum API */
void suscan_gui_spectrum_init(struct suscan_gui_spectrum *spectrum);

void suscan_spectrum_finalize(struct suscan_gui_spectrum *spectrum);

void suscan_gui_spectrum_set_mode(
    struct suscan_gui_spectrum *spectrum,
    enum suscan_gui_spectrum_mode mode);

void suscan_gui_spectrum_update(
    struct suscan_gui_spectrum *spectrum,
    struct suscan_analyzer_psd_msg *msg);

void suscan_gui_spectrum_update_channels(
    struct suscan_gui_spectrum *spectrum,
    struct sigutils_channel **channel_list,
    unsigned int channel_count);

void suscan_gui_spectrum_configure(
    struct suscan_gui_spectrum *spectrum,
    GtkWidget *widget);

void suscan_gui_spectrum_redraw(
    struct suscan_gui_spectrum *spectrum,
    cairo_t *cr);

void suscan_gui_spectrum_parse_scroll(
    struct suscan_gui_spectrum *spectrum,
    const GdkEventScroll *ev);

void suscan_gui_spectrum_parse_motion(
    struct suscan_gui_spectrum *spectrum,
    const GdkEventMotion *ev);

void suscan_gui_spectrum_reset_selection(
    struct suscan_gui_spectrum *spectrum);

/* Constellation API */
void suscan_gui_constellation_init(
    struct suscan_gui_constellation *constellation);

void suscan_gui_constellation_clear(
    struct suscan_gui_constellation *constellation);

void suscan_gui_constellation_push_sample(
    struct suscan_gui_constellation *constellation,
    SUCOMPLEX sample);

/* Some message dialogs */
#define suscan_error(gui, title, fmt, arg...) \
    suscan_gui_msgbox(gui, GTK_MESSAGE_ERROR, title, fmt, ##arg)

#define suscan_warning(gui, title, fmt, arg...) \
    suscan_gui_msgbox(gui, GTK_MESSAGE_WARNING, title, fmt, ##arg)

/* Main GUI inspector list handling methods */
SUBOOL suscan_gui_remove_inspector(
    struct suscan_gui *gui,
    struct suscan_gui_inspector *insp);

SUBOOL suscan_gui_add_inspector(
    struct suscan_gui *gui,
    struct suscan_gui_inspector *insp);

struct suscan_gui_inspector *suscan_gui_get_inspector(
    const struct suscan_gui *gui,
    uint32_t inspector_id);

/* Inspector GUI functions */
void suscan_gui_inspector_feed_w_batch(
    struct suscan_gui_inspector *inspector,
    const struct suscan_analyzer_sample_batch_msg *msg);

struct suscan_gui_inspector *suscan_gui_inspector_new(
    const struct sigutils_channel *channel,
    SUHANDLE handle);

SUBOOL suscan_gui_inspector_update_sensitiveness(
    struct suscan_gui_inspector *insp,
    const struct suscan_inspector_params *params);

void suscan_gui_inspector_detach(struct suscan_gui_inspector *insp);

void suscan_gui_inspector_close(struct suscan_gui_inspector *insp);

void suscan_gui_inspector_destroy(struct suscan_gui_inspector *inspector);

/* Recent source configuration management */
SUBOOL suscan_gui_append_recent(
    struct suscan_gui *gui,
    const struct suscan_source_config *config);

struct suscan_gui_recent *suscan_gui_recent_new(
    struct suscan_gui *gui,
    char *conf_string);

void suscan_gui_recent_destroy(struct suscan_gui_recent *recent);

void suscan_gui_retrieve_recent(struct suscan_gui *gui);

void suscan_gui_store_recent(struct suscan_gui *gui);

#endif /* _GUI_GUI_H */
