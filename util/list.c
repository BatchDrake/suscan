/*
 *    list.c: Doubly-linked lists
 *    Copyright(C) 2021 Gonzalo José Carracedo Carballal
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>
 */
 
#include "list.h"
#include <assert.h>

void *
list_get_tail(void **list)
{
  struct list_head *this;
  
  assert(list != NULL);
  
  if ((this = LIST_HEAD(*list)) == NULL)
    return NULL;
  
  while (this->next)
    this = this->next;
    
  return this;
}

void *
list_get_head(void **list)
{
  assert(list != NULL);
  
  return *list;
}

void
list_join(void **list1, void **list2)
{
  struct list_head *tail1;
  
  assert(list_is_head(list1));
  assert(list_is_head(list2));
  
  tail1 =(struct list_head *) list_get_tail(list1);
  
  if (tail1 == NULL)
  {
    *list1 = *list2;
    return;
  }
  
  tail1->next = LIST_HEAD(*list2);
}

void
list_remove_element(void **list, void *element)
{
  struct list_head *head, *this;
  
  assert(list != NULL);
  assert(element != NULL);
  assert(!list_is_empty(list));
  
  head = LIST_HEAD(*list);
  this = LIST_HEAD(element);
  
  if (head == this)
    *list = this->next;
  
  if (this->prev != NULL)
    this->prev->next = this->next;
  
  if (this->next != NULL)
    this->next->prev = this->prev;
}

void
list_insert_head(void **list, void *element)
{
  assert(list != NULL);
  assert(element != NULL);
  
  LIST_HEAD(element)->prev = NULL;
  LIST_HEAD(element)->next = LIST_HEAD(*list);
  
  if (LIST_HEAD(element)->next != NULL)
    LIST_HEAD(element)->next->prev = LIST_HEAD(element);
  
  *list = element;
}

void
list_insert_after(void **list, void *element, void *new)
{
  assert(list != NULL);
  assert(*list != NULL);
  assert(element != NULL);
  assert(new != NULL);
  
  LIST_HEAD(new)->next = LIST_HEAD(element)->next;
  LIST_HEAD(new)->prev = LIST_HEAD(element);
  
  if (LIST_HEAD(element)->next != NULL)
    LIST_HEAD(element)->next->prev = LIST_HEAD(new);
    
  LIST_HEAD(element)->next = LIST_HEAD(new);
}

void
list_insert_before(void **list, void *element, void *new)
{
  assert(list != NULL);
  assert(*list != NULL);
  assert(element != NULL);
  assert(new != NULL);
  
  LIST_HEAD(new)->prev = LIST_HEAD(element)->prev;
  LIST_HEAD(new)->next = LIST_HEAD(element);
  
  if (LIST_HEAD(element)->prev != NULL)
    LIST_HEAD(element)->prev->next = LIST_HEAD(new);
  else
    *list = new;
    
  LIST_HEAD(element)->prev = LIST_HEAD(new);
}

void
list_insert_tail(void **list, void *element)
{
  void *tail;
  
  if ((tail = list_get_tail(list)) == NULL)
    list_insert_head(list, element);
  else
    list_insert_after(list, tail, element);
}

void *
sorted_list_get_tail(void **list)
{
  return list_get_tail(list);
}

void *
sorted_list_get_head(void **list)
{
  return list_get_head(list);
}

void
sorted_list_remove_element(void **list, void *element)
{
  list_remove_element(list, element);
}

void *
sorted_list_search(void **list, uint64_t index)
{
  struct sorted_list_head *this;
  
  assert(list != NULL);
  
  this = SORTED_LIST_HEAD(*list);
  
  while (this)
  {
    if (this->index == index)
      return this;
    else if (this->index > index)
      return NULL;
      
    this = SORTED_LIST_HEAD(this->head.next);
  }
  
  return NULL;
}

void *
sorted_list_get_next(void **list, uint64_t index)
{
  struct sorted_list_head *this;
  
  assert(list != NULL);
  
  this = SORTED_LIST_HEAD(*list);
  
  while (this)
  {
    if (this->index >= index)
      return this;
      
    this = SORTED_LIST_HEAD(this->head.next);
  }
  
  return NULL;
}

void *
sorted_list_get_previous(void **list, uint64_t index)
{
  struct sorted_list_head *this;
  
  assert(list != NULL);
  
  this = SORTED_LIST_HEAD(*list);
  
  while (this)
  {
    if (this->index > index)
      return this->head.prev;
      
    if (this->head.next == NULL)
      return this;
      
    this = SORTED_LIST_HEAD(this->head.next);
  }
  
  return NULL;
}

void
sorted_list_insert(void **list, void *element, uint64_t index)
{
  void *alike;
  
  assert(list != NULL);
  assert(element != NULL);
  
  SORTED_LIST_HEAD(element)->index = index;
  
  if (sorted_list_is_empty(list))
  {
    list_insert_head(list, element);
    return;
  }
  
  alike = sorted_list_get_next(list, index);
  
  if (alike == NULL)
    list_insert_after(list, list_get_tail(list), element);
  else
    list_insert_before(list, alike, element);
}

void
sorted_list_debug(void **list)
{
  struct sorted_list_head *last, *this;
  
  this = last = SORTED_LIST_HEAD(*list);
  
  while (this)
  {
    last = this;
    
    printf("%d -> ",(int) this->index);
    
    this = SORTED_LIST_HEAD(this->head.next);
  }
  
  printf("TAIL\n");
  
  this = last;
  
  while (this)
  {
    printf("%d <- ",(int) this->index);
    
    this = SORTED_LIST_HEAD(this->head.prev);
  }
  
  printf("HEAD\n");
}
 
int 
circular_list_is_empty(void **list)
{
  assert(list != NULL);
  
  return LIST_HEAD(*list) == NULL;
}

int 
circular_list_is_head(void **list, void *element)
{
  assert(list != NULL);
  assert(element != NULL);
  
  return LIST_HEAD(*list) == LIST_HEAD(element);
}

int 
circular_list_is_tail(void **list, void *element)
{
  assert(list != NULL);
  return LIST_HEAD(*list)->prev == LIST_HEAD(element);
}

void *
circular_list_get_head(void **list)
{
  assert(list != NULL);
  
  return *list;
}

void *
circular_list_get_tail(void **list)
{
  assert(list != NULL);
  
  if (*list == NULL)
    return NULL;
    
  return(void *) LIST_PREV(*list);
}

void
circular_list_remove_element(void **list, void *element)
{
  struct circular_list_head *prev, *next;
  
  assert(list != NULL);
  assert(element != NULL);
  
  prev = CIRCULAR_LIST_HEAD(CIRCULAR_LIST_HEAD(element)->head.prev);
  next = CIRCULAR_LIST_HEAD(CIRCULAR_LIST_HEAD(element)->head.next);
  
  if (prev == element && element == next)
  {
    *list = NULL;
    return;
  }
  else if (circular_list_is_head(list, element))
    *list =(void *) next;
    
  next->head.prev = LIST_HEAD(prev);
  prev->head.next = LIST_HEAD(next);
}

void
circular_list_insert_head(void **list, void *element)
{ 
  struct circular_list_head *old_head;
  
  assert(list != NULL);
  assert(element != NULL);
  
  if (circular_list_is_empty(list))
  {
    CIRCULAR_LIST_HEAD(element)->head.next = LIST_HEAD(element);
    CIRCULAR_LIST_HEAD(element)->head.prev = LIST_HEAD(element);
  }
  else
  {
    old_head = CIRCULAR_LIST_HEAD(*list);
    
    CIRCULAR_LIST_HEAD(element)->head.next = LIST_HEAD(old_head);
    CIRCULAR_LIST_HEAD(element)->head.prev = old_head->head.prev;
    
    old_head->head.prev->next = element;
    old_head->head.prev       = element;
  }
  
  *list =(void *) element;
}

void
circular_list_insert_tail(void **list, void *element)
{ 
  struct circular_list_head *old_tail;
  
  assert(list != NULL);
  assert(element != NULL);
  
  if (circular_list_is_empty(list))
  {
    CIRCULAR_LIST_HEAD(element)->head.next = LIST_HEAD(element);
    CIRCULAR_LIST_HEAD(element)->head.prev = LIST_HEAD(element);
    
    *list =(void *) element;
  }
  else
  {
    old_tail = CIRCULAR_LIST_HEAD(CIRCULAR_LIST_HEAD(*list)->head.prev);
    
    CIRCULAR_LIST_HEAD(element)->head.next = LIST_HEAD(*list);
    CIRCULAR_LIST_HEAD(element)->head.prev = LIST_HEAD(old_tail);
    
    old_tail->head.next->prev = element;
    old_tail->head.next       = element;
  }
}

void
circular_list_scroll_to(void **list, void *element)
{
  assert(list != NULL);
  assert(element != NULL);
  
  if (!circular_list_is_empty(list))
    *list = element;
}

void
circular_list_scroll_next(void **list)
{
  assert(list != NULL);
  
  if (!circular_list_is_empty(list))
    *list =(void *) CIRCULAR_LIST_HEAD(*list)->head.next;
}

void
circular_list_scroll_prev(void **list)
{
  assert(list != NULL);
  
  if (!circular_list_is_empty(list))
    *list =(void *) CIRCULAR_LIST_HEAD(*list)->head.prev;
}

void
circular_list_debug(void **list)
{
  void *first, *this;
  
  first = *list;
  
  if (!first)
  {
    printf("<empty>\n");
    return;
  }
  
  printf("Forward:  ");
  
  this = first;
  do
  {
    printf("<%p> ", this);
    
    this = LIST_NEXT(this);
  }
  while (this != first);
  
  printf("\nBackward: ");
  
  do
  {
    printf("<%p> ", this);
    
    this = LIST_PREV(this);
  }
  while (this != first);
  
  printf("\n");
}
