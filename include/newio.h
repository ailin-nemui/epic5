/*
 * newio.h -- header file for newio.c
 *
 * Copyright 1990, 1995 Michael Sandrof, Matthew Green
 * Copyright 1997 EPIC Software Labs
 */

#ifndef __newio_h__
#define __newio_h__
#ifdef HAVE_SSL
#include "ssl.h"
#endif

extern 	int 	dgets_errno;

	int 	dgets 			(char *, int, int, void *);
	int 	new_select 		(fd_set *, fd_set *, struct timeval *);
	int	new_open		(int);
	int	new_open_for_writing	(int);
	int 	new_close 		(int);
	void 	set_socket_options 	(int);
	size_t	get_pending_bytes	(int);

#define IO_BUFFER_SIZE 8192

#endif
