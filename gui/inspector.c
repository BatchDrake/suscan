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
#include <time.h>
#include <string.h>

#define SU_LOG_DOMAIN "inspector-gui"

void
suscan_gui_inspector_destroy(struct suscan_gui_inspector *inspector)
{
  if (inspector->inshnd != -1)
    suscan_analyzer_close_async(
        inspector->gui->analyzer,
        inspector->inshnd,
        rand());

  if (inspector->channelInspectorGrid != NULL)
    gtk_widget_destroy(GTK_WIDGET(inspector->channelInspectorGrid));

  if (inspector->pageLabelEventBox != NULL)
    gtk_widget_destroy(GTK_WIDGET(inspector->pageLabelEventBox));

  suscan_gui_spectrum_init(&inspector->spectrum);

  g_object_unref(G_OBJECT(inspector->builder));

  if (inspector->symbolView != NULL)
    g_object_unref(G_OBJECT(inspector->symbolView));

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

  gtk_widget_set_sensitive(
      GTK_WIDGET(insp->gainManualAlignment),
      gtk_toggle_button_get_active(
          GTK_TOGGLE_BUTTON(insp->manualGainRadioButton)));

  gtk_widget_set_sensitive(GTK_WIDGET(insp->baudRateEntry), TRUE);
  gtk_widget_set_sensitive(GTK_WIDGET(insp->setBaudRateButton), TRUE);
  gtk_widget_set_sensitive(GTK_WIDGET(insp->detectBaudRateFACButton), TRUE);
  gtk_widget_set_sensitive(GTK_WIDGET(insp->detectBaudRateNLNButton), TRUE);

  /* TODO: setup some values according to params */

  return SU_TRUE;
}

/* Just marks it as detached: it doesn not refer to any existing inspector */
void
suscan_gui_inspector_detach(struct suscan_gui_inspector *insp)
{
  insp->dead = SU_TRUE;
  insp->inshnd = -1;
  gtk_widget_set_sensitive(GTK_WIDGET(insp->channelInspectorGrid), FALSE);
}

/* Sends a close signal to the analyzer */
void
suscan_gui_inspector_close(struct suscan_gui_inspector *insp)
{
  SUHANDLE handle = insp->inshnd;

  if (handle != -1) {
    /* Send close message */
    insp->inshnd = -1;
    suscan_analyzer_close_async(insp->gui->analyzer, handle, rand());
  }

  gtk_widget_set_sensitive(GTK_WIDGET(insp->channelInspectorGrid), FALSE);
}

SUSYMBOL
suscan_gui_inspector_decide(
    const struct suscan_gui_inspector *inspector,
    SUCOMPLEX sample)
{
  SUFLOAT arg = SU_C_ARG(sample);
  char sym_ndx;

  switch (inspector->params.fc_ctrl) {
    case SUSCAN_INSPECTOR_CARRIER_CONTROL_COSTAS_2:
      /* BPSK decision */
      sym_ndx = arg > 0;
      break;

    case SUSCAN_INSPECTOR_CARRIER_CONTROL_COSTAS_4:
      /* QPSK decision */
      if (0 < arg && arg <= .5 * M_PI)
        sym_ndx = 0;
      else if (.5 * M_PI < arg && arg <= M_PI)
        sym_ndx = 1;
      else if (-M_PI < arg && arg <= -.5 * M_PI)
        sym_ndx = 2;
      else
        sym_ndx = 3;
      break;

    case SUSCAN_INSPECTOR_CARRIER_CONTROL_COSTAS_8:
      /* 8PSK decision */
      if (0 < arg && arg <= .25 * M_PI)
        sym_ndx = 0;
      else if (.25 * M_PI < arg && arg <= .5 * M_PI)
        sym_ndx = 1;
      else if (.5 * M_PI < arg && arg <= .75 * M_PI)
        sym_ndx = 2;
      else if (.75 * M_PI < arg && arg <= M_PI)
        sym_ndx = 3;
      else if (-M_PI < arg && arg <= -.75 * M_PI)
        sym_ndx = 4;
      else if (-.75 * M_PI < arg && arg <= -.5 * M_PI)
        sym_ndx = 5;
      else if (-.5 * M_PI < arg && arg <= -.25 * M_PI)
        sym_ndx = 6;
      else
        sym_ndx = 7;
      break;

    default:
      return SU_NOSYMBOL;
  }

  return '0' + sym_ndx;
}

void
suscan_gui_inspector_feed_w_batch(
    struct suscan_gui_inspector *inspector,
    const struct suscan_analyzer_sample_batch_msg *msg)
{
  unsigned int sample_count, full_samp_count;
  unsigned int i;
  GtkTextIter iter;
  char *new_buffer;
  char sym;

  /*
   * Push, at most, the last SUSCAN_GUI_CONSTELLATION_HISTORY. We do this
   * because the previous ones will never be shown
   */
  full_samp_count = msg->sample_count;
  sample_count = MIN(full_samp_count, SUSCAN_GUI_CONSTELLATION_HISTORY);

  /* Check if recording is enabled to assert the symbol buffer */
  if (inspector->recording)
    for (i = 0; i < full_samp_count; ++i)
      if ((sym = suscan_gui_inspector_decide(inspector, msg->samples[i]))
          != SU_NOSYMBOL) /* Append text to buffer */
        sugtk_sym_view_append(inspector->symbolView, 0xff * (sym - '0'));

  for (i = 0; i < sample_count; ++i)
    suscan_gui_constellation_push_sample(
        &inspector->constellation,
        msg->samples[msg->sample_count - sample_count + i]);

}

char *
suscan_gui_inspector_to_filename(
    const struct suscan_gui_inspector *inspector,
    const char *prefix,
    const char *suffix)
{
  time_t now;
  struct tm *tm;
  const char *demod;

  time(&now);
  tm = localtime(&now);

  switch (inspector->params.fc_ctrl) {
    case SUSCAN_INSPECTOR_CARRIER_CONTROL_COSTAS_2:
      demod = "bpsk";
      break;

    case SUSCAN_INSPECTOR_CARRIER_CONTROL_COSTAS_4:
      demod = "qpsk";
      break;

    default:
      demod = "manual";
  }

  return strbuild(
      "%s%+lldHz-%s-%ubaud-%02d%02d%02d-%02d%02d%04d%s",
      prefix,
      (long long int) round(inspector->channel.fc),
      demod,
      (unsigned int) round(inspector->params.baud),
      tm->tm_hour,
      tm->tm_min,
      tm->tm_sec,
      tm->tm_mday,
      tm->tm_mon,
      tm->tm_year + 1900,
      suffix);

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
      inspector->costas8RadioButton =
          GTK_RADIO_BUTTON(gtk_builder_get_object(
              inspector->builder,
              "rbCostas8")),
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
              "rbGainControlManual")),
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

  SU_TRYCATCH(
      inspector->recorderGrid =
          GTK_GRID(gtk_builder_get_object(
              inspector->builder,
              "grRecorder")),
          return SU_FALSE);

  SU_TRYCATCH(
      inspector->autoScrollToggleButton =
          GTK_TOGGLE_BUTTON(gtk_builder_get_object(
              inspector->builder,
              "tbAutoScroll")),
          return SU_FALSE);

  inspector->symbolView = SUGTK_SYM_VIEW(sugtk_sym_view_new());

  gtk_grid_attach(
      inspector->recorderGrid,
      GTK_WIDGET(inspector->symbolView),
      0, /* left */
      1, /* top */
      1, /* width */
      1 /* height */);

  gtk_widget_set_hexpand(GTK_WIDGET(inspector->symbolView), TRUE);
  gtk_widget_set_vexpand(GTK_WIDGET(inspector->symbolView), TRUE);

  gtk_widget_show(GTK_WIDGET(inspector->symbolView));

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

  gtk_toggle_button_set_active(
      GTK_TOGGLE_BUTTON(inspector->autoScrollToggleButton),
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

  suscan_analyzer_get_info_async(insp->gui->analyzer, insp->inshnd, 0);
}

void
suscan_on_get_baudrate_nln(GtkWidget *widget, gpointer data)
{
  struct suscan_gui_inspector *insp = (struct suscan_gui_inspector *) data;

  suscan_analyzer_get_info_async(insp->gui->analyzer, insp->inshnd, 1);
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
  else if (gtk_toggle_button_get_active(
      GTK_TOGGLE_BUTTON(insp->costas8RadioButton)))
    insp->params.fc_ctrl = SUSCAN_INSPECTOR_CARRIER_CONTROL_COSTAS_8;
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
      suscan_analyzer_set_inspector_params_async(
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
        suscan_analyzer_set_inspector_params_async(
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
    suscan_gui_remove_inspector(insp->gui, insp);

    suscan_gui_inspector_destroy(insp);
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
suscan_inspector_spectrum_on_draw(
    GtkWidget *widget,
    cairo_t *cr,
    gpointer data)
{
  struct suscan_gui_inspector *insp = (struct suscan_gui_inspector *) data;

  suscan_gui_spectrum_redraw(&insp->spectrum, cr);

  return FALSE;
}

void
suscan_inspector_spectrum_on_scroll(
    GtkWidget *widget,
    GdkEventScroll *ev,
    gpointer data)
{
  struct suscan_gui_inspector *insp = (struct suscan_gui_inspector *) data;

  suscan_gui_spectrum_parse_scroll(&insp->spectrum, ev);
}

void
suscan_inspector_spectrum_on_motion(
    GtkWidget *widget,
    GdkEventMotion *ev,
    gpointer data)
{
  struct suscan_gui_inspector *insp = (struct suscan_gui_inspector *) data;

  suscan_gui_spectrum_parse_motion(&insp->spectrum, ev);
}

void
suscan_on_change_inspector_params_event(
    GtkWidget *widget,
    GdkEvent *event,
    gpointer data)
{
  suscan_on_change_inspector_params(widget, data);
}

void
suscan_inspector_on_save(
    GtkWidget *widget,
    gpointer data)
{
  struct suscan_gui_inspector *insp = (struct suscan_gui_inspector *) data;
  GtkWidget *dialog;
  GtkFileChooser *chooser;
  gint res;
  char *new_fname = NULL;
  gchar *filename = NULL;
  gchar *text = NULL;
  GtkTextIter istart, iend;
  FILE *fp = NULL;

  SU_TRYCATCH(
      new_fname = suscan_gui_inspector_to_filename(insp, "symbols", ".log"),
      goto done);

  SU_TRYCATCH(
      dialog = gtk_file_chooser_dialog_new(
          "Save symbol record",
          insp->gui->main,
          GTK_FILE_CHOOSER_ACTION_SAVE,
          "_Cancel",
          GTK_RESPONSE_CANCEL,
          "_Save",
          GTK_RESPONSE_ACCEPT,
          NULL),
      goto done);

  chooser = GTK_FILE_CHOOSER(dialog);

  gtk_file_chooser_set_do_overwrite_confirmation(chooser, TRUE);

  gtk_file_chooser_set_current_name(chooser, new_fname);

  if (gtk_dialog_run(GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT) {
    SU_TRYCATCH(
        filename = gtk_file_chooser_get_filename (chooser),
        goto done);
    if ((fp = fopen(filename, "wb")) == NULL) {
      suscan_error(
          insp->gui,
          "Save failed",
          "Cannot save symbols to file: %s",
          strerror(errno));
      goto done;
    }

    suscan_error(insp->gui, "Save failed", "Not implemented!");

/*
    if (fwrite(text, strlen(text), 1, fp) < 1) {
      suscan_error(
          insp->gui,
          "Write failed",
          "Failed to write symbol recording to disk: %s",
          strerror(errno));
      goto done;
    }
    */
  }

done:
  if (text != NULL)
    g_free(text);

  if (fp != NULL)
    fclose(fp);

  if (filename != NULL)
    g_free (filename);

  if (dialog != NULL)
    gtk_widget_destroy(dialog);

  if (new_fname != NULL)
    free(new_fname);
}

void
suscan_inspector_on_toggle_record(
    GtkWidget *widget,
    gpointer data)
{
  struct suscan_gui_inspector *insp = (struct suscan_gui_inspector *) data;

  insp->recording = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

void
suscan_inspector_on_clear(
    GtkWidget *widget,
    gpointer data)
{
  struct suscan_gui_inspector *insp = (struct suscan_gui_inspector *) data;

  sugtk_sym_view_clear(insp->symbolView);
}

void
suscan_inspector_on_toggle_autoscroll(
    GtkWidget *widget,
    gpointer data)
{
  struct suscan_gui_inspector *insp = (struct suscan_gui_inspector *) data;

  sugtk_sym_view_set_autoscroll(
      insp->symbolView,
      gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)));
}
