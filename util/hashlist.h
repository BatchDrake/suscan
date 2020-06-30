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

#ifndef _UTIL_HASHLIST_H
#define _UTIL_HASHLIST_H

#include "rbtree.h"
#include <sigutils/types.h>

struct hashlist {
  rbtree_t *rbtree;
  void *userdata;
  void (*dtor) (const char *key, void *value, void *userdata);
};

typedef struct hashlist hashlist_t;

hashlist_t *hashlist_new(void);
SUBOOL hashlist_set(hashlist_t *, const char *, void *);
void hashlist_set_userdata(hashlist_t *, void *);
void hashlist_set_dtor(hashlist_t *, void (*) (const char *, void *, void *));

SUBOOL hashlist_contains(const hashlist_t *, const char *);
void  *hashlist_get(const hashlist_t *, const char *);
SUBOOL hashlist_remove(hashlist_t *);
void hashlist_destroy(hashlist_t *);

#endif /* _UTIL_HASHLIST_H */
