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

#include <assert.h>
#include <string.h>

#define SU_LOG_DOMAIN "gui"

#include <confdb.h>
#include "modemctl.h"
#include "gui.h"

void
suscan_gui_destroy(suscan_gui_t *gui)
{
  unsigned int i;

  suscan_gui_clear_profile_menu(gui);

  for (i = 0; i < gui->palette_count; ++i)
    if (gui->palette_list[i] != NULL)
      suscan_gui_palette_destroy(gui->palette_list[i]);

  if (gui->palette_list != NULL)
    free(gui->palette_list);

  for (i = 0; i < gui->action_count; ++i)
    if (gui->action_list[i] != NULL)
      free(gui->action_list[i]);

  if (gui->action_list != NULL)
    free(gui->action_list);

  for (i = 0; i < gui->inspector_count; ++i)
    if (gui->inspector_list[i] != NULL)
      suscan_gui_inspector_destroy(gui->inspector_list[i]);

  if (gui->inspector_list != NULL)
    free(gui->inspector_list);

  for (i = 0; i < gui->profile_count; ++i)
    if (gui->profile_list[i] != NULL)
      suscan_gui_profile_destroy(gui->profile_list[i]);

  if (gui->profile_list != NULL)
    free(gui->profile_list);

  if (gui->builder != NULL)
    g_object_unref(gui->builder);

  if (gui->analyzer != NULL)
    suscan_analyzer_destroy(gui->analyzer);

  suscan_mq_finalize(&gui->mq_out);

  free(gui);
}

SUPRIVATE void
suscan_quit_cb(GtkWidget *obj, gpointer data)
{
  suscan_gui_t *gui = (suscan_gui_t *) data;

  suscan_gui_quit(gui);
}

void
suscan_gui_set_freq(suscan_gui_t *gui, uint64_t freq)
{
  sugtk_lcd_set_value(gui->freqLcd, freq);
}

SUBOOL
suscan_gui_set_title(suscan_gui_t *gui, const char *title)
{
  char *full_title = NULL;

  SU_TRYCATCH(full_title = strbuild("%s - Suscan", title), return SU_FALSE);

  gtk_label_set_text(gui->titleLabel, title);

  gtk_window_set_title(gui->main, full_title);

  free(full_title);

  return SU_TRUE;
}

SUBOOL
suscan_gui_connect(suscan_gui_t *gui)
{
  unsigned int i;
  suscan_source_config_t *config = NULL;
  assert(gui->state == SUSCAN_GUI_STATE_STOPPED
      || gui->state == SUSCAN_GUI_STATE_RESTARTING);
  assert(gui->analyzer == NULL);
  assert(gui->active_profile != NULL);

  for (i = 0; i < gui->inspector_count; ++i)
    if (gui->inspector_list[i] != NULL)
      break;

  if (i < gui->inspector_count)
    suscan_warning(
        gui,
        "Existing inspectors",
        "The opened inspector tabs will remain in idle state");

  sugtk_spectrum_reset(gui->spectrum);

  config = suscan_gui_profile_get_source_config(gui->active_profile);

  sugtk_lcd_set_value(gui->freqLcd, suscan_source_config_get_freq(config));

  if ((gui->analyzer = suscan_analyzer_new(
      &gui->analyzer_params,
      config,
      &gui->mq_out)) == NULL)
    goto fail;


  /* Change state. This counts as running because analyzer exists */
  suscan_gui_update_state(gui, SUSCAN_GUI_STATE_RUNNING);

  /* Analyzer created, create async thread */
  SU_TRYCATCH(suscan_gui_start_async_thread(gui), goto fail);

  return SU_TRUE;

fail:
  if (gui->analyzer != NULL) {
    suscan_analyzer_destroy(gui->analyzer);
    gui->analyzer = NULL;

    suscan_analyzer_consume_mq(&gui->mq_out);
  }

  /* Something went wrong. Put GUI in stopped state. */
  suscan_gui_update_state(gui, SUSCAN_GUI_STATE_STOPPED);

  return SU_FALSE;
}

void
suscan_gui_reconnect(suscan_gui_t *gui)
{
  assert(gui->state == SUSCAN_GUI_STATE_RUNNING);
  assert(gui->analyzer != NULL);

  suscan_gui_update_state(gui, SUSCAN_GUI_STATE_RESTARTING);
  suscan_analyzer_req_halt(gui->analyzer);
}

void
suscan_gui_disconnect(suscan_gui_t *gui)
{
  assert(gui->state == SUSCAN_GUI_STATE_RUNNING);
  assert(gui->analyzer != NULL);

  suscan_gui_update_state(gui, SUSCAN_GUI_STATE_STOPPING);
  suscan_analyzer_req_halt(gui->analyzer);
}

void
suscan_gui_quit(suscan_gui_t *gui)
{
  switch (gui->state) {
    case SUSCAN_GUI_STATE_RUNNING:
      suscan_gui_update_state(gui, SUSCAN_GUI_STATE_QUITTING);
      suscan_analyzer_req_halt(gui->analyzer);
      break;

    case SUSCAN_GUI_STATE_RESTARTING:
      suscan_gui_update_state(gui, SUSCAN_GUI_STATE_QUITTING);
      break;

    case SUSCAN_GUI_STATE_STOPPED:
      /* GUI already stopped, proceed to stop safely */
      suscan_gui_store_settings(gui);
      suscan_gui_destroy(gui);
      gtk_main_quit();
      break;
  }

  /* Ignore other states */
}

SUPRIVATE gboolean
suscan_gui_on_set_frequency(SuGtkLcd *lcd, gulong value, gpointer data)
{
  suscan_gui_t *gui = (suscan_gui_t *) data;

  if (gui->state == SUSCAN_GUI_STATE_RUNNING)
    return suscan_analyzer_set_freq(gui->analyzer, value);

  return SU_FALSE;
}

suscan_gui_t *
suscan_gui_new(void)
{
  suscan_gui_t *gui = NULL;
  GtkCssProvider *provider;
  GError *err = NULL;

  provider = gtk_css_provider_new();

  SU_TRYCATCH(
      gtk_css_provider_load_from_path(
          provider,
          PKGDATADIR "/gui/ui.css",
          &err),
      g_prefix_error(&err, "Cannot parse CSS"); goto fail);

  gtk_style_context_add_provider_for_screen(
      gdk_screen_get_default(),
      GTK_STYLE_PROVIDER(provider),
      GTK_STYLE_PROVIDER_PRIORITY_USER);

  SU_TRYCATCH(gui = calloc(1, sizeof(suscan_gui_t)), goto fail);

  SU_TRYCATCH(
      gui->gtkui_ctx = suscan_config_context_assert("gtkui"),
      goto fail);

  SU_TRYCATCH(
      gui->demod_ctx = suscan_config_context_assert("demod"),
      goto fail);

  SU_TRYCATCH(
      gui->builder = gtk_builder_new_from_file(PKGDATADIR "/gui/main.glade"),
      goto fail);

  gtk_builder_connect_signals(gui->builder, gui);

  SU_TRYCATCH(suscan_gui_load_all_widgets(gui), goto fail);

  sugtk_lcd_set_value_cb(gui->freqLcd, suscan_gui_on_set_frequency, gui);

  /* Load source profiles */
  SU_TRYCATCH(suscan_gui_load_profiles(gui), goto fail);

  /* Load palettes */
  SU_TRYCATCH(suscan_gui_load_palettes(gui), goto fail);
  SU_TRYCATCH(
      suscan_gui_populate_pal_box(gui, gui->waterfallPalBox),
      goto fail);

  /* All done. Load settings and apply them */
  SU_TRYCATCH(suscan_gui_load_settings(gui), goto fail);

  suscan_gui_update_state(gui, SUSCAN_GUI_STATE_STOPPED);

  g_signal_connect(
      GTK_WIDGET(gui->main),
      "destroy",
      G_CALLBACK(suscan_quit_cb),
      gui);

  return gui;

fail:
  if (gui != NULL)
    suscan_gui_destroy(gui);

  return NULL;
}


