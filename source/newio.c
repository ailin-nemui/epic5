/* $EPIC: newio.c,v 1.21 2004/01/23 08:03:53 jnelson Exp $ */
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

#include "irc.h"
#include "ircaux.h"
#include "output.h"
#include "newio.h"
#include "ssl.h"

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
	char		*buffer;
	size_t		buffer_size;
	unsigned 	read_pos,
			write_pos;
	int		segments;
	int		error;
	void		(*callback) (int fd);
}           MyIO;

static	MyIO	**io_rec = NULL;
	int	dgets_errno = 0;
static	int	global_max_fd = -1;


/*
 * Get_pending_bytes: What do you think it does?
 */
size_t 	get_pending_bytes (int fd)
{
	if (fd >= 0 && io_rec[fd] && io_rec[fd]->buffer)
		return strlen(io_rec[fd]->buffer);

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
ssize_t	dgets (int des, char *buf, size_t buflen, int buffer, void *ssl_aux)
{
	ssize_t	cnt = 0;
	int	c = 0;		/* gcc can die. */
	MyIO	*ioe;

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
	{
		ioe = io_rec[des] = (MyIO *)new_malloc(sizeof(MyIO));
		ioe->buffer_size = IO_BUFFER_SIZE;
		ioe->buffer = (char *)new_malloc(ioe->buffer_size + 2);
		ioe->read_pos = ioe->write_pos = 0;
		ioe->segments = 0;
		ioe->error = 0;
	}

	if (ioe->read_pos == ioe->write_pos)
	{
		ioe->read_pos = ioe->write_pos = 0;
		ioe->buffer[0] = 0;
		ioe->segments = 0;
	}

	if (!strchr(ioe->buffer + ioe->read_pos, '\n'))
	{
	    if (ioe->read_pos)
	    {
		ov_strcpy(ioe->buffer, ioe->buffer + ioe->read_pos);
		ioe->read_pos = 0;
		ioe->write_pos = strlen(ioe->buffer);
		ioe->segments = 1;
	    }

	    /*
	     * Dont try to read into a full buffer.
	     */
	    if (ioe->write_pos >= ioe->buffer_size)
	    {
		yell("***XXX*** Buffer for des [%d] is filled! ***XXX***", des);
		dgets_errno = ENOMEM; /* Cough */
		return -1;
	    }

	    if (ssl_aux)
	    {
#ifndef HAVE_SSL
		panic("Attempt to call SSL_read on non-ssl client");
#else
		int	numbytes;

		/*
		 * I cannot begin to describe what I think of this interface,
		 * if this is really how it works.  I only hope this code 
		 * here is needlessly defensive.  Read a chunk of data from
		 * SSL, and if there is anything left over, make room for it
		 * and tack that onto the end (resizing if necessary),
		 * because we *MUST* transfer everything buffered by SSL to
		 * our buffers before we return.
		 */
		c = SSL_read((SSL *)ssl_aux, ioe->buffer + ioe->write_pos,
				ioe->buffer_size - ioe->write_pos - 1);

		/* BAH! HUMBUG! */
		numbytes = SSL_pending((SSL *)ssl_aux);
		if (numbytes > 0)
		{
		    /* Copied from down below.  Bah.  Humbug. */
		    ioe->buffer[ioe->write_pos + c] = 0;
		    ioe->write_pos += c;
		    ioe->segments++;

		    /* BAH! HUMBUG! AGAIN! */
		    if (numbytes > ioe->buffer_size - ioe->write_pos - 1)
		    {
		       /* XXX Yes, I know this is larger than it needs to be. */
		       ioe->buffer_size += numbytes;
		       RESIZE(ioe->buffer, char, ioe->buffer_size + 2);
		    }

		    c = SSL_read((SSL *)ssl_aux, ioe->buffer + ioe->write_pos,
				ioe->buffer_size - ioe->write_pos - 1);
		}
#endif
	    }
	    else
	    {
		/* Better safe than sorry... */
		c = read(des, ioe->buffer + ioe->write_pos,
				ioe->buffer_size - ioe->write_pos - 1);
	    }

	    if (x_debug & DEBUG_INBOUND) 
		yell("FD [%d], did [%d]", des, c);

	    if (c <= 0)
	    {
		*buf = 0;
		dgets_errno = (c == 0) ? -1 : errno;
		return -1;
	    }
	    ioe->buffer[ioe->write_pos + c] = 0;
	    ioe->write_pos += c;
	    ioe->segments++;
	}

	dgets_errno = 0;

	/*
	 * If the caller wants us to force line buffering, and if there
	 * is no complete line, just stop right here.
	 */
	if (buffer && !strchr(ioe->buffer + ioe->read_pos, '\n'))
	{
	    if (ioe->segments > MAX_SEGMENTS)
	    {
		yell("***XXX*** Too many read()s on des [%d] without a newline! ***XXX***", des);
		*buf = 0;
		dgets_errno = ECONNABORTED;
		return -1;
	    }
	    return 0;
	}

	/*
	 * Slurp up the data that is available into 'buf'. 
	 */
	while (ioe->read_pos < ioe->write_pos)
	{
	    if (((buf[cnt] = ioe->buffer[ioe->read_pos++])) == '\n')
		break;
	    cnt++;
	}

	/* If the line is too long, truncate it. */
	/* 
	 * Before anyone whines about this, a lot of code in epic 
	 * silently assumes that incoming lines from the server don't
	 * exceed 510 bytes.  Until we "fix" all of those cases, it is
	 * better to truncate excessively long lines than to let them
	 * overflow buffers!
	 */
	if (cnt > (ssize_t)(buflen - 1))
	{
		if (x_debug & DEBUG_INBOUND) 
			yell("FD [%d], Too long (did [%d], max [%d])", des, cnt, buflen);

		/* Remember that 'buf' must be 'buflen + 1' bytes big! */
		if (buf[cnt] == '\n')
			buf[buflen - 1] = '\n';
		cnt = buflen - 1;
	}

	/*
	 * Terminate it
	 */
	buf[cnt + 1] = 0;

	/*
	 * If we end in a newline, then all is well.
	 * Otherwise, we're unbuffered, tell the caller.
	 * The caller then would need to do a strlen() to get
 	 * the amount of data.
	 */
	if (buf[cnt] == '\n')
	    return cnt;
	else
	    return 0;
}

/* set's socket options */
void set_socket_options (int s)
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
int 	select__new_open (int des, void (*callback) (int))
{
	if (des < 0)
		return des;		/* Invalid */

	if (!io_rec)
		init_io();

	if (!FD_ISSET(des, &readables))
		FD_SET(des, &readables);
	if (FD_ISSET(des, &writables))
		FD_CLR(des, &writables);
	if (FD_ISSET(des, &held_readables))
		FD_CLR(des, &held_readables);
	if (FD_ISSET(des, &held_writables))
		FD_CLR(des, &held_writables);

	/*
	 * Keep track of the highest fd in use.
	 */
	if (des > global_max_fd)
		global_max_fd = des;

	if (io_rec[des] == NULL)
	{
		MyIO *ioe;
		ioe = io_rec[des] = (MyIO *)new_malloc(sizeof(MyIO));
		ioe->buffer_size = IO_BUFFER_SIZE;
		ioe->buffer = (char *)new_malloc(ioe->buffer_size + 2);
		ioe->read_pos = ioe->write_pos = 0;
		ioe->segments = 0;
		ioe->error = 0;
	}

	io_rec[des]->callback = callback;
	return des;
}

/*
 * Register a filedesc for readable events
 * Set up its input buffer
 */
int 	select__new_open_for_writing (int des, void (*callback) (int))
{
	if (des < 0)
		return des;		/* Invalid */

	if (!io_rec)
		init_io();

	if (!FD_ISSET(des, &writables))
		FD_SET(des, &writables);
	if (!FD_ISSET(des, &held_writables))
		FD_SET(des, &held_writables);

	/*
	 * Keep track of the highest fd in use.
	 */
	if (des > global_max_fd)
		global_max_fd = des;

	if (io_rec[des] == NULL)
	{
		MyIO *ioe;
		ioe = io_rec[des] = (MyIO *)new_malloc(sizeof(MyIO));
		ioe->buffer_size = IO_BUFFER_SIZE;
		ioe->buffer = (char *)new_malloc(ioe->buffer_size + 2);
		ioe->read_pos = ioe->write_pos = 0;
		ioe->segments = 0;
		ioe->error = 0;
	}

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
	if (0 <= des && des <= global_max_fd
		&& FD_ISSET(des, &readables))
	{
		FD_SET(des, &held_readables);
		FD_CLR(des, &readables);
	}

	return des;
}

/*
 * Add the fd again.
 */
int	select__new_unhold_fd (int des)
{
	if (0 <= des && des <= global_max_fd
		&& FD_ISSET(des, &held_readables))
	{
		FD_SET(des, &readables);
		FD_CLR(des, &held_readables);
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
	if (FD_ISSET(des, &held_readables))
		FD_CLR(des, &held_readables);
	if (FD_ISSET(des, &held_writables))
		FD_CLR(des, &held_writables);

	if (io_rec)
	{
		if (io_rec[des])
			new_free(&io_rec[des]->buffer); 
		new_free((char **)&(io_rec[des]));
	}
	close(des);

	/*
	 * If we're closing the highest fd in use, then we
	 * want to adjust global_max_fd downward to the next highest fd.
	 */
	while ( global_max_fd >= 0 &&
		!FD_ISSET(global_max_fd, &readables) &&
		!FD_ISSET(global_max_fd, &held_readables) &&
		!FD_ISSET(global_max_fd, &writables) &&
		!FD_ISSET(global_max_fd, &held_writables))
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

	working_rd = readables;
	working_wd = writables;

	if (timeout)
	{
		thetimeout = *timeout;
		if (timeout->tv_sec == 0 && timeout->tv_usec == 0)
		{
			if (polls++ > 10000)
				panic("Stuck in a polling loop. Help!");
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
		{
		    if (io_rec[i]->read_pos < io_rec[i]->write_pos &&
			FD_ISSET(i, &working_rd) &&
		        strchr(io_rec[i]->buffer + io_rec[i]->read_pos, '\n'))
		    {
			FD_SET(i, &new_f);
			set++;
		    }
		}
	}

	/*
	 * Only do the expensive select() call if there are no lines waiting
	 * in any fd's buffer.  This allows us to quickly dump the entire 
	 * contents of an ircd packet without doing expensive select() calls
	 * inbetween each line.
	 */
	if (set)
	{
		working_rd = new_f;
		return set;
	}
	else
		return select(global_max_fd + 1, &working_rd, &working_wd, 
					NULL, newtimeout);
}


void	select__do_filedesc (void)
{
	int	i;
	fd_set	rd, wd;

	/* 
	 * Make copies of the result fd_set's, for the callbacks
	 * may recursively call io() which will clober the result fds!
	 */
	rd = working_rd;
	wd = working_wd;

	for (i = 0; i <= global_max_fd; i++)
	{
	    if (FD_ISSET(i, &rd) || FD_ISSET(i, &wd))
	    {
		if (!io_rec[i])
			panic("File descriptor [%d] got a callback but it's not set up", i);
		io_rec[i]->callback(i);
		FD_CLR(i, &rd);
		FD_CLR(i, &wd);
	    }
	}
}

#endif


#ifdef USE_FREEBSD_KQUEUE
#include <sys/event.h>

static int		kqueue_fd = -1;
static struct timespec	kqueue_poll = { 0, 0 };
static struct kevent	event;

static void kqueue_init (void)
{
	kqueue_fd = kqueue();
}

static	void	kread (int fd)
{
	struct kevent ev;
	EV_SET(&ev, fd, EVFILT_READ, EV_ADD|EV_ENABLE, 0, 0, NULL);
	kevent(kqueue_fd, &ev, 1, NULL, 0, &kqueue_poll);
}

static	void	knoread (int fd)
{
	struct kevent ev;
	EV_SET(&ev, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
	kevent(kqueue_fd, &ev, 1, NULL, 0, &kqueue_poll);
}

static	void	kholdread (int fd)
{
	struct kevent ev;
	EV_SET(&ev, fd, EVFILT_READ, EV_DISABLE, 0, 0, NULL);
	kevent(kqueue_fd, &ev, 1, NULL, 0, &kqueue_poll);
}

static	void	kunholdread (int fd)
{
	struct kevent ev;
	EV_SET(&ev, fd, EVFILT_READ, EV_ENABLE, 0, 0, NULL);
	kevent(kqueue_fd, &ev, 1, NULL, 0, &kqueue_poll);
}

static	void	kwrite (int fd)
{
	struct kevent ev;
	EV_SET(&ev, fd, EVFILT_WRITE, EV_ADD|EV_ENABLE, 0, 0, NULL);
	kevent(kqueue_fd, &ev, 1, NULL, 0, &kqueue_poll);
}

static	void	knowrite (int fd)
{
	struct kevent ev;
	EV_SET(&ev, fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
	kevent(kqueue_fd, &ev, 1, NULL, 0, &kqueue_poll);
}


/*
 * Register a filedesc for readable events
 * Set up its input buffer
 */
int 	kqueue__new_open (int des, void (*callback) (int))
{
	if (des < 0)
		return des;		/* Invalid */

	if (!io_rec)
		init_io();

	if (kqueue_fd == -1)
		kqueue_init();

	knowrite(des);
	kread(des);

	/*
	 * Keep track of the highest fd in use.
	 */
	if (des > global_max_fd)
		global_max_fd = des;

	if (io_rec[des] == NULL)
	{
		MyIO *ioe;
		ioe = io_rec[des] = (MyIO *)new_malloc(sizeof(MyIO));
		ioe->buffer_size = IO_BUFFER_SIZE;
		ioe->buffer = (char *)new_malloc(ioe->buffer_size + 2);
		ioe->read_pos = ioe->write_pos = 0;
		ioe->segments = 0;
		ioe->error = 0;
	}

	io_rec[des]->callback = callback;
	return des;
}

/*
 * Register a filedesc for readable events
 * Set up its input buffer
 */
int 	kqueue__new_open_for_writing (int des, void (*callback) (int))
{
	if (des < 0)
		return des;		/* Invalid */

	if (!io_rec)
		init_io();

	knoread(des);
	kwrite(des);

	/*
	 * Keep track of the highest fd in use.
	 */
	if (des > global_max_fd)
		global_max_fd = des;

	if (io_rec[des] == NULL)
	{
		MyIO *ioe;
		ioe = io_rec[des] = (MyIO *)new_malloc(sizeof(MyIO));
		ioe->buffer_size = IO_BUFFER_SIZE;
		ioe->buffer = (char *)new_malloc(ioe->buffer_size + 2);
		ioe->read_pos = ioe->write_pos = 0;
		ioe->segments = 0;
		ioe->error = 0;
	}

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
int	kqueue__new_hold_fd (int des)
{
	if (des < 0 || des > global_max_fd)
		return -1;			/* Bad FD */

	kholdread(des);

	return des;
}

/*
 * Add the fd again.
 */
int	kqueue__new_unhold_fd (int des)
{
	if (des < 0 || des > global_max_fd)
		return -1;			/* Bad FD */

	kunholdread(des);

	return des;
}

/*
 * Unregister a filedesc for readable events 
 * and close it down and free its input buffer
 */
int	kqueue__new_close (int des)
{
	int	i;

	if (des < 0)
		return -1;

	if (io_rec)
	{
		if (io_rec[des])
			new_free(&io_rec[des]->buffer); 
		new_free((char **)&(io_rec[des]));
	}

	knoread(des);
	knowrite(des);
	close(des);

	/*
	 * If we're closing the highest fd in use, then we
	 * want to adjust global_max_fd downward to the next highest fd.
	 */
	if (des == global_max_fd)
	{
	    for (i = IO_ARRAYLEN - 1; i >= 0; i--)
	    {
		if (io_rec[i] != NULL)
		{
			global_max_fd = i;
			break;
		}
	    }
	}

	return -1;
}


/*
 * new_select: works just like select(), execpt I trimmed out the excess
 * parameters I didn't need.
 */
int 	kqueue__do_wait (Timeval *timeout)
{
static	int	polls = 0;
struct timespec thetimeout;
struct timespec *newtimeout = &thetimeout;
	int	i;

	if (timeout)
	{
		thetimeout.tv_sec = timeout->tv_sec;
		thetimeout.tv_nsec = timeout->tv_usec * 1000;

		if (timeout->tv_sec == 0 && timeout->tv_usec == 0)
		{
			if (polls++ > 10000)
				panic("Stuck in a polling loop. Help!");
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
		if (io_rec[i])
		{
		    if (io_rec[i]->read_pos < io_rec[i]->write_pos &&
		        strchr(io_rec[i]->buffer + io_rec[i]->read_pos, '\n'))
		    {
			EV_SET(&event, i, EVFILT_READ, 0, 0, 1, NULL);
			return 1;
		    }
		}
	}

	return kevent(kqueue_fd, NULL, 0, &event, 1, newtimeout);
}


void	kqueue__do_filedesc (void)
{
	int	i;

	i = event.ident;
	if (!io_rec[i])
		panic("File descriptor [%d] got a callback but it's not set up", i);
	io_rec[i]->callback(i);
}

#endif

