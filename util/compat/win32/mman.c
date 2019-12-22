/*
 * Copyright (c) 2014, Newcastle University, UK.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
 

/*
 Memory mapped file - Implementation of mmap() for Windows
 Dan Jackson, 2014 - 'MMAP_CLEANUP' changes to clean up otherwise leaked handles from CreateFileMapping()
 Original author: Mike Frysinger <vapier@gentoo.org>, placed into the public domain.

 References:
   CreateFileMapping: http://msdn.microsoft.com/en-us/library/aa366537(VS.85).aspx
   CloseHandle:       http://msdn.microsoft.com/en-us/library/ms724211(VS.85).aspx
   MapViewOfFile:     http://msdn.microsoft.com/en-us/library/aa366761(VS.85).aspx
   UnmapViewOfFile:   http://msdn.microsoft.com/en-us/library/aa366882(VS.85).aspx
*/

#include <io.h>
#include <windows.h>
#include <sys/types.h>
#include "sys/mman.h"

#ifdef __USE_FILE_OFFSET64
#  define DWORD_HI(x) (x >> 32)
#  define DWORD_LO(x) ((x) & 0xffffffff)
#else
#  define DWORD_HI(x) (0)
#  define DWORD_LO(x) (x)
#endif

#if defined(MMAP_CLEANUP)
struct mmap_cleanup_tag;
typedef struct mmap_cleanup_tag {
	void *addr;
	HANDLE h;
	struct mmap_cleanup_tag *next;
} mmap_cleanup_t;
mmap_cleanup_t *mmap_cleanup = NULL;
#endif /* defined(MMAP_CLEANUP) */

void *
mmap(void *start, size_t length, int prot, int flags, int fd, off_t offset)
{
	if (prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC))
		return MAP_FAILED;
	if (fd == -1) {
		if (!(flags & MAP_ANON) || offset)
			return MAP_FAILED;
	} else if (flags & MAP_ANON)
		return MAP_FAILED;

	DWORD flProtect;
	if (prot & PROT_WRITE) {
		if (prot & PROT_EXEC)
			flProtect = PAGE_EXECUTE_READWRITE;
		else
			flProtect = PAGE_READWRITE;
	} else if (prot & PROT_EXEC) {
		if (prot & PROT_READ)
			flProtect = PAGE_EXECUTE_READ;
		else if (prot & PROT_EXEC)
			flProtect = PAGE_EXECUTE;
	} else
		flProtect = PAGE_READONLY;

	off_t end = length + offset;
	HANDLE mmap_fd, h;
	if (fd == -1)
		mmap_fd = INVALID_HANDLE_VALUE;
	else
		mmap_fd = (HANDLE)_get_osfhandle(fd);
	h = CreateFileMapping(mmap_fd, NULL, flProtect, DWORD_HI(end), DWORD_LO(end), NULL);
	if (h == NULL)
		return MAP_FAILED;

	DWORD dwDesiredAccess;
	if (prot & PROT_WRITE)
		dwDesiredAccess = FILE_MAP_WRITE;
	else
		dwDesiredAccess = FILE_MAP_READ;
	if (prot & PROT_EXEC)
		dwDesiredAccess |= FILE_MAP_EXECUTE;
	if (flags & MAP_PRIVATE)
		dwDesiredAccess |= FILE_MAP_COPY;
	void *ret = MapViewOfFile(h, dwDesiredAccess, DWORD_HI(offset), DWORD_LO(offset), length);
	if (ret == NULL) {
		CloseHandle(h);
		ret = MAP_FAILED;
	}
#ifdef MMAP_CLEANUP
	else
	{
		/* Add a tracking element (to the start of our list) */
		mmap_cleanup_t *mc = (mmap_cleanup_t *)malloc(sizeof(mmap_cleanup_t));
		if (mc != NULL)
		{
			mc->addr = ret;
			mc->h = h;
			mc->next = mmap_cleanup;
			mmap_cleanup = mc;
		}
	}
#endif
	return ret;
}

void 
munmap(void *addr, size_t length)
{
	UnmapViewOfFile(addr);
#ifdef MMAP_CLEANUP
	/* Look up through the tracking elements to close the handle */
	mmap_cleanup_t **prevPtr = &mmap_cleanup;
	mmap_cleanup_t *mc;
	for (mc = *prevPtr; mc != NULL; prevPtr = &mc->next, mc = *prevPtr)
	{
		if (mc->addr == addr)
		{
			CloseHandle(mc->h);
			*prevPtr = mc->next;
			break;
		}
	}
#else
	// ruh-ro, we leaked handle from CreateFileMapping() ...
#endif
}
