/*
 * newio.h -- header file for newio.c
 *
 * Copyright 1990, 1995 Michael Sandrof, Matthew Green
 * Copyright 1997, 2004 EPIC Software Labs
 */

/*
 * Here are the interfaces you must define if you want to add a new 
 * looper to epic.  EPIC will call these functions to do various things:
 *
 *	int	do_wait (struct timeval *timeout)
 *	- PURPOSE: Wait until a filedesc is dirty, or the timeout expires.
 *	- INPUT:  timeout - The maximum amount of time to wait before returning
 *	- OUTPUT: -1 if returning for any reason other than timeout or 
 *		     something happening
 *		   0 if timeout occured before something happened
 *		   1 if something happened before timeout
 *
 *	int	new_open (int fd, void (*callback) (int fd), int type);
 *	- PURPOSE: To indicate that file descriptor 'fd' should be watched
 *		   for readable events
 *	- INPUT:   fd - The file descriptor to watch
 *	           callback - The function to call when 'fd' can be read
 *		      -- Note, 'fd' shall be passed to the callback.
 *	- OUTPUT:  -1 if the file descriptor cannot be watched
 *		   fd if the file descriptor can be watched.
 *	- NOTE:	   Calling new_open() shall cancel and override a previous
 *		   new_open_for_writing().
 *
 *	int	new_open_for_writing (int fd, void (*callback) (int fd));
 *	- PURPOSE: To indicate that file descriptor 'fd' should be watched
 *		   for writable events (nonblocking connects)
 *	- INPUT:   fd - The file descriptor to watch
 *	           callback - The function to call when 'fd' can be read
 *		      -- Note, 'fd' shall be passed to the callback.
 *	- OUTPUT   -1 if the file descriptor cannot be watched
 *		   fd if the file descriptor can be watched.
 *	- NOTE:	   Calling new_open_for_writing() shall cancel and override 
 *		   a previous new_open().
 *
 *	int	new_close (int fd);
 *	- PURPOSE: To indicate that file descriptor 'fd' should no longer
 *		   be watched for read or write events, and that 'fd should
 *		   be close()d or otherwise released to the system.
 *	- INPUT:   fd - The file descriptor to release
 *	- OUTPUT:  -1 shall be returned.
 *
 *	int	do_filedesc (void);
 *	- PURPOSE: To execute callbacks for events previously caught by 
 *		   do_wait().
 *	- INPUT:   No input -- function must arrange with do_wait() to
 *		   determine which file descriptors need to be called back.
 *	- OUTPUT:  No return value.
 *	- NOTE:    Callback functions usually call dgets() to do the read.
 *		   It would be bad to callback a fd that is not ready to 
 *		   read because dgets() will block.
 */
 
#ifndef __newio_h__
#define __newio_h__
#define USE_SELECT 
/* #define USE_FREEBSD_KQUEUE  */
/* #define USE_POLL */
/* #define USE_PTHREADS  */
/* #define VIRTUAL_FILEDESCRIPTORS */

#define NEWIO_READ	1
#define NEWIO_ACCEPT	2
#define NEWIO_SSL_READ	3
#define NEWIO_CONNECT	4
#define NEWIO_RECV	5

#define IO_BUFFER_SIZE 8192

extern 	int 	dgets_errno;

	int	dgets_buffer		(int, void *, ssize_t);
	ssize_t	dgets 			(int, char *, size_t, int);
	int	do_wait			(struct timeval *);
	void	do_filedesc		(void);
	void	init_newio		(void);
	size_t	get_pending_bytes	(int);

	int	new_open		(int, void (*) (int), int);
	int	new_open_for_writing	(int, void (*) (int));
	int	new_hold_fd		(int);
	int	new_unhold_fd		(int);
	int 	new_close 		(int);

#endif
