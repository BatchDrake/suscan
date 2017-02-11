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

#include "ctk.h"

CTKPRIVATE void
ctk_menu_clear_c_item_list(ctk_menu_t *menu)
{
  ITEM **ptr;

  if (menu->c_item_list != NULL) {
    ptr = menu->c_item_list;

    while (*ptr != NULL)
      free_item(*ptr++);

    free(menu->c_item_list);

    menu->c_item_list = NULL;
  }
}

CTKPRIVATE ITEM **
ctk_item_list_to_ITEMpp(
    struct ctk_item * const *list,
    unsigned int sz)
{
  int i;
  ITEM **new;

  if ((new = malloc((sz + 1) * sizeof(ITEM *))) == NULL)
    return NULL;

  for (i = 0; i < (int) sz; ++i) {
    if ((new[i] = new_item(list[i]->name, list[i]->desc)) == NULL) {
      while (--i >= 0)
        free_item(new[i]);
      free(new);
      return NULL;
    }

    set_item_userptr(new[i], list[i]);
  }

  new[sz] = NULL;

  return new;
}

CTKPRIVATE CTKBOOL
ctk_menu_update_c_item_list(ctk_menu_t *menu)
{
  ITEM **new;

  if ((new =
      ctk_item_list_to_ITEMpp(menu->item_list, menu->item_count)) == NULL)
    return CTK_FALSE;

  if (menu->c_menu != NULL)
    set_menu_items(menu->c_menu, new);

  /* Safe to clear elements now */
  ctk_menu_clear_c_item_list(menu);

  menu->c_item_list = new;

  return CTK_TRUE;
}

CTKPRIVATE CTKBOOL
ctk_widget_menu_c_rescale(ctk_widget_t *widget)
{
  int width;
  int height;
  ctk_menu_t *menu;

  menu = CTK_WIDGET_AS_MENU(widget);

  scale_menu(menu->c_menu, &height, &width);

  height = menu->item_count;

  if (height > 16)
    height = 16;

  return ctk_widget_resize(widget, width + 2, height + 2);
}

/* Low-level add functions */
CTKPRIVATE CTKBOOL
__ctk_menu_add_item(
    ctk_menu_t *menu,
    const char *name,
    const char *desc,
    void *private)
{
  struct ctk_item *item = NULL;
  int id;

  if ((item = ctk_item_new(name, desc, private)) == NULL)
    return CTK_FALSE;

  if ((id = PTR_LIST_APPEND_CHECK(menu->item, item)) == -1) {
    ctk_item_destroy(item);
    return CTK_FALSE;
  }

  item->__index = id;

  return CTK_TRUE;
}

CTKPRIVATE CTKBOOL
__ctk_menu_add_multiple_items(
    ctk_menu_t *menu,
    const struct ctk_item *item,
    unsigned int count)
{
  unsigned int i;

  for (i = 0; i < count; ++i) {
    if (!__ctk_menu_add_item(
        menu,
        item[i].name,
        item[i].desc,
        item[i].private))
      return CTK_FALSE;
  }

  return CTK_TRUE;
}

CTKBOOL
ctk_menu_add_item(
    ctk_widget_t *widget,
    const char *name,
    const char *desc,
    void *private)
{
  ctk_menu_t *menu;
  CTKBOOL posted;
  CTK_WIDGET_ASSERT_CLASS(widget, CTK_WIDGET_CLASS_MENU);

  menu = CTK_WIDGET_AS_MENU(widget);
  posted = menu->item_count > 0;

  if (!__ctk_menu_add_item(menu, name, desc, private))
    return CTK_FALSE;

  if (posted)
    if (unpost_menu(menu->c_menu) != E_OK)
      return CTK_FALSE;

  /* Modifications on the menu must be performed after unposting it */
  if (!ctk_menu_update_c_item_list(menu))
    return CTK_FALSE;

  /* Rescale widget according to new elements */
  if (!ctk_widget_menu_c_rescale(widget))
    return CTK_FALSE;

  if (post_menu(menu->c_menu) != E_OK)
    return CTK_FALSE;

  ctk_widget_refresh(widget);

  return CTK_TRUE;
}

CTKBOOL
ctk_menu_add_multiple_items(
    ctk_widget_t *widget,
    const struct ctk_item *items,
    unsigned int count)
{
  ctk_menu_t *menu;
  CTKBOOL posted;

  CTK_WIDGET_ASSERT_CLASS(widget, CTK_WIDGET_CLASS_MENU);

  menu = CTK_WIDGET_AS_MENU(widget);
  posted = menu->item_count > 0;

  if (!__ctk_menu_add_multiple_items(menu, items, count))
    return CTK_FALSE;

  if (posted)
    if (unpost_menu(menu->c_menu) != E_OK)
      return CTK_FALSE;

  /* Modifications on the menu must be performed after unposting it */
  if (!ctk_menu_update_c_item_list(menu))
    return CTK_FALSE;

  /* Rescale widget according to new elements */
  if (!ctk_widget_menu_c_rescale(widget))
    return CTK_FALSE;

  if (post_menu(menu->c_menu) != E_OK)
    return CTK_FALSE;

  ctk_widget_refresh(widget);

  return CTK_TRUE;
}

CTKBOOL
ctk_menu_set_title(ctk_widget_t *widget, const char *title)
{
  char *dup;
  ctk_menu_t *menu;

  CTK_WIDGET_ASSERT_CLASS(widget, CTK_WIDGET_CLASS_MENU);

  menu = CTK_WIDGET_AS_MENU(widget);

  if ((dup = strdup(title)) == NULL)
    return CTK_FALSE;

  if (menu->title != NULL)
    free(menu->title);

  menu->title = dup;

  return CTK_TRUE;
}

const char *
ctk_menu_get_title(ctk_widget_t *widget)
{
  ctk_menu_t *menu;

  CTK_WIDGET_ASSERT_CLASS(widget, CTK_WIDGET_CLASS_MENU);

  menu = CTK_WIDGET_AS_MENU(widget);

  return menu->title;
}

CTKPRIVATE CTKBOOL
ctk_menu_on_resize(ctk_widget_t *widget, unsigned width, unsigned height)
{
  if (wresize(
      CTK_WIDGET_AS_MENU(widget)->c_sub,
      height - 2,
      width - 2) == ERR)
    return CTK_FALSE;

  return CTK_TRUE;
}

CTKPRIVATE void
ctk_menu_on_kbd(ctk_widget_t *widget, int c)
{
  ctk_menu_t *menu;
  ITEM *curr_item;

  menu = CTK_WIDGET_AS_MENU(widget);

  if (widget->visible) {
    switch (c) {
      case KEY_UP:
        menu_driver(menu->c_menu, REQ_UP_ITEM);
        break;

      case KEY_DOWN:
        menu_driver(menu->c_menu, REQ_DOWN_ITEM);
        break;

      case KEY_ENTER:
      case '\n':
        if ((curr_item = current_item(menu->c_menu)) != NULL) {
          ctk_widget_submit(
              widget,
              (struct ctk_item *) item_userptr(curr_item));
        }

        break;
    }

    ctk_widget_refresh(widget);
  }
}

CTKPRIVATE void
ctk_menu_on_destroy(ctk_widget_t *widget)
{
  ctk_menu_t *menu;
  unsigned int i;
  menu = CTK_WIDGET_AS_MENU(widget);

  if (menu->title != NULL)
    free(menu->title);

  for (i = 0; i < menu->item_count; ++i)
    if (menu->item_list[i] != NULL)
      ctk_item_destroy(menu->item_list[i]);

  if (menu->item_list != NULL)
    free(menu->item_list);

  if (menu->c_menu != NULL) {
    unpost_menu(menu->c_menu);
    free_menu(menu->c_menu);
  }

  if (menu->c_sub != NULL)
    delwin(menu->c_sub);

  /* Menu has to be deleted first! */
  ctk_menu_clear_c_item_list(menu);
}

unsigned int
ctk_menu_get_item_count(const ctk_widget_t *widget)
{
  ctk_menu_t *menu;

  CTK_WIDGET_ASSERT_CLASS(widget, CTK_WIDGET_CLASS_MENU);

  menu = CTK_WIDGET_AS_MENU(widget);

  return menu->item_count;
}

struct ctk_item *
ctk_menu_get_first_item(const ctk_widget_t *widget)
{
  unsigned int i;
  ctk_menu_t *menu;

  CTK_WIDGET_ASSERT_CLASS(widget, CTK_WIDGET_CLASS_MENU);

  menu = CTK_WIDGET_AS_MENU(widget);

  for (i = 0; i < menu->item_count; ++i)
    if (menu->item_list[i] != NULL)
      return menu->item_list[i];

  return NULL;
}

unsigned int
ctk_menu_get_max_item_name_length(const ctk_widget_t *widget)
{
  unsigned int i;
  ctk_menu_t *menu;
  unsigned int len = 0;

  CTK_WIDGET_ASSERT_CLASS(widget, CTK_WIDGET_CLASS_MENU);

  menu = CTK_WIDGET_AS_MENU(widget);

  for (i = 0; i < menu->item_count; ++i)
    if (menu->item_list[i] != NULL)
      if (strlen(menu->item_list[i]->name) > len)
        len = strlen(menu->item_list[i]->name);

  return len;
}

ctk_widget_t *
ctk_menu_new(unsigned int x, unsigned int y)
{
  ctk_widget_t *widget = NULL;
  ctk_menu_t *new;
  struct ctk_widget_handlers handlers;

  if ((widget = ctk_widget_ctor_start(
      NULL,
      x,
      y,
      4,
      4,
      CTK_WIDGET_SUB_ALLOC_SIZE(ctk_menu_t))) == NULL)
    goto fail;

  ctk_widget_set_border(widget, CTK_TRUE);
  ctk_widget_set_popup(widget, CTK_TRUE);
  ctk_widget_set_shadow(widget, CTK_TRUE);

  widget->class = CTK_WIDGET_CLASS_MENU;

  new = CTK_WIDGET_AS_MENU(widget);

  /* Install handlers */
  ctk_widget_get_handlers(widget, &handlers);

  handlers.kbd_handler = ctk_menu_on_kbd;
  handlers.resize_handler = ctk_menu_on_resize;
  handlers.dtor_handler = ctk_menu_on_destroy;

  ctk_widget_set_handlers(widget, &handlers);

  /* Create curses objects */
  if (!ctk_menu_update_c_item_list(new))
    goto fail;

  if ((new->c_sub = derwin(widget->c_window, 2, 2, 1, 1)) == NULL)
    goto fail;

  if ((new->c_menu = new_menu(new->c_item_list)) == NULL)
    goto fail;

  set_menu_win(new->c_menu, widget->c_window);
  set_menu_sub(new->c_menu, new->c_sub);

  if (!ctk_widget_ctor_end(widget))
    goto fail;

  return widget;

fail:
  if (widget != NULL)
    ctk_widget_destroy(widget);

  return NULL;
}

