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

const char *
ctk_button_get_caption(ctk_widget_t *widget)
{
  ctk_button_t *button;

  CTK_WIDGET_ASSERT_CLASS(widget, CTK_WIDGET_CLASS_BUTTON);

  button = CTK_WIDGET_AS_BUTTON(widget);

  return button->caption;
}

CTKPRIVATE void
ctk_button_on_redraw(ctk_widget_t *widget)
{
  unsigned int pos, j, len;
  const char *caption;
  ctk_button_t *button;

  button = CTK_WIDGET_AS_BUTTON(widget);

  caption = button->caption;
  len = strlen(caption);
  pos = widget->width / 2 - len / 2;

  mvwaddstr(widget->c_window, 0, pos, caption);

  if (widget->accel != 0)
    for (j = 0; j < len; ++j)
      if (tolower(caption[j]) == widget->accel) {
        wattron(widget->c_window, A_UNDERLINE);
        if (button->has_focus)
          wattron(widget->c_window, A_BOLD | COLOR_PAIR(4));
        mvwaddch(widget->c_window, 0, pos + j, caption[j]);
        if (button->has_focus)
          wattroff(widget->c_window, A_BOLD | COLOR_PAIR(4));
        wattroff(widget->c_window, A_UNDERLINE);
        break;
      }
}

CTKPRIVATE void
ctk_button_on_kbd(ctk_widget_t *widget, int c)
{
  ctk_button_t *button;

  button = CTK_WIDGET_AS_BUTTON(widget);

  if (c == '\n' || c == widget->accel)
    ctk_widget_submit(widget, NULL);
}

CTKPRIVATE void
ctk_button_on_destroy(ctk_widget_t *widget)
{
  ctk_button_t *button;

  button = CTK_WIDGET_AS_BUTTON(widget);

  if (button->caption != NULL)
    free(button->caption);
}

CTKPRIVATE void
ctk_button_on_focus(ctk_widget_t *widget)
{
  ctk_button_t *button;

  button = CTK_WIDGET_AS_BUTTON(widget);

  button->has_focus = CTK_TRUE;

  ctk_widget_redraw(widget);
}

CTKPRIVATE void
ctk_button_on_blur(ctk_widget_t *widget)
{
  ctk_button_t *button;

  button = CTK_WIDGET_AS_BUTTON(widget);

  button->has_focus = CTK_FALSE;

  ctk_widget_redraw(widget);
}

ctk_widget_t *
ctk_button_new(
    ctk_widget_t *root,
    unsigned int x,
    unsigned int y,
    const char *caption)
{
  ctk_widget_t *widget = NULL;
  ctk_button_t *button;
  struct ctk_widget_handlers handlers;

  int width;

  width = strlen(caption) + 2;

  if (width < 10)
    width = 10;

  if ((widget = ctk_widget_ctor_start(
      root,
      x,
      y,
      width,
      1,
      CTK_WIDGET_SUB_ALLOC_SIZE(ctk_button_t))) == NULL) {
    fprintf(stderr, "%s: failed to create base widget\n", __FUNCTION__);
    goto fail;
  }

  widget->class = CTK_WIDGET_CLASS_BUTTON;

  button = CTK_WIDGET_AS_BUTTON(widget);

  if ((button->caption = strdup(caption)) == NULL) {
    fprintf(stderr, "%s: failed to duplicate caption\n", __FUNCTION__);
    goto fail;
  }

  /* Install handlers */
  ctk_widget_get_handlers(widget, &handlers);

  handlers.kbd_handler = ctk_button_on_kbd;
  handlers.dtor_handler = ctk_button_on_destroy;
  handlers.redraw_handler = ctk_button_on_redraw;
  handlers.focus_handler = ctk_button_on_focus;
  handlers.blur_handler = ctk_button_on_blur;

  ctk_widget_set_handlers(widget, &handlers);

  if (!ctk_widget_ctor_end(widget)) {
    fprintf(stderr, "%s: failed to complete ctor\n", __FUNCTION__);
    goto fail;
  }

  return widget;

fail:

  if (widget != NULL)
    ctk_widget_destroy(widget);

  return NULL;
}
