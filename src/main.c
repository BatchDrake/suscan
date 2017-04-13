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

int
main(int argc, char *argv[], char *envp[])
{
  struct suscan_source_config *config = NULL;
  PTR_LIST_LOCAL(struct suscan_source_config, config);
  int exit_code = EXIT_FAILURE;
  unsigned int i;

  if (!suscan_sigutils_init()) {
    fprintf(stderr, "%s: failed to initialize sigutils library\n", argv[0]);
    goto done;
  }

  if (!suscan_init_sources()) {
    fprintf(stderr, "%s: failed to initialize sources\n", argv[0]);
    goto done;
  }

  for (i = 1; i < argc; ++i) {
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

  if (suscan_ctk_ui_start(argv[0], config_list, config_count))
    exit_code = EXIT_SUCCESS;

done:
  for (i = 0; i < config_count; ++i)
    suscan_source_config_destroy(config_list[i]);

  exit(exit_code);
}

