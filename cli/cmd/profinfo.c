/*

  Copyright (C) 2020 Gonzalo José Carracedo Carballal

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

#define SU_LOG_DOMAIN "cli-profinfo"

#include <sigutils/log.h>
#include <analyzer/source.h>

#include <cli/cli.h>
#include <cli/cmds.h>

SUPRIVATE SUBOOL
suscli_profinfo_gain_cb(void *privdata, const char *name, SUFLOAT value)
{
  printf("    %s = %lg dB\n", name, value);
  return SU_TRUE;
}

SUBOOL
suscli_profinfo_cb(const hashlist_t *params)
{
  SUBOOL ok = SU_FALSE;
  const char *name;
  suscan_source_config_t *profile = NULL;

  SU_TRYCATCH(
      suscli_param_read_profile(params, "profile", &profile),
      goto fail);

  name = suscan_source_config_get_label(profile);

  if (name != NULL) {
    printf("Profile:     \"%s\"\n", name);
  } else {
    printf("(Unnamed profile)\n");
  }

  printf("----------------------------\n");

  printf(
      "Frequency:   %.0lf Hz\n",
      suscan_source_config_get_freq(profile));
  printf(
      "LNB:         %.0lf Hz\n",
      suscan_source_config_get_lnb_freq(profile));
  printf(
      "Sample rate: %u sps\n",
      suscan_source_config_get_samp_rate(profile));
  printf(
      "Decimation:  %u\n",
      suscan_source_config_get_average(profile));

  if (strcmp(suscan_source_config_get_type(profile), "file") != 0) {
    printf("Type:        %s\n", suscan_source_config_get_type(profile));
    printf(
        "Channel:     %u\n",
        suscan_source_config_get_channel(profile));
    printf(
        "Bandwidth:   %.0lf Hz\n",
        suscan_source_config_get_bandwidth(profile));
    printf(
        "Antenna:     %s\n",
        suscan_source_config_get_antenna(profile) == NULL
        ? "(none)"
        : suscan_source_config_get_antenna(profile));
    printf(
        "I/Q Balance: %s\n",
        suscan_source_config_get_iq_balance(profile) ? "yes" : "no");

    printf("Gains:\n");
    suscan_source_config_walk_gains(
        profile,
        suscli_profinfo_gain_cb,
        NULL);
  } else {
    printf("Type:        file\n");
    printf("Format:      ");
    switch (suscan_source_config_get_format(profile)) {
      case SUSCAN_SOURCE_FORMAT_AUTO:
        printf("Automatic\n");
        break;
      case SUSCAN_SOURCE_FORMAT_RAW_FLOAT32:
        printf("Raw I/Q samples (complex float32)\n");
        break;
      case SUSCAN_SOURCE_FORMAT_RAW_UNSIGNED8:
        printf("Raw I/Q samples (complex uint8)\n");
        break;
      case SUSCAN_SOURCE_FORMAT_WAV:
        printf("WAV file\n");
        break;
      default:
        printf("Unknown\n");
    }
    printf(
        "Path:        %s\n",
        suscan_source_config_get_path(profile) == NULL
        ? "(unspecified)"
        : suscan_source_config_get_path(profile));
    printf(
        "Loop:        %s\n",
        suscan_source_config_get_loop(profile) ? "yes" : "no");
  }

  ok = SU_TRUE;

fail:
  return ok;
}
