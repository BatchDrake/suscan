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
#include <assert.h>

#define SU_LOG_DOMAIN "async-thread"

#include "gui.h"

/* Asynchronous thread: take messages from analyzer and parse them */
struct suscan_gui_msg_envelope {
  suscan_gui_t *gui;
  uint32_t type;
  void *private;
};

void
suscan_gui_msg_envelope_destroy(struct suscan_gui_msg_envelope *data)
{
  suscan_analyzer_dispose_message(
      data->type,
      data->private);

  free(data);
}

struct suscan_gui_msg_envelope *
suscan_gui_msg_envelope_new(
    suscan_gui_t *gui,
    uint32_t type,
    void *private)
{
  struct suscan_gui_msg_envelope *new;

  SU_TRYCATCH(
      new = malloc(sizeof (struct suscan_gui_msg_envelope)),
      return NULL);

  new->gui = gui;
  new->private = private;
  new->type = type;

  return new;
}

/************************** Update GUI state *********************************/
void
suscan_gui_change_button_icon(GtkButton *button, const char *icon)
{
  GtkWidget *prev;
  GtkWidget *image;

  SU_TRYCATCH(
      image = gtk_image_new_from_icon_name(icon, GTK_ICON_SIZE_BUTTON),
      return);

  prev = gtk_bin_get_child(GTK_BIN(button));
  gtk_container_remove(GTK_CONTAINER(button), prev);
  gtk_widget_show(GTK_WIDGET(image));
  gtk_container_add(GTK_CONTAINER(button), image);
}

void
suscan_gui_update_state(suscan_gui_t *gui, enum suscan_gui_state state)
{
  const suscan_source_config_t *config = NULL;
  const char *source_name = "No source selected";
  const char *subtitle = NULL;

  if (gui->active_profile != NULL) {
    config = suscan_gui_profile_get_source_config(gui->active_profile);
    source_name = suscan_source_config_get_label(config);
  }

  switch (state) {
    case SUSCAN_GUI_STATE_STOPPED:
      subtitle = "Stopped";
      suscan_gui_change_button_icon(
          GTK_BUTTON(gui->toggleConnect),
          "media-playback-start-symbolic");
      gtk_widget_set_sensitive(GTK_WIDGET(gui->toggleConnect), TRUE);
      gtk_widget_set_sensitive(GTK_WIDGET(gui->preferencesButton), TRUE);
      gtk_widget_set_sensitive(
          GTK_WIDGET(gui->throttleOverrideCheckButton),
          FALSE);
      gtk_widget_set_sensitive(
          GTK_WIDGET(gui->throttleSampRateSpinButton),
          FALSE);
      gtk_label_set_text(gui->spectrumSampleRateLabel, "N/A");
      sugtk_spectrum_set_has_menu(gui->spectrum, FALSE);
      break;

    case SUSCAN_GUI_STATE_RUNNING:
      subtitle = "Running";
      suscan_gui_change_button_icon(
          GTK_BUTTON(gui->toggleConnect),
          "media-playback-stop-symbolic");
      gtk_widget_set_sensitive(GTK_WIDGET(gui->toggleConnect), TRUE);
      gtk_widget_set_sensitive(GTK_WIDGET(gui->preferencesButton), TRUE);
      gtk_widget_set_sensitive(
          GTK_WIDGET(gui->throttleOverrideCheckButton),
          !suscan_analyzer_is_real_time(gui->analyzer));
      gtk_widget_set_sensitive(
          GTK_WIDGET(gui->throttleSampRateSpinButton),
          FALSE);
      if (!suscan_analyzer_is_real_time(gui->analyzer))
        gtk_toggle_button_set_active(
            GTK_TOGGLE_BUTTON(gui->throttleOverrideCheckButton),
            FALSE);
      sugtk_spectrum_set_has_menu(gui->spectrum, TRUE);
      break;

    case SUSCAN_GUI_STATE_RESTARTING:
      subtitle = "Restarting...";
      gtk_widget_set_sensitive(GTK_WIDGET(gui->toggleConnect), FALSE);
      gtk_widget_set_sensitive(GTK_WIDGET(gui->preferencesButton), FALSE);
      gtk_widget_set_sensitive(
          GTK_WIDGET(gui->throttleOverrideCheckButton),
          FALSE);
      gtk_widget_set_sensitive(
          GTK_WIDGET(gui->throttleSampRateSpinButton),
          FALSE);
      sugtk_spectrum_set_has_menu(gui->spectrum, FALSE);
      suscan_gui_detach_all_inspectors(gui);
      break;

    case SUSCAN_GUI_STATE_STOPPING:
    case SUSCAN_GUI_STATE_QUITTING:
      subtitle = "Stopping...";
      suscan_gui_change_button_icon(
          GTK_BUTTON(gui->toggleConnect),
          "media-playback-start-symbolic");
      gtk_widget_set_sensitive(GTK_WIDGET(gui->toggleConnect), FALSE);
      gtk_widget_set_sensitive(GTK_WIDGET(gui->preferencesButton), FALSE);
      gtk_widget_set_sensitive(
          GTK_WIDGET(gui->throttleOverrideCheckButton),
          FALSE);
      gtk_widget_set_sensitive(
          GTK_WIDGET(gui->throttleSampRateSpinButton),
          FALSE);
      sugtk_spectrum_set_has_menu(gui->spectrum, FALSE);
      suscan_gui_detach_all_inspectors(gui);
      break;
  }

  gui->state = state;

  gtk_label_set_text(gui->subTitleLabel, subtitle);
  gtk_label_set_text(gui->titleLabel, source_name);
}

/************************** Async callbacks **********************************/
SUPRIVATE gboolean
suscan_async_stopped_cb(gpointer user_data)
{
  suscan_gui_t *gui = (suscan_gui_t *) user_data;
  unsigned int i;

  g_thread_join(gui->async_thread);
  gui->async_thread = NULL;

  /* Destroy all inspectors */
  for (i = 0; i < gui->inspector_count; ++i)
    if (gui->inspector_list[i] != NULL)
      gui->inspector_list[i]->inshnd = -1;

  /* Destroy analyzer object */
  suscan_analyzer_destroy(gui->analyzer);
  gui->analyzer = NULL;

  /* Consume any pending messages */
  suscan_analyzer_consume_mq(&gui->mq_out);

  switch (gui->state) {
    case SUSCAN_GUI_STATE_QUITTING:
      /*
       * Stopped was caused by a transition to QUITTING. Destroy GUI
       * and exit main loop
       */
      suscan_gui_store_settings(gui);
      suscan_gui_destroy(gui);
      gtk_main_quit();
      break;

    case SUSCAN_GUI_STATE_RESTARTING:
      /*
       * Analyzer has stopped because it was restarting with a different
       * configuration. We are ready to connect.
       */
      suscan_gui_connect(gui);
      break;

    default:
      /* Update GUI with new state */
      suscan_gui_update_state(gui, SUSCAN_GUI_STATE_STOPPED);
  }

  return G_SOURCE_REMOVE;
}

SUPRIVATE gboolean
suscan_async_read_error_cb(gpointer user_data)
{
  suscan_gui_t *gui = (suscan_gui_t *) user_data;

  suscan_error(
      gui,
      "Read error",
      "Capture stopped due to source read error (see log)");

  return suscan_async_stopped_cb(user_data);
}

SUPRIVATE gboolean
suscan_async_update_channels_cb(gpointer user_data)
{
  struct suscan_gui_msg_envelope *envelope;
  PTR_LIST(struct sigutils_channel, channel);
  SUFLOAT cpu;
  char cpu_str[10];
  unsigned int i;
  GtkTreeIter new_element;

  envelope = (struct suscan_gui_msg_envelope *) user_data;

  if (envelope->gui->state != SUSCAN_GUI_STATE_RUNNING)
    goto done;

  cpu = envelope->gui->analyzer->cpu_usage;

  snprintf(cpu_str, sizeof(cpu_str), "%.1lf%%", cpu * 100);

  gtk_label_set_text(envelope->gui->cpuLabel, cpu_str);
  gtk_level_bar_set_value(envelope->gui->cpuLevelBar, cpu);

  /* Move channel list to GUI */
  suscan_analyzer_channel_msg_take_channels(
      (struct suscan_analyzer_channel_msg *) envelope->private,
      &channel_list,
      &channel_count);
  sugtk_spectrum_update_channels(
      envelope->gui->spectrum,
      channel_list,
      channel_count);

  if (channel_count > SUSCAN_GUI_MAX_CHANNELS)
    channel_count = SUSCAN_GUI_MAX_CHANNELS;

  /* Update channel list */
  gtk_list_store_clear(envelope->gui->channelListStore);
  for (i = 0; i < channel_count; ++i) {
    gtk_list_store_append(
        envelope->gui->channelListStore,
        &new_element);
    gtk_list_store_set(
        envelope->gui->channelListStore,
        &new_element,
        0, channel_list[i]->fc,
        1, channel_list[i]->snr,
        2, channel_list[i]->S0,
        3, channel_list[i]->N0,
        4, channel_list[i]->bw,
        -1);
  }

done:
  suscan_gui_msg_envelope_destroy(envelope);

  return G_SOURCE_REMOVE;
}

void
sugtk_spectrum_update_from_psd_msg(
    SuGtkSpectrum *spectrum,
    struct suscan_analyzer_psd_msg *msg)
{
  sugtk_spectrum_update(
      spectrum,
      suscan_analyzer_psd_msg_take_psd(msg),
      msg->psd_size,
      msg->samp_rate,
      msg->fc,
      msg->N0);
}

SUPRIVATE gboolean
suscan_async_update_main_spectrum_cb(gpointer user_data)
{
  struct suscan_gui_msg_envelope *envelope;
  struct suscan_analyzer_psd_msg *msg;
  char text[32];
  static const char *units[] = {"sps", "ksps", "Msps"};
  SUFLOAT fs;
  unsigned int i;

  envelope = (struct suscan_gui_msg_envelope *) user_data;
  msg = (struct suscan_analyzer_psd_msg *) envelope->private;

  if (envelope->gui->state != SUSCAN_GUI_STATE_RUNNING)
    goto done;

  /*
   * TODO: Move this functions to something like
   * suscan_update_spectrum_labels
   */
  snprintf(text, sizeof(text), "%.1lf dBFS", SU_POWER_DB(msg->N0));

  gtk_label_set_text(envelope->gui->n0Label, text);
  gtk_level_bar_set_value(
      envelope->gui->n0LevelBar,
      1e-2 * (SU_POWER_DB(msg->N0) + 100));

  fs = msg->samp_rate;

  for (i = 0; i < 3 && fs > 1e3; ++i)
    fs *= 1e-3;

  if (i == 3)
    snprintf(text, sizeof(text), "ridiculous");
  else
    snprintf(text, sizeof(text), "%lg %s", fs, units[i]);

  gtk_label_set_text(envelope->gui->spectrumSampleRateLabel, text);

  sugtk_spectrum_update_from_psd_msg(
      envelope->gui->spectrum,
      msg);

done:
  suscan_gui_msg_envelope_destroy(envelope);

  return G_SOURCE_REMOVE;
}

SUPRIVATE gboolean
suscan_async_parse_sample_batch_msg(gpointer user_data)
{
  struct suscan_gui_msg_envelope *envelope;
  struct suscan_analyzer_sample_batch_msg *msg;
  suscan_gui_inspector_t *insp = NULL;

  envelope = (struct suscan_gui_msg_envelope *) user_data;
  msg = (struct suscan_analyzer_sample_batch_msg *) envelope->private;

  if (envelope->gui->state != SUSCAN_GUI_STATE_RUNNING)
    goto done;

  /* Sample batch messages may arrive out of order */
  insp = suscan_gui_get_inspector(envelope->gui, msg->inspector_id);
  if (insp == NULL)
    goto done;

  /* Append all these samples to the inspector GUI */
  SU_TRYCATCH(suscan_gui_inspector_feed_w_batch(insp, msg), goto done);

done:
  suscan_gui_msg_envelope_destroy(envelope);

  return G_SOURCE_REMOVE;
}

SUPRIVATE gboolean
suscan_async_parse_inspector_msg(gpointer user_data)
{
  struct suscan_gui_msg_envelope *envelope;
  struct suscan_analyzer_inspector_msg *msg;
  suscan_gui_inspector_t *new_insp = NULL;
  suscan_gui_inspector_t *insp = NULL;
  unsigned int i;
  char text[64];

  envelope = (struct suscan_gui_msg_envelope *) user_data;
  msg = (struct suscan_analyzer_inspector_msg *) envelope->private;

  if (envelope->gui->state != SUSCAN_GUI_STATE_RUNNING)
    goto done;

  /* Analyze inspector message type */
  switch (msg->kind) {
    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_OPEN:
      /* Create new inspector and append to tab */
      SU_TRYCATCH(
          new_insp = suscan_gui_inspector_new(
              msg->class,
              &msg->channel,
              msg->config,
              msg->handle),
          goto done);

      /* Apply current GUI settings */
      suscan_gui_apply_settings_on_inspector(envelope->gui, new_insp);

      /* Add available estimators */
      for (i = 0; i < msg->estimator_count; ++i)
        SU_TRYCATCH(
            suscan_gui_inspector_add_estimatorui(
                new_insp,
                msg->estimator_list[i],
                i),
            goto done);

      /* Add all spectrum sources */
      for (i = 0; i < msg->spectsrc_count; ++i)
        suscan_gui_inspector_add_spectrum_source(
            new_insp,
            msg->spectsrc_list[i],
            i + 1);

      SU_TRYCATCH(
          suscan_gui_add_inspector(
              envelope->gui,
              new_insp),
          goto done);

      /* This is rather delicate and should be rethinked. */
      SU_TRYCATCH(
          suscan_analyzer_set_inspector_id_async(
              envelope->gui->analyzer,
              msg->handle,
              new_insp->index,
              rand()),
          suscan_gui_remove_inspector(envelope->gui, new_insp);
          goto done);

      new_insp = NULL;

      break;

    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SET_ID:
      /* Simply check everything is as expected */
      SU_TRYCATCH(
          insp = suscan_gui_get_inspector(envelope->gui, msg->inspector_id),
          goto done);
      SU_TRYCATCH(insp->index == msg->inspector_id, goto done);
      break;

    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SET_CONFIG:
      SU_TRYCATCH(
          insp = suscan_gui_get_inspector(envelope->gui, msg->inspector_id),
          goto done);
      SU_TRYCATCH(
          suscan_gui_inspector_set_config(insp, msg->config),
          goto done);
      break;

    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_CLOSE:
      SU_TRYCATCH(
          insp = suscan_gui_get_inspector(envelope->gui, msg->inspector_id),
          goto done);
      SU_TRYCATCH(
          suscan_gui_remove_inspector(envelope->gui, insp),
          goto done);

      new_insp = insp; /* To be deleted at cleanup */

      break;

    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_ESTIMATOR:
      SU_TRYCATCH(
          insp = suscan_gui_get_inspector(envelope->gui, msg->inspector_id),
          goto done);

      SU_TRYCATCH (msg->estimator_id < insp->estimator_count, goto done);

      if (msg->enabled)
        suscan_gui_estimatorui_set_value(
            insp->estimator_list[msg->estimator_id],
            msg->value);

      break;

    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SPECTRUM:
      SU_TRYCATCH(
          insp = suscan_gui_get_inspector(envelope->gui, msg->inspector_id),
          goto done);

      if (msg->spectrum_size > 0)
        sugtk_spectrum_update(
            insp->spectrum,
            suscan_analyzer_inspector_msg_take_spectrum(msg),
            msg->spectrum_size,
            msg->samp_rate,
            msg->fc,
            msg->N0);
      break;

    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_RESET_EQUALIZER:
      /* Okay */
      break;

    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_HANDLE:
      suscan_error(
          envelope->gui,
          "Suscan inspector",
          "Invalid inspector handle passed");
      break;

    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_OBJECT:
      suscan_error(
          envelope->gui,
          "Suscan inspector",
          "Referred object inside inspector does not exist");
      break;

    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_KIND:
      suscan_error(
          envelope->gui,
          "Suscan inspector",
          "Invalid command passed to inspector");
      break;

    default:
      SU_WARNING("Ignored inspector message %d\n", msg->kind);
  }

done:
  if (new_insp != NULL)
    suscan_gui_inspector_destroy(new_insp);

  suscan_gui_msg_envelope_destroy(envelope);

  return G_SOURCE_REMOVE;
}

/*
 * Async thread: read messages from analyzer object and inject them to the
 * GUI's main loop
 */
SUPRIVATE gpointer
suscan_gui_async_thread(gpointer data)
{
  suscan_gui_t *gui = (suscan_gui_t *) data;
  struct suscan_gui_msg_envelope *envelope;
  void *private;
  uint32_t type;

  for (;;) {
    private = suscan_analyzer_read(gui->analyzer, &type);

    if (type == SUSCAN_WORKER_MSG_TYPE_HALT) {
      g_idle_add(suscan_async_stopped_cb, gui);
      goto done;
    } else if (gui->state == SUSCAN_GUI_STATE_RUNNING) {
      /*
       * We parse messages *only* if an analyzer is running and the
       * current GUI state is set to running.
       */
      switch (type) {
        case SUSCAN_ANALYZER_MESSAGE_TYPE_CHANNEL:
          if ((envelope = suscan_gui_msg_envelope_new(
              gui,
              type,
              private)) == NULL) {
            suscan_analyzer_dispose_message(type, private);
            break;
          }

          g_idle_add(suscan_async_update_channels_cb, envelope);
          break;

        case SUSCAN_ANALYZER_MESSAGE_TYPE_PSD:
          if ((envelope = suscan_gui_msg_envelope_new(
              gui,
              type,
              private)) == NULL) {
            suscan_analyzer_dispose_message(type, private);
            break;
          }

          g_idle_add(suscan_async_update_main_spectrum_cb, envelope);
          break;

        case SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR:
          if ((envelope = suscan_gui_msg_envelope_new(
              gui,
              type,
              private)) == NULL) {
            suscan_analyzer_dispose_message(type, private);
            break;
          }

          g_idle_add(suscan_async_parse_inspector_msg, envelope);
          break;

        case SUSCAN_ANALYZER_MESSAGE_TYPE_SAMPLES:
          if ((envelope = suscan_gui_msg_envelope_new(
              gui,
              type,
              private)) == NULL) {
            suscan_analyzer_dispose_message(type, private);
            break;
          }

          g_idle_add(suscan_async_parse_sample_batch_msg, envelope);
          break;

        case SUSCAN_ANALYZER_MESSAGE_TYPE_READ_ERROR: /* Read error */
          g_idle_add(suscan_async_read_error_cb, gui);
          goto done;

        case SUSCAN_ANALYZER_MESSAGE_TYPE_EOS: /* End of stream */
          g_idle_add(suscan_async_stopped_cb, gui);
          goto done;

        default:
          suscan_analyzer_dispose_message(type, private);
      }
    } else {
      /* Discard message */
      suscan_analyzer_dispose_message(type, private);
    }
  }

done:
  suscan_analyzer_dispose_message(type, private);

  return NULL;
}

SUBOOL
suscan_gui_start_async_thread(suscan_gui_t *gui)
{
  gui->async_thread = g_thread_new("async-task", suscan_gui_async_thread, gui);

  return gui->async_thread != NULL;
}
