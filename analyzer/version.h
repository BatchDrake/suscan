/*

  Copyright (C) 2020 Gonzalo José Carracedo Carballal

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

#ifndef _SUSCAN_VERSION
#define _SUSCAN_VERSION

#include <sigutils/version.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/*
 * Suscan API uses semantic versioning (see https://semver.org/).
 *
 * Suscan ABI follows the same strategy as Mongocxx (see
 * http://mongocxx.org/mongocxx-v3/api-abi-versioning/): ABI is a simple
 * scalar that is bumped on an incompatible ABI change (e.g. a function
 * becomes a macro or vice-versa). ABI additions DO NOT increment
 * the ABI number.
 */

/* API version macros */
#define SUSCAN_VERSION_MAJOR 0
#define SUSCAN_VERSION_MINOR 1
#define SUSCAN_VERSION_PATCH 1

/* ABI version macros */
#define SUSCAN_ABI_VERSION   1

/* Full version macros */
#define SUSCAN_VERSION \
  SU_VER(SUSCAN_VERSION_MAJOR, SUSCAN_VERSION_MINOR, SUSCAN_VERSION_PATCH)

#define SUSCAN_API_VERSION \
  SU_VER(SUSCAN_VERSION_MAJOR, SUSCAN_VERSION_MINOR, 0)

#define SUSCAN_VERSION_STRING        \
  STRINGIFY(SUSCAN_VERSION_MAJOR) "." \
  STRINGIFY(SUSCAN_VERSION_MINOR) "." \
  STRINGIFY(SUSCAN_VERSION_PATCH)


unsigned int suscan_abi_version(void);
const char  *suscan_api_version(void);
const char  *suscan_pkgversion(void);

void suscan_abi_check(unsigned int);

#define SUSCAN_ABI_CHECK() suscan_abi_check(SUSCAN_ABI_VERSION)

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _SUSCAN_VERSION */

