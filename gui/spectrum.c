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

#include "gradient.h"
#include "spectrum.h"

G_DEFINE_TYPE(SuGtkSpectrum, sugtk_spectrum, GTK_TYPE_DRAWING_AREA);

#define SUGTK_SPECTRUM_TO_SCR(s, x, y)         \
  sugtk_spectrum_to_scr_x(s, x), sugtk_spectrum_to_scr_y(s, y)

#define SUGTK_SPECTRUM_SPECTROGRAM_TO_SCR(s, x, y)         \
  sugtk_spectrum_to_scr_x(s, x), sugtk_spectrum_spectrogram_to_scr_y(s, y)

#define SUGTK_SPECTRUM_SETTER(type, name)   \
SUGTK_SPECTRUM_SETTER_PROTO(type, name)     \
{                                           \
  spect->name = value;                      \
}

#define SUGTK_SPECTRUM_SETTER_REDRAW(type, name)  \
SUGTK_SPECTRUM_SETTER_PROTO(type, name)           \
{                                                 \
  spect->name = value;                            \
                                                  \
  sugtk_spectrum_reconfigure_surfaces(spect);     \
                                                  \
  sugtk_spectrum_refresh_hard(spect);             \
}

#define SUGTK_SPECTRUM_GETTER(type, name)   \
SUGTK_SPECTRUM_GETTER_PROTO(type, name)     \
{                                           \
  return spect->name;                       \
}

/* Coordinate translation */
static inline float
sugtk_spectrum_to_graph_x(const SuGtkSpectrum *s, gsufloat x)
{
  return s->g_width * (x + .5);
}

static inline float
sugtk_spectrum_from_graph_x(const SuGtkSpectrum *s, gsufloat x)
{
  return x / s->g_width - .5;
}

/* Screen cordinate conversion functions */
static inline float
sugtk_spectrum_to_scr_x(const SuGtkSpectrum *s, gsufloat x)
{
  return
      sugtk_spectrum_to_graph_x(s, x) + SUGTK_SPECTRUM_LEFT_PADDING;
}

static inline float
sugtk_spectrum_from_scr_x(const SuGtkSpectrum *s, gsufloat x)
{
  return (x - SUGTK_SPECTRUM_LEFT_PADDING) / s->g_width - .5;
}

static inline float
sugtk_spectrum_to_scr_y(const SuGtkSpectrum *s, gsufloat y)
{
  return -y * s->g_height + SUGTK_SPECTRUM_TOP_PADDING;
}

#if 0
static inline float
sugtk_spectrum_from_scr_y(const SuGtkSpectrum *s, gsufloat y)
{
  return (-y - SUGTK_SPECTRUM_TOP_PADDING) / s->g_height;
}
#endif

static inline float
sugtk_spectrum_spectrogram_to_scr_y(const SuGtkSpectrum *s, gsufloat y)
{
  return -y * s->s_height + SUGTK_SPECTRUM_TOP_PADDING;
}

static inline float
sugtk_spectrum_spectrogram_from_scr_y(const SuGtkSpectrum *s, gsufloat y)
{
  return (-y - SUGTK_SPECTRUM_TOP_PADDING) / s->s_height;
}

#if 0
static inline float
sugtk_spectrum_waterfall_to_scr_y(const SuGtkSpectrum *s, gsufloat y)
{
  return -y * s->s_height + SUGTK_SPECTRUM_TOP_PADDING;
}

static inline float
sugtk_spectrum_waterfall_from_scr_y(const SuGtkSpectrum *s, gsufloat y)
{
  return (-y - SUGTK_SPECTRUM_TOP_PADDING) / s->s_height;
}
#endif

/* Coordinate adjust according to scaling parameters */
static inline float
sugtk_spectrum_adjust_x(const SuGtkSpectrum *s, gsufloat x)
{
  return (x - s->freq_offset) * s->freq_scale;
}

static inline float
sugtk_spectrum_adjust_x_inv(const SuGtkSpectrum *s, gsufloat x)
{
  return x / s->freq_scale + s->freq_offset;
}

static inline float
sugtk_spectrum_adjust_y(const SuGtkSpectrum *s, gsufloat y)
{
  return (y - s->ref_level) / (s->dbs_per_div * SUGTK_SPECTRUM_VERTICAL_DIVS);
}

static inline float
sugtk_spectrum_adjust_y_inv(const SuGtkSpectrum *s, gsufloat y)
{
  return y * s->dbs_per_div * SUGTK_SPECTRUM_VERTICAL_DIVS + s->ref_level;
}

/******************************** GUI setup **********************************/
static void
sugtk_spectrum_reconfigure_surfaces(SuGtkSpectrum *spect)
{
  GtkWidget *widget = GTK_WIDGET(spect);

  cairo_surface_t *old_surf0;
  cairo_surface_t *old_surf1;
  cairo_t *cr;
  gsufloat old_g_width;
  gsufloat ratio;

  /* Update geometry parameters */
  old_g_width = spect->g_width;

  spect->g_width =
      spect->width
      - SUGTK_SPECTRUM_LEFT_PADDING
      - SUGTK_SPECTRUM_RIGHT_PADDING
      - 2;

  spect->g_height =
      spect->height
        - SUGTK_SPECTRUM_TOP_PADDING
        - SUGTK_SPECTRUM_BOTTOM_PADDING
        - 2;

  switch (spect->mode) {
    case SUGTK_SPECTRUM_MODE_SPECTROGRAM:
      spect->s_height = spect->g_height;
      break;

    case SUGTK_SPECTRUM_MODE_WATERFALL:
      spect->w_height = spect->g_height;
      spect->w_top = 0;
      break;

    case SUGTK_SPECTRUM_MODE_BOTH:
      spect->s_height = spect->s_wf_ratio * spect->g_height;
      spect->w_height = (1 - spect->s_wf_ratio) * spect->g_height;
      spect->w_top    = spect->s_height;
      break;
  }

  /* Save waterfall surfaces for posterior rescaling  */
  old_surf0 = spect->sf_wf[0];
  old_surf1 = spect->sf_wf[1];

  /* Create similar surfaces */
  if (spect->sf_spectrum != NULL)
    cairo_surface_destroy(spect->sf_spectrum);

  spect->sf_spectrum = gdk_window_create_similar_surface(
      gtk_widget_get_window(widget),
      CAIRO_CONTENT_COLOR,
      spect->width,
      spect->height);

  spect->sf_wf[0] = gdk_window_create_similar_surface(
      gtk_widget_get_window(widget),
      CAIRO_CONTENT_COLOR,
      spect->g_width,
      spect->g_height);

  spect->sf_wf[1] = gdk_window_create_similar_surface(
      gtk_widget_get_window(widget),
      CAIRO_CONTENT_COLOR,
      spect->g_width,
      spect->g_height);

  /* Rescale existing waterfall according to the new ratio */
  ratio = spect->g_width / old_g_width;

  if (old_surf0 != NULL) {
    cr = cairo_create(spect->sf_wf[0]);
    if (ratio != 1) {
      cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);
      cairo_scale(cr, ratio, 1);
    }
    cairo_set_source_surface(cr, old_surf0, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(old_surf0);
  }

  if (old_surf1 != NULL) {
    cr = cairo_create(spect->sf_wf[1]);
    if (ratio != 1) {
      cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);
      cairo_scale(cr, ratio, 1);
    }
    cairo_set_source_surface(cr, old_surf1, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(old_surf1);
  }
}

/************************** Channel information ******************************/
const struct sigutils_channel *
sugtk_spectrum_lookup_channel(const SuGtkSpectrum *spect, gsufloat fc)
{
  guint i;

  /* Selection has precedence, always */
  if (spect->selection.f_lo <= fc && fc <= spect->selection.f_hi)
    return &spect->selection;

  for (i = 0; i < spect->channel_count; ++i)
    if (spect->channel_list[i] != NULL)
      if (SU_ABS(spect->channel_list[i]->fc - fc)
          < .5 * SU_ABS(spect->channel_list[i]->bw))
        return spect->channel_list[i];

  return NULL;
}

/**************************** Spectrum draw **********************************/
static void
sugtk_spectrum_redraw_spectrogram(SuGtkSpectrum *spect, cairo_t *cr)
{
  int i;
  int step;
  gsufloat x, x_prev;
  gsufloat x_adj, x_prev_adj;
  gsufloat psd;

  /* Draw spectrum */
  if (spect->psd_data != NULL) {
    x_prev = .5;

    cairo_set_line_width(cr, 1);

    cairo_set_dash(cr, NULL, 0, 0);

    /* Draw noise level (if applicable) */
    if (spect->mode == SUGTK_SPECTRUM_MODE_SPECTROGRAM && spect->N0 > 0) {
      cairo_set_source_rgb(cr, 0, 1., 1.);
      cairo_move_to(
          cr,
          SUGTK_SPECTRUM_SPECTROGRAM_TO_SCR(
              spect,
              -.5,
              sugtk_spectrum_adjust_y(
                  spect,
                  SU_POWER_DB(spect->N0))));

      cairo_line_to(
          cr,
          SUGTK_SPECTRUM_SPECTROGRAM_TO_SCR(
              spect,
              .5,
              sugtk_spectrum_adjust_y(
                  spect,
                  SU_POWER_DB(spect->N0))));

      cairo_stroke(cr);
    }

    gdk_cairo_set_source_rgba(cr, &spect->fg_color);

    step = (int) floor(
        spect->psd_size / (spect->width * spect->freq_scale));

    if (step < 1)
      step = 1;

    /* Draw PSD */
    x_prev = 0;
    for (i = step; i < spect->psd_size; i += step) {
      if ((x = i / (gsufloat) spect->psd_size) > .5) {
        x -= 1;

        if (x_prev > x)
          x_prev -= 1;
      }

      x_adj = sugtk_spectrum_adjust_x(spect, x);

      if (x_adj >= -.5 && x_adj < .5) {
        x_prev_adj = sugtk_spectrum_adjust_x(spect, x_prev);

        psd = spect->psd_data_smooth == NULL
            ? 10 * log10(spect->psd_data[i - step])
            : 10 * log10(spect->psd_data_smooth[i - step]);

        cairo_move_to(
            cr,
            SUGTK_SPECTRUM_SPECTROGRAM_TO_SCR(
                spect,
                x_prev_adj,
                sugtk_spectrum_adjust_y(spect, psd)));

        psd = spect->psd_data_smooth == NULL
            ? 10 * log10(spect->psd_data[i])
            : 10 * log10(spect->psd_data_smooth[i]);

        cairo_line_to(
            cr,
            SUGTK_SPECTRUM_SPECTROGRAM_TO_SCR(
                spect,
                x_adj,
                sugtk_spectrum_adjust_y(spect, psd)));

        cairo_stroke(cr);
      }

      x_prev = x;
    }
  }
}

/*************************** Waterfall draw **********************************/
static void
sugtk_spectrum_move_waterfall(SuGtkSpectrum *spect, gsufloat off_x)
{
  cairo_t *cr;

  /* Take second surface and dump it to the first with an x-offset */
  cr = cairo_create(spect->sf_wf[0]);
  gdk_cairo_set_source_rgba(cr, &spect->bg_color);
  cairo_paint(cr);
  cairo_set_source_surface(cr, spect->sf_wf[1], off_x, 0);
  cairo_rectangle(
      cr,
      off_x,
      0,
      spect->g_width - abs(off_x),
      spect->g_height);
  cairo_fill(cr);
  cairo_destroy(cr);

  /* Copy first surface to the second */
  cr = cairo_create(spect->sf_wf[1]);
  cairo_set_source_surface(cr, spect->sf_wf[0], 0, 0);
  cairo_rectangle(cr, 0, 0, spect->g_width, spect->g_height);
  cairo_fill(cr);
  cairo_destroy(cr);
}

static void
sugtk_spectrum_scale_waterfall(SuGtkSpectrum *spect, gsufloat factor)
{
  cairo_t *cr;

  /* Take second surface and dump it scaled */
  cr = cairo_create(spect->sf_wf[0]);
  gdk_cairo_set_source_rgba(cr, &spect->bg_color);
  cairo_paint(cr);
  cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);
  cairo_translate(cr, spect->g_width / 2, 0);
  cairo_scale(cr, factor, 1);
  cairo_translate(cr, -spect->g_width / 2, 0);
  cairo_set_source_surface(cr, spect->sf_wf[1], 0, 0);
  cairo_rectangle(
      cr,
      0,
      0,
      spect->g_width,
      spect->g_height);
  cairo_fill(cr);
  cairo_destroy(cr);

  /* Copy first surface to the second */
  cr = cairo_create(spect->sf_wf[1]);
  cairo_set_source_surface(cr, spect->sf_wf[0], 0, 0);
  cairo_rectangle(cr, 0, 0, spect->g_width, spect->g_height);
  cairo_fill(cr);
  cairo_destroy(cr);
}

static cairo_t *
sugtk_spectrum_waterfall_flip(SuGtkSpectrum *spectrum)
{
  cairo_t *cr;

  cr = cairo_create(spectrum->sf_wf[spectrum->flip]);

  spectrum->flip = !spectrum->flip;
  cairo_set_source_surface(
      cr,
      spectrum->sf_wf[spectrum->flip],
      0, /* x */
      1  /* y */);

  /* Scroll existing waterfall to the bottom */
  cairo_rectangle(
      cr,
      0, /* x */
      0, /* y */
      spectrum->g_width,
      spectrum->g_height - 1);
  cairo_fill(cr);

  return cr;
}

static void
sugtk_spectrum_get_waterfall_limits(
    SuGtkSpectrum *spectrum,
    int *start_pix,
    int *end_pix)
{
  /* Compute boundaries */
  *start_pix =
      sugtk_spectrum_to_graph_x(
          spectrum,
          sugtk_spectrum_adjust_x(spectrum, -.5));

  *end_pix =
      sugtk_spectrum_to_graph_x(
          spectrum,
          sugtk_spectrum_adjust_x(spectrum, .5));

  if (*start_pix < 0)
    *start_pix = 0;

  if (*end_pix >= spectrum->g_width)
    *end_pix = spectrum->g_width;
}

static void
sugtk_spectrum_commit_waterfall_line(SuGtkSpectrum *spect)
{
  gsufloat x;
  gint i, j;
  gint start;
  gint end;
  gint index;
  const gsufloat *psd_data;
  guint psd_size;
  cairo_t *cr_wf = NULL;
  gsufloat val;

  psd_data = spect->psd_data;
  psd_size = spect->psd_size;

  /* Flip buffers */
  if (psd_data != NULL) {
    cr_wf = sugtk_spectrum_waterfall_flip(spect);

    /* Get waterfall limits */
    sugtk_spectrum_get_waterfall_limits(spect, &start, &end);

    /* Set background to black */
    gdk_cairo_set_source_rgba(cr_wf, &spect->bg_color);
    cairo_move_to(cr_wf, 0, 0);
    cairo_line_to(cr_wf, spect->g_width - 1, 0);
    cairo_stroke(cr_wf);

    /* Paint new line */
    for (i = start + 1; i < end; ++i) {
      /*
       * We have to conver the pixel coordinate back to the given
       * point in the spectrum
       */
      x = sugtk_spectrum_adjust_x_inv(
          spect,
          sugtk_spectrum_from_graph_x(spect, i));

      j = x * psd_size;

      /* Adjust for negative frequencies */
      if (j < 0)
        j += psd_size;

      if (j < 0 || j >= psd_size)
        break;

      /* We assume this is a power spectrum, hence the 10 */
      val = 1 + sugtk_spectrum_adjust_y(
          spect,
          10 * log10(psd_data[j]) - 5);

      if (val < 0)
        val = 0;
      else if (val > 1 || isnan(val))
        val = 1;

      index = round(val * 255);

      /* TODO: add more gradients */
      cairo_set_source_rgb(
          cr_wf,
          wf_gradient[index][0],
          wf_gradient[index][1],
          wf_gradient[index][2]);

      cairo_move_to(cr_wf, i - 1, 0);
      cairo_line_to(cr_wf, i, 0);
      cairo_stroke(cr_wf);
    }

    if (cr_wf != NULL)
      cairo_destroy(cr_wf);
  }
}

static void
sugtk_spectrum_redraw_waterfall(SuGtkSpectrum *spect, cairo_t *cr)
{
  /* Dump waterfall to an existing cairo object */
  cairo_set_source_surface(
      cr,
      spect->sf_wf[spect->flip],
      SUGTK_SPECTRUM_LEFT_PADDING,
      SUGTK_SPECTRUM_TOP_PADDING + spect->w_top);

  cairo_rectangle(
      cr,
      SUGTK_SPECTRUM_LEFT_PADDING,
      SUGTK_SPECTRUM_TOP_PADDING + spect->w_top,
      spect->g_width,
      spect->w_height);

  cairo_fill(cr);

  /*
   * If we are asking to overlay channels, we darken the whole waterfall
   * so that channels painted on top are more visible
   */

  if (spect->show_channels && spect->mode == SUGTK_SPECTRUM_MODE_WATERFALL) {
    gdk_cairo_set_source_rgba(cr, &spect->bg_color);
    cairo_paint_with_alpha(cr, .5);
  }
}

/************************* Common redrawing methods **************************/
static void
sugtk_spectrum_redraw_levels(SuGtkSpectrum *spect, cairo_t *cr)
{
  int i;
  int last_end;
  gsufloat x, xscr;
  char text[20];
  cairo_text_extents_t extents;

  cairo_select_font_face(
      cr,
      "Inconsolata",
      CAIRO_FONT_SLANT_NORMAL,
      CAIRO_FONT_WEIGHT_BOLD);

  gdk_cairo_set_source_rgba(cr, &spect->text_color);

  if (spect->mode != SUGTK_SPECTRUM_MODE_WATERFALL)
    for (i = 1; i < SUGTK_SPECTRUM_VERTICAL_DIVS; ++i) {
      snprintf(
          text,
          sizeof(text),
          "%d",
          (int) (sugtk_spectrum_adjust_y_inv(
              spect,
              -(gsufloat) i / SUGTK_SPECTRUM_VERTICAL_DIVS)));

      cairo_move_to(
          cr,
          7.5,
          sugtk_spectrum_spectrogram_to_scr_y(
              spect,
              -i * SUGTK_SPECTRUM_DY));

      cairo_show_text(
          cr,
          text);
    }


  if (spect->samp_rate > 0) {
    last_end = 0;

    for (
        i = -SUGTK_SPECTRUM_HORIZONTAL_DIVS / 2 + 1;
        i < SUGTK_SPECTRUM_HORIZONTAL_DIVS / 2;
        ++i) {
      x = i / (gsufloat) SUGTK_SPECTRUM_HORIZONTAL_DIVS;
      xscr = sugtk_spectrum_to_scr_x(spect, x);

      snprintf(
          text,
          sizeof(text),
          "%lli",
          (int64_t) (
              spect->fc
              + (int) round(SU_NORM2ABS_FREQ(
                  spect->samp_rate,
                  2 * sugtk_spectrum_adjust_x_inv(spect, x)))));

      cairo_text_extents(cr, text, &extents);

      if (xscr - extents.width / 2 > last_end) {
        cairo_move_to(
            cr,
            xscr - extents.width / 2,
            spect->height - SUGTK_SPECTRUM_BOTTOM_PADDING + 10);

        cairo_show_text(
            cr,
            text);

        last_end = xscr + extents.width / 2;
      }
    }
  }
}

static void
sugtk_spectrum_redraw_axes(SuGtkSpectrum *spectrum, cairo_t *cr)
{
  static const double axis_pattern[] = {1.0, 1.0};
  int i;

  cairo_set_line_width(cr, 1);

  sugtk_spectrum_redraw_levels(spectrum, cr);

  /* Draw axes in spectrogram or hybrid mode */
  if (spectrum->mode != SUGTK_SPECTRUM_MODE_WATERFALL) {
    cairo_set_dash(cr, axis_pattern, 2, 0);

    for (
        i = -SUGTK_SPECTRUM_HORIZONTAL_DIVS / 2 + 1;
        i < SUGTK_SPECTRUM_HORIZONTAL_DIVS / 2;
        ++i) {

      if (i == 0)
        gdk_cairo_set_source_rgba(cr, &spectrum->text_color);
      else
        gdk_cairo_set_source_rgba(cr, &spectrum->axes_color);

      cairo_move_to(
          cr,
          SUGTK_SPECTRUM_SPECTROGRAM_TO_SCR(
              spectrum,
              i * SUGTK_SPECTRUM_DX,
              0));
      cairo_line_to(
          cr,
          SUGTK_SPECTRUM_SPECTROGRAM_TO_SCR(
              spectrum,
              i * SUGTK_SPECTRUM_DX,
              -1));

      cairo_stroke(cr);
    }

    for (i = 1; i < SUGTK_SPECTRUM_VERTICAL_DIVS; ++i) {
      cairo_move_to(
          cr,
          SUGTK_SPECTRUM_SPECTROGRAM_TO_SCR(
              spectrum,
              -.5,
              -i * SUGTK_SPECTRUM_DY));
      cairo_line_to(
          cr,
          SUGTK_SPECTRUM_SPECTROGRAM_TO_SCR(
              spectrum,
              .5,
              -i * SUGTK_SPECTRUM_DY));

      cairo_stroke(cr);
    }
  }

  /* Draw border */
  gdk_cairo_set_source_rgba(cr, &spectrum->axes_color);
  cairo_set_dash(cr, NULL, 0, 0);

  if (spectrum->mode == SUGTK_SPECTRUM_MODE_BOTH) {
    cairo_move_to(
        cr,
        SUGTK_SPECTRUM_SPECTROGRAM_TO_SCR(spectrum, -.5, -1));
    cairo_line_to(
        cr,
        SUGTK_SPECTRUM_SPECTROGRAM_TO_SCR(spectrum, .5, -1));
  }

  cairo_move_to(
      cr,
      SUGTK_SPECTRUM_TO_SCR(spectrum, -.5, 0));
  cairo_line_to(
      cr,
      SUGTK_SPECTRUM_TO_SCR(spectrum, -.5, -1));
  cairo_stroke(cr);

  cairo_move_to(
      cr,
      SUGTK_SPECTRUM_TO_SCR(spectrum, -.5, 0));

  cairo_line_to(
      cr,
      SUGTK_SPECTRUM_TO_SCR(spectrum, .5, 0));
  cairo_stroke(cr);

  cairo_move_to(
      cr,
      SUGTK_SPECTRUM_TO_SCR(spectrum, .5, -1));
  cairo_line_to(
      cr,
      SUGTK_SPECTRUM_TO_SCR(spectrum, -.5, -1));
  cairo_stroke(cr);

  cairo_move_to(
      cr,
      SUGTK_SPECTRUM_TO_SCR(spectrum, .5, -1));

  cairo_line_to(
      cr,
      SUGTK_SPECTRUM_TO_SCR(spectrum, .5, 0));
  cairo_stroke(cr);
}

static void
sugtk_spectrum_redraw_channel(
    SuGtkSpectrum *spectrum,
    cairo_t *cr,
    const struct sigutils_channel *channel,
    gsufloat red,
    gsufloat green,
    gsufloat blue)
{
  gsufloat x1, xscr1;
  gsufloat x2, xscr2;

  gsufloat y1, yscr1;
  gsufloat y2, yscr2;

  /* Draw channel limits */
  x1 = (channel->f_lo - spectrum->fc) / (gsufloat) spectrum->samp_rate;
  x2 = (channel->f_hi - spectrum->fc) / (gsufloat) spectrum->samp_rate;

  if (x2 > .5) {
    x1 -= 1;
    x2 -= 1;
  }

  /* Apply frequency scaling */
  x1 = sugtk_spectrum_adjust_x(spectrum, x1);
  x2 = sugtk_spectrum_adjust_x(spectrum, x2);

  /* Draw channel if and only if it fits */
  if (x1 < .5 && x2 > -.5) {
    xscr1 = sugtk_spectrum_to_scr_x(spectrum, x1);
    xscr2 = sugtk_spectrum_to_scr_x(spectrum, x2);

    /* Draw levels only if spectrogram mode is enabled */
    if (channel->S0 > channel->N0 &&
        spectrum->mode != SUGTK_SPECTRUM_MODE_WATERFALL) {
      y1 = sugtk_spectrum_adjust_y(
          spectrum,
          channel->S0);
      y2 = sugtk_spectrum_adjust_y(
          spectrum,
          spectrum->smooth_N0 ? SU_POWER_DB(spectrum->prev_N0) : channel->N0);

      yscr1 = sugtk_spectrum_spectrogram_to_scr_y(spectrum, y1);
      yscr2 = sugtk_spectrum_spectrogram_to_scr_y(spectrum, y2);
    } else {
      yscr1 = SUGTK_SPECTRUM_TOP_PADDING;
      yscr2 = spectrum->height - SUGTK_SPECTRUM_BOTTOM_PADDING - 1;
    }

    if (spectrum->mode == SUGTK_SPECTRUM_MODE_WATERFALL)
      cairo_set_source_rgb(cr, .5 * red, .5 * green, .5 * blue);
    else
      cairo_set_source_rgba(cr, red, green, blue, .25);

    cairo_rectangle(cr, xscr1, yscr1, xscr2 - xscr1, yscr2 - yscr1);
    cairo_stroke_preserve(cr);
    cairo_fill(cr);

    /* Draw detected bandwidth */
    if (spectrum->mode == SUGTK_SPECTRUM_MODE_WATERFALL)
      cairo_set_source_rgba(cr, red, green, blue, 1);
    else
      cairo_set_source_rgba(cr, red, green, blue, .5);

    x1 = (channel->fc - channel->bw / 2 - spectrum->fc)
            / (SUFLOAT) spectrum->samp_rate;
    x2 = (channel->fc + channel->bw / 2 - spectrum->fc)
            / (SUFLOAT) spectrum->samp_rate;

    x1 = sugtk_spectrum_adjust_x(spectrum, x1);
    x2 = sugtk_spectrum_adjust_x(spectrum, x2);

    xscr1 = sugtk_spectrum_to_scr_x(spectrum, x1);
    xscr2 = sugtk_spectrum_to_scr_x(spectrum, x2);

    cairo_rectangle(cr, xscr1, yscr1, xscr2 - xscr1, yscr2 - yscr1);
    cairo_stroke_preserve(cr);
    cairo_fill(cr);
  }
}

static void
sugtk_spectrum_redraw_all_channels(SuGtkSpectrum *spectrum, cairo_t *cr)
{
  guint i;

  if (spectrum->samp_rate > 0)
    for (i = 0; i < spectrum->channel_count; ++i)
      sugtk_spectrum_redraw_channel(
          spectrum,
          cr,
          spectrum->channel_list[i],
          spectrum->mode == SUGTK_SPECTRUM_MODE_WATERFALL /* Red */
            ? 1
            : .75,
          0,   /* Green */
          0);  /* Blue */
}

static void
sugtk_spectrum_redraw(SuGtkSpectrum *spect)
{
  cairo_t *cr;

  cr = cairo_create(spect->sf_spectrum);

  cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);

  gdk_cairo_set_source_rgba(cr, &spect->bg_color);
  cairo_paint(cr);

  switch (spect->mode) {
    case SUGTK_SPECTRUM_MODE_SPECTROGRAM:
      sugtk_spectrum_redraw_spectrogram(spect, cr);
      break;

    case SUGTK_SPECTRUM_MODE_WATERFALL:
      sugtk_spectrum_redraw_waterfall(spect, cr);
      break;

    case SUGTK_SPECTRUM_MODE_BOTH:
      sugtk_spectrum_redraw_spectrogram(spect, cr);
      sugtk_spectrum_redraw_waterfall(spect, cr);
  }

  if (spect->samp_rate > 0) {
    if (spect->show_channels)
      sugtk_spectrum_redraw_all_channels(spect, cr);

    if (spect->selection.bw > 0)
      sugtk_spectrum_redraw_channel(
          spect,
          cr,
          &spect->selection,
          0,    /* Red */
          .75,  /* Green */
          .75); /* Blue */
  }

  sugtk_spectrum_redraw_axes(spect, cr);

  cairo_destroy(cr);
}

static void
sugtk_spectrum_refresh_hard(SuGtkSpectrum *spect)
{
  sugtk_spectrum_redraw(spect);
  gtk_widget_queue_draw(GTK_WIDGET(spect));
}

static void
sugtk_spectrum_refresh(SuGtkSpectrum *spect)
{
  struct timeval tv, sub;

  gettimeofday(&tv, NULL);
  timersub(&tv, &spect->last_redraw_time, &sub);

  if (sub.tv_usec > SUGTK_SPECTRUM_MIN_REDRAW_INTERVAL_MS * 1000) {
    sugtk_spectrum_refresh_hard(spect);
    spect->last_redraw_time = tv;
  }
}


/****************************** Public API ***********************************/
void
sugtk_spectrum_set_defaults(SuGtkSpectrum *spect)
{
  spect->show_channels = TRUE;
  spect->auto_level    = TRUE;
  spect->mode          = SUGTK_SPECTRUM_MODE_SPECTROGRAM;
  spect->freq_offset   = SUGTK_SPECTRUM_FREQ_OFFSET_DEFAULT;
  spect->freq_scale    = SUGTK_SPECTRUM_FREQ_SCALE_DEFAULT;
  spect->ref_level     = SUGTK_SPECTRUM_REF_LEVEL_DEFAULT;
  spect->dbs_per_div   = SUGTK_SPECTRUM_DBS_PER_DIV_DEFAULT;
  spect->agc_alpha     = SUGTK_SPECTRUM_AGC_ALPHA;
  spect->s_wf_ratio    = SUGTK_SPECTRUM_S_WF_RATIO_DEFAULT;
  spect->dc_skip       = TRUE;

  (void) gdk_rgba_parse(&spect->bg_color,   "#000000");
  (void) gdk_rgba_parse(&spect->fg_color,   "#ffff00");
  (void) gdk_rgba_parse(&spect->axes_color, "#808080");
  (void) gdk_rgba_parse(&spect->text_color, "#ffffff");
}

void
sugtk_spectrum_reset(SuGtkSpectrum *spect)
{
  if (spect->psd_data != NULL) {
    free(spect->psd_data);
    spect->psd_data = NULL;
  }

  if (spect->psd_data_smooth != NULL) {
    free(spect->psd_data_smooth);
    spect->psd_data_smooth = NULL;
  }

  spect->freq_offset = SUGTK_SPECTRUM_FREQ_OFFSET_DEFAULT;
  spect->freq_scale  = SUGTK_SPECTRUM_FREQ_SCALE_DEFAULT;
  spect->ref_level   = SUGTK_SPECTRUM_REF_LEVEL_DEFAULT;
  spect->dbs_per_div = SUGTK_SPECTRUM_DBS_PER_DIV_DEFAULT;

  spect->N0        = 0;
  spect->prev_N0   = 0;
  spect->selection.bw = 0;
  spect->selecting = FALSE;
  spect->psd_size  = 0;
  spect->samp_rate = 0;
  spect->fc        = 0;
  spect->last_max  = 0;

  sugtk_spectrum_refresh_hard(spect);
}

SUGTK_SPECTRUM_SETTER(gboolean, show_channels);
SUGTK_SPECTRUM_SETTER(gboolean, auto_level);
SUGTK_SPECTRUM_SETTER(gboolean, dc_skip);
SUGTK_SPECTRUM_SETTER(gboolean, smooth_N0);
SUGTK_SPECTRUM_SETTER(gboolean, has_menu);
SUGTK_SPECTRUM_SETTER_REDRAW(enum SuGtkSpectrumMode, mode);
SUGTK_SPECTRUM_SETTER_REDRAW(gsufloat, s_wf_ratio);
SUGTK_SPECTRUM_SETTER(gsufloat, freq_offset);
SUGTK_SPECTRUM_SETTER(gsufloat, freq_scale);
SUGTK_SPECTRUM_SETTER_REDRAW(gsufloat, ref_level);
SUGTK_SPECTRUM_SETTER_REDRAW(gsufloat, dbs_per_div);
SUGTK_SPECTRUM_SETTER(gsufloat, agc_alpha);
SUGTK_SPECTRUM_SETTER(gsufloat, N0);
SUGTK_SPECTRUM_SETTER(guint, samp_rate);
SUGTK_SPECTRUM_SETTER_REDRAW(GdkRGBA, fg_color);
SUGTK_SPECTRUM_SETTER_REDRAW(GdkRGBA, bg_color);
SUGTK_SPECTRUM_SETTER_REDRAW(GdkRGBA, text_color);
SUGTK_SPECTRUM_SETTER_REDRAW(GdkRGBA, axes_color);

SUGTK_SPECTRUM_GETTER(gboolean, show_channels);
SUGTK_SPECTRUM_GETTER(gboolean, auto_level);
SUGTK_SPECTRUM_GETTER(gboolean, dc_skip);
SUGTK_SPECTRUM_GETTER(gboolean, smooth_N0);
SUGTK_SPECTRUM_GETTER(gboolean, has_menu);
SUGTK_SPECTRUM_GETTER(enum SuGtkSpectrumMode, mode);
SUGTK_SPECTRUM_GETTER(gsufloat, s_wf_ratio);
SUGTK_SPECTRUM_GETTER(gsufloat, freq_offset);
SUGTK_SPECTRUM_GETTER(gsufloat, freq_scale);
SUGTK_SPECTRUM_GETTER(gsufloat, ref_level);
SUGTK_SPECTRUM_GETTER(gsufloat, dbs_per_div);
SUGTK_SPECTRUM_GETTER(gsufloat, agc_alpha);
SUGTK_SPECTRUM_GETTER(gsufloat, N0);
SUGTK_SPECTRUM_GETTER(guint, samp_rate);
SUGTK_SPECTRUM_GETTER(GdkRGBA, fg_color);
SUGTK_SPECTRUM_GETTER(GdkRGBA, bg_color);
SUGTK_SPECTRUM_GETTER(GdkRGBA, text_color);
SUGTK_SPECTRUM_GETTER(GdkRGBA, axes_color);

void
sugtk_spectrum_update(
    SuGtkSpectrum *spect,
    gsufloat *spectrum_data,
    guint spectrum_size,
    guint samp_rate,
    gsufloat fc,
    gsufloat N0)
{
  gsufloat *old_data = spect->psd_data;
  guint     old_size = spect->psd_size;
  gsufloat  max = 0;
  gsufloat  range;
  guint     i;
  guint     skip;

  spect->fc        = fc;
  spect->psd_data  = spectrum_data;
  spect->psd_size  = spectrum_size;
  spect->samp_rate = samp_rate;
  spect->N0        = N0;

  if (spect->psd_data_smooth == NULL) {
    if (old_data != NULL) {
      /* Smoothed spectrum not initialized. Attempt to initialize */
      if (old_size == spectrum_size)
        spect->psd_data_smooth = old_data;
      else
        free(old_data); /* Size mismatch */
    }
  } else {
    /* Average against previous update, only if sizes match */
    if (old_size == spectrum_size) {
      if (spect->smooth_N0 && old_size > 0)
        N0 = spect->psd_data[0];
      for (i = 0; i < old_size; ++i) {
        spect->psd_data_smooth[i] +=
            SUGTK_SPECTRUM_ALPHA *
              (spect->psd_data[i] - spect->psd_data_smooth[i]);
        if (spect->smooth_N0 && spect->psd_data_smooth[i] < N0)
          N0 = spect->psd_data_smooth[i];
      }

      if (spect->smooth_N0) {
        if (spect->prev_N0 == 0) {
          spect->prev_N0 = N0;
        } else {
          spect->prev_N0 += SUGTK_SPECTRUM_ALPHA * (N0 - spect->prev_N0);
          N0 = spect->prev_N0;
        }
      }
    } else {
      /* Sizes don't match, reset smoothed spectrum */
      free(spect->psd_data_smooth);
      spect->psd_data_smooth = NULL;
      spect->prev_N0 = 0;
    }

    /* We don't need old_data anymore */
    if (old_data != NULL)
      free(old_data);
  }

  if (spect->auto_level && spect->psd_data_smooth != NULL) {
    skip = spect->dc_skip ? 4 : 0;

    for (i = skip; i < spect->psd_size - skip; ++i)
      if (spect->psd_data_smooth[i] > max)
        max = spect->psd_data_smooth[i];

    spect->last_max +=
        spect->agc_alpha * (SU_POWER_DB(max) - spect->last_max);

    /* Update range (i.e. dBs per division) */
    range =
        SUGTK_SPECTRUM_AUTO_LEVEL_RANGE_SCALE_DB
        * (spect->last_max - SU_POWER_DB(N0));

    if (range < SUGTK_SPECTRUM_MIN_AUTO_RANGE)
      range = SUGTK_SPECTRUM_MIN_AUTO_RANGE;

    spect->dbs_per_div +=
        spect->agc_alpha
        * (range * SUGTK_SPECTRUM_DY - spect->dbs_per_div);

    /*
     * Update reference level (i.e. dBs on top of the spectrum)
     * Since the spectrum may have been scaled by
     * SUSCAN_GUI_SPECTRUM_AUTO_LEVEL_RANGE_SCALE_DB, we correct its
     * layout
     */

    spect->ref_level +=
        spect->agc_alpha
        * (SU_POWER_DB(N0) + range - spect->ref_level);
  }

  sugtk_spectrum_commit_waterfall_line(spect);

  sugtk_spectrum_refresh(spect);
}

void
sugtk_spectrum_update_channels(
    SuGtkSpectrum *spect,
    struct sigutils_channel **channel_list,
    unsigned int channel_count)
{
  unsigned int i;

  for (i = 0; i < spect->channel_count; ++i)
    free(spect->channel_list[i]);

  if (spect->channel_list != NULL)
    free(spect->channel_list);

  spect->channel_list  = channel_list;
  spect->channel_count = channel_count;

  sugtk_spectrum_refresh(spect);
}

static void
sugtk_spectrum_on_menu_action(GtkWidget *widget, gpointer data)
{
  SuGtkSpectrumMenuContext *ctx = (SuGtkSpectrumMenuContext *) data;
  SuGtkSpectrum *spect = ctx->spect;

  (ctx->action)(spect, spect->menu_fc, &spect->menu_channel, ctx->data);
}

gboolean
sugtk_spectrum_add_menu_action(
    SuGtkSpectrum *spect,
    const gchar *label,
    SuGtkSpectrumMenuActionCallback action,
    gpointer data)
{
  SuGtkSpectrumMenuContext *ctx;
  GtkWidget *actionItem;

  ctx = g_new(SuGtkSpectrumMenuContext, 1);

  ctx->action = action;
  ctx->data   = data;
  ctx->spect  = spect;

  /* FIXME: use GList */
  if (PTR_LIST_APPEND_CHECK(spect->context, ctx) == -1) {
    g_free(ctx);
    return FALSE;
  }

  actionItem = gtk_menu_item_new_with_label(label);

  gtk_menu_shell_append(GTK_MENU_SHELL(spect->channelMenu), actionItem);

  g_signal_connect(
      actionItem,
      "activate",
      (GCallback) sugtk_spectrum_on_menu_action,
      ctx);

  return TRUE;
}

/****************************** Event handling *******************************/
static void
sugtk_spectrum_parse_dragging(
    SuGtkSpectrum *spect,
    const GdkEventMotion *ev)
{
  gsufloat  x,  y;
  gsufloat lx, ly;

  if (!spect->dragging) {
    spect->original_ref_level = spect->ref_level;
    spect->original_freq_offset = spect->freq_offset;
    spect->dragging = TRUE;
  }

  /* Change reference level */
  y = sugtk_spectrum_adjust_y_inv(
      spect,
      sugtk_spectrum_spectrogram_from_scr_y(spect, -ev->y));

  ly = sugtk_spectrum_adjust_y_inv(
      spect,
      sugtk_spectrum_spectrogram_from_scr_y(spect, -spect->last_y));

  /* Change reference level only applies to spectrogram */
  if (!spect->auto_level
      && spect->mode == SUGTK_SPECTRUM_MODE_SPECTROGRAM)
    spect->ref_level = spect->original_ref_level + y - ly;

  /* Change frequency offset only if sample rate has been defined */
  if (spect->samp_rate != 0) {
    x = sugtk_spectrum_adjust_x_inv(
          spect,
          sugtk_spectrum_from_scr_x(spect, ev->x));

    lx = sugtk_spectrum_adjust_x_inv(
          spect,
          sugtk_spectrum_from_scr_x(
              spect,
              spect->last_x));

    if (ev->x != spect->prev_ev_x)
      sugtk_spectrum_move_waterfall(
          spect,
          ev->x - spect->prev_ev_x);

    spect->freq_offset = spect->original_freq_offset + lx - x;

    sugtk_spectrum_refresh_hard(spect);
  }
}

static void
sugtk_spectrum_parse_selection(
    SuGtkSpectrum *spect,
    const GdkEventMotion *ev)
{
  SUFLOAT  x;
  SUFLOAT lx;

  spect->selecting = SU_TRUE;

  /* Change frequency offset only if sample rate has been defined */
  if (spect->samp_rate != 0) {
    x = sugtk_spectrum_adjust_x_inv(
          spect,
          sugtk_spectrum_from_scr_x(spect, ev->x));

    lx = sugtk_spectrum_adjust_x_inv(
          spect,
          sugtk_spectrum_from_scr_x(
              spect,
              spect->last_x));

    spect->selection.f_lo = spect->samp_rate * MIN(x, lx) + spect->fc;
    spect->selection.f_hi = spect->samp_rate * MAX(x, lx) + spect->fc;
    spect->selection.bw =
        spect->selection.f_hi - spect->selection.f_lo;
    spect->selection.fc =
        .5 * (spect->selection.f_lo + spect->selection.f_hi);

    spect->selection.ft = spect->fc;

    sugtk_spectrum_refresh_hard(spect);
  }
}

static gboolean
sugtk_spectrum_on_configure_event(
    GtkWidget *widget,
    GdkEventConfigure *event,
    gpointer data)
{
  SuGtkSpectrum *spect = SUGTK_SPECTRUM(widget);

  spect->height = event->height;
  spect->width  = event->width;

  sugtk_spectrum_reconfigure_surfaces(spect);

  sugtk_spectrum_refresh_hard(spect);

  return TRUE;
}

static gboolean
sugtk_spectrum_on_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
  SuGtkSpectrum *spect = SUGTK_SPECTRUM(widget);

  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_surface(cr, spect->sf_spectrum, 0, 0);
  cairo_paint(cr);

  return TRUE;
}

static void
sugtk_spectrum_apply_delta(
    SuGtkSpectrum *spect,
    enum SuGtkSpectrumParam param,
    gsufloat delta)
{
  gsufloat factor;

  switch (param) {
    case SUGTK_SPECTRUM_PARAM_FREQ_OFFSET:
      /* Multiplied by freq_scale to keep proportion */
      spect->freq_offset -=
          SUGTK_SPECTRUM_SCALE_DELTA * delta * spect->freq_scale;
      break;

    case SUGTK_SPECTRUM_PARAM_FREQ_SCALE:
      /* Multiplied by freq_scale to keep proportion */
      factor = spect->freq_scale;
      spect->freq_scale -=
          SUGTK_SPECTRUM_SCALE_DELTA * delta * spect->freq_scale;
      if (spect->freq_scale < 1)
        spect->freq_scale = 1;
      factor = spect->freq_scale / factor;

      if (factor != 1)
        sugtk_spectrum_scale_waterfall(spect, factor);
      break;

    case SUGTK_SPECTRUM_PARAM_REF_LEVEL:
      /* Multiplied by dbs_per_div to keep proportion */
      spect->ref_level -=
          SUGTK_SPECTRUM_SCALE_DELTA * delta * spect->dbs_per_div;
      break;

    case SUGTK_SPECTRUM_PARAM_DBS_PER_DIV:
      /* Multiplied by dbs_per_div to keep proportion */
      spect->dbs_per_div -=
          SUGTK_SPECTRUM_SCALE_DELTA * delta * spect->dbs_per_div;
      break;
  }

  sugtk_spectrum_refresh_hard(spect);
}

static gboolean
sugtk_spectrum_on_scroll_event(
    GtkWidget *widget,
    GdkEventScroll *ev,
    gpointer data)
{
  SuGtkSpectrum *spectrum = SUGTK_SPECTRUM(widget);

  switch (ev->direction) {
    case GDK_SCROLL_SMOOTH:
      if (ev->state & GDK_SHIFT_MASK) {
        if (!spectrum->auto_level)
          sugtk_spectrum_apply_delta(
              spectrum,
              SUGTK_SPECTRUM_PARAM_DBS_PER_DIV,
              ev->delta_y);
      } else {
        sugtk_spectrum_apply_delta(
            spectrum,
            SUGTK_SPECTRUM_PARAM_FREQ_SCALE,
            -ev->delta_y);
      }
      break;
  }
  return TRUE;
}

static gboolean
sugtk_spectrum_on_motion_notify_event(
    GtkWidget *widget,
    GdkEventMotion *ev,
    gpointer data)
{
  SuGtkSpectrum *spectrum = SUGTK_SPECTRUM(widget);
  gboolean selection_mode;
  gboolean was_dragging = spectrum->dragging;
  GdkEventMotion ev_adjusted;

  /*
   * This hack keeps GTK from sending non-integer x offsets. There is a
   * problem with this and Cairo. If we try to move the waterfall by a
   * non-integer offset, Cairo's antialias algorithm start to work,
   * blurring the whole waterfall. Enforcing this condition here
   * prevents this nasty effect from happening.
   */

  /* FIXME: copy required fields only */
  ev_adjusted = *ev;
  ev_adjusted.x = round(ev->x);

  if (ev_adjusted.state & GDK_BUTTON1_MASK) {
    selection_mode = !(ev_adjusted.state & GDK_SHIFT_MASK);

    /* Check whether dragging mode is enabled */
    if (!selection_mode) {
      sugtk_spectrum_parse_dragging(spectrum, &ev_adjusted);
      spectrum->selecting = FALSE;
    } else {
      sugtk_spectrum_parse_selection(spectrum, &ev_adjusted);
      spectrum->dragging = FALSE;
    }
  } else {
    spectrum->dragging  = FALSE;
    spectrum->selecting = FALSE;

    spectrum->last_x = ev_adjusted.x;
    spectrum->last_y = ev_adjusted.y;
  }

  spectrum->prev_ev_x = ev_adjusted.x;

  return TRUE;
}

static gboolean
sugtk_spectrum_on_button_press_event(
    GtkWidget *widget,
    GdkEventButton *ev,
    gpointer data)
{
  SuGtkSpectrum *spect = SUGTK_SPECTRUM(widget);
  char header[80];
  gsufloat x;
  gsufloat freq;
  const struct sigutils_channel *channel;

  if (ev->type == GDK_BUTTON_PRESS) {
    switch (ev->button) {
      case 1:
        /* Reset selection */
        spect->selection.bw = 0;
        sugtk_spectrum_refresh_hard(spect);
        break;

      case 3:
        if (spect->has_menu) {
          x = sugtk_spectrum_adjust_x_inv(
              spect,
              sugtk_spectrum_from_scr_x(spect, ev->x));
          freq = x * spect->samp_rate + spect->fc;

          /* Lookup channel */
          if ((channel = sugtk_spectrum_lookup_channel(
              spect,
              freq)) != NULL) {
            spect->menu_channel = *channel;
            spect->menu_channel.ft = spect->fc; /* This must be fixed */
            spect->menu_fc = channel->fc;

            snprintf(
                header,
                sizeof(header),
                "%lld Hz @ %lld Hz",
                (uint64_t) round(channel->f_hi - channel->f_lo),
                (uint64_t) round(channel->fc));

            gtk_menu_item_set_label(
                spect->channelHeaderMenuItem,
                header);

            gtk_widget_show_all(GTK_WIDGET(spect->channelMenu));

            gtk_menu_popup_at_pointer(spect->channelMenu, (GdkEvent *) ev);

            return TRUE;
          }
        }
        break;
    }
  }

  return TRUE;
}

/**************************** GObject boilerplate ****************************/
static void
sugtk_spectrum_dispose(GObject* object)
{
  SuGtkSpectrum *spect;

  spect = SUGTK_SPECTRUM(object);

  unsigned int i;

  for (i = 0; i < spect->channel_count; ++i)
    free(spect->channel_list[i]);

  if (spect->channel_list != NULL) {
    free(spect->channel_list);
    spect->channel_list = NULL;
    spect->channel_count = 0;
  }

  for (i = 0; i < spect->context_count; ++i)
    g_free(spect->context_list[i]);

  if (spect->context_list != NULL) {
    free(spect->context_list);
    spect->context_list = NULL;
    spect->context_count = 0;
  }

  if (spect->psd_data != NULL) {
    free(spect->psd_data);
    spect->psd_data = NULL;
  }

  if (spect->psd_data_smooth != NULL) {
    free(spect->psd_data_smooth);
    spect->psd_data_smooth = NULL;
  }

  if (spect->sf_wf[0] != NULL) {
    cairo_surface_destroy(spect->sf_wf[0]);
    spect->sf_wf[0] = NULL;
  }

  if (spect->sf_wf[1] != NULL) {
    cairo_surface_destroy(spect->sf_wf[1]);
    spect->sf_wf[1] = NULL;
  }

  if (spect->sf_spectrum != NULL) {
    cairo_surface_destroy(spect->sf_spectrum);
    spect->sf_spectrum = NULL;
  }

  if (spect->channelMenu != NULL) {
    gtk_widget_destroy(GTK_WIDGET(spect->channelMenu));
    spect->channelMenu = NULL;
  }

  G_OBJECT_CLASS(sugtk_spectrum_parent_class)->dispose(object);
}

static void
sugtk_spectrum_class_init(SuGtkSpectrumClass *class)
{
  GObjectClass  *g_object_class;

  g_object_class = G_OBJECT_CLASS(class);

  g_object_class->dispose = sugtk_spectrum_dispose;
}

static void
sugtk_spectrum_init(SuGtkSpectrum *self)
{
  gtk_widget_set_events(
      GTK_WIDGET(self),
        GDK_POINTER_MOTION_MASK
      | GDK_BUTTON_MOTION_MASK
      | GDK_BUTTON1_MOTION_MASK
      | GDK_BUTTON3_MOTION_MASK
      | GDK_BUTTON_PRESS_MASK
      | GDK_STRUCTURE_MASK
      | GDK_SMOOTH_SCROLL_MASK);

  g_signal_connect(
      self,
      "configure-event",
      (GCallback) sugtk_spectrum_on_configure_event,
      NULL);

  g_signal_connect(
      self,
      "draw",
      (GCallback) sugtk_spectrum_on_draw,
      NULL);

  g_signal_connect(
      self,
      "button-press-event",
      (GCallback) sugtk_spectrum_on_button_press_event,
      NULL);

  g_signal_connect(
      self,
      "motion-notify-event",
      (GCallback) sugtk_spectrum_on_motion_notify_event,
      NULL);

  g_signal_connect(
      self,
      "scroll-event",
      (GCallback) sugtk_spectrum_on_scroll_event,
      NULL);
}

GtkWidget *
sugtk_spectrum_new(void)
{
  SuGtkSpectrum *spect;

  spect = SUGTK_SPECTRUM(g_object_new(SUGTK_TYPE_SPECTRUM, NULL));

  spect->channelMenu = GTK_MENU(gtk_menu_new());
  spect->channelHeaderMenuItem =
      GTK_MENU_ITEM(gtk_menu_item_new_with_label("None"));

  gtk_menu_shell_append(
      GTK_MENU_SHELL(spect->channelMenu),
      GTK_WIDGET(spect->channelHeaderMenuItem));

  gtk_menu_shell_append(
      GTK_MENU_SHELL(spect->channelMenu),
      gtk_separator_menu_item_new());

  gtk_widget_set_sensitive(GTK_WIDGET(spect->channelHeaderMenuItem), FALSE);

  sugtk_spectrum_set_defaults(spect);

  return GTK_WIDGET(spect);
}

