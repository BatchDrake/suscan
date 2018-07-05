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

#define SU_LOG_DOMAIN "gui-profile"

#include "gui.h"

void
suscan_gui_profile_destroy(suscan_gui_profile_t *profile)
{
  if (profile->builder != NULL)
    g_object_unref(profile->builder);

  free(profile);
}

void
suscan_gui_profile_update_sensitivity(suscan_gui_profile_t *profile)
{
  SUBOOL is_sdr;

  is_sdr = gtk_toggle_button_get_active(
      GTK_TOGGLE_BUTTON(profile->sdrRadioButton));

  gtk_widget_set_sensitive(GTK_WIDGET(profile->sdrControlsFrame), is_sdr);
  gtk_widget_set_sensitive(GTK_WIDGET(profile->fileControlsFrame), !is_sdr);

  gtk_widget_set_sensitive(
      GTK_WIDGET(profile->sdrRadioButton),
      suscan_source_device_get_count() > 0);
}

SUBOOL
suscan_gui_profile_refresh_config(suscan_gui_profile_t *profile)
{
  int64_t ival;
  unsigned int index;
  const suscan_source_device_t *dev;
  gchar *path;
  const gchar *id;
  SUFREQ bw;
  SUBOOL is_sdr;
  enum suscan_source_format format;

  is_sdr = gtk_toggle_button_get_active(
      GTK_TOGGLE_BUTTON(profile->sdrRadioButton));

  /* Get frequency */
  if (!suscan_gui_text_entry_get_integer(profile->frequencyEntry, &ival)) {
    SU_ERROR("Invalid frequency");
    return SU_FALSE;
  }

  suscan_source_config_set_freq(profile->config, ival);

  /* Get sample rate */
  if (!suscan_gui_text_entry_get_integer(profile->sampleRateEntry, &ival)) {
    SU_ERROR("Invalid sample rate");
    return SU_FALSE;
  }

  suscan_source_config_set_samp_rate(profile->config, ival);

  /* Get source type and format */
  switch (gtk_combo_box_get_active(
      GTK_COMBO_BOX(profile->formatComboBoxText))) {
    case 1:
      format = SUSCAN_SOURCE_FORMAT_RAW;
      break;

    case 2:
      format = SUSCAN_SOURCE_FORMAT_WAV;
      break;

    default:
      format = SUSCAN_SOURCE_FORMAT_AUTO;
  }

  suscan_source_config_set_type_format(
      profile->config,
      is_sdr ? SUSCAN_SOURCE_TYPE_SDR : SUSCAN_SOURCE_TYPE_FILE,
      format);

  if ((path = gtk_file_chooser_get_filename(
      GTK_FILE_CHOOSER(profile->pathFileChooserButton))) != NULL) {
    (void) suscan_source_config_set_path(profile->config, path);
    g_free(path);
  }

  /* Get bandwidth */
  if (!suscan_gui_text_entry_get_freq(profile->bandwidthEntry, &bw)) {
    SU_ERROR("Invalid bandwidth");
    return SU_FALSE;
  }

  suscan_source_config_set_bandwidth(profile->config, bw);

  /* Get spin button values */
  suscan_source_config_set_average(
      profile->config,
      gtk_spin_button_get_value(profile->averageSpinButton));

  suscan_source_config_set_channel(
      profile->config,
      gtk_spin_button_get_value(profile->channelSpinButton));

  /* Get check buttons */
  suscan_source_config_set_dc_remove(
      profile->config,
      gtk_toggle_button_get_active(
          GTK_TOGGLE_BUTTON(profile->removeDcCheckButton)));

  suscan_source_config_set_iq_balance(
      profile->config,
      gtk_toggle_button_get_active(
          GTK_TOGGLE_BUTTON(profile->iqBalanceCheckButton)));

  suscan_source_config_set_loop(
      profile->config,
      gtk_toggle_button_get_active(
          GTK_TOGGLE_BUTTON(profile->loopCheckButton)));

  /* Get device */
  SU_TRYCATCH(
      id = gtk_combo_box_get_active_id(
          GTK_COMBO_BOX(profile->deviceComboBoxText)),
      return SU_FALSE);
  SU_TRYCATCH(sscanf(id, "%u", &index) == 1, return SU_FALSE);
  SU_TRYCATCH(dev = suscan_source_device_get_by_index(index), return SU_FALSE);
  SU_TRYCATCH(
      suscan_source_config_set_device(profile->config, dev),
      return SU_FALSE);

  return SU_TRUE;
}

SUBOOL
suscan_gui_profile_refresh_gui(suscan_gui_profile_t *profile)
{
  const char *string;

  if ((string = suscan_source_config_get_label(profile->config)) == NULL)
    string = "<Unlabeled profile>";

  gtk_label_set_text(profile->profileNameLabel, string);

  suscan_gui_text_entry_set_integer(
      profile->frequencyEntry,
      suscan_source_config_get_freq(profile->config));

  suscan_gui_text_entry_set_integer(
      profile->sampleRateEntry,
      suscan_source_config_get_samp_rate(profile->config));

  suscan_gui_text_entry_set_freq(
      profile->bandwidthEntry,
      suscan_source_config_get_bandwidth(profile->config));

  /* Set path */
  if ((string = suscan_source_config_get_path(profile->config)) != NULL)
    gtk_file_chooser_set_filename(
        GTK_FILE_CHOOSER(profile->pathFileChooserButton),
        string);

  /* Set spin button values */
  gtk_spin_button_set_value(
      profile->averageSpinButton,
      suscan_source_config_get_average(profile->config));

  gtk_spin_button_set_value(
      profile->channelSpinButton,
      suscan_source_config_get_channel(profile->config));

  /* Set source type */
  gtk_toggle_button_set_active(
        GTK_TOGGLE_BUTTON(profile->sdrRadioButton),
        suscan_source_config_get_type(profile->config)
          == SUSCAN_SOURCE_TYPE_SDR);

  gtk_toggle_button_set_active(
        GTK_TOGGLE_BUTTON(profile->fileRadioButton),
        suscan_source_config_get_type(profile->config)
          != SUSCAN_SOURCE_TYPE_SDR);

  switch (suscan_source_config_get_format(profile->config)) {
    case SUSCAN_SOURCE_FORMAT_RAW:
      gtk_combo_box_set_active(GTK_COMBO_BOX(profile->formatComboBoxText), 1);
      break;

    case SUSCAN_SOURCE_FORMAT_WAV:
      gtk_combo_box_set_active(GTK_COMBO_BOX(profile->formatComboBoxText), 2);
      break;

    default:
      gtk_combo_box_set_active(GTK_COMBO_BOX(profile->formatComboBoxText), 0);
      break;
  }

  /* Set check buttons */
  gtk_toggle_button_set_active(
        GTK_TOGGLE_BUTTON(profile->removeDcCheckButton),
        suscan_source_config_get_dc_remove(profile->config));

  gtk_toggle_button_set_active(
        GTK_TOGGLE_BUTTON(profile->iqBalanceCheckButton),
        suscan_source_config_get_iq_balance(profile->config));

  gtk_toggle_button_set_active(
        GTK_TOGGLE_BUTTON(profile->loopCheckButton),
        suscan_source_config_get_loop(profile->config));

  suscan_gui_profile_update_sensitivity(profile);

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_gui_profile_on_device(
    suscan_source_device_t *dev,
    unsigned int index,
    void *private)
{
  suscan_gui_profile_t *profile = (suscan_gui_profile_t *) private;
  char id[16];

  snprintf(id, sizeof(id), "%u", index);

  gtk_combo_box_text_append(
      profile->deviceComboBoxText,
      id,
      suscan_source_device_get_desc(dev));

  return SU_TRUE;
}

SUPRIVATE void
suscan_gui_profile_populate_device_combo(suscan_gui_profile_t *profile)
{
  (void) suscan_source_device_walk(suscan_gui_profile_on_device, profile);

  gtk_combo_box_set_active(GTK_COMBO_BOX(profile->deviceComboBoxText), 0);
}

suscan_gui_profile_t *
suscan_gui_profile_new(suscan_source_config_t *cfg)
{
  suscan_gui_profile_t *new = NULL;

  SU_TRYCATCH(new = calloc(1, sizeof (suscan_gui_profile_t)), goto fail);

  new->config = cfg;

  SU_TRYCATCH(
      new->builder = gtk_builder_new_from_file(PKGDATADIR "/gui/profile.glade"),
      goto fail);

  SU_TRYCATCH(suscan_gui_profile_load_all_widgets(new), goto fail);

  suscan_gui_profile_populate_device_combo(new);

  SU_TRYCATCH(suscan_gui_profile_refresh_gui(new), goto fail);

  gtk_builder_connect_signals(new->builder, new);

  return new;

fail:
  if (new != NULL)
    suscan_gui_profile_destroy(new);

  return NULL;
}
