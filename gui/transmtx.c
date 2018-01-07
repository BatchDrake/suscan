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
#include <complex.h>
#include "transmtx.h"

#define SUGTK_TRANS_MTX_GRAPH_REL_RADIUS .75
#define SUGTK_TRANS_MTX_GRAPH_LINE_WIDTH 4

G_DEFINE_TYPE(SuGtkTransMtx, sugtk_trans_mtx, GTK_TYPE_DRAWING_AREA);

static void
sugtk_trans_mtx_draw_graph(SuGtkTransMtx *mtx, cairo_t *cr)
{
  gfloat p;
  gfloat count_inv;
  gfloat min_phase;
  gfloat w_half, h_half;
  gfloat x0, y0;
  cairo_matrix_t save_matrix;
  complex phi_step, phi0;
  complex phi_j, phi_i;
  guint i, j, ndx;
  const double dashes[] = {4.};

  cairo_set_source_rgb(cr, 0, 0, 0);
  cairo_paint(cr);
  cairo_set_source_rgb(cr, 1, 1, 0);

  if (mtx->order > 0) {
    phi0 = cexp(I * M_PI / mtx->order);
    phi_step = phi0 * phi0;
    w_half = mtx->width / 2;
    h_half = mtx->height / 2;
    phi_j = phi0;

    ndx = 0;

    cairo_get_matrix(cr, &save_matrix);
    cairo_translate(cr, w_half, h_half);
    cairo_scale(cr, w_half / h_half, 1);
    cairo_translate(cr, -w_half, -h_half);
    cairo_new_path(cr);

    cairo_arc(
        cr,
        w_half,
        h_half,
        SUGTK_TRANS_MTX_GRAPH_REL_RADIUS * h_half,
        0,
        2 * M_PI);

    cairo_set_matrix(cr, &save_matrix);

    cairo_set_line_width(cr, 1);
    cairo_set_source_rgb(cr, .5, .5, .5);
    cairo_set_dash(cr, dashes, 1, 0);

    cairo_stroke(cr);

    for (j = 0; j < mtx->order; ++j) {
      /* Draw decision thresholds */
      phi_j *= phi0;
      x0 = w_half * (1 + 2 * cimag(phi_j));
      y0 = h_half * (1 + 2 * creal(phi_j));

      cairo_set_line_width(cr, 1);
      cairo_set_source_rgb(cr, .5, .5, .5);
      cairo_set_dash(cr, dashes, 1, 0);
      cairo_move_to(cr, x0, y0);
      cairo_line_to(cr, w_half, h_half);
      cairo_stroke(cr);

      /* Draw state transition edges */
      phi_j *= phi0;
      x0 = w_half * (1 + SUGTK_TRANS_MTX_GRAPH_REL_RADIUS * cimag(phi_j));
      y0 = h_half * (1 + SUGTK_TRANS_MTX_GRAPH_REL_RADIUS * creal(phi_j));

      if (mtx->coef[ndx] == 0) {
        ndx += mtx->order + 1;
        continue;
      }

      phi_i = phi0;
      count_inv = 1. / mtx->coef[ndx++];

      cairo_set_source_rgb(cr, 1, 1, 0);
      cairo_set_dash(cr, NULL, 0, 0);

      for (i = 0; i < mtx->order; ++i) {
        phi_i *= phi_step;
        p = count_inv * mtx->coef[ndx++];

        if (i != j) {
          /* Transition between different states: draw a line */
          cairo_set_line_width(cr, SUGTK_TRANS_MTX_GRAPH_LINE_WIDTH * p);
          cairo_move_to(cr, x0, y0);
          cairo_line_to(
              cr,
              w_half * (1 + SUGTK_TRANS_MTX_GRAPH_REL_RADIUS * cimag(phi_i)),
              h_half * (1 + SUGTK_TRANS_MTX_GRAPH_REL_RADIUS * creal(phi_i)));
          cairo_stroke(cr);
        } else {
          /* Transition to the same state: draw a circle */
          cairo_set_line_width(cr, 0);
          cairo_arc(
              cr,
              x0,
              y0,
              SUGTK_TRANS_MTX_GRAPH_LINE_WIDTH * p,
              0,
              2 * M_PI);
          cairo_fill(cr);
        }
      }
    }
  }
}

static void
sugtk_trans_mtx_draw_matrix(SuGtkTransMtx *mtx, cairo_t *cr)
{
  gfloat cwidth, cheight;
  gfloat p;
  gfloat count_inv;
  guint i, j, ndx;

  cairo_set_source_rgb(cr, 0, 0, 0);
  cairo_paint(cr);

  if (mtx->order > 0) {
    cwidth  = mtx->width / mtx->order;
    cheight = mtx->height / mtx->order;

    ndx = 0;
    for (j = 0; j < mtx->order; ++j) {
      /* First element of the row: number of occurrences */
      if (mtx->coef[ndx] == 0) {
        ndx += mtx->order + 1;
        continue;
      }

      count_inv = 1. / mtx->coef[ndx++];
      for (i = 0; i < mtx->order; ++i) {
        p = count_inv * mtx->coef[ndx++];
        cairo_set_source_rgb(cr, p, p, p);
        cairo_rectangle(cr, i * cwidth, j * cheight, cwidth, cheight);
        cairo_set_line_width(cr, 0);
        cairo_stroke_preserve(cr);
        cairo_fill(cr);
      }
    }
  }
}

static void
sugtk_trans_mtx_redraw(SuGtkTransMtx *mtx)
{
  cairo_t *cr;

  cr = cairo_create(mtx->surface);

  cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);

  if (mtx->graph_mode)
    return sugtk_trans_mtx_draw_graph(mtx, cr);
  else
    return sugtk_trans_mtx_draw_matrix(mtx, cr);

  cairo_destroy(cr);
}

void
sugtk_trans_mtx_refresh_hard(SuGtkTransMtx *mtx)
{
  sugtk_trans_mtx_redraw(mtx);
  gtk_widget_queue_draw(GTK_WIDGET(mtx));
}

void
sugtk_trans_mtx_refresh(SuGtkTransMtx *mtx)
{
  struct timeval tv, sub;

  gettimeofday(&tv, NULL);
  timersub(&tv, &mtx->last_redraw_time, &sub);

  if (sub.tv_usec > SUGTK_TRANS_MTX_MIN_REDRAW_INTERVAL_MS * 1000) {
    sugtk_trans_mtx_refresh_hard(mtx);
    mtx->last_redraw_time = tv;
  }
}

void
sugtk_trans_mtx_clear(SuGtkTransMtx *mtx)
{
  if (mtx->coef != NULL) {
    g_free(mtx->coef);
    mtx->coef = NULL;
  }

  mtx->order = 0;
}

void
sugtk_trans_mtx_reset(SuGtkTransMtx *mtx)
{
  if (mtx->order > 0)
    memset(mtx->coef, 0, sizeof(guint) * mtx->order * (mtx->order + 1));
}

void
sugtk_trans_mtx_feed(SuGtkTransMtx *mtx, uint8_t data)
{
  guint i;

  if (data >= mtx->order) {
    g_warning(
        "Invalid symbol #%d for a constellation with order %d\n",
        data,
        mtx->order);
    return;
  }

  i = mtx->prev * (mtx->order + 1);
  ++mtx->coef[i]; /* Increment the number of elements in this row */
  ++mtx->coef[i + data + 1]; /* Increment the occurences of this transition */

  mtx->prev = data;

  sugtk_trans_mtx_refresh_hard(mtx);
}

static void
sugtk_trans_mtx_dispose(GObject* object)
{
  SuGtkTransMtx *mtx;

  mtx = SUGTK_TRANS_MTX(object);

  sugtk_trans_mtx_clear(mtx);

  if (mtx->surface != NULL) {
    cairo_surface_destroy(mtx->surface);
    mtx->surface = NULL;
  }

  G_OBJECT_CLASS(sugtk_trans_mtx_parent_class)->dispose(object);
}

static void
sugtk_trans_mtx_class_init(SuGtkTransMtxClass *class)
{
  GObjectClass  *g_object_class;

  g_object_class = G_OBJECT_CLASS(class);

  g_object_class->dispose = sugtk_trans_mtx_dispose;
}

static gboolean
sugtk_trans_mtx_on_configure_event(
    GtkWidget *widget,
    GdkEventConfigure *event,
    gpointer data)
{
  SuGtkTransMtx *mtx = SUGTK_TRANS_MTX(widget);

  mtx->width  = event->width;
  mtx->height = event->height;

  if (mtx->surface != NULL)
    cairo_surface_destroy(mtx->surface);

  mtx->surface = gdk_window_create_similar_surface(
      gtk_widget_get_window(widget),
      CAIRO_CONTENT_COLOR,
      event->width,
      event->height);

  sugtk_trans_mtx_refresh_hard(mtx);

  return TRUE;
}

static gboolean
sugtk_trans_mtx_on_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
  SuGtkTransMtx *mtx = SUGTK_TRANS_MTX(widget);

  cairo_set_source_surface(cr, mtx->surface, 0, 0);
  cairo_paint(cr);

  return FALSE;
}

static gboolean
sugtk_trans_mtx_on_button_press_event(
    GtkWidget *widget,
    GdkEvent *event,
    gpointer data)
{
  SuGtkTransMtx *mtx = SUGTK_TRANS_MTX(widget);

  /* Toggle graph mode if requested */
  if (event->button.button == GDK_BUTTON_PRIMARY) {
    mtx->graph_mode = !mtx->graph_mode;
    sugtk_trans_mtx_refresh_hard(mtx);
  }

  return TRUE;
}

static void
sugtk_trans_mtx_init(SuGtkTransMtx *self)
{
  gtk_widget_set_events(
      GTK_WIDGET(self),
      GDK_EXPOSURE_MASK | GDK_BUTTON_PRESS_MASK);

  g_signal_connect(
      self,
      "configure-event",
      (GCallback) sugtk_trans_mtx_on_configure_event,
      NULL);

  g_signal_connect(
      self,
      "draw",
      (GCallback) sugtk_trans_mtx_on_draw,
      NULL);

  g_signal_connect(
      self,
      "button-press-event",
      (GCallback) sugtk_trans_mtx_on_button_press_event,
      NULL);
}

void
sugtk_trans_mtx_set_order(SuGtkTransMtx *mtx, guint order)
{
  guint *coef = NULL;

  if (order != mtx->order) {
    /* If order is 0, coef is NULL. This is intended */
    coef = g_malloc0(sizeof(guint) * order * (order + 1));

    sugtk_trans_mtx_clear(mtx);

    mtx->coef = coef;
    mtx->order = order;
    mtx->prev = 0;

    sugtk_trans_mtx_refresh_hard(mtx);
  }
}

GtkWidget *
sugtk_trans_mtx_new(void)
{
  return (GtkWidget *) g_object_new(SUGTK_TYPE_TRANS_MTX, NULL);
}

