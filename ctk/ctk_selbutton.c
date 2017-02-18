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

/* This is actually a composite object */
struct ctk_selbutton_data {
  CTKBOOL unrolled;
  ctk_widget_t *button;
  ctk_widget_t *menu;
  struct ctk_item *current;
  ctk_dtor_handler_t button_dtor;

  void *private;
  ctk_submit_handler_t submit_handler;
};

struct ctk_selbutton_data *
ctk_selbutton_data_new(ctk_widget_t *button, ctk_widget_t *menu)
{
  struct ctk_selbutton_data *new;

  if ((new = calloc(1, sizeof (struct ctk_selbutton_data))) == NULL)
    return NULL;

  new->button = button;
  new->menu = menu;

  return new;
}

void
ctk_selbutton_data_destroy(struct ctk_selbutton_data *data)
{
  free(data);
}

CTKPRIVATE void
ctk_selbutton_on_kbd(ctk_widget_t *widget, int c)
{
  struct ctk_selbutton_data *data =
      (struct ctk_selbutton_data *) ctk_widget_get_private(widget);

  switch (c) {
    case '\n':
    case KEY_ENTER:
      /* Toggle menu */
      if (!data->unrolled) {
        data->unrolled = CTK_TRUE;
        ctk_widget_show(data->menu);
      } else {
        data->unrolled = CTK_FALSE;
        ctk_widget_notify_kbd(data->menu, c);
        ctk_widget_hide(data->menu);
      }
      break;

    case KEY_UP:
    case KEY_DOWN:
      if (data->unrolled)
        ctk_widget_notify_kbd(data->menu, c);
      break;
  }
}

CTKPRIVATE void
ctk_selbutton_on_destroy(ctk_widget_t *widget)
{
  ctk_dtor_handler_t dtor;
  struct ctk_selbutton_data *data =
      (struct ctk_selbutton_data *) ctk_widget_get_private(widget);

  dtor = data->button_dtor;

  ctk_selbutton_data_destroy(data);

  /* Forward event to button destructor */
  (dtor) (widget);
}

void
ctk_selbutton_set_on_submit(ctk_widget_t *widget, ctk_submit_handler_t cb)
{
  struct ctk_selbutton_data *data =
      (struct ctk_selbutton_data *) ctk_widget_get_private(widget);

  data->submit_handler = cb;
}

/* Menu submit handlers changes button caption according to current sel */
CTKPRIVATE void
ctk_selbutton_menu_on_submit(ctk_widget_t *widget, struct ctk_item *item)
{
  struct ctk_selbutton_data *data =
      (struct ctk_selbutton_data *) ctk_widget_get_private(widget);

  data->current = item;
  data->unrolled = CTK_FALSE;
  ctk_button_set_caption(data->button, item->name);

  if (data->submit_handler != NULL)
    (data->submit_handler) (widget, item);
}

void
ctk_selbutton_set_current_item(ctk_widget_t *widget, struct ctk_item *item)
{
  ctk_selbutton_menu_on_submit(widget, item);
}

struct ctk_item *
ctk_selbutton_get_current_item(ctk_widget_t *widget)
{
  struct ctk_selbutton_data *data =
      (struct ctk_selbutton_data *) ctk_widget_get_private(widget);

  return ctk_menu_get_current_item(data->menu);
}

void
ctk_selbutton_set_private(ctk_widget_t *widget, void *private)
{
  struct ctk_selbutton_data *data =
      (struct ctk_selbutton_data *) ctk_widget_get_private(widget);

  data->private = private;
}

void *
ctk_selbutton_get_private(const ctk_widget_t *widget)
{
  struct ctk_selbutton_data *data =
      (struct ctk_selbutton_data *) ctk_widget_get_private(widget);

  return data->private;
}

ctk_widget_t *
ctk_selbutton_new(
    ctk_widget_t *root,
    unsigned int x,
    unsigned int y,
    ctk_widget_t *menu)
{
  ctk_widget_t *button = NULL;
  struct ctk_item *first = NULL;
  struct ctk_selbutton_data *data = NULL;
  struct ctk_widget_handlers hnd;

  CTK_WIDGET_ASSERT_CLASS(menu, CTK_WIDGET_CLASS_MENU);

  /* Menu must have an element */
  if ((first = ctk_menu_get_first_item(menu)) == 0)
    goto fail;

  if ((button = ctk_button_new(root, x, y, first->name)) == NULL)
    goto fail;

  /* Store selbutton private data in the widget's private pointer */
  if ((data = ctk_selbutton_data_new(button, menu)) == NULL)
    goto fail;

  /* Both widgets have access to their private data */
  ctk_widget_set_private(button, data);
  ctk_widget_set_private(menu, data);

  data->current = first;

  ctk_widget_get_handlers(button, &hnd);

  /* Save button destructor */
  data->button_dtor = hnd.dtor_handler;
  hnd.dtor_handler = ctk_selbutton_on_destroy;

  /* Overwrite keyboard handler */
  hnd.kbd_handler = ctk_selbutton_on_kbd;

  if (!ctk_widget_resize(button, ctk_menu_get_max_item_name_length(menu), 1))
    goto fail;

  if (root == NULL) {
    if (!ctk_widget_move(menu, x, y))
      if (!ctk_widget_center(menu))
        goto fail;
  } else {
    if (!ctk_widget_move(menu, root->x + x, root->y + y))
      if (!ctk_widget_center(menu))
        goto fail;
  }

  if (!ctk_widget_set_shadow(menu, CTK_TRUE))
    goto fail;

  ctk_widget_hide(menu);

  /* Initialization done, set remaining handlers and return */
  ctk_widget_set_handlers(button, &hnd);

  ctk_widget_get_handlers(menu, &hnd);
  hnd.submit_handler = ctk_selbutton_menu_on_submit;
  ctk_widget_set_handlers(menu, &hnd);

  return button;

fail:

  if (button != NULL)
    ctk_widget_destroy(button);

  if (data != NULL)
    ctk_selbutton_data_destroy(data);

  return NULL;
}
