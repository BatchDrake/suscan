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
#include <sigutils/agc.h>

#include "gui.h"
#include "constellation.h"

#define SUSCAN_CONSTELLATION_TO_SCR_X(cons, x) \
  (.5 * ((x) + 1.) * (cons)->width)

#define SUSCAN_CONSTELLATION_TO_SCR_Y(cons, y) \
  (.5 * (-(y) + 1.) * (cons)->height)

#define SUSCAN_CONSTELLATION_TO_SCR(cons, x, y) \
  SUSCAN_CONSTELLATION_TO_SCR_X(cons, x), \
  SUSCAN_CONSTELLATION_TO_SCR_Y(cons, y)

#define SUSCAN_CONSTELLATION_POINT_RADIUS 1e-2

void
suscan_gui_constellation_finalize(
    struct suscan_gui_constellation *constellation)
{
  if (constellation->surface != NULL)
    cairo_surface_destroy(constellation->surface);
}

void
suscan_gui_constellation_clear(struct suscan_gui_constellation *constellation)
{
  cairo_t *cr;

  cr = cairo_create(constellation->surface);

  cairo_set_source_rgb(cr, 0, 0, 0);

  cairo_paint(cr);

  cairo_destroy(cr);
}

void
suscan_gui_constellation_init(struct suscan_gui_constellation *constellation)
{
  memset(constellation, 0, sizeof(struct suscan_gui_constellation));

  constellation->phase = 1.; /* Zero phase */
}

void
suscan_gui_constellation_configure(
    struct suscan_gui_constellation *constellation,
    GtkWidget *widget)
{
  if (constellation->surface != NULL)
    cairo_surface_destroy(constellation->surface);

  constellation->width = gtk_widget_get_allocated_width(widget);
  constellation->height = gtk_widget_get_allocated_height(widget);

  constellation->surface = gdk_window_create_similar_surface(
      gtk_widget_get_window(widget),
      CAIRO_CONTENT_COLOR,
      constellation->width,
      constellation->height);

  suscan_gui_constellation_clear(constellation);
}

void
suscan_gui_constellation_push_sample(
    struct suscan_gui_constellation *constellation,
    SUCOMPLEX sample)
{
  constellation->history[constellation->p++] = sample;
  if (constellation->p == SUSCAN_GUI_CONSTELLATION_HISTORY)
    constellation->p = 0;
}

void
suscan_gui_constellation_redraw(
    struct suscan_gui_constellation *constellation,
    cairo_t *cr)
{
  static const double axis_pattern[] = {5.0, 5.0};
  SUFLOAT bright;
  unsigned int i, n;

  cairo_set_source_surface(cr, constellation->surface, 0, 0);

  /* Paint in black */
  cairo_paint(cr);

  cairo_set_line_width(cr, 1);

  /* Draw axes */
  cairo_set_source_rgb(cr, 0, 0.5, 0);

  cairo_set_dash(cr, axis_pattern, 2, 0);

  /* Vertical axis */
  cairo_move_to(
      cr,
      SUSCAN_CONSTELLATION_TO_SCR(constellation, 0, -1));
  cairo_line_to(
      cr,
      SUSCAN_CONSTELLATION_TO_SCR(constellation, 0, 1));

  cairo_stroke(cr);

  /* Horizontal axis */
  cairo_move_to(
      cr,
      SUSCAN_CONSTELLATION_TO_SCR(constellation, -1, 0));
  cairo_line_to(
      cr,
      SUSCAN_CONSTELLATION_TO_SCR(constellation, 1, 0));

  cairo_stroke(cr);

  /* Paint points */
  for (i = 0; i < SUSCAN_GUI_CONSTELLATION_HISTORY; ++i) {
    n = (i + constellation->p) % SUSCAN_GUI_CONSTELLATION_HISTORY;
    bright = (i + 1) / (SUFLOAT) SUSCAN_GUI_CONSTELLATION_HISTORY;

    cairo_arc(
        cr,
        SUSCAN_CONSTELLATION_TO_SCR(
            constellation,
            SU_C_IMAG(constellation->history[n] * constellation->phase),
            SU_C_REAL(constellation->history[n] * constellation->phase)),
        SUSCAN_CONSTELLATION_POINT_RADIUS
          * MIN(constellation->width, constellation->height),
        0,
        2 * M_PI);
    cairo_set_source_rgba(cr, 1.0, 1.0, 0.0, bright);

    cairo_fill_preserve(cr);

    cairo_stroke(cr);
  }
}

/************** These callbacks belong to the GUI inspector API **************/
gboolean
suscan_constellation_on_configure_event(
    GtkWidget *widget,
    GdkEventConfigure *event,
    gpointer data)
{
  struct suscan_gui_inspector *insp =
      (struct suscan_gui_inspector *) data;

  suscan_gui_constellation_configure(&insp->constellation, widget);

  return TRUE;
}

gboolean
suscan_constellation_on_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
  struct suscan_gui_inspector *insp =
      (struct suscan_gui_inspector *) data;

  suscan_gui_constellation_redraw(&insp->constellation, cr);

  return FALSE;
}
