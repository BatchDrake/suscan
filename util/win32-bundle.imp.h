/*

  Copyright (C) 2022 Ángel Ruiz Fernández

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

#define SU_LOG_DOMAIN "win32-bundle"

#include <sigutils/log.h>
#include <sigutils/util/util.h>
#include <stdlib.h>
#include <libgen.h>
#include <SoapySDR/Version.h>

#include <windows.h>

SUPRIVATE const char *g_modpath = NULL;
SUPRIVATE const char *g_configpath = NULL;

SUPRIVATE const char *
get_bundle_path(const char *file)
{
  char *thismodpath = NULL, *tmp;
  char *path = NULL;
  char *pathtofile = NULL;

  unsigned int alloc = 256; /* Seems reasonable */

  /* Exponentially allocate bigger and bigger paths until we make it */
  do {
    SU_TRYCATCH(tmp = realloc(thismodpath, alloc), goto done);
    thismodpath = tmp;

    SU_TRYCATCH(GetModuleFileNameA(NULL, thismodpath, alloc), goto done);
    alloc <<= 1;
  } while (GetLastError() == ERROR_INSUFFICIENT_BUFFER);
  
  /* Compute full path and return  */
  path = dirname(thismodpath);
  SU_TRYCATCH(
    pathtofile = strbuild("%s\\%s", path, file),
	  goto done);
  
done:
  if (thismodpath != NULL)
	  free(thismodpath);
	
  return pathtofile;
}

const char *
suscan_bundle_get_soapysdr_module_path(void)
{
  if (g_modpath == NULL)
    g_modpath = get_bundle_path("modules" SOAPY_SDR_ABI_VERSION);
  
  return g_modpath;
}

const char *
suscan_bundle_get_confdb_path(void)
{
  if (g_configpath == NULL)
    g_configpath = get_bundle_path("config");
  
  return g_configpath;
}

const char *
suscan_bundle_get_plugin_path(void)
{
  if (g_configpath == NULL)
    g_configpath = get_bundle_path("plugins");
  
  return g_configpath;
}
