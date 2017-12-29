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

#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <libgen.h>

#define SU_LOG_DOMAIN "symtool"

#include "gui.h"
#include "symtool.h"

void suscan_symtool_on_reshape(GtkWidget *wdiget, gpointer data);

SUPRIVATE void
suscan_gui_symtool_run_encoder(GtkWidget *widget, gpointer *data)
{
  /* Do nothing */
}

SUPRIVATE void
suscan_gui_symtool_run_decoder(GtkWidget *widget, gpointer *data)
{
  /* Do nothing */
}

SUPRIVATE void *
suscan_gui_symtool_dummy_create_private(
    void *unused,
    struct suscan_gui_codec_cfg_ui *ui)
{
  return ui;
}

SUPRIVATE SUBOOL
suscan_gui_symtool_load_all_widgets(suscan_gui_symtool_t *symtool)
{
  SU_TRYCATCH(
      symtool->fileViewGrid =
          GTK_GRID(gtk_builder_get_object(
              symtool->builder,
              "grFileView")),
          return SU_FALSE);

  SU_TRYCATCH(
      symtool->mainSymViewGrid =
          GTK_GRID(gtk_builder_get_object(
              symtool->builder,
              "grMainSymView")),
          return SU_FALSE);

  SU_TRYCATCH(
      symtool->codecNotebook =
          GTK_NOTEBOOK(gtk_builder_get_object(
              symtool->builder,
              "nbCodec")),
          return SU_FALSE);

  SU_TRYCATCH(
      symtool->widthSpinButton =
          GTK_SPIN_BUTTON(gtk_builder_get_object(
              symtool->builder,
              "sbWidth")),
          return SU_FALSE);

  SU_TRYCATCH(
      symtool->pageLabelEventBox =
          GTK_EVENT_BOX(gtk_builder_get_object(
              symtool->builder,
              "ebPageLabel")),
          return SU_FALSE);

  SU_TRYCATCH(
      symtool->pageLabelLabel =
          GTK_LABEL(gtk_builder_get_object(
              symtool->builder,
              "lPageLabel")),
          return SU_FALSE);

  SU_TRYCATCH(
      symtool->autoFitToggleButton =
          GTK_TOGGLE_TOOL_BUTTON(gtk_builder_get_object(
              symtool->builder,
              "tbAutoFit")),
          return SU_FALSE);

  SU_TRYCATCH(
      symtool->symViewScrollAdjustment =
          GTK_ADJUSTMENT(gtk_builder_get_object(
              symtool->builder,
              "aSymViewScroll")),
          return SU_FALSE);

  SU_TRYCATCH(
      symtool->symViewScrollBar =
          GTK_SCROLLBAR(gtk_builder_get_object(
              symtool->builder,
              "sbSymView")),
          return SU_FALSE);

  SU_TRYCATCH(
      symtool->mainPaned =
          GTK_PANED(gtk_builder_get_object(
              symtool->builder,
              "pMain")),
          return SU_FALSE);

  /* Add symbol view */
  symtool->symbolView = SUGTK_SYM_VIEW(sugtk_sym_view_new());

  sugtk_sym_view_set_autofit(symtool->symbolView, TRUE);
  sugtk_sym_view_set_autoscroll(symtool->symbolView, FALSE);

  g_signal_connect(
      G_OBJECT(symtool->symbolView),
      "reshape",
      G_CALLBACK(suscan_symtool_on_reshape),
      symtool);

  gtk_grid_attach(
      symtool->mainSymViewGrid,
      GTK_WIDGET(symtool->symbolView),
      0, /* left */
      0, /* top */
      1, /* width */
      1 /* height */);

  gtk_widget_set_hexpand(GTK_WIDGET(symtool->symbolView), TRUE);
  gtk_widget_set_vexpand(GTK_WIDGET(symtool->symbolView), TRUE);

  gtk_widget_show(GTK_WIDGET(symtool->symbolView));

  SU_TRYCATCH(
      suscan_gui_symsrc_populate_codec_menu(
          &symtool->_parent,
          symtool->symbolView,
          suscan_gui_symtool_dummy_create_private,
          NULL,
          G_CALLBACK(suscan_gui_symtool_run_encoder),
          G_CALLBACK(suscan_gui_symtool_run_decoder)),
      return SU_FALSE);

  return SU_TRUE;
}

SUBOOL
suscan_gui_symtool_helper_guess_properties(
    struct suscan_gui_symtool_file_properties *prop,
    const uint8_t *file_data,
    size_t file_size)
{
  unsigned int page_size;
  uint8_t bitmap = 0;
  unsigned int i;

  page_size = sysconf(_SC_PAGE_SIZE);

  /* Refusing to read beyond certain limit */
  if (file_size > page_size)
    file_size = page_size;

  /* First test: or all bits, and count them */
  for (i = 0; i < file_size; ++i)
    bitmap |= file_data[i];

  /* Regular plaintext */
  if (bitmap >= '0' && bitmap < '8') {
    prop->format = SUSCAN_GUI_SYMTOOL_SYMFILE_FORMAT_PLAIN_TEXT;

    /* Guess bits per symbol */
    if (bitmap < '2')
      prop->bits_per_symbol = 1;
    else if (bitmap < '4')
      prop->bits_per_symbol = 2;
    else
      prop->bits_per_symbol = 3;
  } else if (bitmap < SUSCAN_GUI_SYMTOOL_MAX_BITS_PER_SYMBOL) {
    /* Binary file */
    prop->format = SUSCAN_GUI_SYMTOOL_SYMFILE_FORMAT_BINARY;

    prop->bits_per_symbol = 1;

    while ((1 << prop->bits_per_symbol) <= bitmap)
      ++prop->bits_per_symbol;
  } else {
    /* Assume this is an error */
    return SU_FALSE;
  }

  return SU_TRUE;
}

SUPRIVATE void
suscan_gui_symtool_update_spin_buttons(suscan_gui_symtool_t *symtool)
{
  unsigned int total_rows;
  unsigned int page_rows;

  if (gtk_toggle_tool_button_get_active(
      GTK_TOGGLE_TOOL_BUTTON(symtool->autoFitToggleButton)))
    gtk_spin_button_set_value(
        symtool->widthSpinButton,
        sugtk_sym_view_get_width(symtool->symbolView));

  /* This is not totally correct */
  total_rows =
      sugtk_sym_view_get_buffer_size(symtool->symbolView)
      / (SUGTK_SYM_VIEW_STRIDE_ALIGN
          * sugtk_sym_view_get_width(symtool->symbolView)) + 1;

  page_rows = sugtk_sym_view_get_height(symtool->symbolView);

  if (total_rows < page_rows) {
    gtk_widget_set_sensitive(GTK_WIDGET(symtool->symViewScrollBar), FALSE);
    gtk_adjustment_set_page_size(symtool->symViewScrollAdjustment, page_rows);
    gtk_adjustment_set_upper(
        symtool->symViewScrollAdjustment,
        page_rows);
    gtk_adjustment_set_value(symtool->symViewScrollAdjustment, 0);
  } else {
    gtk_adjustment_set_page_size(symtool->symViewScrollAdjustment, page_rows);
    gtk_adjustment_set_upper(
        symtool->symViewScrollAdjustment,
        total_rows);
    gtk_adjustment_set_value(
        symtool->symViewScrollAdjustment,
        sugtk_sym_view_get_offset(symtool->symbolView)
        / sugtk_sym_view_get_width(symtool->symbolView));
    gtk_widget_set_sensitive(GTK_WIDGET(symtool->symViewScrollBar), TRUE);
  }
}

SUBOOL
suscan_gui_symtool_load_file_data(
    suscan_gui_symtool_t *symtool,
    const uint8_t *file_data,
    size_t file_size)
{
  SUBITS *syms;
  unsigned int i;
  SUSYMBOL max;

  SU_TRYCATCH(symtool->_parent.gui != NULL, return SU_FALSE);

  switch (symtool->properties.format) {
    case SUSCAN_GUI_SYMTOOL_SYMFILE_FORMAT_PLAIN_TEXT:
      SU_TRYCATCH(
          syms = suscan_gui_symsrc_assert(&symtool->_parent, file_size),
          return SU_FALSE);
      max = SU_TOSYM(1 << symtool->properties.bits_per_symbol);

      for (i = 0; i < file_size; ++i) {
        if (file_data[i] >= '0' && file_data[i] < max)
          syms[i] = SU_FROMSYM(file_data[i]);
        else
          break;

        sugtk_sym_view_append(
            symtool->symbolView,
            sugtk_sym_view_code_to_pixel_helper(
                symtool->properties.bits_per_symbol,
                syms[i]));
      }

      if (i < file_size) {
        memset(syms + i, 0, sizeof (SUBITS) * (file_size - i));
        suscan_error(
            symtool->_parent.gui,
            "Read symbol file",
            "Invalid symbol character found in position %d",
            i);
      }

      SU_TRYCATCH(
          suscan_gui_symsrc_commit(&symtool->_parent),
          return SU_FALSE);

      break;

    case SUSCAN_GUI_SYMTOOL_SYMFILE_FORMAT_BINARY:
      SU_TRYCATCH(
          syms = suscan_gui_symsrc_assert(&symtool->_parent, file_size),
          return SU_FALSE);
      max = SU_TOSYM(1 << symtool->properties.bits_per_symbol);

      for (i = 0; i < file_size; ++i) {
        if (file_data[i] < max)
          syms[i] = file_data[i];
        else
          break;

        SU_TRYCATCH(
            sugtk_sym_view_append(
                symtool->symbolView,
                sugtk_sym_view_code_to_pixel_helper(
                    symtool->properties.bits_per_symbol,
                    syms[i])),
            return SU_FALSE);
      }

      if (i < file_size) {
        memset(syms + i, 0, sizeof (SUBITS) * (file_size - i));
        suscan_error(
            symtool->_parent.gui,
            "Read symbol file",
            "Invalid symbol byte found in position %d",
            i);
      }

      SU_TRYCATCH(
          suscan_gui_symsrc_commit(&symtool->_parent),
          return SU_FALSE);

      break;
  }

  suscan_gui_symtool_update_spin_buttons(symtool);

  return SU_TRUE;
}

suscan_gui_symtool_t *
suscan_gui_symtool_new(const struct suscan_gui_symtool_file_properties *prop)
{
  suscan_gui_symtool_t *new = NULL;

  SU_TRYCATCH(new = calloc(1, sizeof(suscan_gui_symtool_t)), goto fail);

  SU_TRYCATCH(suscan_gui_symsrc_init(&new->_parent, NULL), goto fail);

  new->index = -1;
  new->properties = *prop;

  SU_TRYCATCH(
      new->builder = gtk_builder_new_from_file(
          PKGDATADIR "/gui/symbol-tool.glade"),
      goto fail);

  SU_TRYCATCH(suscan_gui_symtool_load_all_widgets(new), goto fail);

  gtk_builder_connect_signals(new->builder, new);

  return new;

fail:
  if (new != NULL)
    suscan_gui_symtool_destroy(new);

  return NULL;
}

GtkWidget *
suscan_gui_symtool_get_root(const suscan_gui_symtool_t *symtool)
{
  return GTK_WIDGET(symtool->fileViewGrid);
}

GtkWidget *
suscan_gui_symtool_get_label(const suscan_gui_symtool_t *symtool)
{
  return GTK_WIDGET(symtool->pageLabelEventBox);
}

void
suscan_gui_symtool_set_title(
    const suscan_gui_symtool_t *symtool,
    const char *title)
{
  gtk_label_set_text(symtool->pageLabelLabel, title);
}

void
suscan_gui_symtool_destroy(suscan_gui_symtool_t *symtool)
{

  if (symtool->builder != NULL)
    g_object_unref(G_OBJECT(symtool->builder));

  if (!suscan_gui_symsrc_finalize(&symtool->_parent)) {
    SU_ERROR("Symbol tool destruction failed somehow\n");
    return;
  }

  free(symtool);
}

/******************************* UI Callbacks ********************************/
void
suscan_on_open_symbol_file(GtkWidget *widget, gpointer *data)
{
  suscan_gui_t *gui = (suscan_gui_t *) data;
  GtkWidget *dialog = NULL;
  GtkFileChooser *chooser;
  suscan_gui_symtool_t *symtool_new = NULL;
  suscan_gui_symtool_t *symtool = NULL;
  gchar *path = NULL;
  char *title;
  int fd = -1;
  uint8_t *file_data = (uint8_t *) -1;
  size_t file_size;
  struct suscan_gui_symtool_file_properties prop;

  dialog = gtk_file_chooser_dialog_new(
      "Open symbol file",
      gui->main,
      GTK_FILE_CHOOSER_ACTION_OPEN,
      "_Cancel",
      GTK_RESPONSE_CANCEL,
      "_Open",
      GTK_RESPONSE_ACCEPT,
      NULL);

  chooser = GTK_FILE_CHOOSER(dialog);

  gtk_file_chooser_set_do_overwrite_confirmation(chooser, TRUE);

  if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
    if ((path = gtk_file_chooser_get_filename(chooser)) == NULL) {
      suscan_error(
          gui,
          "Open file",
          "Selected file is not representable in the filesystem");
      goto done;
    }

    if ((fd = open(path, O_RDONLY)) == -1) {
      suscan_error(
          gui,
          "Open file",
          "Failed to open file: %s",
          strerror(errno));
      goto done;
    }

    if ((file_size = lseek(fd, 0, SEEK_END)) == -1) {
      suscan_error(
          gui,
          "Open file",
          "Failed to get file size: %s",
          strerror(errno));
      goto done;
    }

    if ((file_data = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0))
        == (const uint8_t *) -1) {
      suscan_error(
          gui,
          "Open file",
          "Failed to map file to memory: %s",
          strerror(errno));
      goto done;
    }

    if (!suscan_gui_symtool_helper_guess_properties(
        &prop,
        file_data,
        file_size)) {
      suscan_error(
          gui,
          "Open file",
          "Unrecognized symbol file");
      goto done;
    }

    SU_TRYCATCH(symtool_new = suscan_gui_symtool_new(&prop), goto done);

    title = basename((char *) path);

    suscan_gui_symtool_set_title(symtool_new, title);

    SU_TRYCATCH(suscan_gui_add_symtool(gui, symtool_new), goto done);

    symtool = symtool_new;

    SU_TRYCATCH(
        suscan_gui_symtool_load_file_data(symtool, file_data, file_size),
        goto done);

    symtool = symtool_new = NULL;
  }

done:
  if (symtool != NULL)
    suscan_gui_remove_symtool(gui, symtool);

  if (symtool_new != NULL)
      suscan_gui_symtool_destroy(symtool_new);

  if (file_data != (const uint8_t *) -1)
    munmap(file_data, file_size);

  if (fd != -1)
    close(fd);

  if (path != NULL)
    g_free(path);

  if (dialog != NULL)
    gtk_widget_destroy(dialog);
}

void
suscan_symtool_on_zoom_in(GtkWidget *widget, gpointer data)
{
  suscan_gui_symtool_t *symtool = (suscan_gui_symtool_t *) data;
  guint curr_width = sugtk_sym_view_get_width(symtool->symbolView);
  guint curr_zoom = sugtk_sym_view_get_zoom(symtool->symbolView);

  curr_zoom <<= 1;

  if (curr_width < curr_zoom)
    curr_zoom = curr_width;

  sugtk_sym_view_set_zoom(symtool->symbolView, curr_zoom);
}


void
suscan_symtool_on_zoom_out(GtkWidget *widget, gpointer data)
{
  suscan_gui_symtool_t *symtool = (suscan_gui_symtool_t *) data;
  guint curr_width = sugtk_sym_view_get_width(symtool->symbolView);
  guint curr_zoom = sugtk_sym_view_get_zoom(symtool->symbolView);

  curr_zoom >>= 1;

  if (curr_zoom < 1)
    curr_zoom = 1;

  sugtk_sym_view_set_zoom(symtool->symbolView, curr_zoom);
}

void
suscan_symtool_on_set_width(GtkWidget *widget, gpointer data)
{
  suscan_gui_symtool_t *symtool = (suscan_gui_symtool_t *) data;

  if (!gtk_toggle_tool_button_get_active(
      GTK_TOGGLE_TOOL_BUTTON(symtool->autoFitToggleButton)))
    sugtk_sym_view_set_width(
        symtool->symbolView,
        gtk_spin_button_get_value(symtool->widthSpinButton));
}

void
suscan_symtool_on_toggle_autofit(GtkWidget *widget, gpointer data)
{
  suscan_gui_symtool_t *symtool = (suscan_gui_symtool_t *) data;
  gboolean active;

  active = gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(widget));

  sugtk_sym_view_set_autofit(symtool->symbolView, active);
  gtk_widget_set_sensitive(GTK_WIDGET(symtool->widthSpinButton), !active);
}

void
suscan_symtool_on_reshape(GtkWidget *wdiget, gpointer data)
{
  suscan_gui_symtool_t *symtool = (suscan_gui_symtool_t *) data;

  suscan_gui_symtool_update_spin_buttons(symtool);
}

void
suscan_on_close_symtool(GtkWidget *widget, gpointer data)
{
  suscan_gui_symtool_t *symtool = (suscan_gui_symtool_t *) data;

  suscan_gui_remove_symtool(symtool->_parent.gui, symtool);

  suscan_gui_symtool_destroy(symtool);
}

void
suscan_symtool_on_scroll(GtkWidget *widget, gpointer data)
{
  suscan_gui_symtool_t *symtool = (suscan_gui_symtool_t *) data;

  sugtk_sym_view_set_offset(
      symtool->symbolView,
      floor(gtk_adjustment_get_value(symtool->symViewScrollAdjustment))
      * sugtk_sym_view_get_width(symtool->symbolView));
}

void
suscan_symtool_on_size_allocate(
    GtkWidget *widget,
    GtkAllocation *allocation,
    gpointer data)
{
  gtk_paned_set_position(GTK_PANED(widget), allocation->width / 2);
}

