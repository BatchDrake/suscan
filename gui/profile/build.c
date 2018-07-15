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

#include "profile.h"

SUBOOL
suscan_gui_profile_load_all_widgets(suscan_gui_profile_t *profile)
{
  SU_TRYCATCH(
      profile->root =
          GTK_WIDGET(gtk_builder_get_object(
              profile->builder,
              "fRoot")),
      return SU_FALSE);

  SU_TRYCATCH(
      profile->selector =
          GTK_WIDGET(gtk_builder_get_object(
              profile->builder,
              "grSelector")),
      return SU_FALSE);

  /* Entries */
  SU_TRYCATCH(
      profile->frequencyEntry =
          GTK_ENTRY(gtk_builder_get_object(
              profile->builder,
              "eFrequency")),
      return SU_FALSE);

  SU_TRYCATCH(
      profile->sampleRateEntry =
          GTK_ENTRY(gtk_builder_get_object(
              profile->builder,
              "eSampleRate")),
      return SU_FALSE);

  SU_TRYCATCH(
      profile->bandwidthEntry =
          GTK_ENTRY(gtk_builder_get_object(
              profile->builder,
              "eBandwidth")),
      return SU_FALSE);

  /* Labels */
  SU_TRYCATCH(
      profile->profileNameLabel =
          GTK_LABEL(gtk_builder_get_object(
              profile->builder,
              "lProfileName")),
      return SU_FALSE);

  /* CheckButtons */
  SU_TRYCATCH(
      profile->iqBalanceCheckButton =
          GTK_CHECK_BUTTON(gtk_builder_get_object(
              profile->builder,
              "cbIQBalance")),
      return SU_FALSE);

  SU_TRYCATCH(
      profile->removeDcCheckButton =
          GTK_CHECK_BUTTON(gtk_builder_get_object(
              profile->builder,
              "cbRemoveDC")),
      return SU_FALSE);

  SU_TRYCATCH(
      profile->loopCheckButton =
          GTK_CHECK_BUTTON(gtk_builder_get_object(
              profile->builder,
              "cbLoop")),
      return SU_FALSE);

  /* Radio buttons */
  SU_TRYCATCH(
      profile->sdrRadioButton =
          GTK_RADIO_BUTTON(gtk_builder_get_object(
              profile->builder,
              "rbSDR")),
      return SU_FALSE);

  SU_TRYCATCH(
      profile->fileRadioButton =
          GTK_RADIO_BUTTON(gtk_builder_get_object(
              profile->builder,
              "rbFile")),
      return SU_FALSE);

  /* Spin Buttons */
  SU_TRYCATCH(
      profile->averageSpinButton =
          GTK_SPIN_BUTTON(gtk_builder_get_object(
              profile->builder,
              "sbAverage")),
      return SU_FALSE);

  SU_TRYCATCH(
      profile->channelSpinButton =
          GTK_SPIN_BUTTON(gtk_builder_get_object(
              profile->builder,
              "sbChannel")),
      return SU_FALSE);

  /* ComboBoxes */
  SU_TRYCATCH(
      profile->deviceComboBoxText =
          GTK_COMBO_BOX_TEXT(gtk_builder_get_object(
              profile->builder,
              "cbtDevice")),
      return SU_FALSE);

  SU_TRYCATCH(
      profile->antennaComboBoxText =
          GTK_COMBO_BOX_TEXT(gtk_builder_get_object(
              profile->builder,
              "cbtAntenna")),
      return SU_FALSE);

  SU_TRYCATCH(
      profile->formatComboBoxText =
          GTK_COMBO_BOX_TEXT(gtk_builder_get_object(
              profile->builder,
              "cbtFormat")),
      return SU_FALSE);

  /* Other */
  SU_TRYCATCH(
      profile->pathFileChooserButton =
          GTK_FILE_CHOOSER_BUTTON(gtk_builder_get_object(
              profile->builder,
              "fcbPath")),
      return SU_FALSE);

  SU_TRYCATCH(
      profile->sdrControlsFrame =
          GTK_GRID(gtk_builder_get_object(
              profile->builder,
              "grSDRControls")),
      return SU_FALSE);

  SU_TRYCATCH(
      profile->fileControlsFrame =
          GTK_GRID(gtk_builder_get_object(
              profile->builder,
              "grFileControls")),
      return SU_FALSE);

  SU_TRYCATCH(
      profile->gainsFrame =
          GTK_FRAME(gtk_builder_get_object(
              profile->builder,
              "frGains")),
      return SU_FALSE);

  return SU_TRUE;
}
