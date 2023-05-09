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

#define SU_LOG_DOMAIN "log"

#include <sigutils/log.h>
#include <sigutils/util/compat-time.h>
#include <cli/cli.h>

SUPRIVATE SUBOOL su_log_cr = SU_TRUE;

SUPRIVATE void
print_date(void)
{
  time_t t;
  struct tm tm;
  char mytime[50];

  time(&t);
  localtime_r(&t, &tm);

  strftime(mytime, sizeof(mytime), "%d %b %Y - %H:%M:%S", &tm);

  fprintf(stderr, "%s", mytime);
}

SUPRIVATE void
su_log_func(void *private, const struct sigutils_log_message *msg)
{
  SUBOOL *cr = (SUBOOL *) private;
  SUBOOL is_except;
  size_t msglen;

  if (*cr) {
    switch (msg->severity) {
      case SU_LOG_SEVERITY_DEBUG:
        fprintf(stderr, "\033[1;30m");
        print_date();
        fprintf(stderr, " - debug: ");
        break;

      case SU_LOG_SEVERITY_INFO:
        print_date();
        fprintf(stderr, " - ");
        break;

      case SU_LOG_SEVERITY_WARNING:
        print_date();
        fprintf(stderr, " - \033[1;33mwarning [%s]\033[0m: ", msg->domain);
        break;

      case SU_LOG_SEVERITY_ERROR:
        print_date();

        is_except = 
             strstr(msg->message, "exception in \"") != NULL
          || strstr(msg->message, "failed to create instance") != NULL;

        if (is_except)
          fprintf(stderr, "\033[1;30m   ");
        else
          fprintf(stderr, " - \033[1;31merror   [%s]\033[0;1m: ", msg->domain);
        
        break;

      case SU_LOG_SEVERITY_CRITICAL:
        print_date();
        fprintf(stderr, 
            " - \033[1;37;41mcritical[%s] in %s:%u\033[0m: ",
            msg->domain,
            msg->function,
            msg->line);
        break;
    }
  }

  msglen = strlen(msg->message);

  *cr = msg->message[msglen - 1] == '\n' || msg->message[msglen - 1] == '\r';

  fputs(msg->message, stderr);

  if (*cr)
    fputs("\033[0m", stderr);
}

/* Log config */
SUPRIVATE struct sigutils_log_config g_log_config =
{
  &su_log_cr, /* private */
  SU_TRUE, /* exclusive */
  su_log_func, /* log_func */
};

void
suscli_log_init(void)
{
  su_log_init(&g_log_config);
}
