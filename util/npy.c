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

#define SU_LOG_DOMAIN "npy-file"

#include <sigutils/log.h>

#include "npy.h"
#include <string.h>
#include <errno.h>
#include <stdarg.h>

static const char *g_dtype_str[] = {
  "<i4",
  "<f4",
  "<f8"
};

SUPRIVATE
SU_METHOD(npy_file, SUBOOL, write_header)
{
  SUBOOL ok  = SU_FALSE;
  uint8_t *header = NULL;
  char    *shape  = NULL;
  char    *tmp    = NULL;
  char    *dict = NULL;
  size_t full_size;
  size_t dict_len;
  size_t pad_len;

  uint16_t header_size;

  unsigned int i;

  SU_TRY(shape = strbuild(""));

  for (i = 0; i < self->ndim; ++i) {
    if (i == 0) {
      SU_TRY(tmp = strbuild("%d", self->shape[0]));
    } else {
      SU_TRY(tmp = strbuild("%s, %d", shape, self->shape[i]));
    }

    if (shape != NULL)
      free(shape);
    shape = tmp;
  }

  SU_TRY(
    dict = strbuild(
      "{'descr': '%s', 'fortran_order': False, 'shape': (%s), }",
      g_dtype_str[self->dtype],
      shape));

  dict_len  = strlen(dict);
  full_size = __ALIGN(10 + dict_len, 64);
  pad_len   = full_size - dict_len - 10;

  if (full_size - 10 > 65535) {
    SU_ERROR(
      "NPY header size is too big! (%d byte size does not fit in 16 bits)\n",
      full_size - 10);
    goto done;
  }

  header_size = full_size - 10;

  SU_ALLOCATE_MANY(header, full_size, uint8_t);

  memcpy(header, "\x93NUMPY\x01\x00", 8);
  memcpy(header + 8, &header_size, sizeof(uint16_t));

  memcpy(header + 10, dict, dict_len);
  memset(header + 10 + dict_len, ' ', pad_len);

  header[full_size - 1] = '\n';

  if (fwrite(header, full_size, 1, self->fp) < 1) {
    SU_ERROR("Cannot write NPY header: %s\n", strerror(errno));
    goto done;
  }

  fflush(self->fp);

  ok = SU_TRUE;

done:
  if (dict != NULL)
    free(dict);

  if (shape != NULL)
    free(shape);

  if (header != NULL)
    free(header);
  
  return ok;
}

SU_INSTANCER(npy_file, FILE *fp, enum npy_dtype dtype, ...)
{
  SUBOOL        ok  = SU_FALSE;
  npy_file_t   *new = NULL;
  int           dim_size;
  unsigned int *tmp = NULL;
  va_list ap;

  va_start(ap, dtype);

  if (dtype < 0 || dtype >= NPY_DTYPE_MAX) {
    SU_ERROR("Unsupported NPY dtype\n");
    goto done;
  }

  SU_ALLOCATE(new, npy_file_t);

  new->fp    = fp;
  new->dtype = dtype;
  
  while ((dim_size = va_arg(ap, int)) >= 0) {
    if (new->ndim >= NPY_MAX_DIMS) {
      SU_ERROR("Too many array dimensions\n");
      goto done;
    }

    if ((tmp = realloc(
      new->shape, 
      sizeof(unsigned) * (new->ndim + 1))) == NULL) {
        SU_ERROR("Memory exhausted while allocating array dimensions\n");
        goto done;
    }

    new->shape              = tmp;
    new->shape[new->ndim++] = dim_size;
  }

  SU_TRY(npy_file_write_header(new));

  ok = SU_TRUE;

done:
  if (!ok && new != NULL) {
    SU_DISPOSE(npy_file, new);
    new = NULL;
  }

  va_end(ap);

  return new;
}

SU_COLLECTOR(npy_file)
{
  if (self->shape != NULL)
    free(self->shape);

  free(self);
}


