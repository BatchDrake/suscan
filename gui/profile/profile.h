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

#ifndef _GUI_PROFILE_H
#define _GUI_PROFILE_H

#include <sigutils/sigutils.h>
#include <suscan.h>
#include <analyzer/source.h>

#include <gtk/gtk.h>

struct suscan_gui;

struct suscan_gui_gain_slider {
  const struct suscan_source_gain_desc *desc;
  GtkLabel *nameLabel;
  GtkLabel *dbLabel;
  GtkScale *gainScale;
  GtkAdjustment *gainAdjustment;
};

struct suscan_gui_profile;

struct suscan_gui_gain_ui {
  struct suscan_gui_profile *profile;
  const suscan_source_device_t *device;
  GtkGrid *uiGrid;

  PTR_LIST(struct suscan_gui_gain_slider, gain_slider);
};

struct suscan_gui_profile {
  struct suscan_gui *gui;
  GtkBuilder *builder;
  GtkWidget *root;
  GtkWidget *selector;

  /* GUI Widgets */
  GtkEntry *frequencyEntry;
  GtkEntry *sampleRateEntry;
  GtkSpinButton *averageSpinButton;
  GtkCheckButton *iqBalanceCheckButton;
  GtkCheckButton *removeDcCheckButton;
  GtkComboBoxText *deviceComboBoxText;
  GtkComboBoxText *antennaComboBoxText;
  GtkSpinButton *channelSpinButton;
  GtkEntry *bandwidthEntry;
  GtkRadioButton *sdrRadioButton;
  GtkRadioButton *fileRadioButton;
  GtkFileChooserButton *pathFileChooserButton;
  GtkComboBoxText *formatComboBoxText;
  GtkCheckButton *loopCheckButton;
  GtkLabel *profileNameLabel;
  GtkFrame *gainsFrame;

  GtkGrid *sdrControlsFrame;
  GtkGrid *fileControlsFrame;

  PTR_LIST(struct suscan_gui_gain_ui, gain_ui_cache); /* UI cache */
  struct suscan_gui_gain_ui *gain_ui; /* Curren gain UI */

  const suscan_source_device_t *device; /* Currently selected device */
  suscan_source_config_t *config; /* Got from source_config_walk, borrowed */
  suscan_source_t *source;

  SUBOOL changed;
  SUBOOL in_callback;
};

typedef struct suscan_gui_profile suscan_gui_profile_t;

SUINLINE void
suscan_gui_profile_set_gui(
    suscan_gui_profile_t *profile,
    struct suscan_gui *gui)
{
  profile->gui = gui;
}

SUINLINE struct suscan_gui *
suscan_gui_profile_get_gui(suscan_gui_profile_t *profile)
{
  return profile->gui;
}

SUINLINE GtkWidget *
suscan_gui_profile_get_root(const suscan_gui_profile_t *profile)
{
  return profile->root;
}

SUINLINE GtkWidget *
suscan_gui_profile_get_selector(const suscan_gui_profile_t *profile)
{
  return profile->selector;
}

SUINLINE SUBOOL
suscan_gui_profile_has_changed(const suscan_gui_profile_t *profile)
{
  return profile->changed;
}

SUINLINE void
suscan_gui_profile_reset_changed(suscan_gui_profile_t *profile)
{
  profile->changed = SU_FALSE;
}

/************************** Callbacks ****************************************/
void suscan_gui_profile_on_changed(GtkWidget *widget, gpointer data);

/************************* Suscan gain UI ************************************/
void suscan_gui_gain_ui_destroy(struct suscan_gui_gain_ui *ui);

struct suscan_gui_gain_ui *suscan_gui_gain_ui_new(
    const suscan_source_device_t *device);

SUBOOL suscan_gui_gain_ui_walk_gains(
    const struct suscan_gui_gain_ui *ui,
    SUBOOL (*gain_cb) (void *private, const char *name, SUFLOAT value),
    void *private);

SUBOOL suscan_gui_gain_ui_set_gain(
    const struct suscan_gui_gain_ui *ui,
    const char *name,
    SUFLOAT value);

void suscan_gui_gain_ui_set_profile(
    struct suscan_gui_gain_ui *ui,
    suscan_gui_profile_t *profile);

/*************************** Internal API ************************************/
void suscan_gui_profile_update_sensitivity(suscan_gui_profile_t *profile);

void suscan_gui_profile_update_antennas(suscan_gui_profile_t *profile);

SUBOOL suscan_gui_profile_load_all_widgets(suscan_gui_profile_t *profile);

SUBOOL suscan_gui_profile_update_device(suscan_gui_profile_t *profile);

SUBOOL suscan_gui_profile_update_gains(suscan_gui_profile_t *profile);

SUBOOL suscan_gui_profile_refresh_config(suscan_gui_profile_t *profile);

SUBOOL suscan_gui_profile_refresh_gui(suscan_gui_profile_t *profile);

SUBOOL suscan_gui_profile_update_gain_ui(
    suscan_gui_profile_t *profile,
    const suscan_source_device_t *device);

void suscan_gui_profile_destroy(suscan_gui_profile_t *profile);

suscan_gui_profile_t *suscan_gui_profile_new(suscan_source_config_t *cfg);

#endif /* _GUI_PROFILE_H */
