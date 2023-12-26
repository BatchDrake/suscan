/*
  
  Copyright (C) 2023 Gonzalo José Carracedo Carballal
  
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

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <sigutils/types.h>
#include <sigutils/defs.h>

#include <unistd.h>
#include <string.h>
#include <errno.h>

struct suscan_vm_circbuf_state {
  int        fd;
  SUSCOUNT   size;
  SUCOMPLEX *buf1;
  SUCOMPLEX *buf2;
};

typedef struct suscan_vm_circbuf_state suscan_vm_circbuf_state_t;

SU_COLLECTOR(suscan_vm_circbuf_state)
{
  if (self->buf2 != NULL && (caddr_t) self->buf2 != (caddr_t) -1) {
    munmap(self->buf2, self->size * sizeof(SUCOMPLEX));
    munmap(self->buf1, self->size * sizeof(SUCOMPLEX));
  } else if (self->buf1 != NULL && (caddr_t) self->buf1 != (caddr_t) -1) {
    munmap(self->buf1, 2 * self->size * sizeof(SUCOMPLEX));
  }

  if (self->fd != -1)
    close(self->fd);

  free(self);
}

SU_INSTANCER(suscan_vm_circbuf_state, SUSCOUNT size)
{
  suscan_vm_circbuf_state_t *state = NULL;
  char *name = NULL;
  size_t alloc_size;

  if (!suscan_vm_circbuf_allowed(size)) {
    SU_ERROR("The requested circular VM buffer size is not supported\n");
    goto fail;
  }

  SU_ALLOCATE_FAIL(state, suscan_vm_circbuf_state_t);

  state->fd = -1;

  SU_TRY_FAIL(name = strbuild("/vmcircbuf-%d-%p", getpid(), state));
  state->fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);

  if (state->fd == -1) {
    SU_ERROR("Failed to allocate shared memory: %s\n", strerror(errno));
    goto fail;
  } 

  free(name);

  state->size = size;
  alloc_size = size * sizeof(SUCOMPLEX);

  SU_TRYC_FAIL(ftruncate(state->fd, alloc_size));

  /* In the first stage, we allocate TWICE the memory of the file */
  state->buf1 = mmap(
    NULL,
    2 * alloc_size,
    PROT_READ | PROT_WRITE,
    MAP_SHARED,
    state->fd,
    0);
  
  if ((caddr_t) state->buf1 == (caddr_t) -1) {
    SU_ERROR(
      "Cannot mmap %d bytes of VM circbuf memfd: %s\n",
      2 * alloc_size,
      strerror(errno));
    goto fail;
  }

  /* Next, we reallocate on top of it */
  state->buf2 = mmap(
    state->buf1 + size,
    alloc_size,
    PROT_READ | PROT_WRITE,
    MAP_SHARED | MAP_FIXED,
    state->fd,
    0);
  
  if ((caddr_t) state->buf2 == (caddr_t) -1) {
    SU_ERROR(
      "Cannot mmap %d bytes of mirrored memory: %s\n",
      alloc_size,
      strerror(errno));
    goto fail;
  }

  close(state->fd);
  state->fd = -1;

  return state;

fail:
  if (name != NULL)
    free(name);
  
  if (state != NULL)
    suscan_vm_circbuf_destroy(state);

  return NULL;
}

SUBOOL
suscan_vm_circbuf_allowed(SUSCOUNT size)
{
  return ((sizeof(SUCOMPLEX) * size) % getpagesize()) == 0;
}

SUCOMPLEX *
suscan_vm_circbuf_new(void **handle, SUSCOUNT size)
{
  suscan_vm_circbuf_state_t *state = suscan_vm_circbuf_state_new(size);

  if (state != NULL) {
    *handle = (void *) state;
    return state->buf1;
  }

  return NULL;
}

void
suscan_vm_circbuf_destroy(void *handle)
{
  suscan_vm_circbuf_state_t *state = (suscan_vm_circbuf_state_t *) handle;

  suscan_vm_circbuf_state_destroy(state);
}
