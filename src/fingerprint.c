/*

  Copyright (C) 2017 Gonzalo José Carracedo Carballal

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation, either version 3 of the
  License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this program.  If not, see
  <http://www.gnu.org/licenses/>

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <getopt.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/select.h>

#include "suscan.h"

#define SUSCAN_SKIP_CHANNELS 10

SUBOOL
suscan_perform_fingerprint(struct suscan_source_config *config)
{
  struct suscan_mq mq;
  void *private;
  uint32_t type;
  suscan_analyzer_t *analyzer = NULL;
  const struct suscan_analyzer_channel_msg *ch_msg;
  const struct suscan_analyzer_status_msg  *st_msg;
  unsigned int chskip = SUSCAN_SKIP_CHANNELS;
  unsigned int i;

  SUBOOL running = SU_TRUE;
  SUBOOL ok = SU_FALSE;

  if (!suscan_mq_init(&mq))
    return SU_FALSE;

  SU_TRYCATCH(analyzer = suscan_analyzer_new(config, &mq), goto done);

  while (running) {
    private = suscan_analyzer_read(analyzer, &type);

    switch (type) {
      case SUSCAN_ANALYZER_MESSAGE_TYPE_CHANNEL:
        ch_msg = (struct suscan_analyzer_channel_msg *) private;
        if (chskip > 0) {
          --chskip;
        } else {
          suscan_channel_list_sort(ch_msg->channel_list, ch_msg->channel_count);

          for (i = 0; i < ch_msg->channel_count; ++i)
            if (!suscan_channel_is_dc(ch_msg->channel_list[i]))
              SU_INFO(
                  "%2d. | %+8.1lf Hz | %7.1lf (%7.1lf) Hz | %5.1lf dB\n",
                  i + 1,
                  ch_msg->channel_list[i]->fc,
                  ch_msg->channel_list[i]->bw,
                  ch_msg->channel_list[i]->f_hi - ch_msg->channel_list[i]->f_lo,
                  ch_msg->channel_list[i]->snr);

          running = SU_FALSE;

        }
        break;

      case SUSCAN_ANALYZER_MESSAGE_TYPE_EOS:
        st_msg = (struct suscan_analyzer_status_msg *) private;

        if (st_msg->err_msg != NULL)
          SU_WARNING("End of stream: %s\n", st_msg->err_msg);
        else
          SU_WARNING("Unexpected end of stream\n");

        running = SU_FALSE;
        break;
    }

    suscan_analyzer_dispose_message(type, private);
  }

  ok = SU_TRUE;

done:
  if (analyzer != NULL)
    suscan_analyzer_destroy(analyzer);

  suscan_mq_finalize(&mq);

  return ok;
}
