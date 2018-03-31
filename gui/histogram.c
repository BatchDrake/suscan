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

#include "histogram.h"

#define SUGTK_HISTOGRAM_GRAPH_REL_RADIUS .75
#define SUGTK_HISTOGRAM_GRAPH_LINE_WIDTH 4

G_DEFINE_TYPE(SuGtkHistogram, sugtk_histogram, GTK_TYPE_DRAWING_AREA);

#define SUGTK_HISTOGRAM_TO_SCR_X(cons, x) \
  (.5 * ((x) + 1.) * (cons)->width)

#define SUGTK_HISTOGRAM_FROM_SCR_X(cons, x) \
  (2. * (gfloat) x / (cons)->width - 1.)

#define SUGTK_HISTOGRAM_TO_SCR_Y(cons, y) \
  (.5 * (-(y) + 1.) * (cons)->height)

#define SUSCAN_HISTOGRAM_TO_SCR(cons, x, y) \
  SUGTK_HISTOGRAM_TO_SCR_X(cons, x), \
  SUGTK_HISTOGRAM_TO_SCR_Y(cons, y)

#define SUGTK_HISTOGRAM_POINT_RADIUS 3e-3

static void
sugtk_histogram_redraw(SuGtkHistogram *histogram)
{
  static const double axis_pattern[] = {1.0, 1.0};
  char text[32];
  gfloat bright;
  gfloat last = 0;
  gfloat scale_y;
  gfloat scale_x;
  guint i;
  cairo_t *cr;

  cr = cairo_create(histogram->sf_histogram);

  cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);

  gdk_cairo_set_source_rgba(cr, &histogram->bg_color);

  /* Paint in black */
  cairo_paint(cr);

  cairo_set_line_width(cr, 1);

  /* Draw axes */
  gdk_cairo_set_source_rgba(cr, &histogram->axes_color);
  cairo_set_dash(cr, axis_pattern, 2, 0);

  /* Draw floor */
  cairo_move_to(
      cr,
      SUSCAN_HISTOGRAM_TO_SCR(histogram, -1, SUGTK_HISTOGRAM_FLOOR_FRAC));
  cairo_line_to(
      cr,
      SUSCAN_HISTOGRAM_TO_SCR(histogram, 1, SUGTK_HISTOGRAM_FLOOR_FRAC));

  cairo_stroke(cr);

  /* Draw centers */
  for (i = 0; i < histogram->levels; ++i) {
    cairo_move_to(
        cr,
        SUSCAN_HISTOGRAM_TO_SCR(histogram, (i + .5) * histogram->frac - 1, -1));
    cairo_line_to(
        cr,
        SUSCAN_HISTOGRAM_TO_SCR(histogram, (i + .5) * histogram->frac - 1, 1));
    cairo_stroke(cr);
  }

  cairo_set_dash(cr, NULL, 0, 0);

  /* Draw vertical thresholds */
  for (i = 1; i < histogram->levels; ++i) {
    cairo_move_to(
        cr,
        SUSCAN_HISTOGRAM_TO_SCR(histogram, i * histogram->frac - 1, -1));
    cairo_line_to(
        cr,
        SUSCAN_HISTOGRAM_TO_SCR(histogram, i * histogram->frac - 1, 1));
    cairo_stroke(cr);
  }

  /* Paint points */
  gdk_cairo_set_source_rgba(cr, &histogram->fg_color);

  cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);

  if (histogram->max == 0) {
    cairo_move_to(
        cr,
        SUSCAN_HISTOGRAM_TO_SCR(histogram, -1, SUGTK_HISTOGRAM_FLOOR_FRAC));
    cairo_line_to(
        cr,
        SUSCAN_HISTOGRAM_TO_SCR(histogram, 1, SUGTK_HISTOGRAM_FLOOR_FRAC));
  } else {
    scale_y = SUGTK_HISTOGRAM_HEIGHT / histogram->max;
    scale_x = histogram->zoom_x * histogram->width / SUGTK_HISTOGRAM_LENGTH;
    for (i = 1; i < SUGTK_HISTOGRAM_LENGTH; ++i) {
      cairo_move_to(
          cr,
          (i - 1) * scale_x,
          SUGTK_HISTOGRAM_TO_SCR_Y(
              histogram,
              histogram->bins[i - 1] * scale_y + SUGTK_HISTOGRAM_FLOOR_FRAC));
      cairo_line_to(
          cr,
          i * scale_x,
          SUGTK_HISTOGRAM_TO_SCR_Y(
              histogram,
              histogram->bins[i] * scale_y + SUGTK_HISTOGRAM_FLOOR_FRAC));
      cairo_stroke(cr);
    }
  }


  /* If there was a selection, paint it */
  if (histogram->selection) {
    gdk_cairo_set_source_rgba(cr, &histogram->axes_color);
    cairo_move_to(
        cr,
        SUGTK_HISTOGRAM_TO_SCR_X(histogram, histogram->sel_min),
        SUGTK_HISTOGRAM_TO_SCR_Y(histogram, -1));
    cairo_line_to(
        cr,
        SUGTK_HISTOGRAM_TO_SCR_X(histogram, histogram->sel_min),
        SUGTK_HISTOGRAM_TO_SCR_Y(histogram, 1));
    cairo_stroke(cr);

    cairo_move_to(
        cr,
        SUGTK_HISTOGRAM_TO_SCR_X(histogram, histogram->sel_max),
        SUGTK_HISTOGRAM_TO_SCR_Y(histogram, -1));
    cairo_line_to(
        cr,
        SUGTK_HISTOGRAM_TO_SCR_X(histogram, histogram->sel_max),
        SUGTK_HISTOGRAM_TO_SCR_Y(histogram, 1));
    cairo_stroke(cr);

    cairo_set_dash(cr, axis_pattern, 2, 0);
    cairo_move_to(
        cr,
        SUGTK_HISTOGRAM_TO_SCR_X(histogram, histogram->sel_min),
        SUGTK_HISTOGRAM_TO_SCR_Y(histogram, 0));
    cairo_line_to(
        cr,
        SUGTK_HISTOGRAM_TO_SCR_X(histogram, histogram->sel_max),
        SUGTK_HISTOGRAM_TO_SCR_Y(histogram, 0));
    cairo_stroke(cr);

    cairo_select_font_face(
        cr,
        "Inconsolata",
        CAIRO_FONT_SLANT_NORMAL,
        CAIRO_FONT_WEIGHT_BOLD);

    snprintf(
        text,
        sizeof(text),
        "%d%%",
        (int) round(50 * (histogram->sel_max - histogram->sel_min)));

    cairo_move_to(
        cr,
        SUGTK_HISTOGRAM_TO_SCR_X(
            histogram,
            .5 * (histogram->sel_max + histogram->sel_min)) - 8,
        SUGTK_HISTOGRAM_TO_SCR_Y(histogram, 0) - 3);
    cairo_show_text(
        cr,
        text);
  }

  cairo_destroy(cr);
}

void
sugtk_histogram_reset(SuGtkHistogram *histogram)
{
  histogram->max = 0;
  histogram->count = 0;
  memset(histogram->bins, 0, sizeof (histogram->bins));
}

void
sugtk_histogram_push(SuGtkHistogram *histogram, gfloat sample)
{
  unsigned int bin;
  unsigned int i;

  /* TODO: Keep track of off-interval samples */
  if (sample >= histogram->decider_params.min_val
      && sample < histogram->decider_params.max_val) {
    bin = floor(
        (sample - histogram->decider_params.min_val) * histogram->h_inv);
    if (++histogram->bins[bin] > histogram->max)
      histogram->max = histogram->bins[bin];
    if (++histogram->count == histogram->reset) {
        histogram->count = histogram->min_count;
        histogram->max *=
            (gfloat) histogram->min_count / (gfloat) histogram->count;
        for (i = 0; i < SUGTK_HISTOGRAM_LENGTH; ++i)
          histogram->bins[i] *=
              (gfloat) histogram->min_count / (gfloat) histogram->count;
      }
  }
}

void
sugtk_histogram_commit(SuGtkHistogram *histogram)
{
  struct timeval tv, sub;

  if (histogram->count - histogram->last_drawn
      >= SUGTK_HISTOGRAM_DRAW_THRESHOLD) {
    gettimeofday(&tv, NULL);

    timersub(&tv, &histogram->last_redraw_time, &sub);

    if (sub.tv_usec > SUGTK_HISTOGRAM_MIN_REDRAW_INTERVAL_MS * 1000) {
      histogram->last_drawn = histogram->count;
      sugtk_histogram_redraw(histogram);
      gtk_widget_queue_draw(GTK_WIDGET(histogram));
      histogram->last_redraw_time = tv;
    }
  }
}

static void
sugtk_histogram_dispose(GObject* object)
{
  SuGtkHistogram *histogram = SUGTK_HISTOGRAM(object);

  if (histogram->sf_histogram != NULL) {
    cairo_surface_destroy(histogram->sf_histogram);
    histogram->sf_histogram = NULL;
  }

  if (histogram->deciderMenu != NULL) {
    gtk_widget_destroy(GTK_WIDGET(histogram->deciderMenu));
    histogram->deciderMenu = NULL;
  }

  G_OBJECT_CLASS(sugtk_histogram_parent_class)->dispose(object);
}

static void
sugtk_histogram_class_init(SuGtkHistogramClass *class)
{
  GObjectClass  *g_object_class;

  g_object_class = G_OBJECT_CLASS(class);

  g_object_class->dispose = sugtk_histogram_dispose;

  class->sig_set_decider =  g_signal_new(
      "set-decider",
      G_TYPE_FROM_CLASS (g_object_class),
      G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS,
      0, /* class offset */
      NULL /* accumulator */,
      NULL /* accu_data */,
      g_cclosure_marshal_VOID__POINTER, /* marshaller */
      G_TYPE_NONE /* return_type */,
      1,
      G_TYPE_POINTER); /* n_params */
}

static gboolean
sugtk_histogram_on_configure_event(
    GtkWidget *widget,
    GdkEventConfigure *event,
    gpointer data)
{
  SuGtkHistogram *histogram = SUGTK_HISTOGRAM(widget);

  histogram->width  = event->width;
  histogram->height = event->height;

  if (histogram->sf_histogram != NULL)
    cairo_surface_destroy(histogram->sf_histogram);

  histogram->sf_histogram = gdk_window_create_similar_surface(
      gtk_widget_get_window(widget),
      CAIRO_CONTENT_COLOR,
      histogram->width,
      histogram->height);

  histogram->last_redraw_time.tv_sec  = 0;
  histogram->last_redraw_time.tv_usec = 0;
  histogram->last_drawn = 0;
  histogram->count = SUGTK_HISTOGRAM_DRAW_THRESHOLD;

  sugtk_histogram_commit(histogram);

  return TRUE;
}

void
sugtk_histogram_set_fg_color(SuGtkHistogram *histogram, GdkRGBA color)
{
  histogram->fg_color = color;
  sugtk_histogram_redraw(histogram);
  gtk_widget_queue_draw(GTK_WIDGET(histogram));
}

void
sugtk_histogram_set_bg_color(SuGtkHistogram *histogram, GdkRGBA color)
{
  histogram->bg_color = color;
  sugtk_histogram_redraw(histogram);
  gtk_widget_queue_draw(GTK_WIDGET(histogram));
}

void
sugtk_histogram_set_axes_color(SuGtkHistogram *histogram, GdkRGBA color)
{
  histogram->axes_color = color;
  sugtk_histogram_redraw(histogram);
  gtk_widget_queue_draw(GTK_WIDGET(histogram));
}

void
sugtk_histogram_init_levels(SuGtkHistogram *hist)
{
  hist->levels = 1 << hist->decider_params.bits;
  hist->frac = 2. / hist->levels;
  hist->h_inv = SUGTK_HISTOGRAM_LENGTH /
      (hist->decider_params.max_val - hist->decider_params.min_val);
  sugtk_histogram_reset(hist);
}

void
sugtk_histogram_set_decider_params(
    SuGtkHistogram *hist,
    const struct sigutils_decider_params *params)
{
  hist->decider_params = *params;

  sugtk_histogram_init_levels(hist);
  sugtk_histogram_redraw(hist);
  gtk_widget_queue_draw(GTK_WIDGET(hist));
}


static gboolean
sugtk_histogram_on_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
  SuGtkHistogram *histogram = SUGTK_HISTOGRAM(widget);

  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_surface(cr, histogram->sf_histogram, 0, 0);
  cairo_paint(cr);

  return FALSE;
}

static void
sugtk_histogram_parse_selection(
    SuGtkHistogram *histogram,
    const GdkEventMotion *ev)
{
  gfloat x;
  gfloat lx;

  x = SUGTK_HISTOGRAM_FROM_SCR_X(histogram, ev->x);
  lx = SUGTK_HISTOGRAM_FROM_SCR_X(histogram, histogram->last_x);

  if (x < lx) {
    histogram->sel_min = x;
    histogram->sel_max = lx;
  } else {
    histogram->sel_min = lx;
    histogram->sel_max = x;
  }

  sugtk_histogram_redraw(histogram);
  gtk_widget_queue_draw(GTK_WIDGET(histogram));
}

static gboolean
sugtk_histogram_on_motion_notify_event(
    GtkWidget *widget,
    GdkEventMotion *ev,
    gpointer data)
{
  SuGtkHistogram *histogram = SUGTK_HISTOGRAM(widget);
  GdkEventMotion ev_adjusted;

  ev_adjusted = *ev;
  ev_adjusted.x = round(ev->x);

  if (ev_adjusted.state & GDK_BUTTON1_MASK) {
    histogram->selection = TRUE; /* This is a selection */
    histogram->selecting = TRUE; /* And also, we are selecting */
    sugtk_histogram_parse_selection(histogram, &ev_adjusted);
  } else {
    histogram->selecting = FALSE; /* Not selecting */
    histogram->last_x = ev_adjusted.x;
  }

  return TRUE;
}

static gboolean
sugtk_histogram_on_button_press_event(
    GtkWidget *widget,
    GdkEventButton *ev,
    gpointer data)
{
  SuGtkHistogram *histogram = SUGTK_HISTOGRAM(widget);

  if (ev->type == GDK_BUTTON_PRESS) {
    switch (ev->button) {
      case 1:
        /* Reset selection */
        if (histogram->selection) {
          histogram->selection = FALSE;
          sugtk_histogram_redraw(histogram);
          gtk_widget_queue_draw(GTK_WIDGET(histogram));
        }
        break;

      case 3:
        /* Menu */
        gtk_widget_set_sensitive(
            GTK_WIDGET(histogram->setDecider),
            histogram->selection && histogram->levels > 1);
        gtk_widget_show_all(GTK_WIDGET(histogram->deciderMenu));
        gtk_menu_popup_at_pointer(histogram->deciderMenu, (GdkEvent *) ev);
        break;
    }
  }

  return TRUE;
}

static void
sugtk_histogram_on_set_params(GtkWidget *widget, gpointer data)
{
  SuGtkHistogram *histogram = SUGTK_HISTOGRAM(data);
  gfloat rel_min, rel_max;
  gfloat width;
  gfloat half_bin;
  struct sigutils_decider_params params = histogram->decider_params;

  if (histogram->selection) {
    histogram->selection = FALSE;

    width = params.max_val - params.min_val;
    rel_min = .5 * (histogram->sel_min + 1);
    rel_max = .5 * (histogram->sel_max + 1);

    /*
     * Compute the size of the interval and half the bin size. Append
     * this value to both ends of the interval (we are selecting the
     * centroids, not the thresholds)
     */
    half_bin = (rel_max - rel_min) / (2 * (histogram->levels - 1));
    rel_min -= half_bin;
    rel_max += half_bin;

    params.max_val = params.min_val + rel_max * width;
    params.min_val = params.min_val + rel_min * width;

    g_signal_emit(
        histogram,
        SUGTK_HISTOGRAM_GET_CLASS(histogram)->sig_set_decider,
        0,
        &params);

    sugtk_histogram_set_decider_params(histogram, &params);
  }
}

static void
sugtk_histogram_on_reset(GtkWidget *widget, gpointer data)
{
  SuGtkHistogram *histogram = SUGTK_HISTOGRAM(data);
  struct sigutils_decider_params params = sigutils_decider_params_INITIALIZER;

  params.bits = histogram->decider_params.bits;
  histogram->selection = FALSE;

  g_signal_emit(
      histogram,
      SUGTK_HISTOGRAM_GET_CLASS(histogram)->sig_set_decider,
      0,
      &params);

  sugtk_histogram_set_decider_params(histogram, &params);
}

static void
sugtk_histogram_init(SuGtkHistogram *self)
{
  struct sigutils_decider_params params = sigutils_decider_params_INITIALIZER;

  self->sf_histogram = NULL;

  gtk_widget_set_events(
      GTK_WIDGET(self),
        GDK_EXPOSURE_MASK
      | GDK_POINTER_MOTION_MASK
      | GDK_BUTTON_MOTION_MASK
      | GDK_BUTTON1_MOTION_MASK
      | GDK_BUTTON3_MOTION_MASK
      | GDK_BUTTON_PRESS_MASK
      | GDK_STRUCTURE_MASK);

  g_signal_connect(
      self,
      "configure-event",
      (GCallback) sugtk_histogram_on_configure_event,
      NULL);

  g_signal_connect(
      self,
      "draw",
      (GCallback) sugtk_histogram_on_draw,
      NULL);

  g_signal_connect(
      self,
      "button-press-event",
      (GCallback) sugtk_histogram_on_button_press_event,
      NULL);

  g_signal_connect(
      self,
      "motion-notify-event",
      (GCallback) sugtk_histogram_on_motion_notify_event,
      NULL);

  self->deciderMenu = GTK_MENU(gtk_menu_new());

  self->setDecider =
      GTK_MENU_ITEM(gtk_menu_item_new_with_label("Update decider"));
  gtk_menu_shell_append(
      GTK_MENU_SHELL(self->deciderMenu),
      GTK_WIDGET(self->setDecider));
  gtk_widget_set_sensitive(GTK_WIDGET(self->setDecider), FALSE);
  g_signal_connect(
      GTK_WIDGET(self->setDecider),
      "activate",
      (GCallback) sugtk_histogram_on_set_params,
      self);

  gtk_menu_shell_append(
      GTK_MENU_SHELL(self->deciderMenu),
      gtk_separator_menu_item_new());

  self->resetDecider =
      GTK_MENU_ITEM(gtk_menu_item_new_with_label("Reset range"));
  gtk_menu_shell_append(
      GTK_MENU_SHELL(self->deciderMenu),
      GTK_WIDGET(self->resetDecider));
  gtk_widget_set_sensitive(GTK_WIDGET(self->resetDecider), TRUE);
  g_signal_connect(
      GTK_WIDGET(self->resetDecider),
      "activate",
      (GCallback) sugtk_histogram_on_reset,
      self);

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

  self->zoom_x = 1;

  self->decider_params = params;
  sugtk_histogram_init_levels(self);
}

GtkWidget *
sugtk_histogram_new(void)
{
  return (GtkWidget *) g_object_new(SUGTK_TYPE_HISTOGRAM, NULL);
}

