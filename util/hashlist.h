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

#ifndef _UTIL_HASHLIST_H
#define _UTIL_HASHLIST_H

#include "rbtree.h"
#include <sigutils/defs.h>
#include <sigutils/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define hashlist_free_dtor rbtree_node_free_dtor

struct hashlist_iterator {
  void *node;
  void *entry;
  char *name;
  void *value;
};

typedef struct hashlist_iterator hashlist_iterator_t;

struct hashlist {
  rbtree_t *rbtree;
  size_t    size;
  void     *userdata;
  void (*dtor) (const char *key, void *value, void *userdata);
};

typedef struct hashlist hashlist_t;


SU_INSTANCER(hashlist);
SU_CONSTRUCTOR(hashlist);

SU_COLLECTOR(hashlist);
SU_DESTRUCTOR(hashlist);

uint64_t murmur_hash_64(const void *key, int len, uint64_t seed);

SU_METHOD(hashlist, SUBOOL, set,          const char *, void *);
SU_METHOD(hashlist, void,   set_userdata, void *);
SU_METHOD(hashlist, void,   set_dtor,     void (*) (const char *, void *, void *));
SU_METHOD(hashlist, void,   clear);

SU_GETTER(hashlist, SUBOOL, contains, const char *);
SU_GETTER(hashlist, void *, get,      const char *);
SU_GETTER(hashlist, size_t, size);

SU_GETTER(hashlist, hashlist_iterator_t, begin);
SU_GETTER(hashlist_iterator, SUBOOL, end);
SU_METHOD(hashlist_iterator, void, advance);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _UTIL_HASHLIST_H */
