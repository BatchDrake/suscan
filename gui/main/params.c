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

#define SU_LOG_DOMAIN "gui-params"

#include <confdb.h>
#include "modemctl.h"
#include "gui.h"


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

