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

#ifndef _GUI_HISTOGRAM_H
#define _GUI_HISTOGRAM_H

#include <glib-object.h>
#include <gtk/gtk.h>
#include <sigutils/decider.h>
#include <util.h>
#include <stdint.h>
#include <sys/time.h>
#include <complex.h>
#include <math.h>

G_BEGIN_DECLS

#define SUGTK_HISTOGRAM_STRIDE_ALIGN sizeof(gpointer)
#define SUGTK_HISTOGRAM_LENGTH                 1024
#define SUGTK_HISTOGRAM_DRAW_THRESHOLD         16
#define SUGTK_HISTOGRAM_MIN_REDRAW_INTERVAL_MS 200 /* 5 fps */

#define SUGTK_TYPE_HISTOGRAM            (sugtk_histogram_get_type ())
#define SUGTK_HISTOGRAM(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SUGTK_TYPE_HISTOGRAM, SuGtkHistogram))
#define SUGTK_HISTOGRAM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST  ((klass), SUGTK_TYPE_HISTOGRAM, SuGtkHistogramClass))
#define SUGTK_IS_HISTOGRAM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SUGTK_TYPE_HISTOGRAM))
#define SUGTK_IS_HISTOGRAM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE  ((klass), SUGTK_TYPE_HISTOGRAM))
#define SUGTK_HISTOGRAM_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS  ((obj), SUGTK_TYPE_HISTOGRAM, SuGtkHistogramClass))
#define SUGTK_HISTOGRAM_FLOOR_FRAC      -.9
#define SUGTK_HISTOGRAM_CEIL_FRAC       .9
#define SUGTK_HISTOGRAM_HEIGHT          (SUGTK_HISTOGRAM_CEIL_FRAC - SUGTK_HISTOGRAM_FLOOR_FRAC)

typedef complex double gcomplex;

struct _SuGtkHistogram
{
  GtkDrawingArea parent_instance;
  cairo_surface_t *sf_histogram; /* Mainly used for caching */

  gfloat width;
  gfloat height;

  gfloat zoom_x;
  struct sigutils_decider_params decider_params;
  guint  levels;
  gfloat max;
  gfloat frac;
  gfloat h_inv;
  gfloat bins[SUGTK_HISTOGRAM_LENGTH];

  /* Range selection */
  gboolean selecting;
  gboolean selection;
  gfloat last_x;
  gfloat sel_min;
  gfloat sel_max;

  /* Menu */
  GtkMenu *deciderMenu;
  GtkMenuItem *setDecider;
  GtkMenuItem *resetDecider;

  GdkRGBA fg_color;
  GdkRGBA bg_color;
  GdkRGBA axes_color;

  struct timeval last_redraw_time;
  guint last_drawn;
  guint count;
  guint reset;
  guint min_count; /* Keep this resolution */
};

struct _SuGtkHistogramClass
{
  GtkDrawingAreaClass parent_class;

  int sig_set_decider;
};

typedef struct _SuGtkHistogram      SuGtkHistogram;
typedef struct _SuGtkHistogramClass SuGtkHistogramClass;

GType sugtk_histogram_get_type(void);
GtkWidget *sugtk_histogram_new(void);

/* Reset contents */
void sugtk_histogram_reset(SuGtkHistogram *hist);

/* Append symbol */
void sugtk_histogram_push(SuGtkHistogram *hist, gfloat sample);

/* This is what actually triggers the redraw */
void sugtk_histogram_commit(SuGtkHistogram *hist);

/* To draw decision levels */
void sugtk_histogram_set_decider_params(
    SuGtkHistogram *hist,
    const struct sigutils_decider_params *params);

/* Generic look & feel methods */
void sugtk_histogram_set_fg_color(
    SuGtkHistogram *histogram,
    GdkRGBA color);

void sugtk_histogram_set_bg_color(
    SuGtkHistogram *histogram,
    GdkRGBA color);

void sugtk_histogram_set_axes_color(
    SuGtkHistogram *histogram,
    GdkRGBA color);

G_END_DECLS

#endif /* _GUI_HISTOGRAM_H */
