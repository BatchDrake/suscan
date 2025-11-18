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

#ifndef _UTIL_NPY_H
#define _UTIL_NPY_H

#include <sigutils/defs.h>
#include <sigutils/types.h>

#define NPY_MAX_DIMS 10

enum npy_dtype {
  NPY_DTYPE_INT32,
  NPY_DTYPE_FLOAT32,
  NPY_DTYPE_FLOAT64,
  NPY_DTYPE_MAX
};

struct npy_file {
  FILE          *fp;

  enum npy_dtype dtype;

  unsigned *shape;
  unsigned  ndim;
};

typedef struct npy_file npy_file_t;

SU_INSTANCER(npy_file, FILE *fp, enum npy_dtype, ...);
SU_COLLECTOR(npy_file);

#define DEFINE_NPY_WRITER(name, type)                                          \
  SUINLINE                                                                     \
  SU_METHOD(npy_file, SUBOOL, JOIN(write, name), const type *data, size_t len) \
  {                                                                            \
    return fwrite(data, sizeof(type), len, self->fp) == len;                   \
  }

DEFINE_NPY_WRITER(int32,   int32_t)
DEFINE_NPY_WRITER(float32, float)
DEFINE_NPY_WRITER(float64, double)

#endif /* _UTIL_NPY_H */
