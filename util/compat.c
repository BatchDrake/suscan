/*
  
  Copyright (C) 2019 Gonzalo José Carracedo Carballal
  
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

#define _COMPAT_BARRIERS

#include <stdlib.h>
#include "compat.h"

#if defined(__APPLE__)
#  include "macos-barriers.imp.h"
#  include "macos-bundle.imp.h"
#elif defined(_WIN32)
#  include "win32-bundle.imp.h"
#else
const char *
suscan_bundle_get_confdb_path(void)
{
  return NULL; /* No bundle path in the default OS */
}

const char *
suscan_bundle_get_soapysdr_module_path(void)
{
  return NULL; /* No default SoapySDR root in the default OS */
}

#endif /* defined(__APPLE__) */

