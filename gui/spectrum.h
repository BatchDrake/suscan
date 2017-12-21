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

#ifndef _GUI_SPECTRUM_H
#define _GUI_SPECTRUM_H

#include <gtk/gtk.h>
#include <sigutils/sigutils.h>
#include <util.h>

#define SUSCAN_GUI_SPECTRUM_FREQ_OFFSET_DEFAULT 0
#define SUSCAN_GUI_SPECTRUM_FREQ_SCALE_DEFAULT  1
#define SUSCAN_GUI_SPECTRUM_DBS_PER_DIV_DEFAULT 10
#define SUSCAN_GUI_SPECTRUM_REF_LEVEL_DEFAULT   0
#define SUSCAN_GUI_SPECTRUM_AGC_ALPHA .1

enum suscan_gui_spectrum_param {
  SUSCAN_GUI_SPECTRUM_PARAM_FREQ_OFFSET,
  SUSCAN_GUI_SPECTRUM_PARAM_FREQ_SCALE,
  SUSCAN_GUI_SPECTRUM_PARAM_REF_LEVEL,
  SUSCAN_GUI_SPECTRUM_PARAM_DBS_PER_DIV,
};

enum suscan_gui_spectrum_mode {
  SUSCAN_GUI_SPECTRUM_MODE_SPECTROGRAM,
  SUSCAN_GUI_SPECTRUM_MODE_WATERFALL,
};

struct suscan_gui_spectrum {
  enum suscan_gui_spectrum_mode mode;
  unsigned width;
  unsigned height;
  int g_width;
  int g_height;

  uint64_t fc;
  SUFLOAT *psd_data;
  SUFLOAT *psd_data_smooth;
  SUSCOUNT psd_size;
  SUSCOUNT samp_rate;
  SUFLOAT  N0;
  SUSCOUNT updates; /* Number of spectrum updates */
  SUSCOUNT last_update; /* Last update in which waterfall has been repainted */
  SUBOOL   auto_level; /* Automatic reference level */
  SUFLOAT  agc_alpha; /* AGC alpha for smooth update */

  /* Representation properties */
  SUBOOL  show_channels; /* Defaults to TRUE */
  SUFLOAT freq_offset; /* Defaults to 0 */
  SUFLOAT freq_scale;  /* Defaults to 1 */
  SUFLOAT dbs_per_div; /* Defaults to 10 */
  SUFLOAT ref_level;   /* Defaults to 0 */
  SUFLOAT last_max;

  /* Waterfall members */
  SUBOOL flip;
  cairo_surface_t *wf_surf[2];
  SUFLOAT last_freq_offset;

  /* Scroll and motion state */
  gdouble last_x;
  gdouble last_y;

  gdouble  prev_ev_x;
  SUBOOL   dragging;
  SUFLOAT  original_freq_offset;
  SUFLOAT  original_ref_level;

  /* Current channel selection state */
  SUBOOL selecting;
  struct sigutils_channel selection;

  /* Current channel list */
  PTR_LIST(struct sigutils_channel, channel);
};

/* Spectrum API */
void suscan_gui_spectrum_init(struct suscan_gui_spectrum *spectrum);

void suscan_spectrum_finalize(struct suscan_gui_spectrum *spectrum);

void suscan_gui_spectrum_set_mode(
    struct suscan_gui_spectrum *spectrum,
    enum suscan_gui_spectrum_mode mode);

void suscan_gui_spectrum_reset(struct suscan_gui_spectrum *spectrum);

void suscan_gui_spectrum_update(
    struct suscan_gui_spectrum *spectrum,
    struct suscan_analyzer_psd_msg *msg);

void suscan_gui_spectrum_update_channels(
    struct suscan_gui_spectrum *spectrum,
    struct sigutils_channel **channel_list,
    unsigned int channel_count);

void suscan_gui_spectrum_configure(
    struct suscan_gui_spectrum *spectrum,
    GtkWidget *widget);

void suscan_gui_spectrum_redraw(
    struct suscan_gui_spectrum *spectrum,
    cairo_t *cr);

void suscan_gui_spectrum_parse_scroll(
    struct suscan_gui_spectrum *spectrum,
    const GdkEventScroll *ev);

void suscan_gui_spectrum_parse_motion(
    struct suscan_gui_spectrum *spectrum,
    const GdkEventMotion *ev);

void suscan_gui_spectrum_reset_selection(
    struct suscan_gui_spectrum *spectrum);

#endif /* _GUI_SPECTRUM_H */
