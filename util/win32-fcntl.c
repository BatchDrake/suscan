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

#include "win32-fcntl.h"

#include "win32-socket.h"

/* WARN: EXTREMELY ADHOC */
int fcntl(int fd, int cmd, ... /* arg */ ) {
	switch (cmd) {
		case F_GETFL: {
			/* Assume suscan just wants whatever flags the fd has and add O_NONBLOCK to them, so it doesn't matter what this returns */
			return 0;
		} break;
		case F_SETFL: {
			/* Assume suscan always wants to set fd to non blocking mode */
			u_long iMode = 0;
			int iResult = ioctlsocket(fd, FIONBIO, &iMode);
			if (iResult != NO_ERROR)
				return -1;
		} break;
	}
	return 0;
}