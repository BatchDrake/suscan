/*

  Copyright (C) 2020 Gonzalo José Carracedo Carballal

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation, either version 3 of the
  License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this program.  If not, see
  <http://www.gnu.org/licenses/>

*/

#define SU_LOG_DOMAIN "hashlist"

#include <sigutils/log.h>
#include <string.h>
#include <stdint.h>
#include "hashlist.h"

#define HASHLIST_SEED 0xdeadcefe00b00110ull

struct hashlist_entry {
  char *key;
  void *value;
  struct hashlist_entry *next;
};

SUPRIVATE void
hashlist_entry_destroy(struct hashlist_entry *self)
{
  if (self->key != NULL)
    free(self->key);

  free(self);
}

SUPRIVATE struct hashlist_entry *
hashlist_entry_new(const char *key, void *value)
{
  struct hashlist_entry *new = NULL;

  SU_TRYCATCH(
      new = calloc(1, sizeof(struct hashlist_entry)),
      goto fail);

  SU_TRYCATCH(new->key = strdup(key), goto fail);
  new->value = value;

  return new;

fail:
  if (new != NULL)
    hashlist_entry_destroy(new);

  return NULL;
}

SUPRIVATE struct hashlist_entry *
hashlist_entry_find(const struct hashlist_entry *self, const char *key)
{
  const struct hashlist_entry *entry = self;

  while (entry != NULL) {
    if (strcmp(entry->key, key) == 0)
      return (struct hashlist_entry *) entry;

    entry = entry->next;
  }

  return NULL;
}

SUPRIVATE void
hashlist_entry_dtor(void *data, void *private)
{
  struct hashlist_entry *this, *next;
  hashlist_t *owner = (hashlist_t *) private;

  this = (struct hashlist_entry *) data;

  while (this != NULL) {
    next = this->next;

    if (owner->dtor != NULL)
      (owner->dtor) (this->key, this->value, owner->userdata);

    hashlist_entry_destroy(this);

    this = next;
  }
}

/***************************** Hashlist object ********************************/
hashlist_t *
hashlist_new(void)
{
  hashlist_t *new = NULL;

  SU_TRYCATCH(new = calloc(1, sizeof(hashlist_t)), goto fail);

  SU_TRYCATCH(new->rbtree = rbtree_new(), goto fail);

  rbtree_set_dtor(new->rbtree, hashlist_entry_dtor, new);

  return new;

fail:
  if (new != NULL)
    hashlist_destroy(new);

  return NULL;
}

/* https://stackoverflow.com/questions/40440762/is-it-possible-for-murmurhash3-to-produce-a-64-bit-hash-where-the-upper-32-bits*/

uint64_t
murmur_hash_64(const void *key, int len, uint64_t seed)
{
    const uint64_t m = 0xc6a4a7935bd1e995ull;
    const int r = 47;
    const unsigned char *data2;
    uint64_t h = seed ^ (len * m);

    const uint64_t *data = (const uint64_t *) key;
    const uint64_t *end = data + (len / 8);

    while (data != end) {
#ifdef PLATFORM_BIG_ENDIAN
        uint64 k = *data++;
        char *p = (char *)&k;
        char c;
        c = p[0]; p[0] = p[7]; p[7] = c;
        c = p[1]; p[1] = p[6]; p[6] = c;
        c = p[2]; p[2] = p[5]; p[5] = c;
        c = p[3]; p[3] = p[4]; p[4] = c;
#else
        uint64_t k = *data++;
#endif

        k *= m;
        k ^= k >> r;
        k *= m;

        h ^= k;
        h *= m;
    }

    data2 = (const unsigned char *) data;

    switch (len & 7) {
      case 7:
        h ^= ((uint64_t) data2[6]) << 48;

      case 6:
        h ^= ((uint64_t) data2[5]) << 40;

      case 5:
        h ^= ((uint64_t) data2[4]) << 32;

      case 4:
        h ^= ((uint64_t) data2[3]) << 24;

      case 3:
        h ^= ((uint64_t) data2[2]) << 16;

      case 2:
        h ^= ((uint64_t) data2[1]) << 8;

      case 1:
        h ^= ((uint64_t) data2[0]);
        h *= m;
    };

    h ^= h >> r;
    h *= m;
    h ^= h >> r;

    return h;
}

SUPRIVATE struct hashlist_entry *
hashlist_find_entry_list(const hashlist_t *self, uint64_t hash)
{
  struct rbtree_node *node;

  if ((node = rbtree_search(self->rbtree, (int64_t) hash, RB_EXACT)) == NULL)
    return NULL;

  return rbtree_node_data(node);
}

SUBOOL
hashlist_set(hashlist_t *self, const char *key, void *val)
{
  uint64_t hash = murmur_hash_64(key, strlen(key), HASHLIST_SEED);
  struct hashlist_entry *list = NULL, *entry, *new = NULL;

  if ((list = hashlist_find_entry_list(self, hash)) != NULL) {
    if ((entry = hashlist_entry_find(list, key)) != NULL) {
      if (self->dtor != NULL)
        (self->dtor) (key, entry->value, self->userdata);
      entry->value = val;
      return SU_TRUE;
    }
  }

  /* Not found. Create object */
  SU_TRYCATCH(new = hashlist_entry_new(key, val), goto fail);

  if (list != NULL) {
    new->next = list->next;
    list->next = new;
  } else {
    SU_TRYCATCH(
        rbtree_insert(self->rbtree, (int64_t) hash, new) != -1,
        goto fail);
  }

  return SU_TRUE;

fail:
  if (new != NULL)
    hashlist_entry_destroy(new);

  return SU_FALSE;
}

void
hashlist_set_userdata(hashlist_t *self, void *userdata)
{
  self->userdata = userdata;
}

void
hashlist_set_dtor(hashlist_t *self, void (*dtor) (const char *, void *, void *))
{
  self->dtor = dtor;
}

SUBOOL
hashlist_contains(const hashlist_t *self, const char *key)
{
  uint64_t hash = murmur_hash_64(key, strlen(key), HASHLIST_SEED);
  const struct hashlist_entry *list = hashlist_find_entry_list(self, hash);

  if (list == NULL)
    return SU_FALSE;

  return hashlist_entry_find(list, key) != NULL;
}

void *
hashlist_get(const hashlist_t *self, const char *key)
{
  uint64_t hash = murmur_hash_64(key, strlen(key), HASHLIST_SEED);
  const struct hashlist_entry *list = hashlist_find_entry_list(self, hash);
  const struct hashlist_entry *entry = NULL;

  if (list == NULL)
    return NULL;

  if ((entry = hashlist_entry_find(list, key)) == NULL)
    return NULL;

  return entry->value;
}

void
hashlist_destroy(hashlist_t *self)
{
  if (self->rbtree != NULL)
    rbtree_destroy(self->rbtree);

  free(self);
}
