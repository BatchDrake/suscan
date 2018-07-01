/*

  Copyright (C) 2018 Gonzalo José Carracedo Carballal

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

#define SU_LOG_DOMAIN "gui-symtool"

#include <confdb.h>
#include "modemctl.h"
#include "gui.h"


/*************************** Symbol tool handling ****************************/
SUBOOL
suscan_gui_remove_symtool(
    suscan_gui_t *gui,
    suscan_gui_symtool_t *symtool)
{
  gint num;
  int index = symtool->index;
  if (index < 0 || index >= gui->symtool_count)
    return SU_FALSE;

  SU_TRYCATCH(gui->symtool_list[index] == symtool, return SU_FALSE);

  SU_TRYCATCH(
      (num = gtk_notebook_page_num(
          gui->symToolNotebook,
          suscan_gui_symtool_get_root(symtool))) != -1,
      return SU_FALSE);

  gtk_notebook_remove_page(gui->symToolNotebook, num);

  gui->symtool_list[index] = NULL;

  return SU_TRUE;
}

SUBOOL
suscan_gui_add_symtool(
    suscan_gui_t *gui,
    suscan_gui_symtool_t *symtool)
{
  gint page;
  SUBOOL symtool_added = SU_FALSE;

  SU_TRYCATCH(
      (symtool->index = PTR_LIST_APPEND_CHECK(gui->symtool, symtool)) != -1,
      goto fail);

  symtool_added = SU_TRUE;
  symtool->_parent.gui = gui;

  SU_TRYCATCH(
      (page = gtk_notebook_append_page_menu(
          gui->symToolNotebook,
          suscan_gui_symtool_get_root(symtool),
          suscan_gui_symtool_get_label(symtool),
          NULL)) >= 0,
      goto fail);

  gtk_notebook_set_tab_reorderable(
      gui->symToolNotebook,
      suscan_gui_symtool_get_label(symtool),
      TRUE);

  gtk_notebook_set_current_page(gui->symToolNotebook, page);

  return TRUE;

fail:
  if (symtool_added)
    (void) suscan_gui_remove_symtool(gui, symtool);

  return FALSE;
}

suscan_gui_symtool_t *
suscan_gui_get_symtool(const suscan_gui_t *gui, uint32_t symtool_id)
{
  if (symtool_id >= gui->symtool_count)
    return NULL;

  return gui->symtool_list[symtool_id];
}
