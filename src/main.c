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

#include "suscan.h"

SUPRIVATE struct option long_options[] = {
    {"fingerprint", no_argument, NULL, 'f'},
    {"help", no_argument, NULL, 'h'},
    {NULL, 0, NULL, 0}
};

extern int optind;

SUPRIVATE void
help(const char *argv0)
{
  fprintf(stderr, "Usage:\n");
  fprintf(stderr, "  %s [options] [source1 [source2 [...]]]\n\n", argv0);
  fprintf(
      stderr,
      "A GNU/Linux sigutils-based frequency scanner\n\n");
  fprintf(stderr, "Options:\n\n");
  fprintf(stderr, "     -f, --fingerprint     Performs fingerprinting on all\n");
  fprintf(stderr, "                           specified sources\n");
  fprintf(stderr, "     -h, --help            This help\n\n");
  fprintf(stderr, "(c) 2017 Gonzalo J. Caracedo <BatchDrake@gmail.com>\n");
}

int
main(int argc, char *argv[], char *envp[])
{
  struct suscan_source_config *config = NULL;
  PTR_LIST_LOCAL(struct suscan_source_config, config);
  struct timeval tv;
  enum suscan_mode mode = SUSCAN_MODE_GTK_UI;
  int exit_code = EXIT_FAILURE;
  char *msgs;
  unsigned int i;
  int c;
  int index;

  while ((c = getopt_long(argc, argv, "fh", long_options, &index)) != -1) {
    switch (c) {
      case 'f':
        mode = SUSCAN_MODE_FINGERPRINT;
        break;

      case 'h':
        help(argv[0]);
        exit(EXIT_SUCCESS);
        break;

      case '?':
        help(argv[0]);
        exit(EXIT_FAILURE);
        break;

      default:
        abort();
    }
  }

  if (!suscan_sigutils_init(mode)) {
    fprintf(stderr, "%s: failed to initialize sigutils library\n", argv[0]);
    goto done;
  }

  if (!suscan_init_sources()) {
    fprintf(stderr, "%s: failed to initialize sources\n", argv[0]);
    goto done;
  }

  for (i = optind; i < argc; ++i) {
    if ((config = suscan_source_string_to_config(argv[i])) == NULL) {
      fprintf(
          stderr,
          "%s: cannot parse source string:\n\n  %s\n\n",
          argv[0],
          argv[i]);
      goto done;
    }

    if (PTR_LIST_APPEND_CHECK(config, config) == -1) {
      fprintf(stderr, "%s: failed to build source list\n", argv[0]);
      goto done;
    }
  }

  switch (mode) {
    case SUSCAN_MODE_GTK_UI:
      gettimeofday(&tv, NULL);
      if (suscan_gui_start(argc, argv, config_list, config_count)) {
        exit_code = EXIT_SUCCESS;
      } else {
        if ((msgs = suscan_log_get_last_messages(tv, 20)) != NULL) {
          fprintf(
              stderr,
              "%s: Gtk GUI failed to start, last error messages were:\n",
              argv[0]);

          fprintf(stderr, "---------8<-------------------------------------\n");
          fprintf(stderr, "%s", msgs);
          fprintf(stderr, "---------8<-------------------------------------\n");
        } else {
          fprintf(
              stderr,
              "%s: Gtk GUI failed to start, memory error?\n",
              argv[0]);
        }

      }
      break;

    case SUSCAN_MODE_FINGERPRINT:
      if (config_count == 0) {
        fprintf(stderr, "%s: no sources given for fingerprint\n", argv[0]);
        goto done;
      } else {
        for (i = 0; i < config_count; ++i) {
          fprintf(
              stderr,
              "%s: fingerprinting `%s'...\n",
              argv[0],
              argv[optind + i]);
          if (!suscan_perform_fingerprint(config_list[i]))
            fprintf(
                stderr,
                "%s: cannot fingerprint `%s'\n",
                argv[0],
                argv[optind + i]);
        }

        exit_code = EXIT_SUCCESS;
      }

      break;
  }

done:
  for (i = 0; i < config_count; ++i)
    suscan_source_config_destroy(config_list[i]);

  exit(exit_code);
}

