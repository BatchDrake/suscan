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

#ifndef _UTIL_FCNTL_H
#define _UTIL_FCNTL_H

#define	_O_BINARY	0x8000	/* Input and output is not translated. */

#define	F_GETFL		3	    /* Get file flags */
#define	F_SETFL		4	    /* Set file flags */

#define	O_NONBLOCK	0x4000  /* non blocking I/O (POSIX style) */

#include <stdarg.h>

int fcntl(int fd, int cmd, ... /* arg */ );

#endif /* _UTIL_FCNTL_H */