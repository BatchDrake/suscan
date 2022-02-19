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

#include "encap.h"

SUPRIVATE void
suscli_multicast_processor_encap_dtor(void *userdata)
{
  struct suscli_multicast_processor_encap *self = 
    (struct suscli_multicast_processor_encap *) userdata;

  if (self->pdu_data != NULL)
    free(self->pdu_data);
  
  if (self->pdu_bitmap != NULL)
    free(self->pdu_bitmap);
  
  free(self);
}

/* I know this is suboptimal. Feel free to optimize it. */
SUPRIVATE void
suscli_multicast_processor_encap_copy(
  struct suscli_multicast_processor_encap *self,
  const uint8_t *__restrict data,
  unsigned int offset,
  unsigned int size)
{
  unsigned int i;
  unsigned int block, bit, mask;
  unsigned int p = offset;

  for (i = 0; i < size; ++i, ++p) {
    block = p >> 6;
    bit   = p & 0x3f;
    mask  = 1ull << bit;

    /* Is this byte free? */
    if (!(self->pdu_bitmap[block] & mask)) {
      self->pdu_data[p] = data[i];
      self->pdu_bitmap[block] |= mask;
      --self->pdu_remaining;
    }
  }
}

SUPRIVATE void
suscli_multicast_processor_encap_clear(
  struct suscli_multicast_processor_encap *self)
{
  if (self->pdu_data != NULL)
    free(self->pdu_data);

  if (self->pdu_bitmap)
    free(self->pdu_bitmap);
  

  self->pdu_bitmap    = NULL;
  self->pdu_data      = NULL;
  self->pdu_size      = 0;
  self->pdu_remaining = 0;
}


SUPRIVATE SUBOOL
suscli_multicast_processor_encap_on_fragment(
  void *userdata,
  const struct suscan_analyzer_fragment_header *header)
{
  struct suscli_multicast_processor_encap *self =
    (struct suscli_multicast_processor_encap *) userdata;
  uint32_t full_size = ntohl(header->sf_size);
  uint32_t offset    = ntohl(header->sf_offset);
  uint16_t size      = ntohs(header->size);
  unsigned int entries;
  SUBOOL ok = SU_FALSE;

  /*
   * Adding a fragment involves several sanity checks, in particular:
   *  Is it safe to place it where the client asks?
   *  Does it fit?
   *  Have the buffer properties changed?
   * 
   * On the other hand, if remaining drops to zero, we must
   * trigger on_call to flush the current PDU.
   */

  /* New PDU size. Discard current data */
  if (full_size != self->pdu_size || self->sf_id != header->sf_id) {
    self->sf_id = header->sf_id;

    suscli_multicast_processor_encap_clear(self);

    if (full_size > SUSCLI_MULTICAST_MAX_SUPERFRAME_SIZE) {
      SU_WARNING("Warning: superframe size is too big, ignored\n");
      return SU_TRUE;
    }

    /* Number of byte in the PDU --> blocks */
    entries = (full_size + 63) >> 6;

    self->pdu_size = full_size;
    self->pdu_remaining = full_size;

    if (full_size > 0) {
      SU_ALLOCATE_MANY(self->pdu_data,   full_size, uint8_t);
      SU_ALLOCATE_MANY(self->pdu_bitmap, entries,   uint64_t);
    }
  }

  /* Does it even fit? */
  if (offset + size > full_size) {
    SU_WARNING("Warning: buffer overflow attempt\n");
    return SU_TRUE;
  }

  if (full_size > 0) {
    /* Copy these bytes */
    suscli_multicast_processor_encap_copy(
      self,
      header->sf_data,
      offset,
      size);

    if (self->pdu_remaining == 0)
      suscli_multicast_processor_trigger_on_call(self->proc);
  }

  ok = SU_TRUE;
  
done:
  return ok;
}

SUPRIVATE SUBOOL
suscli_multicast_processor_encap_try_flush(
  void *userdata,
  struct suscan_analyzer_remote_call *call)
{
  struct suscli_multicast_processor_encap *self =
    (struct suscli_multicast_processor_encap *) userdata;
  grow_buf_t buf = grow_buf_INITIALIZER;

  SUBOOL ok = SU_FALSE;

  if (self->pdu_remaining == 0) {
    grow_buf_init_loan(
      &buf,
      self->pdu_data,
      self->pdu_size,
      self->pdu_size);

    SU_TRY(suscan_analyzer_remote_call_deserialize(call, &buf));

    ok = SU_TRUE;
  }

  /* Return 0 otherwise */

done:
  /* No need to release grow_buf (it is a loan) */

  return ok;
}

SUPRIVATE void *
suscli_multicast_processor_encap_ctor(struct suscli_multicast_processor *proc)
{
  struct suscli_multicast_processor_encap *new = NULL;

  SU_ALLOCATE_FAIL(new, struct suscli_multicast_processor_encap);

  new->proc = proc;

  return new;

fail:
  if (new != NULL)
    suscli_multicast_processor_encap_dtor(new);

  return NULL;
}

SUBOOL
suscli_multicast_processor_encap_register(void)
{
  static struct suscli_multicast_processor_impl impl;

  impl.name        = "encap";
  impl.sf_type     = SUSCAN_ANALYZER_SUPERFRAME_TYPE_ENCAP;
  impl.ctor        = suscli_multicast_processor_encap_ctor;
  impl.dtor        = suscli_multicast_processor_encap_dtor;
  impl.on_fragment = suscli_multicast_processor_encap_on_fragment;
  impl.try_flush   = suscli_multicast_processor_encap_try_flush;

  return suscli_multicast_processor_register(&impl);
}
