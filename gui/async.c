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
#include "gui.h"

/* Asynchronous thread: take messages from analyzer and parse them */
struct suscan_channel_update_data {
  struct suscan_gui *gui;
  struct suscan_analyzer_channel_msg *msg;
  SUFLOAT cpu;
};

void
suscan_channel_update_data_destroy(struct suscan_channel_update_data *data)
{
  suscan_analyzer_dispose_message(
      SUSCAN_ANALYZER_MESSAGE_TYPE_CHANNEL,
      data->msg);

  free(data);
}

struct suscan_channel_update_data *
suscan_channel_update_data_new(
    struct suscan_gui *gui,
    struct suscan_analyzer_channel_msg *msg)
{
  struct suscan_channel_update_data *data;

  SU_TRYCATCH(
      data = malloc(sizeof (struct suscan_channel_update_data)),
      return NULL);

  data->gui = gui;
  data->msg = msg;

  return data;
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
suscan_gui_update_state(struct suscan_gui *gui, enum suscan_gui_state state)
{
  const char *source_name = "No source selected";
  char *subtitle = NULL;

  if (gui->selected_config != NULL)
    source_name = gui->selected_config->source->desc;

  switch (state) {
    case SUSCAN_GUI_STATE_STOPPED:
      subtitle = strbuild("%s (Stopped)", source_name);
      suscan_gui_change_button_icon(
          GTK_BUTTON(gui->toggleConnect),
          "media-playback-start-symbolic");
      gtk_widget_set_sensitive(GTK_WIDGET(gui->toggleConnect), TRUE);
      gtk_widget_set_sensitive(GTK_WIDGET(gui->preferencesButton), TRUE);
      break;

    case SUSCAN_GUI_STATE_RUNNING:
      subtitle = strbuild("%s (Running)", source_name);
      suscan_gui_change_button_icon(
          GTK_BUTTON(gui->toggleConnect),
          "media-playback-stop-symbolic");
      gtk_widget_set_sensitive(GTK_WIDGET(gui->toggleConnect), TRUE);
      gtk_widget_set_sensitive(GTK_WIDGET(gui->preferencesButton), FALSE);
      break;

    case SUSCAN_GUI_STATE_STOPPING:
      subtitle = strbuild("%s (Stopping...)", source_name);
      suscan_gui_change_button_icon(
          GTK_BUTTON(gui->toggleConnect),
          "media-playback-start-symbolic");
      gtk_widget_set_sensitive(GTK_WIDGET(gui->toggleConnect), FALSE);
      gtk_widget_set_sensitive(GTK_WIDGET(gui->preferencesButton), FALSE);
      break;
  }

  gui->state = state;

  SU_TRYCATCH(subtitle != NULL, return);

  gtk_header_bar_set_subtitle(gui->headerBar, subtitle);

  free(subtitle);
}

/************************** Async callbacks **********************************/
SUPRIVATE gboolean
suscan_async_stopped_cb(gpointer user_data)
{
  struct suscan_gui *gui = (struct suscan_gui *) user_data;

  g_thread_join(gui->async_thread);
  gui->async_thread = NULL;

  /* Destroy analyzer object */
  suscan_analyzer_destroy(gui->analyzer);
  gui->analyzer = NULL;

  /* Consume any pending messages */
  suscan_analyzer_consume_mq(&gui->mq_out);

  /* Update GUI with new state */
  suscan_gui_update_state(gui, SUSCAN_GUI_STATE_STOPPED);

  return G_SOURCE_REMOVE;
}

SUPRIVATE gboolean
suscan_async_update_channels_cb(gpointer user_data)
{
  struct suscan_channel_update_data *data;
  char cpu[10];
  unsigned int i;
  GtkTreeIter new_element;

  if (user_data != NULL) {
    data = (struct suscan_channel_update_data *) user_data;

    snprintf(cpu, sizeof(cpu), "%.1lf%%", data->cpu * 100);

    gtk_label_set_text(data->gui->cpuLabel, cpu);
    gtk_level_bar_set_value(data->gui->cpuLevelBar, data->cpu);

    /* Update channel list */
    gtk_list_store_clear(data->gui->channelListStore);
    for (i = 0; i < data->msg->channel_count; ++i) {
      gtk_list_store_append(
          data->gui->channelListStore,
          &new_element);
      gtk_list_store_set(
          data->gui->channelListStore,
          &new_element,
          0, data->msg->channel_list[i]->fc,
          1, data->msg->channel_list[i]->snr,
          2, data->msg->channel_list[i]->S0,
          3, data->msg->channel_list[i]->N0,
          4, data->msg->channel_list[i]->bw,
          -1);
    }

    suscan_channel_update_data_destroy(data);
  }

  return G_SOURCE_REMOVE;
}

SUPRIVATE gpointer
suscan_gui_async_thread(gpointer data)
{
  struct suscan_gui *gui = (struct suscan_gui *) data;
  struct suscan_channel_update_data *chdata;
  void *private;
  uint32_t type;

  for (;;) {
    private = suscan_analyzer_read(gui->analyzer, &type);

    switch (type) {
      case SUSCAN_WORKER_MSG_TYPE_HALT: /* Halt response */
        g_idle_add(suscan_async_stopped_cb, gui);
        /* This message is empty */
        goto done;

      case SUSCAN_ANALYZER_MESSAGE_TYPE_CHANNEL:
        if ((chdata = suscan_channel_update_data_new(gui, private)) == NULL) {
          suscan_analyzer_dispose_message(type, private);
          break;
        }

        chdata->cpu = gui->analyzer->cpu_usage;

        g_idle_add(suscan_async_update_channels_cb, chdata);
        break;

      case SUSCAN_ANALYZER_MESSAGE_TYPE_EOS: /* End of stream */
        g_idle_add(suscan_async_stopped_cb, gui);
        suscan_analyzer_dispose_message(type, private);
        goto done;

      default:
        g_print("Unknown message %d\n", type);
        suscan_analyzer_dispose_message(type, private);
    }
  }

done:
  return NULL;
}

/************************** GUI Thread functions *****************************/
SUBOOL
suscan_gui_connect(struct suscan_gui *gui)
{
  assert(gui->state == SUSCAN_GUI_STATE_STOPPED);
  assert(gui->analyzer == NULL);
  assert(gui->selected_config != NULL);

  if ((gui->analyzer = suscan_analyzer_new(
      gui->selected_config->config,
      &gui->mq_out)) == NULL)
    return SU_FALSE;

  /* Analyzer created, create async thread */
  SU_TRYCATCH(
      gui->async_thread = g_thread_new(
          "async-task",
          suscan_gui_async_thread,
          gui),
      goto fail);

  /* Change state and succeed */
  suscan_gui_update_state(gui, SUSCAN_GUI_STATE_RUNNING);

  return SU_TRUE;

fail:
  if (gui->analyzer != NULL) {
    suscan_analyzer_destroy(gui->analyzer);
    gui->analyzer = NULL;

    suscan_analyzer_consume_mq(&gui->mq_out);
  }

  return SU_FALSE;
}

void
suscan_gui_disconnect(struct suscan_gui *gui)
{
  assert(gui->state == SUSCAN_GUI_STATE_RUNNING);
  assert(gui->analyzer != NULL);

  suscan_gui_update_state(gui, SUSCAN_GUI_STATE_STOPPING);

  suscan_analyzer_req_halt(gui->analyzer);
}


