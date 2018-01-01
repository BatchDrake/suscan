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

#ifndef _GUI_ESTIMATORUI_H
#define _GUI_ESTIMATORUI_H

#include <gtk/gtk.h>
#include <sigutils/sigutils.h>

struct suscan_gui_inspector;

struct suscan_gui_estimatorui_params {
  struct suscan_gui_inspector *inspector;
  const char *field;
  const char *desc;
  uint32_t estimator_id;
};

struct suscan_gui_estimatorui {
  uint32_t estimator_id;
  char *field;

  GtkBuilder *builder;

  GtkGrid *root;

  GtkToggleButton *enableToggleButton;
  GtkEntry *valueEntry;

  struct suscan_gui_inspector *inspector;

  SUFLOAT value;
};

typedef struct suscan_gui_estimatorui suscan_gui_estimatorui_t;

suscan_gui_estimatorui_t *suscan_gui_estimatorui_new(
    struct suscan_gui_estimatorui_params *params);

GtkWidget *suscan_gui_estimatorui_get_root(const suscan_gui_estimatorui_t *ui);

void suscan_gui_estimatorui_destroy(suscan_gui_estimatorui_t *ui);

#endif /* _GUI_ESTIMATORUI_H */
