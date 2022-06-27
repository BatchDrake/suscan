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

#include <analyzer/source.h>
#include <cli/cli.h>
#include <cli/cmds.h>
#include <unistd.h>
#include <util/confdb.h>

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
suscli_device_register_cb(
    const suscan_source_device_t *dev,
    unsigned int ndx,
    void *userdata)
{
  struct suscli_makeprof_ctx *ctx = (struct suscli_makeprof_ctx *) userdata;
  suscan_source_config_t *prof = NULL;
  char *label = NULL;
  struct suscan_source_device_info info = suscan_source_device_info_INITIALIZER;
  SUBOOL ok = SU_FALSE;

  if (!ctx->unavailable && !suscan_source_device_is_available(dev)) {
    if (!ctx->warned) {
      ctx->warned = SU_TRUE;
      fprintf(
          stderr,
          "Skipping unavailable devices as requested. Pass unavailable=true to override.\n");
    }

    return SU_TRUE;
  }

  if (ctx->append_device) {
    if (ctx->prefix[0] == '\0') {
      SU_TRYCATCH(
          label = strdup(suscan_source_device_get_desc(dev)),
          goto done);
    } else {
      SU_TRYCATCH(
          label = strbuild(
              "%s - %s",
              ctx->prefix,
              suscan_source_device_get_desc(dev)),
          goto done);
    }
  } else {
    SU_TRYCATCH(
        label = strdup(suscan_source_device_get_desc(dev)),
        goto done);
  }

  (void) suscan_source_device_get_info(dev, 0, &info);

  SU_TRYCATCH(
      prof = suscan_source_config_new(
          SUSCAN_SOURCE_TYPE_SDR,
          SUSCAN_SOURCE_FORMAT_AUTO),
      goto done);

  SU_TRYCATCH(suscan_source_config_set_label(prof, label), goto done);
  SU_TRYCATCH(suscan_source_config_set_device(prof, dev), goto done);

  suscan_source_config_set_freq(prof, ctx->freq);
  suscan_source_config_set_dc_remove(prof, SU_TRUE);

  if (info.samp_rate_count > 0) {
    suscan_source_config_set_samp_rate(prof, info.samp_rate_list[0]);
    suscan_source_config_set_bandwidth(
        prof,
        suscan_source_config_get_samp_rate(prof));
  }


  SU_TRYCATCH(PTR_LIST_APPEND_CHECK(ctx->profile, prof) != -1, goto done);
  prof = NULL;

  ok = SU_TRUE;

done:
  if (label != NULL)
    free(label);

  if (prof != NULL)
    suscan_source_config_destroy(prof);

  suscan_source_device_info_finalize(&info);

  return ok;
}

SUBOOL
suscli_makeprof_cb(const hashlist_t *params)
{
  SUBOOL ok = SU_FALSE;
  struct suscli_makeprof_ctx ctx;
  const suscan_source_device_t *dev = NULL;
  SUBOOL ask;
  int c;
  int index;
  unsigned int i;
  suscan_config_context_t *cfgctx = NULL;

  memset(&ctx, 0, sizeof(struct suscli_makeprof_ctx));

  SU_TRYCATCH(
      suscli_param_read_string(params, "prefix", &ctx.prefix, ""),
      goto done);

  SU_TRYCATCH(
      suscli_param_read_int(params, "device", &index, -1),
      goto done);

  SU_TRYCATCH(
      suscli_param_read_bool(params, "ask", &ask, SU_TRUE),
      goto done);

  SU_TRYCATCH(
      suscli_param_read_double(
          params,
          "freq",
          &ctx.freq,
          SUSCLI_MAKEPROF_DEFAULT_FREQUENCY),
      goto done);

  SU_TRYCATCH(
      suscli_param_read_bool(params, "unavailable", &ctx.unavailable, SU_FALSE),
      goto done);

  ctx.append_device = index == -1;

  if (getenv("SUSCAN_DISCOVERY_IF") != NULL) {
    fprintf(
        stderr,
        "Leaving 2 seconds grace period to allow remote devices to be discovered\n\n");
    sleep(2);
  }

  if (index >= 0) {
    ctx.unavailable = SU_TRUE;

    dev = suscan_source_device_get_by_index((unsigned int) index);
    if (dev == NULL) {
      fprintf(stderr, "error: no device with index=%d\n", index);
      goto done;
    }

    SU_TRYCATCH(
        suscli_device_register_cb(dev, (unsigned int) index, &ctx),
        goto done);

  } else {
    SU_TRYCATCH(
        suscan_source_device_walk(suscli_device_register_cb, &ctx),
        goto done);
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
            " - %s\n",
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

    SU_TRYCATCH(
        cfgctx = suscan_config_context_lookup("sources"),
        goto done);

    for (i = 0; i < ctx.profile_count; ++i) {
      SU_TRYCATCH(
          suscan_source_config_register(ctx.profile_list[i]),
          goto done);
      ctx.profile_list[i] = NULL;
    }

    SU_TRYCATCH(suscan_confdb_save_all(), goto done);

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

  return ok;
}
