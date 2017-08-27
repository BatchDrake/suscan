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
#include "symview.h"

G_DEFINE_TYPE(SuGtkSymView, sugtk_sym_view, GTK_TYPE_DRAWING_AREA);

void
sugtk_sym_view_clear(SuGtkSymView *view)
{
  if (view->data_buf != NULL) {
    free(view->data_buf);
    view->data_buf = NULL;
  }

  view->data_alloc = 0;
  view->data_size = 0;
  view->window_offset = 0;
}

static gboolean
sugtk_sym_view_append_internal(SuGtkSymView *view, uint8_t data)
{
  uint8_t *tmp = NULL;
  guint new_alloc;
  gboolean ok = FALSE;

  if (view->data_alloc <= view->data_size) {
    if (view->data_alloc > 0)
      new_alloc = view->data_alloc << 1;
    else
      new_alloc = 1;

    if ((tmp = realloc(view->data_buf, new_alloc)) == NULL)
      goto fail;

    view->data_alloc = new_alloc;
    view->data_buf = tmp;
    tmp = NULL;
  }

  view->data_buf[view->data_size++] = data;

  ok = TRUE;

fail:
  if (tmp != NULL)
    free(tmp);

  return ok;
}

static guint
sugtk_sym_view_get_height(SuGtkSymView *view)
{
  return gtk_widget_get_allocated_height(GTK_WIDGET(view))
        / view->window_zoom;
}

gboolean
sugtk_sym_view_append(SuGtkSymView *view, uint8_t data)
{
  guint i;
  guint width;
  guint height;

  for (i = 0; i < SUGTK_SYM_VIEW_STRIDE_ALIGN; ++i)
    if (!sugtk_sym_view_append_internal(view, data))
      return FALSE;

  if (view->autoscroll) {
    width = SUGTK_SYM_VIEW_STRIDE_ALIGN * view->window_width;
    height = sugtk_sym_view_get_height(view);

    if (width * height < view->data_size)
      view->window_offset =
          width * (1 + view->data_size / width - height)
          / SUGTK_SYM_VIEW_STRIDE_ALIGN;
  }

  return TRUE;
}

void
sugtk_sym_view_set_autoscroll(SuGtkSymView *view, gboolean value)
{
  view->autoscroll = value;
}

void
sugtk_sym_view_set_autofit(SuGtkSymView *view, gboolean value)
{
  view->autofit = value;

  if (value)
    sugtk_sym_view_set_width(
        view,
        gtk_widget_get_allocated_width(GTK_WIDGET(view)) / view->window_zoom);
}

gboolean
sugtk_sym_view_set_width(SuGtkSymView *view, guint width)
{
  if (width < 1)
    return FALSE;

  view->window_width = width;

  return TRUE;
}

guint
sugtk_sym_view_get_width(const SuGtkSymView *view)
{
  return view->window_width;
}

gboolean
sugtk_sym_view_set_zoom(SuGtkSymView *view, guint zoom)
{
  if (zoom < 1)
    return FALSE;

  view->window_zoom = zoom;

  if (view->autofit)
    sugtk_sym_view_set_width(
        view,
        gtk_widget_get_allocated_width(GTK_WIDGET(view)) / view->window_zoom);

  return TRUE;
}

guint
sugtk_sym_view_get_zoom(const SuGtkSymView *view)
{
  return view->window_zoom;
}

gboolean
sugtk_sym_view_set_offset(SuGtkSymView *view, guint offset)
{
  if (offset >= view->data_size)
    return FALSE;

  view->window_offset = offset;

  return TRUE;
}

guint
sugtk_sym_view_get_offset(const SuGtkSymView *view)
{
  return view->window_offset;
}

const uint8_t *
sugtk_sym_get_buffer_bytes(const SuGtkSymView *view)
{
  return view->data_buf;
}

size_t
sugtk_sym_get_buffer_size(const SuGtkSymView *view)
{
  return view->data_size;
}

static void
sugtk_sym_view_dispose(GObject* object)
{
  sugtk_sym_view_clear(SUGTK_SYM_VIEW(object));
  G_OBJECT_CLASS(sugtk_sym_view_parent_class)->dispose(object);
}

static void
sugtk_sym_view_class_init(SuGtkSymViewClass *class)
{
  GObjectClass  *g_object_class;

  g_object_class = G_OBJECT_CLASS(class);

  g_object_class->dispose = sugtk_sym_view_dispose;
}

static gboolean
sugtk_sym_view_on_configure_event(
    GtkWidget *widget,
    GdkEventConfigure *event,
    gpointer data)
{
  SuGtkSymView *view = SUGTK_SYM_VIEW(widget);

  if (view->autofit)
    sugtk_sym_view_set_width(view, event->width / view->window_zoom);

  return TRUE;
}

static gboolean
suscan_constellation_on_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
  SuGtkSymView *view = SUGTK_SYM_VIEW(widget);
  cairo_surface_t *surface;
  guint stride;
  guint height;
  guint width;
  guint tail = 0;
  guint offset;

  /* Precedence is imortant here */
  width = SUGTK_SYM_VIEW_STRIDE_ALIGN * view->window_width;
  height = sugtk_sym_view_get_height(view);
  stride = cairo_format_stride_for_width(CAIRO_FORMAT_A8, width);

  cairo_set_source_rgb(cr, 0, 0, 0);
  cairo_paint(cr);

  cairo_scale(
      cr,
      view->window_zoom / (gdouble) SUGTK_SYM_VIEW_STRIDE_ALIGN,
      view->window_zoom);

  offset = SUGTK_SYM_VIEW_STRIDE_ALIGN * view->window_offset;

  if (offset < view->data_size) {
    if (width * height + offset > view->data_size) {
      height = (view->data_size - offset) / width;
      tail = view->data_size - offset - width * height;
    }

    if (height > 0) {
      surface = cairo_image_surface_create_for_data(
          view->data_buf + offset,
          CAIRO_FORMAT_A8,
          width,
          height,
          stride);

      /* Paint background */
      cairo_set_source_rgb(cr, 1, 1, 1);
      cairo_rectangle(cr, 0, 0, width, height);
      cairo_set_line_width(cr, 0);
      cairo_stroke_preserve(cr);
      cairo_fill(cr);

      /* Apply pixels */
      cairo_set_source_surface(cr, surface, 0, 0);
      cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
      cairo_paint(cr);

      /* Destroy surface */
      cairo_surface_destroy(surface);
    }

    if (tail > 0) {
      surface = cairo_image_surface_create_for_data(
          view->data_buf + width * height + offset,
          CAIRO_FORMAT_A8,
          tail,
          1,
          stride);

      /* Paint background */
      cairo_set_source_rgb(cr, 1, 1, 1);
      cairo_rectangle(cr, 0, height, tail, 1);
      cairo_set_line_width(cr, 0);
      cairo_stroke_preserve(cr);
      cairo_fill(cr);

      /* Apply pixels */
      cairo_set_source_surface(cr, surface, 0, height);
      cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
      cairo_paint(cr);

      /* Destroy surface */
      cairo_surface_destroy(surface);
    }
  }

  return TRUE;
}

static void
sugtk_sym_view_init(SuGtkSymView *self)
{
  self->data_alloc = 0;
  self->data_size = 0;
  self->data_buf = NULL;

  self->autoscroll = TRUE;
  self->autofit = TRUE;
  self->window_zoom = 1;

  g_signal_connect(
      self,
      "configure-event",
      (GCallback) sugtk_sym_view_on_configure_event,
      NULL);

  g_signal_connect(
      self,
      "draw",
      (GCallback) suscan_constellation_on_draw,
      NULL);

}

GtkWidget *
sugtk_sym_view_new(void)
{
  return (GtkWidget *) g_object_new(SUGTK_TYPE_SYM_VIEW, NULL);
}

