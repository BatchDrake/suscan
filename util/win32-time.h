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

#ifndef _UTIL_TIME_H
#define _UTIL_TIME_H

#include <stdint.h>
#include <time.h>
#include <sys/time.h>

#define gmtime_r(timep, result) gmtime_s(result, timep)
#define localtime_r(timep, result) localtime_s(result, timep)

void timeradd(const struct timeval *a, const struct timeval *b,
							struct timeval *res);

void timersub(const struct timeval *a, const struct timeval *b,
							struct timeval *res);
				
#endif /* _UTIL_TIME_H */