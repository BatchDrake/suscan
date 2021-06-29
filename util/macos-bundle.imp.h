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

#define SU_LOG_DOMAIN "macos-bundle"

#include <stdlib.h>
#include <sigutils/log.h>
#include <SoapySDR/Version.h>
#include "compat.h"

#include <CoreFoundation/CoreFoundation.h>

SUPRIVATE const char *
suscan_bundle_get_resource_path(CFStringRef relpath)
{
  CFBundleRef main_bundle = NULL;
  CFURLRef dir_url = NULL;
  CFStringRef dir_path = NULL;
  CFStringEncoding encmethod;
  const char *path = NULL;

  if ((main_bundle = CFBundleGetMainBundle()) != NULL) {
    SU_TRYCATCH(
        dir_url = CFBundleCopyResourceURL(
                  main_bundle,
                  relpath,
                  NULL, /* resourceType */
                  NULL /* dirName */),
        goto done);

    SU_TRYCATCH(
        dir_path = CFURLCopyFileSystemPath(dir_url, kCFURLPOSIXPathStyle),
        goto done);

    encmethod = CFStringGetSystemEncoding();
    path = CFStringGetCStringPtr(dir_path, encmethod);
  }

done:
  return path;
}


const char *
suscan_bundle_get_soapysdr_module_path(void)
{
  return suscan_bundle_get_resource_path(
            CFSTR("../Framework/SoapySDR/modules" SOAPY_SDR_ABI_VERSION));
}

const char *
suscan_bundle_get_confdb_path(void)
{
  return suscan_bundle_get_resource_path(CFSTR("suscan/config"));
}
