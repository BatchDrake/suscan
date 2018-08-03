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

#include "defaults.h"

#include <locale.h>

/* Transfer settings to objects */
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

/* Settings transfer to and from configuration dialogs */
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
SUPRIVATE SUBOOL
suscan_gui_assert_settings_obj(suscan_gui_t *gui)
{
  const suscan_object_t *list;
  suscan_object_t *ui_settings = NULL;

  SU_TRYCATCH(
      list = suscan_config_context_get_list(gui->gtkui_ctx),
      goto fail);

  if ((gui->gtkui_obj = suscan_object_set_get(list, 0)) == NULL) {
    SU_TRYCATCH(
        ui_settings = suscan_object_new(SUSCAN_OBJECT_TYPE_OBJECT),
        goto fail);
    SU_TRYCATCH(
        suscan_config_context_put(gui->gtkui_ctx, ui_settings),
        goto fail);
    gui->gtkui_obj = ui_settings;
    ui_settings = NULL;
  }

  return SU_TRUE;

fail:
  if (ui_settings != NULL)
    suscan_object_destroy(ui_settings);

  return SU_FALSE;
}

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

/* Settings getters */
SUPRIVATE SUBOOL
suscan_gui_settings_get_window(
    enum sigutils_channel_detector_window *dest,
    const suscan_gui_t *gui,
    const char *field,
    const char *dflt)
{
  const char *str = NULL;

  if ((str = suscan_object_get_field_value(gui->gtkui_obj, field)) == NULL)
    str = dflt;

  *dest = suscan_gui_str_to_window(str);

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_gui_settings_get_color(
    GdkRGBA *dest,
    const suscan_gui_t *gui,
    const char *field,
    const char *dflt)
{
  const char *str = NULL;

  if ((str = suscan_object_get_field_value(gui->gtkui_obj, field)) == NULL)
    str = dflt;

  SU_TRYCATCH(gdk_rgba_parse(dest, str), return SU_FALSE);

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_gui_settings_get_float(
    SUFLOAT *dest,
    const suscan_gui_t *gui,
    const char *field,
    SUFLOAT dflt)
{
  *dest = suscan_object_get_field_float(gui->gtkui_obj, field, dflt);

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_gui_settings_get_uint(
    SUSCOUNT *dest,
    const suscan_gui_t *gui,
    const char *field,
    unsigned int dflt)
{
  *dest = suscan_object_get_field_uint(gui->gtkui_obj, field, dflt);

  return SU_TRUE;
}

SUPRIVATE SUBOOL
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

  return suscan_object_set_field_value(gui->gtkui_obj, field, color);
}

SUPRIVATE SUBOOL
suscan_gui_settings_set_float(
    const SUFLOAT *val,
    const suscan_gui_t *gui,
    const char *field)
{
  return suscan_object_set_field_float(gui->gtkui_obj, field, *val);
}

SUPRIVATE SUBOOL
suscan_gui_settings_set_uint(
    const SUSCOUNT *val,
    const suscan_gui_t *gui,
    const char *field)
{
  return suscan_object_set_field_uint(gui->gtkui_obj, field, *val);
}

SUPRIVATE SUBOOL
suscan_gui_settings_set_window(
    enum sigutils_channel_detector_window *window,
    const suscan_gui_t *gui,
    const char *field)
{
  return suscan_object_set_field_value(
      gui->gtkui_obj,
      field,
      suscan_gui_window_to_str(*window));

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_gui_load_gtkui_settings(suscan_gui_t *gui)
{
  struct suscan_analyzer_params analyzer_params =
      suscan_analyzer_params_INITIALIZER;
  SUBOOL ok = SU_FALSE;

  /* Load general GUI parameters */
  SU_TRYCATCH(
      suscan_gui_settings_get_color(
          &gui->settings.pa_bg,
          gui,
          "pa-bg-color",
          SUSCAN_DEFAULT_PA_BG_COLOR),
      goto done);

  SU_TRYCATCH(
      suscan_gui_settings_get_color(
          &gui->settings.pa_fg,
          gui,
          "pa-fg-color",
          SUSCAN_DEFAULT_PA_FG_COLOR),
      goto done);

  SU_TRYCATCH(
      suscan_gui_settings_get_color(
          &gui->settings.pa_axes,
          gui,
          "pa-axes-color",
          SUSCAN_DEFAULT_PA_AXES_COLOR),
      goto done);

  SU_TRYCATCH(
      suscan_gui_settings_get_color(
          &gui->settings.pa_text,
          gui,
          "pa-text-color",
          SUSCAN_DEFAULT_PA_TEXT_COLOR),
      goto done);

  /* Inspector look and feel */
  SU_TRYCATCH(
      suscan_gui_settings_get_color(
          &gui->settings.insp_bg,
          gui,
          "insp-bg-color",
          SUSCAN_DEFAULT_INSP_BG_COLOR),
      goto done);

  SU_TRYCATCH(
      suscan_gui_settings_get_color(
          &gui->settings.insp_fg,
          gui,
          "insp-fg-color",
          SUSCAN_DEFAULT_INSP_FG_COLOR),
      goto done);

  SU_TRYCATCH(
      suscan_gui_settings_get_color(
          &gui->settings.insp_axes,
          gui,
          "insp-axes-color",
          SUSCAN_DEFAULT_INSP_AXES_COLOR),
      goto done);

  SU_TRYCATCH(
      suscan_gui_settings_get_color(
          &gui->settings.insp_text,
          gui,
          "insp-text-color",
          SUSCAN_DEFAULT_INSP_TEXT_COLOR),
      goto done);

  /* LCD settings */
  SU_TRYCATCH(
      suscan_gui_settings_get_color(
          &gui->settings.lcd_bg,
          gui,
          "lcd-bg-color",
          SUSCAN_DEFAULT_LCD_BG_COLOR),
      goto done);

  SU_TRYCATCH(
      suscan_gui_settings_get_color(
          &gui->settings.lcd_fg,
          gui,
          "lcd-fg-color",
          SUSCAN_DEFAULT_LCD_FG_COLOR),
      goto done);

  /* Load analyzer parameters */
  SU_TRYCATCH(
      suscan_gui_settings_get_float(
          &analyzer_params.detector_params.alpha,
          gui,
          "spectrum-avg-factor",
          SUSCAN_DEFAULT_SPECTRUM_AVG_FACTOR),
      goto done);

  SU_TRYCATCH(
      suscan_gui_settings_get_float(
          &analyzer_params.detector_params.beta,
          gui,
          "signal-avg-factor",
          SUSCAN_DEFAULT_SIGNAL_AVG_FACTOR),
      goto done);

  SU_TRYCATCH(
      suscan_gui_settings_get_float(
          &analyzer_params.detector_params.gamma,
          gui,
          "noise-avg-factor",
          SUSCAN_DEFAULT_NOISE_AVG_FACTOR),
      goto done);

  SU_TRYCATCH(
      suscan_gui_settings_get_float(
          &analyzer_params.detector_params.snr,
          gui,
          "snr-threshold",
          SUSCAN_DEFAULT_SNR_THRESHOLD),
      goto done);

  SU_TRYCATCH(
      suscan_gui_settings_get_window(
          &analyzer_params.detector_params.window,
          gui,
          "window-func",
          SUSCAN_DEFAULT_WINDOW_FUNC),
      goto done);

  SU_TRYCATCH(
      suscan_gui_settings_get_uint(
          &analyzer_params.detector_params.window_size,
          gui,
          "window-size",
          SUSCAN_DEFAULT_BUFFER_SIZE),
      goto done);

  SU_TRYCATCH(
      suscan_gui_settings_get_float(
          &analyzer_params.channel_update_int,
          gui,
          "channel-interval",
          SUSCAN_DEFAULT_CHANNEL_INTERVAL),
      goto done);

  SU_TRYCATCH(
      suscan_gui_settings_get_float(
          &analyzer_params.psd_update_int,
          gui,
          "psd-interval",
          SUSCAN_DEFAULT_PSD_INTERVAL),
      goto done);

  /* TODO: send update message to analyzer */
  gui->analyzer_params = analyzer_params;

  ok = SU_TRUE;

done:
  return ok;
}

SUBOOL
suscan_gui_load_settings(suscan_gui_t *gui)
{
  const char *value;
  suscan_gui_profile_t *profile = NULL;

  /* This is SO broken. Seriously, wtf */
  setlocale(LC_NUMERIC, "C");

  SU_TRYCATCH(suscan_gui_assert_settings_obj(gui), return SU_FALSE);

  SU_TRYCATCH(suscan_gui_load_gtkui_settings(gui), return SU_FALSE);

  if ((value = suscan_object_get_field_value(
      gui->gtkui_obj,
      "active_profile")) != NULL) {

    if ((profile = suscan_gui_lookup_profile(gui, value)) != NULL)
      SU_TRYCATCH(suscan_gui_select_profile(gui, profile), return SU_FALSE);
  }

  /* All set, move settings to dialog */
  suscan_gui_analyzer_params_to_dialog(gui);
  suscan_gui_settings_to_dialog(gui);

  /* Apply these settings */
  suscan_gui_apply_settings(gui);

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_gui_store_gtkui_settings(suscan_gui_t *gui)
{
  /* Store general GUI parameters */
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      suscan_gui_settings_set_color(
          &gui->settings.pa_bg,
          gui,
          "pa-bg-color"),
      goto done);

  SU_TRYCATCH(
      suscan_gui_settings_set_color(
          &gui->settings.pa_fg,
          gui,
          "pa-fg-color"),
      goto done);

  SU_TRYCATCH(
      suscan_gui_settings_set_color(
          &gui->settings.pa_axes,
          gui,
          "pa-axes-color"),
      goto done);

  SU_TRYCATCH(
      suscan_gui_settings_set_color(
          &gui->settings.pa_text,
          gui,
          "pa-text-color"),
      goto done);

  /* Inspector look and feel */
  SU_TRYCATCH(
      suscan_gui_settings_set_color(
          &gui->settings.insp_bg,
          gui,
          "insp-bg-color"),
      goto done);

  SU_TRYCATCH(
      suscan_gui_settings_set_color(
          &gui->settings.insp_fg,
          gui,
          "insp-fg-color"),
      goto done);

  SU_TRYCATCH(
      suscan_gui_settings_set_color(
          &gui->settings.insp_axes,
          gui,
          "insp-axes-color"),
      goto done);

  SU_TRYCATCH(
      suscan_gui_settings_set_color(
          &gui->settings.insp_text,
          gui,
          "insp-text-color"),
      goto done);

  /* LCD settings */
  SU_TRYCATCH(
      suscan_gui_settings_set_color(
          &gui->settings.lcd_bg,
          gui,
          "lcd-bg-color"),
      goto done);

  SU_TRYCATCH(
      suscan_gui_settings_set_color(
          &gui->settings.lcd_fg,
          gui,
          "lcd-fg-color"),
      goto done);

  /* Load analyzer parameters */
  SU_TRYCATCH(
      suscan_gui_settings_set_float(
          &gui->analyzer_params.detector_params.alpha,
          gui,
          "spectrum-avg-factor"),
      goto done);

  SU_TRYCATCH(
      suscan_gui_settings_set_float(
          &gui->analyzer_params.detector_params.beta,
          gui,
          "signal-avg-factor"),
      goto done);

  SU_TRYCATCH(
      suscan_gui_settings_set_float(
          &gui->analyzer_params.detector_params.gamma,
          gui,
          "noise-avg-factor"),
      goto done);

  SU_TRYCATCH(
      suscan_gui_settings_set_float(
          &gui->analyzer_params.detector_params.snr,
          gui,
          "snr-threshold"),
      goto done);

  SU_TRYCATCH(
      suscan_gui_settings_set_window(
          &gui->analyzer_params.detector_params.window,
          gui,
          "window-func"),
      goto done);

  SU_TRYCATCH(
      suscan_gui_settings_set_uint(
          &gui->analyzer_params.detector_params.window_size,
          gui,
          "window-size"),
      goto done);

  SU_TRYCATCH(
      suscan_gui_settings_set_float(
          &gui->analyzer_params.channel_update_int,
          gui,
          "channel-interval"),
      goto done);

  SU_TRYCATCH(
      suscan_gui_settings_set_float(
          &gui->analyzer_params.psd_update_int,
          gui,
          "psd-interval"),
      goto done);

  ok = SU_TRUE;

done:
  return ok;
}


void
suscan_gui_store_settings(suscan_gui_t *gui)
{
  (void) suscan_gui_store_gtkui_settings(gui);

  if (gui->active_profile != NULL)
    suscan_object_set_field_value(
        gui->gtkui_obj,
        "active_profile",
        suscan_source_config_get_label(
            suscan_gui_profile_get_source_config(gui->active_profile)));
}

