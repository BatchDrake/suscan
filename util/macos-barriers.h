/*
 * 
 * pthread_barriers implementation for MacOS obtained from GitHub
 * user isotes at https://github.com/isotes/pthread-barrier-macos
 *
 * (c) Copyright 2019 Robert Sauter
 * 
 * MIT License
 * 
 * Copyright (c) 2019
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#ifndef _COMPAT_MACOS_BARRIERS_H
#define _COMPAT_MACOS_BARRIERS_H

#  ifndef __APPLE__
#    error This header file must not be included in non-MacOS targets!
#  endif

#include <pthread.h>

#  ifdef __cplusplus
extern "C" {
#  endif /* __cplusplus */

#ifndef PTHREAD_BARRIER_SERIAL_THREAD
# define PTHREAD_BARRIER_SERIAL_THREAD -1
#endif

typedef pthread_mutexattr_t pthread_barrierattr_t;

/* structure for internal use that should be considered opaque */
typedef struct {
  pthread_mutex_t mutex;
  pthread_cond_t cond;
  unsigned count;
  unsigned left;
  unsigned round;
} pthread_barrier_t;

int pthread_barrier_init(
  pthread_barrier_t *__restrict barrier, 
  const pthread_barrierattr_t * __restrict attr,
  unsigned count);
int pthread_barrier_destroy(pthread_barrier_t *barrier);

int pthread_barrier_wait(pthread_barrier_t *barrier);

int pthread_barrierattr_init(pthread_barrierattr_t *attr);
int pthread_barrierattr_destroy(pthread_barrierattr_t *attr);
int pthread_barrierattr_getpshared(
  const pthread_barrierattr_t *__restrict attr,
  int *__restrict pshared);
int pthread_barrierattr_setpshared(
  pthread_barrierattr_t *attr,
  int pshared);

#  ifdef __cplusplus
}
#  endif /* __cplusplus */
#endif /* _COMPAT_MACOS_BARRIERS_H */
