/*
 *    list.c: Doubly-linked lists
 *    Copyright (C) 2021 Gonzalo José Carracedo Carballal
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>
 */
 
    
#ifndef _MISC_LIST_H
#define _MISC_LIST_H

#include <sigutils/types.h>

#define AS_LIST(ptr)            ((void **) (&ptr))
#define LIST_HEAD(ptr)          ((struct list_head *) (ptr))
#define SORTED_LIST_HEAD(ptr)   ((struct sorted_list_head *) (ptr))
#define CIRCULAR_LIST_HEAD(ptr) ((struct circular_list_head *) (ptr))

#define LINKED_LIST   struct list_head _head
#define SORTED_LIST   struct sorted_list_head _head
#define CIRCULAR_LIST struct circular_list_head _head

#define LIST_NEXT(x) ((void *) ((struct list_head *) (x))->next)
#define LIST_PREV(x) ((void *) ((struct list_head *) (x))->prev)

#define FOR_EACH(iter, list) for (iter = list; iter != NULL; iter = LIST_NEXT (iter))
#define FOR_EACH_SAFE(iter, tmp, list) \
  for (iter = tmp = list; tmp = (iter == NULL ? NULL : LIST_NEXT(iter)), iter != NULL; iter = tmp)

struct list_head
{
  struct list_head *next;
  struct list_head *prev;
};

struct sorted_list_head
{
  struct list_head head;
  uint64_t         index;
};

struct circular_list_head
{
  struct list_head head;
};

#define QWORD_MAX 0xffffffffffffffffull

SUINLINE int
list_is_empty(void **list)
{
  return *list == NULL;
}

SUINLINE int
list_is_head(void **list)
{
  if (list_is_empty(list))
    return 1;
    
  return LIST_HEAD(*list)->prev == NULL;
}

SUINLINE int
sorted_list_is_empty(void **list)
{
  return list_is_empty(list);
}

SUINLINE int
sorted_list_is_head(void **list)
{
  return list_is_head(list);
}

void *list_get_tail (void **);
void *list_get_head (void **);
void  list_join (void **, void **);
void  list_remove_element (void **, void *);
void  list_insert_head (void **, void *);
void  list_insert_tail (void **, void *);
void  list_insert_after (void **, void *, void *);
void  list_insert_before (void **, void *, void *);

void *sorted_list_get_tail (void **);
void *sorted_list_get_head (void **);
void  sorted_list_remove_element (void **, void *);
void *sorted_list_search (void **, uint64_t);
void *sorted_list_get_next (void **, uint64_t);
void *sorted_list_get_previous (void **, uint64_t);
void  sorted_list_insert (void **, void *, uint64_t);

#define rsorted_list_get_tail(l) sorted_list_get_tail (l)
#define rsorted_list_get_head(l) sorted_list_get_head (l)
#define rsorted_list_remove_element(l, e) sorted_list_remove_element (l, e)
#define rsorted_list_search(l, i) sorted_list_search (l, QWORD_MAX - (i))
#define rsorted_list_get_next(l, i) sorted_list_get_next (l, QWORD_MAX - (i))
#define rsorted_list_get_previous(l, i) sorted_list_get_previous (l, QWORD_MAX - (i))
#define rsorted_list_insert(l, e, i) sorted_list_insert (l, e, QWORD_MAX - (i))

int   circular_list_is_empty (void **);
int   circular_list_is_head (void **, void *);
int   circular_list_is_tail (void **, void *);
void *circular_list_get_head (void **);
void *circular_list_get_tail (void **);
void  circular_list_remove_element (void **, void *);
void  circular_list_insert_head (void **, void *);
void  circular_list_insert_tail (void **, void *);
void  circular_list_scroll_to (void **, void *);
void  circular_list_scroll_next (void **);
void  circular_list_scroll_prev (void **);

#endif /* _MISC_LIST_H */

