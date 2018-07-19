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

#define SU_LOG_DOMAIN "gui-profile"

#include "profile.h"

SUPRIVATE void
suscan_gui_gain_slider_destroy(struct suscan_gui_gain_slider *slider)
{
  if (slider->dbLabel != NULL)
    g_object_unref(G_OBJECT(slider->dbLabel));

  if (slider->nameLabel != NULL)
    g_object_unref(G_OBJECT(slider->nameLabel));

  if (slider->gainAdjustment)
    g_object_unref(G_OBJECT(slider->gainAdjustment));

  if (slider->gainScale != NULL)
    g_object_unref(G_OBJECT(slider->gainScale));

  free(slider);
}

SUPRIVATE struct suscan_gui_gain_slider *
suscan_gui_gain_slider_new(const struct suscan_source_gain_desc *desc)
{
  struct suscan_gui_gain_slider *new = NULL;

  SU_TRYCATCH(
      new = calloc(1, sizeof (struct suscan_gui_gain_slider)),
      goto fail);

  new->desc = desc;
  new->dbLabel = GTK_LABEL(gtk_label_new("dB"));
  new->nameLabel = GTK_LABEL(gtk_label_new(desc->name));
  new->gainAdjustment = GTK_ADJUSTMENT(gtk_adjustment_new(
      desc->def,
      desc->min,
      desc->max,
      desc->step,
      1,
      desc->step * 10));
  new->gainScale = GTK_SCALE(gtk_scale_new(
      GTK_ORIENTATION_HORIZONTAL,
      new->gainAdjustment));

  gtk_range_set_value(GTK_RANGE(new->gainScale), desc->def);

  gtk_widget_set_hexpand(GTK_WIDGET(new->gainScale), TRUE);
  gtk_widget_set_margin_start(GTK_WIDGET(new->dbLabel), 3);
  gtk_widget_set_margin_end(GTK_WIDGET(new->dbLabel), 3);

  gtk_widget_set_margin_start(GTK_WIDGET(new->nameLabel), 3);
  gtk_widget_set_margin_end(GTK_WIDGET(new->nameLabel), 3);

  gtk_widget_set_margin_start(GTK_WIDGET(new->gainScale), 3);
  gtk_widget_set_margin_end(GTK_WIDGET(new->gainScale), 3);

  gtk_widget_show(GTK_WIDGET(new->dbLabel));
  gtk_widget_show(GTK_WIDGET(new->nameLabel));
  gtk_widget_show(GTK_WIDGET(new->gainScale));

  /* This is intentional. Calling _sink acquires ownership */
  g_object_ref_sink(G_OBJECT(new->dbLabel));
  g_object_ref_sink(G_OBJECT(new->nameLabel));
  g_object_ref_sink(G_OBJECT(new->gainAdjustment));
  g_object_ref_sink(G_OBJECT(new->gainScale));

  return new;

fail:
  if (new != NULL)
    suscan_gui_gain_slider_destroy(new);

  return NULL;
}

void
suscan_gui_gain_ui_destroy(struct suscan_gui_gain_ui *ui)
{
  unsigned int i;

  for (i = 0; i < ui->gain_slider_count; ++i)
    if (ui->gain_slider_list[i] != NULL)
      suscan_gui_gain_slider_destroy(ui->gain_slider_list[i]);

  if (ui->gain_slider_list != NULL)
    free(ui->gain_slider_list);

  if (ui->uiGrid != NULL)
    g_object_unref(G_OBJECT(ui->uiGrid));

  free(ui);
}

struct suscan_gui_gain_ui *
suscan_gui_gain_ui_new(const suscan_source_device_t *device)
{
  struct suscan_gui_gain_ui *new = NULL;
  struct suscan_gui_gain_slider *slider = NULL;
  struct suscan_gui_gain_slider *borrowed_slider = NULL;
  struct suscan_source_device_info info =
      suscan_source_device_info_INITIALIZER;
  unsigned int i;

  SU_TRYCATCH(new = calloc(1, sizeof (struct suscan_gui_gain_ui)), goto fail);

  new->device = device;
  new->uiGrid = GTK_GRID(gtk_grid_new());
  g_object_ref_sink(G_OBJECT(new->uiGrid));

  /* This only makes sense in this particular case */
  if (suscan_source_device_get_info(device, 0, &info)) {
    for (i = 0; i < info.gain_desc_count; ++i) {
      SU_TRYCATCH(
          slider = suscan_gui_gain_slider_new(info.gain_desc_list[i]),
          goto fail);
      SU_TRYCATCH(
          PTR_LIST_APPEND_CHECK(new->gain_slider, slider) != -1,
          goto fail);
      borrowed_slider = slider;
      slider = NULL;

      gtk_grid_attach(
          new->uiGrid,
          GTK_WIDGET(borrowed_slider->nameLabel),
          0, /* Left */
          i, /* Top */
          1, /* Width */
          1 /* Height */);

      gtk_grid_attach(
          new->uiGrid,
          GTK_WIDGET(borrowed_slider->gainScale),
          1, /* Left */
          i, /* Top */
          1, /* Width */
          1 /* Height */);

      gtk_grid_attach(
          new->uiGrid,
          GTK_WIDGET(borrowed_slider->dbLabel),
          2, /* Left */
          i, /* Top */
          1, /* Width */
          1 /* Height */);
    }
  }

  gtk_widget_show(GTK_WIDGET(new->uiGrid));

  return new;

fail:
  if (slider != NULL)
    suscan_gui_gain_slider_destroy(slider);

  if (new != NULL)
    suscan_gui_gain_ui_destroy(new);

  return NULL;
}

void
suscan_gui_gain_ui_set_profile(
    struct suscan_gui_gain_ui *ui,
    suscan_gui_profile_t *profile)
{
  unsigned int i;

  ui->profile = profile;

  for (i = 0; i < ui->gain_slider_count; ++i)
    g_signal_connect(
        ui->gain_slider_list[i]->gainScale,
        "value-changed",
        (GCallback) suscan_gui_profile_on_changed,
        profile);
}

SUBOOL
suscan_gui_gain_ui_walk_gains(
    const struct suscan_gui_gain_ui *ui,
    SUBOOL (*gain_cb) (void *private, const char *name, SUFLOAT value),
    void *private)
{
  unsigned int i;

  for (i = 0; i < ui->gain_slider_count; ++i)
    if (!(gain_cb) (
        private,
        ui->gain_slider_list[i]->desc->name,
        gtk_range_get_value(GTK_RANGE(ui->gain_slider_list[i]->gainScale))))
        return SU_FALSE;

  return SU_TRUE;
}

SUBOOL
suscan_gui_gain_ui_set_gain(
    const struct suscan_gui_gain_ui *ui,
    const char *name,
    SUFLOAT value)
{
  unsigned int i;

  for (i = 0; i < ui->gain_slider_count; ++i)
    if (strcmp(ui->gain_slider_list[i]->desc->name, name) == 0) {
      gtk_range_set_value(GTK_RANGE(ui->gain_slider_list[i]->gainScale), value);
      return SU_TRUE;
    }

  /* Not found, just fail */
  return SU_FALSE;
}

