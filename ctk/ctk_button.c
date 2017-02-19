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

CTKBOOL
ctk_button_set_caption(ctk_widget_t *widget, const char *caption)
{
  ctk_button_t *button;
  char *cap_dup;

  CTK_WIDGET_ASSERT_CLASS(widget, CTK_WIDGET_CLASS_BUTTON);

  button = CTK_WIDGET_AS_BUTTON(widget);

  if ((cap_dup = strdup(caption)) == NULL)
    return CTK_FALSE;

  if (button->caption != NULL)
    free(button->caption);

  button->caption = cap_dup;

  ctk_widget_redraw(widget);

  return CTK_TRUE;
}

CTKPRIVATE void
ctk_button_on_redraw(ctk_widget_t *widget)
{
  unsigned int pos, j, len;
  const char *caption;
  ctk_button_t *button;
  CTKBOOL is_accel;
  CTKBOOL has_focus;
  CTKBOOL highlighted = CTK_FALSE;
  button = CTK_WIDGET_AS_BUTTON(widget);

  caption = button->caption;
  len = strlen(caption);
  pos = widget->width / 2 - len / 2;

  has_focus = button->has_focus;

  if (has_focus)
    wattron(widget->c_window, A_BOLD);

  for (j = 0; j < len; ++j) {
    is_accel = !highlighted && tolower(caption[j]) == widget->accel;
    if (is_accel) {
      highlighted = CTK_TRUE;
      wattron(widget->c_window, A_UNDERLINE);
      if (has_focus)
        wattron(widget->c_window, COLOR_PAIR(CTK_CP_ACCEL_HIGHLIGHT));
    }

    mvwaddch(widget->c_window, 0, pos + j, caption[j]);

    if (is_accel) {
      if (has_focus)
        wattroff(widget->c_window, COLOR_PAIR(CTK_CP_ACCEL_HIGHLIGHT));
      wattroff(widget->c_window, A_UNDERLINE);
    }
  }

  if (has_focus)
    wattroff(widget->c_window, A_BOLD);
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

  if (width < CTK_BUTTON_MIN_SIZE)
    width = CTK_BUTTON_MIN_SIZE;

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
