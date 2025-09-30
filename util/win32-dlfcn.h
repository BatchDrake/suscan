/*
  
  Copyright (C) 2025 Gonzalo José Carracedo Carballal
  
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

#ifndef _COMPAT_WIN32_DLFCN_H
#define _COMPAT_WIN32_DLFCN_H

#include <stdarg.h>
#include <inttypes.h>

#define DLFCN_ERR_BUFF_MAX 256
#define RTLD_GLOBAL        0x100
#define RTLD_LOCAL         0x000
#define RTLD_LAZY          0x000
#define RTLD_NOW           0x001

struct dlfcn_error_state {
  const char *last_error;
  char errbuf[DLFCN_ERR_BUFF_MAX];
};

void       *dlopen(const char *path, int);
int         dlclose(void *handle);
void       *dlsym(void *handle, const char *name);
const char *dlerror();

#endif /* _COMPAT_WIN32_DLFCN_H */
