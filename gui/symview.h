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

#ifndef _GUI_SYMVIEW_H
#define _GUI_SYMVIEW_H

#include <glib-object.h>
#include <gtk/gtk.h>
#include <stdint.h>

G_BEGIN_DECLS

#define SUGTK_SYM_VIEW_STRIDE_ALIGN sizeof(gpointer)

#define SUGTK_TYPE_SYM_VIEW            (sugtk_sym_view_get_type ())
#define SUGTK_SYM_VIEW(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SUGTK_TYPE_SYM_VIEW, SuGtkSymView))
#define SUGTK_SYM_VIEW_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST  ((klass), SUGTK_TYPE_SYM_VIEW, SuGtkSymViewClass))
#define SUGTK_IS_SYM_VIEW(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SUGTK_TYPE_SYM_VIEW))
#define SUGTK_IS_SYM_VIEW_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE  ((klass), SUGTK_TYPE_SYM_VIEW))
#define SUGTK_SYM_VIEW_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS  ((obj), SUGTK_TYPE_SYM_VIEW, SuGtkSymViewClass))

struct _SuGtkSymView
{
  GtkDrawingArea parent_instance;

  guint window_width;
  guint window_offset;
  guint window_zoom;

  uint8_t *data_buf;
  guint data_size;
  guint data_alloc;

  gboolean autofit;
  gboolean autoscroll;
};

struct _SuGtkSymViewClass
{
  GtkDrawingAreaClass parent_class;
};

typedef struct _SuGtkSymView      SuGtkSymView;
typedef struct _SuGtkSymViewClass SuGtkSymViewClass;

GType sugtk_sym_view_get_type(void);
GtkWidget *sugtk_sym_view_new(void);
void sugtk_sym_view_clear(SuGtkSymView *view);

gboolean sugtk_sym_view_set_zoom(SuGtkSymView *view, guint zoom);
guint sugtk_sym_view_get_zoom(const SuGtkSymView *view);

gboolean sugtk_sym_view_set_width(SuGtkSymView *view, guint width);
guint sugtk_sym_view_get_width(const SuGtkSymView *view);

gboolean sugtk_sym_view_set_offset(SuGtkSymView *view, guint offset);
guint sugtk_sym_view_get_offset(const SuGtkSymView *view);

gboolean sugtk_sym_view_append(SuGtkSymView *view, uint8_t data);
void sugtk_sym_view_set_autoscroll(SuGtkSymView *view, gboolean value);
void sugtk_sym_view_set_autofit(SuGtkSymView *view, gboolean value);

G_END_DECLS

#endif /* _SYMVIEW_H */
