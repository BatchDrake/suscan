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

#define SU_LOG_DOMAIN "gui-settings"

#include <confdb.h>
#include "modemctl.h"
#include "gui.h"

SUPRIVATE SUBOOL
suscan_gui_assert_settings_obj(suscan_gui_t *gui)
{
  const suscan_object_t *list;
  suscan_object_t *ui_settings = NULL;

  SU_TRYCATCH(
      list = suscan_config_context_get_list(gui->settings_ctx),
      goto fail);

  if ((gui->settings_obj = suscan_object_set_get(list, 0)) == NULL) {
    SU_TRYCATCH(
        ui_settings = suscan_object_new(SUSCAN_OBJECT_TYPE_OBJECT),
        goto fail);
    SU_TRYCATCH(
        suscan_config_context_put(gui->settings_ctx, ui_settings),
        goto fail);
    gui->settings_obj = ui_settings;
    ui_settings = NULL;
  }

  return SU_TRUE;

fail:
  if (ui_settings != NULL)
    suscan_object_destroy(ui_settings);

  return SU_FALSE;
}

void
suscan_gui_apply_settings_on_inspector(
    suscan_gui_t *gui,
    suscan_gui_inspector_t *insp)
{
  sugtk_spectrum_set_fg_color(insp->spectrum, gui->settings.insp_fg);
  sugtk_spectrum_set_bg_color(insp->spectrum, gui->settings.insp_bg);
  sugtk_spectrum_set_text_color(insp->spectrum, gui->settings.insp_text);
  sugtk_spectrum_set_axes_color(insp->spectrum, gui->settings.insp_axes);

  sugtk_constellation_set_fg_color(insp->constellation, gui->settings.insp_fg);
  sugtk_constellation_set_bg_color(insp->constellation, gui->settings.insp_bg);
  sugtk_constellation_set_axes_color(insp->constellation, gui->settings.insp_axes);

  sugtk_waveform_set_fg_color(insp->phasePlot, gui->settings.insp_fg);
  sugtk_waveform_set_bg_color(insp->phasePlot, gui->settings.insp_bg);
  sugtk_waveform_set_axes_color(insp->phasePlot, gui->settings.insp_axes);

  sugtk_histogram_set_fg_color(insp->histogram, gui->settings.insp_fg);
  sugtk_histogram_set_bg_color(insp->histogram, gui->settings.insp_bg);
  sugtk_histogram_set_axes_color(insp->histogram, gui->settings.insp_axes);
}

void
suscan_gui_apply_settings(suscan_gui_t *gui)
{
  unsigned int i;

  sugtk_spectrum_set_fg_color(gui->spectrum, gui->settings.pa_fg);
  sugtk_spectrum_set_bg_color(gui->spectrum, gui->settings.pa_bg);
  sugtk_spectrum_set_text_color(gui->spectrum, gui->settings.pa_text);
  sugtk_spectrum_set_axes_color(gui->spectrum, gui->settings.pa_axes);

  sugtk_lcd_set_fg_color(gui->freqLcd, gui->settings.lcd_fg);
  sugtk_lcd_set_bg_color(gui->freqLcd, gui->settings.lcd_bg);

  for (i = 0; i < gui->inspector_count; ++i)
    if (gui->inspector_list[i] != NULL)
      suscan_gui_apply_settings_on_inspector(gui, gui->inspector_list[i]);
}

void
suscan_gui_settings_to_dialog(suscan_gui_t *gui)
{
  gtk_color_chooser_set_rgba(
      GTK_COLOR_CHOOSER(gui->paFgColorButton),
      &gui->settings.pa_fg);

  gtk_color_chooser_set_rgba(
      GTK_COLOR_CHOOSER(gui->paBgColorButton),
      &gui->settings.pa_bg);

  gtk_color_chooser_set_rgba(
      GTK_COLOR_CHOOSER(gui->paAxesColorButton),
      &gui->settings.pa_axes);

  gtk_color_chooser_set_rgba(
      GTK_COLOR_CHOOSER(gui->paTextColorButton),
      &gui->settings.pa_text);

  gtk_color_chooser_set_rgba(
      GTK_COLOR_CHOOSER(gui->inspFgColorButton),
      &gui->settings.insp_fg);

  gtk_color_chooser_set_rgba(
      GTK_COLOR_CHOOSER(gui->inspBgColorButton),
      &gui->settings.insp_bg);

  gtk_color_chooser_set_rgba(
      GTK_COLOR_CHOOSER(gui->inspAxesColorButton),
      &gui->settings.pa_axes);

  gtk_color_chooser_set_rgba(
      GTK_COLOR_CHOOSER(gui->inspTextColorButton),
      &gui->settings.insp_text);

  gtk_color_chooser_set_rgba(
      GTK_COLOR_CHOOSER(gui->lcdFgColorButton),
      &gui->settings.lcd_fg);

  gtk_color_chooser_set_rgba(
      GTK_COLOR_CHOOSER(gui->lcdBgColorButton),
      &gui->settings.lcd_bg);
}

void
suscan_gui_settings_from_dialog(suscan_gui_t *gui)
{
  gtk_color_chooser_get_rgba(
      GTK_COLOR_CHOOSER(gui->paFgColorButton),
      &gui->settings.pa_fg);

  gtk_color_chooser_get_rgba(
      GTK_COLOR_CHOOSER(gui->paBgColorButton),
      &gui->settings.pa_bg);

  gtk_color_chooser_get_rgba(
      GTK_COLOR_CHOOSER(gui->paAxesColorButton),
      &gui->settings.pa_axes);

  gtk_color_chooser_get_rgba(
      GTK_COLOR_CHOOSER(gui->paTextColorButton),
      &gui->settings.pa_text);

  gtk_color_chooser_get_rgba(
      GTK_COLOR_CHOOSER(gui->inspFgColorButton),
      &gui->settings.insp_fg);

  gtk_color_chooser_get_rgba(
      GTK_COLOR_CHOOSER(gui->inspBgColorButton),
      &gui->settings.insp_bg);

  gtk_color_chooser_get_rgba(
      GTK_COLOR_CHOOSER(gui->inspAxesColorButton),
      &gui->settings.insp_axes);

  gtk_color_chooser_get_rgba(
      GTK_COLOR_CHOOSER(gui->inspTextColorButton),
      &gui->settings.insp_text);

  gtk_color_chooser_get_rgba(
      GTK_COLOR_CHOOSER(gui->lcdFgColorButton),
      &gui->settings.lcd_fg);

  gtk_color_chooser_get_rgba(
      GTK_COLOR_CHOOSER(gui->lcdBgColorButton),
      &gui->settings.lcd_bg);

  suscan_gui_apply_settings(gui);
}

/************************* Settings storage **********************************/

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

SUPRIVATE void
suscan_gui_load_g_settings(suscan_gui_t *gui)
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

SUBOOL
suscan_gui_load_settings(suscan_gui_t *gui)
{
  const char *value;
  suscan_gui_profile_t *profile = NULL;

  suscan_gui_load_g_settings(gui); /* Delete */

  SU_TRYCATCH(suscan_gui_assert_settings_obj(gui), return SU_FALSE);

  if ((value = suscan_object_get_field_value(
      gui->settings_obj,
      "active_profile")) != NULL) {

    if ((profile = suscan_gui_lookup_profile(gui, value)) != NULL)
      SU_TRYCATCH(suscan_gui_select_profile(gui, profile), return SU_FALSE);
  }

  return SU_TRUE;
}

SUPRIVATE void
suscan_gui_store_g_settings(suscan_gui_t *gui)
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


void
suscan_gui_store_settings(suscan_gui_t *gui)
{
  suscan_gui_store_g_settings(gui);

  if (gui->active_profile != NULL)
    suscan_object_set_field_value(
        gui->settings_obj,
        "active_profile",
        suscan_source_config_get_label(
            suscan_gui_profile_get_source_config(gui->active_profile)));
}

