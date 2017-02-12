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
#include <stdint.h>

#include "ctk.h"

CTKPRIVATE void
ctk_entry_on_redraw(ctk_widget_t *widget)
{
  unsigned int pos, i, len;
  unsigned int cur_pos;
  const char *text;
  ctk_entry_t *entry;

  entry = CTK_WIDGET_AS_ENTRY(widget);

  if ((text = entry->buffer) != NULL) {
    cur_pos = entry->p - entry->pos;

    for (i = 0; i < widget->width; ++i) {
      if (entry->has_focus && i == cur_pos)
        wattron(widget->c_window, entry->cur_attr);

      if (i + entry->pos < entry->length)
        mvwaddch(widget->c_window, 0, i, entry->buffer[i + entry->pos]);
      else
        mvwaddch(widget->c_window, 0, i, ' ');

      if (entry->has_focus && i == cur_pos)
        wattroff(widget->c_window, entry->cur_attr);
    }
  } else if (entry->has_focus) {
    wattron(widget->c_window, entry->cur_attr);
    mvwaddch(widget->c_window, 0, 0, ' ');
    wattroff(widget->c_window, entry->cur_attr);
  }
}

CTKPRIVATE unsigned int
ctk_entry_get_allocation(unsigned int size)
{
  unsigned int i;

  for (i = 0; i < (sizeof(unsigned int) << 3); ++i)
    if ((1u << i) >= size)
      return (1u << i);

  return size;
}

CTKPRIVATE CTKBOOL
ctk_entry_buffer_set_length(ctk_widget_t *widget, unsigned int length)
{
  void *new_buffer = NULL;
  unsigned int new_alloc;
  ctk_entry_t *entry;
  unsigned int size;

  entry = CTK_WIDGET_AS_ENTRY(widget);

  size = length + 1;

  if (size > entry->allocation) {
    new_alloc = ctk_entry_get_allocation(size);
    if ((new_buffer = realloc(entry->buffer, new_alloc)) == NULL)
      return CTK_FALSE;

    if (entry->allocation == 0)
      /* Clean up new memory */
      memset(new_buffer, 0, new_alloc);

    entry->allocation = new_alloc;
    entry->buffer = new_buffer;
  }

  entry->length = length;

  return CTK_TRUE;
}

CTKBOOL
ctk_entry_set_cursor(ctk_widget_t *widget, unsigned int p)
{
  ctk_entry_t *entry;

  CTK_WIDGET_ASSERT_CLASS(widget, CTK_WIDGET_CLASS_ENTRY);

  entry = CTK_WIDGET_AS_ENTRY(widget);

  if (p > entry->length)
    return CTK_FALSE;

  entry->p = p;

  if (p < entry->pos)
    entry->pos = p;
  else if (p >= entry->pos + widget->width)
    entry->pos = p - widget->width + 1;

  return CTK_TRUE;
}

CTKPRIVATE CTKBOOL
ctk_entry_move_cursor(ctk_widget_t *widget, int delta)
{
  ctk_entry_t *entry;
  int res_p;

  entry = CTK_WIDGET_AS_ENTRY(widget);

  res_p = entry->p + delta;
  if (res_p < 0)
    res_p = 0;
  else if (res_p > entry->length)
    res_p = entry->length;

  return ctk_entry_set_cursor(widget, res_p);
}

CTKPRIVATE CTKBOOL
ctk_entry_insert(ctk_widget_t *widget, char c)
{
  ctk_entry_t *entry;
  unsigned int size;

  entry = CTK_WIDGET_AS_ENTRY(widget);

  size = entry->length + 1; /* Semantically different than length + 1 */

  if (!ctk_entry_buffer_set_length(widget, entry->length + 1))
    return CTK_FALSE;

  memmove(
      entry->buffer + entry->p + 1,
      entry->buffer + entry->p,
      size - entry->p);

  entry->buffer[entry->p] = c;

  ctk_entry_move_cursor(widget, 1);

  return CTK_TRUE;
}

CTKPRIVATE void
ctk_entry_erase(ctk_widget_t *widget, CTKBOOL previous)
{
  ctk_entry_t *entry;
  unsigned int size;

  entry = CTK_WIDGET_AS_ENTRY(widget);

  /* Boundary check */
  if ((previous && entry->p == 0) || (!previous && entry->p == entry->length))
    return;

  size = entry->length + 1;

  if (previous)
    ctk_entry_move_cursor(widget, -1);

  memmove(
      entry->buffer + entry->p,
      entry->buffer + entry->p + 1,
      size - entry->p - 1);

  ctk_entry_buffer_set_length(widget, entry->length - 1);
}

CTKPRIVATE void
ctk_entry_on_kbd(ctk_widget_t *widget, int c)
{
  ctk_entry_t *entry;

  entry = CTK_WIDGET_AS_ENTRY(widget);

  if (c == '\n' || c == widget->accel)
    ctk_widget_submit(widget, NULL);
  else if (c == KEY_BACKSPACE)
    ctk_entry_erase(widget, CTK_TRUE);
  else if (c == KEY_DC)
    ctk_entry_erase(widget, CTK_FALSE);
  else if (c == KEY_LEFT || c == KEY_SLEFT)
    ctk_entry_move_cursor(widget, -1);
  else if (c == KEY_RIGHT || c == KEY_SRIGHT)
    ctk_entry_move_cursor(widget, 1);
  else if (c == KEY_HOME)
    ctk_entry_set_cursor(widget, 0);
  else if (c == KEY_END)
    ctk_entry_set_cursor(widget, entry->length);
  else if (c != '\t' && c != '\r')
    ctk_entry_insert(widget, c);

  ctk_widget_redraw(widget);
}

CTKPRIVATE void
ctk_entry_on_destroy(ctk_widget_t *widget)
{
  ctk_entry_t *entry;

  entry = CTK_WIDGET_AS_ENTRY(widget);

  if (entry->buffer != NULL)
    free(entry->buffer);
}

CTKPRIVATE void
ctk_entry_on_focus(ctk_widget_t *widget)
{
  ctk_entry_t *entry;

  entry = CTK_WIDGET_AS_ENTRY(widget);

  entry->has_focus = CTK_TRUE;

  ctk_widget_redraw(widget);
}

CTKPRIVATE void
ctk_entry_on_blur(ctk_widget_t *widget)
{
  ctk_entry_t *entry;

  entry = CTK_WIDGET_AS_ENTRY(widget);

  entry->has_focus = CTK_FALSE;

  ctk_widget_redraw(widget);
}

const char *
ctk_entry_get_text(const ctk_widget_t *widget)
{
  ctk_entry_t *entry;

  CTK_WIDGET_ASSERT_CLASS(widget, CTK_WIDGET_CLASS_ENTRY);

  entry = CTK_WIDGET_AS_ENTRY(widget);

  return entry->buffer == NULL ? "" : entry->buffer;
}

CTKBOOL
ctk_entry_set_text(ctk_widget_t *widget, const char *text)
{
  ctk_entry_t *entry;

  CTK_WIDGET_ASSERT_CLASS(widget, CTK_WIDGET_CLASS_ENTRY);

  entry = CTK_WIDGET_AS_ENTRY(widget);

  if (!ctk_entry_buffer_set_length(widget, strlen(text)))
    return CTK_FALSE;

  memcpy(entry->buffer, text, strlen(text));

  ctk_entry_move_cursor(widget, strlen(text));

  ctk_widget_redraw(widget);

  return CTK_TRUE;
}

ctk_widget_t *
ctk_entry_new(
    ctk_widget_t *root,
    unsigned int x,
    unsigned int y,
    unsigned int width)
{
  ctk_widget_t *widget = NULL;
  ctk_entry_t *entry;
  struct ctk_widget_handlers handlers;

  if (width < 2)
    width = 2;

  if ((widget = ctk_widget_ctor_start(
      root,
      x,
      y,
      width,
      1,
      CTK_WIDGET_SUB_ALLOC_SIZE(ctk_entry_t))) == NULL)
    goto fail;

  widget->class = CTK_WIDGET_CLASS_ENTRY;

  entry = CTK_WIDGET_AS_ENTRY(widget);

  /* A_UNDERLINE is nice too, but it doesn't work with graphic consoles */
  entry->cur_attr = COLOR_PAIR(CTK_CP_MENU_SELECT) | A_BOLD;

  ctk_widget_set_attrs(widget, COLOR_PAIR(CTK_CP_TEXTAREA));

  /* Install handlers */
  ctk_widget_get_handlers(widget, &handlers);

  handlers.kbd_handler = ctk_entry_on_kbd;
  handlers.dtor_handler = ctk_entry_on_destroy;
  handlers.redraw_handler = ctk_entry_on_redraw;
  handlers.focus_handler = ctk_entry_on_focus;
  handlers.blur_handler = ctk_entry_on_blur;

  ctk_widget_set_handlers(widget, &handlers);

  if (!ctk_widget_ctor_end(widget))
    goto fail;

  return widget;

fail:

  if (widget != NULL)
    ctk_widget_destroy(widget);

  return NULL;
}
