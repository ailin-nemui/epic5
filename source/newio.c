/* $EPIC: newio.c,v 1.9 2002/12/23 15:11:27 jnelson Exp $ */
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
 * Copyright © 1997, 2002 EPIC Software Labs.
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

#if 0
	void		(*read_callback)  (int fd, int numbytes, char *data);
	char *		(*write_callback) (int fd);
	int	strategy;
#endif
}           MyIO;

static	MyIO	**io_rec = NULL;
	int	dgets_errno = 0;

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
int	dgets (char *str, int des, int buffer, void *ssl_aux)
{
	int	cnt = 0, c;
	MyIO	*ioe;

	if (!io_rec)
		init_io();

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
		/* Better safe than sorry... */
		c = SSL_read((SSL *)ssl_aux, ioe->buffer + ioe->write_pos,
				ioe->buffer_size - ioe->write_pos - 1);
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
		*str = 0;
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
		*str = 0;
		dgets_errno = ECONNABORTED;
		return -1;
	    }
	    return 0;
	}

	/*
	 * Slurp up the data that is available into 'str'. 
	 */
	while (ioe->read_pos < ioe->write_pos)
	{
	    if (((str[cnt] = ioe->buffer[ioe->read_pos++])) == '\n')
		break;
	    cnt++;
	}

	/*
	 * Terminate it
	 */
	str[cnt + 1] = 0;

	/*
	 * If we end in a newline, then all is well.
	 * Otherwise, we're unbuffered, tell the caller.
	 * The caller then would need to do a strlen() to get
 	 * the amount of data.
	 */
	if (str[cnt] == '\n')
	    return cnt;
	else
	    return 0;
}

static	int global_max_fd = -1;

/*
 * new_select: works just like select(), execpt I trimmed out the excess
 * parameters I didn't need.
 */
int 	new_select (fd_set *rd, fd_set *wd, Timeval *timeout)
{
static	int	polls = 0;
	Timeval	thetimeout;
	Timeval *newtimeout = &thetimeout;
	int	i,
		set = 0;
	fd_set 	new_f;


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
			FD_ISSET(i, rd) &&
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
		*rd = new_f;
		return set;
	}
	else
		return select(global_max_fd + 1, rd, wd, NULL, newtimeout);
}

/*
 * Register a filedesc for readable events
 * Set up its input buffer
 */
int 	new_open (int des)
{
	if (des < 0)
		return des;		/* Invalid */

	if (!io_rec)
		init_io();

	if (!FD_ISSET(des, &readables))
		FD_SET(des, &readables);
	if (FD_ISSET(des, &writables))
		FD_CLR(des, &writables);

	/*
	 * Keep track of the highest fd in use.
	 */
	if (des > global_max_fd)
		global_max_fd = des;

	return des;
}

/*
 * Register a filedesc for readable events
 * Set up its input buffer
 */
int 	new_open_for_writing (int des)
{
	if (des < 0)
		return des;		/* Invalid */

	if (!io_rec)
		init_io();

	if (!FD_ISSET(des, &writables))
		FD_SET(des, &writables);

	/*
	 * Keep track of the highest fd in use.
	 */
	if (des > global_max_fd)
		global_max_fd = des;

	return des;
}

/*
 * Unregister a filedesc for readable events 
 * and close it down and free its input buffer
 */
int	new_close (int des)
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
			new_free(&io_rec[des]->buffer); 
		new_free((char **)&(io_rec[des]));
	}
	close(des);

	/*
	 * If we're closing the highest fd in use, then we
	 * want to adjust global_max_fd downward to the next highest fd.
	 */
	if (des == global_max_fd)
	{
		do
			des--;
		while (!FD_ISSET(des, &readables));

		global_max_fd = des;
	}
	return -1;
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
