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

#include <confdb.h>
#include <suscan.h>
#include <codec.h>

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

#ifdef DEBUG_WITH_MTRACE
  mtrace();
#endif

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

  if (mode == SUSCAN_MODE_GTK_UI)
    gettimeofday(&tv, NULL);

  if (!suscan_sigutils_init(mode)) {
    fprintf(stderr, "%s: failed to initialize sigutils library\n", argv[0]);
    goto done;
  }

  /*
   * This block has been moved to gui.c, but it will probably be used
   * in fingerprint mode.
   */
#if 0
  if (!suscan_codec_class_register_builtin()) {
    fprintf(
        stderr,
        "%s: failed to initialize builtin codecs\n",
        argv[0]);
    goto done;
  }

  if (!suscan_init_sources()) {
    fprintf(stderr, "%s: failed to initialize sources\n", argv[0]);
    goto done;
  }

  if (!suscan_init_estimators()) {
    fprintf(stderr, "%s: failed to initialize estimators\n", argv[0]);
    goto done;
  }

  if (!suscan_init_spectsrcs()) {
    fprintf(stderr, "%s: failed to initialize spectrum sources\n", argv[0]);
    goto done;
  }

  if (!suscan_init_inspectors()) {
    fprintf(stderr, "%s: failed to initialize inspectors\n", argv[0]);
    goto done;
  }
#endif

  switch (mode) {
    case SUSCAN_MODE_GTK_UI:
      if (suscan_gui_start(argc, argv, config_list, config_count)) {
        exit_code = EXIT_SUCCESS;
      } else {
        fprintf(
            stderr,
            "%s: Gtk GUI failed to start, last error messages were:\n",
            argv[0]);
      }
      break;

    case SUSCAN_MODE_FINGERPRINT:
      fprintf(stderr, "%s: fingerprint mode not implemented\n", argv[0]);
      break;
  }

done:
  if ((msgs = suscan_log_get_last_messages(tv, 20)) != NULL) {
    if (*msgs) {
      fprintf(stderr, "---------8<-------------------------------------\n");
      fprintf(stderr, "%s", msgs);
      fprintf(stderr, "---------8<-------------------------------------\n");
    }
    free(msgs);
  }

  for (i = 0; i < config_count; ++i)
    suscan_source_config_destroy(config_list[i]);

  (void) suscan_confdb_save_all();

#ifdef DEBUG_WITH_MTRACE
  muntrace();
#endif

  exit(exit_code);
}

