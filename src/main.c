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
    {"help", no_argument, NULL, 'h'},
    {NULL, 0, NULL, 0}
};

extern int optind;

SUPRIVATE void
help(const char *argv0)
{
  fprintf(stderr, "Usage:\n");
  fprintf(stderr, "  %s [options] \n\n", argv0);
  fprintf(
      stderr,
      "This command will attempt to load Suscan library and display load errors.\n\n");
  fprintf(stderr, "Options:\n\n");
  fprintf(stderr, "     -h, --help            This help\n\n");
  fprintf(stderr, "(c) 2019 Gonzalo J. Caracedo <BatchDrake@gmail.com>\n");
}

int
main(int argc, char *argv[], char *envp[])
{
  struct timeval tv;
  int exit_code = EXIT_FAILURE;
  char *msgs;
  unsigned int i;
  int c;
  int index;

#ifdef DEBUG_WITH_MTRACE
  mtrace();
#endif

  while ((c = getopt_long(argc, argv, "h", long_options, &index)) != -1) {
    switch (c) {
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

  gettimeofday(&tv, NULL);

  if (!suscan_sigutils_init(SUSCAN_MODE_GTK_UI)) {
    fprintf(stderr, "%s: failed to initialize sigutils library\n", argv[0]);
    goto done;
  }

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

  fprintf(stderr, "%s: suscan library loaded successfully.\n", argv[0]);
  exit_code = 0;
  
done:
  if ((msgs = suscan_log_get_last_messages(tv, 20)) != NULL) {
    if (*msgs) {
      fprintf(stderr, "---------8<-------------------------------------\n");
      fprintf(stderr, "%s", msgs);
      fprintf(stderr, "---------8<-------------------------------------\n");
    }
    free(msgs);
  }

#ifdef DEBUG_WITH_MTRACE
  muntrace();
#endif

  exit(exit_code);
}

