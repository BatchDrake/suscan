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

#define SU_LOG_DOMAIN "inspector-gui"

#include <sigutils/agc.h>
#include <codec/codec.h>

#include "gui.h"
#include "inspector.h"

void suscan_gui_inspector_on_reshape(GtkWidget *widget, gpointer data);

void
suscan_gui_inspector_destroy(suscan_gui_inspector_t *inspector)
{
  unsigned int i;

  if (inspector->inshnd != -1 && inspector->_parent.gui != NULL)
    suscan_analyzer_close_async(
        inspector->_parent.gui->analyzer,
        inspector->inshnd,
        rand());

  suscan_gui_modemctl_set_finalize(&inspector->modemctl_set);

  for (i = 0; i < inspector->estimator_count; ++i)
    suscan_gui_estimatorui_destroy(inspector->estimator_list[i]);

  if (inspector->estimator_list != NULL)
    free(inspector->estimator_list);

  if (inspector->config != NULL)
    suscan_config_destroy(inspector->config);

  if (inspector->builder != NULL)
    g_object_unref(G_OBJECT(inspector->builder));

  if (!suscan_gui_symsrc_finalize(&inspector->_parent)) {
    SU_ERROR("Inspector destruction failed somehow\n");
    return;
  }

  free(inspector);
}

/* Just marks it as detached: it doesn not refer to any existing inspector */
void
suscan_gui_inspector_detach(suscan_gui_inspector_t *insp)
{
  insp->dead = SU_TRUE;
  insp->inshnd = -1;
  gtk_widget_set_sensitive(GTK_WIDGET(insp->channelInspectorGrid), FALSE);
}

/* Sends a close signal to the analyzer */
void
suscan_gui_inspector_close(suscan_gui_inspector_t *insp)
{
  SUHANDLE handle = insp->inshnd;

  if (handle != -1) {
    /* Send close message */
    insp->inshnd = -1;
    suscan_analyzer_close_async(insp->_parent.gui->analyzer, handle, rand());
  }

  gtk_widget_set_sensitive(GTK_WIDGET(insp->channelInspectorGrid), FALSE);
}

SUPRIVATE void
suscan_gui_inspector_set_bits(suscan_gui_inspector_t *insp, unsigned int bpp)
{
  insp->decider_params.bits = bpp;

  if (bpp != 0)
    su_decider_init(&insp->decider, &insp->decider_params);

  sugtk_histogram_set_decider_params(insp->histogram, &insp->decider_params);

  sugtk_trans_mtx_set_order(insp->transMatrix, 1 << bpp);
}

SUSYMBOL
suscan_gui_inspector_decide(
    const suscan_gui_inspector_t *inspector,
    SUCOMPLEX sample)
{
  if (suscan_gui_inspector_get_bits(inspector) > 0)
    return SU_TOSYM(su_decider_decide(&inspector->decider, SU_C_ARG(sample)));
  else
    return SU_NOSYMBOL;
}

SUPRIVATE void
suscan_gui_inspector_update_spin_buttons(suscan_gui_inspector_t *insp)
{
  unsigned int total_rows;
  unsigned int page_rows;

  gtk_spin_button_set_value(
      insp->offsetSpinButton,
      sugtk_sym_view_get_offset(insp->symbolView));

  if (gtk_toggle_tool_button_get_active(
      GTK_TOGGLE_TOOL_BUTTON(insp->autoFitToggleButton)))
    gtk_spin_button_set_value(
        insp->widthSpinButton,
        sugtk_sym_view_get_width(insp->symbolView));

  /* This is not totally correct */
  total_rows =
      sugtk_sym_view_get_buffer_size(insp->symbolView)
      / (SUGTK_SYM_VIEW_STRIDE_ALIGN
          * sugtk_sym_view_get_width(insp->symbolView)) + 1;

  page_rows = sugtk_sym_view_get_height(insp->symbolView);

  if (total_rows < page_rows) {
    gtk_widget_set_sensitive(GTK_WIDGET(insp->symViewScrollbar), FALSE);
    gtk_adjustment_set_page_size(insp->symViewScrollAdjustment, page_rows);
    gtk_adjustment_set_upper(
        insp->symViewScrollAdjustment,
        page_rows);
    gtk_adjustment_set_value(insp->symViewScrollAdjustment, 0);
  } else {
    gtk_adjustment_set_page_size(insp->symViewScrollAdjustment, page_rows);
    gtk_adjustment_set_upper(
        insp->symViewScrollAdjustment,
        total_rows);
    gtk_adjustment_set_value(
        insp->symViewScrollAdjustment,
        sugtk_sym_view_get_offset(insp->symbolView)
        / sugtk_sym_view_get_width(insp->symbolView));
    gtk_widget_set_sensitive(GTK_WIDGET(insp->symViewScrollbar), TRUE);
  }
}

SUBOOL
suscan_gui_inspector_feed_w_batch(
    suscan_gui_inspector_t *insp,
    const struct suscan_analyzer_sample_batch_msg *msg)
{
  unsigned int sample_count, full_samp_count;
  unsigned int i, n = 0;
  GtkTextIter iter;
  SUBITS *decbuf;
  SUSYMBOL sym;
  SUBITS bits;
  SUBOOL ok = SU_FALSE;

  /*
   * Push, at most, the last SUSCAN_GUI_CONSTELLATION_HISTORY. We do this
   * because the previous ones will never be shown
   */
  full_samp_count = msg->sample_count;
  sample_count = MIN(full_samp_count, SUGTK_CONSTELLATION_HISTORY);

  /* Cache decision */
  if (insp->recording)
    SU_TRYCATCH(
        decbuf = suscan_gui_symsrc_assert(&insp->_parent, full_samp_count),
        goto done);

  /* Check if recording is enabled to assert the symbol buffer */

  sugtk_trans_mtx_reset(insp->transMatrix);

  for (i = 0; i < full_samp_count; ++i)
    if ((sym = suscan_gui_inspector_decide(insp, msg->samples[i]))
        != SU_NOSYMBOL) {
      bits = SU_FROMSYM(sym);

      if (insp->recording) {
        /* Save decision */
        decbuf[n++] = bits;

        /* Update symbol view */
        sugtk_sym_view_append(
            insp->symbolView,
            sugtk_sym_view_code_to_pixel_helper(
                suscan_gui_inspector_get_bits(insp),
                bits));
      }

      /* Feed transition matrix and phase plot */
      sugtk_trans_mtx_push(insp->transMatrix, bits);
      sugtk_waveform_push(insp->phasePlot, SU_C_ARG(msg->samples[i]) / PI);
      sugtk_histogram_push(insp->histogram, SU_C_ARG(msg->samples[i]));
    }

  /* Transition matrix has been fed. Update */
  if (full_samp_count > 0) {
    sugtk_trans_mtx_commit(insp->transMatrix);
    sugtk_waveform_commit(insp->phasePlot);
    sugtk_histogram_commit(insp->histogram);
  }

  if (insp->recording) {
    /* Wake up all listeners with new data */
    SU_TRYCATCH(suscan_gui_symsrc_commit(&insp->_parent), goto done);
  }

  for (i = 0; i < sample_count; ++i)
    sugtk_constellation_push(
        insp->constellation,
        msg->samples[msg->sample_count - sample_count + i]);

  sugtk_constellation_commit(insp->constellation);

  ok = SU_TRUE;

done:
  return ok;
}

char *
suscan_gui_inspector_to_filename(
    const suscan_gui_inspector_t *inspector,
    const char *prefix,
    const char *suffix)
{
  time_t now;
  struct tm *tm;
  const char *demod;

  time(&now);
  tm = localtime(&now);

  switch (suscan_gui_inspector_get_bits(inspector)) {
    case 1:
      demod = "bpsk";
      break;

    case 2:
      demod = "qpsk";
      break;

    case 3:
      demod = "8psk";
      break;

    default:
      demod = "mpsk";
  }

  return strbuild(
      "%s%+lldHz-%s-%ubaud-%02d%02d%02d-%02d%02d%04d%s",
      prefix,
      (long long int) round(inspector->channel.fc),
      demod,
      (unsigned int) round(inspector->baudrate),
      tm->tm_hour,
      tm->tm_min,
      tm->tm_sec,
      tm->tm_mday,
      tm->tm_mon,
      tm->tm_year + 1900,
      suffix);

}

SUPRIVATE void
suscan_gui_inspector_on_codec_progress(
    struct suscan_gui_symsrc *symsrc,
    const struct suscan_codec_progress *progress)
{
  suscan_gui_inspector_t *as_inspector =
      (suscan_gui_inspector_t *) symsrc;

  if (progress->updated) {
    gtk_widget_show_all(GTK_WIDGET(as_inspector->progressDialog));

    if (progress->progress == SUSCAN_CODEC_PROGRESS_UNDEFINED)
      gtk_progress_bar_pulse(as_inspector->progressBar);
    else
      gtk_progress_bar_set_fraction(
          as_inspector->progressBar,
          progress->progress);

    if (progress->message != NULL)
      gtk_progress_bar_set_text(
          as_inspector->progressBar,
          progress->message);
  }
}

SUPRIVATE void
suscan_gui_inspector_on_codec_error(
    struct suscan_gui_symsrc *symsrc,
    const struct suscan_codec_progress *progress)
{
  if (progress->updated && progress->message != NULL)
    suscan_error(
        symsrc->gui,
        "Codec error",
        "Codec error: %s",
        progress->message);
  else
    suscan_error(
        symsrc->gui,
        "Codec error",
        "Internal codec error");
}

SUPRIVATE void
suscan_gui_inspector_on_codec_unref(
    struct suscan_gui_symsrc *symsrc,
    const struct suscan_codec_progress *progress)
{
  suscan_gui_inspector_t *as_inspector =
      (suscan_gui_inspector_t *) symsrc;

  gtk_widget_hide(GTK_WIDGET(as_inspector->progressDialog));
}

SUPRIVATE void
suscan_gui_inspector_on_activate_codec(
    struct suscan_gui_codec_context *ctx,
    unsigned int direction)
{
  suscan_gui_inspector_t *as_inspector =
      (suscan_gui_inspector_t *) ctx->ui->symsrc;

  (void) suscan_gui_inspector_open_codec_tab(
      as_inspector,
      ctx->ui,
      ctx->codec->output_bits,
      direction,
      ctx->codec->symbolView,
      ctx->codec->symbuf);
}

SUPRIVATE void
suscan_gui_inspector_on_close_codec(
    struct suscan_gui_symsrc *symsrc,
    suscan_gui_codec_t *codec)
{
  suscan_gui_inspector_t *as_inspector =
      (suscan_gui_inspector_t *) symsrc;

  suscan_gui_inspector_remove_codec(as_inspector, codec);
}

SUBOOL
suscan_gui_inspector_open_codec_tab(
    suscan_gui_inspector_t *inspector,
    struct suscan_gui_codec_cfg_ui *ui,
    unsigned int bits,
    unsigned int direction,
    const SuGtkSymView *view,
    suscan_symbuf_t *source)
{
  suscan_gui_codec_t *codec = NULL;
  struct suscan_gui_codec_params params = suscan_gui_codec_params_INITIALIZER;
  guint start;
  guint end;

  params.symsrc = ui->symsrc;
  params.class = ui->desc;
  params.bits_per_symbol = bits;
  params.config = ui->config;
  params.direction = direction;
  params.source = source;

  /* GUI integration callbacks */
  params.on_parse_progress = suscan_gui_inspector_on_codec_progress;
  params.on_display_error  = suscan_gui_inspector_on_codec_error;
  params.on_unref          = suscan_gui_inspector_on_codec_unref;
  params.on_activate_codec = suscan_gui_inspector_on_activate_codec;
  params.on_close_codec    = suscan_gui_inspector_on_close_codec;

  /* In selection mode, live update is disabled */
  if (sugtk_sym_view_get_selection(view, &start, &end)) {
    params.live = SU_FALSE;
    params.start = start;
    params.end = end;
  } else {
    params.live = SU_TRUE;
  }

  if (suscan_gui_codec_cfg_ui_run(ui)) {
    if ((codec = suscan_gui_codec_new(&params)) == NULL) {

      if (direction == SU_CODEC_DIRECTION_FORWARDS) {
        suscan_error(
            ui->symsrc->gui,
            "Encoder constructor",
            "Failed to create encoder object. This usually means "
            "that the current encoder settings are not supported "
            "by the underlying implementation.\n\n"
            "You can get additional details on this error in the Log "
            "Messages tab");
      } else {
        suscan_error(
            ui->symsrc->gui,
            "Decoder constructor",
            "Failed to create codec object. This usually means "
            "that the current codec settings are not supported "
            "by the underlying implementation.\n\n"
            "You can get additional details on this error in the Log "
            "Messages tab");
      }
      goto fail;
    }

    SU_TRYCATCH(suscan_gui_inspector_add_codec(inspector, codec), goto fail);
  }

  return SU_TRUE;

fail:
  if (codec != NULL)
    suscan_gui_codec_destroy_hard(codec);

  return SU_FALSE;
}

SUPRIVATE void
suscan_gui_inspector_run_encoder(GtkWidget *widget, gpointer *data)
{
  struct suscan_gui_codec_cfg_ui *ui = (struct suscan_gui_codec_cfg_ui *) data;
  suscan_gui_inspector_t *as_inspector;

  if (!suscan_gui_codec_cfg_ui_assert_parent_gui(ui))
    return;  /* Weird */

  /* We can do this because this symsrc is actually an inspector tab */
  as_inspector = (suscan_gui_inspector_t *) ui->symsrc;

  (void) suscan_gui_inspector_open_codec_tab(
      as_inspector,
      ui,
      suscan_gui_inspector_get_bits(as_inspector),
      SUSCAN_CODEC_DIRECTION_FORWARDS,
      as_inspector->symbolView,
      ui->symsrc->symbuf);
}

SUPRIVATE void
suscan_gui_inspector_run_decoder(GtkWidget *widget, gpointer *data)
{
  struct suscan_gui_codec_cfg_ui *ui = (struct suscan_gui_codec_cfg_ui *) data;
  suscan_gui_inspector_t *as_inspector;

  if (!suscan_gui_codec_cfg_ui_assert_parent_gui(ui))
    return;  /* Weird */

  /* We can do this because this symsrc is actually an inspector tab */
  as_inspector = (suscan_gui_inspector_t *) ui->symsrc;

  (void) suscan_gui_inspector_open_codec_tab(
      as_inspector,
      ui,
      suscan_gui_inspector_get_bits(as_inspector),
      SUSCAN_CODEC_DIRECTION_BACKWARDS,
      as_inspector->symbolView,
      ui->symsrc->symbuf);
}

SUPRIVATE void *
suscan_gui_inspector_dummy_create_private(
    void *unused,
    struct suscan_gui_codec_cfg_ui *ui)
{
  return ui;
}

void
suscan_gui_inspector_add_spectrum_source(
    suscan_gui_inspector_t *inspector,
    const struct suscan_spectsrc_class *class,
    uint32_t id)
{
  char id_str[32];

  snprintf(id_str, sizeof(id_str), "%u", id);

  gtk_combo_box_text_append(
      inspector->spectrumSourceComboBoxText,
      id_str,
      class->desc);
}

SUBOOL
suscan_gui_inspector_add_estimatorui(
    suscan_gui_inspector_t *inspector,
    const struct suscan_estimator_class *class,
    uint32_t estimator_id)
{
  suscan_gui_estimatorui_t *ui = NULL;
  struct suscan_gui_estimatorui_params params;
  int index;

  params.desc = class->desc;
  params.field = class->field;
  params.inspector = inspector;
  params.estimator_id = estimator_id;

  SU_TRYCATCH(
      ui = suscan_gui_estimatorui_new(&params),
      goto fail);

  SU_TRYCATCH(
      (index = PTR_LIST_APPEND_CHECK(inspector->estimator, ui)) != -1,
      goto fail);

  suscan_gui_estimatorui_set_index(ui, index);

  gtk_grid_attach(
      inspector->estimatorGrid,
      suscan_gui_estimatorui_get_root(ui),
      0, /* left */
      index, /* top */
      1, /* width */
      1); /* height */

  return SU_TRUE;

fail:
  if (ui != NULL)
    suscan_gui_estimatorui_destroy(ui);

  return SU_FALSE;
}

SUPRIVATE void
suscan_gui_inspector_on_set_decider(
    SuGtkHistogram *hist,
    const struct sigutils_decider_params *params,
    gpointer data)
{
  suscan_gui_inspector_t *insp = (suscan_gui_inspector_t *) data;

  /* We only keep limit information */
  insp->decider_params.min_val = params->min_val;
  insp->decider_params.max_val = params->max_val;

  /* Initialize decider appropriately */
  if (insp->decider_params.bits != 0)
    su_decider_init(&insp->decider, &insp->decider_params);
}

SUPRIVATE SUBOOL
suscan_gui_inspector_load_all_widgets(suscan_gui_inspector_t *inspector)
{
  SU_TRYCATCH(
      inspector->spectrumSourceComboBoxText =
          GTK_COMBO_BOX_TEXT(gtk_builder_get_object(
              inspector->builder,
              "cbSpectrumSource")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->channelInspectorGrid =
          GTK_GRID(gtk_builder_get_object(
              inspector->builder,
              "grChannelInspector")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->estimatorGrid =
          GTK_GRID(gtk_builder_get_object(
              inspector->builder,
              "grEstimator")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->pageLabelEventBox =
          GTK_EVENT_BOX(gtk_builder_get_object(
              inspector->builder,
              "ebPageLabel")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->pageLabel =
          GTK_LABEL(gtk_builder_get_object(
              inspector->builder,
              "lPageLabel")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->recorderGrid =
          GTK_GRID(gtk_builder_get_object(
              inspector->builder,
              "grRecorder")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->autoScrollToggleButton =
          GTK_TOGGLE_TOOL_BUTTON(gtk_builder_get_object(
              inspector->builder,
              "tbAutoscroll")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->autoFitToggleButton =
          GTK_TOGGLE_TOOL_BUTTON(gtk_builder_get_object(
              inspector->builder,
              "tbFitWidth")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->offsetSpinButton =
          GTK_SPIN_BUTTON(gtk_builder_get_object(
              inspector->builder,
              "sbOffset")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->widthSpinButton =
          GTK_SPIN_BUTTON(gtk_builder_get_object(
              inspector->builder,
              "sbWidth")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->constellationNotebook =
          GTK_NOTEBOOK(gtk_builder_get_object(
              inspector->builder,
              "nbConstellation")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->transAlignment =
          GTK_ALIGNMENT(gtk_builder_get_object(
              inspector->builder,
              "aTransition")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->codecNotebook =
          GTK_NOTEBOOK(gtk_builder_get_object(
              inspector->builder,
              "nbDecoder")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->progressDialog =
          GTK_DIALOG(gtk_builder_get_object(
              inspector->builder,
              "dProgress")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->progressBar =
          GTK_PROGRESS_BAR(gtk_builder_get_object(
              inspector->builder,
              "pProgress")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->symViewScrollbar =
          GTK_SCROLLBAR(gtk_builder_get_object(
              inspector->builder,
              "sbSymView")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->symViewScrollAdjustment =
          GTK_ADJUSTMENT(gtk_builder_get_object(
              inspector->builder,
              "aSymViewScroll")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->controlsGrid =
          GTK_GRID(gtk_builder_get_object(
              inspector->builder,
              "grControls")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->freqLabel =
          GTK_LABEL(gtk_builder_get_object(
              inspector->builder,
              "lFreq")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->bwLabel =
          GTK_LABEL(gtk_builder_get_object(
              inspector->builder,
              "lBw")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->snrLabel =
          GTK_LABEL(gtk_builder_get_object(
              inspector->builder,
              "lSNR")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->spectrumAlignment =
          GTK_ALIGNMENT(gtk_builder_get_object(
              inspector->builder,
              "aSpectrum")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->constellationAlignment =
          GTK_ALIGNMENT(gtk_builder_get_object(
              inspector->builder,
              "aConstellation")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->phasePlotAlignment =
          GTK_ALIGNMENT(gtk_builder_get_object(
              inspector->builder,
              "aPhasePlot")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->histogramAlignment =
          GTK_ALIGNMENT(gtk_builder_get_object(
              inspector->builder,
              "aHistogram")),
          return SU_FALSE);

  /* Add symbol view */
  inspector->symbolView = SUGTK_SYM_VIEW(sugtk_sym_view_new());

  g_signal_connect(
      G_OBJECT(inspector->symbolView),
      "reshape",
      G_CALLBACK(suscan_gui_inspector_on_reshape),
      inspector);

  gtk_grid_attach(
      inspector->recorderGrid,
      GTK_WIDGET(inspector->symbolView),
      0, /* left */
      0, /* top */
      1, /* width */
      1 /* height */);

  SU_TRYCATCH(
      suscan_gui_symsrc_populate_codec_menu(
          &inspector->_parent,
          inspector->symbolView,
          suscan_gui_inspector_dummy_create_private,
          NULL,
          G_CALLBACK(suscan_gui_inspector_run_encoder),
          G_CALLBACK(suscan_gui_inspector_run_decoder)),
      return SU_FALSE);

  gtk_widget_set_hexpand(GTK_WIDGET(inspector->symbolView), TRUE);
  gtk_widget_set_vexpand(GTK_WIDGET(inspector->symbolView), TRUE);

  gtk_widget_show(GTK_WIDGET(inspector->symbolView));

  /* Add transition matrix view */
  inspector->transMatrix = SUGTK_TRANS_MTX(sugtk_trans_mtx_new());

  gtk_container_add(
      GTK_CONTAINER(inspector->transAlignment),
      GTK_WIDGET(inspector->transMatrix));

  gtk_widget_set_hexpand(GTK_WIDGET(inspector->transMatrix), TRUE);
  gtk_widget_set_vexpand(GTK_WIDGET(inspector->transMatrix), TRUE);

  gtk_widget_show(GTK_WIDGET(inspector->transMatrix));

  /* Add phase plot widget */
  inspector->phasePlot = SUGTK_WAVEFORM(sugtk_waveform_new());

  gtk_container_add(
      GTK_CONTAINER(inspector->phasePlotAlignment),
      GTK_WIDGET(inspector->phasePlot));

  gtk_widget_set_hexpand(GTK_WIDGET(inspector->phasePlot), TRUE);
  gtk_widget_set_vexpand(GTK_WIDGET(inspector->phasePlot), TRUE);

  gtk_widget_show(GTK_WIDGET(inspector->phasePlot));

  /* Add constellation widget */
  inspector->constellation = SUGTK_CONSTELLATION(sugtk_constellation_new());
  gtk_container_add(
      GTK_CONTAINER(inspector->constellationAlignment),
      GTK_WIDGET(inspector->constellation));

  gtk_widget_set_hexpand(GTK_WIDGET(inspector->constellation), TRUE);
  gtk_widget_set_vexpand(GTK_WIDGET(inspector->constellation), TRUE);

  gtk_widget_show(GTK_WIDGET(inspector->constellation));

  /* Add spectrum widget */
  inspector->spectrum = SUGTK_SPECTRUM(sugtk_spectrum_new());
  sugtk_spectrum_set_smooth_N0(inspector->spectrum, TRUE);
  sugtk_spectrum_set_has_menu(inspector->spectrum, TRUE);
  sugtk_spectrum_set_dc_skip(inspector->spectrum, FALSE);

  gtk_container_add(
      GTK_CONTAINER(inspector->spectrumAlignment),
      GTK_WIDGET(inspector->spectrum));

  gtk_widget_set_hexpand(GTK_WIDGET(inspector->spectrum), TRUE);
  gtk_widget_set_vexpand(GTK_WIDGET(inspector->spectrum), TRUE);

  gtk_widget_show(GTK_WIDGET(inspector->spectrum));

  /* Add histogram widget */
  inspector->histogram = SUGTK_HISTOGRAM(sugtk_histogram_new());
  gtk_container_add(
      GTK_CONTAINER(inspector->histogramAlignment),
      GTK_WIDGET(inspector->histogram));

  gtk_widget_set_hexpand(GTK_WIDGET(inspector->histogram), TRUE);
  gtk_widget_set_vexpand(GTK_WIDGET(inspector->histogram), TRUE);

  g_signal_connect(
      G_OBJECT(inspector->histogram),
      "set-decider",
      G_CALLBACK(suscan_gui_inspector_on_set_decider),
      inspector);

  gtk_widget_show(GTK_WIDGET(inspector->histogram));

  /* Somehow Glade fails to set these default values */
  gtk_toggle_tool_button_set_active(
      GTK_TOGGLE_TOOL_BUTTON(inspector->autoScrollToggleButton),
      TRUE);

  gtk_toggle_tool_button_set_active(
        GTK_TOGGLE_TOOL_BUTTON(inspector->autoFitToggleButton),
        TRUE);

  return SU_TRUE;
}

SUBOOL
suscan_gui_inspector_commit_config(suscan_gui_inspector_t *insp)
{
  SU_TRYCATCH(
      suscan_analyzer_set_inspector_config_async(
          insp->_parent.gui->analyzer,
          insp->inshnd,
          insp->config,
          rand()),
      return SU_FALSE);

  return SU_TRUE;
}

SUBOOL
suscan_gui_inspector_on_config_changed(suscan_gui_inspector_t *insp)
{
  struct suscan_field_value *value;

  if ((value = suscan_config_get_value(
      insp->config,
      "afc.bits-per-symbol")) != NULL)
    suscan_gui_inspector_set_bits(insp, value->as_int);
  else if ((value = suscan_config_get_value(
      insp->config,
      "fsk.bits-per-symbol")) != NULL)
    suscan_gui_inspector_set_bits(insp, value->as_int);
  else
    suscan_gui_inspector_set_bits(insp, 1);

  return SU_TRUE;
}

/* Used for outcoming configuration */
void
suscan_gui_inspector_on_update_config(suscan_gui_modemctl_t *ctl, void *data)
{
  suscan_gui_inspector_t *insp = (suscan_gui_inspector_t *) data;

  /* This only makes sense if the inspector is tied to a GUI */
  if (insp->index != -1)
    SU_TRYCATCH(
        suscan_gui_inspector_commit_config(insp),
        return);

  SU_TRYCATCH(suscan_gui_inspector_on_config_changed(insp), return);
}

SUBOOL
suscan_gui_inspector_refresh_on_config(suscan_gui_inspector_t *insp)
{
  SU_TRYCATCH(
        suscan_gui_modemctl_set_refresh(&insp->modemctl_set),
        return SU_FALSE);

  SU_TRYCATCH(suscan_gui_inspector_on_config_changed(insp), return SU_FALSE);

  return SU_TRUE;
}

/* Used for incoming configuration */
SUBOOL
suscan_gui_inspector_set_config(
    suscan_gui_inspector_t *insp,
    const suscan_config_t *config)
{
  SU_TRYCATCH(suscan_config_copy(insp->config, config), return SU_FALSE);

  SU_TRYCATCH(suscan_gui_inspector_refresh_on_config(insp), return SU_FALSE);

  return SU_TRUE;
}

SUPRIVATE void
suscan_gui_inspector_populate_channel_summary(suscan_gui_inspector_t *insp)
{
  char text[64];

  snprintf(text, sizeof(text), "%lg Hz", insp->channel.fc);
  gtk_label_set_text(insp->freqLabel, text);

  snprintf(text, sizeof(text), "%lg Hz", insp->channel.bw);
  gtk_label_set_text(insp->bwLabel, text);

  snprintf(text, sizeof(text), "%lg dB", insp->channel.snr);
  gtk_label_set_text(insp->snrLabel, text);
}

SUPRIVATE const char *
suscan_gui_inspector_class_to_desc(const char *class)
{
  const struct suscan_inspector_interface *iface;

  if ((iface = suscan_inspector_interface_lookup(class)) == NULL)
    return class;
  else
    return iface->desc;
}

suscan_gui_inspector_t *
suscan_gui_inspector_new(
    const char *class,
    const struct sigutils_channel *channel,
    const suscan_config_t *config,
    SUHANDLE handle)
{
  suscan_gui_inspector_t *new = NULL;
  struct sigutils_decider_params params = sigutils_decider_params_INITIALIZER;
  char *page_label = NULL;
  unsigned int i;

  SU_TRYCATCH(new = calloc(1, sizeof (suscan_gui_inspector_t)), goto fail);

  /* Superclass constructor */
  SU_TRYCATCH(suscan_gui_symsrc_init(&new->_parent, NULL), goto fail);

  new->channel = *channel;
  new->index = -1;
  new->inshnd = handle;
  new->decider_params = params;

  SU_TRYCATCH(new->config = suscan_config_new(config->desc), return SU_FALSE);

  SU_TRYCATCH(
      new->builder = gtk_builder_new_from_file(
          PKGDATADIR "/gui/channel-inspector-new.glade"),
      goto fail);

  SU_TRYCATCH(suscan_gui_inspector_load_all_widgets(new), goto fail);

  gtk_builder_connect_signals(new->builder, new);

  sugtk_spectrum_set_mode(new->spectrum, SUSCAN_GUI_INSPECTOR_SPECTRUM_MODE);
  sugtk_spectrum_set_auto_level(new->spectrum, TRUE);
  sugtk_spectrum_set_show_channels(new->spectrum, FALSE);
  sugtk_spectrum_set_smooth_N0(new->spectrum, TRUE);
  sugtk_spectrum_set_agc_alpha(
      new->spectrum,
      SUSCAN_GUI_INSPECTOR_SPECTRUM_AGC_ALPHA);

  SU_TRYCATCH(
      page_label = strbuild(
          "%s at %lli Hz",
          suscan_gui_inspector_class_to_desc(class),
          (uint64_t) round(channel->fc)),
      goto fail);

  gtk_label_set_text(new->pageLabel, page_label);

  free(page_label);
  page_label = NULL;

  /* Set bits per symbol to 0 */
  suscan_gui_inspector_set_bits(new, 0);

  /* Initialize inspector-specific set of modem controls */
  SU_TRYCATCH(
      suscan_gui_modemctl_set_init(
          &new->modemctl_set,
          new->config,
          suscan_gui_inspector_on_update_config,
          new),
      goto fail);

  /* Add them to the control grid */
  for (i = 0; i < new->modemctl_set.modemctl_count; ++i)
    gtk_grid_attach(
        new->controlsGrid,
        suscan_gui_modemctl_get_root(new->modemctl_set.modemctl_list[i]),
        0, /* left */
        i, /* top */
        1, /* width */
        1 /* height */);

  /* Set config */
  SU_TRYCATCH(suscan_gui_inspector_set_config(new, config), goto fail);

  /* Update channel summary */
  suscan_gui_inspector_populate_channel_summary(new);

  return new;

fail:
  if (new != NULL)
    suscan_gui_inspector_destroy(new);

  if (page_label != NULL)
    free(page_label);

  return NULL;
}

/************************** Inspector tab callbacks **************************/
void
suscan_on_close_inspector_tab(GtkWidget *widget, gpointer data)
{
  suscan_gui_inspector_t *insp = (suscan_gui_inspector_t *) data;

  if (!insp->dead) {
    /*
     * Inspector is not dead: send a close signal and wait for analyzer
     * response to close it
     */
    suscan_gui_inspector_close(insp);
  } else {
    /*
     * Inspector is dead (because its analyzer has disappeared). Just
     * remove the page and free allocated memory
     */
    suscan_gui_remove_inspector(insp->_parent.gui, insp);

    suscan_gui_inspector_destroy(insp);
  }
}

void
suscan_inspector_on_save(
    GtkWidget *widget,
    gpointer data)
{
  suscan_gui_inspector_t *insp = (suscan_gui_inspector_t *) data;
  char *new_fname = NULL;

  SU_TRYCATCH(
      new_fname = suscan_gui_inspector_to_filename(insp, "symbols", ".log"),
      goto done);

  SU_TRYCATCH(
      sugtk_sym_view_save_helper(
          insp->symbolView,
          "Save symbol view",
          new_fname,
          suscan_gui_inspector_get_bits(insp)),
      goto done);

done:
  if (new_fname != NULL)
    free(new_fname);
}

void
suscan_inspector_on_toggle_record(
    GtkWidget *widget,
    gpointer data)
{
  suscan_gui_inspector_t *insp = (suscan_gui_inspector_t *) data;

  insp->recording = gtk_toggle_tool_button_get_active(
      GTK_TOGGLE_TOOL_BUTTON(widget));
}

void
suscan_inspector_on_clear(
    GtkWidget *widget,
    gpointer data)
{
  suscan_gui_inspector_t *insp = (suscan_gui_inspector_t *) data;

  sugtk_sym_view_clear(insp->symbolView);
}

void
suscan_inspector_on_zoom_in(
    GtkWidget *widget,
    gpointer data)
{
  suscan_gui_inspector_t *insp = (suscan_gui_inspector_t *) data;
  guint curr_width = sugtk_sym_view_get_width(insp->symbolView);
  guint curr_zoom = sugtk_sym_view_get_zoom(insp->symbolView);

  curr_zoom <<= 1;

  if (curr_width < curr_zoom)
    curr_zoom = curr_width;

  sugtk_sym_view_set_zoom(insp->symbolView, curr_zoom);
}


void
suscan_inspector_on_zoom_out(
    GtkWidget *widget,
    gpointer data)
{
  suscan_gui_inspector_t *insp = (suscan_gui_inspector_t *) data;
  guint curr_width = sugtk_sym_view_get_width(insp->symbolView);
  guint curr_zoom = sugtk_sym_view_get_zoom(insp->symbolView);

  curr_zoom >>= 1;

  if (curr_zoom < 1)
    curr_zoom = 1;

  sugtk_sym_view_set_zoom(insp->symbolView, curr_zoom);
}

void
suscan_inspector_on_toggle_autoscroll(
    GtkWidget *widget,
    gpointer data)
{
  suscan_gui_inspector_t *insp = (suscan_gui_inspector_t *) data;
  gboolean active;

  active = gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(widget));

  sugtk_sym_view_set_autoscroll(insp->symbolView, active);
  gtk_widget_set_sensitive(GTK_WIDGET(insp->offsetSpinButton), !active);
}

void
suscan_inspector_on_toggle_autofit(
    GtkWidget *widget,
    gpointer data)
{
  suscan_gui_inspector_t *insp = (suscan_gui_inspector_t *) data;
  gboolean active;

  active = gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(widget));

  sugtk_sym_view_set_autofit(insp->symbolView, active);
  gtk_widget_set_sensitive(GTK_WIDGET(insp->widthSpinButton), !active);
}

void
suscan_inspector_on_set_offset(
    GtkWidget *widget,
    gpointer data)
{
  suscan_gui_inspector_t *insp = (suscan_gui_inspector_t *) data;

  if (!gtk_toggle_tool_button_get_active(
      GTK_TOGGLE_TOOL_BUTTON(insp->autoScrollToggleButton)))
    sugtk_sym_view_set_offset(
        insp->symbolView,
        gtk_spin_button_get_value(insp->offsetSpinButton));
}

void
suscan_inspector_on_set_width(
    GtkWidget *widget,
    gpointer data)
{
  suscan_gui_inspector_t *insp = (suscan_gui_inspector_t *) data;

  if (!gtk_toggle_tool_button_get_active(
      GTK_TOGGLE_TOOL_BUTTON(insp->autoFitToggleButton)))
    sugtk_sym_view_set_width(
        insp->symbolView,
        gtk_spin_button_get_value(insp->widthSpinButton));
}

void
suscan_gui_inspector_on_reshape(GtkWidget *widget, gpointer data)
{
  suscan_gui_inspector_t *insp = (suscan_gui_inspector_t *) data;

  suscan_gui_inspector_update_spin_buttons(insp);
}


/************************* Decoder tab handling ******************************/
SUBOOL
suscan_gui_inspector_remove_codec(
    suscan_gui_inspector_t *gui,
    suscan_gui_codec_t *codec)
{
  gint num;

  SU_TRYCATCH(
      suscan_gui_symsrc_unregister_codec(&gui->_parent, codec),
      return SU_FALSE);

  SU_TRYCATCH(
      (num = gtk_notebook_page_num(
          gui->codecNotebook,
          suscan_gui_codec_get_root(codec))) != -1,
      return SU_FALSE);

  gtk_notebook_remove_page(gui->codecNotebook, num);

  return SU_TRUE;
}

SUBOOL
suscan_gui_inspector_add_codec(
    suscan_gui_inspector_t *inspector,
    suscan_gui_codec_t *codec)
{
  gint page;
  SUBOOL codec_added = SU_FALSE;

  SU_TRYCATCH(
      suscan_gui_symsrc_register_codec(&inspector->_parent, codec),
      goto fail);

  codec_added = SU_TRUE;

  SU_TRYCATCH(
      (page = gtk_notebook_append_page_menu(
          inspector->codecNotebook,
          suscan_gui_codec_get_root(codec),
          suscan_gui_codec_get_label(codec),
          NULL)) >= 0,
      goto fail);

  gtk_notebook_set_tab_reorderable(
      inspector->codecNotebook,
      suscan_gui_codec_get_root(codec),
      TRUE);

  gtk_notebook_set_current_page(inspector->codecNotebook, page);

  return TRUE;

fail:
  if (codec_added)
    (void) suscan_gui_inspector_remove_codec(inspector, codec);

  return FALSE;
}

void
suscan_inspector_on_scroll(GtkWidget *widget, gpointer data)
{
  suscan_gui_inspector_t *inspector = (suscan_gui_inspector_t *) data;

  sugtk_sym_view_set_offset(
      inspector->symbolView,
      floor(gtk_adjustment_get_value(inspector->symViewScrollAdjustment))
      * sugtk_sym_view_get_width(inspector->symbolView));
}

void
suscan_inspector_on_change_spectrum(GtkWidget *widget, gpointer data)
{
  suscan_gui_inspector_t *inspector = (suscan_gui_inspector_t *) data;
  int id;

  id = suscan_gui_modemctl_helper_try_read_combo_id(
      GTK_COMBO_BOX(inspector->spectrumSourceComboBoxText));

  suscan_analyzer_inspector_set_spectrum_async(
      inspector->_parent.gui->analyzer,
      inspector->inshnd,
      id,
      rand());

  sugtk_spectrum_reset(inspector->spectrum);
}

void
suscan_inspector_on_spectrum_center(GtkWidget *widget, gpointer data)
{
  suscan_gui_inspector_t *inspector = (suscan_gui_inspector_t *) data;

  sugtk_spectrum_reset(inspector->spectrum);
}

void
suscan_inspector_on_spectrum_reset(GtkWidget *widget, gpointer data)
{
  suscan_gui_inspector_t *inspector = (suscan_gui_inspector_t *) data;

  sugtk_spectrum_set_freq_offset(inspector->spectrum, 0);
}

void
suscan_inspector_on_toggle_spectrum_autolevel(GtkWidget *widget, gpointer data)
{
  suscan_gui_inspector_t *inspector = (suscan_gui_inspector_t *) data;

  sugtk_spectrum_set_auto_level(
      inspector->spectrum,
      gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)));
}

void
suscan_inspector_on_toggle_spectrum_mode(GtkWidget *widget, gpointer data)
{
  suscan_gui_inspector_t *inspector = (suscan_gui_inspector_t *) data;
  SUBOOL use_wf = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));

  if (use_wf) {
    sugtk_spectrum_set_mode(
        inspector->spectrum,
        SUGTK_SPECTRUM_MODE_WATERFALL);
    gtk_button_set_label(GTK_BUTTON(widget), "Waterfall");
  } else {
    sugtk_spectrum_set_mode(
        inspector->spectrum,
        SUGTK_SPECTRUM_MODE_SPECTROGRAM);
    gtk_button_set_label(GTK_BUTTON(widget), "Spectrogram");
  }
}

