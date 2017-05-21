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

  /* Somehow Glade fails to set these default values */
  gtk_toggle_button_set_active(
      GTK_TOGGLE_BUTTON(inspector->manualRadioButton),
      TRUE);

  gtk_toggle_button_set_active(
      GTK_TOGGLE_BUTTON(inspector->clockManualRadioButton),
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

void
suscan_on_change_inspector_params(GtkWidget *widget, gpointer data)
{
  struct suscan_gui_inspector *insp = (struct suscan_gui_inspector *) data;
  const gchar *text;
  SUFLOAT freq;
  SUFLOAT baud;
  SUFLOAT beta;

  text = gtk_entry_get_text(insp->carrierOffsetEntry);
  if (sscanf(text, SUFLOAT_FMT, &freq) < 1) {
    suscan_error(
        insp->gui,
        "Set carrier offset", "Invalid carrier offset string `%s'",
        text);
    return;
  }

  text = gtk_entry_get_text(insp->baudRateEntry);
  if (sscanf(text, SUFLOAT_FMT, &baud) < 1) {
    suscan_error(
        insp->gui,
        "Set baudrate", "Invalid baudrate string `%s'",
        text);
    return;
  }

  text = gtk_entry_get_text(insp->gardnerBetaEntry);
  if (sscanf(text, SUFLOAT_FMT, &beta) < 1) {
    suscan_error(
        insp->gui,
        "Frequency correction", "Invalid loop gain `%s'",
        text);
    return;
  }

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
