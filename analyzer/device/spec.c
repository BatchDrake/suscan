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

#define SU_LOG_DOMAIN "device-spec"

#include "spec.h"
#include "properties.h"
#include "facade.h"
#include <string.h>
#include <stdarg.h>
#include <util/urlhelpers.h>
#include <sigutils/log.h>

/*
 * In local analyzers, "source" is whether it is soapysdr, file, tonegen...
 * In remote analyzers, "source" is host and port
 */
char *
suscan_device_make_uri(
  const char *analyzer, const char *source, strmap_t const *traits)
{
  char **keys;
  char *result = NULL;
  const char *entry;
  char *urlkey = NULL, *urlval = NULL;
  unsigned int i = 0, n = 0;

  SU_TRY(keys = strmap_keys(traits));
  SU_TRY(result = strappend(result, "%s://%s/", analyzer, source));

  if ((entry = strmap_get(traits, "device")) != NULL)
    SU_TRY(result = strappend(result, "%s", entry));

  while (keys[i] != NULL) {
    if (strcmp(keys[i], "device") != 0) {
      entry = strmap_get(traits, keys[i]);

      SU_TRY(urlkey = suscan_urlencode(keys[i]));
      SU_TRY(urlval = suscan_urlencode(entry));

      SU_TRY(result = strappend(result, "%c%s=%s", n == 0 ? '?' : '&', urlkey, urlval));
      free(urlkey);
      free(urlval);

      urlkey = NULL;
      urlval = NULL;
      ++n;
    }

    ++i;
  }

done:
  if (urlkey != NULL)
    free(urlkey);

  if (urlval != NULL)
    free(urlval);

  if (keys != NULL)
    free(keys);

  return result;
}

uint64_t
suscan_device_make_uuid(
  const char *analyzer, const char *source, strmap_t const *traits)
{
  uint64_t hash = SUSCAN_DEVICE_UUID_INVALID;
  char *result = NULL;

  if (analyzer == NULL || source == NULL) {
    SU_ERROR("Cannot make UUID: analyzer = %p, source = %p\n", analyzer, source);
    goto done;
  }
  
  SU_TRY(result = suscan_device_make_uri(analyzer, source, traits));

  hash = murmur_hash_64(result, strlen(result), 0x5005cafacadeull);

done:
  if (result != NULL)
    free(result);
  
  return hash;
}

SU_INSTANCER(suscan_device_spec)
{
  suscan_device_spec_t *new = NULL;

  SU_ALLOCATE_FAIL(new, suscan_device_spec_t);

  SU_CONSTRUCT_FAIL(strmap, &new->traits);
  SU_CONSTRUCT_FAIL(strmap, &new->params);
  SU_TRY_FAIL(new->analyzer = strdup("local"));
  SU_TRY_FAIL(new->source = strdup("soapysdr"));
  
  new->uuid = SUSCAN_DEVICE_UUID_INVALID;
  
  return new;

fail:
  if (new != NULL)
    SU_DISPOSE(suscan_device_spec, new);

  return NULL;
}

SU_DESTRUCTOR(suscan_device_spec)
{
  SU_DESTRUCT(strmap, &self->traits);
  SU_DESTRUCT(strmap, &self->params);

  if (self->analyzer != NULL)
    free(self->analyzer);

  if (self->source != NULL)
    free(self->source);

  if (self->properties != NULL)
    SU_DISPOSE(suscan_device_properties, self->properties);
}

SU_COLLECTOR(suscan_device_spec)
{
  SU_DESTRUCT(suscan_device_spec, self);
  free(self);
}

SUSCAN_SERIALIZER_PROTO(suscan_device_spec)
{
  SUSCAN_PACK_BOILERPLATE_START;
  char *uri = NULL;

  SU_TRY_FAIL(uri = suscan_device_spec_to_uri(self));

  SUSCAN_PACK(str,    uri);
  SU_TRY_FAIL(strmap_serialize(&self->params, buffer));
  
  SUSCAN_PACK_BOILERPLATE_FINALLY;

  if (uri != NULL)
    free(uri);
  
  SUSCAN_PACK_BOILERPLATE_RETURN;
}

SUSCAN_DESERIALIZER_PROTO(suscan_device_spec)
{
  SUSCAN_UNPACK_BOILERPLATE_START;
  suscan_device_spec_t *tmp = NULL;
  char *uri = NULL;

  SUSCAN_UNPACK(str, uri);
  
  SU_TRY_FAIL(tmp = suscan_device_spec_from_uri(uri));
  SU_TRY_FAIL(strmap_deserialize(&self->params, buffer));

  suscan_device_spec_update_uuid(tmp);
  suscan_device_spec_swap(tmp, self);

  SUSCAN_UNPACK_BOILERPLATE_FINALLY;

  if (uri != NULL)
    free(uri);

  SU_DISPOSE(suscan_device_spec, tmp);

  SUSCAN_UNPACK_BOILERPLATE_RETURN;
}

SU_GETTER(suscan_device_spec, suscan_device_spec_t *, copy)
{
  suscan_device_spec_t *new = NULL;

  SU_MAKE_FAIL(new, suscan_device_spec);

  SU_TRY_FAIL(suscan_device_spec_set_analyzer(new, self->analyzer));
  SU_TRY_FAIL(suscan_device_spec_set_source(new,   self->source));
  SU_TRY_FAIL(suscan_device_spec_set_traits(new , &self->traits));
  SU_TRY_FAIL(suscan_device_spec_set_params(new,  &self->params));

  return new;

fail:
  if (new != NULL)
    SU_DISPOSE(suscan_device_spec, new);

  return NULL;
}

SU_GETTER(suscan_device_spec, struct suscan_device_properties *, properties)
{
  int epoch;
  suscan_device_spec_t *mutable = (suscan_device_spec_t *) self;
  suscan_device_facade_t *facade = suscan_device_facade_instance();

  /* TODO: Ask FACADE about the latest version of the properties, if they exist */
  /* If properties are null or old, refresh */

  if (facade == NULL)
    goto done;

  if (self->uuid == SUSCAN_DEVICE_UUID_INVALID)
    mutable->uuid = suscan_device_spec_uuid(self);

  SU_INFO("Self UUID: %d\n", self->uuid);
  
  epoch = suscan_device_facade_get_epoch_for_uuid(facade, self->uuid);

  /* Properties are up to date */
  if (mutable->properties != NULL && self->properties->epoch == epoch)
    goto done;
  
  /* Changes found, discard current version */
  if (self->properties != NULL) {
    SU_INFO("Found properties, discarding...\n");
    SU_DISPOSE(suscan_device_properties, self->properties);
    mutable->properties = NULL;
  }
  
  /* Also, a new version of the properties has been found */
  if (self->epoch <= epoch) {
    mutable->properties = suscan_device_facade_get_properties(facade, self);
    mutable->epoch = epoch;
  } else {
    SU_INFO("Discarding properties. Self epoch is %d, curr epoch is %d\n", self->epoch, epoch);
  }
  
done:
  return self->properties;
}

SU_GETTER(suscan_device_spec, strmap_t *, make_args)
{
  strmap_t *new = NULL;

  SU_MAKE_FAIL(new, strmap);

  SU_TRY_FAIL(strmap_assign(new, &self->traits));
  SU_TRY_FAIL(strmap_assign(new, &self->params));

  return new;

fail:
  if (new != NULL)
    SU_DESTRUCT(strmap, new);

  return new;
}

SU_GETTER(suscan_device_spec, const char *, analyzer)
{
  return self->analyzer;
}

SU_GETTER(suscan_device_spec, const char *, source)
{
  return self->source;
}

SU_GETTER(suscan_device_spec, const char *, get, const char *key)
{
  const char *result;

  if ((result = strmap_get(&self->params, key)) == NULL)
    result = strmap_get(&self->traits, key);

  return result;
}

SU_GETTER(suscan_device_spec, uint64_t, uuid)
{
  return suscan_device_make_uuid(self->analyzer, self->source, &self->traits);
}

SU_GETTER(suscan_device_spec, char *, to_uri)
{
  return suscan_device_make_uri(self->analyzer, self->source, &self->traits);
}

SU_GETTER(suscan_device_spec, suscan_object_t *, to_object)
{
  suscan_object_t *obj    = NULL;
  suscan_object_t *params = NULL;
  strmap_iterator_t it;
  char *uri = NULL;
  SUBOOL ok = SU_FALSE;

  SU_MAKE(obj, suscan_object, SUSCAN_OBJECT_TYPE_OBJECT);
  SU_MAKE(params, suscan_object, SUSCAN_OBJECT_TYPE_OBJECT);

  SU_TRY(uri = suscan_device_spec_to_uri(self));
  SU_TRY(suscan_object_set_field_value(obj, "uri", uri));

  it = strmap_begin(&self->params);
  while (!strmap_iterator_end(&it)) {
    if (it.value != NULL)
      SU_TRY(suscan_object_set_field_value(params, it.name, it.value));
    strmap_iterator_advance(&it);
  }

  SU_TRY(suscan_object_set_field(obj, "params", params));
  params = NULL;

  ok = SU_TRUE;

done:
  if (!ok && obj != NULL) {
    SU_DISPOSE(suscan_object, obj);
    obj = NULL;
  }
  
  if (params != NULL)
    SU_DISPOSE(suscan_object, params);
  
  if (uri != NULL)
    free(uri);
  
  return obj;
}

SU_GETTER(suscan_device_spec, strmap_t *, get_all)
{
  strmap_t *map = NULL;
  strmap_iterator_t it;

  SU_MAKE_FAIL(map, strmap);

  it = strmap_begin(&self->traits);
  while (!strmap_iterator_end(&it)) {
    if (it.value != NULL)
      SU_TRY_FAIL(strmap_set(map, it.name, it.value));
    strmap_iterator_advance(&it);
  }

  it = strmap_begin(&self->params);
  while (!strmap_iterator_end(&it)) {
    if (it.value != NULL)
      SU_TRY_FAIL(strmap_set(map, it.name, it.value));
    strmap_iterator_advance(&it);
  }

  return map;

fail:
  if (map != NULL)
    SU_DISPOSE(strmap, map);

  return map;
}

suscan_device_spec_t *
suscan_device_spec_from_uri(const char *uri)
{
  suscan_device_spec_t *spec = NULL;
  suscan_device_spec_t *ret = NULL;
  strmap_t *traits = NULL;

  char *uri_dup = NULL;

  char *p_source;
  char *p_device;
  char *p_traits;
  char *p_key;
  char *p_val;
  char *decoded_key = NULL;
  char *decoded_val = NULL;

  SU_TRY(uri_dup = strdup(uri));

  if ((p_source = strstr(uri_dup, "://")) == NULL) {
    SU_ERROR("Malformed URI: no analyzer / source suscan_local_analyzer_set_psd_samp_rate_overridable\n");
    goto done;
  }
  *p_source = '\0';
  p_source += 3;

  if ((p_device = strchr(p_source, '/')) == NULL) {
    SU_ERROR("Malformed URI: no device traits separator\n");
    goto done;
  }
  *p_device++ = '\0';
  
  if ((p_traits = strchr(p_device, '?')) != NULL)
    *p_traits++ = '\0';
  
  SU_MAKE(traits, strmap);

  if (*p_device != '\0')
    SU_TRY(strmap_set(traits, "device", p_device));

  while (p_traits != NULL) {
    p_val = strchr(p_traits, '=');
    p_key = strchr(p_traits, '&'); /* NOT the current key, the next! */
    
    if (p_val == NULL) {
      SU_ERROR("Malformed URI: non-keyval traits are not allowed\n");
      goto done;
    }

    *p_val++ = '\0';

    if (p_key != NULL)
      *p_key++ = '\0';

    /* Already splitted, let us save this trait */
    if (strcmp(p_traits, "device") == 0) {
      SU_ERROR("Malformed URI: device trait cannot be explicitly set in the traits list\n");
      goto done;
    }
    
    SU_TRY(decoded_key = suscan_urldecode(p_traits));
    SU_TRY(decoded_val = suscan_urldecode(p_val));

    SU_TRY(strmap_set(traits, decoded_key, decoded_val));

    free(decoded_key);
    free(decoded_val);

    decoded_key = NULL;
    decoded_val = NULL;

    p_traits = p_key;
  }

  /* We now have the bare minimum! Let's go ahead and allocate spec */
  SU_MAKE(spec, suscan_device_spec);

  SU_TRY(suscan_device_spec_set_analyzer(spec, uri_dup));
  SU_TRY(suscan_device_spec_set_source(spec, p_source));
  SU_TRY(suscan_device_spec_set_traits(spec, traits));
  
  /* All traits set, update UUID and return to user */
  suscan_device_spec_update_uuid(spec);

  ret = spec;
  spec = NULL;

done:
  if (uri_dup != NULL)
    free(uri_dup);

  if (decoded_key != NULL)
    free(decoded_key);

  if (decoded_val != NULL)
    free(decoded_val);
  
  if (spec != NULL)
    SU_DISPOSE(suscan_device_spec, spec);
  
  if (traits != NULL)
    SU_DISPOSE(strmap, traits);
  
  return ret;
}

suscan_device_spec_t *
suscan_device_spec_from_object(const suscan_object_t *obj)
{
  suscan_device_spec_t *spec = NULL;
  const suscan_object_t *params = NULL;
  const suscan_object_t *entry = NULL;
  const char *key, *val;
  unsigned int i, count;
  const char *uri = suscan_object_get_field_value(obj, "uri");

  SU_TRY_FAIL(spec = suscan_device_spec_from_uri(uri));

  if ((params = suscan_object_get_field(obj, "params")) != NULL
    && suscan_object_get_type(params) == SUSCAN_OBJECT_TYPE_OBJECT) {
    count = suscan_object_field_count(params);
    for (i = 0; i < count; ++i) {
      SU_TRY_FAIL(entry = suscan_object_get_field_by_index(params, i));
      SU_TRY_FAIL(key = suscan_object_get_name(entry));
      SU_TRY_FAIL(val = suscan_object_get_value(entry));
      SU_TRY_FAIL(suscan_device_spec_set(spec, key, val));
    }
  }

  return spec;

fail:
  if (spec != NULL)
    SU_DISPOSE(suscan_device_spec, spec);

  return NULL;
}

SU_METHOD(suscan_device_spec, void, swap, suscan_device_spec_t *spec)
{
  suscan_device_spec_t tmp;

  memcpy(&tmp, spec, sizeof(suscan_device_spec_t));
  memcpy(spec, self, sizeof(suscan_device_spec_t));
  memcpy(self, &tmp, sizeof(suscan_device_spec_t));

  strmap_notify_move(&self->traits);
  strmap_notify_move(&self->params);

  strmap_notify_move(&spec->traits);
  strmap_notify_move(&spec->params);
}

/* Used to handle the user-specific tweaks */
SU_METHOD(suscan_device_spec, void, reset)
{
  strmap_clear(&self->params);

  if (self->properties != NULL) {
    SU_DISPOSE(suscan_device_properties, self->properties);
    self->epoch = 0;
  }
}

SU_METHOD(suscan_device_spec, SUBOOL, set, const char *key, const char *val)
{
  return strmap_set(&self->params, key, val);
}

SU_METHOD(suscan_device_spec, void, update_uuid)
{
  suscan_device_spec_reset(self);
  self->uuid = suscan_device_spec_uuid(self);
}

SU_METHOD(suscan_device_spec, SUBOOL, set_analyzer, const char *analyzer)
{
  char *dup;

  SU_TRYCATCH(dup = strdup(analyzer), return SU_FALSE);

  free(self->analyzer);
  self->analyzer = dup;
  self->uuid = SUSCAN_DEVICE_UUID_INVALID;

  return SU_TRUE;
}

SU_METHOD(suscan_device_spec, SUBOOL, set_source, const char *source)
{
  char *dup;

  SU_TRYCATCH(dup = strdup(source), return SU_FALSE);

  free(self->source);
  self->source = dup;
  self->uuid = SUSCAN_DEVICE_UUID_INVALID;

  return SU_TRUE;
}

SU_METHOD(suscan_device_spec, SUBOOL, set_traits, const strmap_t *traits)
{
  if (!strmap_copy(&self->traits, traits))
    return SU_FALSE;

  self->uuid = SUSCAN_DEVICE_UUID_INVALID;
  
  return SU_TRUE;
}

SU_METHOD(suscan_device_spec, SUBOOL, set_params, const strmap_t *params)
{
  return strmap_copy(&self->params, params);
}
