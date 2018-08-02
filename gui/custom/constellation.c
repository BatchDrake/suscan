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

#include <glib-object.h>
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "constellation.h"

#define SUGTK_CONSTELLATION_GRAPH_REL_RADIUS .75
#define SUGTK_CONSTELLATION_GRAPH_LINE_WIDTH 4

G_DEFINE_TYPE(SuGtkConstellation, sugtk_constellation, GTK_TYPE_DRAWING_AREA);

#define SUGTK_CONSTELLATION_TO_SCR_X(cons, x) \
  (.5 * ((x) + 1.) * (cons)->width)

#define SUGTK_CONSTELLATION_TO_SCR_Y(cons, y) \
  (.5 * (-(y) + 1.) * (cons)->height)

#define SUSCAN_CONSTELLATION_TO_SCR(cons, x, y) \
  SUGTK_CONSTELLATION_TO_SCR_X(cons, x), \
  SUGTK_CONSTELLATION_TO_SCR_Y(cons, y)

#define SUGTK_CONSTELLATION_POINT_RADIUS 3e-3

static void
sugtk_constellation_redraw(SuGtkConstellation *constellation)
{
  static const double axis_pattern[] = {1.0, 1.0};
  gfloat bright;
  guint i, n;
  cairo_t *cr;

  cr = cairo_create(constellation->sf_constellation);

  cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);

  gdk_cairo_set_source_rgba(cr, &constellation->bg_color);

  /* Paint in black */
  cairo_paint(cr);

  cairo_set_line_width(cr, 1);

  /* Draw axes */
  gdk_cairo_set_source_rgba(cr, &constellation->axes_color);

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
  for (i = 0; i < SUGTK_CONSTELLATION_HISTORY; ++i) {
    n = (i + constellation->p) % SUGTK_CONSTELLATION_HISTORY;
    bright = (i + 1) / (gfloat) SUGTK_CONSTELLATION_HISTORY;

    cairo_arc(
        cr,
        SUSCAN_CONSTELLATION_TO_SCR(
            constellation,
            cimag(constellation->history[n]),
            creal(constellation->history[n])),
        SUGTK_CONSTELLATION_POINT_RADIUS
          * MIN(constellation->width, constellation->height),
        0,
        2 * M_PI);

    constellation->fg_color.alpha = bright;

    gdk_cairo_set_source_rgba(cr, &constellation->fg_color);

    cairo_fill_preserve(cr);

    cairo_stroke(cr);
  }

  cairo_destroy(cr);
}

void
sugtk_constellation_reset(SuGtkConstellation *constellation)
{
  memset(constellation->history, 0, sizeof (constellation->history));
}

void
sugtk_constellation_push(SuGtkConstellation *constellation, gcomplex sample)
{
  ++constellation->count;
  constellation->history[constellation->p++] = sample;
  if (constellation->p == SUGTK_CONSTELLATION_HISTORY)
    constellation->p = 0;
}

void
sugtk_constellation_commit(SuGtkConstellation *constellation)
{
  struct timeval tv, sub;
  unsigned long long int ms;

  if (constellation->count - constellation->last_drawn
      >= SUGTK_CONSTELLATION_DRAW_THRESHOLD) {
    gettimeofday(&tv, NULL);

    timersub(&tv, &constellation->last_redraw_time, &sub);
    ms = sub.tv_usec / 1000 + sub.tv_sec * 1000;

    if (ms > SUGTK_CONSTELLATION_MIN_REDRAW_INTERVAL_MS) {
      constellation->last_drawn = constellation->count;
      sugtk_constellation_redraw(constellation);
      gtk_widget_queue_draw(GTK_WIDGET(constellation));
      constellation->last_redraw_time = tv;
    }
  }
}

static void
sugtk_constellation_dispose(GObject* object)
{
  SuGtkConstellation *constellation = SUGTK_CONSTELLATION(object);

  if (constellation->sf_constellation != NULL) {
    cairo_surface_destroy(constellation->sf_constellation);
    constellation->sf_constellation = NULL;
  }

  G_OBJECT_CLASS(sugtk_constellation_parent_class)->dispose(object);
}

static void
sugtk_constellation_class_init(SuGtkConstellationClass *class)
{
  GObjectClass  *g_object_class;

  g_object_class = G_OBJECT_CLASS(class);

  g_object_class->dispose = sugtk_constellation_dispose;
}

static gboolean
sugtk_constellation_on_configure_event(
    GtkWidget *widget,
    GdkEventConfigure *event,
    gpointer data)
{
  SuGtkConstellation *constellation = SUGTK_CONSTELLATION(widget);

  constellation->width  = event->width;
  constellation->height = event->height;

  if (constellation->sf_constellation != NULL)
    cairo_surface_destroy(constellation->sf_constellation);

  constellation->sf_constellation = gdk_window_create_similar_surface(
      gtk_widget_get_window(widget),
      CAIRO_CONTENT_COLOR,
      constellation->width,
      constellation->height);

  constellation->last_redraw_time.tv_sec  = 0;
  constellation->last_redraw_time.tv_usec = 0;
  constellation->last_drawn = 0;
  constellation->count = SUGTK_CONSTELLATION_DRAW_THRESHOLD;

  sugtk_constellation_commit(constellation);

  return TRUE;
}

void
sugtk_constellation_set_fg_color(SuGtkConstellation *constellation, GdkRGBA color)
{
  constellation->fg_color = color;
  sugtk_constellation_redraw(constellation);
  gtk_widget_queue_draw(GTK_WIDGET(constellation));
}

void
sugtk_constellation_set_bg_color(SuGtkConstellation *constellation, GdkRGBA color)
{
  constellation->bg_color = color;
  sugtk_constellation_redraw(constellation);
  gtk_widget_queue_draw(GTK_WIDGET(constellation));
}

void
sugtk_constellation_set_axes_color(SuGtkConstellation *constellation, GdkRGBA color)
{
  constellation->axes_color = color;
  sugtk_constellation_redraw(constellation);
  gtk_widget_queue_draw(GTK_WIDGET(constellation));
}

static gboolean
sugtk_constellation_on_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
  SuGtkConstellation *constellation = SUGTK_CONSTELLATION(widget);

  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_surface(cr, constellation->sf_constellation, 0, 0);
  cairo_paint(cr);

  return FALSE;
}

static void
sugtk_constellation_init(SuGtkConstellation *self)
{
  gtk_widget_set_events(
      GTK_WIDGET(self),
      GDK_EXPOSURE_MASK);

  g_signal_connect(
      self,
      "configure-event",
      (GCallback) sugtk_constellation_on_configure_event,
      NULL);

  g_signal_connect(
      self,
      "draw",
      (GCallback) sugtk_constellation_on_draw,
      NULL);

  self->fg_color.red   = 1;
  self->fg_color.green = 1;
  self->fg_color.blue  = 0;
  self->fg_color.alpha = 1;

  self->bg_color.red   = 0;
  self->bg_color.green = 0;
  self->bg_color.blue  = 0;
  self->bg_color.alpha = 1;

  self->axes_color.red   = .5;
  self->axes_color.green = .5;
  self->axes_color.blue  = .5;
  self->axes_color.alpha = 1;

  self->phase = 1;
}

GtkWidget *
sugtk_constellation_new(void)
{
  return (GtkWidget *) g_object_new(SUGTK_TYPE_CONSTELLATION, NULL);
}

