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

SUPRIVATE SUBOOL
suscan_gui_settings_get_color(
    GdkRGBA *dest,
    const suscan_gui_t *gui,
    const char *field)
{
  gchar *str = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(str = g_settings_get_string(gui->g_settings, field), goto done);

  SU_TRYCATCH(gdk_rgba_parse(dest, str), goto done);

  ok = SU_TRUE;

done:
  if (str != NULL)
    g_free(str);

  return ok;
}

SUPRIVATE void
suscan_gui_settings_set_color(
    const GdkRGBA *dest,
    suscan_gui_t *gui,
    const char *field)
{
  SUBOOL ok = SU_FALSE;
  unsigned char r, g, b;
  char color[8];

  r = dest->red * 255;
  g = dest->green * 255;
  b = dest->blue * 255;

  snprintf(color, sizeof(color), "#%02x%02x%02x", r, g, b);

  g_settings_set_string(gui->g_settings, field, color);
}

void
suscan_gui_load_settings(suscan_gui_t *gui)
{
  gchar *win = NULL;
  SUPRIVATE struct suscan_analyzer_params analyzer_params =
      suscan_analyzer_params_INITIALIZER;

  /* Load general GUI parameters */
  SU_TRYCATCH(
      suscan_gui_settings_get_color(&gui->settings.pa_bg, gui, "pa-bg-color"),
      goto done);

  SU_TRYCATCH(
      suscan_gui_settings_get_color(&gui->settings.pa_fg, gui, "pa-fg-color"),
      goto done);

  SU_TRYCATCH(
      suscan_gui_settings_get_color(&gui->settings.pa_axes, gui, "pa-axes-color"),
      goto done);

  SU_TRYCATCH(
      suscan_gui_settings_get_color(&gui->settings.pa_text, gui, "pa-text-color"),
      goto done);

  SU_TRYCATCH(
      suscan_gui_settings_get_color(&gui->settings.insp_bg, gui, "insp-bg-color"),
      goto done);

  SU_TRYCATCH(
      suscan_gui_settings_get_color(&gui->settings.insp_fg, gui, "insp-fg-color"),
      goto done);

  SU_TRYCATCH(
      suscan_gui_settings_get_color(&gui->settings.insp_axes, gui, "insp-axes-color"),
      goto done);

  SU_TRYCATCH(
      suscan_gui_settings_get_color(&gui->settings.insp_text, gui, "insp-text-color"),
      goto done);

  SU_TRYCATCH(
      suscan_gui_settings_get_color(&gui->settings.lcd_bg, gui, "lcd-bg-color"),
      goto done);

  SU_TRYCATCH(
      suscan_gui_settings_get_color(&gui->settings.lcd_fg, gui, "lcd-fg-color"),
      goto done);

  /* Load detector parameters */
  analyzer_params.detector_params.alpha = g_settings_get_double(
      gui->g_settings,
      "spectrum-avg-factor");

  analyzer_params.detector_params.beta = g_settings_get_double(
      gui->g_settings,
      "signal-avg-factor");

  analyzer_params.detector_params.gamma = g_settings_get_double(
      gui->g_settings,
      "noise-avg-factor");

  analyzer_params.detector_params.snr = g_settings_get_double(
      gui->g_settings,
      "snr-threshold");

  SU_TRYCATCH(
      win = g_settings_get_string(gui->g_settings, "window-func"),
      goto done);
  analyzer_params.detector_params.window = suscan_gui_str_to_window(win);

  /* FFT Window Size */
  analyzer_params.detector_params.window_size = g_settings_get_uint(
      gui->g_settings,
      "buffer-size");

  /* Update intervals */
  analyzer_params.channel_update_int = g_settings_get_double(
      gui->g_settings,
      "channel-interval");

  analyzer_params.psd_update_int = g_settings_get_double(
      gui->g_settings,
      "psd-interval");

  /* TODO: send update message to analyzer */
  gui->analyzer_params = analyzer_params;

done:
  if (win != NULL)
    g_free(win);
}

void
suscan_gui_store_settings(suscan_gui_t *gui)
{
  /* Store general GUI parameters */
  suscan_gui_settings_set_color(&gui->settings.pa_bg, gui, "pa-bg-color");

  suscan_gui_settings_set_color(&gui->settings.pa_fg, gui, "pa-fg-color");

  suscan_gui_settings_set_color(&gui->settings.pa_axes, gui, "pa-axes-color");

  suscan_gui_settings_set_color(&gui->settings.pa_text, gui, "pa-text-color");

  suscan_gui_settings_set_color(&gui->settings.pa_bg, gui, "insp-bg-color");

  suscan_gui_settings_set_color(&gui->settings.pa_fg, gui, "insp-fg-color");

  suscan_gui_settings_set_color(&gui->settings.pa_axes, gui, "insp-axes-color");

  suscan_gui_settings_set_color(&gui->settings.pa_text, gui, "insp-text-color");

  suscan_gui_settings_set_color(&gui->settings.lcd_bg, gui, "lcd-bg-color");

  suscan_gui_settings_set_color(&gui->settings.lcd_fg, gui, "lcd-fg-color");

  g_settings_set_double(
      gui->g_settings,
      "spectrum-avg-factor",
      gui->analyzer_params.detector_params.alpha);

  g_settings_set_double(
      gui->g_settings,
      "signal-avg-factor",
      gui->analyzer_params.detector_params.beta);

  g_settings_set_double(
      gui->g_settings,
      "noise-avg-factor",
      gui->analyzer_params.detector_params.gamma);

  g_settings_set_double(
      gui->g_settings,
      "snr-threshold",
      gui->analyzer_params.detector_params.snr);

  g_settings_set_string(
      gui->g_settings,
      "window-func",
      suscan_gui_window_to_str(gui->analyzer_params.detector_params.window));

  g_settings_set_uint(
      gui->g_settings,
      "buffer-size",
      gui->analyzer_params.detector_params.window_size);

  g_settings_set_double(
      gui->g_settings,
      "channel-interval",
      gui->analyzer_params.channel_update_int);

  g_settings_set_double(
      gui->g_settings,
      "psd-interval",
      gui->analyzer_params.psd_update_int);

  g_settings_sync();
}
