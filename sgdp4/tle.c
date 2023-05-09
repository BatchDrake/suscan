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

#define SU_LOG_DOMAIN "tle"

#include <sigutils/util/compat-stdlib.h>
#include "sgdp4.h"
#include <sigutils/log.h>
#include <ctype.h>
#include <fcntl.h>
#include <sigutils/util/compat-stat.h>
#include <sys/types.h>
#include <sigutils/util/compat-mman.h>
#include <sigutils/util/compat-time.h>
#include <inttypes.h>


#define SUSCAN_TLE_LINE_LEN 69

SUPRIVATE unsigned
su_orbit_tle_line_checksum(const char *linebuf)
{
  int i;
  unsigned int sum = 0;

  for (i = 0; i < SUSCAN_TLE_LINE_LEN - 1; ++i)
    if (isdigit(linebuf[i]))
      sum += linebuf[i] - '0';
    else if (linebuf[i] == '-')
      ++sum;

  return sum % 10;
}

/*
 * Before you put your eyes in this function, I would like you to tell that
 * although I am generally a big fan of C due to its expresiveness, 
 * simplicity and access to the memory structure of my program, things like
 * scanf make me reconsider my feelings. [fsv]scanf does many things I hate,
 * in particular: mimic the behavior of printf formats only partially AND
 * attempting to be smarter than the programmer. In an ideal world, a printf
 * generated string with width specifiers should be parsed back by scanf
 * without loss of information. However, as scanf believes the programmer is
 * dumb and lazy, it will interpret spaces (0x20) not as a regular format
 * character, but as an arbitrary number of spaces (in the isspace sense)
 * before the next field is found. This means that if I specify something
 * like %4u%1u and the string is " 2971", it will not read 297 and 1, but
 * 2971 and fail to read the %1d.
 * 
 * The good and bad thing of C is that is old. Very old. And while programmers
 * face well-documented problems whose approach is unambiguously standarized,
 * sometimes they have to deal with APIs that were designed for people that
 * dealt with a slow wardrobe-sized power-consuming barely digital computer
 * through an even slower teletype that printed command outputs to continuous
 * form paper. No wonder why nobody wanted to spend more time doing things
 * right. It is 2021 now, and the chickens have come home to roost.
 */
SUPRIVATE SUBOOL
su_orbit_parse_tle_line(orbit_t *self, unsigned int num, const char *linebuf)
{
  SUDOUBLE fields[5];
  unsigned int checksum, line, catalog, tlenum, epoch, ecc;
  int mmdotdot, mmdotdotexp, dragterm, dragtermexp, ephtype;
  char classification, designator[16];
  char str_revol[8];
  int n;
  char linebufcpy[SUSCAN_TLE_LINE_LEN + 1];

  switch (num) {
    case 0:
      /* This is the title line. Used to identify the spacecraft */
      n = strlen(linebuf);

      while (n > 0 && isspace(linebuf[n - 1]))
        --n;
      
      SU_TRYCATCH(self->name = malloc(n + 1), return SU_FALSE);

      memcpy(self->name, linebuf, n);
      self->name[n] = '\0';
      break;

    case 1:  
      /* 
       * This line is mostly informative, tells you information about
       * the epoch this TLE refers to, and it is useful to verify
       * whether the TLE is too old.
       */
      n = sscanf(
        linebuf,
        "%1u %5u%c %8[A-Z0-9 ] %2u%12lf %10lf %6d%2d %6d%2d %1d %5u",
        &line,
        &catalog,
        &classification,
        designator,
        &epoch,
        fields + 0,
        fields + 1,
        &mmdotdot,
        &mmdotdotexp,
        &dragterm,
        &dragtermexp,
        &ephtype,
        &tlenum);
      
      if (n != 13) {
        SU_ERROR("Malformed line 1 of TLE\n");
        return SU_FALSE;
      }

      if (line != num) {
        SU_ERROR("Unexpected line number\n");
        return SU_FALSE;
      }

      /* Fix and verify checksum */
      checksum = tlenum % 10;
      tlenum  /= 10;

      if (su_orbit_tle_line_checksum(linebuf) != checksum) {
        SU_ERROR("Line 1: bad checksum\n");
        return SU_FALSE;
      }

      /* Populate */
      self->ep_year  = epoch < 57 ? epoch + 2000 : epoch + 1900;
      self->ep_day   = fields[0];
      self->bstar    = dragterm * 1e-5 * pow(10., dragtermexp);
      self->drevdt   = fields[1];
      self->d2revdt2 = mmdotdot * 1e-5 * pow(10., mmdotdotexp);

      break;

    case 2:
      /* 
       * This is the most critical line. It contains the orbital
       * elements that fully describe the orbit in the current epoch
       */

      n = strlen(linebuf);
      memcpy(linebufcpy, linebuf, n);

      if (n >= 52 && linebufcpy[52] == ' ')
        linebufcpy[52] = '0';

      n = sscanf(
        linebuf,
        "%1u %05u %8lf %8lf %07u %8lf %8lf %11lf%5[0-9 ]%1u",
        &line,
        &catalog,
        fields + 0,
        fields + 1,
        &ecc,
        fields + 2,
        fields + 3,
        fields + 4,
        str_revol,
        &checksum);
      
      if (n != 10) {
        SU_ERROR("Malformed line 2 of TLE\n");
        return SU_FALSE;
      }

      if (line != num) {
        SU_ERROR("Unexpected line number\n");
        return SU_FALSE;
      }

      /* Fix verify checksum */
      if (su_orbit_tle_line_checksum(linebuf) != checksum) {
        SU_ERROR(
          "Line 2: bad TLE checksum (%d computed, %d expected)\n",
          su_orbit_tle_line_checksum(linebuf), checksum);
        return SU_FALSE;
      }

      /* Everything looks sane, populate */
      if (sscanf(str_revol, "%5" SCNd64, &self->norb) != 1) {
        SU_ERROR("Line 2: bad TLE revolution number\n");
        return SU_FALSE;
      }

      /* The 6 orbital elements: */
      self->eqinc  = SU_DEG2RAD(fields[0]); /* i */
      self->ascn   = SU_DEG2RAD(fields[1]); /* \Omega */
      self->ecc    = ecc * 1e-7;            /* e */
      self->argp   = SU_DEG2RAD(fields[2]); /* \omega */
      self->mnan   = SU_DEG2RAD(fields[3]); /* M */
      self->rev    = fields[4]; /* n */
      break;
  }

  return SU_TRUE;
}

SUBOOL
orbit_copy(orbit_t *dest, const orbit_t *orig)
{
  SUBOOL ok = SU_FALSE;

  *dest = *orig;

  if (orig->name != NULL)
    SU_TRYCATCH(dest->name = strdup(orig->name), goto done);

  ok = SU_TRUE;

done:

  return ok;
}

SUSDIFF
orbit_init_from_data(orbit_t *self, const void *data, SUSCOUNT len)
{
  SUSDIFF consumed = -1;
  SUSDIFF i = 0;
  unsigned int p = 0;
  unsigned int linenum = 0;
  char *linebuf = NULL;
  const char *as_string = (const char *) data;
  char *line;

  SU_TRYCATCH(
    linebuf = malloc(SUSCAN_TLE_LINE_LEN + 1), 
    goto done);

  linebuf[SUSCAN_TLE_LINE_LEN] = '\0';

  consumed = 0;
  
  memset(self, 0, sizeof(orbit_t));

  for (i = 0; consumed == 0 && i < len; ++i) {
    switch (as_string[i]) {
      case '\r':
        /* CRLF files? Skip */
        break;

      case '\n':
        /* End of line. Remove leading spaces */
        linebuf[p] = '\0';
        p    = 0;
        line = linebuf;
        while (isspace(*line))
          ++line;
        
        /* Skip empty lines */
        if (*line == '\0')
          break;

        SU_TRYCATCH(
          su_orbit_parse_tle_line(self, linenum++, linebuf), 
          goto done);

        /* All 3 lines of the TLE have been properly parsed */
        if (linenum == 3)
          consumed = i + 1;
        break;

      default:
        /* Sanity checks */
        if (!isprint(as_string[i])) {
          SU_ERROR("Invalid character found in offset %d\n", i);
          consumed = -1;
        }

        if (p < SUSCAN_TLE_LINE_LEN)
          linebuf[p++] = as_string[i];
    }
  }

done:
  if (consumed <= 0)
    orbit_finalize(self);

  if (linebuf != NULL)
    free(linebuf);

  return consumed;
}

SUBOOL
orbit_init_from_file(orbit_t *self, const char *file)
{
  struct stat sbuf;
  SUSDIFF got;
  int fd = -1;
  void *buffer = (void *) -1;
  SUBOOL ok = SU_FALSE;

  if (stat(file, &sbuf) == -1) {
    SU_ERROR("Cannot stat `%s': %s\n", file, strerror(errno));
    goto done;
  }

  if ((fd = open(file, O_RDONLY)) == -1) {
    SU_ERROR("Cannot open `%s': %s\n", file, strerror(errno));
    goto done;
  }

  if ((buffer = mmap(NULL, sbuf.st_size, PROT_READ, MAP_PRIVATE, fd, 0)) 
    == (void *) -1) {
    SU_ERROR("mmap failed: %s\n", strerror(errno));
    goto done;
  }

  if ((got = orbit_init_from_data(self, buffer, sbuf.st_size)) <= 0) {
    SU_ERROR("This does not look like a valid TLE file\n");
    goto done;
  }

  ok = SU_TRUE;

done:
  if (buffer != (void *) -1)
    munmap(buffer, sbuf.st_size);
  
  if (fd != -1)
    close(fd);

  return ok;
}

time_t 
tle_mktime(struct tm *tm) 
{
  time_t ret;
  char *tz;

  tz = getenv("TZ");
  setenv("TZ", "", 1);
  tzset();
  ret = mktime(tm);
  if (tz)
    setenv("TZ", tz, 1);
  else
    unsetenv("TZ");
  tzset();
  return ret;
}

void
orbit_epoch_to_timeval(const orbit_t *self, struct timeval *tv)
{
  struct tm tm;
  time_t result;
  SUDOUBLE daysecs;

  memset(&tm, 0, sizeof(struct tm));
  
  tm.tm_year = self->ep_year - 1900;
  tm.tm_mday = 0; /* Yes. This is not a bug. It is intended. */
  tm.tm_mon  = 0;
  tm.tm_hour = 0;
  tm.tm_min  = 0;
  tm.tm_sec  = 0;
  
  result = tle_mktime(&tm);
  
  daysecs = self->ep_day * 24 * 3600;
  result += (time_t) floor(daysecs);

  tv->tv_sec  = result;
  tv->tv_usec = floor((daysecs - floor(daysecs)) * 1e6);
}

SUDOUBLE
orbit_epoch_to_unix(const orbit_t *self)
{
  struct timeval tv;

  orbit_epoch_to_timeval(self, &tv);

  return tv.tv_sec + 1e-6 * tv.tv_usec;
}

SUDOUBLE
orbit_minutes_from_timeval(
  const orbit_t *self,
  const struct timeval *when)
{
  struct timeval epoch;
  struct timeval diff;

  orbit_epoch_to_timeval(self, &epoch);
  timersub(when, &epoch, &diff);

  return (diff.tv_sec + 1e-6 * diff.tv_usec) / 60.;
}

SUDOUBLE
orbit_minutes(const orbit_t *self, SUDOUBLE time)
{
  return (time - orbit_epoch_to_unix(self)) / 60.;
}

void
orbit_debug(const orbit_t *self)
{
  SU_INFO("SAT NAME: %s\n", self->name);
  SU_INFO("  Epoch:    %d + %g\n", self->ep_year, self->ep_day);
  SU_INFO("  MM:       %g rev / day\n", self->rev);
  SU_INFO("  dMM/dt:   %g rev / day²\n", self->drevdt);
  SU_INFO("  d²MM/dt²: %g rev / day³\n", self->d2revdt2);
  SU_INFO("  B*:       %g\n", self->bstar);
  SU_INFO("  Incl:     %gº\n", SU_RAD2DEG(self->eqinc));
  SU_INFO("  Ecc:      %g\n", self->ecc);
  SU_INFO("  Mnan:     %gº\n", SU_RAD2DEG(self->mnan));
  SU_INFO("  Argp:     %gº\n", SU_RAD2DEG(self->argp));
  SU_INFO("  RAAN:     %gº\n", SU_RAD2DEG(self->ascn));
  SU_INFO("  S. axis:  %g km\n", self->smjaxs);
  SU_INFO("  Norb:     %ld\n", self->norb);
  SU_INFO("  Satno:    %d\n", self->satno);
}

void 
orbit_finalize(orbit_t *self)
{
  if (self->name != NULL)
    free(self->name);
}