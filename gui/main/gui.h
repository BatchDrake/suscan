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
#include <confdb.h>
#include <suscan.h>

#include <gtk/gtk.h>
#include <profile/profile.h>
#include <spectrum.h>
#include <lcd.h>
#include <palbox.h>

#include <inspector/inspector.h>
#include <symsrc/symtool.h>

#define SUSCAN_GUI_SETTINGS_ID          "org.actinid.SUScan"
#define SUSCAN_GUI_MAX_CHANNELS         10
#define SUSCAN_GUI_SPECTRUM_PANEL_WIDTH 320

struct suscan_gui_cfgui {
  suscan_config_t *config; /* Borrowed pointer */
  PTR_LIST(GtkWidget, widget);
  GtkGrid *grid;
};

enum suscan_gui_state {
  SUSCAN_GUI_STATE_STOPPED,
  SUSCAN_GUI_STATE_RUNNING,
  SUSCAN_GUI_STATE_RESTARTING,
  SUSCAN_GUI_STATE_STOPPING,
  SUSCAN_GUI_STATE_QUITTING
};

struct suscan_gui;

struct suscan_gui_spectrum_action {
  int index;
  struct suscan_gui *gui;
  const struct suscan_inspector_interface *insp_iface;
  suscan_object_t *demod;
};


struct suscan_gui_settings {
  GdkRGBA pa_fg;
  GdkRGBA pa_bg;
  GdkRGBA pa_axes;
  GdkRGBA pa_text;

  GdkRGBA insp_fg;
  GdkRGBA insp_bg;
  GdkRGBA insp_axes;
  GdkRGBA insp_text;

  GdkRGBA lcd_fg;
  GdkRGBA lcd_bg;
};

struct suscan_gui {
  /* Application settings */
  GSettings *g_settings; /* TODO: send to the deepest of hells */
  suscan_config_context_t *gtkui_ctx;
  suscan_config_context_t *demod_ctx;
  suscan_object_t *gtkui_obj;
  const suscan_object_t *demod_obj;

  struct suscan_gui_settings settings;

  /* Widgets */
  GtkBuilder *builder;
  GtkWindow *main;
  GtkLabel *titleLabel;
  GtkLabel *subTitleLabel;
  GtkToggleToolButton *toggleConnect;
  GtkToolButton *preferencesButton;
  GtkListStore *sourceListStore;
  GtkListStore *channelListStore;
  GtkDialog *settingsDialog;
  GtkDialog *aboutDialog;
  GtkGrid *sourceGrid;
  GtkGrid *spectrumGrid;
  GtkComboBox *sourceCombo;
  GtkHeaderBar *headerBar;
  GtkMenuBar *menuBar;
  GtkBox *freqBox;
  SuGtkLcd *freqLcd;
  GtkSpinButton *throttleSampRateSpinButton;
  GtkCheckButton *throttleOverrideCheckButton;

  GtkGrid           *spectrumControlsGrid;
  GtkToggleButton   *overlayChannelToggleButton;
  GtkToggleButton   *autoGainToggleButton;
  GtkScale          *gainScale;
  GtkScale          *rangeScale;
  GtkScale          *panadapterScale;
  SuGtkPalBox       *waterfallPalBox;

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

  /* Setting dialogs widgets */
  GtkStack       *settingsViewStack;
  GtkColorButton *paFgColorButton;
  GtkColorButton *paBgColorButton;
  GtkColorButton *paTextColorButton;
  GtkColorButton *paAxesColorButton;

  GtkColorButton *inspFgColorButton;
  GtkColorButton *inspBgColorButton;
  GtkColorButton *inspTextColorButton;
  GtkColorButton *inspAxesColorButton;

  GtkColorButton *lcdFgColorButton;
  GtkColorButton *lcdBgColorButton;

  GtkFrame *channelDiscoveryFrame;
  GtkFrame *colorsFrame;
  GtkFrame *demodulatorsFrame;

  /* Demodulator list widgets */
  GtkListStore *demodulatorsListStore;

  /* Source summary */
  GtkLabel *spectrumSampleRateLabel;

  GtkNotebook *analyzerViewsNotebook;

  GtkTreeView *logMessagesTreeView;
  GtkListStore *logMessagesListStore;

  /* Symtool widgets */
  GtkNotebook *symToolNotebook;

  /* Settings widgets */
  GtkEntry *alphaEntry;
  GtkEntry *betaEntry;
  GtkEntry *gammaEntry;
  GtkEntry *snrEntry;

  GtkEntry *bufSizeEntry;
  GtkEntry *chIntervalEntry;
  GtkEntry *psdIntervalEntry;

  GtkRadioButton *rectangularWindowButton;
  GtkRadioButton *hammingWindowButton;
  GtkRadioButton *hannWindowButton;
  GtkRadioButton *blackmannHarrisWindowButton;
  GtkRadioButton *flatTopWindowButton;

  guint stack_first_ndx;
  GtkListBox *settingsSelectorListBox;

  /* Profile name dialog */
  GtkDialog *profileNameDialog;
  GtkEntry *profileNameEntry;
  GtkLabel *profileTextLabel;

  /* Demodulator chooser dialog */
  GtkDialog *chooseDemodulatorDialog;
  GtkTreeView *selectDemodTreeView;
  suscan_object_t *selected_demod;
  GtkMenuItem *demodMenuItem;

  /* Demodulator properties dialog */
  GtkDialog *demodPropertiesDialog;
  GtkEntry *demodNameEntry;
  GtkLabel *demodClassLabel;
  GtkListStore *demodPropertiesListStore;
  GtkTreeView *demodPropertiesTreeView;
  GtkTreeView *demodListTreeView;
  GtkButton *demodPropertiesButton;
  GtkButton *demodRemoveButton;
  /* Profile menu */
  GtkMenu *profilesMenu;

  PTR_LIST(GtkRadioMenuItem, profileRadioButton);

  /* GUI state */
  enum suscan_gui_state state;

  /* Analyzer integration */
  struct suscan_analyzer_params analyzer_params;
  suscan_analyzer_t *analyzer;
  struct suscan_mq mq_out;
  GThread *async_thread;

  /* Main spectrum */
  SuGtkSpectrum *spectrum;

  /* Spectrum actions */
  PTR_LIST(struct suscan_gui_spectrum_action, action);

  /* Inspector tab list */
  PTR_LIST(suscan_gui_inspector_t, inspector);

  /* Symbol tool tab list */
  PTR_LIST(suscan_gui_symtool_t, symtool);

  /* Source configuration profiles */
  PTR_LIST(suscan_gui_profile_t, profile);
  suscan_gui_profile_t *active_profile;

  /* Waterfall palettes */
  PTR_LIST(suscan_gui_palette_t, palette);

  /* Flags to prevent nested callback calling */
  SUBOOL updating_settings;
  SUBOOL updating_state;
};

typedef struct suscan_gui suscan_gui_t;

void suscan_gui_clear_profile_menu(suscan_gui_t *gui);

void suscan_gui_destroy(suscan_gui_t *gui);

void suscan_gui_apply_settings_on_inspector(
    suscan_gui_t *gui,
    suscan_gui_inspector_t *insp);

suscan_gui_t *suscan_gui_new(void);

SUBOOL suscan_graphical_init(int argc, char **argv);

SUBOOL suscan_gui_start(
    int argc,
    char **argv,
    struct suscan_source_config **config_list,
    unsigned int config_count);

/* Internal API */
SUBOOL suscan_gui_load_all_widgets(suscan_gui_t *gui);

SUBOOL suscan_gui_start_async_thread(suscan_gui_t *gui);

void suscan_gui_setup_logging(suscan_gui_t *gui);

/* Generic UI functions */
void suscan_gui_text_entry_set_float(GtkEntry *entry, SUFLOAT value);

void suscan_gui_text_entry_set_scount(GtkEntry *entry, SUSCOUNT value);

void suscan_gui_text_entry_set_integer(GtkEntry *entry, int64_t value);

void suscan_gui_text_entry_set_freq(GtkEntry *entry, SUFREQ result);

SUBOOL suscan_gui_text_entry_get_float(GtkEntry *entry, SUFLOAT *result);

SUBOOL suscan_gui_text_entry_get_freq(GtkEntry *entry, SUFREQ *result);

SUBOOL suscan_gui_text_entry_get_scount(GtkEntry *entry, SUSCOUNT *result);

SUBOOL suscan_gui_text_entry_get_integer(GtkEntry *entry, int64_t *result);

void suscan_gui_msgbox(
    suscan_gui_t *gui,
    GtkMessageType type,
    const char *title,
    const char *fmt,
    ...);

SUBOOL suscan_gui_yes_or_no(
    suscan_gui_t *gui,
    const char *title,
    const char *fmt,
    ...);

const char *suscan_gui_prompt(
    suscan_gui_t *gui,
    const char *title,
    const char *text,
    const char *defl);

const char *suscan_gui_ask_for_profile_name(
    suscan_gui_t *gui,
    const char *title,
    const char *text);

/* Generic configuration UI */
SUBOOL suscan_gui_cfgui_parse(struct suscan_gui_cfgui *ui);

void suscan_gui_cfgui_dump(struct suscan_gui_cfgui *ui);

void suscan_gui_cfgui_destroy(struct suscan_gui_cfgui *ui);

GtkWidget *suscan_gui_cfgui_get_root(const struct suscan_gui_cfgui *ui);

struct suscan_gui_cfgui *suscan_gui_cfgui_new(suscan_config_t *config);

/* GUI settings */
void suscan_gui_settings_from_dialog(suscan_gui_t *gui);
void suscan_gui_settings_to_dialog(suscan_gui_t *gui);

void suscan_gui_apply_settings(suscan_gui_t *gui);
void suscan_gui_apply_settings_on_inspector(
    suscan_gui_t *gui,
    suscan_gui_inspector_t *insp);

/* Analyzer params API */
void suscan_gui_analyzer_params_to_dialog(suscan_gui_t *gui);

SUBOOL suscan_gui_analyzer_params_from_dialog(suscan_gui_t *gui);

/* GUI State */
void suscan_gui_update_state(
    suscan_gui_t *gui,
    enum suscan_gui_state state);

void suscan_gui_detach_all_inspectors(suscan_gui_t *gui);

SUBOOL suscan_gui_set_title(suscan_gui_t *gui, const char *title);

SUBOOL suscan_gui_connect(suscan_gui_t *gui);
void suscan_gui_reconnect(suscan_gui_t *gui);
void suscan_gui_disconnect(suscan_gui_t *gui);
void suscan_gui_quit(suscan_gui_t *gui);

/* Some message dialogs */
#define suscan_error(gui, title, fmt, arg...) \
    suscan_gui_msgbox(gui, GTK_MESSAGE_ERROR, title, fmt, ##arg)

#define suscan_warning(gui, title, fmt, arg...) \
    suscan_gui_msgbox(gui, GTK_MESSAGE_WARNING, title, fmt, ##arg)

/* Main GUI inspector list handling methods */
SUBOOL suscan_gui_add_all_inspector_actions(suscan_gui_t *gui);

SUBOOL suscan_gui_remove_inspector(
    suscan_gui_t *gui,
    suscan_gui_inspector_t *insp);

SUBOOL suscan_gui_add_inspector(
    suscan_gui_t *gui,
    suscan_gui_inspector_t *insp);

suscan_gui_inspector_t *suscan_gui_get_inspector(
    const suscan_gui_t *gui,
    uint32_t inspector_id);

struct suscan_gui_spectrum_action *suscan_gui_assert_spectrum_action(
    suscan_gui_t *gui,
    const struct suscan_inspector_interface *insp_iface,
    suscan_object_t *demod);

/* Main GUI symtool list handling methods */
SUBOOL suscan_gui_remove_symtool(
    suscan_gui_t *gui,
    suscan_gui_symtool_t *symtool);

SUBOOL suscan_gui_add_symtool(
    suscan_gui_t *gui,
    suscan_gui_symtool_t *symtool);

suscan_gui_symtool_t *suscan_gui_get_symtool(
    const suscan_gui_t *gui,
    uint32_t symtool_id);

/* Source API */
SUBOOL suscan_gui_load_profiles(suscan_gui_t *gui);

void suscan_gui_show_profile(
    suscan_gui_t *gui,
    suscan_gui_profile_t *profile);

SUBOOL suscan_gui_select_profile(
    suscan_gui_t *gui,
    suscan_gui_profile_t *profile);

SUBOOL suscan_gui_update_profile_menu(suscan_gui_t *gui);

suscan_gui_profile_t *suscan_gui_lookup_profile(
    const suscan_gui_t *gui,
    const char *label);

SUBOOL suscan_gui_create_profile(suscan_gui_t *gui, const char *name);

SUBOOL suscan_gui_parse_all_changed_profiles(suscan_gui_t *gui);

void suscan_gui_reset_all_profiles(suscan_gui_t *gui);

struct suscan_gui_src_ui *suscan_gui_lookup_source_config(
    const suscan_gui_t *gui,
    const struct suscan_source *src);

/* Palettes */
SUBOOL suscan_gui_load_palettes(suscan_gui_t *gui);

suscan_gui_palette_t *suscan_gui_lookup_palette(
    const suscan_gui_t *gui,
    const char *name);

SUBOOL suscan_gui_populate_pal_box(
    const suscan_gui_t *gui,
    SuGtkPalBox *palbox);

/* Demodulator API */
suscan_object_t *suscan_gui_demod_lookup(
    const suscan_gui_t *gui,
    const char *name);

SUBOOL suscan_gui_demod_append(
    suscan_gui_t *gui,
    const char *name,
    suscan_object_t *object);

SUBOOL suscan_gui_demod_remove(suscan_gui_t *gui, suscan_object_t *obj);

const suscan_object_t *suscan_gui_ask_for_demod(suscan_gui_t *gui);

const char *suscan_gui_show_demod_properties(
    suscan_gui_t *gui,
    const suscan_object_t *obj);

void suscan_gui_demod_refresh_ui(suscan_gui_t *gui);

SUBOOL suscan_gui_load_settings(suscan_gui_t *gui);

void suscan_gui_store_settings(suscan_gui_t *gui);

#endif /* _GUI_GUI_H */
