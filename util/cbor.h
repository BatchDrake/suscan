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

#include <string.h>
#include <sigutils/types.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define CBOR_UNKNOWN_NELEM  (~0ul)

#ifdef _SU_SINGLE_PRECISION
#  define cbor_pack_float   cbor_pack_single
#  define cbor_unpack_float cbor_unpack_single
#  define CBOR_ADDL_FLOAT_SUFLOAT CBOR_ADDL_FLOAT_FLOAT32
#else
#  define cbor_pack_float   cbor_pack_double
#  define cbor_unpack_float cbor_unpack_double
#  define CBOR_ADDL_FLOAT_SUFLOAT CBOR_ADDL_FLOAT_FLOAT64
#endif /* _SU_SINGLE_PRECISION */

#define cbor_pack_freq      cbor_pack_double
#define cbor_unpack_freq    cbor_unpack_double

#define CBOR_MEM_REUSE_SIZE_LIMIT (1 << 20)

enum cbor_major_type {
  CMT_UINT  = 0,
  CMT_NINT  = 1,
  CMT_BYTE  = 2,
  CMT_TEXT  = 3,
  CMT_ARRAY = 4,
  CMT_MAP   = 5,
  CMT_TAG   = 6,
  CMT_FLOAT = 7,
  CMT_INVALID = 8, // enum for invalid value
};

#define CBOR_ADDL_UINT8        24
#define CBOR_ADDL_UINT16       25
#define CBOR_ADDL_UINT32       26
#define CBOR_ADDL_UINT64       27
#define CBOR_ADDL_ARRAY_INDEF  31
#define CBOR_ADDL_MAP_INDEF    31

#define CBOR_ADDL_FLOAT_FLOAT32 26
#define CBOR_ADDL_FLOAT_FLOAT64 27

#define CBOR_ADDL_FLOAT_FALSE   20
#define CBOR_ADDL_FLOAT_TRUE    21
#define CBOR_ADDL_FLOAT_NULL    22
#define CBOR_ADDL_FLOAT_BREAK   31

/*
 * Integer byteorder
 */

#ifndef __cplusplus
/* Too obscene for C++ */

SUINLINE uint64_t
be64_to_cpu_unaligned(const void *in)
{
  const uint8_t *p = in;

  return (((uint64_t) p[0] << 56) |
    ((uint64_t) p[1] << 48) |
    ((uint64_t) p[2] << 40) |
    ((uint64_t) p[3] << 32) |
    ((uint64_t) p[4] << 24) |
    ((uint64_t) p[5] << 16) |
    ((uint64_t) p[6] << 8) |
    ((uint64_t) p[7]));
}

SUINLINE void
cpu64_to_be_unaligned(uint64_t in, void *out)
{
  uint8_t *p = out;

  p[0] = (in >> 56) & 0xff;
  p[1] = (in >> 48) & 0xff;
  p[2] = (in >> 40) & 0xff;
  p[3] = (in >> 32) & 0xff;
  p[4] = (in >> 24) & 0xff;
  p[5] = (in >> 16) & 0xff;
  p[6] = (in >> 8) & 0xff;
  p[7] = in & 0xff;
}

SUINLINE uint64_t
cpu64_to_be(uint64_t in)
{
  uint64_t out;

  cpu64_to_be_unaligned(in, &out);

  return out;
}

SUINLINE uint32_t
be32_to_cpu_unaligned(const void *in)
{
  const uint8_t *p = in;

  return (((uint32_t) p[0] << 24) |
    ((uint32_t) p[1] << 16) |
    ((uint32_t) p[2] << 8) |
    ((uint32_t) p[3]));
}

SUINLINE void
cpu32_to_be_unaligned(uint32_t in, void *out)
{
  uint8_t *p = out;

  p[0] = (in >> 24) & 0xff;
  p[1] = (in >> 16) & 0xff;
  p[2] = (in >> 8) & 0xff;
  p[3] = in & 0xff;
}

SUINLINE uint32_t
cpu32_to_be(uint32_t in)
{
  uint32_t out;

  cpu32_to_be_unaligned(in, &out);

  return out;
}

SUINLINE uint16_t
be16_to_cpu_unaligned(const void *in)
{
  const uint8_t *p = in;

  return (((uint16_t) p[0] << 8) |
    ((uint16_t) p[1]));
}

SUINLINE void
cpu16_to_be_unaligned(uint16_t in, void *out)
{
  uint8_t *p = out;

  p[0] = (in >> 8) & 0xff;
  p[1] = in & 0xff;
}

SUINLINE uint16_t
cpu16_to_be(uint16_t in)
{
  uint16_t out;

  cpu16_to_be_unaligned(in, &out);

  return out;
}

SUINLINE uint8_t
be8_to_cpu_unaligned(const void *in)
{
  return *((const uint8_t *) in);
}

SUINLINE void
cpu8_to_be_unaligned(uint8_t in, void *out)
{
  uint8_t *p = out;

  *p = in;
}

SUINLINE uint8_t
cpu8_to_be(uint8_t in)
{
  return in;
}
#endif /* __cplusplus */

/*
 * On failure, the buffer may contain partially encoded data items.  On
 * success, a fully encoded data item is appended to the buffer.
 */
int cbor_pack_uint(grow_buf_t *buffer, uint64_t v);
int cbor_pack_nint(grow_buf_t *buffer, uint64_t v);
int cbor_pack_int(grow_buf_t *buffer, int64_t v);
int cbor_pack_blob(grow_buf_t *buffer, const void *data, size_t size);
void *cbor_alloc_blob(grow_buf_t *buffer, size_t size);
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

int cbor_peek_type(
    grow_buf_t *buffer,
    enum cbor_major_type *type,
    uint8_t *extra);
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


#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* _UTIL_CBOR_H */
