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

  spectrum->show_channels = SU_TRUE;

  spectrum->freq_offset = SUSCAN_GUI_SPECTRUM_FREQ_OFFSET_DEFAULT;
  spectrum->freq_scale  = SUSCAN_GUI_SPECTRUM_FREQ_SCALE_DEFAULT;
  spectrum->ref_level   = SUSCAN_GUI_SPECTRUM_REF_LEVEL_DEFAULT;
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
suscan_gui_spectrum_apply_delta(
    struct suscan_gui_spectrum *spectrum,
    enum suscan_gui_spectrum_param param,
    SUFLOAT delta)
{
  switch (param) {
    case SUSCAN_GUI_SPECTRUM_PARAM_FREQ_OFFSET:
      /* Multiplied by freq_scale to keep proportion */
      spectrum->ref_level -=
          SUSCAN_GUI_SPECTRUM_SCALE_DELTA * delta * spectrum->freq_scale;
      break;

    case SUSCAN_GUI_SPECTRUM_PARAM_FREQ_SCALE:
      /* Multiplied by freq_scale to keep proportion */
      spectrum->freq_scale -=
          SUSCAN_GUI_SPECTRUM_SCALE_DELTA * delta * spectrum->freq_scale;
      if (spectrum->freq_scale < 1)
        spectrum->freq_scale = 1;
      break;

    case SUSCAN_GUI_SPECTRUM_PARAM_REF_LEVEL:
      /* Multiplied by dbs_per_div to keep proportion */
      spectrum->ref_level -=
          SUSCAN_GUI_SPECTRUM_SCALE_DELTA * delta * spectrum->dbs_per_div;
      break;

    case SUSCAN_GUI_SPECTRUM_PARAM_DBS_PER_DIV:
      /* Multiplied by dbs_per_div to keep proportion */
      spectrum->dbs_per_div -=
          SUSCAN_GUI_SPECTRUM_SCALE_DELTA * delta * spectrum->dbs_per_div;
      break;
  }
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
    snprintf(
        text,
        sizeof(text),
        "%d",
        -(int) (SUSCAN_GUI_SPECTRUM_ADJUST_Y_INV(
            spectrum,
            (SUFLOAT) i / SUSCAN_GUI_VERTICAL_DIVS)));

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

  /* Draw channels, if enabled */
  if (spectrum->show_channels)
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

/******************* This callbacks belong to the GUI API ********************/

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

  char text[32];

  if (gui->current_samp_rate != gui->main_spectrum.samp_rate) {
    gui->current_samp_rate = gui->main_spectrum.samp_rate;
    snprintf(text, sizeof(text), "%li", gui->current_samp_rate);
    gtk_label_set_text(gui->spectrumSampleRate, text);
  }

  gui->main_spectrum.show_channels =
      gtk_toggle_button_get_active(
          GTK_TOGGLE_BUTTON(gui->spectrumShowChannelsCheck));

  suscan_gui_spectrum_redraw(&gui->main_spectrum, cr);

  return FALSE;
}

void
suscan_spectrum_on_scroll(GtkWidget *widget, GdkEventScroll *ev, gpointer data)
{
  char text[32];

  struct suscan_gui *gui = (struct suscan_gui *) data;
  switch (ev->direction) {
    case GDK_SCROLL_SMOOTH:
      if (ev->state & GDK_SHIFT_MASK) {
        suscan_gui_spectrum_apply_delta(
            &gui->main_spectrum,
            SUSCAN_GUI_SPECTRUM_PARAM_DBS_PER_DIV,
            -ev->delta_y);
        snprintf(text, sizeof(text), "%.2lg dB", gui->main_spectrum.dbs_per_div);
        gtk_label_set_text(gui->spectrumDbsPerDivLabel, text);
      } else {
        suscan_gui_spectrum_apply_delta(
            &gui->main_spectrum,
            SUSCAN_GUI_SPECTRUM_PARAM_FREQ_SCALE,
            ev->delta_y);
        snprintf(text, sizeof(text), "%.2lgx", gui->main_spectrum.freq_scale);
        gtk_label_set_text(gui->spectrumFreqScaleLabel, text);
      }
      break;
  }
}

void
suscan_spectrum_on_motion(GtkWidget *widget, GdkEventMotion *ev, gpointer data)
{
  char text[32];
  SUFLOAT  x,  y;
  SUFLOAT lx, ly;
  SUFLOAT dx, dy;
  struct suscan_gui *gui = (struct suscan_gui *) data;

  if (ev->state & GDK_BUTTON1_MASK) {
    if (!gui->dragging) {
      gui->original_ref_level = gui->main_spectrum.ref_level;
      gui->original_freq_offset = gui->main_spectrum.freq_offset;
      gui->dragging = SU_TRUE;
    }

    /* Change reference level */
    y = SUSCAN_GUI_SPECTRUM_ADJUST_Y_INV(
        &gui->main_spectrum,
        SUSCAN_SPECTRUM_FROM_SCR_Y(&gui->main_spectrum, ev->y));

    ly = SUSCAN_GUI_SPECTRUM_ADJUST_Y_INV(
        &gui->main_spectrum,
        SUSCAN_SPECTRUM_FROM_SCR_Y(&gui->main_spectrum, gui->last_y));

    gui->main_spectrum.ref_level = gui->original_ref_level + ly - y;
    snprintf(
        text,
        sizeof(text),
        "%.2lg dB",
        gui->main_spectrum.ref_level);
    gtk_label_set_text(gui->spectrumRefLevelLabel, text);

    /* Change frequency offset only if sample rate has been defined */
    if (gui->main_spectrum.samp_rate != 0) {
      x = SUSCAN_GUI_SPECTRUM_ADJUST_X_INV(
            &gui->main_spectrum,
            SUSCAN_SPECTRUM_FROM_SCR_X(&gui->main_spectrum, ev->x));


      lx = SUSCAN_GUI_SPECTRUM_ADJUST_X_INV(
            &gui->main_spectrum,
            SUSCAN_SPECTRUM_FROM_SCR_X(&gui->main_spectrum, gui->last_x));

      gui->main_spectrum.freq_offset = gui->original_freq_offset + lx - x;
      snprintf(
          text,
          sizeof(text),
          "%.2lg Hz",
          gui->main_spectrum.samp_rate * gui->main_spectrum.freq_offset);
      gtk_label_set_text(gui->spectrumFreqOffsetLabel, text);
    }
  } else {
    if (gui->dragging)
      gui->dragging = SU_FALSE;

    gui->last_x = ev->x;
    gui->last_y = ev->y;
  }
}

