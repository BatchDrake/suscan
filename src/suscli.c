/*

  Copyright (C) 2020 Gonzalo José Carracedo Carballal

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation, version 3.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this program.  If not, see
  <http://www.gnu.org/licenses/>

*/

#define SU_LOG_DOMAIN "suscli"

#include <stdio.h>
#include <stdlib.h>
#include <suscan.h>
#include <cli/cli.h>
#include <analyzer/version.h>
#include <analyzer/device/facade.h>
#ifdef __unix__
#  include <signal.h>
#endif /* __unix__ */


SUPRIVATE SUBOOL
suscan_init(const char *a0)
{
  SUBOOL ok = SU_FALSE;

  if (!suscan_sigutils_init(SUSCAN_MODE_IMMEDIATE)) {
    fprintf(stderr, "%s: failed to initialize sigutils library\n", a0);
    goto done;
  }

  if (!suscan_plugin_load_all()) {
    fprintf(stderr, "%s: failed to load all plugins\n", a0);
    goto done;
  }

  su_log_set_mask(0);

  ok = SU_TRUE;

done:
  return ok;
}

SUPRIVATE void
help(const char *a0)
{
  fprintf(stderr, "Usage:\n");
  fprintf(stderr, "  %s command [param1=val [param2=val [...]]]\n\n", a0);

  fprintf(
      stderr,
      "Type `%s list` to print a list of available commands\n\n",
        a0);

  fprintf(stderr, "suscan " SUSCAN_VERSION_STRING "\n");
  fprintf(stderr, "pkgversion: %s\n", suscan_pkgversion());
  fprintf(
      stderr,
      "Using sigutils version %s (%s)\n\n",
      sigutils_api_version(),
      sigutils_pkgversion());

  fprintf(stderr, "Copyright © 2020 Gonzalo José Carracedo Carballal\n");
  fprintf(
        stderr,
        "License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>\n");
}

#ifdef __unix__
void
suscli_sigint_handler(int sig)
{
  exit(1);
}
#endif /* __unix__ */

int
main(int argc, const char *argv[], char *envp[])
{
  int ret = EXIT_FAILURE;

  if (argc < 2) {
    help(argv[0]);
    ret = EXIT_SUCCESS;
    goto done;
  }

  if (!suscan_init(argv[0])) {
    fprintf(stderr, "%s: required components could not be loaded\n", argv[0]);
    goto done;
  }

  if (!suscli_init()) {
    fprintf(stderr, "%s: Suscan command line failed to load\n", argv[0]);
    goto done;
  }

#ifdef __unix__
  signal(SIGINT, suscli_sigint_handler);
#endif /* __unix__ */

  if (suscli_run_command(argv[1], &argv[2]))
    ret = EXIT_SUCCESS;

done:
  suscan_device_facade_cleanup();
  
  exit(ret);
}
