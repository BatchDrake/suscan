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

#include "version.h"

#ifdef SUSCAN_THIN_CLIENT
# define SUSCAN_LIB_SFX "-thinclient"
#else
# define SUSCAN_LIB_SFX ""
#endif /* SUSCAN_THIN_CLIENT */

#ifndef SUSCAN_PKGVERSION
#  define SUSCAN_PKGVERSION SUSCAN_VERSION_STRING SUSCAN_LIB_SFX \
  " custom build on " __DATE__ " at " __TIME__ " (" __VERSION__ ")"
#endif /* SUSCAN_BUILD_STRING */

unsigned int
suscan_abi_version(void)
{
  return SUSCAN_ABI_VERSION;
}

const char *
suscan_api_version(void)
{
  return SUSCAN_VERSION_STRING SUSCAN_LIB_SFX;
}

const char *
suscan_pkgversion(void)
{
  return SUSCAN_PKGVERSION;
}

void
suscan_abi_check(unsigned int abi)
{
  if (abi != SUSCAN_ABI_VERSION) {
    fprintf(stderr, "*** SUSCAN CRITICAL LIBRARY ERROR ***\n");
    fprintf(stderr, "Expected ABI version (v%u) is incompatible with current\n", abi);
    fprintf(stderr, "suscan ABI version (v%u).\n\n", (unsigned) SUSCAN_ABI_VERSION);

    if (abi < SUSCAN_ABI_VERSION) {
      fprintf(stderr, "The current suscan ABI version is too new compared to\n");
      fprintf(stderr, "the version expected by the user software. Please\n");
      fprintf(stderr, "update your software or rebuild it with an updated\n");
      fprintf(stderr, "version of suscan' development files\n\n");
    } else {
      fprintf(stderr, "The current suscan ABI version is too old compared to\n");
      fprintf(stderr, "the version expected by the user software. This usually\n");
      fprintf(stderr, "happens when the user software is installed in an older\n");
      fprintf(stderr, "system without fixing its dependencies. Please verify\n");
      fprintf(stderr, "your installation and try again.\n");
    }

    abort();
  }
}

