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

#define _GNU_SOURCE

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "ctk.h"

CTKPRIVATE CTKBOOL
ctk_menu_c_menu_should_be_posted(const ctk_menu_t *menu)
{
  return menu->c_item_list != NULL && menu->c_item_list[0] != NULL;
}

CTKPRIVATE void
ITEMpp_destroy(ITEM **c_item_list)
{
  ITEM **ptr;

  if (c_item_list != NULL) {
    ptr = c_item_list;

    while (*ptr != NULL)
      free_item(*ptr++);

    free(c_item_list);
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
    if ((new[i] = new_item(list[i]->__printable_name, list[i]->desc)) == NULL) {
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
ctk_menu_update_c_item_list(ctk_menu_t *menu, ITEM ***old_c_item_list)
{
  ITEM **new;

  if ((new =
      ctk_item_list_to_ITEMpp(menu->item_list, menu->item_count)) == NULL)
    return CTK_FALSE;

  if (menu->c_menu != NULL)
    if (set_menu_items(menu->c_menu, new) != E_OK) {
      ITEMpp_destroy(new);
      return CTK_FALSE;
    }

  if (old_c_item_list != NULL)
    *old_c_item_list = menu->c_item_list;

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

  return ctk_widget_resize(widget, width + 2, height + 2);
}

/* Low-level add functions */
CTKBOOL
__ctk_menu_add_item(
    ctk_widget_t *widget,
    const char *name,
    const char *desc,
    void *private)
{
  ctk_menu_t *menu = CTK_WIDGET_AS_MENU(widget);
  struct ctk_item *item = NULL;
  int id;

  if ((item = ctk_item_new(name, desc, private)) == NULL)
    return CTK_FALSE;

  ctk_item_remove_non_printable(item, widget->width - 2);

  if ((id = PTR_LIST_APPEND_CHECK(menu->item, item)) == -1) {
    ctk_item_destroy(item);
    return CTK_FALSE;
  }

  item->__index = id;

  return CTK_TRUE;
}

CTKPRIVATE CTKBOOL
__ctk_menu_add_multiple_items(
    ctk_widget_t *widget,
    const struct ctk_item *item,
    unsigned int count)
{
  unsigned int i;

  for (i = 0; i < count; ++i) {
    if (!__ctk_menu_add_item(
        widget,
        item[i].name,
        item[i].desc,
        item[i].private))
      return CTK_FALSE;
  }

  return CTK_TRUE;
}

CTKPRIVATE int
__ctk_menu_cmp(const void *a, const void *b, void *priv)
{
  int (*cmp)(const struct ctk_item *, const struct ctk_item *) =
      (int (*)(const struct ctk_item *, const struct ctk_item *)) priv;
  const ITEM *c_a = *(const ITEM **) a;
  const ITEM *c_b = *(const ITEM **) b;

  return (cmp) (
      (const struct ctk_item *) item_userptr(c_a),
      (const struct ctk_item *) item_userptr(c_b));
}

CTKPRIVATE unsigned int
ctk_menu_c_item_list_size(const ctk_menu_t *menu)
{
  unsigned int i = 0;

  if (menu->c_item_list != NULL)
    while (menu->c_item_list[i] != NULL)
      ++i;

  return i;
}

/* TODO: Return bool */
void
ctk_menu_clear(ctk_widget_t *widget)
{
  ctk_menu_t *menu;
  unsigned int i;

  CTK_WIDGET_ASSERT_CLASS(widget, CTK_WIDGET_CLASS_MENU);

  menu = CTK_WIDGET_AS_MENU(widget);

  if (menu->old_item_count > 0) {
    for (i = 0; i < menu->old_item_count; ++i)
      if (menu->old_item_list[i] != NULL)
        ctk_item_destroy(menu->old_item_list[i]);

    if (menu->old_item_list != NULL)
      free(menu->old_item_list);
  }

  /*
   * You may ask at this point why I'm not freeing the menu item list
   * immediately, and leave it in old_item_list until the next list has
   * to be cleared. The answer is, as it couldn't be otherwise, NCurses.
   *
   * It turns out that set_menu_items will try to access to elements of
   * the older list for whatever reason. Risky, but correct: it would only
   * mean that I must free the older list after calling set_menu_items, and
   * not the other way around.
   *
   * *However*, as the whole NCurses API design seems to be oriented towards
   * absolute slopiness, this is not as easy as it seems: the ITEM type doesn't
   * hold copies of the strings it was created with, but just pointers to
   * them. And yes, as you can guess, set_menu_items does something with
   * those pointers I *explicitly* told set_menu_items not to use anymore,
   * crashing the application when it feels like it, failing sometimes, etc.
   *
   * Therefore, I need to keep a temporary reference of the old item list
   * until the set_menu_items operation is performed. It's been a week
   * of work now, and I spent half of it debugging segfaults and surprising
   * behaviors like this. If you came here looking for examples on
   * programming with NCurses, I strongly recommend you to give up on it.
   * Writing your own text mode UI from scratch will probably be a better idea.
   */

  menu->old_item_list  = menu->item_list;
  menu->old_item_count = menu->item_count;

  menu->item_list = NULL;
  menu->item_count = 0;
}

CTKBOOL
ctk_menu_sort(
    ctk_widget_t *widget,
    int (*cmp)(const struct ctk_item *, const struct ctk_item *))
{
  ctk_menu_t *menu;
  unsigned int count;

  CTK_WIDGET_ASSERT_CLASS(widget, CTK_WIDGET_CLASS_MENU);

  menu = CTK_WIDGET_AS_MENU(widget);

  count = ctk_menu_c_item_list_size(menu);

  /* Trivial cases are sorted by definition */
  if (menu->item_count < 2)
    return CTK_TRUE;

  (void) unpost_menu(menu->c_menu);

  qsort_r(
      menu->c_item_list,
      count,
      sizeof (ITEM *),
      __ctk_menu_cmp,
      cmp);

  set_menu_items(menu->c_menu, menu->c_item_list);

  /*
   * Since the ctk_item list is a write-only object and the ITEM list is
   * recreated every time the ctk_item list is modified, we will sort
   * the ITEM list directly. This way we can save a reallocation.
   */
  if (post_menu(menu->c_menu) != E_OK)
    return CTK_FALSE;

  ctk_widget_refresh(widget);

  return CTK_TRUE;
}

CTKBOOL
__ctk_menu_update(ctk_widget_t *widget)
{
  ctk_menu_t *menu;
  ITEM **old_item_list;

  CTK_WIDGET_ASSERT_CLASS(widget, CTK_WIDGET_CLASS_MENU);

  menu = CTK_WIDGET_AS_MENU(widget);

  /* Ensure menu is unposted from here */
  (void) unpost_menu(menu->c_menu);

  /* Modifications on the menu must be performed after unposting it */
  if (!ctk_menu_update_c_item_list(menu, &old_item_list))
    return CTK_FALSE;

  /* Rescale widget according to new elements */
  if (menu->autoresize && !ctk_widget_menu_c_rescale(widget))
    return CTK_FALSE;

  if (!menu->autoresize)
    set_menu_format(menu->c_menu, widget->height - 2, 1);

  if (ctk_menu_c_menu_should_be_posted(menu))
    if (post_menu(menu->c_menu) != E_OK)
      return CTK_FALSE;

  /* Safe to clear elements now */
  if (old_item_list != NULL)
    ITEMpp_destroy(old_item_list);

  ctk_widget_redraw(widget);

  return CTK_TRUE;
}

CTKBOOL
ctk_menu_add_item(
    ctk_widget_t *widget,
    const char *name,
    const char *desc,
    void *private)
{
  CTK_WIDGET_ASSERT_CLASS(widget, CTK_WIDGET_CLASS_MENU);

  if (!__ctk_menu_add_item(widget, name, desc, private))
    return CTK_FALSE;

  if (!__ctk_menu_update(widget))
    return CTK_FALSE;

  return CTK_TRUE;
}

CTKBOOL
ctk_menu_add_multiple_items(
    ctk_widget_t *widget,
    const struct ctk_item *items,
    unsigned int count)
{
  CTK_WIDGET_ASSERT_CLASS(widget, CTK_WIDGET_CLASS_MENU);

  if (!__ctk_menu_add_multiple_items(widget, items, count))
    return CTK_FALSE;

  if (!__ctk_menu_update(widget))
    return CTK_FALSE;

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
  ctk_menu_t *menu;
  menu = CTK_WIDGET_AS_MENU(widget);

  if (wresize(menu->c_sub, height - 2, width - 2) == ERR)
    return CTK_FALSE;

  return CTK_TRUE;
}

void
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

      case KEY_PPAGE:
        menu_driver(menu->c_menu, REQ_SCR_UPAGE);
        break;

      case KEY_NPAGE:
        menu_driver(menu->c_menu, REQ_SCR_DPAGE);
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

  if (menu->c_menu != NULL) {
    (void) unpost_menu(menu->c_menu);
    free_menu(menu->c_menu);
  }

  for (i = 0; i < menu->item_count; ++i)
    if (menu->item_list[i] != NULL)
      ctk_item_destroy(menu->item_list[i]);
  if (menu->item_list != NULL)
    free(menu->item_list);

  for (i = 0; i < menu->old_item_count; ++i)
    if (menu->old_item_list[i] != NULL)
      ctk_item_destroy(menu->old_item_list[i]);
  if (menu->old_item_list != NULL)
    free(menu->old_item_list);

  if (menu->c_sub != NULL)
    delwin(menu->c_sub);

  /* Menu has to be deleted first! */
  if (menu->c_item_list != NULL)
    ITEMpp_destroy(menu->c_item_list);
}

CTKPRIVATE void
ctk_menu_on_focus(ctk_widget_t *widget)
{
  ctk_menu_t *menu;
  menu = CTK_WIDGET_AS_MENU(widget);

  menu->has_focus = CTK_TRUE;

  set_menu_back(menu->c_menu, widget->attrs);
  set_menu_fore(
      menu->c_menu,
      widget->root == NULL
        ? widget->attrs ^ A_REVERSE
        : COLOR_PAIR(CTK_CP_MENU_SELECT));

  if (current_item(menu->c_menu) == NULL)
    menu_driver(menu->c_menu, REQ_UP_ITEM);
}

CTKPRIVATE void
ctk_menu_on_blur(ctk_widget_t *widget)
{
  ctk_menu_t *menu;
  menu = CTK_WIDGET_AS_MENU(widget);

  menu->has_focus = CTK_FALSE;

  set_menu_back(menu->c_menu, widget->attrs);
  set_menu_fore(menu->c_menu, widget->attrs ^ A_REVERSE);
}

CTKPRIVATE void
ctk_menu_on_redraw(ctk_widget_t *widget)
{
  ctk_menu_t *menu;

  menu = CTK_WIDGET_AS_MENU(widget);

  if (unpost_menu(menu->c_menu) == E_OK)
    if (ctk_menu_c_menu_should_be_posted(menu))
      post_menu(menu->c_menu);
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

struct ctk_item *
ctk_menu_get_item_at(const ctk_widget_t *widget, unsigned int index)
{
  ctk_menu_t *menu;

  CTK_WIDGET_ASSERT_CLASS(widget, CTK_WIDGET_CLASS_MENU);

  menu = CTK_WIDGET_AS_MENU(widget);

  if (index >= menu->item_count)
    return NULL;

  return menu->item_list[index];
}

struct ctk_item *
ctk_menu_get_current_item(const ctk_widget_t *widget)
{
  unsigned int i;
  ctk_menu_t *menu;
  ITEM *item;

  CTK_WIDGET_ASSERT_CLASS(widget, CTK_WIDGET_CLASS_MENU);

  menu = CTK_WIDGET_AS_MENU(widget);

  if ((item = current_item(menu->c_menu)) != NULL)
    return (struct ctk_item *) item_userptr(item);

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

void
ctk_menu_set_autoresize(const ctk_widget_t *widget, CTKBOOL val)
{
  ctk_menu_t *menu;

  CTK_WIDGET_ASSERT_CLASS(widget, CTK_WIDGET_CLASS_MENU);

  menu = CTK_WIDGET_AS_MENU(widget);

  menu->autoresize = val;
}

ctk_widget_t *
ctk_menu_new(ctk_widget_t *root, unsigned int x, unsigned int y)
{
  ctk_widget_t *widget = NULL;
  ctk_menu_t *new;
  struct ctk_widget_handlers handlers;

  if ((widget = ctk_widget_ctor_start(
      root,
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
  if (root != NULL)
    ctk_widget_set_attrs(widget, root->attrs);

  new = CTK_WIDGET_AS_MENU(widget);
  new->autoresize = CTK_TRUE;

  /* Install handlers */
  ctk_widget_get_handlers(widget, &handlers);

  handlers.kbd_handler = ctk_menu_on_kbd;
  handlers.resize_handler = ctk_menu_on_resize;
  handlers.dtor_handler = ctk_menu_on_destroy;
  handlers.blur_handler = ctk_menu_on_blur;
  handlers.focus_handler = ctk_menu_on_focus;
  handlers.redraw_handler = ctk_menu_on_redraw;

  ctk_widget_set_handlers(widget, &handlers);

  /* Create curses objects */
  if (!ctk_menu_update_c_item_list(new, NULL))
    goto fail;

  if ((new->c_sub = derwin(widget->c_window, 2, 2, 1, 1)) == NULL)
    goto fail;

  if ((new->c_menu = new_menu(new->c_item_list)) == NULL)
    goto fail;

  set_menu_mark(new->c_menu, "");
  set_menu_win(new->c_menu, widget->c_window);
  set_menu_sub(new->c_menu, new->c_sub);

  set_menu_back(new->c_menu, widget->attrs);
  set_menu_fore(new->c_menu, widget->attrs ^ A_REVERSE);

  if (!ctk_widget_ctor_end(widget))
    goto fail;

  return widget;

fail:
  if (widget != NULL)
    ctk_widget_destroy(widget);

  return NULL;
}


