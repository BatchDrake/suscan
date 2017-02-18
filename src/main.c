/*
 * main.c: entry point for suscan
 * Creation date: Fri Feb  3 19:41:40 2017
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <getopt.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/select.h>

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
  struct suscan_mq mq; /* Message queue */

  /* Menu bar widgets */
  ctk_widget_t *menubar;
  ctk_widget_t *m_source;
};

SUPRIVATE pthread_t kbd_thread;
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
  struct suscan_source_config *config = NULL;
  enum ctk_dialog_response resp;

  switch (CTK_ITEM_INDEX(item)) {
    case 0:
      if ((resp = suscan_open_source_dialog(&config))
          == CTK_DIALOG_RESPONSE_ERROR)
        ctk_error("SUScan", "Failed to open source dialog");
      else if (resp == CTK_DIALOG_RESPONSE_OK)
        suscan_source_config_destroy(config);
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

/*
 * Keyboard thread: stupid and cumbersome kludge used to tell the main thread
 * that it's time to attempt to read a character from the keyboard. Again,
 * this is ncurses fault: you cannot have console input in one thread and
 * console output on other thread. This design flaw is difficult to explain,
 * but it's probably related to the fact that the people that designed
 * ncurses lived in an age where multi-threaded text UI applications were
 * rare. Again, ncurses is making me waste time writing senseless hacks
 * to overcome all these shortcomings.
 */

void *
suscan_keyboard_thread(void *unused)
{
  fd_set stdin_set;

  while (!exit_flag) {
    FD_ZERO(&stdin_set);
    FD_SET(0, &stdin_set);

    if (select(1, &stdin_set, NULL, NULL, NULL) == -1)
      break;

    suscan_mq_write_urgent(
        &main_interface.mq,
        SUSCAN_WORKER_MESSAGE_TYPE_KEYBOARD,
        NULL);
  }

  return NULL;
}

SUBOOL
suscan_iface_init(void)
{
  SUSCAN_MANDATORY(suscan_mq_init(&main_interface.mq));
  SUSCAN_MANDATORY(suscan_init_menus());

  ctk_update();

  SUSCAN_MANDATORY(
      pthread_create(&kbd_thread, NULL, suscan_keyboard_thread, NULL) != -1);

  return SU_TRUE;
}

SUBOOL
suscan_screen_init(void)
{
  const char *app_name =
      "suscan 0.1 - (c) 2017 Gonzalo J. Carracedo <BatchDrake@gmail.com>";

  if (!ctk_init())
    return SU_FALSE;

  scrollok(stdscr, TRUE);

  wattron(stdscr, A_BOLD | COLOR_PAIR(CTK_CP_BACKGROUND_TEXT));
  mvwaddstr(stdscr, LINES - 1, COLS - strlen(app_name), app_name);

  return SU_TRUE;
}

SUBOOL
suscan_ui_loop(const char *a0)
{
  int c;
  void *ptr;
  uint32_t type;
  struct suscan_worker_status_msg *status;

  while (!exit_flag) {
    ptr = suscan_mq_read(&main_interface.mq, &type);

    switch (type) {
      case SUSCAN_WORKER_MESSAGE_TYPE_KEYBOARD:
        c = ctk_getch_async();

        if (!suscan_interface_notify_kbd(c)) {
          fprintf(stderr, "%s: failed to send key to interface\n", a0);
          return SU_FALSE;
        }
        break;

      case SUSCAN_WORKER_MESSAGE_TYPE_SOURCE_INIT:
        status = (struct suscan_worker_status_msg *) ptr;

        if (status->code != SUSCAN_WORKER_INIT_OK)
          ctk_error(
              "SUScan",
              "%s",
              status->err_msg == NULL
              ? "Source couldn't be initialized"
              : status->err_msg);
        break;
    }

    suscan_worker_dispose_message(type, ptr);

    ctk_update();
  }
}

int
main(int argc, char *argv[], char *envp[])
{

  int exit_code = EXIT_FAILURE;

  char text[50];
  int n = 0;

  if (!suscan_init_sources()) {
    fprintf(stderr, "%s: failed to initialize sources\n", argv[0]);
    goto done;
  }

  if (!suscan_screen_init()) {
    fprintf(stderr, "%s: failed to initialize screen\n", argv[0]);
    goto done;
  }

  if (!suscan_iface_init()) {
    fprintf(stderr, "%s: failed to initialize interface\n", argv[0]);
    goto done;
  }

  if (suscan_ui_loop(argv[0]))
    exit_code = EXIT_SUCCESS;

  pthread_cancel(kbd_thread);
  pthread_join(kbd_thread, NULL);

  endwin();

done:
  exit(exit_code);
}

