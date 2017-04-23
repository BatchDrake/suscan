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

#include <sigutils/sampling.h>

void
suscan_gui_spectrum_clear(struct suscan_gui_spectrum *spectrum)
{
  cairo_t *cr;

  cr = cairo_create(spectrum->surface);

  cairo_set_source_rgb(cr, 0, 0, 0);

  cairo_paint(cr);

  cairo_destroy(cr);
}

void
suscan_gui_spectrum_configure(
    struct suscan_gui_spectrum *spectrum,
    GtkWidget *widget)
{
  if (spectrum->surface != NULL)
    cairo_surface_destroy(spectrum->surface);

  spectrum->width = gtk_widget_get_allocated_width(widget);
  spectrum->height = gtk_widget_get_allocated_height(widget);

  spectrum->surface = gdk_window_create_similar_surface(
      gtk_widget_get_window(widget),
      CAIRO_CONTENT_COLOR,
      spectrum->width,
      spectrum->height);

  suscan_gui_spectrum_clear(spectrum);
}

void
suscan_gui_spectrum_update(
    struct suscan_gui_spectrum *spectrum,
    struct suscan_analyzer_psd_msg *msg)
{
  if (spectrum->psd_data != NULL)
    free(spectrum->psd_data);

  spectrum->fc        = msg->fc;
  spectrum->psd_data  = suscan_analyzer_psd_msg_take_psd(msg);
  spectrum->psd_size  = msg->psd_size;
  spectrum->samp_rate = msg->samp_rate;
  spectrum->N0        = msg->N0;
}

void
suscan_gui_spectrum_draw_levels(
    struct suscan_gui_spectrum *spectrum,
    cairo_t *cr)
{
  int i;
  int last_end;
  SUFLOAT x, xscr;

  char text[20];
  cairo_text_extents_t extents;

  cairo_select_font_face(
      cr,
      "Inconsolata",
      CAIRO_FONT_SLANT_NORMAL,
      CAIRO_FONT_WEIGHT_NORMAL);

  cairo_set_source_rgb(cr, 1, 1, 1);

  for (i = 1; i < SUSCAN_GUI_VERTICAL_DIVS; ++i) {
    snprintf(text, sizeof(text), "-%d", (int) round(i * spectrum->db_per_div));

    cairo_move_to(
        cr,
        7.5,
        SUSCAN_SPECTRUM_TO_SCR_Y(spectrum, i * SUSCAN_GUI_SPECTRUM_DY));

    cairo_show_text(
        cr,
        text);
  }

  if (spectrum->samp_rate > 0) {
    last_end = 0;

    for (
        i = -SUSCAN_GUI_HORIZONTAL_DIVS / 2 + 1;
        i < SUSCAN_GUI_HORIZONTAL_DIVS / 2;
        ++i) {
      x = i / (SUFLOAT) SUSCAN_GUI_HORIZONTAL_DIVS;
      xscr = SUSCAN_SPECTRUM_TO_SCR_X(spectrum, x + .5);
      snprintf(
          text,
          sizeof(text),
          "%lli",
          (int64_t) (
              spectrum->fc
              + (int) round(SU_NORM2ABS_FREQ(spectrum->samp_rate, 2 * x))));

      cairo_text_extents(cr, text, &extents);

      if (xscr - extents.width / 2 > last_end) {
        cairo_move_to(
            cr,
            xscr - extents.width / 2,
            spectrum->height - SUSCAN_GUI_SPECTRUM_BOTTOM_PADDING + 10);

        cairo_show_text(
            cr,
            text);

        last_end = xscr + extents.width / 2;
      }
    }
  }
}

void
suscan_gui_spectrum_redraw(
    struct suscan_gui_spectrum *spectrum,
    cairo_t *cr)
{
  static const double axis_pattern[] = {5.0, 5.0};
  int i;
  SUFLOAT x, x_prev;
  SUFLOAT hy;

  cairo_set_source_surface(cr, spectrum->surface, 0, 0);

  /* Paint in black */
  cairo_paint(cr);

  cairo_set_line_width(cr, 1);

  suscan_gui_spectrum_draw_levels(spectrum, cr);

  /* Draw axes */
  cairo_set_dash(cr, axis_pattern, 2, 0);

  for (
      i = -SUSCAN_GUI_HORIZONTAL_DIVS / 2 + 1;
      i < SUSCAN_GUI_HORIZONTAL_DIVS / 2;
      ++i) {

    if (i == 0)
      cairo_set_source_rgb(cr, 1, 1, 1);
    else
      cairo_set_source_rgb(cr, 0, 0.5, 0);

    cairo_move_to(
        cr,
        SUSCAN_SPECTRUM_TO_SCR(spectrum, .5 + i * SUSCAN_GUI_SPECTRUM_DX, 0));
    cairo_line_to(
        cr,
        SUSCAN_SPECTRUM_TO_SCR(spectrum, .5 + i * SUSCAN_GUI_SPECTRUM_DX, 1));

    cairo_stroke(cr);
  }

  for (i = 1; i < SUSCAN_GUI_VERTICAL_DIVS; ++i) {
    cairo_move_to(
        cr,
        SUSCAN_SPECTRUM_TO_SCR(spectrum, 0, i * SUSCAN_GUI_SPECTRUM_DY));
    cairo_line_to(
        cr,
        SUSCAN_SPECTRUM_TO_SCR(spectrum, 1, i * SUSCAN_GUI_SPECTRUM_DY));

    cairo_stroke(cr);
  }

  /* Draw border */
  cairo_set_dash(cr, NULL, 0, 0);
  cairo_move_to(
      cr,
      SUSCAN_SPECTRUM_TO_SCR(spectrum, 0, 0));
  cairo_line_to(
      cr,
      SUSCAN_SPECTRUM_TO_SCR(spectrum, 0, 1));
  cairo_stroke(cr);

  cairo_move_to(
      cr,
      SUSCAN_SPECTRUM_TO_SCR(spectrum, 0, 0));

  cairo_line_to(
      cr,
      SUSCAN_SPECTRUM_TO_SCR(spectrum, 1, 0));
  cairo_stroke(cr);

  cairo_move_to(
      cr,
      SUSCAN_SPECTRUM_TO_SCR(spectrum, 1, 1));
  cairo_line_to(
      cr,
      SUSCAN_SPECTRUM_TO_SCR(spectrum, 0, 1));
  cairo_stroke(cr);

  cairo_move_to(
      cr,
      SUSCAN_SPECTRUM_TO_SCR(spectrum, 1, 1));

  cairo_line_to(
      cr,
      SUSCAN_SPECTRUM_TO_SCR(spectrum, 1, 0));
  cairo_stroke(cr);

  /* Draw spectrum */

  if (spectrum->psd_data != NULL) {
    x_prev = .5;
    hy = 1. / (spectrum->db_per_div * SUSCAN_GUI_VERTICAL_DIVS);

    cairo_set_dash(cr, NULL, 0, 0);

    /* Draw noise level (if applicable) */
    if (spectrum->N0 > 0) {
      cairo_set_source_rgb(cr, 0, 1., 1.);
      cairo_move_to(
          cr,
          SUSCAN_SPECTRUM_TO_SCR(
              spectrum,
              0,
              -SU_POWER_DB(spectrum->N0) * hy));

      cairo_line_to(
          cr,
          SUSCAN_SPECTRUM_TO_SCR(
              spectrum,
              1,
              -SU_POWER_DB(spectrum->N0) * hy));

      cairo_stroke(cr);
    }

    /* Draw spectrum */
    cairo_set_source_rgb(cr, 1., 1., 0);
    for (i = 1; i < spectrum->psd_size; ++i) {
      if ((x = .5 + i / (SUFLOAT) spectrum->psd_size) > 1)
        x -= 1;

      if (x_prev > x)
        x_prev = 0;

      cairo_move_to(
          cr,
          SUSCAN_SPECTRUM_TO_SCR(
              spectrum,
              x_prev,
              -SU_POWER_DB(spectrum->psd_data[i - 1]) * hy));
      cairo_line_to(
          cr,
          SUSCAN_SPECTRUM_TO_SCR(
              spectrum,
              x,
              -SU_POWER_DB(spectrum->psd_data[i]) * hy));

      cairo_stroke(cr);

      x_prev = x;
    }
  }
}


gboolean
suscan_spectrum_on_configure_event(
    GtkWidget *widget,
    GdkEventConfigure *event,
    gpointer data)
{
  struct suscan_gui *gui = (struct suscan_gui *) data;

  suscan_gui_spectrum_configure(&gui->main_spectrum, widget);

  return TRUE;
}


gboolean
suscan_spectrum_on_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
  struct suscan_gui *gui = (struct suscan_gui *) data;

  suscan_gui_spectrum_redraw(&gui->main_spectrum, cr);

  return FALSE;
}
