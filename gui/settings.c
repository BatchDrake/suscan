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

#define SU_LOG_DOMAIN "settings"

#include "gui.h"

SUPRIVATE enum sigutils_channel_detector_window
suscan_gui_str_to_window(const char *window)
{
  if (strcasecmp(window, "rectangular") == 0
      || strcasecmp(window, "none") == 0
      || strcasecmp(window, "") == 0) {
    return SU_CHANNEL_DETECTOR_WINDOW_NONE;
  } else if (strcasecmp(window, "hamming") == 0) {
    return SU_CHANNEL_DETECTOR_WINDOW_HAMMING;
  } else if (strcasecmp(window, "hann") == 0) {
    return SU_CHANNEL_DETECTOR_WINDOW_HANN;
  } else if (strcasecmp(window, "blackmann-harris") == 0) {
    return SU_CHANNEL_DETECTOR_WINDOW_BLACKMANN_HARRIS;
  } else if (strcasecmp(window, "flat-top") == 0) {
    return SU_CHANNEL_DETECTOR_WINDOW_FLAT_TOP;
  } else {
    SU_WARNING("Invalid window function `%s'\n", window);
  }

  return SU_CHANNEL_DETECTOR_WINDOW_NONE;
}

SUPRIVATE const char *
suscan_gui_window_to_str(enum sigutils_channel_detector_window window)
{
  switch (window) {
    case SU_CHANNEL_DETECTOR_WINDOW_NONE:
      return "rectangular";

    case SU_CHANNEL_DETECTOR_WINDOW_HAMMING:
      return "hamming";

    case SU_CHANNEL_DETECTOR_WINDOW_HANN:
      return "hann";

    case SU_CHANNEL_DETECTOR_WINDOW_BLACKMANN_HARRIS:
      return "blackmann-harris";

    case SU_CHANNEL_DETECTOR_WINDOW_FLAT_TOP:
      return "flat-top";
  }

  return "none"; /* Formally equivalent to rectangular */
}

void
suscan_gui_retrieve_analyzer_params(struct suscan_gui *gui)
{
  gchar *win = NULL;
  SUPRIVATE struct suscan_analyzer_params analyzer_params =
      suscan_analyzer_params_INITIALIZER;

  /* Load detector parameters */
  analyzer_params.detector_params.alpha = g_settings_get_double(
      gui->settings,
      "spectrum-avg-factor");

  analyzer_params.detector_params.beta = g_settings_get_double(
      gui->settings,
      "signal-avg-factor");

  analyzer_params.detector_params.gamma = g_settings_get_double(
      gui->settings,
      "noise-avg-factor");

  analyzer_params.detector_params.snr = g_settings_get_double(
      gui->settings,
      "snr-threshold");

  SU_TRYCATCH(
      win = g_settings_get_string(gui->settings, "window-func"),
      goto done);
  analyzer_params.detector_params.window = suscan_gui_str_to_window(win);

  /* FFT Window Size */
  analyzer_params.detector_params.window_size = g_settings_get_uint(
      gui->settings,
      "buffer-size");

  /* Update intervals */
  analyzer_params.channel_update_int = g_settings_get_double(
      gui->settings,
      "channel-interval");

  analyzer_params.psd_update_int = g_settings_get_double(
      gui->settings,
      "psd-interval");

  /* TODO: send update message to analyzer */
  gui->analyzer_params = analyzer_params;

done:
  if (win != NULL)
    g_free(win);
}

void
suscan_gui_store_analyzer_params(struct suscan_gui *gui)
{
  g_settings_set_double(
      gui->settings,
      "spectrum-avg-factor",
      gui->analyzer_params.detector_params.alpha);

  g_settings_set_double(
      gui->settings,
      "signal-avg-factor",
      gui->analyzer_params.detector_params.beta);

  g_settings_set_double(
      gui->settings,
      "noise-avg-factor",
      gui->analyzer_params.detector_params.gamma);

  g_settings_set_double(
      gui->settings,
      "snr-threshold",
      gui->analyzer_params.detector_params.snr);

  g_settings_set_string(
      gui->settings,
      "window-func",
      suscan_gui_window_to_str(gui->analyzer_params.detector_params.window));

  g_settings_set_uint(
      gui->settings,
      "buffer-size",
      gui->analyzer_params.detector_params.window_size);

  g_settings_set_double(
      gui->settings,
      "channel-interval",
      gui->analyzer_params.channel_update_int);

  g_settings_set_double(
      gui->settings,
      "psd-interval",
      gui->analyzer_params.psd_update_int);

  g_settings_sync();
}
