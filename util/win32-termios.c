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

#include "win32-termios.h"

#include <conio.h>

ssize_t read_noecho_noicanon(int fd, void *buf, size_t count) {
	char *buff = (char*)buf;
	for (int i = 0; i < count; i++) {
		buff[i] = (char)_getch();
		buff++;
	}
	return count;
}

int tcgetattr(int fd, struct termios *termios_p) {
	return 0;
}

int tcsetattr(int fd, int optional_actions, const struct termios *termios_p) {
	return 0;
}

