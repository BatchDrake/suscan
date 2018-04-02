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

#include "waveform.h"

#define SUGTK_WAVEFORM_GRAPH_REL_RADIUS .75
#define SUGTK_WAVEFORM_GRAPH_LINE_WIDTH 4

G_DEFINE_TYPE(SuGtkWaveForm, sugtk_waveform, GTK_TYPE_DRAWING_AREA);

#define SUGTK_WAVEFORM_TO_SCR_X(cons, x) \
  (.5 * ((x) + 1.) * (cons)->width)

#define SUGTK_WAVEFORM_TO_SCR_Y(cons, y) \
  (.5 * (-(y) + 1.) * (cons)->height)

#define SUSCAN_WAVEFORM_TO_SCR(cons, x, y) \
  SUGTK_WAVEFORM_TO_SCR_X(cons, x), \
  SUGTK_WAVEFORM_TO_SCR_Y(cons, y)

#define SUGTK_WAVEFORM_POINT_RADIUS 3e-3

static void
sugtk_waveform_redraw(SuGtkWaveForm *waveform)
{
  static const double axis_pattern[] = {1.0, 1.0};
  gfloat bright;
  gfloat last = 0;
  guint i, n;
  guint max_points;
  cairo_t *cr;

  cr = cairo_create(waveform->sf_waveform);

  cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);

  gdk_cairo_set_source_rgba(cr, &waveform->bg_color);

  /* Paint in black */
  cairo_paint(cr);

  cairo_set_line_width(cr, 1);

  /* Draw axes */
  gdk_cairo_set_source_rgba(cr, &waveform->axes_color);

  cairo_set_dash(cr, axis_pattern, 2, 0);

  /* Horizontal axis */
  cairo_move_to(
      cr,
      SUSCAN_WAVEFORM_TO_SCR(waveform, -1, 0));
  cairo_line_to(
      cr,
      SUSCAN_WAVEFORM_TO_SCR(waveform, 1, 0));

  cairo_stroke(cr);

  /* Paint points */
  gdk_cairo_set_source_rgba(cr, &waveform->fg_color);
  cairo_set_dash(cr, NULL, 0, 0);

  cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);

  max_points = waveform->width / waveform->zoom_t;
  if (max_points > SUGTK_WAVEFORM_HISTORY)
    max_points = SUGTK_WAVEFORM_HISTORY;

  for (i = 0; i < max_points; ++i) {
    n = (SUGTK_WAVEFORM_HISTORY + waveform->p - i) % SUGTK_WAVEFORM_HISTORY;

    if (i > 0) {
      cairo_move_to(
          cr,
          (waveform->width - (i - 1) * waveform->zoom_t - 1),
          SUGTK_WAVEFORM_TO_SCR_Y(waveform, last));

      cairo_line_to(
          cr,
          (waveform->width - i * waveform->zoom_t - 1),
          SUGTK_WAVEFORM_TO_SCR_Y(waveform, last));
      cairo_stroke(cr);

      cairo_move_to(
          cr,
          (waveform->width - i * waveform->zoom_t - 1),
          SUGTK_WAVEFORM_TO_SCR_Y(waveform, last));

      cairo_line_to(
          cr,
          (waveform->width - i * waveform->zoom_t - 1),
          SUGTK_WAVEFORM_TO_SCR_Y(waveform, waveform->history[n]));
      cairo_stroke(cr);
    }

    last = waveform->history[n];
  }

  cairo_destroy(cr);
}

void
sugtk_waveform_reset(SuGtkWaveForm *waveform)
{
  memset(waveform->history, 0, sizeof (waveform->history));
}

void
sugtk_waveform_push(SuGtkWaveForm *waveform, gfloat sample)
{
  ++waveform->count;
  waveform->history[waveform->p++] = sample;
  if (waveform->p == SUGTK_WAVEFORM_HISTORY)
    waveform->p = 0;
}

void
sugtk_waveform_commit(SuGtkWaveForm *waveform)
{
  struct timeval tv, sub;

  if (waveform->count - waveform->last_drawn
      >= SUGTK_WAVEFORM_DRAW_THRESHOLD) {
    gettimeofday(&tv, NULL);

    timersub(&tv, &waveform->last_redraw_time, &sub);

    if (sub.tv_usec > SUGTK_WAVEFORM_MIN_REDRAW_INTERVAL_MS * 1000) {
      waveform->last_drawn = waveform->count;
      sugtk_waveform_redraw(waveform);
      gtk_widget_queue_draw(GTK_WIDGET(waveform));
      waveform->last_redraw_time = tv;
    }
  }
}

static void
sugtk_waveform_dispose(GObject* object)
{
  SuGtkWaveForm *waveform = SUGTK_WAVEFORM(object);

  if (waveform->sf_waveform != NULL) {
    cairo_surface_destroy(waveform->sf_waveform);
    waveform->sf_waveform = NULL;
  }

  G_OBJECT_CLASS(sugtk_waveform_parent_class)->dispose(object);
}

static void
sugtk_waveform_class_init(SuGtkWaveFormClass *class)
{
  GObjectClass  *g_object_class;

  g_object_class = G_OBJECT_CLASS(class);

  g_object_class->dispose = sugtk_waveform_dispose;
}

static gboolean
sugtk_waveform_on_configure_event(
    GtkWidget *widget,
    GdkEventConfigure *event,
    gpointer data)
{
  SuGtkWaveForm *waveform = SUGTK_WAVEFORM(widget);

  waveform->width  = event->width;
  waveform->height = event->height;

  if (waveform->sf_waveform != NULL)
    cairo_surface_destroy(waveform->sf_waveform);

  waveform->sf_waveform = gdk_window_create_similar_surface(
      gtk_widget_get_window(widget),
      CAIRO_CONTENT_COLOR,
      waveform->width,
      waveform->height);

  waveform->last_redraw_time.tv_sec  = 0;
  waveform->last_redraw_time.tv_usec = 0;
  waveform->last_drawn = 0;
  waveform->count = SUGTK_WAVEFORM_DRAW_THRESHOLD;

  sugtk_waveform_commit(waveform);

  return TRUE;
}

void
sugtk_waveform_set_fg_color(SuGtkWaveForm *waveform, GdkRGBA color)
{
  waveform->fg_color = color;
  sugtk_waveform_redraw(waveform);
  gtk_widget_queue_draw(GTK_WIDGET(waveform));
}

void
sugtk_waveform_set_bg_color(SuGtkWaveForm *waveform, GdkRGBA color)
{
  waveform->bg_color = color;
  sugtk_waveform_redraw(waveform);
  gtk_widget_queue_draw(GTK_WIDGET(waveform));
}

void
sugtk_waveform_set_axes_color(SuGtkWaveForm *waveform, GdkRGBA color)
{
  waveform->axes_color = color;
  sugtk_waveform_redraw(waveform);
  gtk_widget_queue_draw(GTK_WIDGET(waveform));
}

static gboolean
sugtk_waveform_on_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
  SuGtkWaveForm *waveform = SUGTK_WAVEFORM(widget);

  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_surface(cr, waveform->sf_waveform, 0, 0);
  cairo_paint(cr);

  return FALSE;
}

static void
sugtk_waveform_init(SuGtkWaveForm *self)
{
  gtk_widget_set_events(
      GTK_WIDGET(self),
      GDK_EXPOSURE_MASK);

  g_signal_connect(
      self,
      "configure-event",
      (GCallback) sugtk_waveform_on_configure_event,
      NULL);

  g_signal_connect(
      self,
      "draw",
      (GCallback) sugtk_waveform_on_draw,
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

  self->zoom_t = 3;
}

GtkWidget *
sugtk_waveform_new(void)
{
  return (GtkWidget *) g_object_new(SUGTK_TYPE_WAVEFORM, NULL);
}

