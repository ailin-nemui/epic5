/* $EPIC: newio.c,v 1.30 2005/02/09 02:23:25 jnelson Exp $ */
/*
 * newio.c: This is some handy stuff to deal with file descriptors in a way
 * much like stdio's FILE pointers 
 *
 * IMPORTANT NOTE:  If you use the routines here-in, you shouldn't switch to
 * using normal reads() on the descriptors cause that will cause bad things
 * to happen.  If using any of these routines, use them all 
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
 * This file contains two major sections, which deal with multiplexing I/O 
 * intended for systems that have integer based file descriptors.  Porting
 * this to work on other integer based file descriptor systems should be 
 * pretty easy, and doesn't require changing anything else in EPIC.  Systems
 * that use something other than integers for file descriptors will require
 * more work, but shouldn't be /too/ difficult.
 *
 * In section 1, is "dgets", which does line-buffering on any file
 * descriptor in the same way fgets() does for (FILE *)s.  Everything in 
 * EPIC that does line buffering uses dgets() to handle this job.
 *
 * "dgets" includes these functions:
 *   get_pending_bytes(), which returns the number of bytes buffered by 
 *		the dgets system for a file descriptor.  
 *   init_io(), which initializes the dgets() subsystem.  The name sucks.
 *   unix_read(), which is a dgets() reader function that uses the unix
 *		read(2) system call to fill the buffers.
 *   dgets() which takes a file descriptor, a buffer, a buffer size, and 
 *		a reader callback function.  dgets() works in two steps,
 *		first filling its own buffers with a supplied reader func,
 *		and then returning the first line from this buffer.  Further
 *		lines will be returned in subsequent calls to dgets().
 * Making "dgets" work on non-unix systems is as simple as replacing the 
 * "unix_read()" function with whatever you use on the new system to pull
 * bytes off of a file descriptor.  The rest of EPIC does not need to be
 * changed, though you may want to look at how servers work with SSL.
 * 
 * In section 2, are the "event drivers" which are sets of functions that 
 * handle the management of file descriptors and a "sleep function" which 
 * epic calls when it has nothing else to do, and which does not return 
 * until there is something to do.  See newio.h for a lot more info, but
 * basically there are implementations for unix select(2) and freebsd's 
 * kqueue(2) systems here.  You should be able to write new sets of functions
 * for non-unix systems (like VMS) pretty easily, and nothing else in EPIC
 * need change.
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
	int	(*io_callback) (int, char **, size_t *, size_t *);
}           MyIO;

static	MyIO **	io_rec = NULL;
static	int	global_max_fd = -1;
	int	dgets_errno = 0;

static void	new_io_event (int fd);

/*
 * Get_pending_bytes: What do you think it does?
 */
size_t 	get_pending_bytes (int fd)
{
	if (fd >= 0 && io_rec[fd] && io_rec[fd]->buffer)
		return io_rec[fd]->write_pos - io_rec[fd]->read_pos;

	return 0;
}

static	void	init_io (void)
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

static int	unix_read (int fd, char **buffer, size_t *buffer_size, size_t *start)
{
	int	c;

	c = read(fd, (*buffer) + (*start), (*buffer_size) - (*start) - 1);
	return c;
}

static int	unix_accept (int fd, char **buffer, size_t *buffer_size, size_t *start)
{
	int	c;
	int	newfd;
	SS	addr;
	socklen_t len;

	len = sizeof(addr);
	newfd = Accept(fd, (SA *)&addr, &len);

	if ((*buffer_size) - (*start) < sizeof(newfd) + sizeof(addr))
		panic("unix_accept: Buffer isn't big enough.  "
			"%d bytes left, need %d.", 
				(*buffer) - (*start), 
				sizeof(newfd) + sizeof(addr));

	memcpy((*buffer) + (*start), &newfd, sizeof(newfd));
	memcpy((*buffer) + (*start) + sizeof(newfd), &addr, sizeof(addr));
	return sizeof(newfd) + sizeof(addr);
}

static int	unix_connect (int fd, char **buffer, size_t *buffer_size, size_t *start)
{
	int	gsn_result;
	SS	localaddr;
	int	gpn_result;
	SS	remoteaddr;
	socklen_t len;
	int	x;

	len = sizeof(localaddr);
	errno = 0;
	getsockname(fd, (SA *)&localaddr, &len);
	gsn_result = errno;

	len = sizeof(remoteaddr);
	errno = 0;
	getpeername(fd, (SA *)&remoteaddr, &len);
	gpn_result = errno;

	if ((*buffer_size) - (*start) < (sizeof(SS) + sizeof(int)) * 2)
		panic("unix_accept: Buffer isn't big enough.  "
			"%d bytes left, need %d.", 
				(*buffer) - (*start), 
				(sizeof(SS) + sizeof(int)) * 2);

	x = 0;
	memcpy((*buffer) + (*start) + x, &gsn_result, sizeof(gsn_result));
	x += sizeof(gsn_result);
	memcpy((*buffer) + (*start) + x, &localaddr, sizeof(localaddr));
	x += sizeof(localaddr);
	memcpy((*buffer) + (*start) + x, &gpn_result, sizeof(gpn_result));
	x += sizeof(gpn_result);
	memcpy((*buffer) + (*start) + x, &remoteaddr, sizeof(remoteaddr));
	x += sizeof(remoteaddr);

	return x;
}

/*
 * dgets() is used by:
 *	dcc.c for DCC CHAT (buffer = 1)
 *	dcc.c for DCC RAW (buffer = 0)
 *	exec.c for handling inbound data from execd processes (buffer = 0)
 *	screen.c for reading in from a created screen (buffer = 0)
 *	screen.c for reading stdin when process is in dumb mode. (buffer = 1)
 *	server.c for reading in lines from server (buffer = 1)
 *	server.c for the /flush command (buffer = 0)
 */
/*
 * All new dgets -- no more trap doors!
 *
 * There are at least four ways to look at this function.
 * The most important variable is 'buffer', which determines if
 * we force line buffering.  If it is on, then we will sit on any
 * incomplete lines until they get a newline.  This is the default
 * behavior for server connections, because they *must* be line
 * delineated.  However, when are getting input from an untrusted
 * source (eg, dcc chat, /exec'd process), we cannot assume that every
 * line will be newline delinated.  So in those cases, 'buffer' is 0,
 * and we force a flush on whatever we can slurp, without waiting for
 * a newline.
 *
 * Return values:
 *
 *	-1 -- The file descriptor is dead.  Either an EOF occured, or a
 *	      read() error occured, or the buffer for the filedesc filled
 *	      up, or some other non-recoverable error occured.
 *	 0 -- If the data read in from the file descriptor did not form a 
 *	      complete line, then zero is always returned.  This should be
 *	      considered a stopping condition.  Do not call dgets() again
 *	      after it returns 0, because unless more data is avaiable on
 *	      the fd, it will return -1, which you would misinterpret as an
 *	      error condition.
 *	      If "buffer" is 0, then whatever we have available will be 
 *	      returned in "str".
 *	      If "buffer" is not 0, then we will retain whatever we have
 *	      available, waiting for the newline to occur perhaps next time.
 *	>0 -- If a full, newline terminated line was available, the length
 *	      of the line is returned.
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
		yell("***XXX*** Buffer for des [%d] is zero-length! ***XXX***", des);
		dgets_errno = ENOMEM; /* Cough */
		return -1;
	}

	ioe = io_rec[des];

	if (ioe == NULL)
		panic("dgets called on unsetup fd %d", des);

	if ((dgets_errno = ioe->error))
		return -1;


	/*
	 * So the buffer probably has changed now, because we just read
	 * in more data.  Check again to see if there is a newline.  If
	 * there is not, and the caller wants a complete line, just punt.
	 */
	if (buffer == 1 && !strchr(ioe->buffer + ioe->read_pos, '\n'))
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

	    if (cnt <= buflen - 1)
		buf[cnt++] = h;
	    if (buffer >= 0 && h == '\n')
		break;
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

/* set's socket options */
void	set_socket_options (int s)
{
	int	opt = 1;
	int	optlen = sizeof(opt);
#ifndef NO_STRUCT_LINGER
	struct linger	lin;

	lin.l_onoff = lin.l_linger = 0;
	setsockopt(s, SOL_SOCKET, SO_LINGER, (char *)&lin, optlen);
#endif

	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, optlen);
	opt = 1;
	setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, (char *)&opt, optlen);

#if notyet
	/* This is waiting for nonblock-aware code */
	info = fcntl(fd, F_GETFL, 0);
	info |= O_NONBLOCK;
	fcntl(fd, F_SETFL, info);
#endif
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
	fd_set 	new_f;
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

	FD_ZERO(&new_f);
	for (i = 0; i <= global_max_fd; i++)
	{
		/*
		 * Check to see if there is a complete line sitting in the
		 * fd's buffer.  If there is, then we just tag this fd as
		 * being ready.  No sweat.  Don't mark the fd ready if the
		 * caller didn't ask about the fd, because that leads to 
		 * a busy-loop.
		 */
		if (io_rec[i])
		    if (!io_rec[i]->clean)
			set++;
	}

	/*
	 * Only do the expensive select() call if there are no lines waiting
	 * in any fd's buffer.  This allows us to quickly dump the entire 
	 * contents of an ircd packet without doing expensive select() calls
	 * inbetween each line.
	 */
	if (set)
		return set;

	else
	{
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
}


void	select__do_filedesc (void)
{
	int	i;

	for (i = 0; i <= global_max_fd; i++)
	{
		if (!io_rec[i])
			continue;

		/* Then tell the user they have data ready for them. */
		if (!io_rec[i]->clean)
			io_rec[i]->callback(i);
	}
}

static void	new_io_event (int fd)
{
	MyIO *ioe;
	int	c;

	ioe = io_rec[fd];

	/* If it's dirty, something is very wrong. */
	if (!ioe->clean)
		panic("new_io_event: fd [%d] hasn't been cleaned yet", fd);

	/* DGETS BUFFER MANAGEMENT */
	/* 
	 * An old exploit just sends us characters every .8 seconds without
	 * ever sending a newline.  Cut off anyone who tries that.
	 */
	if (ioe->segments > MAX_SEGMENTS)
	{
		yell("***XXX*** Too many read()s on des [%d] without a "
				"newline! ***XXX***", fd);
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
		ioe->segments = 1;
	}

	/*
	 * Dont try to read into a full buffer.
	 */
	if (ioe->write_pos >= ioe->buffer_size)
	{
		yell("***XXX*** Buffer for des [%d] is filled! ***XXX***", fd);
		ioe->error = ENOMEM; /* Cough */
		ioe->clean = 0;
		return;
	}

	c = ioe->io_callback(fd, &ioe->buffer, &ioe->buffer_size, &ioe->write_pos);
	ioe->clean = 0;

	if (c <= 0)
	{
		ioe->error = errno;
		if (x_debug & DEBUG_INBOUND) 
			yell("FD [%d] FAILED [%d:%s]", 
				fd, c, strerror(ioe->error));
		return;
	}

	if (x_debug & DEBUG_INBOUND) 
		yell("FD [%d], did [%d]", fd, c);

	ioe->write_pos += c;
	ioe->segments++;
}


#endif

/* No KQUEUE support for now. */
