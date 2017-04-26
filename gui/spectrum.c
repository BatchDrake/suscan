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

#include <string.h>
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
suscan_gui_spectrum_init(struct suscan_gui_spectrum *spectrum)
{
  memset(spectrum, 0, sizeof (struct suscan_gui_spectrum));

  spectrum->freq_offset = SUSCAN_GUI_SPECTRUM_FREQ_OFFSET_DEFAULT;
  spectrum->freq_scale  = SUSCAN_GUI_SPECTRUM_FREQ_SCALE_DEFAULT;
  spectrum->dbs_per_div = SUSCAN_GUI_SPECTRUM_DBS_PER_DIV_DEFAULT;
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
suscan_gui_spectrum_update_channels(
    struct suscan_gui_spectrum *spectrum,
    struct sigutils_channel **channel_list,
    unsigned int channel_count)
{
  unsigned int i;

  for (i = 0; i < spectrum->channel_count; ++i)
    free(spectrum->channel_list[i]);

  if (spectrum->channel_list != NULL)
    free(spectrum->channel_list);

  spectrum->channel_list  = channel_list;
  spectrum->channel_count = channel_count;
}

void
suscan_gui_spectrum_draw_channels(
    struct suscan_gui_spectrum *spectrum,
    cairo_t *cr)
{
  int i;
  SUFLOAT x1, xscr1;
  SUFLOAT x2, xscr2;

  SUFLOAT y1, yscr1;
  SUFLOAT y2, yscr2;

  if (spectrum->samp_rate > 0) {
    for (i = 0; i < spectrum->channel_count; ++i) {
      /* Draw channel limits */
      x1 = (spectrum->channel_list[i]->f_lo
          - spectrum->fc)
              / (SUFLOAT) spectrum->samp_rate;
      x2 = (spectrum->channel_list[i]->f_hi
          - spectrum->fc)
              / (SUFLOAT) spectrum->samp_rate;

      /* Correct negative frequencies */
      if (x1 > .5) {
        x1 -= 1;
        x2 -= 1;
      }

      /* Apply frequency scaling */
      x1 = SUSCAN_GUI_SPECTRUM_ADJUST_X(spectrum, x1);
      x2 = SUSCAN_GUI_SPECTRUM_ADJUST_X(spectrum, x2);

      /* Draw channel if and only if it fits */
      if (x1 < .5 && x2 > -.5) {
        xscr1 = SUSCAN_SPECTRUM_TO_SCR_X(spectrum, x1 + .5);
        xscr2 = SUSCAN_SPECTRUM_TO_SCR_X(spectrum, x2 + .5);

        y1 = SUSCAN_GUI_SPECTRUM_ADJUST_Y(
            spectrum,
            -spectrum->channel_list[i]->S0);
        y2 = SUSCAN_GUI_SPECTRUM_ADJUST_Y(
            spectrum,
            -spectrum->channel_list[i]->N0);

        yscr1 = SUSCAN_SPECTRUM_TO_SCR_Y(spectrum, y1);
        yscr2 = SUSCAN_SPECTRUM_TO_SCR_Y(spectrum, y2);


        cairo_set_source_rgba(cr, .75, .0, .0, .25);
        cairo_rectangle(cr, xscr1, yscr1, xscr2 - xscr1, yscr2 - yscr1);
        cairo_stroke_preserve(cr);
        cairo_fill(cr);

        /* Draw detected bandwidth */
        cairo_set_source_rgba(cr, .75, .0, .0, .5);
        x1 = (spectrum->channel_list[i]->fc
            - spectrum->channel_list[i]->bw / 2
            - spectrum->fc)
                / (SUFLOAT) spectrum->samp_rate;
        x2 = (spectrum->channel_list[i]->fc
            + spectrum->channel_list[i]->bw / 2
            - spectrum->fc)
                / (SUFLOAT) spectrum->samp_rate;

        if (x1 > .5) {
          x1 -= 1;
          x2 -= 1;
        }

        x1 = SUSCAN_GUI_SPECTRUM_ADJUST_X(spectrum, x1);
        x2 = SUSCAN_GUI_SPECTRUM_ADJUST_X(spectrum, x2);

        xscr1 = SUSCAN_SPECTRUM_TO_SCR_X(spectrum, x1 + .5);
        xscr2 = SUSCAN_SPECTRUM_TO_SCR_X(spectrum, x2 + .5);

        cairo_rectangle(cr, xscr1, yscr1, xscr2 - xscr1, yscr2 - yscr1);
        cairo_stroke_preserve(cr);
        cairo_fill(cr);
      }
    }
  }

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

  cairo_set_source_rgba(cr, 1, 1, 1, 1);

  for (i = 1; i < SUSCAN_GUI_VERTICAL_DIVS; ++i) {
    snprintf(text, sizeof(text), "-%d", (int) round(i * spectrum->dbs_per_div));

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
              + (int) round(SU_NORM2ABS_FREQ(
                  spectrum->samp_rate,
                  2 * SUSCAN_GUI_SPECTRUM_ADJUST_X_INV(spectrum, x)))));

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
  int step;
  SUFLOAT x, x_prev;
  SUFLOAT x_adj, x_prev_adj;

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

  suscan_gui_spectrum_draw_channels(spectrum, cr);

  /* Draw spectrum */
  if (spectrum->psd_data != NULL) {
    x_prev = .5;

    cairo_set_dash(cr, NULL, 0, 0);

    /* Draw noise level (if applicable) */
    if (spectrum->N0 > 0) {
      cairo_set_source_rgb(cr, 0, 1., 1.);
      cairo_move_to(
          cr,
          SUSCAN_SPECTRUM_TO_SCR(
              spectrum,
              0,
              SUSCAN_GUI_SPECTRUM_ADJUST_Y(
                  spectrum,
                  -SU_POWER_DB(spectrum->N0))));

      cairo_line_to(
          cr,
          SUSCAN_SPECTRUM_TO_SCR(
              spectrum,
              1,
              SUSCAN_GUI_SPECTRUM_ADJUST_Y(
                  spectrum,
                  -SU_POWER_DB(spectrum->N0))));

      cairo_stroke(cr);
    }

    cairo_set_source_rgb(cr, 1., 1., 0);

    step = (int) SU_FLOOR(
        spectrum->psd_size / (spectrum->width * spectrum->freq_scale));

    if (step < 1)
      step = 1;

    x_prev = 0;
    for (i = step; i < spectrum->psd_size; i += step) {
      if ((x = i / (SUFLOAT) spectrum->psd_size) > .5) {
        x -= 1;

        if (x_prev > x)
          x_prev -= 1;
      }

      x_adj = SUSCAN_GUI_SPECTRUM_ADJUST_X(spectrum, x) + .5;

      if (x_adj >= 0 && x_adj < 1) {
        x_prev_adj = SUSCAN_GUI_SPECTRUM_ADJUST_X(spectrum, x_prev) + .5;

        cairo_move_to(
            cr,
            SUSCAN_SPECTRUM_TO_SCR(
                spectrum,
                x_prev_adj,
                SUSCAN_GUI_SPECTRUM_ADJUST_Y(
                    spectrum,
                    -SU_POWER_DB(spectrum->psd_data[i - step]))));
        cairo_line_to(
            cr,
            SUSCAN_SPECTRUM_TO_SCR(
                spectrum,
                x_adj,
                SUSCAN_GUI_SPECTRUM_ADJUST_Y(
                    spectrum,
                    -SU_POWER_DB(spectrum->psd_data[i]))));

        cairo_stroke(cr);
      }

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

  /* Retrieve spectrum parameters */
  gui->main_spectrum.freq_scale  = gtk_range_get_value(gui->scaleRange);
  gui->main_spectrum.freq_offset = gtk_range_get_value(gui->offsetRange);
  gui->main_spectrum.dbs_per_div = gtk_range_get_value(gui->dbRange);

  suscan_gui_spectrum_redraw(&gui->main_spectrum, cr);

  return FALSE;
}

void
suscan_spectrum_on_scroll(GtkWidget *widget, GdkEventScroll *ev, gpointer data)
{
  struct suscan_gui *gui = (struct suscan_gui *) data;
  switch (ev->direction) {
    case GDK_SCROLL_SMOOTH:
      gui->main_spectrum.freq_scale
        -=
            SUSCAN_GUI_SPECTRUM_SCALE_DELTA
            * ev->delta_y
            * gui->main_spectrum.freq_scale;
      gtk_range_set_value(gui->scaleRange, gui->main_spectrum.freq_scale);
      break;
  }
}


