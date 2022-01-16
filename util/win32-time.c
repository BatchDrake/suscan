/*
 * Copyright (C) 2013 Martin Willi
 * Copyright (C) 2013 revosec AG
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.  See <http://www.fsf.org/copyleft/gpl.txt>.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#include "win32-time.h"

/**
 * timersub(3) from <sys/time.h>
 */
void timersub(const struct timeval *a, const struct timeval *b,
							struct timeval *res)
{
	res->tv_sec = a->tv_sec - b->tv_sec;
	res->tv_usec = a->tv_usec - b->tv_usec;
	if (res->tv_usec < 0)
	{
		res->tv_usec += 1000000;
		res->tv_sec--;
	}
}

/**
 * timeradd(3) from <sys/time.h>
 */
void timeradd(const struct timeval *a, const struct timeval *b,
							struct timeval *res)
{
	res->tv_sec = a->tv_sec + b->tv_sec;
	res->tv_usec = a->tv_usec + b->tv_usec;
	if (res->tv_usec > 1000000)
	{
		res->tv_usec -= 1000000;
		res->tv_sec++;
	}
}