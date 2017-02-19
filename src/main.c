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

  ctk_widget_t *w_status;
  ctk_widget_t *w_results;
  ctk_widget_t *w_channel;
  ctk_widget_t *m_results;

  /* Signal workers */
  PTR_LIST(suscan_worker_t, worker);
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

SUBOOL
suscan_open_source(struct suscan_source_config *config)
{
  suscan_worker_t *worker;

  if ((worker = suscan_worker_new(config, &main_interface.mq)) == NULL) {
    return SU_FALSE;
  } else if (PTR_LIST_APPEND_CHECK(main_interface.worker, worker) == -1) {
    suscan_worker_destroy(worker);
    return SU_FALSE;
  }

  return SU_TRUE;
}

void
suscan_source_submit_handler(ctk_widget_t *widget, struct ctk_item *item)
{
  struct suscan_source_config *config = NULL;
  enum ctk_dialog_response resp;
  suscan_worker_t *worker = NULL;

  switch (CTK_ITEM_INDEX(item)) {
    case 0:
      if ((resp = suscan_open_source_dialog(&config))
          == CTK_DIALOG_RESPONSE_ERROR)
        ctk_error("SUScan", "Failed to open source dialog");
      else if (resp == CTK_DIALOG_RESPONSE_OK) {
        if (!suscan_open_source(config)) {
          ctk_error("SUScan", "Failed to create worker thread");
          suscan_source_config_destroy(config);
        }
      }
      break;

    case 1:
      exit_flag = SU_TRUE;
      break;
  }
}

void
suscan_redraw_results_header(void)
{
  wattron(main_interface.m_results->c_window, COLOR_PAIR(CTK_CP_BACKGROUND_TEXT));

  /* mvwaddch (main_interface.m_results->c_window, 0, 2, ACS_RTEE); */
  mvwprintw(main_interface.m_results->c_window, 0, 3,  " Channel freq");
  /* mvwaddch (main_interface.m_results->c_window, 0, 16, ACS_LTEE); */

  /* mvwaddch (main_interface.m_results->c_window, 0, 18, ACS_RTEE); */
  mvwprintw(main_interface.m_results->c_window, 0, 19, " Bandwidth");
  /* mvwaddch (main_interface.m_results->c_window, 0, 29, ACS_LTEE); */

  /* mvwaddch (main_interface.m_results->c_window, 0, 31, ACS_RTEE); */
  mvwprintw(main_interface.m_results->c_window, 0, 32, "  SNR  ");
  /* mvwaddch (main_interface.m_results->c_window, 0, 39, ACS_LTEE); */

  /* mvwaddch (main_interface.m_results->c_window, 0, 41, ACS_RTEE); */
  mvwprintw(main_interface.m_results->c_window, 0, 42, "   N0   ");
  /* mvwaddch (main_interface.m_results->c_window, 0, 50, ACS_LTEE); */

  /* mvwaddch (main_interface.m_results->c_window, 0, 55, ACS_RTEE); */
  mvwprintw(main_interface.m_results->c_window, 0, 56, "    Signal source   ");
  /* mvwaddch (main_interface.m_results->c_window, 0, 77, ACS_LTEE); */

  /* wattroff(main_interface.m_results->c_window, A_REVERSE); */
}

SUBOOL
suscan_init_windows(void)
{
  init_pair(24, COLOR_CYAN, COLOR_BLUE);
  init_pair(25, COLOR_BLACK, COLOR_BLUE);

  SUSCAN_MANDATORY(main_interface.w_status = ctk_window_new("Source status"));
  ctk_widget_set_attrs(main_interface.w_status, COLOR_PAIR(24) | A_BOLD);

  SUSCAN_MANDATORY(ctk_widget_move(main_interface.w_status, 0, 1));
  SUSCAN_MANDATORY(ctk_widget_resize(main_interface.w_status, 25, 10));

  SUSCAN_MANDATORY(main_interface.w_results = ctk_window_new("Results"));
  ctk_widget_set_attrs(main_interface.w_results, COLOR_PAIR(24) | A_BOLD);

  SUSCAN_MANDATORY(ctk_widget_move(main_interface.w_results, 0, 11));
  SUSCAN_MANDATORY(ctk_widget_resize(main_interface.w_results, COLS, LINES - 12));

  SUSCAN_MANDATORY(main_interface.w_channel = ctk_window_new("Channel"));
  ctk_widget_set_attrs(main_interface.w_channel, COLOR_PAIR(24) | A_BOLD);

  SUSCAN_MANDATORY(ctk_widget_move(main_interface.w_channel, 25, 1));
  SUSCAN_MANDATORY(ctk_widget_resize(main_interface.w_channel, COLS - 25, 10));

  SUSCAN_MANDATORY(main_interface.m_results =
      ctk_menu_new(
          main_interface.w_results,
          2,
          1));

  ctk_widget_set_shadow(main_interface.m_results, CTK_FALSE);
  ctk_menu_set_autoresize(main_interface.m_results, SU_FALSE);

  ctk_widget_resize(
      main_interface.m_results,
      main_interface.w_results->width - 4,
      main_interface.w_results->height - 2);

  ctk_widget_show(main_interface.m_results);
  ctk_widget_show(main_interface.w_status);
  ctk_widget_show(main_interface.w_channel);
  ctk_widget_show(main_interface.w_results);

  suscan_redraw_results_header();

  return SU_TRUE;
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
 * console output in other thread. This design flaw is difficult to explain,
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
  SUSCAN_MANDATORY(suscan_init_windows());

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

  wattron(stdscr, A_BOLD | COLOR_PAIR(CTK_CP_BACKGROUND_TEXT));
  mvwaddstr(stdscr, LINES - 1, COLS - strlen(app_name), app_name);

  return SU_TRUE;
}

void
suscan_clear_mq(void)
{
  uint32_t type;
  void *private;
  unsigned int n = 0;

  while (suscan_mq_poll(&main_interface.mq, &type, &private)) {
    suscan_worker_dispose_message(type, private);
    ++n;
  }

  printf("%d messages cleared\n", n);

  suscan_mq_finalize(&main_interface.mq);
}

SUPRIVATE int
suscan_compare_channels(const struct ctk_item *a, const struct ctk_item *b)
{
  const struct sigutils_channel *c_a =
      (const struct sigutils_channel *) a->private;
  const struct sigutils_channel *c_b =
      (const struct sigutils_channel *) b->private;

  return c_a->fc - c_b->fc;
}

SUBOOL
suscan_ui_loop(const char *a0)
{
  int c;
  unsigned int i;
  char *channel_line;
  void *ptr;
  uint32_t type;
  struct suscan_worker_status_msg *status;
  struct suscan_worker_channel_msg *channels;

  while (!exit_flag) {
    ptr = suscan_mq_read(&main_interface.mq, &type);

    switch (type) {
      case SUSCAN_WORKER_MESSAGE_TYPE_KEYBOARD:
        c = ctk_getch_async();

        if (c != ERR && !suscan_interface_notify_kbd(c)) {
          fprintf(stderr, "%s: failed to send key to interface\n", a0);
          return SU_FALSE;
        }
        break;

      case SUSCAN_WORKER_MESSAGE_TYPE_SOURCE_INIT:
        status = (struct suscan_worker_status_msg *) ptr;

        if (status->code != SUSCAN_WORKER_INIT_SUCCESS)
          ctk_error(
              "SUScan",
              "%s",
              status->err_msg == NULL
              ? "Source couldn't be initialized"
              : status->err_msg);
        break;

      case SUSCAN_WORKER_MESSAGE_TYPE_CHANNEL:
        channels = (struct suscan_worker_channel_msg *) ptr;

        ctk_menu_clear(main_interface.m_results);

        for (i = 0; i < channels->channel_count; ++i) {
          if ((channel_line = strbuild(
              "%+12.2lf Hz   %7.2lf Hz   %3.1lf dB  %6.1lf dB      %s",
              channels->channel_list[i]->fc,
              channels->channel_list[i]->bw,
              channels->channel_list[i]->snr,
              channels->channel_list[i]->N0,
              channels->source->desc)) != NULL) {
            ctk_menu_add_item(
                main_interface.m_results,
                channel_line,
                "                                            ",
                channels->channel_list[i]);
            free(channel_line);
          }
        }

        ctk_menu_sort(main_interface.m_results, suscan_compare_channels);

        ctk_widget_redraw(main_interface.m_results);

        suscan_redraw_results_header();
        break;
    }

    suscan_worker_dispose_message(type, ptr);

    ctk_update();
  }
}

int
main(int argc, char *argv[], char *envp[])
{
  unsigned int i;
  int exit_code = EXIT_FAILURE;
  struct suscan_source_config *config;
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

  for (i = 1; i < argc; ++i) {
    if ((config = suscan_source_string_to_config(argv[i])) == NULL) {
      fprintf(stderr, "%s: failed to parse source string\n", argv[0]);
      goto done;
    }

    if (!suscan_open_source(config)) {
      fprintf(
          stderr,
          "%s: failed to open source `%s'\n",
          argv[0],
          config->source->desc);
      suscan_source_config_destroy(config);
      goto done;
    }
  }

  if (suscan_ui_loop(argv[0]))
    exit_code = EXIT_SUCCESS;

  endwin();

  fprintf(stderr, "%s: waiting for other threads to stop...\n", argv[0]);
  pthread_cancel(kbd_thread);
  pthread_join(kbd_thread, NULL);

  fprintf(stderr, "%s: terminating all workers...\n", argv[0]);
  for (i = 0; i < main_interface.worker_count; ++i)
    suscan_worker_destroy(main_interface.worker_list[i]);

  suscan_clear_mq();

done:
  exit(exit_code);
}

