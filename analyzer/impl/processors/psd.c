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

#include "psd.h"
#include <analyzer/msg.h>

SUPRIVATE void
suscli_multicast_processor_psd_dtor(void *userdata)
{
  struct suscli_multicast_processor_psd *self = 
    (struct suscli_multicast_processor_psd *) userdata;

  if (self->psd_data != NULL)
    free(self->psd_data);
  
  free(self);
}

SUPRIVATE void
suscli_multicast_processor_psd_clear(
  struct suscli_multicast_processor_psd *self)
{
  if (self->psd_data != NULL)
    free(self->psd_data);

  self->psd_data      = NULL;
  self->psd_size      = 0;
}


SUPRIVATE SUBOOL
suscli_multicast_processor_psd_on_fragment(
  void *userdata,
  const struct suscan_analyzer_fragment_header *header)
{
  struct suscli_multicast_processor_psd *self =
    (struct suscli_multicast_processor_psd *) userdata;
  const struct suscan_analyzer_psd_sf_fragment *frag;

  uint32_t full_size = ntohl(header->sf_size);
  uint32_t offset    = ntohl(header->sf_offset);
  uint16_t size      = ntohl(header->size);
  SUBOOL ok = SU_FALSE;

  /*
   * Adding a fragment involves several sanity checks, in particular:
   *  Is it safe to place it where the client asks?
   *  Does it fit?
   *  Have the buffer properties changed?
   * 
   * We only trigger call when the processor asks for it
   */

  /* Malformed PDU? */
  if (size < sizeof(struct suscan_analyzer_psd_sf_fragment))
    return SU_TRUE;

  /* The true number of fragments is obtained by subtracting
     the fragment header */
  size -= sizeof(struct suscan_analyzer_psd_sf_fragment);
  size /= sizeof(SUFLOAT);

  frag = (struct suscan_analyzer_psd_sf_fragment *) header->sf_data;

  /* New PDU size. Discard current data */
  if (full_size != self->psd_size) {
    /* Trigger whatever we have cached */
    suscli_multicast_processor_trigger_on_call(self->proc);

    /* Clear everything */
    suscli_multicast_processor_psd_clear(self);

    if (full_size > SUSCLI_MULTICAST_MAX_SUPERFRAME_SIZE) {
      SU_WARNING("Warning: superframe size is too big, ignored\n");
      return SU_TRUE;
    }

    self->psd_size = full_size;

    /* Allocate again */
    if (full_size > 0)
      SU_ALLOCATE_MANY(self->psd_data, full_size, SUFLOAT);

    self->updates = 0;
  }
  
  /* Does it even fit? */
  if (offset + size > full_size) {
    SU_WARNING("Warning: buffer overflow attempt\n");
    return SU_TRUE;
  }

  memcpy(
    self->psd_data + offset,
    frag->bytes,
    size * sizeof(SUFLOAT));

  /* Fragment header is updated only once */
  if (self->updates == 0)
    self->sf_header = *frag;
  
  ++self->updates;

  ok = SU_TRUE;
  
done:
  return ok;
}

SUPRIVATE SUBOOL
suscli_multicast_processor_psd_try_flush(
  void *userdata,
  struct suscan_analyzer_remote_call *call)
{
  struct suscli_multicast_processor_psd *self =
    (struct suscli_multicast_processor_psd *) userdata;
  struct suscan_analyzer_psd_msg *msg = NULL;

  SUBOOL ok = SU_FALSE;

  if (self->updates > 0) {
    self->sf_header.samp_rate_u32 = 
      ntohl(self->sf_header.samp_rate_u32);

    self->sf_header.measured_samp_rate_u32 = 
      ntohl(self->sf_header.measured_samp_rate_u32);

    SU_TRY(
      msg = suscan_analyzer_psd_msg_new_from_data(
        self->sf_header.samp_rate,
        self->psd_data,
        self->psd_size));
    
    msg->fc                 = su_ntohll(self->sf_header.fc);
    msg->timestamp.tv_sec   = su_ntohll(self->sf_header.timestamp_sec);
    msg->timestamp.tv_usec  = ntohl(self->sf_header.timestamp_usec);
    msg->rt_time.tv_sec     = su_ntohll(self->sf_header.rt_timestamp_sec);
    msg->rt_time.tv_usec    = ntohl(self->sf_header.rt_timestamp_usec);
    msg->measured_samp_rate = self->sf_header.measured_samp_rate;
    msg->looped             = su_ntohll(self->sf_header.flags) & 1;

    /* Populate message */
    call->type = SUSCAN_ANALYZER_REMOTE_MESSAGE;
    call->msg.ptr = msg;
    call->msg.type = SUSCAN_ANALYZER_MESSAGE_TYPE_PSD;

    msg = NULL;

    /* Reset update counter */
    self->updates = 0;
    ok = SU_TRUE;
  }

done:
  return ok;
}

SUPRIVATE void *
suscli_multicast_processor_psd_ctor(struct suscli_multicast_processor *proc)
{
  struct suscli_multicast_processor_psd *new = NULL;

  SU_ALLOCATE_FAIL(new, struct suscli_multicast_processor_psd);

  new->proc = proc;

  return new;

fail:
  if (new != NULL)
    suscli_multicast_processor_psd_dtor(new);

  return NULL;
}

SUBOOL
suscli_multicast_processor_psd_register(void)
{
  static struct suscli_multicast_processor_impl impl;

  impl.name        = "psd";
  impl.sf_type     = SUSCAN_ANALYZER_SUPERFRAME_TYPE_PSD;
  impl.ctor        = suscli_multicast_processor_psd_ctor;
  impl.dtor        = suscli_multicast_processor_psd_dtor;
  impl.on_fragment = suscli_multicast_processor_psd_on_fragment;
  impl.try_flush   = suscli_multicast_processor_psd_try_flush;

  return suscli_multicast_processor_register(&impl);
}
