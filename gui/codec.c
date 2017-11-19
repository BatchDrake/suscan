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
#include <time.h>

#define SU_LOG_DOMAIN "codec-gui"

#include "gui.h"
#include <sigutils/agc.h>
#include "codec.h"

void
suscan_gui_codec_destroy(struct suscan_gui_codec *codec)
{
  unsigned int i;

  if (codec->input_buffer != NULL)
    free(codec->input_buffer);

  if (codec->codec != NULL)
    suscan_codec_destroy(codec->codec);

  for (i = 0; i < codec->context_count; ++i)
    if (codec->context_list[i] != NULL)
      free(codec->context_list[i]);

  if (codec->context_list != NULL)
    free(codec->context_list);

  if (codec->builder != NULL)
    g_object_unref(G_OBJECT(codec->builder));

  free(codec);
}

SUPRIVATE void
suscan_gui_codec_run_encoder(GtkWidget *widget, gpointer *data)
{
  struct suscan_gui_codec_context *ctx =
      (struct suscan_gui_codec_context *) data;

  /* This may happen if the context creation failed */
  if (ctx == NULL)
    return;

  if (!suscan_gui_codec_cfg_ui_assert_parent_gui(ctx->ui))
    return;  /* Weird */

  (void) suscan_gui_inspector_open_codec_tab(
      ctx->ui->inspector,
      ctx->ui,
      suscan_codec_get_output_bits_per_symbol(ctx->codec->codec),
      SUSCAN_CODEC_DIRECTION_FORWARDS);
}

SUPRIVATE void
suscan_gui_codec_run_codec(GtkWidget *widget, gpointer *data)
{
  struct suscan_gui_codec_context *ctx =
      (struct suscan_gui_codec_context *) data;

  /* This may happen if the context creation failed */
  if (ctx == NULL)
    return;

  if (!suscan_gui_codec_cfg_ui_assert_parent_gui(ctx->ui))
    return;  /* Weird */

  (void) suscan_gui_inspector_open_codec_tab(
      ctx->ui->inspector,
      ctx->ui,
      suscan_codec_get_output_bits_per_symbol(ctx->codec->codec),
      SUSCAN_CODEC_DIRECTION_BACKWARDS);
}


SUPRIVATE void *
suscan_gui_codec_create_context(
    void *private,
    struct suscan_gui_codec_cfg_ui *ui)
{
  struct suscan_gui_codec *codec = (struct suscan_gui_codec *) private;
  struct suscan_gui_codec_context *ctx = NULL;

  SU_TRYCATCH(
      ctx = malloc(sizeof (struct suscan_gui_codec_context)),
      goto fail);

  ctx->codec = codec;
  ctx->ui = ui;

  SU_TRYCATCH(PTR_LIST_APPEND_CHECK(codec->context, ctx) != -1, goto fail);

  return ctx;

fail:
  if (ctx != NULL)
    free(ctx);

  return NULL;
}

SUPRIVATE SUBOOL
suscan_gui_codec_load_all_widgets(struct suscan_gui_codec *codec)
{
  SU_TRYCATCH(
      codec->pageLabelEventBox =
          GTK_EVENT_BOX(gtk_builder_get_object(
              codec->builder,
              "ebPageLabel")),
          return SU_FALSE);

  SU_TRYCATCH(
      codec->pageLabel =
          GTK_LABEL(gtk_builder_get_object(
              codec->builder,
              "lPageLabel")),
          return SU_FALSE);

  SU_TRYCATCH(
      codec->codecGrid =
          GTK_GRID(gtk_builder_get_object(
              codec->builder,
              "grCodec")),
          return SU_FALSE);

  SU_TRYCATCH(
      codec->autoFitToggleButton =
          GTK_TOGGLE_TOOL_BUTTON(gtk_builder_get_object(
              codec->builder,
              "tbFitWidth")),
          return SU_FALSE);

  SU_TRYCATCH(
      codec->offsetSpinButton =
          GTK_SPIN_BUTTON(gtk_builder_get_object(
              codec->builder,
              "sbOffset")),
          return SU_FALSE);

  SU_TRYCATCH(
      codec->widthSpinButton =
          GTK_SPIN_BUTTON(gtk_builder_get_object(
              codec->builder,
              "sbWidth")),
          return SU_FALSE);

  /* Add symbol view */
  codec->symbolView = SUGTK_SYM_VIEW(sugtk_sym_view_new());

  SU_TRYCATCH(
      suscan_gui_inspector_populate_codec_menu(
          codec->inspector,
          codec->symbolView,
          suscan_gui_codec_create_context,
          codec,
          G_CALLBACK(suscan_gui_codec_run_encoder),
          G_CALLBACK(suscan_gui_codec_run_codec)),
      return SU_FALSE);

  gtk_grid_attach(
      codec->codecGrid,
      GTK_WIDGET(codec->symbolView),
      0, /* left */
      1, /* top */
      1, /* width */
      1 /* height */);

  gtk_widget_set_hexpand(GTK_WIDGET(codec->symbolView), TRUE);
  gtk_widget_set_vexpand(GTK_WIDGET(codec->symbolView), TRUE);

  gtk_widget_show(GTK_WIDGET(codec->symbolView));

  return SU_TRUE;
}

struct suscan_gui_codec *
suscan_gui_codec_new(
    struct suscan_gui_inspector *inspector,
    const struct suscan_codec_class *class,
    uint8_t bits_per_symbol,
    suscan_config_t *config,
    unsigned int direction)
{
  struct suscan_gui_codec *new = NULL;
  char *page_label = NULL;

  SU_TRYCATCH(new = calloc(1, sizeof (struct suscan_gui_codec)), goto fail);

  SU_TRYCATCH(
      new->codec = suscan_codec_class_make_codec(
          class,
          bits_per_symbol,
          config,
          direction),
      goto fail);

  new->direction = direction;
  new->index = -1;
  new->class = class;
  new->inspector = inspector;

  SU_TRYCATCH(
      new->builder = gtk_builder_new_from_file(
          PKGDATADIR "/gui/codec-tab.glade"),
      goto fail);

  SU_TRYCATCH(suscan_gui_codec_load_all_widgets(new), goto fail);

  gtk_builder_connect_signals(new->builder, new);

  SU_TRYCATCH(
      page_label = strbuild(
          "%s with %s",
          direction ==  SU_CODEC_DIRECTION_BACKWARDS ? "Decode" : "Encode",
          class->desc),
      goto fail);

  gtk_label_set_text(new->pageLabel, page_label);

  free(page_label);
  page_label = NULL;

  return new;

fail:
  if (new != NULL)
    suscan_gui_codec_destroy(new);

  if (page_label != NULL)
    free(page_label);

  return NULL;
}

void
suscan_on_close_codec_tab(GtkWidget *widget, gpointer data)
{
  struct suscan_gui_codec *codec = (struct suscan_gui_codec *) data;

  suscan_gui_inspector_remove_codec(codec->inspector, codec);

  suscan_gui_codec_destroy(codec);
}

/******************** Decoder view toolbar buttons ****************************/
void
suscan_codec_on_save(
    GtkWidget *widget,
    gpointer data)
{
  struct suscan_gui_codec *codec = (struct suscan_gui_codec *) data;
  char *new_fname = NULL;

  SU_TRYCATCH(
      new_fname = strbuild(
          "%s-output-%s-%dbpp.log",
          codec->direction == SUSCAN_CODEC_DIRECTION_BACKWARDS ?
              "codec" :
              "encoder",
          codec->codec->class->desc,
          suscan_codec_get_output_bits_per_symbol(codec->codec)),
      goto done);

  SU_TRYCATCH(
      sugtk_sym_view_save_helper(
          codec->symbolView,
          "Save symbol view",
          new_fname,
          suscan_codec_get_output_bits_per_symbol(codec->codec)),
      goto done);

done:
  if (new_fname != NULL)
    free(new_fname);
}

SUPRIVATE void
suscan_gui_codec_update_spin_buttons(struct suscan_gui_codec *codec)
{
  if (gtk_toggle_tool_button_get_active(
      GTK_TOGGLE_TOOL_BUTTON(codec->autoFitToggleButton)))
    gtk_spin_button_set_value(
        codec->widthSpinButton,
        sugtk_sym_view_get_width(codec->symbolView));
}

void
suscan_codec_on_zoom_in(
    GtkWidget *widget,
    gpointer data)
{
  struct suscan_gui_codec *codec = (struct suscan_gui_codec *) data;
  guint curr_width = sugtk_sym_view_get_width(codec->symbolView);
  guint curr_zoom = sugtk_sym_view_get_zoom(codec->symbolView);

  curr_zoom <<= 1;

  if (curr_width < curr_zoom)
    curr_zoom = curr_width;

  sugtk_sym_view_set_zoom(codec->symbolView, curr_zoom);

  suscan_gui_codec_update_spin_buttons(codec);
}


void
suscan_codec_on_zoom_out(
    GtkWidget *widget,
    gpointer data)
{
  struct suscan_gui_codec *codec = (struct suscan_gui_codec *) data;
  guint curr_width = sugtk_sym_view_get_width(codec->symbolView);
  guint curr_zoom = sugtk_sym_view_get_zoom(codec->symbolView);

  curr_zoom >>= 1;

  if (curr_zoom < 1)
    curr_zoom = 1;

  sugtk_sym_view_set_zoom(codec->symbolView, curr_zoom);

  suscan_gui_codec_update_spin_buttons(codec);
}

void
suscan_codec_on_toggle_autofit(
    GtkWidget *widget,
    gpointer data)
{
  struct suscan_gui_codec *codec = (struct suscan_gui_codec *) data;
  gboolean active;

  active = gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(widget));

  sugtk_sym_view_set_autofit(codec->symbolView, active);
  gtk_widget_set_sensitive(GTK_WIDGET(codec->widthSpinButton), !active);
}

void
suscan_codec_on_set_offset(
    GtkWidget *widget,
    gpointer data)
{
  struct suscan_gui_codec *codec = (struct suscan_gui_codec *) data;

  sugtk_sym_view_set_offset(
      codec->symbolView,
      gtk_spin_button_get_value(codec->offsetSpinButton));
}

void
suscan_codec_on_set_width(
    GtkWidget *widget,
    gpointer data)
{
  struct suscan_gui_codec *codec = (struct suscan_gui_codec *) data;

  if (!gtk_toggle_tool_button_get_active(
      GTK_TOGGLE_TOOL_BUTTON(codec->autoFitToggleButton)))
    sugtk_sym_view_set_width(
        codec->symbolView,
        gtk_spin_button_get_value(codec->widthSpinButton));
}
