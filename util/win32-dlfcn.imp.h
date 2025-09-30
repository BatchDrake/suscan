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

#define SU_LOG_DOMAIN "win32-dlfcn"

#include "win32-dlfcn.h"
#include <sigutils/log.h>
#include <errno.h>
#include <windows.h>

SUPRIVATE dlfcn_error_state g_dlfcn_state;

SUPRIVATE void
dl_set_last_error(const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);

  vsnprintf(g_dlfcn_state.errbuf, DLFCN_ERR_BUFF_MAX, fmt, ap);
  g_dlfcn_state.last_error = g_dlfcn_state.errbuf;

  va_end(ap);
}

void *
dlopen(const char *path, int)
{
  HINSTANCE hInst;

  hInst = LoadLibraryA(path);
  if (hInst == nullptr)
    dl_set_last_error("LoadLibrary: %s", GetLastError());
  return hInst;
}

int
dlclose(void *handle)
{
  int ret = 0;

  if (!FreeLibrary(SCAST(HINSTANCE, handle))) {
    dl_set_last_error("FreeLibrary: %s", GetLastError());
    ret = -1;
  }

  return ret;
}

void *
dlsym(void *handle, const char *name)
{
  FARPROC proc;
  void *asPtr;

  proc  = GetProcAddress(SCAST(HINSTANCE, handle), name);
  asPtr = reinterpret_cast<void *>(proc);

  if (asPtr == nullptr)
    dl_set_last_error("GetProcAddress: %s", GetLastError());

  return asPtr;
}

char *
dlerror()
{
  const char *error = g_dlfcn_state.last_error;

  g_dlfcn_state.last_error = nullptr;

  return error;
}
