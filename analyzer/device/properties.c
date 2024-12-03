/*

  Copyright (C) 2024 Gonzalo José Carracedo Carballal

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

#define SU_LOG_DOMAIN "device-properties"

#include "properties.h"
#include "spec.h"
#include <analyzer/analyzer.h>
#include <sigutils/log.h>

/********************************** GainDesc **********************************/
SU_COLLECTOR(suscan_device_gain_desc)
{
  if (self->name != NULL)
    free(self->name);

  free(self);
}


SU_INSTANCER(suscan_device_gain_desc, const char *name, SUFLOAT min, SUFLOAT max)
{
  suscan_device_gain_desc_t *new = NULL;

  SU_TRY_FAIL(min <= max);

  SU_ALLOCATE_FAIL(new, suscan_device_gain_desc_t);

  SU_TRY_FAIL(new->name = strdup(name));

  new->min = min;
  new->max = max;

  return new;

fail:
  if (new != NULL)
    SU_DISPOSE(suscan_device_gain_desc, new);

  return NULL;
}

/**************************** DeviceProperties ********************************/
SU_INSTANCER(suscan_device_properties, const char *label)
{
  suscan_device_properties_t *new = NULL;

  SU_ALLOCATE_FAIL(new, suscan_device_properties_t);

  if (label == NULL)
    label = "Unnamed device";

  SU_CONSTRUCT_FAIL(hashlist, &new->gain_map);
  SU_CONSTRUCT_FAIL(strmap, &new->traits);
  SU_TRY_FAIL(new->label = strdup(label));

  new->channels = 1;
  
  return new;

fail:
  if (new != NULL)
    SU_DISPOSE(suscan_device_properties, new);

  return NULL;
}

SU_COPY_INSTANCER(suscan_device_properties)
{
  suscan_device_properties_t *new = NULL;
  const suscan_device_gain_desc_t *gain;
  suscan_device_gain_desc_t *new_gain;
  unsigned int i;

  SU_MAKE_FAIL(new, suscan_device_properties, self->label);

  SU_TRY_FAIL(strmap_copy(&new->traits, &self->traits));

  new->analyzer = self->analyzer;
  new->source   = self->source;
  new->uuid     = self->uuid;
  new->freq_min = self->freq_min;
  new->freq_max = self->freq_max;
  new->channels = self->channels;

  SU_ALLOCATE_MANY_FAIL(new->samp_rate_list, self->samp_rate_count, double);
  memcpy(
    new->samp_rate_list,
    self->samp_rate_list,
    self->samp_rate_count * sizeof(double));
  new->samp_rate_count = self->samp_rate_count;
  
  for (i = 0; i < self->gain_desc_count; ++i) {
    gain = self->gain_desc_list[i];
    SU_TRY_FAIL(
      new_gain = suscan_device_properties_make_gain(
        new,
        gain->name,
        gain->min,
        gain->max));

    new_gain->step = gain->step;
    new_gain->def  = gain->def;
  }

  for (i = 0; i < self->antenna_count; ++i)
    SU_TRY_FAIL(
      suscan_device_properties_add_antenna(new, self->antenna_list[i]));

  return new;

fail:
  if (new != NULL)
    SU_DISPOSE(suscan_device_properties, new);

  return NULL;
}

SU_COLLECTOR(suscan_device_properties)
{
  unsigned int i;

  if (self->label != NULL)
    free(self->label);

  if (self->samp_rate_list != NULL)
    free(self->samp_rate_list);

  for (i = 0; i < self->gain_desc_count; ++i)
    if (self->gain_desc_list[i] != NULL)
      suscan_device_gain_desc_destroy(self->gain_desc_list[i]);
  
  if (self->gain_desc_list != NULL)
    free(self->gain_desc_list);

  for (i = 0; i < self->antenna_count; ++i)
    if (self->antenna_list[i] != NULL)
      free(self->antenna_list[i]);
  
  if (self->antenna_list != NULL)
    free(self->antenna_list);
  
  free(self);
}

SU_METHOD(
  suscan_device_properties,
  void,
  swap,
  suscan_device_properties_t *properties)
{
  suscan_device_properties_t tmp;

  memcpy(&tmp, properties, sizeof(suscan_device_properties_t));
  memcpy(properties, self, sizeof(suscan_device_properties_t));
  memcpy(self, &tmp,       sizeof(suscan_device_properties_t));
}


/* Easy way to do this: match traits against each other */
SU_GETTER(suscan_device_properties, SUBOOL, match, const suscan_device_spec_t *spec)
{
  if (self->analyzer != NULL)
    return SU_FALSE;
  
  if (spec->source != NULL)
    return SU_FALSE;

  if (strcmp(self->analyzer->name, spec->analyzer) != 0)
    return SU_FALSE;
  
  if (strcmp(self->source->name, spec->source) != 0)
    return SU_FALSE;
  
  return strmap_equals(&self->traits, &spec->traits);
}

SU_GETTER(suscan_device_properties, suscan_device_spec_t *, make_spec)
{
  suscan_device_spec_t *new = NULL;

  if (self->analyzer == NULL) {
    SU_ERROR("Cannot make device spec: undefined analyzer\n");
    goto fail;
  }

  if (self->source == NULL) {
    SU_ERROR("Cannot make device spec: undefined signal source\n");
    goto fail;
  }

  SU_MAKE_FAIL(new, suscan_device_spec);

  new->epoch = self->epoch;

  SU_TRY_FAIL(suscan_device_spec_set_analyzer(new, self->analyzer->name));
  SU_TRY_FAIL(suscan_device_spec_set_source(new, self->source->name));
  SU_TRY_FAIL(suscan_device_spec_set_traits(new, &self->traits));
  
  /* All traits set, update UUID and return to user */
  suscan_device_spec_update_uuid(new);

  return new;

fail:
  if (new != NULL)
    SU_DISPOSE(suscan_device_spec, new);
  
  return NULL;
}

SU_GETTER(suscan_device_properties, uint64_t, uuid)
{
  return self->uuid;
}


SU_GETTER(suscan_device_properties, char *, uri)
{
  return suscan_device_make_uri(
    self->analyzer->name,
    self->source->name,
    &self->traits);  
}

SU_GETTER(suscan_device_properties, 
  suscan_device_gain_desc_t *, lookup_gain, const char *name)
{
  return hashlist_get(&self->gain_map, name);
}

SU_GETTER(
  suscan_device_properties,
  int, get_all_gains, suscan_device_gain_desc_t *const **gains)
{
  *gains = self->gain_desc_list;
  return self->gain_desc_count;
}

SU_GETTER(suscan_device_properties, const char *, get, const char *trait)
{
  return strmap_get(&self->traits, trait);
}

SU_METHOD(suscan_device_properties, SUBOOL, set_analyzer, const char *analyzer)
{
  const struct suscan_analyzer_interface *iface;

  iface = suscan_analyzer_interface_lookup(analyzer);
  if (iface == NULL) {
    SU_ERROR("Unrecognized analyzer interface `%s'\n", analyzer);
    return SU_FALSE;
  }

  self->analyzer = iface;

  return SU_TRUE;
}

SU_METHOD(suscan_device_properties, SUBOOL, set_source, const char *source)
{
  const struct suscan_source_interface *iface;

  if (self->analyzer == NULL) {
    SU_ERROR("Cannot set a source before setting the analyzer\n");
    return SU_FALSE;
  }

  iface = suscan_source_lookup(self->analyzer->name, source);
  if (iface == NULL) {
    SU_ERROR(
      "Unrecognized signal source type `%s' (analyzer = `%s')\n",
      source,
      self->analyzer->name);
    return SU_FALSE;
  }

  self->source = iface;

  return SU_TRUE;
}

SU_METHOD(suscan_device_properties, SUBOOL, set_label, const char *label)
{
  char *dup;
  SUBOOL ok = SU_FALSE;

  SU_TRY(dup = strdup(label));
  if (self->label != NULL)
    free(self->label);

  self->label = dup;

  ok = SU_TRUE;

done:
  return ok;
}

SU_METHOD(
  suscan_device_properties,
  SUBOOL, set_trait, const char *key, const char *val)
{
  return strmap_set(&self->traits, key, val);
}

SU_METHOD(suscan_device_properties, SUBOOL, add_antenna, const char *antenna)
{
  SUBOOL ok = SU_FALSE;
  char *dup;

  SU_TRY(dup = strdup(antenna));

  SU_TRYC(PTR_LIST_APPEND_CHECK(self->antenna, dup));
  dup = NULL;

  ok = SU_TRUE;

done:
  return ok;
}

SU_METHOD(suscan_device_properties,
  SUBOOL, add_gain, const suscan_device_gain_desc_t *desc)
{
  SUBOOL ok = SU_FALSE;
  suscan_device_gain_desc_t *new;

  SU_TRY(
    new = suscan_device_properties_make_gain(
      self,
      desc->name,
      desc->min,
      desc->max));

  new->step = desc->step;
  new->def  = desc->def;

  ok = SU_TRUE;

done:
  return ok;
}

SU_METHOD(suscan_device_properties,
  suscan_device_gain_desc_t *, make_gain, const char *name, SUFLOAT min, SUFLOAT max)
{
  suscan_device_gain_desc_t *new = NULL, *entry;

  SU_MAKE_FAIL(new, suscan_device_gain_desc, name, min, max);
  SU_TRYC_FAIL(PTR_LIST_APPEND_CHECK(self->gain_desc, new));
  entry = new;
  new = NULL;

  SU_TRY_FAIL(hashlist_set(&self->gain_map, name, entry));

  return entry;

fail:
  if (new != NULL)
    SU_DISPOSE(suscan_device_gain_desc, new);
  
  return NULL;
}

SU_METHOD(suscan_device_properties, SUBOOL, add_samp_rate, double rate)
{
  SUBOOL ok = SU_FALSE;
  double *tmp = NULL;

  SU_TRY(
    tmp = realloc(
      self->samp_rate_list,
      (self->samp_rate_count + 1) * sizeof(double)));
  
  tmp[self->samp_rate_count++] = rate;
  self->samp_rate_list = tmp;

  ok = SU_TRUE;

done:
  return ok;
}

SU_METHOD(suscan_device_properties, SUBOOL, set_freq_range, SUFREQ min, SUFREQ max)
{
  if (max < min)
    return SU_FALSE;
  
  self->freq_min = min;
  self->freq_max = max;

  return SU_TRUE;
}

SU_METHOD(suscan_device_properties, SUBOOL, set_num_channels, unsigned channels)
{
  if (channels < 1)
    return SU_FALSE;

  self->channels = channels;

  return SU_TRUE;
}

SU_METHOD(suscan_device_properties, SUBOOL, update_uuid)
{
  self->uuid = suscan_device_make_uuid(
    self->analyzer->name,
    self->source->name,
    &self->traits);

  return SU_TRUE;
}
