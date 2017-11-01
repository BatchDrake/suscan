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
#include "transmtx.h"

G_DEFINE_TYPE(SuGtkTransMtx, sugtk_trans_mtx, GTK_TYPE_DRAWING_AREA);

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
}

static void
sugtk_trans_mtx_dispose(GObject* object)
{
  SuGtkTransMtx *mtx;

  mtx = SUGTK_TRANS_MTX(object);

  sugtk_trans_mtx_clear(mtx);

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

  return TRUE;
}

static gboolean
suscan_constellation_on_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
  SuGtkTransMtx *mtx = SUGTK_TRANS_MTX(widget);
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

  return TRUE;
}

static void
sugtk_trans_mtx_init(SuGtkTransMtx *self)
{
  gtk_widget_set_events(GTK_WIDGET(self), GDK_EXPOSURE_MASK);

  g_signal_connect(
      self,
      "configure-event",
      (GCallback) sugtk_trans_mtx_on_configure_event,
      NULL);

  g_signal_connect(
      self,
      "draw",
      (GCallback) suscan_constellation_on_draw,
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
  }
}

GtkWidget *
sugtk_trans_mtx_new(void)
{
  return (GtkWidget *) g_object_new(SUGTK_TYPE_TRANS_MTX, NULL);
}

