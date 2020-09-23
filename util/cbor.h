/*
 * Copyright (c) 2017-2019 Josef 'Jeff' Sipek <jeffpc@josefsipek.net>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _UTIL_CBOR_H
#define _UTIL_CBOR_H

#include <errno.h>
#include <sigutils/types.h>

#define CBOR_UNKNOWN_NELEM  (~0ul)

#ifdef _SU_SINGLE_PRECISION
#  define cbor_pack_float   cbor_pack_single
#  define cbor_unpack_float cbor_unpack_single
#else
#  define cbor_pack_float   cbor_pack_double
#  define cbor_unpack_float cbor_unpack_double
#endif /* _SU_SINGLE_PRECISION */

#define cbor_pack_freq      cbor_pack_double
#define cbor_unpack_freq    cbor_unpack_double

/*
 * On failure, the buffer may contain partially encoded data items.  On
 * success, a fully encoded data item is appended to the buffer.
 */
int cbor_pack_uint(grow_buf_t *buffer, uint64_t v);
int cbor_pack_nint(grow_buf_t *buffer, uint64_t v);
int cbor_pack_int(grow_buf_t *buffer, int64_t v);
int cbor_pack_blob(grow_buf_t *buffer, const void *data,
        size_t size);
int cbor_pack_cstr_len(grow_buf_t *buffer, const char *str, size_t len);
int cbor_pack_str(grow_buf_t *buffer, const char *str);
int cbor_pack_bool(grow_buf_t *buffer, SUBOOL b);
int cbor_pack_single(grow_buf_t *buffer, SUSINGLE v);
int cbor_pack_double(grow_buf_t *buffer, SUDOUBLE v);
int cbor_pack_null(grow_buf_t *buffer);
int cbor_pack_break(grow_buf_t *buffer);
int cbor_pack_array_start(grow_buf_t *buffer, size_t nelem);
int cbor_pack_array_end(grow_buf_t *buffer, size_t nelem);
int cbor_pack_map_start(grow_buf_t *buffer, size_t npairs);
int cbor_pack_map_end(grow_buf_t *buffer, size_t npairs);

SUINLINE
int cbor_pack_cstr(grow_buf_t *buffer, const char *str)
{
  return cbor_pack_cstr_len(buffer, str, strlen(str));
}


/*
 * On failure, the buffer state is unchanged.  On success, the buffer is
 * updated to point to the first byte of the next data item.
 */
int cbor_unpack_uint(grow_buf_t *buffer, uint64_t *v);
int cbor_unpack_nint(grow_buf_t *buffer, uint64_t *v);
int cbor_unpack_int(grow_buf_t *buffer, int64_t *v);
int cbor_unpack_blob(grow_buf_t *buffer, void **data, size_t *size);
int cbor_unpack_cstr_len(grow_buf_t *buffer, char **str,
        size_t *len);
int cbor_unpack_str(grow_buf_t *buffer, char **str);
int cbor_unpack_bool(grow_buf_t *buffer, SUBOOL *b);
int cbor_unpack_single(grow_buf_t *buffer, SUSINGLE *value);
int cbor_unpack_double(grow_buf_t *buffer, SUDOUBLE *value);
int cbor_unpack_null(grow_buf_t *buffer);
int cbor_unpack_break(grow_buf_t *buffer);
int cbor_unpack_map_start(grow_buf_t *buffer, uint64_t *npairs,
                                 SUBOOL *end_required);
int cbor_unpack_map_end(grow_buf_t *buffer, SUBOOL end_required);
int cbor_unpack_array_start(grow_buf_t *buffer, uint64_t *nelem,
                                   SUBOOL *end_required);
int cbor_unpack_array_end(grow_buf_t *buffer, SUBOOL end_required);

#define CBOR_INT_UNPACKER(size)                                         \
SUINLINE int                                                            \
JOIN(cbor_unpack_int, size)(                                            \
    grow_buf_t *buffer,                                                 \
    JOIN(JOIN(int, size), _t) *v)                                       \
{                                                                       \
  int ret;                                                              \
  int64_t gen_int;                                                      \
                                                                        \
  if ((ret = cbor_unpack_int(buffer, &gen_int)) != 0)                   \
    return ret;                                                         \
                                                                        \
  /* TODO: Detect overflows */                                          \
  *v = gen_int;                                                         \
                                                                        \
  return 0;                                                             \
}

#define CBOR_UINT_UNPACKER(size)                                        \
SUINLINE int                                                            \
JOIN(cbor_unpack_uint, size)(                                           \
    grow_buf_t *buffer,                                                 \
    JOIN(JOIN(uint, size), _t) *v)                                      \
{                                                                       \
  int ret;                                                              \
  uint64_t gen_int;                                                     \
                                                                        \
  if ((ret = cbor_unpack_uint(buffer, &gen_int)) != 0)                  \
    return ret;                                                         \
                                                                        \
  /* TODO: Detect overflows */                                          \
  *v = gen_int;                                                         \
                                                                        \
  return 0;                                                             \
}

CBOR_INT_UNPACKER(8)
CBOR_INT_UNPACKER(16)
CBOR_INT_UNPACKER(32)
CBOR_INT_UNPACKER(64)

CBOR_UINT_UNPACKER(8)
CBOR_UINT_UNPACKER(16)
CBOR_UINT_UNPACKER(32)
CBOR_UINT_UNPACKER(64)

#endif /* _UTIL_CBOR_H */
