/* $EPIC: newio.c,v 1.32 2005/02/10 05:10:57 jnelson Exp $ */
/*
 * newio.c:  Passive, callback-driven IO handling for sockets-n-stuff.
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright © 1997, 2003 EPIC Software Labs.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notices, the above paragraph (the one permitting redistribution),
 *    this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the author(s) may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Here's the plan -- a two-phase data buffering system that can be adapted
 * to systems with fully asynchronous i/o (vms, threads).
 *
 * Each file descriptor is tracked based on two things:
 *	1) How the fd produces data.
 *	2) A function to call when there is data to consume.
 *
 * At the highest level, a file descriptor is declared for use with
 *
 *	int new_open (int fd, void (*callback)(int), int io_type)
 *
 * whereby we provide the file descriptor, the consumer callback, and the
 * io production type.  After calling new_open(), you must never call read()
 * or write() or accept() or connect() on the fd.
 *
 * Newio arranges for the file descriptor to send any data it generates to
 * the dgets buffers:
 *	int dgets_buffer (int fd, void *data, size_t len)
 * When data is generated and buffered, the file descriptor is considered
 * "dirty" ("not clean").  After a file descriptor is dirty, your callback
 * is repeatedly called until the file descriptor is no longer dirty.
 *
 * Your callback can clean a file descriptor by calling dgets():
 *	ssize_t	dgets (int des, char *buf, size_t buflen, int buffer)
 * Dgets() will remove some data from the buffer and return it to you.
 * 'buffer' controls how much data you want dgets() to return.
 *
 * When all file descriptors are "clean", the do_wait() function is called,
 * and it is expected to return when one or more file descriptors are "dirty"
 * or when the timeout expires.  After it returns, do_filedesc() is called
 * to "clean" all of the dirty file descriptors by repeatedly calling your
 * provided callback.
 *
 * So here is the flow of the system:
 *	1) You create a file descriptor
 * 	2) Call new_open()
 *	3) Call do_wait()
 *	4) Call handle_filedesc()
 *	   4a) This will call your callbacks on any dirty fd's.
 *	5) Go back to 3
 *	
 * This is the essential parts of the main loop that drives EPIC.
 *
 * 
 * Questions and answers:
 * Q) So how is this different than what was done before?
 * A) Before, do_wait() returned when an IO operation was possible, and 
 *    your callback was expected to perform the operation (usually by calling
 *    dgets() with a io callback).  Now, the IO operation is performed before
 *    do_wait() returns, and the only thing dgets() does is copy data from
 *    its buffers to yours.  Therefore, you can call dgets() anytime and it
 *    will never block.
 * Q) Should I create a loop around dgets() in my callback?
 * A) That is not necessary.  Your callback should just perform one logical
 *    operation.  If there is more data left, your callback will be called
 *    again until you've exhausted all of the data.  An implied loop around 
 *    dgets() is a of this system.
 * Q) Why does this documentation suck so much?
 * A) I wrote it in a big hurry.  Maybe I'll "fix" it again later.
 */

#include "irc.h"
#include "ircaux.h"
#include "output.h"
#include "newio.h"
#include "ssl.h"
#include "timer.h"

#include <sys/ioctl.h>
#ifdef HAVE_SYS_FILIO_H
#include <sys/filio.h>
#endif
#ifdef HAVE_STROPTS_H
#include <stropts.h>		/* XXXX nonportable */
#endif
#ifdef HAVE_SYS_STROPTS_H
#include <sys/stropts.h>	/* XXXX nonportable */
#endif

#if defined(HAVE_SYSCONF) && defined(_SC_OPEN_MAX)
#  define IO_ARRAYLEN 	sysconf(_SC_OPEN_MAX)
#else
# if defined(FD_SETSIZE)
#  define IO_ARRAYLEN 	FD_SETSIZE
# else
#  define IO_ARRAYLEN 	NFDBITS
# endif
#endif

#define MAX_SEGMENTS 16

typedef	struct	myio_struct
{
	char *	buffer;
	size_t	buffer_size,
		read_pos,
		write_pos;
	short	segments,
		error,
		clean,
		held;
	void	(*callback) (int fd);
	int	(*io_callback) (int fd);
}           MyIO;

static	MyIO **	io_rec = NULL;
static	int	global_max_fd = -1;
	int	dgets_errno = 0;

static void	new_io_event (int fd);

static int	unix_read (int fd);
static int	unix_accept (int fd);
static int	unix_connect (int fd);

/*
 * Get_pending_bytes: What do you think it does?
 */
size_t 	get_pending_bytes (int fd)
{
	if (fd >= 0 && io_rec[fd] && io_rec[fd]->buffer)
		return io_rec[fd]->write_pos - io_rec[fd]->read_pos;

	return 0;
}

static void	init_io (void)
{
	static	int	first = 1;

	if (first)
	{
		int	c, max_fd = IO_ARRAYLEN;

		io_rec = (MyIO **)new_malloc(sizeof(MyIO *) * max_fd);
		for (c = 0; c < max_fd; c++)
			io_rec[c] = (MyIO *) 0;
		first = 0;
	}
}

int	dgets_buffer (int des, void *data, size_t len)
{
	MyIO *ioe;

	if (!io_rec)
		init_io();

	if (len < 0)
		return 0;			/* XXX ? */

	if (!(ioe = io_rec[des]))
		panic("dgets called on unsetup fd %d", des);

	/* 
	 * An old exploit just sends us characters every .8 seconds without
	 * ever sending a newline.  Cut off anyone who tries that.
	 */
	if (ioe->segments > MAX_SEGMENTS)
	{
		yell("***XXX*** Too many read()s on des [%d] without a "
				"newline! ***XXX***", des);
		ioe->error = ECONNABORTED;
		ioe->clean = 0;
		return;
	}
	/* If the buffer completely empties, then clean it.  */
	else if (ioe->read_pos == ioe->write_pos)
	{
		ioe->read_pos = ioe->write_pos = 0;
		ioe->buffer[0] = 0;
		ioe->segments = 0;
	}
	/*
	 * If read_pos is non-zero, then some of the data was consumed,
	 * but not all of it (or it would be caught above), so we have
	 * an incomplete line of data in the buffer.  Move it to the 
	 * start of the buffer.
	 */
	else if (ioe->read_pos)
	{
		size_t	len;
		len = ioe->write_pos - ioe->read_pos;
		memmove(ioe->buffer, ioe->buffer + ioe->read_pos, len);
		ioe->read_pos = 0;
		ioe->write_pos = len;
		ioe->buffer[len] = 0;
		ioe->segments = 1;
	}

	if (ioe->buffer_size - ioe->write_pos < len)
	{
		while (ioe->buffer_size - ioe->write_pos < len)
			ioe->buffer_size += IO_BUFFER_SIZE;
		RESIZE(ioe->buffer, char, ioe->buffer_size);
	}

	memcpy((ioe->buffer) + (ioe->write_pos), data, len);
	ioe->write_pos += len;
	ioe->clean = 0;
	ioe->segments++;
	return 0;
}

/*
 * All new dgets -- no more trap doors!
 *
 * dgets() returns the next buffered unit of data from the buffers.
 * "buffer" controls what "buffered unit of data" means.  This function
 * should only be called from within a new_open() callback function!
 *
 * Arguments:
 * 1) des    - A "dirty" file descriptor.  You know that a file descriptor is
 *	       dirty when your new_open() callback is called.
 * 2) buf    - A buffer into which to copy the data from the file descriptor
 * 3) buflen - The size of 'buf'.
 * 4) buffer - The type of buffering to perform.
 *	-1	No line buffering, return everything.
 *	 0	Partial line buffering.  Return a line of data if there is
 *		one; Return everything if there isn't one.
 *	 1	Full line buffering.  Return a line of data if there is one,
 *		Return nothing if there isn't one.
 *
 * Return values:
 *	buffer == -1		(The results are NOT null terminated)
 *		-1	The file descriptor is dead.  dgets_errno contains
 *			the error (-1 means EOF).
 *		 0	There is no data available to read.
 *		>0	The number of bytes returned.
 *	buffer == 0		(The results are null terminated)
 *		-1	The file descriptor is dead.  dgets_errno contains
 *			the error (-1 means EOF).
 *		 0	Some data was returned, but it was an incomplete line.
 *		>0	A full line of data was returned.
 *	buffer == 1		(The results are null terminated)
 *		-1	The file descriptor is dead.  dgets_errno contains
 *			the error (-1 means EOF).
 *		 0	No data was returned.
 *		>0	A full line of data was returned.
 */
ssize_t	dgets (int des, char *buf, size_t buflen, int buffer)
{
	size_t	cnt = 0;
	size_t	consumed = 0;
	int	c = 0;		/* gcc can die. */
	char	h = 0;
	MyIO *	ioe;

	if (!io_rec)
		init_io();

	if (buflen == 0)
	{
	    yell("**XX** Buffer for des [%d] is zero-length! **XX**", des);
	    dgets_errno = ENOMEM; /* Cough */
	    return -1;
	}

	if (!(ioe = io_rec[des]))
		panic("dgets called on unsetup fd %d", des);

	if ((dgets_errno = ioe->error))
		return -1;


	/*
	 * So the buffer probably has changed now, because we just read
	 * in more data.  Check again to see if there is a newline.  If
	 * there is not, and the caller wants a complete line, just punt.
	 */
	if (buffer == 1 && !memchr(ioe->buffer + ioe->read_pos, '\n', 
					ioe->write_pos - ioe->read_pos))
	{
		ioe->clean = 1;
		return 0;
	}

	/*
	 * AT THIS POINT WE'VE COMMITED TO RETURNING WHATEVER WE HAVE.
	 */

	consumed = 0;
	while (ioe->read_pos < ioe->write_pos)
	{
	    h = ioe->buffer[ioe->read_pos++];
	    consumed++;

	    /* Only copy the data if there is some place to put it. */
	    if (cnt <= buflen - 1)
		buf[cnt++] = h;

	    /* 
	     * For buffered data, don't stop until we see the newline. 
	     * For unbuffered data, stop if we run out of space.
	     */
	    if (buffer >= 0)
	    {
		if (h == '\n')
		    break;
	    }
	    else
	    {
		if (cnt == buflen)
		    break;
	    }
	}

	if (ioe->read_pos == ioe->write_pos)
		ioe->clean = 1;

	/* 
	 * Before anyone whines about this, a lot of code in epic 
	 * silently assumes that incoming lines from the server don't
	 * exceed 510 bytes.  Until we "fix" all of those cases, it is
	 * better to truncate excessively long lines than to let them
	 * overflow buffers!
	 */
	if (cnt < consumed)
	{
		if (x_debug & DEBUG_INBOUND) 
			yell("FD [%d], Too long (did [%d], max [%d])", 
					des, consumed, cnt);

		/* If the line had a newline, then put the newline in. */
		if (buffer >= 0 && h == '\n')
		{
			cnt = buflen - 2;
			buf[cnt++] = '\n';
		}
	}

	/*
	 * Terminate it
	 */
	if (buffer >= 0)
		buf[cnt] = 0;

	/*
	 * If we end in a newline, then all is well.
	 * Otherwise, we're unbuffered, tell the caller.
	 * The caller then would need to do a strlen() to get
 	 * the amount of data.
	 */
	if (buffer == -1 || (cnt > 0 && buf[cnt - 1] == '\n'))
	    return cnt;
	else
	    return 0;
}

void	do_filedesc (void)
{
	int	i;

	for (i = 0; i <= global_max_fd; i++)
	{
		/* Then tell the user they have data ready for them. */
		while (io_rec[i] && !io_rec[i]->clean)
			io_rec[i]->callback(i);
	}
}

/************************************************************************/
/******************** Start of unix-specific code here ******************/
/************************************************************************/
static void	new_io_event (int fd)
{
	MyIO *ioe;
	int	c;

	ioe = io_rec[fd];

	/* If it's dirty, something is very wrong. */
	if (!ioe->clean)
		panic("new_io_event: fd [%d] hasn't been cleaned yet", fd);

	if ((c = ioe->io_callback(fd)) <= 0)
	{
		ioe->error = errno;
		if (x_debug & DEBUG_INBOUND) 
			yell("FD [%d] FAILED [%d:%s]", 
				fd, c, strerror(ioe->error));
		return;
	}

	if (x_debug & DEBUG_INBOUND) 
		yell("FD [%d], did [%d]", fd, c);
}


#ifdef USE_SELECT

/*
 * Register a filedesc for readable events
 * Set up its input buffer
 */
int 	select__new_open (int des, void (*callback) (int), int io_type)
{
	MyIO *ioe;

	if (des < 0)
		return des;		/* Invalid */

	if (!io_rec)
		init_io();

	if (io_type == NEWIO_CONNECT)
	{
		if (!FD_ISSET(des, &writables))
			FD_SET(des, &writables);
		if (FD_ISSET(des, &readables))
			FD_CLR(des, &readables);
	}
	else
	{
		if (!FD_ISSET(des, &readables))
			FD_SET(des, &readables);
		if (FD_ISSET(des, &writables))
			FD_CLR(des, &writables);
	}

	/*
	 * Keep track of the highest fd in use.
	 */
	if (des > global_max_fd)
		global_max_fd = des;

	if (!(ioe = io_rec[des]))
	{
		ioe = io_rec[des] = (MyIO *)new_malloc(sizeof(MyIO));
		ioe->buffer_size = IO_BUFFER_SIZE;
		ioe->buffer = (char *)new_malloc(ioe->buffer_size + 2);
	}

	ioe->read_pos = ioe->write_pos = 0;
	ioe->segments = 0;
	ioe->error = 0;
	ioe->clean = 1;
	ioe->held = 0;

	if (io_type == NEWIO_READ)
		io_rec[des]->io_callback = unix_read;
	else if (io_type == NEWIO_ACCEPT)
		io_rec[des]->io_callback = unix_accept;
#ifdef HAVE_SSL
	else if (io_type == NEWIO_SSL_READ)
	{
		io_rec[des]->io_callback = ssl_reader;
		startup_ssl(des);
	}
#endif
	else if (io_type == NEWIO_CONNECT)
		io_rec[des]->io_callback = unix_connect;
	else
		panic("New_open doesn't recognize io type %d", io_type);

	io_rec[des]->callback = callback;
	return des;
}

/*
 * This isn't really new, but what the hey..
 *
 * Remove the fd from the select fd sets so
 * that it won't bother us until we unhold it.
 *
 * Note that this is meant to be a read/write
 * hold and that this only operates on read
 * sets because that's all the client uses.
 */
int	select__new_hold_fd (int des)
{
	if (0 <= des && des <= global_max_fd && io_rec[des]->held == 0)
	{
		io_rec[des]->held = 1;
		FD_CLR(des, &readables);
	}

	return des;
}

/*
 * Add the fd again.
 */
int	select__new_unhold_fd (int des)
{
	if (0 <= des && des <= global_max_fd && io_rec[des]->held == 1)
	{
		FD_SET(des, &readables);
		io_rec[des]->held = 0;
	}

	return des;
}

/*
 * Unregister a filedesc for readable events 
 * and close it down and free its input buffer
 */
int	select__new_close (int des)
{
	if (des < 0)
		return -1;

	if (FD_ISSET(des, &readables))
		FD_CLR(des, &readables);
	if (FD_ISSET(des, &writables))
		FD_CLR(des, &writables);

	if (io_rec)
	{
	    if (io_rec[des])
	    {
		new_free(&io_rec[des]->buffer); 
		new_free((char **)&(io_rec[des]));
	    }
	}
	close(des);

	/*
	 * If we're closing the highest fd in use, then we
	 * want to adjust global_max_fd downward to the next highest fd.
	 */
	while ( global_max_fd >= 0 &&
		!FD_ISSET(global_max_fd, &readables) &&
		!FD_ISSET(global_max_fd, &writables))
			global_max_fd--;
	return -1;
}


/* * * * * * Begin the select-specific part of things * * * * */
static	fd_set	working_rd, working_wd;

/*
 * new_select: works just like select(), execpt I trimmed out the excess
 * parameters I didn't need.
 */
int 	select__do_wait (Timeval *timeout)
{
static	int	polls = 0;
	Timeval	thetimeout;
	Timeval *newtimeout = &thetimeout;
	int	i,
		set = 0;
	int	retval;

	working_rd = readables;
	working_wd = writables;

	if (timeout)
	{
		thetimeout = *timeout;
		if (timeout->tv_sec == 0 && timeout->tv_usec == 0)
		{
			if (polls++ > 10000)
			{
				dump_timers();
				panic("Stuck in a polling loop. Help!");
			}
			return 0;		/* Timers are more important */
		}
		else
			polls = 0;
	}
	else
		newtimeout = NULL;

	if (!io_rec)
		panic("new_select called before io_rec was initialized");

	for (i = 0; i <= global_max_fd; i++)
	{
		/*
		 * Check to see if there is a complete line sitting in the
		 * fd's buffer.  If there is, then we just tag this fd as
		 * being ready.  No sweat.  Don't mark the fd ready if the
		 * caller didn't ask about the fd, because that leads to 
		 * a busy-loop.
		 */
		if (io_rec[i] && !io_rec[i]->clean)
			panic("fd [%d] is not clean", i);
	}

	/*
	 * Only do the expensive select() call if there are no lines waiting
	 * in any fd's buffer.  This allows us to quickly dump the entire 
	 * contents of an ircd packet without doing expensive select() calls
	 * inbetween each line.
	 */
	retval = select(global_max_fd + 1, &working_rd, &working_wd, 
				NULL, newtimeout);

	if (retval <= 0)
		return retval;

	for (i = 0; i <= global_max_fd; i++)
	{
	    if (FD_ISSET(i, &working_rd) || FD_ISSET(i, &working_wd))
	    {
		if (!io_rec[i])
			panic("FD %d is ready, but not set up!", i);

		/* If it's clean, read in more data */
		if (io_rec[i]->clean)
		    new_io_event(i);
	    }
	}

	return retval;
}

#endif


static int	unix_read (int fd)
{
	char	buffer[8192];
	int	c;

	c = read(fd, buffer, sizeof(buffer));
	if (c == 0)
		errno = -1;
	else if (c > 0)
		dgets_buffer(fd, buffer, c);
	return c;
}

static int	unix_accept (int fd)
{
	int	newfd;
	SS	addr;
	socklen_t len;

	len = sizeof(addr);
	newfd = Accept(fd, (SA *)&addr, &len);

	dgets_buffer(fd, &newfd, sizeof(newfd));
	dgets_buffer(fd, &addr, sizeof(addr));
	return sizeof(newfd) + sizeof(addr);
}

static int	unix_connect (int fd)
{
	int	gsn_result;
	SS	localaddr;
	int	gpn_result;
	SS	remoteaddr;
	socklen_t len;

	len = sizeof(localaddr);
	errno = 0;
	getsockname(fd, (SA *)&localaddr, &len);
	gsn_result = errno;

	dgets_buffer(fd, &gsn_result, sizeof(gsn_result));
	dgets_buffer(fd, &localaddr, sizeof(localaddr));

	len = sizeof(remoteaddr);
	errno = 0;
	getpeername(fd, (SA *)&remoteaddr, &len);
	gpn_result = errno;

	dgets_buffer(fd, &gpn_result, sizeof(gpn_result));
	dgets_buffer(fd, &remoteaddr, sizeof(localaddr));

	return (sizeof(int) + sizeof(SS)) * 2;
}


/* No KQUEUE support for now. */
