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
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>

#include "ctk.h"

CTKPRIVATE int
ctk_menubar_find_new_accel(const ctk_menubar_t *bar, const char *title)
{
  unsigned int i;

  for (i = 0; i < strlen(title); ++i)
    if (ctk_widget_lookup_index_by_accel(
        bar->menu_list,
        bar->menu_count,
        title[i]) == -1)
      return title[i];

  /* Failed to find a new accelerator, this menu will not have one */
  return 0;
}

CTKPRIVATE void
ctk_menubar_on_redraw(ctk_widget_t *widget)
{
  unsigned int ptr = 2;
  ctk_menubar_t *bar;
  ctk_widget_t *curr_menu = NULL;
  const char *title;
  unsigned int i, j, len;

  bar = CTK_WIDGET_AS_MENUBAR(widget);

  for (i = 0; i < bar->menu_count; ++i) {
    if ((curr_menu = bar->menu_list[i]) != NULL) {
      title = CTK_WIDGET_AS_MENU(curr_menu)->title;
      len = strlen(title);

      wattron(widget->c_window, COLOR_PAIR(i == bar->active ? 3 : 2));
      mvwaddstr(widget->c_window, 0, ptr, title);

      /* Highlight accelerator */
      if (curr_menu->accel != 0)
        for (j = 0; j < len; ++j)
          if (tolower(title[j]) == curr_menu->accel) {
            wattron(widget->c_window, A_UNDERLINE);
            if (bar->escape && bar->active == -1)
              wattron(widget->c_window, A_BOLD | COLOR_PAIR(4));
            mvwaddch(widget->c_window, 0, ptr + j, title[j]);
            if (bar->escape && bar->active == -1)
              wattroff(widget->c_window, A_BOLD | COLOR_PAIR(4));
            wattroff(widget->c_window, A_UNDERLINE);
            break;
          }

      ctk_widget_move(bar->menu_list[i], ptr - 1, 1);

      ptr += len + 3;
    }
  }
}

CTKBOOL
ctk_menubar_add_menu(ctk_widget_t *widget, const char *title, ctk_widget_t *menu)
{
  ctk_menubar_t *bar;
  int accel;

  CTK_WIDGET_ASSERT_CLASS(widget, CTK_WIDGET_CLASS_MENUBAR);

  bar = CTK_WIDGET_AS_MENUBAR(widget);

  if (!ctk_menu_set_title(menu, title))
    return CTK_FALSE;

  accel = ctk_menubar_find_new_accel(bar, title);
  ctk_widget_set_accel(menu, accel);

  if (PTR_LIST_APPEND_CHECK(bar->menu, menu) == -1)
    return CTK_FALSE;

  ctk_widget_redraw(widget);

  return CTK_TRUE;
}

CTKPRIVATE CTKBOOL
ctk_menubar_set_active(ctk_menubar_t *bar, int index)
{
  if (index != -1 && (index < 0 || index >= bar->menu_count))
    return CTK_FALSE;

  if (bar->active != -1)
    ctk_widget_hide(bar->menu_list[bar->active]);

  bar->active = index;

  if (bar->active != -1)
    ctk_widget_show(bar->menu_list[bar->active]);

  return CTK_TRUE;
}

CTKPRIVATE void
ctk_menubar_on_kbd(ctk_widget_t *widget, int c)
{
  ctk_menubar_t *bar;
  int active;

  bar = CTK_WIDGET_AS_MENUBAR(widget);

  if (widget->visible) {
    if (!bar->escape) {
      if (c == CTK_KEY_ESCAPE) {
        /* Enable escaped mode: menu selection */
        bar->escape = CTK_TRUE;
        bar->active = -1;
      }
    } else {
      if (bar->active == -1) {
        /* Pressing down enables first element */
        if (c == KEY_DOWN)
          ctk_menubar_set_active(bar, 0);
        else if (c != CTK_KEY_ESCAPE) {
          /* Pressing a character key opens a new menu */
          if ((active = ctk_widget_lookup_index_by_accel(
              bar->menu_list,
              bar->menu_count,
              c)) != -1)
            ctk_menubar_set_active(bar, active);
          else
            bar->escape = CTK_FALSE; /* Leave escape mode */
        }
      } else {
        /* Active menu, forward keys to selected menu */
        if (c == CTK_KEY_ESCAPE)
          /* Leave current menu */
          ctk_menubar_set_active(bar, -1);
        else if (c == KEY_LEFT) {
          /* Show next menu */
          if (bar->active > 0)
            ctk_menubar_set_active(bar, bar->active - 1);
        }
        else if (c == KEY_RIGHT) {
          /* Show previos menu */
          if (bar->active < bar->menu_count - 1)
            ctk_menubar_set_active(bar, bar->active + 1);
        }
        else {
          /* Forward key to menu */
          ctk_widget_notify_kbd(bar->menu_list[bar->active], c);

          if (c == KEY_ENTER || c == '\n') {
            /* Menu option enabled, menu can be hidden now */
            ctk_menubar_set_active(bar, -1);
            bar->escape = CTK_FALSE; /* Leave escape mode */
          }
        }
      }
    }

    ctk_widget_redraw(widget);
    ctk_widget_refresh(widget);
  }
}

CTKPRIVATE void
ctk_menubar_on_destroy(ctk_widget_t *widget)
{
  ctk_menubar_t *bar;

  bar = CTK_WIDGET_AS_MENUBAR(widget);

  if (bar->menu_list != NULL)
    free(bar->menu_list);
}

ctk_widget_t *
ctk_menubar_new(void)
{
  ctk_widget_t *widget = NULL;
  ctk_menubar_t *bar;
  struct ctk_widget_handlers handlers;

  if ((widget = ctk_widget_ctor_start(
      NULL,
      0,
      0,
      COLS,
      1,
      CTK_WIDGET_SUB_ALLOC_SIZE(ctk_menubar_t))) == NULL)
    goto fail;

  widget->class = CTK_WIDGET_CLASS_MENUBAR;
  ctk_widget_set_attrs(widget, COLOR_PAIR(CTK_CP_WIDGET));

  bar = CTK_WIDGET_AS_MENUBAR(widget);
  bar->active = -1;

  /* Install handlers */
  ctk_widget_get_handlers(widget, &handlers);

  handlers.kbd_handler = ctk_menubar_on_kbd;
  handlers.dtor_handler = ctk_menubar_on_destroy;
  handlers.redraw_handler = ctk_menubar_on_redraw;

  ctk_widget_set_handlers(widget, &handlers);

  if (!ctk_widget_ctor_end(widget))
    goto fail;

  ctk_widget_redraw(widget);

  return widget;

fail:

  if (widget != NULL)
    ctk_widget_destroy(widget);

  return NULL;
}
