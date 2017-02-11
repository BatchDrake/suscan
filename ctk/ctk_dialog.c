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

CTKPRIVATE void
ctk_dialog_get_text_size(
    const char *text,
    unsigned int *width,
    unsigned int *height)
{
  unsigned int rows = 1;
  unsigned int cols = 0;
  unsigned int max_cols = 0;


  while (*text) {
    if (*text++ == '\n') {
      ++rows;
      cols = 0;
    } else {
      if (++cols > max_cols)
        max_cols = cols;
    }
  }

  *width = max_cols;
  *height = rows;
}

CTKBOOL
ctk_msgbox(enum ctk_dialog_kind kind, const char *title, const char *msg)
{
  ctk_widget_t *window = NULL;
  ctk_widget_t *button = NULL;
  unsigned int text_width, text_height;
  unsigned int i, row = 0, col = 0;
  unsigned int win_width, win_height;
  unsigned int button_width;
  int c;

  CTKBOOL ok = CTK_FALSE;

  if ((window = ctk_window_new(title)) == NULL)
    goto done;

  ctk_widget_set_shadow(window, CTK_TRUE);

  ctk_widget_set_attrs(window, COLOR_PAIR(kind + 6));

  ctk_dialog_get_text_size(msg, &text_width, &text_height);

  if (text_width < 10)
    text_width = 10;

  if (text_height < 1)
    text_height = 1;

  win_width = window->width;

  if (text_width + 4 > win_width)
    win_width = text_width + 4;
  win_height = text_height + 6;

  if (!ctk_widget_resize(window, win_width, text_height + 6))
    goto done;

  if (!ctk_widget_move(
      window,
      COLS / 2 - win_width / 2,
      LINES / 2 - win_height / 2))
    goto done;

  for (i = 0; i < strlen(msg); ++i) {
    if (msg[i] == '\n') {
      ++row;
      col = 0;
    } else {
      mvwaddch(window->c_window, row + 2, col++ + 2, msg[i]);
    }
  }


  button_width = 10;

  if ((button = ctk_button_new(
      window,
      win_width / 2 - button_width / 2,
      row + 4, "OK")) == NULL)
    goto done;

  ctk_widget_set_attrs(button, COLOR_PAIR(1));

  ctk_widget_show(button);
  ctk_widget_show(window);

  update_panels();
  doupdate();

  while ((c = getch()) != '\n') {
    ctk_widget_notify_kbd(window, c);
    update_panels();
    doupdate();
  }

  ctk_widget_hide(window);

  ok = CTK_TRUE;

done:
  if (button != NULL)
    ctk_widget_destroy(button);

  if (window != NULL)
    ctk_widget_destroy(window);

  update_panels();
  doupdate();

  return ok;
}
