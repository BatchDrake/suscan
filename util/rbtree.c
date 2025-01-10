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

#include <assert.h>

#include <sigutils/util/util.h>
#include <rbtree.h>

static void rbtree_insert_case_1 (struct rbtree_node *);
static void rbtree_insert_case_2 (struct rbtree_node *);
static void rbtree_insert_case_3 (struct rbtree_node *);
static void rbtree_insert_case_4 (struct rbtree_node *);
static void rbtree_insert_case_5 (struct rbtree_node *);

rbtree_t *
rbtree_new (void)
{
  rbtree_t *new;

  if ((new = calloc (1, sizeof (rbtree_t))) == NULL)
    return NULL;

  return new;
}

void
rbtree_node_free_dtor (void *data, void *userdata)
{
  free(data);
}

void
rbtree_set_dtor (rbtree_t *tree, void (*dtor) (void *, void *), void *node_dtor_data)
{
  tree->node_dtor = dtor;
  tree->node_dtor_data = node_dtor_data;
}

void
rbtree_node_clear (struct rbtree_node *node)
{
  if (node->data != NULL && node->owner != NULL)
    if (node->owner->node_dtor != NULL) {
      (node->owner->node_dtor) (node->data, node->owner->node_dtor_data);
      node->data = NULL;
    }   
}

void
rbtree_node_destroy (struct rbtree_node *node)
{
  rbtree_node_clear (node);

  free (node);
}

void
rbtree_debug_node (struct rbtree_node *node, FILE *fp)
{
  fprintf (fp, "  node_%p [label = \"%s\"];\n", node, (char *) node->data);

  if (node->left)
  {
    fprintf (fp, "  node_%p -> node_%p [color=green];\n", node, node->left);
    rbtree_debug_node (node->left, fp);
  }
  
  if (node->right)
  {
    fprintf (fp, "  node_%p -> node_%p [color=red];\n", node, node->right);
    rbtree_debug_node (node->right, fp);
  }
}

void
rbtree_debug (rbtree_t *tree, FILE *output)
{
  fprintf (output, "digraph G\n{\n");

  if (tree->root != NULL)
    rbtree_debug_node (tree->root, output);
  
  fprintf (output, "}\n");
}

struct rbtree_node *
rbtree_node_grandparent (const struct rbtree_node *node)
{
  if (node == NULL)
    return NULL;
  else if (node->parent == NULL)
    return NULL;
  else
    return node->parent->parent;
}

struct rbtree_node *
rbtree_node_uncle (const struct rbtree_node *node)
{
  struct rbtree_node *grandpa;
  
  if ((grandpa = rbtree_node_grandparent (node)) == NULL)
    return NULL;
  else if (grandpa->left == node->parent)
    return grandpa->right;
  else
    return grandpa->left;
}

struct rbtree_node *
rbtree_node_new (rbtree_t *tree, int64_t key, void *data)
{
  struct rbtree_node *new;

  if ((new = calloc (1, sizeof (struct rbtree_node))) == NULL)
    return NULL;

  new->color = RB_RED;
  new->key   = key;
  new->data  = data;
  new->owner = tree;

  return new;
}

static inline void
rbtree_node_set_color (struct rbtree_node *node, enum rbtree_node_color color)
{
  node->color = color;
}

static inline int
rbtree_node_is_root (const struct rbtree_node *node)
{
  return node->parent == NULL;
}

static inline enum rbtree_node_color
rbtree_node_color (const struct rbtree_node *node)
{
  return node->color;
}

/* Classical insert */
static int
rbtree_insert_node (struct rbtree_node *parent, struct rbtree_node *node)
{
  /* Replacement case */
  if (parent->key == node->key)
  {
    rbtree_node_clear (parent);

    parent->data = node->data;

    return 1;
  }
  else if (node->key < parent->key)
  {
    if (parent->left != NULL)
      return rbtree_insert_node (parent->left, node);
        
    parent->left = node;
    node->parent = parent;

    node->prev = parent->prev;
    node->next = parent;

    if (node->prev != NULL)
      node->prev->next = node;
    else /* This is the first of the list now */
      node->owner->first = node; 
    
    parent->prev = node;
  }
  else if (parent->key < node->key)
  {
    if (parent->right != NULL)
      return rbtree_insert_node (parent->right, node);
    
    parent->right = node;
    node->parent  = parent;

    node->next = parent->next;
    node->prev = parent;

    if (node->next != NULL)
      node->next->prev = node;
    else /* This is the last of the list now */
      node->owner->last = node;
      
    parent->next = node;
  }

  return 0;
}

static struct rbtree_node *
rbtree_node_search (struct rbtree_node *node, int64_t key)
{
  if (node->key == key)
    return node;
  else if (key < node->key)
  {
    /* It's still too big, need to search leftwards */
    if (node->left == NULL)
      return node;
    else
      return rbtree_node_search (node->left, key);
  }
  else /* (node->key < key) */
  {
    /* Still to small, need to search rightwards */
    if (node->right == NULL)
      return node;
    else
      return rbtree_node_search (node->right, key);
  }
}

struct rbtree_node *
rbtree_search (rbtree_t *tree, int64_t key, enum rbtree_node_search_mode mode)
{
  static struct rbtree_node *closest;

  if (tree->root == NULL)
    return NULL;

  if (tree->cached_key == key && tree->cached_mode == mode &&
    tree->cached_node != NULL)
    return tree->cached_node;
  
  closest = rbtree_node_search (tree->root, key);

  if (closest->key != key && mode == RB_EXACT)
    return NULL;

  if (key < closest->key && mode == RB_LEFTWARDS && closest->prev)
    closest = closest->prev;
  else if (closest->key < key && mode == RB_RIGHTWARDS && closest->next)
    closest = closest->next;

  tree->cached_mode = mode;
  tree->cached_key  = key;
  tree->cached_node = closest;
  
  return closest;
}

/* Thank you, Wikipedia, for that wonderful explanation
   between pseudocode and C, with the vices of both
   and the virtues of none. */
static void
rbtree_insert_case_1 (struct rbtree_node *node)
{
  /* Case 1: Inserted node is the root node */
  if (rbtree_node_is_root (node))
    rbtree_node_set_color (node, RB_BLACK);
  else
    rbtree_insert_case_2 (node);
}


static void
rbtree_insert_case_2 (struct rbtree_node *node)
{
  /* Case 2: parent color is black, still valid */
  if (rbtree_node_color (node->parent) == RB_RED)
    rbtree_insert_case_3 (node);
}

static void
rbtree_insert_case_3 (struct rbtree_node *node)
{
  struct rbtree_node *uncle, *grandpa;

  uncle = rbtree_node_uncle (node);
  
  /* Case 3: parent and uncle are both red */
  if (uncle != NULL && rbtree_node_color (uncle) == RB_RED)
  {
    rbtree_node_set_color (node->parent, RB_BLACK);
    rbtree_node_set_color (uncle, RB_BLACK);
    grandpa = rbtree_node_grandparent (node);

    rbtree_node_set_color (grandpa, RB_RED);

    rbtree_insert_case_1 (grandpa);
  }
  else
    rbtree_insert_case_4 (node);
}

static void
rbtree_node_rotate_left (struct rbtree_node *node)
{
  struct rbtree_node *g = node->parent;
  struct rbtree_node *p = node;
  struct rbtree_node *n = node->right;
  struct rbtree_node *t = n->left;

  if (g != NULL)
  {
    assert (g->left != g->right);
    
    if (g->left == p)
      g->left = n;
    else
      g->right = n;
  }
  else
    node->owner->root = n;
  
  n->left = p;
  p->right = t;

  if (t != NULL)
    t->parent = p;
  
  n->parent = g;
  p->parent = n;
}

static void
rbtree_node_rotate_right (struct rbtree_node *node)
{
  struct rbtree_node *g = node->parent;
  struct rbtree_node *p = node;
  struct rbtree_node *n = node->left;
  struct rbtree_node *t = n->right;

  if (g != NULL)
  {
    assert (g->left != g->right);

    if (g->left == p)
      g->left = n;
    else
      g->right = n;
  }
  else
    node->owner->root = n;
  
  n->right = p;
  p->left = t;

  if (t != NULL)
    t->parent = p;
  
  n->parent = g;
  p->parent = n;
}

static void
rbtree_insert_case_4 (struct rbtree_node *node)
{
  struct rbtree_node *grandpa;

  grandpa = rbtree_node_grandparent (node);
  
  /* Case 4: the parent is red but the uncle is black.
   We have two subcases here, according to whether
   this node is the right or left child */
  if ((node == node->parent->right) &&
      node->parent == grandpa->left)
  {
    assert (grandpa == node->parent->parent);
    
    rbtree_node_rotate_left (node->parent);
    node = node->left;
  }
  else if ((node == node->parent->left) &&
	   node->parent == grandpa->right)
  {
    assert (grandpa == node->parent->parent);
    
    rbtree_node_rotate_right (node->parent);
    node = node->right;
  }

  assert (node->parent->parent == grandpa);
      
  rbtree_insert_case_5 (node);
}


static void
rbtree_insert_case_5 (struct rbtree_node *node)
{
  struct rbtree_node *grandpa;

  grandpa = rbtree_node_grandparent (node);

  assert (grandpa);
  
  /* Case 5: The parent node is red but the uncle is black, the current node is the left child, and the parent is the left child of the grandparent. */
  
  rbtree_node_set_color (node->parent, RB_BLACK);

  rbtree_node_set_color (grandpa, RB_RED);

  if (node == node->parent->left)
    rbtree_node_rotate_right (grandpa);
  else
    rbtree_node_rotate_left (grandpa);
}

int
rbtree_set (rbtree_t *self, int64_t key, void *data)
{
  struct rbtree_node *node;

  node = rbtree_search(self, key, RB_EXACT);

  if (node != NULL) {
    if (node->data != NULL && self->node_dtor != NULL)
      (self->node_dtor) (node->data, self->node_dtor_data);
    node->data = data;

    return 0;
  }

  return rbtree_insert(self, key, data);
}

int
rbtree_insert (rbtree_t *tree, int64_t key, void *data)
{
  struct rbtree_node *node;
  
  if ((node = rbtree_node_new (tree, key, data)) == NULL)
    return -1;

  rbtree_invalidate_cache (tree);
  
  if (tree->root == NULL)
    tree->first = tree->last = tree->root = node;
  else if (rbtree_insert_node (tree->root, node))
    return 1;

  rbtree_insert_case_1 (node);
  
  return 0;
}

void
rbtree_clear (rbtree_t *tree)
{
  struct rbtree_node *this;

  this = tree->first;

  while (this != NULL) {
    struct rbtree_node *next = this->next;
    rbtree_node_destroy (this);
    this = next;
  }

  tree->root = tree->first = tree->last = NULL;
}

void
rbtree_destroy (rbtree_t *tree)
{
  rbtree_clear (tree);

  free (tree);
}
