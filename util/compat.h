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

#ifndef _COMPAT_H
#define _COMPAT_H

#  ifdef _COMPAT_BARRIERS
#    ifdef __APPLE__
#      include "macos-barriers.h"
#    else
#      include <pthread.h>
#    endif /* __APPLE__ */
#  endif /* _COMPAT_BARRIERS */

const char *suscan_bundle_get_confdb_path(void);
const char *suscan_bundle_get_soapysdr_module_path(void);

#endif /* _COMPAT_H */

