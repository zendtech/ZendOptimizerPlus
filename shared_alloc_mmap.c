/*
   +----------------------------------------------------------------------+
   | Zend Optimizer+                                                      |
   +----------------------------------------------------------------------+
   | Copyright (c) 1998-2013 The PHP Group                                |
   +----------------------------------------------------------------------+
   | This source file is subject to version 3.01 of the PHP license,      |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | http://www.php.net/license/3_01.txt                                  |
   | If you did not receive a copy of the PHP license and are unable to   |
   | obtain it through the world-wide-web, please send a note to          |
   | license@php.net so we can mail you a copy immediately.               |
   +----------------------------------------------------------------------+
   | Authors: Andi Gutmans <andi@zend.com>                                |
   |          Zeev Suraski <zeev@zend.com>                                |
   |          Stanislav Malyshev <stas@zend.com>                          |
   |          Dmitry Stogov <dmitry@zend.com>                             |
   +----------------------------------------------------------------------+
*/

#include "zend_shared_alloc.h"

#ifdef USE_MMAP

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

#if defined(MAP_ANON) && !defined(MAP_ANONYMOUS)
# define MAP_ANONYMOUS MAP_ANON
#endif

#ifdef PHP_OPTIMIZER_MMAP_FILE
#include <fcntl.h>
static int create_segments_file(size_t requested_size) {
#ifdef PHP_OPTIMIZER_MMAP_FILE_PID
	pid_t pid = getpid();
#else
	pid_t pid = 0000;
#endif
	
	/* this should maybe come from an ini setting, or environment */
	ZSMMG(fl) = snprintf(NULL, 0, "%s/ZendOptimizerPlus.%d", PHP_OPTIMIZER_MMAP_FILE, pid);

	if (ZSMMG(fl)) {
		ZSMMG(fn) = calloc(ZSMMG(fl)+1, sizeof(char));
		if (ZSMMG(fn) && snprintf(
				ZSMMG(fn), ZSMMG(fl), "%s/ZendOptimizerPlus.%d", PHP_OPTIMIZER_MMAP_FILE, pid)) {

			ZSMMG(fd) = open(ZSMMG(fn), O_RDWR | O_CREAT | O_TRUNC, (mode_t)0666);
		
			if (ZSMMG(fd) > -1) {
				if (lseek(ZSMMG(fd), requested_size-1, SEEK_SET) != -1) {
					if (write(ZSMMG(fd), "", 1)) {
						goto success;
					}
				}
				close(ZSMMG(fd));
			} else {
				zend_error(
					E_WARNING, 
					"Zend Optimizer+ failed to open mmap file for storage in %s, %d", 
					PHP_OPTIMIZER_MMAP_FILE, errno
				);
			}

			free(ZSMMG(fn));

			ZSMMG(fn) = NULL;
			ZSMMG(fd) = -1;
			ZSMMG(fl) = 0;
			
			return FAILURE;
		}
	}

success:
	return SUCCESS;
}
#endif

static int create_segments(size_t requested_size, zend_shared_segment ***shared_segments_p, int *shared_segments_count, char **error_in)
{
	zend_shared_segment *shared_segment;
#ifdef PHP_OPTIMIZER_MMAP_FILE
	ZSMMG(fd) = create_segments_file(requested_size);
#endif
	*shared_segments_count = 1;
	*shared_segments_p = (zend_shared_segment **) calloc(1, sizeof(zend_shared_segment)+sizeof(void *));
	shared_segment = (zend_shared_segment *)((char *)(*shared_segments_p) + sizeof(void *));
	(*shared_segments_p)[0] = shared_segment;

#ifdef PHP_OPTIMIZER_MMAP_FILE
	if (ZSMMG(fd) > -1) {
		shared_segment->p = mmap(0, requested_size, PROT_READ | PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, ZSMMG(fd), 0);
	} else shared_segment->p = mmap(0, requested_size, PROT_READ | PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
#else
	shared_segment->p = mmap(0, requested_size, PROT_READ | PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
#endif

	if(shared_segment->p == MAP_FAILED) {
		*error_in = "mmap";
		return ALLOC_FAILURE;
	}

	shared_segment->pos = 0;
	shared_segment->size = requested_size;

	return ALLOC_SUCCESS;
}

static int detach_segment(zend_shared_segment *shared_segment)
{
	munmap(shared_segment->p, shared_segment->size);
	return 0;
}

static size_t segment_type_size(void)
{
	return sizeof(zend_shared_segment);
}

#ifdef PHP_OPTIMIZER_MMAP_FILE
static int close_segments(void) {
	
	/* if the filename is alloced then read */
	if (ZSMMG(fn)) {
		/* close map file */
		close(ZSMMG(fd));	

		/* delete map file */
		unlink(ZSMMG(fn));
		
		/* free filename alloc'd for file */
		free(ZSMMG(fn));
		
		/* set null filename */
		ZSMMG(fn) = NULL;

		return SUCCESS;
	}

	return FAILURE;
}
#endif

zend_shared_memory_handlers zend_alloc_mmap_handlers = {
	create_segments,
	detach_segment,
#ifdef PHP_OPTIMIZER_MMAP_FILE
	close_segments,
#endif
	segment_type_size
};

#endif /* USE_MMAP */
