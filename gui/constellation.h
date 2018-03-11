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

#ifndef _GUI_CONSTELLATION_H
#define _GUI_CONSTELLATION_H

#include <glib-object.h>
#include <gtk/gtk.h>
#include <util.h>
#include <stdint.h>
#include <sys/time.h>
#include <complex.h>
#include <math.h>

G_BEGIN_DECLS

#define SUGTK_CONSTELLATION_STRIDE_ALIGN sizeof(gpointer)
#define SUGTK_CONSTELLATION_HISTORY                200
#define SUGTK_CONSTELLATION_DRAW_THRESHOLD         16
#define SUGTK_CONSTELLATION_MIN_REDRAW_INTERVAL_MS 40 /* 25 fps */

#define SUGTK_TYPE_CONSTELLATION            (sugtk_constellation_get_type ())
#define SUGTK_CONSTELLATION(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SUGTK_TYPE_CONSTELLATION, SuGtkConstellation))
#define SUGTK_CONSTELLATION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST  ((klass), SUGTK_TYPE_CONSTELLATION, SuGtkConstellationClass))
#define SUGTK_IS_CONSTELLATION(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SUGTK_TYPE_CONSTELLATION))
#define SUGTK_IS_CONSTELLATION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE  ((klass), SUGTK_TYPE_CONSTELLATION))
#define SUGTK_CONSTELLATION_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS  ((obj), SUGTK_TYPE_CONSTELLATION, SuGtkConstellationClass))

typedef complex double gcomplex;

struct _SuGtkConstellation
{
  GtkDrawingArea parent_instance;
  cairo_surface_t *sf_constellation; /* Mainly used for caching */

  gfloat width;
  gfloat height;

  gcomplex phase;
  gcomplex history[SUGTK_CONSTELLATION_HISTORY];

  guint p;

  struct timeval last_redraw_time;
  guint last_drawn;
  guint count;
};

struct _SuGtkConstellationClass
{
  GtkDrawingAreaClass parent_class;
};

typedef struct _SuGtkConstellation      SuGtkConstellation;
typedef struct _SuGtkConstellationClass SuGtkConstellationClass;

GType sugtk_constellation_get_type(void);
GtkWidget *sugtk_constellation_new(void);

/* Reset contents */
void sugtk_constellation_reset(SuGtkConstellation *mtx);

/* Append symbol */
void sugtk_constellation_push(SuGtkConstellation *mtx, gcomplex sample);

/* This is what actually triggers the redraw */
void sugtk_constellation_commit(SuGtkConstellation *mtx);

G_END_DECLS

#endif /* _GUI_CONSTELLATION_H */
