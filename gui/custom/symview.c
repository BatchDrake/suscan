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
#include "symview.h"

G_DEFINE_TYPE(SuGtkSymView, sugtk_sym_view, GTK_TYPE_DRAWING_AREA);

static gchar *
sugtk_sym_view_apply_berlekamp_massey(
    const SuGtkSymView *view,
    guint *len,
    gboolean inv)
{
  guint start, end;
  guint size;

  gchar *b = NULL;
  gchar *c = NULL;
  gchar *t = NULL;
  gchar *result = NULL;

  gint ibit = !!inv;
  gint i;
  gint d;
  gint N, p;
  gint L = 0;
  gint m = -1;

  /* Work only if a selection is available */
  if (sugtk_sym_view_get_selection(view, &start, &end)) {
    size = end - start;

    if ((b = g_malloc0(size)) == NULL)
      goto done;

    if ((c = g_malloc0(size)) == NULL)
      goto done;

    if ((t = g_malloc(size)) == NULL)
      goto done;

    b[0] = 1;
    c[0] = 1;

    /* TODO: Add support for non-binary symbols */
    p = start * SUGTK_SYM_VIEW_STRIDE_ALIGN;
    for (N = 0; N < size; ++N, p += SUGTK_SYM_VIEW_STRIDE_ALIGN) {
      d = (view->data_buf[p] & 1) ^ ibit;
      for (i = 1; i <= L; ++i)
        d ^= c[i]
          & ((view->data_buf[p - i * SUGTK_SYM_VIEW_STRIDE_ALIGN] & 1) ^ ibit);

      /* Discrepancy is found! */
      if (d) {
        memcpy(t, c, size); /* Save copy of C */

        for (i = N - m; i < size; ++i)
          c[i] ^= b[i - (N - m)];

        if (L <= N / 2) {
          L = N + 1 - L;
          m = N;
          memcpy(b, t, size);
        }
      }
    }

    result = c;
    c = NULL;
    *len = L;
  }

done:
  if (b != NULL)
    g_free(b);

  if (c != NULL)
    g_free(c);

  if (t != NULL)
    g_free(t);

  return result;
}

static void
sugtk_sym_view_clear_internal(SuGtkSymView *view)
{
  if (view->data_buf != NULL) {
    free(view->data_buf);
    view->data_buf = NULL;
  }

  view->data_alloc = 0;
  view->data_size = 0;
  view->window_offset = 0;
}

static void
sugtk_sym_view_redraw(SuGtkSymView *view)
{
  cairo_surface_t *surface;
  cairo_t *cr;
  guint stride;
  guint height;
  guint width;
  guint tail = 0;
  guint offset;

  guint sel_start, sel_end;
  guint sel_x0, sel_y0;
  guint sel_x1, sel_y1;
  guint sel_size;
  guint sel_width, sel_tmp;

  gboolean selection = FALSE;

  cr = cairo_create(view->surface);

  cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);

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

  offset    = SUGTK_SYM_VIEW_STRIDE_ALIGN * view->window_offset;

  /* Prepare selection parameters (if something is selected) */
  if (view->selection) {
    sel_start = SUGTK_SYM_VIEW_STRIDE_ALIGN * view->sel_off0;
    sel_end   = SUGTK_SYM_VIEW_STRIDE_ALIGN * view->sel_off1;

    if (sel_start > sel_end) {
      sel_tmp = sel_start;
      sel_start = sel_end;
      sel_end = sel_tmp;
    }

    if (sel_start < width * height + offset && sel_end >= offset) {
      selection = TRUE;

      if (sel_start < offset)
        sel_start = offset;

      if (sel_end > width * height + offset)
        sel_end = width * height + offset;

      sel_start -= offset;
      sel_end -= offset;
    }
  }

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

  /* Paint selection */
  if (selection) {
    sel_x0 = sel_start % width;
    sel_y0 = sel_start / width;

    sel_x1 = sel_end % width;
    sel_y1 = sel_end / width;

    sel_size = sel_end - sel_start + 1;

    cairo_set_source_rgba(cr, 0, 0, 1, .5);

    if (sel_x0 > 0) {
      if (sel_size + sel_x0 < width)
        sel_width = sel_size;
      else
        sel_width = width - sel_x0;

      cairo_rectangle(cr, sel_x0, sel_y0, sel_width, 1);
      cairo_fill(cr);

      ++sel_y0;
      sel_size -= sel_width;
    }

    if (sel_y1 > sel_y0) {
      cairo_rectangle(cr, 0, sel_y0, width, sel_y1 - sel_y0);
      cairo_fill(cr);

      sel_size -= (sel_y1 - sel_y0) * width;
    }

    if (sel_size > 0) {
      cairo_rectangle(cr, 0, sel_y1, sel_size, 1);
      cairo_fill(cr);
    }
  }

  cairo_destroy(cr);
}

void
sugtk_sym_view_refresh_hard(SuGtkSymView *view)
{
  if (view->reshaped) {
    g_signal_emit(view, SUGTK_SYM_VIEW_GET_CLASS(view)->sig_reshape, 0);
    view->reshaped = FALSE;
  }

  sugtk_sym_view_redraw(view);
  gtk_widget_queue_draw(GTK_WIDGET(view));
}

void
sugtk_sym_view_refresh(SuGtkSymView *view)
{
  struct timeval tv, sub;
  unsigned long long int ms;

  gettimeofday(&tv, NULL);
  timersub(&tv, &view->last_redraw_time, &sub);

  ms = sub.tv_usec / 1000 + sub.tv_sec * 1000;

  if (ms > SUGTK_SYM_VIEW_MIN_REDRAW_INTERVAL_MS) {
    sugtk_sym_view_refresh_hard(view);
    view->last_redraw_time = tv;
  }
}


void
sugtk_sym_view_clear(SuGtkSymView *view)
{
  sugtk_sym_view_clear_internal(view);

  sugtk_sym_view_refresh_hard(view);
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

guint
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

    if (width * height < view->data_size) {
      view->window_offset =
          width * (1 + view->data_size / width - height)
          / SUGTK_SYM_VIEW_STRIDE_ALIGN;

      view->reshaped = TRUE;
    }
  }

  sugtk_sym_view_refresh(view);

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

  if (view->window_width != width) {
    view->window_width = width;
    view->reshaped = TRUE;
    sugtk_sym_view_refresh_hard(view);
  }

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

  if (view->window_zoom != zoom) {
    view->window_zoom = zoom;

    if (view->autofit)
      sugtk_sym_view_set_width(
          view,
          gtk_widget_get_allocated_width(GTK_WIDGET(view)) / view->window_zoom);

    view->reshaped = TRUE;
    sugtk_sym_view_refresh_hard(view);
  }

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
  int max_offset;
  int32_t data_size;
  data_size = view->data_size / SUGTK_SYM_VIEW_STRIDE_ALIGN;

  max_offset = data_size
      - sugtk_sym_view_get_width(view) * (sugtk_sym_view_get_height(view) - 1);

  if (max_offset < 0)
    max_offset = 0;

  if (offset > (guint) max_offset)
    return FALSE;

  if (view->window_offset != offset) {
    view->window_offset = offset;
    view->reshaped = TRUE;
    sugtk_sym_view_refresh_hard(view);
  }

  return TRUE;
}

guint
sugtk_sym_view_get_offset(const SuGtkSymView *view)
{
  return view->window_offset;
}

const uint8_t *
sugtk_sym_view_get_buffer_bytes(const SuGtkSymView *view)
{
  return view->data_buf;
}

size_t
sugtk_sym_view_get_buffer_size(const SuGtkSymView *view)
{
  return view->data_size;
}

GtkMenu *
sugtk_sym_view_get_menu(const SuGtkSymView *view)
{
  return view->menu;
}

static void
sugtk_sym_view_dispose(GObject* object)
{
  SuGtkSymView *view;

  view = SUGTK_SYM_VIEW(object);

  sugtk_sym_view_clear_internal(view);

  /*
   * Remember: this function may be called several times on the
   * same object.
   */
  if (view->surface != NULL) {
    cairo_surface_destroy(view->surface);
    view->surface = NULL;
  }

  if (view->fft_plan != NULL) {
    fftw_destroy_plan(view->fft_plan);
    view->fft_plan = NULL;
  }

  if (view->fft_plan_rev != NULL) {
    fftw_destroy_plan(view->fft_plan_rev);
    view->fft_plan_rev = NULL;
  }

  if (view->fft_buf != NULL) {
    fftw_free(view->fft_buf);
    view->fft_buf = NULL;
  }

  G_OBJECT_CLASS(sugtk_sym_view_parent_class)->dispose(object);
}

static void
sugtk_sym_view_class_init(SuGtkSymViewClass *class)
{
  GObjectClass  *g_object_class;

  g_object_class = G_OBJECT_CLASS(class);

  g_object_class->dispose = sugtk_sym_view_dispose;

  class->sig_reshape =  g_signal_new(
      "reshape",
      G_TYPE_FROM_CLASS (g_object_class),
      G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS,
      0, /* class offset */
      NULL /* accumulator */,
      NULL /* accu_data */,
      NULL, /* marshaller */
      G_TYPE_NONE /* return_type */,
      0);     /* n_params */
}

static gboolean
sugtk_sym_view_on_configure_event(
    GtkWidget *widget,
    GdkEventConfigure *event,
    gpointer data)
{
  SuGtkSymView *view = SUGTK_SYM_VIEW(widget);

  if (GDK_IS_WINDOW(gtk_widget_get_window(widget))) {
    if (view->surface != NULL)
      cairo_surface_destroy(view->surface);

    view->surface = gdk_window_create_similar_surface(
        gtk_widget_get_window(widget),
        CAIRO_CONTENT_COLOR,
        event->width,
        event->height);

    if (view->autofit)
      sugtk_sym_view_set_width(view, event->width / view->window_zoom);

    sugtk_sym_view_refresh_hard(view);
  }

  return TRUE;
}

static gboolean
sugtk_sym_view_on_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
  SuGtkSymView *view = SUGTK_SYM_VIEW(widget);

  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_surface(cr, view->surface, 0, 0);
  cairo_paint(cr);

  return FALSE;
}

guint
sugtk_sym_view_coords_to_offset(const SuGtkSymView *widget, gfloat x, gfloat y)
{
  int32_t offset;
  int32_t data_size;
  data_size = widget->data_size / SUGTK_SYM_VIEW_STRIDE_ALIGN;

  x /= widget->window_zoom;
  y /= widget->window_zoom;

  if (x > widget->window_width)
    x = widget->window_width;

  offset = floor(x) + floor(y) * widget->window_width + widget->window_offset;

  if (offset >= data_size)
    offset = data_size - 1;
  if (offset < 0)
    offset = 0;

  return offset;
}

static gboolean
sugtk_sym_view_on_button_press_event(
    GtkWidget *widget,
    GdkEvent *event,
    gpointer data)
{
  SuGtkSymView *view = SUGTK_SYM_VIEW(widget);
  GtkMenu *menu;
  uint32_t offset;
  guint start, end;

  offset = sugtk_sym_view_coords_to_offset(
      view,
      event->motion.x,
      event->motion.y);

  switch (event->button.button) {
    case GDK_BUTTON_PRIMARY:
      view->selection = FALSE;
      view->sel_started = TRUE;
      view->sel_off0 = offset;
      view->sel_off1 = view->sel_off0;
      break;

    case GDK_BUTTON_SECONDARY:
      gtk_widget_set_sensitive(view->apply_bm, view->selection);
      gtk_menu_popup_at_pointer(view->menu, event);
      break;
  }

  return TRUE;
}

static gboolean
sugtk_sym_view_on_button_release_event(
    GtkWidget *widget,
    GdkEvent *event,
    gpointer data)
{
  SuGtkSymView *view = SUGTK_SYM_VIEW(widget);
  uint32_t offset;

  switch (event->button.button) {
    case GDK_BUTTON_PRIMARY:
      if (view->sel_started) {
        view->sel_started = FALSE;

        offset = sugtk_sym_view_coords_to_offset(
            view,
            event->motion.x,
            event->motion.y);

        if (view->sel_off0 == offset) {
          view->selection = FALSE;
          sugtk_sym_view_refresh(view);
        }
      }
      break;
  }

  return TRUE;
}

static gboolean
sugtk_sym_view_on_motion_notify_event(
    GtkWidget *widget,
    GdkEventMotion *event,
    gpointer data)
{
  SuGtkSymView *view = SUGTK_SYM_VIEW(widget);
  int32_t offset;

  if (view->sel_started) {
    offset = sugtk_sym_view_coords_to_offset(view, event->x, event->y);

    view->sel_off1 = offset;
    view->selection = TRUE;

    sugtk_sym_view_refresh_hard(view);
  }

  return TRUE;
}

static gboolean
sugtk_sym_view_poly_to_gbuf(grow_buf_t *gbuf, gchar *result, guint len)
{
  guint i;

  for (i = 0; i < len; ++i)
    if (result[i]) {
      if (len - i > 1) {
        if (grow_buf_append_printf(gbuf, "x<sup>%d</sup> + ", len - i) == -1)
          return FALSE;
      } else {
        if (grow_buf_append(gbuf, "x + ", 4) == -1)
          return FALSE;
      }
    }

  if (grow_buf_append(gbuf, "1", 1) == -1)
    return FALSE;

  return TRUE;
}

static void
sugtk_sym_view_on_bm(GtkWidget *widget, gpointer *data)
{
  SuGtkSymView *view = SUGTK_SYM_VIEW(data);
  grow_buf_t gbuf = grow_buf_INITIALIZER;
  GtkWidget *dialog = NULL;
  gchar *result_dir = NULL;
  gchar *result_inv = NULL;
  guint dir_len, inv_len;
  guint i;
  guint len;
  guint start, end;

  if (sugtk_sym_view_get_selection(view, &start, &end)) {
    result_dir = sugtk_sym_view_apply_berlekamp_massey(view, &dir_len, FALSE);
    result_inv = sugtk_sym_view_apply_berlekamp_massey(view, &inv_len, TRUE);
    len = end - start + 1;

    if (result_dir != NULL && result_inv != NULL) {
      if (grow_buf_append_printf(&gbuf, "Input length: %u\n", len) == -1)
        goto done;

      if (grow_buf_append_printf(&gbuf, "Direct sequence polynomial: ") == -1)
        goto done;

      if (!sugtk_sym_view_poly_to_gbuf(&gbuf, result_dir, dir_len))
        goto done;

      if (grow_buf_append_printf(&gbuf, "\nNegated sequence polynomial: ") == -1)
        goto done;

      if (!sugtk_sym_view_poly_to_gbuf(&gbuf, result_inv, inv_len))
        goto done;

      if (grow_buf_append_null(&gbuf) == -1)
        goto done;

      dialog = gtk_message_dialog_new(
          GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(view))),
          GTK_DIALOG_DESTROY_WITH_PARENT,
          GTK_MESSAGE_INFO,
          GTK_BUTTONS_CLOSE,
          NULL);

      gtk_window_set_title(GTK_WINDOW(dialog), "Berlekamp-Massey analysis");

      gtk_message_dialog_set_markup(
          GTK_MESSAGE_DIALOG(dialog),
          (gchar *) grow_buf_get_buffer(&gbuf));

      gtk_dialog_run(GTK_DIALOG(dialog));
    }
  }

done:
  if (result_dir != NULL)
    g_free(result_dir);

  if (result_inv != NULL)
    g_free(result_inv);

  if (dialog != NULL)
    gtk_widget_destroy(dialog);

  grow_buf_finalize(&gbuf);
}

static void
sugtk_sym_view_on_fac(GtkWidget *widget, gpointer *data)
{
  SuGtkSymView *view = SUGTK_SYM_VIEW(data);
  GtkWidget *dialog = NULL;
  gdouble inv;
  guint i;
  gint len;
  guint start, end;
  guint max_tau = 0;
  gfloat max = 0;
  char *msg = NULL;

  if (!sugtk_sym_view_get_selection(view, &start, &end)) {
    start = 0;
    end = view->data_size / SUGTK_SYM_VIEW_STRIDE_ALIGN - 1;
  }

  len = end - start + 1;

  if (len > 0) {
    if (len > SUGTK_SYM_VIEW_FFT_SIZE) {
      dialog = gtk_message_dialog_new(
          GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(view))),
          GTK_DIALOG_DESTROY_WITH_PARENT,
          GTK_MESSAGE_INFO,
          GTK_BUTTONS_YES_NO,
          "The selected symbol stream is too big (%d symbols) to be analyzed "
          "by fast autocorrelation (FAC). Only the last %d samples will be "
          "taken into account. Do you want to continue?",
          len,
          SUGTK_SYM_VIEW_FFT_SIZE);

      gtk_window_set_title(GTK_WINDOW(dialog), "Symbol autocorrelation");

      /* Abort */
      if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_YES)
        goto done;

      gtk_widget_destroy(dialog);
      dialog = NULL;

      start = end - SUGTK_SYM_VIEW_FFT_SIZE + 1;
      len = SUGTK_SYM_VIEW_FFT_SIZE;
    }

    memset(view->fft_buf, 0, SUGTK_SYM_VIEW_FFT_SIZE * sizeof (fftw_complex));

    inv = 1. / 128.0;
    for (i = 0; i < len; ++i)
      view->fft_buf[i] =
          ((int) view->data_buf[(i + start) * SUGTK_SYM_VIEW_STRIDE_ALIGN] - 128)
          * inv;

    /* Direct FFT */
    fftw_execute(view->fft_plan);

    for (i = 0; i < len; ++i)
      view->fft_buf[i] *= conj(view->fft_buf[i]);

    /* Inverse FFT */
    fftw_execute(view->fft_plan_rev);

    for (i = 1; i < len; ++i) {
      if (creal(view->fft_buf[i]) > max) {
        max = creal(view->fft_buf[i]);
        max_tau = i;
      }
    }

    if (max_tau > SUGTK_SYM_VIEW_FFT_SIZE / 2)
      max_tau = SUGTK_SYM_VIEW_FFT_SIZE - max_tau;

    msg = strbuild(
        "Maximum autocorrelation found at tau = <b>%d</b> and <b>%d</b> symbols "
        "(significance: %lg%%)",
        max_tau,
        SUGTK_SYM_VIEW_FFT_SIZE - max_tau,
        100.0 * max / creal(view->fft_buf[0]));
    g_assert_nonnull(msg);

    dialog = gtk_message_dialog_new(
        GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(view))),
        GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_INFO,
        GTK_BUTTONS_CLOSE,
        NULL);

    gtk_message_dialog_set_markup(GTK_MESSAGE_DIALOG(dialog), (gchar *) msg);

    gtk_window_set_title(GTK_WINDOW(dialog), "Symbol autocorrelation");

    gtk_dialog_run(GTK_DIALOG(dialog));
  }

done:
  if (dialog != NULL)
    gtk_widget_destroy(dialog);

  if (msg != NULL)
    free(msg);
}

static void
sugtk_sym_view_error(
    GtkWindow *toplevel,
    const char *title,
    const char *fmt,
    ...)
{
  va_list ap;
  char *message;
  GtkWidget *dialog;

  va_start(ap, fmt);

  if ((message = vstrbuild(fmt, ap)) != NULL) {
    dialog = gtk_message_dialog_new(
        toplevel,
        GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_ERROR,
        GTK_BUTTONS_CLOSE,
        "%s",
        message);

    gtk_window_set_title(GTK_WINDOW(dialog), title);

    gtk_dialog_run(GTK_DIALOG(dialog));

    gtk_widget_destroy(dialog);

    free(message);
  }

  va_end(ap);
}

guint
sugtk_sym_view_pixel_to_code_helper(uint8_t bits_per_symbol, uint8_t pixel)
{
  return pixel >> (8 - bits_per_symbol);
}

guint
sugtk_sym_view_code_to_pixel_helper(uint8_t bits_per_symbol, uint8_t code)
{
  return (0xff * (uint32_t) code) / ((1 << bits_per_symbol) - 1);
}

gboolean
sugtk_sym_view_save_helper(
    SuGtkSymView *view,
    const gchar *title,
    const gchar *file_name_hint,
    uint8_t bits_per_symbol)
{
  GtkWidget *toplevel;
  GtkWindow *window = NULL;
  GtkWidget *dialog;
  GtkFileChooser *chooser;
  gchar *filename = NULL;
  char result;
  const uint8_t *bytes;
  size_t size;
  FILE *fp = NULL;
  gboolean ok = FALSE;
  unsigned int i;

  toplevel = gtk_widget_get_toplevel(GTK_WIDGET(view));
  if (gtk_widget_is_toplevel(toplevel))
    window = GTK_WINDOW(toplevel);

  dialog = gtk_file_chooser_dialog_new(
          title,
          window,
          GTK_FILE_CHOOSER_ACTION_SAVE,
          "_Cancel",
          GTK_RESPONSE_CANCEL,
          "_Save",
          GTK_RESPONSE_ACCEPT,
          NULL);

  chooser = GTK_FILE_CHOOSER(dialog);

  gtk_file_chooser_set_do_overwrite_confirmation(chooser, TRUE);

  gtk_file_chooser_set_current_name(chooser, file_name_hint);

  if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
    if ((filename = gtk_file_chooser_get_filename(chooser)) == NULL) {
      sugtk_sym_view_error(
          window,
          "Save file",
          "Selected file is not representable in the filesystem");
      goto done;
    }

    if ((fp = fopen(filename, "wb")) == NULL) {
      sugtk_sym_view_error(
          window,
          "Save failed",
          "Cannot save symbols to file: %s",
          strerror(errno));
      goto done;
    }

    bytes = sugtk_sym_view_get_buffer_bytes(view);
    size = sugtk_sym_view_get_buffer_size(view);

    for (i = 0; i < size; i += SUGTK_SYM_VIEW_STRIDE_ALIGN) {
      result = sugtk_sym_view_pixel_to_code_helper(bits_per_symbol, bytes[i])
          + '0';

      if (fwrite(&result, 1, 1, fp) < 1) {
        sugtk_sym_view_error(
            window,
            "Write failed",
            "Failed to write symbol recording to disk: %s",
            strerror(errno));
        goto done;
      }
    }
  }

  ok = TRUE;

done:
  if (fp != NULL)
    fclose(fp);

  if (filename != NULL)
    g_free (filename);

  if (dialog != NULL)
    gtk_widget_destroy(dialog);

  return ok;
}

static void
sugtk_sym_view_on_scroll(GtkWidget *widget, GdkEventScroll *ev, gpointer data)
{
  gboolean delta;
  int delta_int;
  int new_offset;

  SuGtkSymView *view = (SuGtkSymView *) widget;

  if (ev->direction == GDK_SCROLL_SMOOTH && !view->autoscroll) {
    delta = ev->delta_y;
    if (delta < 0)
      delta_int = floor(delta);
    else
      delta_int = ceil(delta);

    delta_int *= sugtk_sym_view_get_width(view) * 10;

    new_offset = sugtk_sym_view_get_offset(view) + delta_int;
    if (new_offset < 0)
      new_offset = 0;

    sugtk_sym_view_set_offset(view, new_offset);

    sugtk_sym_view_refresh_hard(view);
  }
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

  /*
   * I dislike the way Gtk handles memory and forces me to
   * abort in case of allocation error
   */
  self->fft_buf = fftw_malloc(SUGTK_SYM_VIEW_FFT_SIZE * sizeof(fftw_complex));
  g_assert_nonnull(self->fft_buf);

  /*
   * In and out buffers must be complex because I want to do an in-place
   * transform, and input will not be symmetrical most of the time
   */
  self->fft_plan = fftw_plan_dft_1d(
      SUGTK_SYM_VIEW_FFT_SIZE,
      self->fft_buf,
      self->fft_buf,
      FFTW_FORWARD,
      FFTW_ESTIMATE);
  g_assert_nonnull(self->fft_plan);

  self->fft_plan_rev = fftw_plan_dft_1d(
        SUGTK_SYM_VIEW_FFT_SIZE,
        self->fft_buf,
        self->fft_buf,
        FFTW_FORWARD,
        FFTW_BACKWARD);
  g_assert_nonnull(self->fft_plan);

  /* Create context menu */
  self->menu = GTK_MENU(gtk_menu_new());
  gtk_menu_attach_to_widget(self->menu, GTK_WIDGET(self), NULL);

  gtk_menu_shell_append(
      GTK_MENU_SHELL(self->menu),
      self->apply_fac = gtk_menu_item_new_with_label("FAC analysis"));

  gtk_menu_shell_append(
      GTK_MENU_SHELL(self->menu),
      self->apply_bm = gtk_menu_item_new_with_label("Apply Berlekamp-Massey"));

  gtk_widget_show_all(GTK_WIDGET(self->menu));

  /* Connect calbacks */
  g_signal_connect(
      G_OBJECT(self->apply_fac),
      "activate",
      G_CALLBACK(sugtk_sym_view_on_fac),
      self);

  g_signal_connect(
      G_OBJECT(self->apply_bm),
      "activate",
      G_CALLBACK(sugtk_sym_view_on_bm),
      self);

  gtk_widget_set_events(
      GTK_WIDGET(self),
        GDK_EXPOSURE_MASK
      | GDK_LEAVE_NOTIFY_MASK
      | GDK_BUTTON_PRESS_MASK
      | GDK_BUTTON_RELEASE_MASK
      | GDK_POINTER_MOTION_MASK
      | GDK_SMOOTH_SCROLL_MASK);


  g_signal_connect(
      self,
      "configure-event",
      (GCallback) sugtk_sym_view_on_configure_event,
      NULL);

  g_signal_connect(
      self,
      "draw",
      (GCallback) sugtk_sym_view_on_draw,
      NULL);

  g_signal_connect(
      self,
      "button-press-event",
      (GCallback) sugtk_sym_view_on_button_press_event,
      NULL);

  g_signal_connect(
      self,
      "button-release-event",
      (GCallback) sugtk_sym_view_on_button_release_event,
      NULL);

  g_signal_connect(
      self,
      "motion-notify-event",
      (GCallback) sugtk_sym_view_on_motion_notify_event,
      NULL);

  g_signal_connect(
      self,
      "scroll-event",
      (GCallback) sugtk_sym_view_on_scroll,
      NULL);
}

gboolean
sugtk_sym_view_get_selection(const SuGtkSymView *view, guint *start, guint *end)
{
  if (view->sel_off0 < view->sel_off1) {
    *start = view->sel_off0;
    *end   = view->sel_off1;
  } else {
    *start = view->sel_off1;
    *end   = view->sel_off0;
  }

  return view->selection;
}

GtkWidget *
sugtk_sym_view_new(void)
{
  return (GtkWidget *) g_object_new(SUGTK_TYPE_SYM_VIEW, NULL);
}
