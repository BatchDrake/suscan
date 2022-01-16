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

#include "win32-pwd.h"
#include <Shlobj.h>

// WARN: VERY ADHOC: dummy function lol
uid_t getuid() {
	return 0;
}

// WARN: VERY ADHOC: ignores uid, only populates pw_dir
struct passwd *getpwuid(uid_t uid) {
	struct passwd *pw = malloc(sizeof(struct passwd));
	memset(pw, 0, sizeof(struct passwd));

	char *homeDirStr = malloc(MAX_PATH);
	if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_PERSONAL, NULL, 0, homeDirStr))) {
		pw->pw_dir = homeDirStr;
	} else {
		return NULL;
	}
	
	return pw;
}