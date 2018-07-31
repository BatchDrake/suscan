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

#ifndef _GUI_SYMTOOL_H
#define _GUI_SYMTOOL_H

#include "symsrc.h"

#define SUSCAN_GUI_SYMTOOL_MAX_BITS_PER_SYMBOL 0x10

enum suscan_gui_symtool_symfile_format {
  SUSCAN_GUI_SYMTOOL_SYMFILE_FORMAT_UNKNOWN,
  SUSCAN_GUI_SYMTOOL_SYMFILE_FORMAT_PLAIN_TEXT,
  SUSCAN_GUI_SYMTOOL_SYMFILE_FORMAT_BINARY,
};

struct suscan_gui_symtool_file_properties {
  enum suscan_gui_symtool_symfile_format format;
  int bits_per_symbol;
};

struct suscan_gui_symtool {
  struct suscan_gui_symsrc _parent;
  struct suscan_gui_symtool_file_properties properties;
  int index;

  /* Symbol tool properties */
  int bits_per_sym;

  /* Builder object */
  GtkBuilder *builder;

  /* Widgets */
  GtkGrid             *fileViewGrid;
  GtkGrid             *mainSymViewGrid;
  SuGtkSymView        *symbolView;
  GtkAdjustment       *symViewScrollAdjustment;
  GtkScrollbar        *symViewScrollbar;
  GtkNotebook         *codecNotebook;
  GtkSpinButton       *widthSpinButton;
  GtkToggleToolButton *autoFitToggleButton;
  GtkEventBox         *pageLabelEventBox;
  GtkLabel            *pageLabelLabel;
  GtkPaned            *mainPaned;
};

typedef struct suscan_gui_symtool suscan_gui_symtool_t;

suscan_gui_symtool_t *suscan_gui_symtool_new(
    const struct suscan_gui_symtool_file_properties *prop);

SUBOOL suscan_gui_symtool_helper_guess_properties(
    struct suscan_gui_symtool_file_properties *prop,
    const uint8_t *file_data,
    size_t file_size);

SUBOOL suscan_gui_symtool_load_file_data(
    suscan_gui_symtool_t *symtool,
    const uint8_t *file_data,
    size_t file_size);

GtkWidget *suscan_gui_symtool_get_root(const suscan_gui_symtool_t *symtool);

GtkWidget *suscan_gui_symtool_get_label(const suscan_gui_symtool_t *symtool);

void suscan_gui_symtool_destroy(suscan_gui_symtool_t *symtool);

#endif /* _GUI_SYMTOOL_H */
