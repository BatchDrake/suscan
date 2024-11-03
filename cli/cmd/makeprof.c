/*

  Copyright (C) 2021 Gonzalo José Carracedo Carballal

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

#define SU_LOG_DOMAIN "cli-makeprof"

#include <analyzer/device/properties.h>
#include <analyzer/device/facade.h>
#include <analyzer/device/spec.h>
#include <analyzer/source.h>
#include <cli/cli.h>
#include <cli/cmds.h>
#include <unistd.h>
#include <util/confdb.h>
#include <inttypes.h>

#define SUSCLI_MAKEPROF_DEFAULT_FREQUENCY 433000000.

struct suscli_makeprof_ctx {
  const char *prefix;
  SUBOOL unavailable;
  SUBOOL warned;
  SUBOOL append_device;
  SUFREQ freq;
  PTR_LIST(suscan_source_config_t, profile);
};

SUPRIVATE SUBOOL
suscli_makeprof_ctx_register_device(
  struct suscli_makeprof_ctx *self,
  const suscan_device_properties_t *prop)
{
  suscan_source_config_t *prof = NULL;
  suscan_device_spec_t *spec = NULL;
  char *label = NULL;
  SUBOOL ok = SU_FALSE;

  if (self->append_device && self->prefix[0] != '\0') {
    SU_TRY(label = strbuild("%s - %s", self->prefix, prop->label));
  } else {
    SU_TRY(label = strdup(prop->label));
  }

  SU_TRY(spec = suscan_device_properties_make_spec(prop));
  SU_TRY(prof = suscan_source_config_new_default());

  SU_TRY(suscan_source_config_set_label(prof, label));
  SU_TRY(suscan_source_config_set_device_spec(prof, spec));

  suscan_source_config_set_freq(prof, self->freq);
  suscan_source_config_set_dc_remove(prof, SU_TRUE);

  if (prop->samp_rate_count > 0) {
    suscan_source_config_set_samp_rate(prof, prop->samp_rate_list[0]);
    suscan_source_config_set_bandwidth(
        prof,
        suscan_source_config_get_samp_rate(prof));
  }

  SU_TRYC(PTR_LIST_APPEND_CHECK(self->profile, prof));
  prof = NULL;

  ok = SU_TRUE;

done:
  if (label != NULL)
    free(label);

  if (prof != NULL)
    suscan_source_config_destroy(prof);

  if (spec != NULL)
    SU_DISPOSE(suscan_device_spec, spec);
  
  return ok;
}

SUPRIVATE SUBOOL
suscli_makeprof_ctx_make_all(struct suscli_makeprof_ctx *self)
{
  suscan_device_facade_t *facade = NULL;
  suscan_device_properties_t **prop_list = NULL;
  int i, count;
  SUBOOL ok = SU_FALSE;

  SU_TRY(facade = suscan_device_facade_instance());
  SU_TRYC(count = suscan_device_facade_get_all_devices(facade, &prop_list));

  for (i = 0; i < count; ++i)
    suscli_makeprof_ctx_register_device(self, prop_list[i]);

  ok = SU_TRUE;

done:
  for (i = 0; i < count; ++i)
    if (prop_list[i] != NULL)
      SU_DISPOSE(suscan_device_properties, prop_list[i]);
  if (prop_list != NULL)
    free(prop_list);

  return ok;
}

SUBOOL
suscli_makeprof_cb(const hashlist_t *params)
{
  SUBOOL ok = SU_FALSE;
  struct suscli_makeprof_ctx ctx;
  suscan_device_properties_t *prop = NULL;
  suscan_device_properties_t **prop_list = NULL;
  int count = 0;
  SUBOOL ask;
  int c;
  uint64_t uuid = SUSCAN_DEVICE_UUID_INVALID;
  unsigned int i;
  suscan_config_context_t *cfgctx = NULL;
  suscan_device_facade_t *facade = NULL;
  char *what;

  memset(&ctx, 0, sizeof(struct suscli_makeprof_ctx));

  SU_TRY(facade = suscan_device_facade_instance());

  SU_TRY(suscli_param_read_string(params, "prefix", &ctx.prefix, ""));
  SU_TRY(suscli_param_read_uuid(params, "device", &uuid, uuid));
  SU_TRY(suscli_param_read_bool(params, "ask", &ask, SU_TRUE));
  SU_TRY(suscli_param_read_double(
          params,
          "freq",
          &ctx.freq,
          SUSCLI_MAKEPROF_DEFAULT_FREQUENCY));

  suscan_device_facade_discover_all(facade);

  SU_INFO("Waiting for devices (2000 ms)...\n");
  while ((what = suscan_device_facade_wait_for_devices(facade, 2000)) != NULL) {
    free(what);
  }

  if (uuid != SUSCAN_DEVICE_UUID_INVALID) {
    prop = suscan_device_facade_get_device_by_uuid(facade, uuid);
    if (prop == NULL) {
      fprintf(stderr, "error: no device with uuid=%016" PRIx64 "\n", uuid);
      goto done;
    }

    suscli_makeprof_ctx_register_device(&ctx, prop);
  } else {
    SU_TRY(suscli_makeprof_ctx_make_all(&ctx));
  }

  if (ctx.profile_count == 0) {
    fprintf(stderr, "No devices eligible for profile generation were found\n");
  } else {
    if (ask) {
      fprintf(
          stderr,
          "You are about to generate %u profiles:\n\n",
          ctx.profile_count);

      for (i = 0; i < ctx.profile_count; ++i) {
        fprintf(
            stderr,
            " [%6s] %s\n",
            suscan_device_spec_analyzer(
              suscan_source_config_get_device_spec(
                ctx.profile_list[i])),
            suscan_source_config_get_label(ctx.profile_list[i]));
      }

      fprintf(stderr, "\nAre you sure? [y/N] ");

      c = getchar();

      if (c != 'y' && c != 'Y') {
        fprintf(stderr, "Cancelled by user.\n");
        ok = SU_TRUE;
        goto done;
      }
    }

    SU_TRY(suscan_config_context_lookup("sources"));

    for (i = 0; i < ctx.profile_count; ++i) {
      SU_TRY(suscan_source_config_register(ctx.profile_list[i]));
      ctx.profile_list[i] = NULL;
    }

    SU_TRY(suscan_confdb_save_all());

    fprintf(
        stderr,
        "Profiles saved. You can tweak individual settings by editing the "
        "%s file inside your personal Suscan config directory (usually "
        "~/.suscan/config)\n",
        suscan_config_context_get_save_file(cfgctx));
  }

  ok = SU_TRUE;

done:
  for (i = 0; i < ctx.profile_count; ++i)
    if (ctx.profile_list[i] != NULL)
      suscan_source_config_destroy(ctx.profile_list[i]);

  if (ctx.profile_list != NULL)
    free(ctx.profile_list);

  if (prop != NULL)
    SU_DISPOSE(suscan_device_properties, prop);
  
  for (i = 0; i < count; ++i)
    if (prop_list[i] != NULL)
      SU_DISPOSE(suscan_device_properties, prop_list[i]);
  if (prop_list != NULL)
    free(prop_list);

  return ok;
}
