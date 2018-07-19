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

#define SU_LOG_DOMAIN "gui"

#include <confdb.h>
#include "modemctl.h"
#include "gui.h"

#define suscan_gui_splash_INITIALIZER { \
  NULL, /* builder */ \
  NULL, /* splashWindow */ \
  NULL, /* progressLabel */ \
  NULL, /* thread */  \
  SU_FALSE  \
}

struct suscan_gui_splash {
  GtkBuilder *builder;

  GtkWindow *splashWindow;
  GtkLabel *progressLabel;
  GThread *thread;

  SUBOOL initialized;
};

struct suscan_gui_splash_message {
  struct suscan_gui_splash *splash; /* borrowed */
  char *message;
};

SUPRIVATE void
suscan_gui_splash_message_destroy(struct suscan_gui_splash_message *msg)
{
  if (msg->message != NULL)
    free(msg->message);

  free(msg);
}

SUPRIVATE struct suscan_gui_splash_message *
suscan_gui_splash_message_new(
    struct suscan_gui_splash *splash,
    const char *fmt,
    va_list ap)
{
  struct suscan_gui_splash_message *new = NULL;

  SU_TRYCATCH(
      new = calloc(1, sizeof(struct suscan_gui_splash_message)),
      goto fail);

  new->splash = splash;
  SU_TRYCATCH(new->message = vstrbuild(fmt, ap), goto fail);

  return new;

fail:
  if (new != NULL)
    suscan_gui_splash_message_destroy(new);

  return NULL;
}

SUPRIVATE gboolean
suscan_gui_splash_message_cb(gpointer user_data)
{
  struct suscan_gui_splash_message *msg =
      (struct suscan_gui_splash_message *) user_data;

  gtk_label_set_text(msg->splash->progressLabel, msg->message);

  suscan_gui_splash_message_destroy(msg);

  return G_SOURCE_REMOVE;
}

SUPRIVATE SUBOOL
suscan_gui_splash_progress(
    struct suscan_gui_splash *splash,
    const char *fmt,
    ...)
{
  struct suscan_gui_splash_message *new = NULL;
  va_list ap;
  SUBOOL ok = SU_FALSE;

  va_start(ap, fmt);

  SU_TRYCATCH(new = suscan_gui_splash_message_new(splash, fmt, ap), goto done);

  g_idle_add(suscan_gui_splash_message_cb, new);

  ok = SU_TRUE;

done:
  if (!ok)
    suscan_gui_splash_message_destroy(new);

  va_end(ap);

  return ok;
}

SUPRIVATE gpointer
suscan_gui_init_thread(gpointer data)
{
  struct suscan_gui_splash *splash =
      (struct suscan_gui_splash *) data;

  suscan_gui_splash_progress(splash, "Registering codecs...");
  SU_TRYCATCH(suscan_codec_class_register_builtin(), goto done);

  suscan_gui_splash_progress(splash, "Initializing signal sources...");
  SU_TRYCATCH(suscan_init_sources(), goto done);

  suscan_gui_splash_progress(splash, "Initializing estimators...");
  SU_TRYCATCH(suscan_init_estimators(), goto done);

  suscan_gui_splash_progress(splash, "Initializing spectrum sources...");
  SU_TRYCATCH(suscan_init_spectsrcs(), goto done);

  suscan_gui_splash_progress(splash, "Initializing inspectors...");
  SU_TRYCATCH(suscan_init_inspectors(), goto done);

  splash->initialized = SU_TRUE;

done:
  /* Break main loop */
  g_idle_add((GSourceFunc) gtk_main_quit, NULL);

  return NULL;
}

SUPRIVATE SUBOOL
suscan_graphical_init(int argc, char **argv)
{
  struct suscan_gui_splash splash = suscan_gui_splash_INITIALIZER;
  SUBOOL ok = SU_FALSE;

  gtk_init(&argc, &argv);

  /* Create builder */
  SU_TRYCATCH(
      splash.builder = gtk_builder_new_from_file(
          PKGDATADIR "/gui/splash.glade"),
      goto done);

  /* Load window and label */
  SU_TRYCATCH(
      splash.splashWindow= GTK_WINDOW(
          gtk_builder_get_object(splash.builder, "wSplash")),
      goto done);

  SU_TRYCATCH(
      splash.progressLabel= GTK_LABEL(
          gtk_builder_get_object(splash.builder, "lProgress")),
      goto done);

  /* Connect destruction */
  g_signal_connect(
      GTK_WIDGET(splash.splashWindow),
      "destroy",
      G_CALLBACK(gtk_main_quit),
      NULL);

  gtk_widget_show(GTK_WIDGET(splash.splashWindow));

  /* Done, spawn initialization thread */
  splash.thread = g_thread_new("init-task", suscan_gui_init_thread, &splash);

  /* Run gtk_main */
  gtk_main();

  /* Hide */
  gtk_widget_hide(GTK_WIDGET(splash.splashWindow));

  ok = splash.initialized;

done:
  /* Wait for thread to finish */
  if (splash.thread != NULL)
    g_thread_join(splash.thread);

  if (splash.builder != NULL)
    g_object_unref(splash.builder);

  return ok;
}

SUBOOL
suscan_gui_helper_preload(void)
{
  SU_TRYCATCH(suscan_gui_modemctl_agc_init(), return SU_FALSE);
  SU_TRYCATCH(suscan_gui_modemctl_afc_init(), return SU_FALSE);
  SU_TRYCATCH(suscan_gui_modemctl_fsk_init(), return SU_FALSE);
  SU_TRYCATCH(suscan_gui_modemctl_mf_init(), return SU_FALSE);
  SU_TRYCATCH(suscan_gui_modemctl_equalizer_init(), return SU_FALSE);
  SU_TRYCATCH(suscan_gui_modemctl_clock_init(), return SU_FALSE);

  return SU_TRUE;
}

SUBOOL
suscan_gui_start(
    int argc,
    char **argv,
    struct suscan_source_config **config_list,
    unsigned int config_count)
{
  suscan_gui_t *gui = NULL;

  SU_TRYCATCH(suscan_graphical_init(argc, argv), goto fail);

  SU_TRYCATCH(suscan_confdb_use("gtkui"), goto fail);

  SU_TRYCATCH(suscan_gui_helper_preload(), goto fail);

  SU_TRYCATCH(gui = suscan_gui_new(), goto fail);

  gtk_widget_show(GTK_WIDGET(gui->main));

  suscan_gui_set_title(gui, "No source selected");

  suscan_gui_setup_logging(gui);

  SU_INFO("SUScan GTK interface initialized\n");

  gtk_main();

  return SU_TRUE;

fail:
  if (gui != NULL)
    suscan_gui_destroy(gui);

  return SU_FALSE;
}

