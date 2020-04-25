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
 *	int	new_open (int fd, void (*callback) (int fd), int type,
 *				int quiet, int server);
 *	- PURPOSE: To indicate that file descriptor 'fd' should be watched
 *		   for readable events
 *	- INPUT:   fd - The file descriptor to watch
 *	           callback - The function to call when 'fd' is "dirty".
 *		      -- Note, 'fd' shall be passed to the callback.
 *		   type - One of the NEWIO_* macros below that tell us
 *			  how data from the fd is generated:
 *			NEWIO_READ - When Readable, call read().
 *			NEWIO_ACCEPT - When Readable, call accept().
 *			NEWIO_SSL_READ - When Readable, call SSL_read().
 *			NEWIO_CONNECT - When Writable, call getpeerbyname().
 *			NEWIO_RECV - When Readable, call recv().
 *			NEWIO_NULL - To reversibly cease operations on 'fd'.
 *			NEWIO_SSL_CONNECT - When Readable, call SSL_connect().
 *		   quiet - When set, errors should not be displayed to screen
 *		   server - Errors should go to this server's windows.
 *	- OUTPUT:  -1 if the file descriptor cannot be watched
 *		   a "channel" if the file descriptor can be watched.
 *	- NOTE:	   Calling new_open() shall cancel and override a previous
 *		   new_open().
 *
 *	#define new_close(fd) new_close_with_option(fd, 0)
 *	int	new_close_with_option (int fd, int virtual);
 *	- PURPOSE: To irreversibly cease operations on 'fd'.  The fd shall no
 *		   longer be watched, and shall generate no more events, and
 *		   any pending data shall be discarded.  The fd shall be 
 *		   close(2)d [returned to the operating system] if virtual == 0
 *	- INPUT:   fd - The file descriptor to release
 *		   virtual - The file descriptor is managed by the caller,
 *			     so it must not be close(2)d.
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

#define NEWIO_READ	  1
#define NEWIO_ACCEPT	  2
#define NEWIO_SSL_READ	  3
#define NEWIO_CONNECT	  4
#define NEWIO_RECV	  5
#define NEWIO_NULL	  6
#define NEWIO_SSL_CONNECT 7
#define NEWIO_PASSTHROUGH_READ 8
#define NEWIO_PASSTHROUGH_WRITE 9

#define IO_BUFFER_SIZE 8192

	int	dgets_buffer		(int, void *, ssize_t);
	ssize_t	dgets 			(int, char *, size_t, int);
	int	do_wait			(struct timeval *);
	void	do_filedesc		(void);
	void	init_newio		(void);
	size_t	get_pending_bytes	(int);
	int	get_server_by_vfd	(int);
#define SRV(vfd) get_server_by_vfd(vfd)

	int	new_open		(int, void (*) (int), int, int, int);
	int     new_open_failure_callback (int vfd, void (*) (int, int));
	int	new_hold_fd		(int);
	int	new_unhold_fd		(int);
	int 	new_close_with_option	(int, int);
#define new_close(fd) new_close_with_option(fd, 0)

	int	my_sleep		(double);
	int	my_isreadable		(int, double);
	int	my_iswritable		(int, double);

#endif
