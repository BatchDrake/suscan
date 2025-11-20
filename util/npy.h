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

#define NPY_MAX_DIMS            10
#define NPY_FLUSH_HEADER_STEP 1024

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
  uint16_t  full_header_size;

  uint64_t  size;
  uint64_t  stride;
  uint64_t  column;
  uint64_t  last_flush;
};

typedef struct npy_file npy_file_t;

SU_INSTANCER(npy_file, FILE *fp, enum npy_dtype, ...);
SU_METHOD(npy_file, SUBOOL, update_shape);
SU_COLLECTOR(npy_file);

#define DECLARE_NPY_STORE_FUNC(name, type)                                     \
  SUBOOL                                                                       \
  JOIN(npy_file_store_, name)(const char *path, const type *data, size_t count)

#define DEFINE_NPY_WRITER(name, type)                                          \
  SUINLINE                                                                     \
  SU_METHOD(npy_file, SUBOOL, JOIN(write_, name), const type *data, size_t len) \
  {                                                                            \
    if (fwrite(data, sizeof(type), len, self->fp) == len) {                    \
      self->size   += len;                                                     \
      self->column += len;                                                     \
      if (self->column >= self->stride) {                                      \
        self->column %= self->stride;                                          \
        return npy_file_update_shape(self);                                    \
      }                                                                        \
      return SU_TRUE;                                                          \
    }                                                                          \
                                                                               \
    return SU_FALSE;                                                           \
  }

DEFINE_NPY_WRITER(int32,   int32_t)
DEFINE_NPY_WRITER(float32, float)
DEFINE_NPY_WRITER(float64, double)

DECLARE_NPY_STORE_FUNC(int32,   int32_t);
DECLARE_NPY_STORE_FUNC(float32, float);
DECLARE_NPY_STORE_FUNC(float64, double);

#endif /* _UTIL_NPY_H */
