/*
 * Copyright (c) 2017-2020 Josef 'Jeff' Sipek <jeffpc@josefsipek.net>
 * Copyright (c) 2020 Gonzalo J. Carracedo <BatchDrake@gmail.com>
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

#include <sigutils/types.h>
#include <stdint.h>
#include <string.h>
#include "cbor.h"


enum cbor_major_type {
  CMT_UINT  = 0,
  CMT_NINT  = 1,
  CMT_BYTE  = 2,
  CMT_TEXT  = 3,
  CMT_ARRAY = 4,
  CMT_MAP   = 5,
  CMT_TAG   = 6,
  CMT_FLOAT = 7,
};

#define ADDL_UINT8    24
#define ADDL_UINT16    25
#define ADDL_UINT32    26
#define ADDL_UINT64    27
#define ADDL_ARRAY_INDEF  31
#define ADDL_MAP_INDEF    31
#define ADDL_FLOAT_FALSE  20
#define ADDL_FLOAT_TRUE    21
#define ADDL_FLOAT_NULL    22
#define ADDL_FLOAT_BREAK  31

#define MKTYPE(type, additional)    \
    ({        \
      (type << 5) | additional;      \
    })

#define MKTYPE_STATIC(type, additional)    \
    ({        \
      (type << 5) | additional;      \
    })


/*
 * Integer byteorder
 */

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
  cpu64_to_be_unaligned(in, &in);

  return in;
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
  cpu32_to_be_unaligned(in, &in);

  return in;
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
  cpu16_to_be_unaligned(in, &in);

  return in;
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

/*
 * pack
 */

SUPRIVATE int
pack_cbor_type_byte(grow_buf_t *buffer, enum cbor_major_type type,
       uint8_t additional)
{
  uint8_t byte;

  byte = MKTYPE(type, additional);

  return grow_buf_append(buffer, &byte, 1);
}

/* pack a type byte with specified additional information */
SUINLINE int
pack_cbor_type(
    grow_buf_t *buffer,
    enum cbor_major_type type,
    uint64_t additional)
{
  union {
    uint64_t u64;
    uint32_t u32;
    uint16_t u16;
    uint8_t u8;
  } u;
  uint8_t addl;
  size_t size;
  int ret;

  if (additional <= 23)
    return pack_cbor_type_byte(buffer, type, additional);

  if (additional <= 0xff) {
    size = sizeof(uint8_t);
    addl = ADDL_UINT8;
    u.u8 = cpu8_to_be(additional);
  } else if (additional <= 0xffff) {
    size = sizeof(uint16_t);
    addl = ADDL_UINT16;
    u.u16 = cpu16_to_be(additional);
  } else if (additional <= 0xffffffff) {
    size = sizeof(uint32_t);
    addl = ADDL_UINT32;
    u.u32 = cpu32_to_be(additional);
  } else {
    size = sizeof(uint64_t);
    addl = ADDL_UINT64;
    u.u64 = cpu64_to_be(additional);
  }

  if ((ret = pack_cbor_type_byte(buffer, type, addl)))
    return ret;

  return grow_buf_append(buffer, &u, size);
}

int
cbor_pack_uint(grow_buf_t *buffer, uint64_t v)
{
  return pack_cbor_type(buffer, CMT_UINT, v);
}

int
cbor_pack_nint(grow_buf_t *buffer, uint64_t v)
{
  return pack_cbor_type(buffer, CMT_NINT, v);
}

int
cbor_pack_int(grow_buf_t *buffer, int64_t v)
{
  if (v < 0)
    return cbor_pack_nint(buffer, -v);

  return cbor_pack_uint(buffer, v);
}

int
cbor_pack_blob(grow_buf_t *buffer, const void *data,
       size_t size)
{
  int ret;

  if ((ret = pack_cbor_type(buffer, CMT_BYTE, size)))
    return ret;

  return grow_buf_append(buffer, data, size);
}

int
cbor_pack_cstr_len(grow_buf_t *buffer, const char *str, size_t len)
{
  int ret;

  if ((ret = pack_cbor_type(buffer, CMT_TEXT, len)))
    return ret;

  return grow_buf_append(buffer, str, len);
}

int
cbor_pack_str(grow_buf_t *buffer, const char *str)
{
  return cbor_pack_cstr_len(buffer, str, strlen(str));
}

int
cbor_pack_bool(grow_buf_t *buffer, SUBOOL b)
{
  /* bools use the float major type */
  return pack_cbor_type(buffer, CMT_FLOAT,
      b ? ADDL_FLOAT_TRUE : ADDL_FLOAT_FALSE);
}

int
cbor_pack_null(grow_buf_t *buffer)
{
  /* null uses the float major type */
  return pack_cbor_type(buffer, CMT_FLOAT, ADDL_FLOAT_NULL);
}

int
cbor_pack_break(grow_buf_t *buffer)
{
  SUPRIVATE const uint8_t byte = MKTYPE_STATIC(CMT_FLOAT, ADDL_FLOAT_BREAK);

  return grow_buf_append(buffer, &byte, 1);
}

int
cbor_pack_array_start(grow_buf_t *buffer, size_t nelem)
{
  SUPRIVATE const uint8_t byte = MKTYPE_STATIC(CMT_ARRAY, ADDL_ARRAY_INDEF);

  if (nelem != CBOR_UNKNOWN_NELEM)
    return pack_cbor_type(buffer, CMT_ARRAY, nelem);

  /* indefinite-length array */
  return grow_buf_append(buffer, &byte, 1);
}

int
cbor_pack_array_end(grow_buf_t *buffer, size_t nelem)
{
  if (nelem != CBOR_UNKNOWN_NELEM)
    return 0;

  /* indefinite-length array */
  return cbor_pack_break(buffer);
}

int
cbor_pack_map_start(grow_buf_t *buffer, size_t npairs)
{
  if (npairs == CBOR_UNKNOWN_NELEM) {
    /* indefinite-length map */
    SUPRIVATE const uint8_t byte = MKTYPE_STATIC(CMT_MAP,
    ADDL_MAP_INDEF);

    return grow_buf_append(buffer, &byte, 1);
  } else {
    /* definite-length map */
    return pack_cbor_type(buffer, CMT_MAP, npairs);
  }
}

int
cbor_pack_map_end(grow_buf_t *buffer, size_t npairs)
{
  if (npairs == CBOR_UNKNOWN_NELEM) {
    /* indefinite-length map */
    return cbor_pack_break(buffer);
  } else {
    /* definite-length map */
    return 0;
  }
}

/*
 * unpack
 */

SUPRIVATE int
read_cbor_type(grow_buf_t *buffer, enum cbor_major_type *type,
  uint8_t *extra)
{
  uint8_t byte;
  ssize_t ret;

  ret = grow_buf_read(buffer, &byte, 1);
  if (ret < 1)
    return ret ? ret : -EINVAL;

  *type = byte >> 5;
  *extra = byte & 0x1f;

  return 0;
}

SUPRIVATE int
get_addl_bytes(grow_buf_t *buffer, uint8_t extra, uint64_t *out)
{
  const void *ptr;
  ssize_t ret;
  size_t size;

  switch (extra) {
    case ADDL_UINT8:
      size = 1;
      break;
    case ADDL_UINT16:
      size = 2;
      break;
    case ADDL_UINT32:
      size = 4;
      break;
    case ADDL_UINT64:
      size = 8;
      break;
    default:
      if (extra > 23)
  return -EINVAL;

      size = 0;
      break;
  }

  if (grow_buf_avail(buffer) < size)
    return -EINVAL;

  ptr = grow_buf_current_data(buffer);

  switch (size) {
    case 0:
      *out = extra;
      break;
    case 1:
      *out = be8_to_cpu_unaligned(ptr);
      break;
    case 2:
      *out = be16_to_cpu_unaligned(ptr);
      break;
    case 4:
      *out = be32_to_cpu_unaligned(ptr);
      break;
    case 8:
      *out = be64_to_cpu_unaligned(ptr);
      break;
  }

  ret = grow_buf_seek(buffer, size, SEEK_CUR);

  return (ret < 0) ? ret : 0;
}

SUPRIVATE int
unpack_cbor_int(
    grow_buf_t *buffer,
    enum cbor_major_type expected_type,
   uint64_t *out)
{
  enum cbor_major_type type;
  uint8_t extra;
  int ret;

  ret = read_cbor_type(buffer, &type, &extra);
  if (ret)
    return ret;

  if (expected_type != type)
    return -EILSEQ;

  return get_addl_bytes(buffer, extra, out);
}

/* NOTE: the FLOAT major type is used for a *lot* of different things */
SUPRIVATE int
unpack_cbor_float(grow_buf_t *buffer, uint8_t *extra)
{
  enum cbor_major_type type;
  int ret;

  ret = read_cbor_type(buffer, &type, extra);
  if (ret)
    return ret;

  if (type != CMT_FLOAT)
    return -EILSEQ;

  switch (*extra) {
    case ADDL_FLOAT_FALSE:
    case ADDL_FLOAT_TRUE:
    case ADDL_FLOAT_NULL:
    case ADDL_FLOAT_BREAK:
      return 0;
  }

  return -EILSEQ;
}

SUPRIVATE int
unpack_cbor_arraymap_start(grow_buf_t *buffer,
        enum cbor_major_type exp_type, uint8_t indef,
        uint64_t *len, SUBOOL *end_required)
{
  enum cbor_major_type type;
  uint8_t extra;
  int ret;

  ret = read_cbor_type(buffer, &type, &extra);
  if (ret)
    return ret;

  if (type != exp_type)
    return -EILSEQ;

  if (extra == indef) {
    *end_required = SU_TRUE;
    *len = 0;
    return 0;
  } else {
    *end_required = SU_FALSE;
    return get_addl_bytes(buffer, extra, len);
  }
}

SUPRIVATE int
sync_buffers(grow_buf_t *orig, grow_buf_t *tmp)
{
  ssize_t ret;

  ret = grow_buf_seek(orig, grow_buf_ptr(tmp), SEEK_CUR);

  return (ret < 0) ? ret : 0;
}

int
cbor_unpack_uint(grow_buf_t *buffer, uint64_t *v)
{
  grow_buf_t tmp;
  int ret;

  grow_buf_init_loan(
      &tmp,
      grow_buf_current_data(buffer),
      grow_buf_avail(buffer),
      grow_buf_avail(buffer));

  ret = unpack_cbor_int(&tmp, CMT_UINT, v);
  if (ret)
    return ret;

  return sync_buffers(buffer, &tmp);
}

int
cbor_unpack_nint(grow_buf_t *buffer, uint64_t *v)
{
  grow_buf_t tmp;
  int ret;

  grow_buf_init_loan(
      &tmp,
      grow_buf_current_data(buffer),
      grow_buf_avail(buffer),
      grow_buf_avail(buffer));

  ret = unpack_cbor_int(&tmp, CMT_NINT, v);
  if (ret)
    return ret;

  return sync_buffers(buffer, &tmp);
}

int
cbor_unpack_int(grow_buf_t *buffer, int64_t *v)
{
  grow_buf_t tmp;
  uint64_t tmpv;
  int ret;

  /*
   * First, try unsigned ints
   */

  grow_buf_init_loan(
      &tmp,
      grow_buf_current_data(buffer),
      grow_buf_avail(buffer),
      grow_buf_avail(buffer));

  ret = cbor_unpack_uint(&tmp, &tmpv);
  if (!ret) {
    if (tmpv > INT64_MAX)
      return -EOVERFLOW;

    *v = tmpv;

    return sync_buffers(buffer, &tmp);
  }

  /*
   * Second, try negative ints
   */

  grow_buf_init_loan(
      &tmp,
      grow_buf_current_data(buffer),
      grow_buf_avail(buffer),
      grow_buf_avail(buffer));

  ret = cbor_unpack_nint(&tmp, &tmpv);
  if (!ret) {
    /* 2's complement has one extra negative number */
    if (tmpv > (((uint64_t) INT64_MAX) + 1))
      return -EOVERFLOW;

    *v = ((~tmpv) + 1);

    return sync_buffers(buffer, &tmp);
  }

  return ret;
}

int
cbor_unpack_blob(grow_buf_t *buffer, void **data, size_t *size)
{
  return -ENOTSUP;
}

int
cbor_unpack_cstr_len(grow_buf_t *buffer, char **str, size_t *len)
{
  uint64_t parsed_len;
  grow_buf_t tmp;
  ssize_t ret;
  char *out;

  grow_buf_init_loan(
      &tmp,
      grow_buf_current_data(buffer),
      grow_buf_avail(buffer),
      grow_buf_avail(buffer));

  ret = unpack_cbor_int(&tmp, CMT_TEXT, &parsed_len);
  if (ret)
    return ret;

  /*
   * We can't handle strings longer than what fits in memory (the +1
   * is for nul termination).
   */
  if (parsed_len >= SIZE_MAX)
    return -EOVERFLOW;

  out = malloc(parsed_len + 1);
  if (!out)
    return -ENOMEM;

  ret = grow_buf_read(&tmp, out, parsed_len);
  if (ret < 0)
    goto err;

  /* must read exactly the number of bytes */
  if (ret != parsed_len) {
    ret = -EILSEQ;
    goto err;
  }

  out[parsed_len] = '\0';

  *len = parsed_len;
  *str = out;

  return sync_buffers(buffer, &tmp);

err:
  free(out);

  return ret;
}

int
cbor_unpack_str(grow_buf_t *buffer, char **str)
{
  char *s;
  size_t len;
  int ret;

  ret = cbor_unpack_cstr_len(buffer, &s, &len);
  if (ret)
    return ret;

  *str = s;

  return 0;
}

int
cbor_unpack_bool(grow_buf_t *buffer, SUBOOL *b)
{
  grow_buf_t tmp;
  uint8_t extra;
  int ret;

  grow_buf_init_loan(
      &tmp,
      grow_buf_current_data(buffer),
      grow_buf_avail(buffer),
      grow_buf_avail(buffer));

  ret = unpack_cbor_float(&tmp, &extra);
  if (ret)
    return ret;

  switch (extra) {
    case ADDL_FLOAT_FALSE:
      *b = SU_FALSE;
      break;
    case ADDL_FLOAT_TRUE:
      *b = SU_TRUE;
      break;
    default:
      return -EILSEQ;
  }

  return sync_buffers(buffer, &tmp);
}

int
cbor_unpack_null(grow_buf_t *buffer)
{
  grow_buf_t tmp;
  uint8_t extra;
  int ret;

  grow_buf_init_loan(
      &tmp,
      grow_buf_current_data(buffer),
      grow_buf_avail(buffer),
      grow_buf_avail(buffer));

  ret = unpack_cbor_float(&tmp, &extra);
  if (ret)
    return ret;

  switch (extra) {
    case ADDL_FLOAT_NULL:
      break;
    default:
      return -EILSEQ;
  }

  return sync_buffers(buffer, &tmp);
}

int
cbor_unpack_break(grow_buf_t *buffer)
{
  grow_buf_t tmp;
  uint8_t extra;
  int ret;

  grow_buf_init_loan(
      &tmp,
      grow_buf_current_data(buffer),
      grow_buf_avail(buffer),
      grow_buf_avail(buffer));

  ret = unpack_cbor_float(&tmp, &extra);
  if (ret)
    return ret;

  switch (extra) {
    case ADDL_FLOAT_BREAK:
      break;
    default:
      return -EILSEQ;
  }

  return sync_buffers(buffer, &tmp);
}

int
cbor_unpack_map_start(grow_buf_t *buffer, uint64_t *npairs,
  SUBOOL *end_required)
{
  grow_buf_t tmp;
  int ret;

  grow_buf_init_loan(
      &tmp,
      grow_buf_current_data(buffer),
      grow_buf_avail(buffer),
      grow_buf_avail(buffer));

  ret = unpack_cbor_arraymap_start(&tmp, CMT_MAP, ADDL_MAP_INDEF,
     npairs, end_required);
  if (ret)
    return ret;

  return sync_buffers(buffer, &tmp);
}

int
cbor_unpack_map_end(grow_buf_t *buffer, SUBOOL end_required)
{
  if (!end_required)
    return 0;

  return cbor_unpack_break(buffer);
}

int
cbor_unpack_array_start(grow_buf_t *buffer, uint64_t *nelem,
    SUBOOL *end_required)
{
  grow_buf_t tmp;
  int ret;

  grow_buf_init_loan(
      &tmp,
      grow_buf_current_data(buffer),
      grow_buf_avail(buffer),
      grow_buf_avail(buffer));

  ret = unpack_cbor_arraymap_start(&tmp, CMT_ARRAY, ADDL_ARRAY_INDEF,
     nelem, end_required);
  if (ret)
    return ret;

  return sync_buffers(buffer, &tmp);
}

int
cbor_unpack_array_end(grow_buf_t *buffer, SUBOOL end_required)
{
  if (!end_required)
    return 0;

  return cbor_unpack_break(buffer);
}
