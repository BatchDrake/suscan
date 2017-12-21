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

#include <sigutils/sigutils.h>
#include <gtk/gtk.h>

#define SUSCAN_GUI_CONSTELLATION_HISTORY 200

struct suscan_gui_constellation {
  cairo_surface_t *surface;
  unsigned width;
  unsigned height;

  SUCOMPLEX phase;
  SUCOMPLEX history[SUSCAN_GUI_CONSTELLATION_HISTORY];
  unsigned int p;
};

/* Constellation API */
void suscan_gui_constellation_finalize(
    struct suscan_gui_constellation *constellation);

void suscan_gui_constellation_init(
    struct suscan_gui_constellation *constellation);

void suscan_gui_constellation_clear(
    struct suscan_gui_constellation *constellation);

void suscan_gui_constellation_push_sample(
    struct suscan_gui_constellation *constellation,
    SUCOMPLEX sample);

#endif /* _GUI_CONSTELLATION_H */
