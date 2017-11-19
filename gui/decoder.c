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

#define SU_LOG_DOMAIN "decoder-gui"

#include "gui.h"
#include <sigutils/agc.h>
#include <decoder.h>

void
suscan_gui_decoder_destroy(struct suscan_gui_decoder *decoder)
{
  unsigned int i;

  if (decoder->input_buffer != NULL)
    free(decoder->input_buffer);

  if (decoder->codec != NULL)
    su_codec_destroy(decoder->codec);

  for (i = 0; i < decoder->context_count; ++i)
    if (decoder->context_list[i] != NULL)
      free(decoder->context_list[i]);

  if (decoder->context_list != NULL)
    free(decoder->context_list);

  if (decoder->builder != NULL)
    g_object_unref(G_OBJECT(decoder->builder));

  free(decoder);
}

SUPRIVATE void
suscan_gui_decoder_run_encoder(GtkWidget *widget, gpointer *data)
{
  struct suscan_gui_decoder_context *ctx =
      (struct suscan_gui_decoder_context *) data;

  /* This may happen if the context creation failed */
  if (ctx == NULL)
    return;

  if (!suscan_gui_decodercfgui_assert_parent_gui(ctx->ui))
    return;  /* Weird */

  (void) suscan_gui_inspector_open_codec_tab(
      ctx->ui->inspector,
      ctx->ui,
      su_codec_get_output_bits(ctx->decoder->codec),
      SU_CODEC_DIRECTION_FORWARDS);
}

SUPRIVATE void
suscan_gui_decoder_run_decoder(GtkWidget *widget, gpointer *data)
{
  struct suscan_gui_decoder_context *ctx =
      (struct suscan_gui_decoder_context *) data;

  /* This may happen if the context creation failed */
  if (ctx == NULL)
    return;

  if (!suscan_gui_decodercfgui_assert_parent_gui(ctx->ui))
    return;  /* Weird */

  (void) suscan_gui_inspector_open_codec_tab(
      ctx->ui->inspector,
      ctx->ui,
      su_codec_get_output_bits(ctx->decoder->codec),
      SU_CODEC_DIRECTION_BACKWARDS);
}


SUPRIVATE void *
suscan_gui_decoder_create_context(
    void *private,
    struct suscan_gui_decodercfgui *ui)
{
  struct suscan_gui_decoder *decoder = (struct suscan_gui_decoder *) private;
  struct suscan_gui_decoder_context *ctx = NULL;

  SU_TRYCATCH(
      ctx = malloc(sizeof (struct suscan_gui_decoder_context)),
      goto fail);

  ctx->decoder = decoder;
  ctx->ui = ui;

  SU_TRYCATCH(PTR_LIST_APPEND_CHECK(decoder->context, ctx) != -1, goto fail);

  return ctx;

fail:
  if (ctx != NULL)
    free(ctx);
}

SUPRIVATE SUBOOL
suscan_gui_decoder_load_all_widgets(struct suscan_gui_decoder *decoder)
{
  SU_TRYCATCH(
      decoder->pageLabelEventBox =
          GTK_EVENT_BOX(gtk_builder_get_object(
              decoder->builder,
              "ebPageLabel")),
          return SU_FALSE);

  SU_TRYCATCH(
      decoder->pageLabel =
          GTK_LABEL(gtk_builder_get_object(
              decoder->builder,
              "lPageLabel")),
          return SU_FALSE);

  SU_TRYCATCH(
      decoder->decoderGrid =
          GTK_GRID(gtk_builder_get_object(
              decoder->builder,
              "grDecoder")),
          return SU_FALSE);

  SU_TRYCATCH(
      decoder->autoFitToggleButton =
          GTK_TOGGLE_TOOL_BUTTON(gtk_builder_get_object(
              decoder->builder,
              "tbFitWidth")),
          return SU_FALSE);

  SU_TRYCATCH(
      decoder->offsetSpinButton =
          GTK_SPIN_BUTTON(gtk_builder_get_object(
              decoder->builder,
              "sbOffset")),
          return SU_FALSE);

  SU_TRYCATCH(
      decoder->widthSpinButton =
          GTK_SPIN_BUTTON(gtk_builder_get_object(
              decoder->builder,
              "sbWidth")),
          return SU_FALSE);

  /* Add symbol view */
  decoder->symbolView = SUGTK_SYM_VIEW(sugtk_sym_view_new());

  SU_TRYCATCH(
      suscan_gui_inspector_populate_decoder_menu(
          decoder->inspector,
          decoder->symbolView,
          suscan_gui_decoder_create_context,
          decoder,
          G_CALLBACK(suscan_gui_decoder_run_encoder),
          G_CALLBACK(suscan_gui_decoder_run_decoder)),
      return SU_FALSE);

  gtk_grid_attach(
      decoder->decoderGrid,
      GTK_WIDGET(decoder->symbolView),
      0, /* left */
      1, /* top */
      1, /* width */
      1 /* height */);

  gtk_widget_set_hexpand(GTK_WIDGET(decoder->symbolView), TRUE);
  gtk_widget_set_vexpand(GTK_WIDGET(decoder->symbolView), TRUE);

  gtk_widget_show(GTK_WIDGET(decoder->symbolView));

  return SU_TRUE;
}

struct suscan_gui_decoder *
suscan_gui_decoder_new(
    struct suscan_gui_inspector *inspector,
    const struct suscan_decoder_desc *desc,
    uint8_t bits_per_symbol,
    suscan_config_t *config,
    enum su_codec_direction direction)
{
  struct suscan_gui_decoder *new = NULL;
  char *page_label = NULL;

  SU_TRYCATCH(new = calloc(1, sizeof (struct suscan_gui_decoder)), goto fail);

  SU_TRYCATCH(
      new->codec = suscan_decoder_make_codec(desc, bits_per_symbol, config),
      goto fail);

  su_codec_set_direction(new->codec, direction);

  new->index = -1;
  new->desc = desc;
  new->inspector = inspector;

  SU_TRYCATCH(
      new->builder = gtk_builder_new_from_file(
          PKGDATADIR "/gui/decoder-tab.glade"),
      goto fail);

  SU_TRYCATCH(suscan_gui_decoder_load_all_widgets(new), goto fail);

  gtk_builder_connect_signals(new->builder, new);

  SU_TRYCATCH(
      page_label = strbuild(
          "%s with %s",
          direction ==  SU_CODEC_DIRECTION_BACKWARDS ? "Decode" : "Encode",
          desc->desc),
      goto fail);

  gtk_label_set_text(new->pageLabel, page_label);

  free(page_label);
  page_label = NULL;

  return new;

fail:
  if (new != NULL)
    suscan_gui_decoder_destroy(new);

  if (page_label != NULL)
    free(page_label);

  return NULL;
}

void
suscan_on_close_decoder_tab(GtkWidget *widget, gpointer data)
{
  struct suscan_gui_decoder *decoder = (struct suscan_gui_decoder *) data;

  suscan_gui_inspector_remove_decoder(decoder->inspector, decoder);

  suscan_gui_decoder_destroy(decoder);
}

/******************** Decoder view toolbar buttons ****************************/
void
suscan_decoder_on_save(
    GtkWidget *widget,
    gpointer data)
{
  struct suscan_gui_decoder *decoder = (struct suscan_gui_decoder *) data;
  char *new_fname = NULL;

  SU_TRYCATCH(
      new_fname = strbuild(
          "%s-output-%s-%dbpp.log",
          decoder->codec->direction == SU_CODEC_DIRECTION_BACKWARDS ?
              "decoder" :
              "encoder",
          decoder->codec->class->name,
          su_codec_get_output_bits(decoder->codec)),
      goto done);

  SU_TRYCATCH(
      sugtk_sym_view_save_helper(
          decoder->symbolView,
          "Save symbol view",
          new_fname,
          su_codec_get_output_bits(decoder->codec)),
      goto done);

done:
  if (new_fname != NULL)
    free(new_fname);
}

SUPRIVATE void
suscan_gui_decoder_update_spin_buttons(struct suscan_gui_decoder *decoder)
{
  if (gtk_toggle_tool_button_get_active(
      GTK_TOGGLE_TOOL_BUTTON(decoder->autoFitToggleButton)))
    gtk_spin_button_set_value(
        decoder->widthSpinButton,
        sugtk_sym_view_get_width(decoder->symbolView));
}

void
suscan_decoder_on_zoom_in(
    GtkWidget *widget,
    gpointer data)
{
  struct suscan_gui_decoder *decoder = (struct suscan_gui_decoder *) data;
  guint curr_width = sugtk_sym_view_get_width(decoder->symbolView);
  guint curr_zoom = sugtk_sym_view_get_zoom(decoder->symbolView);

  curr_zoom <<= 1;

  if (curr_width < curr_zoom)
    curr_zoom = curr_width;

  sugtk_sym_view_set_zoom(decoder->symbolView, curr_zoom);

  suscan_gui_decoder_update_spin_buttons(decoder);
}


void
suscan_decoder_on_zoom_out(
    GtkWidget *widget,
    gpointer data)
{
  struct suscan_gui_decoder *decoder = (struct suscan_gui_decoder *) data;
  guint curr_width = sugtk_sym_view_get_width(decoder->symbolView);
  guint curr_zoom = sugtk_sym_view_get_zoom(decoder->symbolView);

  curr_zoom >>= 1;

  if (curr_zoom < 1)
    curr_zoom = 1;

  sugtk_sym_view_set_zoom(decoder->symbolView, curr_zoom);

  suscan_gui_decoder_update_spin_buttons(decoder);
}

void
suscan_decoder_on_toggle_autofit(
    GtkWidget *widget,
    gpointer data)
{
  struct suscan_gui_decoder *decoder = (struct suscan_gui_decoder *) data;
  gboolean active;

  active = gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(widget));

  sugtk_sym_view_set_autofit(decoder->symbolView, active);
  gtk_widget_set_sensitive(GTK_WIDGET(decoder->widthSpinButton), !active);
}

void
suscan_decoder_on_set_offset(
    GtkWidget *widget,
    gpointer data)
{
  struct suscan_gui_decoder *decoder = (struct suscan_gui_decoder *) data;

  sugtk_sym_view_set_offset(
      decoder->symbolView,
      gtk_spin_button_get_value(decoder->offsetSpinButton));
}

void
suscan_decoder_on_set_width(
    GtkWidget *widget,
    gpointer data)
{
  struct suscan_gui_decoder *decoder = (struct suscan_gui_decoder *) data;

  if (!gtk_toggle_tool_button_get_active(
      GTK_TOGGLE_TOOL_BUTTON(decoder->autoFitToggleButton)))
    sugtk_sym_view_set_width(
        decoder->symbolView,
        gtk_spin_button_get_value(decoder->widthSpinButton));
}
