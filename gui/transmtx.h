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

#ifndef _GUI_TRANSMTX_H
#define _GUI_TRANSMTX_H

#include <glib-object.h>
#include <gtk/gtk.h>
#include <util.h>
#include <stdint.h>
#include <sys/time.h>

G_BEGIN_DECLS

#define SUGTK_TRANS_MTX_STRIDE_ALIGN sizeof(gpointer)
#define SUGTK_TRANS_MTX_MIN_REDRAW_INTERVAL_MS 40 /* 25 fps */

#define SUGTK_TYPE_TRANS_MTX            (sugtk_trans_mtx_get_type ())
#define SUGTK_TRANS_MTX(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SUGTK_TYPE_TRANS_MTX, SuGtkTransMtx))
#define SUGTK_TRANS_MTX_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST  ((klass), SUGTK_TYPE_TRANS_MTX, SuGtkTransMtxClass))
#define SUGTK_IS_TRANS_MTX(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SUGTK_TYPE_TRANS_MTX))
#define SUGTK_IS_TRANS_MTX_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE  ((klass), SUGTK_TYPE_TRANS_MTX))
#define SUGTK_TRANS_MTX_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS  ((obj), SUGTK_TYPE_TRANS_MTX, SuGtkTransMtxClass))

struct _SuGtkTransMtx
{
  GtkDrawingArea parent_instance;

  guint order;
  guint *coef;

  gfloat width;
  gfloat height;

  gboolean graph_mode;

  /* Previous state */
  uint8_t prev;

  /* Surface of off-screen rendering */
  cairo_surface_t *surface;
  struct timeval last_redraw_time;
};

struct _SuGtkTransMtxClass
{
  GtkDrawingAreaClass parent_class;
};

typedef struct _SuGtkTransMtx      SuGtkTransMtx;
typedef struct _SuGtkTransMtxClass SuGtkTransMtxClass;

GType sugtk_trans_mtx_get_type(void);
GtkWidget *sugtk_trans_mtx_new(void);

/* Reset coefficients */
void sugtk_trans_mtx_reset(SuGtkTransMtx *mtx);

/* Set constellation order. Order 0: disables view */
void sugtk_trans_mtx_set_order(SuGtkTransMtx *mtx, guint order);
guint sugtk_trans_mtx_get_order(const SuGtkTransMtx *mtx);

/* Append symbol */
void sugtk_trans_mtx_push(SuGtkTransMtx *mtx, uint8_t state);
void sugtk_trans_mtx_commit(SuGtkTransMtx *mtx);

G_END_DECLS

#endif /* _GUI_TRANSMTX_H */
