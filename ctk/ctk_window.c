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
ctk_window_find_new_accel(const ctk_window_t *bar, const char *caption)
{
  unsigned int i;

  for (i = 0; i < strlen(caption); ++i)
    if (ctk_widget_lookup_index_by_accel(
        bar->widget_list,
        bar->widget_count,
        caption[i]) == -1)
      return caption[i];

  return 0;
}

CTKPRIVATE void
ctk_window_redraw_children(ctk_widget_t *widget)
{
  int i;
  ctk_window_t *window;

  window = CTK_WIDGET_AS_WINDOW(widget);

  for (i = 0; i < window->widget_count; ++i)
    if (window->widget_list[i] != NULL)
      ctk_widget_redraw(window->widget_list[i]);
}

CTKPRIVATE void
ctk_window_redraw_shadow(ctk_widget_t *widget)
{
  ctk_window_t *window;


}

CTKPRIVATE void
ctk_window_on_redraw(ctk_widget_t *widget)
{
  unsigned int pos;
  ctk_window_t *window;

  window = CTK_WIDGET_AS_WINDOW(widget);

  pos = widget->width / 2 - ((4 + strlen(window->title)) / 2);

  mvwaddch(widget->c_window, 0, pos, ACS_RTEE);
  wattron(widget->c_window, A_REVERSE);
  mvwprintw(widget->c_window, 0, pos + 1, " %s ", window->title);
  wattroff(widget->c_window, A_REVERSE);
  mvwaddch(widget->c_window, 0, pos + 3 + strlen(window->title), ACS_LTEE);

  ctk_window_redraw_children(widget);
}

CTKPRIVATE CTKBOOL
ctk_window_set_focus(ctk_window_t *window, int next)
{
  if (next < -1 || next >= window->widget_count)
    return CTK_FALSE;

  /* When a widget is removed, we cannot trigger any handler in it */
  if (window->focus != -1 && window->widget_list[window->focus] != NULL)
    ctk_widget_blur(window->widget_list[window->focus]);

  window->focus = next;

  if (window->focus != -1)
    ctk_widget_focus(window->widget_list[window->focus]);

  return CTK_TRUE;
}

CTKPRIVATE void
ctk_window_focus_next(ctk_widget_t *widget)
{
  ctk_window_t *window;
  int next_widget;

  window = CTK_WIDGET_AS_WINDOW(widget);

  next_widget = window->focus;

  while (++next_widget < window->widget_count
      && window->widget_list[next_widget] == NULL);

  if (next_widget >= window->widget_count) {
    /* Nothing found, repeat from start */
    next_widget = -1;
    while (++next_widget < window->widget_count
        && window->widget_list[next_widget] == NULL);

    /* Nothing found, no widgets */
    if (next_widget >= window->widget_count)
      next_widget = -1;
  }

  /* Focus next widget */
  ctk_window_set_focus(window, next_widget);
}

CTKPRIVATE void
ctk_window_on_kbd(ctk_widget_t *widget, int c)
{
  ctk_window_t *window;

  window = CTK_WIDGET_AS_WINDOW(widget);

  if (c == '\t')
    /* Tabulator pressed: cycle around widgets */
    ctk_window_focus_next(widget);
  else if (c == CTK_KEY_ESCAPE)
    ctk_window_set_focus(window, -1);
  else if (window->focus != -1)
    /* Forward key to focused widget */
    ctk_widget_notify_kbd(window->widget_list[window->focus], c);
}

CTKPRIVATE void
ctk_window_on_destroy(ctk_widget_t *widget)
{
  ctk_window_t *window;

  window = CTK_WIDGET_AS_WINDOW(widget);

  if (window->title != NULL)
    free(window->title);

  /* We don't own widgets, we just loan them */
  if (window->widget_list != NULL)
    free(window->widget_list);
}

CTKPRIVATE int
ctk_window_lookup_child(ctk_window_t *window, ctk_widget_t *child)
{
  int i;

  for (i = 0; i < window->widget_count; ++i)
    if (window->widget_list[i] == child)
      return i;

  return -1;
}

CTKPRIVATE CTKBOOL
ctk_window_on_attach(ctk_widget_t *widget, ctk_widget_t *child)
{
  ctk_window_t *window;
  const char *caption;
  int accel;
  window = CTK_WIDGET_AS_WINDOW(widget);

  if (ctk_window_lookup_child(window, child) != -1)
    return CTK_TRUE;

  /* Find an accelerator for each widget */
  switch (child->class) {
    case CTK_WIDGET_CLASS_BUTTON:
      caption = ctk_button_get_caption(child);
      break;

    default:
      caption = NULL;
  }

  if (caption != NULL) {
    accel = ctk_window_find_new_accel(window, caption);
    ctk_widget_set_accel(child, accel);
  }

  if (PTR_LIST_APPEND_CHECK(window->widget, child) == -1)
    return CTK_FALSE;

  ctk_window_focus_next(widget);

  return CTK_TRUE;
}

CTKPRIVATE void
ctk_window_on_detach(ctk_widget_t *widget, ctk_widget_t *child)
{
  ctk_window_t *window;
  int index;

  window = CTK_WIDGET_AS_WINDOW(widget);

  if ((index = ctk_window_lookup_child(window, child)) != -1) {
    window->widget_list[index] = NULL;
    ctk_window_focus_next(widget);
  }
}

ctk_widget_t *
ctk_window_new(const char *title)
{
  ctk_widget_t *widget = NULL;
  ctk_window_t *window;
  struct ctk_widget_handlers handlers;

  if ((widget = ctk_widget_ctor_start(
      NULL,
      0,
      0,
      strlen(title) + 16,
      5,
      CTK_WIDGET_SUB_ALLOC_SIZE(ctk_window_t))) == NULL)
    goto fail;

  widget->class = CTK_WIDGET_CLASS_WINDOW;
  widget->attrs = COLOR_PAIR(2);

  ctk_widget_set_border(widget, CTK_TRUE);

  window = CTK_WIDGET_AS_WINDOW(widget);
  window->focus = -1;

  if ((window->title = strdup(title)) == NULL)
    goto fail;

  /* Install handlers */
  ctk_widget_get_handlers(widget, &handlers);

  handlers.kbd_handler = ctk_window_on_kbd;
  handlers.dtor_handler = ctk_window_on_destroy;
  handlers.redraw_handler = ctk_window_on_redraw;
  handlers.attach_handler = ctk_window_on_attach;
  handlers.detach_handler = ctk_window_on_detach;

  ctk_widget_set_handlers(widget, &handlers);

  if (!ctk_widget_ctor_end(widget))
    goto fail;

  return widget;

fail:

  if (widget != NULL)
    ctk_widget_destroy(widget);

  return NULL;
}
