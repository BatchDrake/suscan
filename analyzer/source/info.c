/*

  Copyright (C) 2023 Gonzalo José Carracedo Carballal

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

#include <string.h>

#define SU_LOG_DOMAIN "source-info"

#include <analyzer/source/info.h>
#include <sigutils/defs.h>

/* Come on */
#ifdef bool
#  undef bool
#endif

/********************************** Gain info *********************************/
void
suscan_source_gain_info_destroy(struct suscan_source_gain_info *self)
{
  if (self->name != NULL)
    free(self->name);

  free(self);
}

struct suscan_source_gain_info *
suscan_source_gain_info_dup(
    const struct suscan_source_gain_info *old)
{
  struct suscan_source_gain_info *new = NULL;

  SU_TRYCATCH(
      new = calloc(1, sizeof(struct suscan_source_gain_info)),
      goto fail);

  SU_TRYCATCH(new->name = strdup(old->name), goto fail);

  new->max   = old->max;
  new->min   = old->min;
  new->step  = old->step;
  new->value = old->value;

  return new;

fail:
  if (new != NULL)
    suscan_source_gain_info_destroy(new);

  return NULL;
}

struct suscan_source_gain_info *
suscan_source_gain_info_new(
    const struct suscan_source_gain_value *value)
{
  struct suscan_source_gain_info *new = NULL;

  SU_TRYCATCH(
      new = calloc(1, sizeof(struct suscan_source_gain_info)),
      goto fail);

  SU_TRYCATCH(new->name = strdup(value->desc->name), goto fail);

  new->max   = value->desc->max;
  new->min   = value->desc->min;
  new->step  = value->desc->step;
  new->value = value->val;

  return new;

fail:
  if (new != NULL)
    suscan_source_gain_info_destroy(new);

  return NULL;
}

struct suscan_source_gain_info *
suscan_source_gain_info_new_value_only(
    const char *name,
    SUFLOAT value)
{
  struct suscan_source_gain_info *new = NULL;

  SU_TRYCATCH(
      new = calloc(1, sizeof(struct suscan_source_gain_info)),
      goto fail);

  SU_TRYCATCH(new->name = strdup(name), goto fail);

  new->value = value;

  return new;

fail:
  if (new != NULL)
    suscan_source_gain_info_destroy(new);

  return NULL;
}

/* Helper methods */
SUSCAN_SERIALIZER_PROTO(suscan_source_info)
{
  SUSCAN_PACK_BOILERPLATE_START;
  unsigned int i;

  SUSCAN_PACK(uint,  self->permissions);
  SUSCAN_PACK(uint,  self->mtu);
  SUSCAN_PACK(bool,  self->realtime);
  SUSCAN_PACK(bool,  self->replay);
  SUSCAN_PACK(uint,  self->source_samp_rate);
  SUSCAN_PACK(uint,  self->effective_samp_rate);
  SUSCAN_PACK(float, self->measured_samp_rate);
  SUSCAN_PACK(uint,  self->history_length);
  SUSCAN_PACK(freq,  self->frequency);
  SUSCAN_PACK(freq,  self->freq_min);
  SUSCAN_PACK(freq,  self->freq_max);
  SUSCAN_PACK(freq,  self->lnb);
  SUSCAN_PACK(float, self->bandwidth);
  SUSCAN_PACK(float, self->ppm);
  SUSCAN_PACK(str,   self->antenna);
  SUSCAN_PACK(bool,  self->dc_remove);
  SUSCAN_PACK(bool,  self->iq_reverse);
  SUSCAN_PACK(bool,  self->agc);

  SUSCAN_PACK(bool,   self->have_qth);
  if (self->have_qth) {
    SUSCAN_PACK(double, self->qth.lat);
    SUSCAN_PACK(double, self->qth.lon);
    SUSCAN_PACK(double, self->qth.height);
  }

  SUSCAN_PACK(uint, self->source_time.tv_sec);
  SUSCAN_PACK(uint, self->source_time.tv_usec);

  SUSCAN_PACK(bool, self->seekable);
  if (self->seekable || self->replay) {
    SUSCAN_PACK(uint,  self->source_start.tv_sec);
    SUSCAN_PACK(uint,  self->source_start.tv_usec);  
    SUSCAN_PACK(uint, self->source_end.tv_sec);
    SUSCAN_PACK(uint, self->source_end.tv_usec);
  }

  SU_TRYCATCH(cbor_pack_map_start(buffer, self->gain_count) == 0, goto fail);
  for (i = 0; i < self->gain_count; ++i)
    SU_TRYCATCH(
        suscan_source_gain_info_serialize(self->gain_list[i], buffer),
        goto fail);

  SU_TRYCATCH(cbor_pack_map_start(buffer, self->antenna_count) == 0, goto fail);
  for (i = 0; i < self->antenna_count; ++i)
    SUSCAN_PACK(str, self->antenna_list[i]);

  SUSCAN_PACK_BOILERPLATE_END;
}

SUSCAN_DESERIALIZER_PROTO(suscan_source_info)
{
  SUSCAN_UNPACK_BOILERPLATE_START;
  SUBOOL end_required = SU_FALSE;
  size_t i;
  uint64_t nelem = 0;
  uint64_t tv_sec = 0;
  uint32_t tv_usec = 0;

  SUSCAN_UNPACK(uint64, self->permissions);
  SUSCAN_UNPACK(uint32, self->mtu);
  SUSCAN_UNPACK(bool,   self->realtime);
  SUSCAN_UNPACK(bool,   self->replay);
  SUSCAN_UNPACK(uint64, self->source_samp_rate);
  SUSCAN_UNPACK(uint64, self->effective_samp_rate);
  SUSCAN_UNPACK(float,  self->measured_samp_rate);
  SUSCAN_UNPACK(uint64, self->history_length);
  SUSCAN_UNPACK(freq,   self->frequency);
  SUSCAN_UNPACK(freq,   self->freq_min);
  SUSCAN_UNPACK(freq,   self->freq_max);
  SUSCAN_UNPACK(freq,   self->lnb);
  SUSCAN_UNPACK(float,  self->bandwidth);
  SUSCAN_UNPACK(float,  self->ppm);
  SUSCAN_UNPACK(str,    self->antenna);
  SUSCAN_UNPACK(bool,   self->dc_remove);
  SUSCAN_UNPACK(bool,   self->iq_reverse);
  SUSCAN_UNPACK(bool,   self->agc);

  SUSCAN_UNPACK(bool,   self->have_qth);
  if (self->have_qth) {
    SUSCAN_UNPACK(double, self->qth.lat);
    SUSCAN_UNPACK(double, self->qth.lon);
    SUSCAN_UNPACK(double, self->qth.height);
  }

  SUSCAN_UNPACK(uint64, tv_sec);
  SUSCAN_UNPACK(uint32, tv_usec);
  self->source_time.tv_sec  = tv_sec;
  self->source_time.tv_usec = tv_usec;

  SUSCAN_UNPACK(bool, self->seekable);
  if (self->seekable || self->replay) {
    SUSCAN_UNPACK(uint64, tv_sec);
    SUSCAN_UNPACK(uint32, tv_usec);  
    self->source_start.tv_sec  = tv_sec;
    self->source_start.tv_usec = tv_usec;

    SUSCAN_UNPACK(uint64, tv_sec);
    SUSCAN_UNPACK(uint32, tv_usec);
    self->source_end.tv_sec  = tv_sec;
    self->source_end.tv_usec = tv_usec;
  }

  /* Deserialize gains */
  SU_TRYCATCH(
      cbor_unpack_map_start(buffer, &nelem, &end_required) == 0,
      goto fail);
  SU_TRYCATCH(!end_required, goto fail);

  self->gain_count = (unsigned int) nelem;

  if (self->gain_count > 0) {
    SU_TRYCATCH(
        self->gain_list = calloc(
            nelem,
            sizeof (struct suscan_source_gain_info *)),
        goto fail);

    for (i = 0; i < self->gain_count; ++i) {
      SU_TRYCATCH(
          self->gain_list[i] = 
            calloc(1, sizeof (struct suscan_source_gain_info)),
          goto fail);

      SU_TRYCATCH(
          suscan_source_gain_info_deserialize(self->gain_list[i], buffer),
          goto fail);
    }
  } else {
    self->gain_list = NULL;
  }
  
  /* Deserialize antennas */
  SU_TRYCATCH(
      cbor_unpack_map_start(buffer, &nelem, &end_required) == 0,
      goto fail);
  SU_TRYCATCH(!end_required, goto fail);

  self->antenna_count = (unsigned int) nelem;

  if (self->antenna_count > 0) {
    SU_TRYCATCH(
      self->antenna_list = calloc(nelem, sizeof (char *)), 
      goto fail);

    for (i = 0; i < self->antenna_count; ++i)
      SUSCAN_UNPACK(str, self->antenna_list[i]);
  } else {
    self->antenna_list = NULL;
  }

  SUSCAN_UNPACK_BOILERPLATE_END;
}

void
suscan_source_info_init(struct suscan_source_info *self)
{
  memset(self, 0, sizeof(struct suscan_source_info));

  self->permissions = SUSCAN_ANALYZER_PERM_ALL;
}

SUBOOL
suscan_source_info_init_copy(
    struct suscan_source_info *self,
    const struct suscan_source_info *origin)
{
  struct suscan_source_gain_info *gi = NULL;
  char *dup = NULL;
  unsigned int i;
  SUBOOL ok = SU_FALSE;

  suscan_source_info_init(self);

  self->permissions         = origin->permissions;
  self->source_samp_rate    = origin->source_samp_rate;
  self->effective_samp_rate = origin->effective_samp_rate;
  self->measured_samp_rate  = origin->measured_samp_rate;
  self->history_length      = origin->history_length;
  self->frequency           = origin->frequency;
  self->freq_min            = origin->freq_min;
  self->freq_max            = origin->freq_max;
  self->lnb                 = origin->lnb;
  self->bandwidth           = origin->bandwidth;
  self->ppm                 = origin->ppm;
  self->source_time         = origin->source_time;
  self->seekable            = origin->seekable;
  self->replay              = origin->replay;
  
  if (self->seekable || self->replay) {
    self->source_start = origin->source_start;
    self->source_end   = origin->source_end;
  }

  if (origin->antenna != NULL)
    SU_TRYCATCH(self->antenna = strdup(origin->antenna), goto done);

  self->dc_remove  = origin->dc_remove;
  self->iq_reverse = origin->iq_reverse;
  self->agc        = origin->agc;

  for (i = 0; i < origin->gain_count; ++i) {
    SU_TRYCATCH(
        gi = suscan_source_gain_info_dup(origin->gain_list[i]),
        goto done);

    SU_TRYCATCH(PTR_LIST_APPEND_CHECK(self->gain, gi) != -1, goto done);
    gi = NULL;
  }

  for (i = 0; i < origin->antenna_count; ++i) {
    SU_TRYCATCH(dup = strdup(origin->antenna_list[i]), goto done);
    SU_TRYCATCH(PTR_LIST_APPEND_CHECK(self->antenna, dup) != -1, goto done);
    dup = NULL;
  }

  ok = SU_TRUE;

done:
  if (gi != NULL)
    suscan_source_gain_info_destroy(gi);

  if (dup != NULL)
    free(dup);

  if (!ok)
  suscan_source_info_finalize(self);

  return ok;
}

void
suscan_source_info_finalize(struct suscan_source_info *self)
{
  unsigned int i;

  if (self->antenna != NULL)
    free(self->antenna);

  for (i = 0; i < self->gain_count; ++i)
    if (self->gain_list[i] != NULL)
      suscan_source_gain_info_destroy(self->gain_list[i]);

  if (self->gain_list != NULL)
    free(self->gain_list);

  for (i = 0; i < self->antenna_count; ++i)
    if (self->antenna_list[i] != NULL)
      free(self->antenna_list[i]);

  if (self->antenna_list != NULL)
    free(self->antenna_list);

  memset(self, 0, sizeof(struct suscan_source_info));
}
