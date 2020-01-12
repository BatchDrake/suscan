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

#include "macos-barriers.h"
#include <errno.h>

int 
pthread_barrier_init(
  pthread_barrier_t *__restrict barrier,
  const pthread_barrierattr_t * __restrict attr,
  unsigned count) 
{
  if (count == 0) {
    return EINVAL;
  }

  int ret;

  pthread_condattr_t condattr;
  pthread_condattr_init(&condattr);
  if (attr) {
    int pshared;
    ret = pthread_barrierattr_getpshared(attr, &pshared);
    if (ret) {
      return ret;
    }
    ret = pthread_condattr_setpshared(&condattr, pshared);
    if (ret) {
      return ret;
    }
  }

  ret = pthread_mutex_init(&barrier->mutex, attr);
  if (ret) {
    return ret;
  }

  ret = pthread_cond_init(&barrier->cond, &condattr);
  if (ret) {
    pthread_mutex_destroy(&barrier->mutex);
    return ret;
  }

  barrier->count = count;
  barrier->left = count;
  barrier->round = 0;

  return 0;
}

int 
pthread_barrier_destroy(pthread_barrier_t *barrier) 
{
  if (barrier->count == 0) {
    return EINVAL;
  }

  barrier->count = 0;
  int rm = pthread_mutex_destroy(&barrier->mutex);
  int rc = pthread_cond_destroy(&barrier->cond);
  return rm ? rm : rc;
}


int 
pthread_barrier_wait(pthread_barrier_t *barrier) 
{
  pthread_mutex_lock(&barrier->mutex);
  if (--barrier->left) {
    unsigned round = barrier->round;
    do {
      pthread_cond_wait(&barrier->cond, &barrier->mutex);
    } while (round == barrier->round);
    pthread_mutex_unlock(&barrier->mutex);
    return 0;
  } else {
    barrier->round += 1;
    barrier->left = barrier->count;
    pthread_cond_broadcast(&barrier->cond);
    pthread_mutex_unlock(&barrier->mutex);
    return PTHREAD_BARRIER_SERIAL_THREAD;
  }
}


int 
pthread_barrierattr_init(pthread_barrierattr_t *attr) 
{
  return pthread_mutexattr_init(attr);
}

int 
pthread_barrierattr_destroy(pthread_barrierattr_t *attr) 
{
  return pthread_mutexattr_destroy(attr);
}

int 
pthread_barrierattr_getpshared(
  const pthread_barrierattr_t *__restrict attr,
  int *__restrict pshared) 
{
  return pthread_mutexattr_getpshared(attr, pshared);
}

int 
pthread_barrierattr_setpshared(pthread_barrierattr_t *attr, int pshared) 
{
  return pthread_mutexattr_setpshared(attr, pshared);
}
