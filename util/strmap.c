/*

  Copyright (C) 2024 Gonzalo José Carracedo Carballal

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

#define SU_LOG_DOMAIN "strmap"

#include "strmap.h"
#include <sigutils/log.h>
#include <stdarg.h>

SUPRIVATE void
strmap_dtor(const char *key, void *value, void *userdata)
{
  if (value != NULL)
    free(value);
}

SU_CONSTRUCTOR(strmap)
{
  SUBOOL ok;

  if ((ok = hashlist_init(self)))
    hashlist_set_dtor(self, strmap_dtor);

  return ok;
}

SU_INSTANCER(strmap)
{
  strmap_t *map = NULL;

  SU_MAKE(map, hashlist);

  hashlist_set_dtor(map, strmap_dtor);

done:
  return map;
}

SUSCAN_TYPE_SERIALIZER_PROTO(strmap)
{
  SUSCAN_PACK_BOILERPLATE_START;
  strmap_iterator_t it;

  SUSCAN_PACK(uint, strmap_size(self));

  it = strmap_begin(self);

  while (!strmap_iterator_end(&it)) {
    if (it.value != NULL) {
      SUSCAN_PACK(str, it.name);
      SUSCAN_PACK(str, it.value);
    }

    strmap_iterator_advance(&it);
  }
  
  SUSCAN_PACK_BOILERPLATE_END;
}

SUSCAN_TYPE_DESERIALIZER_PROTO(strmap)
{
  SUSCAN_UNPACK_BOILERPLATE_START;
  uint32_t i, size;
  char *key = NULL;
  char *value = NULL;

  strmap_clear(self);

  SUSCAN_UNPACK(uint32, size);
  for (i = 0; i < size; ++i) {
    SUSCAN_UNPACK(str, key);
    SUSCAN_UNPACK(str, value);

    SU_TRY_FAIL(strmap_set(self, key, value));

    free(key);
    free(value);
    key = value = NULL;
  }

  SUSCAN_UNPACK_BOILERPLATE_FINALLY;

  if (key != NULL)
    free(key);

  if (value != NULL)
    free(value);
  
  SUSCAN_UNPACK_BOILERPLATE_RETURN;
}

SU_METHOD(strmap, SUBOOL, set, const char *key, const char *val)
{
  SUBOOL ok = SU_FALSE;
  char *dup = NULL;

  SU_TRY(dup = strdup(val));
  SU_TRY(hashlist_set(self, key, dup));

  dup = NULL;

  ok = SU_TRUE;

done:
  if (dup != NULL)
    free(dup);
  
  return ok;
}

SU_METHOD(strmap, SUBOOL, set_int, const char *key, int val)
{
  char buffer[12]; /* -2 XXX XXX XXX */

  snprintf(buffer, sizeof(buffer), "%d", val);
  return strmap_set(self, key, buffer);
}

SU_METHOD(strmap, SUBOOL, set_uint, const char *key, unsigned int val)
{
  char buffer[12]; /* -2 XXX XXX XXX */

  snprintf(buffer, sizeof(buffer), "%u", val);
  return strmap_set(self, key, buffer);
}

SU_METHOD(strmap, SUBOOL, set_asprintf, const char *key, const char *fmt, ...)
{
  va_list ap;
  char *buffer = NULL;
  SUBOOL ok = SU_FALSE;

  va_start(ap, fmt);

  SU_TRY(buffer = vstrbuild(fmt, ap));
  SU_TRY(strmap_set(self, key, buffer));

  ok = SU_TRUE;

done:
  if (buffer != NULL)
    free(buffer);
  
  va_end(ap);

  return ok;
}

SU_METHOD(strmap, void, clear)
{
  hashlist_clear(self);
}

SU_METHOD(strmap, void, notify_move)
{
  hashlist_set_dtor(self, strmap_dtor);
}

SU_METHOD(strmap, SUBOOL, copy, const strmap_t *existing)
{
  strmap_t *new = NULL;
  SUBOOL ok = SU_FALSE;

  SU_MAKE(new, strmap);

  SU_TRY(strmap_assign(new, existing));

  SU_DESTRUCT(strmap, self);
  memcpy(self, new, sizeof(strmap_t));
  
  strmap_notify_move(self);
  
  free(new);
  new = NULL;

  ok = SU_TRUE;

done:
  if (new != NULL)
    SU_DISPOSE(strmap, new);
  
  return ok;
}

SU_METHOD(strmap, SUBOOL, assign, const strmap_t *existing)
{
  hashlist_iterator_t it;
  SUBOOL ok = SU_FALSE;

  it = hashlist_begin(existing);

  while (!hashlist_iterator_end(&it)) {
    SU_TRY(strmap_set(self, it.name, it.value));
    hashlist_iterator_advance(&it);
  }
  
  ok = SU_TRUE;

done:
  return ok;
}

SU_GETTER(strmap, const char *, get, const char *key)
{
  return hashlist_get(self, key);
}

SU_GETTER(strmap, const char *, get_default, const char *key, const char *dfl)
{
  const char *result = hashlist_get(self, key);

  if (result == NULL)
    result = dfl;

  return result;
}

SUPRIVATE int
strmap_keycmp(const void *a, const void *b)
{
  const char *a_s = a;
  const char *b_s = b;

  return strcmp(a_s, b_s);
}

SU_GETTER(strmap, char **, keys)
{
  PTR_LIST_LOCAL(char, key);

  strmap_iterator_t it;

  it = strmap_begin(self);

  while (!strmap_iterator_end(&it)) {
    SU_TRYC_FAIL(PTR_LIST_APPEND_CHECK(key, it.name));
    strmap_iterator_advance(&it);
  }

  qsort(key_list, key_count, sizeof(char *), strmap_keycmp);
  
  SU_TRYC_FAIL(PTR_LIST_APPEND_CHECK(key, NULL));

  return key_list;

fail:
  if (key_list != NULL)
    free(key_list);

  return NULL;
}

SU_GETTER(strmap, SUBOOL, equals, const strmap_t *map)
{
  strmap_iterator_t it;

  it = strmap_begin(self);
  while (!strmap_iterator_end(&it)) {
    const char *val   = it.value;
    const char *other = strmap_get(map, it.name);
    if (other == NULL || strcmp(val, other) != 0)
      return SU_FALSE;
    
    strmap_iterator_advance(&it);
  }

  it = strmap_begin(map);
  while (!strmap_iterator_end(&it)) {
    const char *val   = it.value;
    const char *other = strmap_get(self, it.name);
    if (other == NULL || strcmp(val, other) != 0)
      return SU_FALSE;
    
    strmap_iterator_advance(&it);
  }

  return SU_TRUE;
}

SU_COLLECTOR(strmap)
{
  SU_DISPOSE(hashlist, self);
}

SU_DESTRUCTOR(strmap)
{
  SU_DESTRUCT(hashlist, self);
}
