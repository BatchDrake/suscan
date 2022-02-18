/*

  Copyright (C) 2022 Gonzalo José Carracedo Carballal

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
#define SU_LOG_DOMAIN "multicast-processor"

#include "multicast.h"
#include "processors/psd.h"
#include "processors/encap.h"

SUPRIVATE rbtree_t *g_mc_processor_hash = NULL;
SUPRIVATE SUBOOL    g_mc_processor_init = SU_FALSE;

SUBOOL
suscli_multicast_processor_register(
  const struct suscli_multicast_processor_impl *impl)
{
  SUBOOL ok = SU_FALSE;
  void *result;

  result = rbtree_search_data(
    g_mc_processor_hash,
    impl->sf_type,
    RB_EXACT,
    NULL);

  if (result == NULL) {
    SU_ERROR("Superframe processor already registered\n");
    goto done;
  }

  SU_TRYC(rbtree_insert(g_mc_processor_hash, impl->sf_type, (void *) impl));

  ok = SU_TRUE;

done:
  return ok;
}
  
SUBOOL
suscli_multicast_processor_init(void)
{
  SUBOOL ok = SU_FALSE;

  if (!g_mc_processor_init) {
    if (g_mc_processor_hash == NULL)
    SU_TRY(g_mc_processor_hash = rbtree_new());

    SU_TRY(suscli_multicast_processor_psd_register());
    SU_TRY(suscli_multicast_processor_encap_register());

    g_mc_processor_init = SU_TRUE;
  }

  ok = SU_TRUE;

done:
  return ok;
}

SUPRIVATE SU_METHOD(
  suscli_multicast_processor,
  SUBOOL,
  make_processor_tree)
{
  struct rbtree_node *this;
  const struct suscli_multicast_processor_impl *impl = NULL;
  void *state = NULL;
  uint8_t type;

  SUBOOL ok = SU_FALSE;

  this = rbtree_get_first(g_mc_processor_hash);

  while (this != NULL) {
    impl = this->data;
    type = impl->sf_type;

    SU_TRY(state = (impl->ctor) (self));
    SU_TRY(rbtree_insert(self->processor_tree, type, state));
    
    state = NULL;

    this = this->next;
  }

  ok = SU_TRUE;

done:
  if (impl != NULL && state != NULL)
    (impl->dtor) (state);

  return ok;
}

SU_INSTANCER(
  suscli_multicast_processor,
  suscli_multicast_processor_call_cb_t on_call,
  void *userdata)
{
  suscli_multicast_processor_t *new = NULL;

  SU_TRY_FAIL(suscli_multicast_processor_init());

  SU_ALLOCATE_FAIL(new, suscli_multicast_processor_t);

  SU_TRY_FAIL(new->processor_tree = rbtree_new());

  SU_TRY_FAIL(suscli_multicast_processor_make_processor_tree(new));

  new->on_call  = on_call;
  new->userdata = userdata;

  return new;

fail:
  if (new != NULL)
    suscli_multicast_processor_destroy(new);

  return NULL;
}

SU_METHOD(
  suscli_multicast_processor,
  SUBOOL,
  trigger_on_call)
{
  struct suscan_analyzer_remote_call call =
    suscan_analyzer_remote_call_INITIALIZER;
  SUBOOL result = SU_TRUE;;

  /* Do nothing if current implementation is missing */
  if (self->curr_impl == NULL)
    return SU_TRUE;

  /* try_flush returns true if a call is available */
  if ((self->curr_impl->try_flush) (self->curr_state, &call)) {

    /* On the other hand, on_call may fail */
    result = (self->on_call) (self, self->userdata, &call);
    suscan_analyzer_remote_call_finalize(&call);
  }

  return result;
}


SU_METHOD(
  suscli_multicast_processor,
  SUBOOL,
  process,
  const struct suscan_analyzer_fragment_header *header)
{
  SUBOOL first;
  const struct suscli_multicast_processor_impl *impl = NULL;
  void *state = NULL;
  int8_t delta;
  SUBOOL ok = SU_FALSE;

  /* Announces are gracefully ignored. */
  if (header->sf_type == SUSCAN_ANALYZER_SUPERFRAME_TYPE_ANNOUNCE)
    return SU_TRUE;
  /* 
   * Two situations possible:
   *  1. This ID is old. Discard.
   *  2. This ID corresponds to a current packet. Process.
   *  3. This ID is new. Flush previous, if possible. Then process.
   * 
   * Of course, there is a roll-over situation that must be handled
   * appropriately. We'll live a +127 grace delta for these cases.
   */

  first = self->curr_impl == NULL;

  delta = header->sf_id - self->curr_id;

  if (delta >= 0 || first) {
    /* Check if we must refresh the current ID */
    if (delta > 1 || first) {
      if (self->curr_impl != NULL) {
        /* 
         * We are about to drop the current cached processor, 
         * try to flush if possible
         *  */
        SU_TRY(suscli_multicast_processor_trigger_on_call(self));
      }

      impl = rbtree_search_data(
        g_mc_processor_hash,
        header->sf_type,
        RB_EXACT,
        NULL);

      if (impl == NULL) {
        SU_WARNING("Unknown superframe type %d\n", header->sf_type);
        self->curr_impl  = NULL;
        self->curr_state = NULL;
        self->curr_id    = header->sf_id;  
        return SU_TRUE;
      }

      state = rbtree_search_data(
        self->processor_tree,
        header->sf_type,
        RB_EXACT,
        NULL);

      self->curr_impl  = impl;
      self->curr_state = state;
      self->curr_id    = header->sf_id;
    }

    (self->curr_impl->on_fragment) (self->curr_state, header);

    /* We do not trigger on_call here. We let on_fragment decide that */
  }

  ok = SU_TRUE;

done:
  return ok;
}

SU_COLLECTOR(suscli_multicast_processor)
{
  struct rbtree_node *this;
  const struct suscli_multicast_processor_impl *impl = NULL;

  /* Destroy all processors */
  if (self->processor_tree != NULL) {
    this = rbtree_get_first(self->processor_tree);

    while (this != NULL) {
      /* Find dtor for this object */
      impl = rbtree_search_data(
        g_mc_processor_hash,
        this->key,
        RB_EXACT,
        NULL);
      
      (impl->dtor)(this->data);
      this = this->next;
    }

    rbtree_destroy(self->processor_tree);
  }

  free(self);
}

