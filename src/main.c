/*
 * main.c: entry point for suscan
 * Creation date: Fri Feb  3 19:41:40 2017
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>

#include "ctk.h"
#include "suscan.h"

#define SUSCAN_MANDATORY(expr)          \
  if (!(expr)) {                        \
    fprintf(                            \
      stderr,                           \
      "%s: operation \"%s\" failed\r\n",\
      __FUNCTION__,                     \
      STRINGIFY(expr));                 \
      return SU_FALSE;                  \
  }

struct suscan_interface {
  /* Menu bar widgets */
  ctk_widget_t *menubar;
  ctk_widget_t *m_source;
};

SUBOOL exit_flag = SU_FALSE;
struct suscan_interface main_interface;

SUBOOL
suscan_interface_notify_kbd(int c)
{
  if (c == 'q') {
    exit_flag = SU_TRUE;
    return SU_TRUE;
  }

  ctk_widget_notify_kbd(main_interface.menubar, c);

  return SU_TRUE;
}

void
suscan_source_submit_handler(ctk_widget_t *widget, struct ctk_item *item)
{
  switch (CTK_ITEM_INDEX(item)) {
    case 0:
      if (!suscan_open_source_dialog())
        ctk_msgbox(CTK_DIALOG_ERROR, "SUScan", "Failed to open source dialog");
      break;

    case 1:
      exit_flag = SU_TRUE;
      break;
  }
}

SUBOOL
suscan_init_menus(void)
{
  struct ctk_widget_handlers hnd;

  SUSCAN_MANDATORY(main_interface.menubar = ctk_menubar_new());
  SUSCAN_MANDATORY(main_interface.m_source = ctk_menu_new(NULL, 0, 0));

  /* Create source menu */
  SUSCAN_MANDATORY(
      ctk_menu_add_item(
          main_interface.m_source,
          "Open...",
          "",
          NULL));
  SUSCAN_MANDATORY(
      ctk_menu_add_item(
          main_interface.m_source,
          "Quit",
          "",
          NULL));

  SUSCAN_MANDATORY(
      ctk_menubar_add_menu(
          main_interface.menubar,
          "Source",
          main_interface.m_source));

  ctk_widget_get_handlers(main_interface.m_source, &hnd);
  hnd.submit_handler = suscan_source_submit_handler;
  ctk_widget_set_handlers(main_interface.m_source, &hnd);

  /* Show menu bar */
  ctk_widget_show(main_interface.menubar);

  return SU_TRUE;
}

SUBOOL
suscan_iface_init(void)
{
  SUSCAN_MANDATORY(suscan_init_menus());

  ctk_update();

  return SU_TRUE;
}

SUBOOL
suscan_screen_init(void)
{
  const char *app_name =
      "suscan 0.1 - (c) 2017 Gonzalo J. Carracedo <BatchDrake@gmail.com>";

  if (!ctk_init())
    return SU_FALSE;

  wattron(stdscr, A_BOLD | COLOR_PAIR(CTK_CP_BACKGROUND_TEXT));
  mvwaddstr(stdscr, LINES - 1, COLS - strlen(app_name), app_name);

  return SU_TRUE;
}

int
main(int argc, char *argv[], char *envp[])
{
  int c;
  int exit_code = EXIT_FAILURE;

  char text[50];
  int n = 0;

  if (!suscan_screen_init()) {
    fprintf(stderr, "%s: failed to initialize screen\n", argv[0]);
    goto done;
  }

  scrollok(stdscr,TRUE);
  if (!suscan_iface_init()) {
    fprintf(stderr, "%s: failed to initialize interface\n", argv[0]);
    goto done;
  }

  while (!exit_flag) {
    c = wgetch(stdscr);

    if (!suscan_interface_notify_kbd(c)) {
      fprintf(stderr, "%s: failed to send command to interface\n", argv[0]);
      goto done;
    }

    ctk_update();
  }

  exit_code = EXIT_SUCCESS;

done:
  endwin();

  exit(exit_code);
}

