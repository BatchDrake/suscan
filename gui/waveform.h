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

#ifndef _GUI_WAVEFORM_H
#define _GUI_WAVEFORM_H

#include <glib-object.h>
#include <gtk/gtk.h>
#include <util.h>
#include <stdint.h>
#include <sys/time.h>
#include <complex.h>
#include <math.h>

G_BEGIN_DECLS

#define SUGTK_WAVEFORM_STRIDE_ALIGN sizeof(gpointer)
#define SUGTK_WAVEFORM_HISTORY                4096
#define SUGTK_WAVEFORM_DRAW_THRESHOLD         16
#define SUGTK_WAVEFORM_MIN_REDRAW_INTERVAL_MS 40 /* 25 fps */

#define SUGTK_TYPE_WAVEFORM            (sugtk_waveform_get_type ())
#define SUGTK_WAVEFORM(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SUGTK_TYPE_WAVEFORM, SuGtkWaveForm))
#define SUGTK_WAVEFORM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST  ((klass), SUGTK_TYPE_WAVEFORM, SuGtkWaveFormClass))
#define SUGTK_IS_WAVEFORM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SUGTK_TYPE_WAVEFORM))
#define SUGTK_IS_WAVEFORM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE  ((klass), SUGTK_TYPE_WAVEFORM))
#define SUGTK_WAVEFORM_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS  ((obj), SUGTK_TYPE_WAVEFORM, SuGtkWaveFormClass))

typedef complex double gcomplex;

struct _SuGtkWaveForm
{
  GtkDrawingArea parent_instance;
  cairo_surface_t *sf_waveform; /* Mainly used for caching */

  gfloat width;
  gfloat height;

  gfloat zoom_t;

  gfloat history[SUGTK_WAVEFORM_HISTORY];

  GdkRGBA fg_color;
  GdkRGBA bg_color;
  GdkRGBA axes_color;

  guint p;

  struct timeval last_redraw_time;
  guint last_drawn;
  guint count;
};

struct _SuGtkWaveFormClass
{
  GtkDrawingAreaClass parent_class;
};

typedef struct _SuGtkWaveForm      SuGtkWaveForm;
typedef struct _SuGtkWaveFormClass SuGtkWaveFormClass;

GType sugtk_waveform_get_type(void);
GtkWidget *sugtk_waveform_new(void);

/* Reset contents */
void sugtk_waveform_reset(SuGtkWaveForm *mtx);

/* Append symbol */
void sugtk_waveform_push(SuGtkWaveForm *mtx, gfloat sample);

/* This is what actually triggers the redraw */
void sugtk_waveform_commit(SuGtkWaveForm *mtx);

/* Generic look & feel methods */
void sugtk_waveform_set_fg_color(
    SuGtkWaveForm *waveform,
    GdkRGBA color);

void sugtk_waveform_set_bg_color(
    SuGtkWaveForm *waveform,
    GdkRGBA color);

void sugtk_waveform_set_axes_color(
    SuGtkWaveForm *waveform,
    GdkRGBA color);

G_END_DECLS

#endif /* _GUI_WAVEFORM_H */
