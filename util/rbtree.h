/*
  
  Copyright (C) 2014 Gonzalo José Carracedo Carballal
  
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

#ifndef _UTIL_RBTREE_H
#define _UTIL_RBTREE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus*/

enum rbtree_node_search_mode
{
  RB_LEFTWARDS = -1,
  RB_EXACT = 0,
  RB_RIGHTWARDS = 1
};

enum rbtree_node_color
{
  RB_RED,
  RB_BLACK
};

typedef struct rbtree
{
  struct rbtree_node *root;
  struct rbtree_node *first, *last;

  int64_t                      cached_key;
  struct rbtree_node          *cached_node;
  enum rbtree_node_search_mode cached_mode;
  
  void *node_dtor_data;
  
  void (*node_dtor) (void *, void *);
}
rbtree_t;

struct rbtree_node
{
  enum rbtree_node_color color;
  int64_t key;

  rbtree_t *owner;
  
  /* General tree structure */
  struct rbtree_node *parent, *left, *right;

  /* List view, to quickly iterate among nodes */
  struct rbtree_node *prev, *next;

  void *data;
};

static inline void
rbtree_invalidate_cache (rbtree_t *tree)
{
  tree->cached_node = NULL;
}


static inline struct rbtree_node *
rbtree_get_first (rbtree_t *tree)
{
  return tree->first;
}

static inline struct rbtree_node *
rbtree_get_last (rbtree_t *tree)
{
  return tree->last;
}

static inline struct rbtree_node *
rbtree_node_next (struct rbtree_node *node)
{
  return node->next;
}

static inline struct rbtree_node *
rbtree_node_prev (struct rbtree_node *node)
{
  return node->prev;
}

static inline void *
rbtree_node_data (struct rbtree_node *node)
{
  return node->data;
}

rbtree_t *rbtree_new (void);
void rbtree_node_free_dtor (void *data, void *userdata); /* Convenience */
void rbtree_set_dtor (rbtree_t *, void (*) (void *, void *), void *);
void rbtree_debug (rbtree_t *, FILE *);
int  rbtree_set (rbtree_t *self, int64_t key, void *data);
int  rbtree_insert (rbtree_t *, int64_t, void *);
void rbtree_clear (rbtree_t *);
void rbtree_destroy (rbtree_t *);
struct rbtree_node *rbtree_search (rbtree_t *, int64_t, enum rbtree_node_search_mode);

static inline void *
rbtree_search_data (
  rbtree_t *self,
  int64_t key,
  enum rbtree_node_search_mode mode,
  void *dfl)
{
  struct rbtree_node *node = rbtree_search(self, key, mode);

  if (node == NULL)
    return dfl;

  return node->data;
}

#ifdef __cplusplus
}
#endif /* __cplusplus*/
#endif /* _UTIL_RBTREE_H */
