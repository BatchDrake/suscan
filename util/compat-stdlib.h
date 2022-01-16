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

#ifndef _UTIL_COMPAT_STDLIB_H
#define _UTIL_COMPAT_STDLIB_H

#  ifdef _WIN32
#    include "win32-stdlib.h"
#  else
#    include <stdlib.h>
#  endif /* _WIN32 */

#endif /* _UTIL_COMPAT_STDLIB_H */