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

#include "gui.h"
#include <sigutils/agc.h>

void
suscan_gui_inspector_destroy(struct suscan_gui_inspector *inspector)
{
  /*
   * There are no GTK methods to destroy a GtkBuilder. Actual release
   * happens after performing destroy on all top-level widgets.
   */

  if (inspector->inshnd != -1)
    suscan_inspector_close_async(
        inspector->gui->analyzer,
        inspector->inshnd,
        rand());

  /* TODO: gtk_widget_destroy on all toplevels */
  if (inspector->channelInspectorGrid != NULL)
    gtk_widget_destroy(GTK_WIDGET(inspector->channelInspectorGrid));

  if (inspector->pageLabelEventBox != NULL)
    gtk_widget_destroy(GTK_WIDGET(inspector->pageLabelEventBox));

  suscan_gui_spectrum_init(&inspector->spectrum);

  free(inspector);
}

SUBOOL
suscan_gui_inspector_update_sensitiveness(
    struct suscan_gui_inspector *insp,
    const struct suscan_inspector_params *params)
{
  gtk_widget_set_sensitive(GTK_WIDGET(insp->channelInspectorGrid), TRUE);

  gtk_widget_set_sensitive(
      GTK_WIDGET(insp->carrierManualAlignment),
      params->fc_ctrl == SUSCAN_INSPECTOR_CARRIER_CONTROL_MANUAL);

  gtk_widget_set_sensitive(
      GTK_WIDGET(insp->clockManualAlignment),
      params->br_ctrl == SUSCAN_INSPECTOR_BAUDRATE_CONTROL_MANUAL);

  gtk_widget_set_sensitive(
      GTK_WIDGET(insp->clockGardnerAlignment),
      params->br_ctrl == SUSCAN_INSPECTOR_BAUDRATE_CONTROL_GARDNER);

  gtk_widget_set_sensitive(
      GTK_WIDGET(insp->gardnerBetaEntry),
      gtk_toggle_button_get_active(
          GTK_TOGGLE_BUTTON(insp->gardnerEnableBetaCheckButton)));

  gtk_widget_set_sensitive(GTK_WIDGET(insp->baudRateEntry), TRUE);
  gtk_widget_set_sensitive(GTK_WIDGET(insp->setBaudRateButton), TRUE);
  gtk_widget_set_sensitive(GTK_WIDGET(insp->detectBaudRateFACButton), TRUE);
  gtk_widget_set_sensitive(GTK_WIDGET(insp->detectBaudRateNLNButton), TRUE);

  /* TODO: setup some values according to params */

  return SU_TRUE;
}

void
suscan_gui_inspector_disable(struct suscan_gui_inspector *insp)
{
  gtk_widget_set_sensitive(GTK_WIDGET(insp->channelInspectorGrid), FALSE);
}

void
suscan_gui_inspector_feed_w_batch(
    struct suscan_gui_inspector *inspector,
    const struct suscan_analyzer_sample_batch_msg *msg)
{
  unsigned sample_count;
  unsigned int i;
  /*
   * Push, at most, the last SUSCAN_GUI_CONSTELLATION_HISTORY. We do this
   * because the previous ones will never be shown
   */
  sample_count = MIN(msg->sample_count, SUSCAN_GUI_CONSTELLATION_HISTORY);

  for (i = 0; i < sample_count; ++i)
    suscan_gui_constellation_push_sample(
        &inspector->constellation,
        msg->samples[msg->sample_count - sample_count + i]);
}

SUPRIVATE SUBOOL
suscan_gui_inspector_load_all_widgets(struct suscan_gui_inspector *inspector)
{
  SU_TRYCATCH(
      inspector->channelInspectorGrid =
          GTK_GRID(gtk_builder_get_object(
              inspector->builder,
              "grChannelInspector")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->carrierOffsetEntry =
          GTK_ENTRY(gtk_builder_get_object(
              inspector->builder,
              "eCarrierOffset")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->fineTuneScale =
          GTK_SCALE(gtk_builder_get_object(
              inspector->builder,
              "sFineTune")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->phaseScale =
          GTK_SCALE(gtk_builder_get_object(
              inspector->builder,
              "sPhase")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->baudRateEntry =
          GTK_ENTRY(gtk_builder_get_object(
              inspector->builder,
              "eBaudRate")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->setBaudRateButton =
          GTK_BUTTON(gtk_builder_get_object(
              inspector->builder,
              "bSetBaudRate")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->detectBaudRateFACButton =
          GTK_BUTTON(gtk_builder_get_object(
              inspector->builder,
              "bDetectBaudRateFAC")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->detectBaudRateNLNButton =
          GTK_BUTTON(gtk_builder_get_object(
              inspector->builder,
              "bDetectBaudRateNLN")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->fineBaudScale =
          GTK_SCALE(gtk_builder_get_object(
              inspector->builder,
              "sFineBaud")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->symbolPhaseScale =
          GTK_SCALE(gtk_builder_get_object(
              inspector->builder,
              "sSymbolPhase")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->costas2RadioButton =
          GTK_RADIO_BUTTON(gtk_builder_get_object(
              inspector->builder,
              "rbCostas2")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->costas4RadioButton =
          GTK_RADIO_BUTTON(gtk_builder_get_object(
              inspector->builder,
              "rbCostas4")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->manualRadioButton =
          GTK_RADIO_BUTTON(gtk_builder_get_object(
              inspector->builder,
              "rbManual")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->clockGardnerRadioButton =
          GTK_RADIO_BUTTON(gtk_builder_get_object(
              inspector->builder,
              "rbClockGardner")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->clockManualRadioButton =
          GTK_RADIO_BUTTON(gtk_builder_get_object(
              inspector->builder,
              "rbClockManual")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->clockDisableButton =
          GTK_RADIO_BUTTON(gtk_builder_get_object(
              inspector->builder,
              "rbClockDisable")),
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
      inspector->clockGardnerAlignment =
          GTK_ALIGNMENT(gtk_builder_get_object(
              inspector->builder,
              "alClockGardner")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->clockManualAlignment =
          GTK_ALIGNMENT(gtk_builder_get_object(
              inspector->builder,
              "alClockManual")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->carrierManualAlignment =
          GTK_ALIGNMENT(gtk_builder_get_object(
              inspector->builder,
              "alCarrierManual")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->gardnerAlphaEntry =
          GTK_ENTRY(gtk_builder_get_object(
              inspector->builder,
              "eGardnerAlpha")),
          return SU_FALSE);
  SU_TRYCATCH(
      inspector->gardnerEnableBetaCheckButton =
          GTK_CHECK_BUTTON(gtk_builder_get_object(
              inspector->builder,
              "cbGardnerEnableBeta")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->gardnerBetaEntry =
          GTK_ENTRY(gtk_builder_get_object(
              inspector->builder,
              "eGardnerBeta")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->powerSpectrumRadioButton =
          GTK_RADIO_BUTTON(gtk_builder_get_object(
              inspector->builder,
              "rbPowerSpectrum")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->cycloSpectrumRadioButton =
          GTK_RADIO_BUTTON(gtk_builder_get_object(
              inspector->builder,
              "rbCycloSpectrum")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->noSpectrumRadioButton =
          GTK_RADIO_BUTTON(gtk_builder_get_object(
              inspector->builder,
              "rbNoSpectrum")),
          return SU_FALSE);


  SU_TRYCATCH(
      inspector->automaticGainRadioButton =
          GTK_RADIO_BUTTON(gtk_builder_get_object(
              inspector->builder,
              "rbGainControlAuto")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->manualGainRadioButton =
          GTK_RADIO_BUTTON(gtk_builder_get_object(
              inspector->builder,
              "rbGainControlAuto")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->gainManualAlignment =
          GTK_ALIGNMENT(gtk_builder_get_object(
              inspector->builder,
              "alManualGainControl")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->gainEntry =
          GTK_ENTRY(gtk_builder_get_object(
              inspector->builder,
              "eGain")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->gainFineTuneScale =
          GTK_SCALE(gtk_builder_get_object(
              inspector->builder,
              "sGainFineTune")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->matchedFilterBypassRadioButton =
          GTK_RADIO_BUTTON(gtk_builder_get_object(
              inspector->builder,
              "rbMatchedFilterBypass")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->matchedFilterRRCRadioButton =
          GTK_RADIO_BUTTON(gtk_builder_get_object(
              inspector->builder,
              "rbMatchedFilterRRC")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->rootRaisedCosineAlignment =
          GTK_ALIGNMENT(gtk_builder_get_object(
              inspector->builder,
              "alRootRaisedCosine")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->rollOffScale =
          GTK_SCALE(gtk_builder_get_object(
              inspector->builder,
              "sRollOff")),
          return SU_FALSE);

  /* Somehow Glade fails to set these default values */
  gtk_toggle_button_set_active(
      GTK_TOGGLE_BUTTON(inspector->manualRadioButton),
      TRUE);

  gtk_toggle_button_set_active(
      GTK_TOGGLE_BUTTON(inspector->clockManualRadioButton),
      TRUE);

  gtk_toggle_button_set_active(
      GTK_TOGGLE_BUTTON(inspector->noSpectrumRadioButton),
      TRUE);

  return SU_TRUE;
}

struct suscan_gui_inspector *
suscan_gui_inspector_new(
    const struct sigutils_channel *channel,
    SUHANDLE handle)
{
  struct suscan_gui_inspector *new = NULL;
  char *page_label = NULL;

  SU_TRYCATCH(new = calloc(1, sizeof (struct suscan_gui_inspector)), goto fail);

  new->channel = *channel;
  new->index = -1;
  new->inshnd = handle;

  suscan_gui_constellation_init(&new->constellation);
  suscan_gui_spectrum_init(&new->spectrum);

  SU_TRYCATCH(
      new->builder = gtk_builder_new_from_file(
          PKGDATADIR "/gui/channel-inspector.glade"),
      goto fail);

  SU_TRYCATCH(suscan_gui_inspector_load_all_widgets(new), goto fail);

  gtk_builder_connect_signals(new->builder, new);

  SU_TRYCATCH(
      page_label = strbuild(
          "Inspecting at %lli Hz",
          (uint64_t) round(channel->fc)),
      goto fail);

  gtk_label_set_text(new->pageLabel, page_label);

  free(page_label);
  page_label = NULL;

  /* Update sensitiveness */
  suscan_gui_inspector_update_sensitiveness(new, &new->params);

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
suscan_on_get_baudrate_fac(GtkWidget *widget, gpointer data)
{
  struct suscan_gui_inspector *insp = (struct suscan_gui_inspector *) data;

  suscan_inspector_get_info_async(insp->gui->analyzer, insp->inshnd, 0);
}

void
suscan_on_get_baudrate_nln(GtkWidget *widget, gpointer data)
{
  struct suscan_gui_inspector *insp = (struct suscan_gui_inspector *) data;

  suscan_inspector_get_info_async(insp->gui->analyzer, insp->inshnd, 1);
}

SUPRIVATE void
suscan_attempt_to_read_entry(GtkEntry *entry, SUFLOAT *result)
{
  const gchar *text;
  char number[32];
  SUFLOAT value;

  text = gtk_entry_get_text(entry);
  if (sscanf(text, SUFLOAT_FMT, &value) < 1) {
    snprintf(number, sizeof(number), SUFLOAT_FMT, *result);
    gtk_entry_set_text(entry, number);
  } else {
    *result = value;
  }
}

void
suscan_on_change_inspector_params(GtkWidget *widget, gpointer data)
{
  struct suscan_gui_inspector *insp = (struct suscan_gui_inspector *) data;

  SUFLOAT freq;
  SUFLOAT baud;
  SUFLOAT gain;
  SUFLOAT alpha;
  SUFLOAT beta;

  /* Block callback while we check values */
  g_signal_handlers_block_matched(
      G_OBJECT(widget),
      G_SIGNAL_MATCH_FUNC,
      0,
      0,
      NULL,
      suscan_on_change_inspector_params,
      NULL);

  gain = round(SU_DB_RAW(insp->params.gc_gain));
  suscan_attempt_to_read_entry(insp->gainEntry, &gain);
  gain += gtk_range_get_value(GTK_RANGE(insp->gainFineTuneScale));

  freq = insp->params.fc_off;
  suscan_attempt_to_read_entry(insp->carrierOffsetEntry, &freq);

  baud = insp->params.baud;
  suscan_attempt_to_read_entry(insp->baudRateEntry, &baud);

  alpha = round(SU_DB_RAW(insp->params.br_alpha));
  suscan_attempt_to_read_entry(insp->gardnerAlphaEntry, &alpha);

  beta = round(SU_DB_RAW(insp->params.br_beta));
  suscan_attempt_to_read_entry(insp->gardnerBetaEntry, &beta);

  /* Our work is done here */
  g_signal_handlers_unblock_matched(
       G_OBJECT(widget),
       G_SIGNAL_MATCH_FUNC,
       0,
       0,
       NULL,
       suscan_on_change_inspector_params,
       NULL);

  /* Set matched filter */
  if (gtk_toggle_button_get_active(
      GTK_TOGGLE_BUTTON(insp->matchedFilterBypassRadioButton)))
    insp->params.mf_conf = SUSCAN_INSPECTOR_MATCHED_FILTER_BYPASS;
  else
    insp->params.mf_conf = SUSCAN_INSPECTOR_MATCHED_FILTER_MANUAL;

  insp->params.mf_rolloff = gtk_range_get_value(GTK_RANGE(insp->rollOffScale));

  /* Set gain control */
  if (gtk_toggle_button_get_active(
      GTK_TOGGLE_BUTTON(insp->automaticGainRadioButton)))
    insp->params.gc_ctrl = SUSCAN_INSPECTOR_GAIN_CONTROL_AUTOMATIC;
  else
    insp->params.gc_ctrl = SUSCAN_INSPECTOR_GAIN_CONTROL_MANUAL;

  insp->params.gc_gain = SU_MAG_RAW(gain);

  /* Set carrier control */
  if (gtk_toggle_button_get_active(
      GTK_TOGGLE_BUTTON(insp->costas2RadioButton)))
    insp->params.fc_ctrl = SUSCAN_INSPECTOR_CARRIER_CONTROL_COSTAS_2;
  else if (gtk_toggle_button_get_active(
      GTK_TOGGLE_BUTTON(insp->costas4RadioButton)))
    insp->params.fc_ctrl = SUSCAN_INSPECTOR_CARRIER_CONTROL_COSTAS_4;
  else
    insp->params.fc_ctrl = SUSCAN_INSPECTOR_CARRIER_CONTROL_MANUAL;

  insp->params.fc_off =
      freq + gtk_range_get_value(GTK_RANGE(insp->fineTuneScale));

  insp->params.fc_phi =
      gtk_range_get_value(GTK_RANGE(insp->phaseScale)) / 180 * M_PI;


  /* Set baudrate control */
  if (gtk_toggle_button_get_active(
        GTK_TOGGLE_BUTTON(insp->clockDisableButton))) {
    insp->params.br_ctrl = SUSCAN_INSPECTOR_BAUDRATE_CONTROL_MANUAL;
    insp->params.baud = 0;
  } else {
    if (gtk_toggle_button_get_active(
        GTK_TOGGLE_BUTTON(insp->clockGardnerRadioButton))) {
      insp->params.br_ctrl = SUSCAN_INSPECTOR_BAUDRATE_CONTROL_GARDNER;

      insp->params.br_alpha = SU_MAG_RAW(alpha);
      insp->params.br_beta = gtk_toggle_button_get_active(
          GTK_TOGGLE_BUTTON(insp->gardnerEnableBetaCheckButton))
              ? SU_MAG_RAW(beta)
              : 0;
    } else if (gtk_toggle_button_get_active(
        GTK_TOGGLE_BUTTON(insp->clockManualRadioButton))) {
      insp->params.br_ctrl = SUSCAN_INSPECTOR_BAUDRATE_CONTROL_MANUAL;
    }

    insp->params.baud = baud +
        gtk_range_get_value(GTK_RANGE(insp->fineBaudScale));

    insp->params.sym_phase =
        gtk_range_get_value(GTK_RANGE(insp->symbolPhaseScale));

    if (insp->params.sym_phase < .0)
      insp->params.sym_phase += 1.0;
  }

  /* Configure spectrum */
  if (gtk_toggle_button_get_active(
      GTK_TOGGLE_BUTTON(insp->powerSpectrumRadioButton)))
    insp->params.psd_source = SUSCAN_INSPECTOR_PSD_SOURCE_FAC;
  else if (gtk_toggle_button_get_active(
      GTK_TOGGLE_BUTTON(insp->cycloSpectrumRadioButton)))
    insp->params.psd_source = SUSCAN_INSPECTOR_PSD_SOURCE_NLN;
  else if (gtk_toggle_button_get_active(
      GTK_TOGGLE_BUTTON(insp->noSpectrumRadioButton)))
    insp->params.psd_source = SUSCAN_INSPECTOR_PSD_SOURCE_NONE;
  suscan_gui_inspector_update_sensitiveness(insp, &insp->params);

  SU_TRYCATCH(
      suscan_inspector_set_params_async(
          insp->gui->analyzer,
          insp->inshnd,
          &insp->params,
          rand()),
      return);
}

void
suscan_on_set_baudrate(GtkWidget *widget, gpointer data)
{
  struct suscan_gui_inspector *insp = (struct suscan_gui_inspector *) data;
  const gchar *text;
  SUFLOAT baud;

  text = gtk_entry_get_text(insp->baudRateEntry);

  if (sscanf(text, SUFLOAT_FMT, &baud) < 1) {
    suscan_error(
        insp->gui,
        "Set baudrate", "Invalid baudrate string `%s'",
        text);
  } else {
    insp->params.baud = baud +
        gtk_range_get_value(GTK_RANGE(insp->fineBaudScale));

    SU_TRYCATCH(
        suscan_inspector_set_params_async(
            insp->gui->analyzer,
            insp->inshnd,
            &insp->params,
            rand()),
        return);
  }
}

void
suscan_on_close_inspector_tab(GtkWidget *widget, gpointer data)
{
  struct suscan_gui_inspector *insp = (struct suscan_gui_inspector *) data;

  if (insp->inshnd != -1) {
    suscan_gui_inspector_disable(insp);

    /* Send close message */
    suscan_inspector_close_async(insp->gui->analyzer, insp->inshnd, rand());

    insp->inshnd = -1;
  }
}

gboolean
suscan_inspector_spectrum_on_configure_event(
    GtkWidget *widget,
    GdkEventConfigure *event,
    gpointer data)
{
  struct suscan_gui_inspector *insp = (struct suscan_gui_inspector *) data;

  suscan_gui_spectrum_configure(&insp->spectrum, widget);

  return TRUE;
}


gboolean
suscan_inspector_spectrum_on_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
  struct suscan_gui_inspector *insp = (struct suscan_gui_inspector *) data;

  suscan_gui_spectrum_redraw(&insp->spectrum, cr);

  return FALSE;
}

void
suscan_inspector_spectrum_on_scroll(GtkWidget *widget, GdkEventScroll *ev, gpointer data)
{
  struct suscan_gui_inspector *insp = (struct suscan_gui_inspector *) data;

  suscan_gui_spectrum_parse_scroll(&insp->spectrum, widget, ev);
}

void
suscan_inspector_spectrum_on_motion(GtkWidget *widget, GdkEventMotion *ev, gpointer data)
{
  struct suscan_gui_inspector *insp = (struct suscan_gui_inspector *) data;

  suscan_gui_spectrum_parse_motion(&insp->spectrum, widget, ev);
}

void
suscan_on_change_inspector_params_event(
    GtkWidget *widget,
    GdkEvent *event,
    gpointer data)
{
  suscan_on_change_inspector_params(widget, data);
}

