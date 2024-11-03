/*

  Copyright (C) 2020 Gonzalo José Carracedo Carballal

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
SU_CONSTRUCTOR(hashlist)
{
  SUBOOL ok = SU_FALSE;

  memset(self, 0, sizeof(hashlist_t));

  SU_MAKE(self->rbtree, rbtree);

  rbtree_set_dtor(self->rbtree, hashlist_entry_dtor, self);

  ok = SU_TRUE;

done:
  if (!ok)
    SU_DESTRUCT(hashlist, self);
  
  return ok;
}

SU_INSTANCER(hashlist)
{
  hashlist_t *new = NULL;

  SU_ALLOCATE_FAIL(new, hashlist_t);
  SU_CONSTRUCT_FAIL(hashlist, new);

  return new;

fail:
  free(new);

  return NULL;
}

SU_GETTER(hashlist, hashlist_iterator_t, begin)
{
  hashlist_iterator_t it = {NULL, NULL, NULL, NULL};
  struct rbtree_node *first = rbtree_get_first(self->rbtree);
  struct rbtree_node *curr = first;
  struct hashlist_entry *entry;

  while (curr != NULL && curr->data == NULL)
    curr = rbtree_node_next(curr);

  if (curr != NULL) {
    entry = curr->data;

    it.node  = curr;
    it.entry = entry;
    
    it.name  = entry->key;
    it.value = entry->value;
  }
  
  return it;
}

SU_METHOD(hashlist_iterator, void, advance)
{
  hashlist_iterator_t *it = self;

  if (it->node != NULL) {
    struct rbtree_node *curr     = it->node;
    struct hashlist_entry *entry = it->entry;

    entry = entry->next;

    if (entry == NULL) {
      while ((curr = rbtree_node_next(curr)) != NULL && curr->data == NULL);

      if (curr == NULL || curr->data == NULL) {
        /* End of the road. Sorry. */
        it->node  = NULL;
        it->entry = NULL;
      } else {
        entry     = curr->data;
        it->node  = curr;
        it->entry = entry;
      }
    }

    if (entry != NULL) {
      it->name = entry->key;
      it->value = entry->value;
    } else {
      it->name  = NULL;
      it->value = NULL;
    }
  } 
}

SU_GETTER(hashlist_iterator, SUBOOL, end)
{
  return self->node == NULL;
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

SU_GETTER(hashlist, size_t, size)
{
  return self->size;
}

SU_METHOD(hashlist, SUBOOL, set, const char *key, void *val)
{
  uint64_t hash = murmur_hash_64(key, strlen(key), HASHLIST_SEED);
  struct hashlist_entry *list = NULL, *entry, *new = NULL;

  if ((list = hashlist_find_entry_list(self, hash)) != NULL) {
    if ((entry = hashlist_entry_find(list, key)) != NULL) {
      if (self->dtor != NULL)
        (self->dtor) (key, entry->value, self->userdata);
      if (val != NULL)
        ++self->size;
      if (entry->value != NULL)
        --self->size;
      entry->value = val;
      return SU_TRUE;
    }
  }

  /* Not found. Create object */
  SU_TRY_FAIL(new = hashlist_entry_new(key, val));

  if (list != NULL) {
    new->next = list->next;
    list->next = new;
  } else {
    SU_TRYC_FAIL(rbtree_insert(self->rbtree, (int64_t) hash, new));
  }

  /* Count only the non-null entries */
  if (val != NULL)
    ++self->size;
  
  return SU_TRUE;

fail:
  if (new != NULL)
    hashlist_entry_destroy(new);

  return SU_FALSE;
}

SU_METHOD(hashlist, void,   set_userdata, void *userdata)
{
  self->userdata = userdata;
}

SU_METHOD(hashlist, void, set_dtor, void (*dtor) (const char *, void *, void *))
{
  self->dtor = dtor;
  rbtree_set_dtor(self->rbtree, hashlist_entry_dtor, self);
}

SU_METHOD(hashlist, void, clear)
{
  rbtree_clear(self->rbtree);
  self->size = 0;
}

SU_GETTER(hashlist, SUBOOL, contains, const char *key)
{
  uint64_t hash = murmur_hash_64(key, strlen(key), HASHLIST_SEED);
  const struct hashlist_entry *list = hashlist_find_entry_list(self, hash);

  if (list == NULL)
    return SU_FALSE;

  return hashlist_entry_find(list, key) != NULL;
}

SU_GETTER(hashlist, void *, get, const char *key)
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

SU_DESTRUCTOR(hashlist)
{
  if (self->rbtree != NULL) {
    rbtree_destroy(self->rbtree);
    self->rbtree = NULL;
  }
}

SU_COLLECTOR(hashlist)
{
  SU_DESTRUCT(hashlist, self);
  free(self);
}

