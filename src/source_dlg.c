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

#include <ctk.h>
#include "suscan.h"

SUPRIVATE SUBOOL exit_flag = SU_FALSE;

SUPRIVATE struct ctk_item source_item_list[] = {
    {"BladeRF",    "Nuand's BladeRF", SUSCAN_SOURCE_TYPE_BLADE_RF},
    {"HackRF",     "Great Scott Gadget's HackRF", SUSCAN_SOURCE_TYPE_HACK_RF},
    {"I/Q file",   "Previously recorded I/Q samples", SUSCAN_SOURCE_TYPE_IQ_FILE},
    {"WAV file",   "PCM / WAVE / AIFF audio file", SUSCAN_SOURCE_TYPE_WAV_FILE},
    {"ALSA input", "Read samples from soundcard", SUSCAN_SOURCE_TYPE_ALSA}
};

SUPRIVATE void
suscan_dialog_on_submit(ctk_widget_t *widget, struct ctk_item *item)
{
  exit_flag = SU_TRUE;
}

SUPRIVATE void
suscan_dialog_file_on_submit(ctk_widget_t *widget, struct ctk_item *item)
{
  char *result = NULL;

  if (ctk_file_dialog("Open file...", &result) == CTK_DIALOG_RESPONSE_ERROR)
    ctk_msgbox(CTK_DIALOG_ERROR, "SUScan", "Failed to open dialog");
}

SUBOOL
suscan_open_source_dialog(void)
{
  ctk_widget_t *window = NULL;
  ctk_widget_t *button = NULL;
  ctk_widget_t *selbutton = NULL;
  ctk_widget_t *menu = NULL;
  ctk_widget_t *file_button = NULL;

  struct ctk_widget_handlers hnd;
  unsigned int button_width;
  int c;

  SUBOOL ok = SU_FALSE;

  exit_flag = SU_FALSE;

  /* Create Dialog Window */
  if ((window = ctk_window_new("Open source")) == NULL)
    goto done;

  ctk_widget_resize(window, 30, 15);
  ctk_widget_center(window);
  ctk_widget_set_shadow(window, CTK_TRUE);

  /* Create source menu list */
  if ((menu = ctk_menu_new(NULL, 0, 0)) == NULL)
    goto done;

  if (!ctk_menu_add_multiple_items(
      menu,
      source_item_list,
      ARRAY_SZ(source_item_list)))
    goto done;

  /* Create source selection button */
  mvwaddstr(window->c_window, 2, 2, "Source type:");
  if ((selbutton = ctk_selbutton_new(window, 15, 2, menu)) == NULL)
    goto done;
  ctk_widget_set_attrs(selbutton, COLOR_PAIR(CTK_CP_TEXTAREA));

  /* Create file selection button */
  mvwaddstr(window->c_window, 4, 2, "File:");
  if ((file_button = ctk_button_new(window, 15, 4, "Browse...")) == NULL)
    goto done;
  ctk_widget_set_attrs(file_button, COLOR_PAIR(CTK_CP_TEXTAREA));

  /* Okay button */
  button_width = 10;

  if ((button = ctk_button_new(
      window,
      window->width / 2 - button_width / 2,
      13, "OK")) == NULL)
    goto done;
  ctk_widget_set_attrs(button, COLOR_PAIR(CTK_CP_TEXTAREA));

  ctk_widget_get_handlers(button, &hnd);
  hnd.submit_handler = suscan_dialog_on_submit;
  ctk_widget_set_handlers(button, &hnd);

  ctk_widget_get_handlers(file_button, &hnd);
  hnd.submit_handler = suscan_dialog_file_on_submit;
  ctk_widget_set_handlers(file_button, &hnd);

  ctk_widget_show(file_button);
  ctk_widget_show(selbutton);
  ctk_widget_show(button);
  ctk_widget_show(window);

  ctk_window_focus_next(window);

  ctk_update();

  while (!exit_flag) {
    c = getch();
    if (c == 'q')
      break;

    ctk_widget_notify_kbd(window, c);
    ctk_update();
  }

  ctk_widget_hide(window);

  ok = SU_TRUE;

done:
  if (file_button != NULL)
    ctk_widget_destroy(file_button);

  if (button != NULL)
    ctk_widget_destroy(button);

  if (menu != NULL)
    ctk_widget_destroy(menu);

  if (selbutton != NULL)
    ctk_widget_destroy(selbutton);

  if (window != NULL)
    ctk_widget_destroy(window);

  ctk_update();

  return ok;
}
