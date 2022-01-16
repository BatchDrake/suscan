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

#ifndef _UTIL_STAT_H
#define _UTIL_STAT_H

#include <sys/stat.h>

#if (defined(_WIN32) || defined(__WIN32__))
# define mkdir(A, B) mkdir(A)
#endif
				
#endif /* _UTIL_STAT_H */