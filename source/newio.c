/* $EPIC: newio.c,v 1.39 2005/03/01 00:54:55 jnelson Exp $ */
/*
 * newio.c:  Passive, callback-driven IO handling for sockets-n-stuff.
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright © 1997, 2005 EPIC Software Labs.
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
#include "timer.h"
#include <pthread.h>

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
	int	channel;		/* XXX Future expansion XXX */
	char *	buffer;
	size_t	buffer_size,
		read_pos,
		write_pos;
	short	segments,
		error,
		clean,
		held;
	void	(*callback) (int vfd);
	int	(*io_callback) (int vfd);
}           MyIO;

static	MyIO **	io_rec = NULL;
static	int	global_max_vfd = -1;
static	int	global_max_channel = -1;
	int	dgets_errno = 0;

/* These functions should be exposed by your i/o strategy */
static  void    kread (int vfd);
static  void    knoread (int vfd);
static  void    kholdread (int vfd);
static  void    kunholdread (int vfd);
static  void    kwrite (int vfd);
static  void    knowrite (int vfd);
static	int	kdoit (Timeval *timeout);
static	void	kinit (void);
static	void	kcleaned (int vfd);
static	void	klock (void);
static	void	kunlock (void);

/* These functions implement basic i/o operations for unix */
static int	unix_read (int channel);
static int	unix_recv (int channel);
static int	unix_accept (int channel);
static int	unix_connect (int channel);
static int	unix_close (int channel);

/* 
 * On systems where vfd != channel, you need these functions to 
 * map between the two.  You don't want to use these on unix,
 * because they're unnecessarily expensive.
 */
#ifdef VIRTUAL_FILEDESCRIPTORS
static int	get_vfd_by_channel (int channel) 
{
	int	vfd;
	for (vfd = 0; vfd <= global_max_vfd; vfd++)
	    if (io_rec[vfd] && io_rec[vfd]->channel == channel)
		return vfd;
	return -1;
}
#define VFD(channel) get_vfd_by_channel(channel)

static	int	get_new_vfd (int channel)
{
	int	vfd;

	for (vfd = 0; vfd <= global_max_vfd; vfd++)
	    if (io_rec[vfd] && io_rec[vfd]->channel == channel)
		return vfd;
	for (vfd = 0; vfd <= global_max_vfd; vfd++)
	    if (io_rec[vfd] == NULL)
		return vfd;
	return global_max_vfd + 1;
}

static int	get_channel_by_vfd (int vfd)
{
	if (!io_rec[vfd])
		panic("get_channel_by_vfd(%d): vfd is not set up!");
	return io_rec[vfd]->channel;
}
#define CHANNEL(vfd) get_channel_by_vfd(vfd);
#else
#define VFD(channel) channel
#define CHANNEL(vfd) vfd
#define get_new_vfd(channel) channel
#endif


/**************************************************************************/
/*
 * Call this function when an I/O operation completes and data is available
 * to be given to the user.  On systems where channel != vfd, it is expected
 * that you would more likely have the channel than the vfd, so we require
 * that.
 */
int	dgets_buffer (int channel, void *data, ssize_t len)
{
	MyIO *	ioe;
	int	vfd;

	if (len < 0)
		return 0;			/* XXX ? */

	klock();
	vfd = VFD(channel);
	if (!(ioe = io_rec[vfd]))
		panic("dgets called on unsetup channel %d", channel);

	/* 
	 * An old exploit just sends us characters every .8 seconds without
	 * ever sending a newline.  Cut off anyone who tries that.
	 */
	if (ioe->segments > MAX_SEGMENTS)
	{
		yell("***XXX*** Too many read()s on channel [%d] without a "
				"newline! ***XXX***", channel);
		ioe->error = ECONNABORTED;
		ioe->clean = 0;
		kunlock();
		return -1;
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
		size_t	mlen;

		mlen = ioe->write_pos - ioe->read_pos;
		memmove(ioe->buffer, ioe->buffer + ioe->read_pos, mlen);
		ioe->read_pos = 0;
		ioe->write_pos = mlen;
		ioe->buffer[mlen] = 0;
		ioe->segments = 1;
	}

	if ((ssize_t)ioe->buffer_size - (ssize_t)ioe->write_pos < len)
	{
		while ((ssize_t)ioe->buffer_size - (ssize_t)ioe->write_pos < len)
			ioe->buffer_size += IO_BUFFER_SIZE;
		RESIZE(ioe->buffer, char, ioe->buffer_size);
	}

	memcpy((ioe->buffer) + (ioe->write_pos), data, len);
	ioe->write_pos += len;
	ioe->clean = 0;
	ioe->segments++;
	kunlock();
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
 * 1) vfd    - A "dirty" newio file descriptor.  You know that a newio file 
 *	       descriptor is dirty when your new_open() callback is called.
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
 *			the error (-100 means EOF).
 *		 0	There is no data available to read.
 *		>0	The number of bytes returned.
 *	buffer == 0		(The results are null terminated)
 *		-1	The file descriptor is dead.  dgets_errno contains
 *			the error (-100 means EOF).
 *		 0	Some data was returned, but it was an incomplete line.
 *		>0	A full line of data was returned.
 *	buffer == 1		(The results are null terminated)
 *		-1	The file descriptor is dead.  dgets_errno contains
 *			the error (-100 means EOF).
 *		 0	No data was returned.
 *		>0	A full line of data was returned.
 */
ssize_t	dgets (int vfd, char *buf, size_t buflen, int buffer)
{
	size_t	cnt = 0;
	size_t	consumed = 0;
	char	h = 0;
	MyIO *	ioe;

	if (buflen == 0)
	{
	    yell("**XX** Buffer for vfd [%d] is zero-length! **XX**", vfd);
	    dgets_errno = ENOMEM; /* Cough */
	    return -1;
	}

	if (!(ioe = io_rec[vfd]))
		panic("dgets called on unsetup vfd %d", vfd);

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
		kcleaned(vfd);
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
	{
		ioe->read_pos = ioe->write_pos = 0;
		ioe->clean = 1;
		kcleaned(vfd);
	}

	/* Remember, you can't use 'ioe' after this point! */
	ioe = NULL;	/* XXX Don't try to cheat! XXX */

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
			yell("VFD [%d], Truncated (did [%d], max [%d])", 
					vfd, consumed, cnt);

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

/*************************************************************************/
/*
 * do_wait: called when all of the fd's are clean, and we want to go to sleep
 * until an fd is dirty again (or a timer goes off)
 */
int 	do_wait (Timeval *timeout)
{
static	int	polls = 0;
	int	vfd;

	/*
	 * Sanity Check -- A polling loop is caused when the
	 * timeout is 0, and occurs whenever timer needs to go off.  The
	 * main io() loop depends on us to tell it when a timer wants to go
	 * off by returning 0, so we check for that here.
	 *
	 * If we get more than 10,000 polls in a row, then something is 
	 * broken somewhere else, and we need to abend.
	 */
	if (timeout)
	{
	    if (timeout->tv_sec == 0 && timeout->tv_usec == 0)
	    {
		if (polls++ > 10000)
		{
		    dump_timers();
		    panic("Stuck in a polling loop. Help!");
		}
	    }
	    else
		polls = 0;
	}

	/* 
	 * It is possible that as a result of the previous I/O run, we are
	 * running recursively in io(). (/WAIT, /REDIRECT, etc).  This will
	 * obviously result in the IO buffers not being cleaned yet.  So we
	 * check for whether there are any dirty buffers, and if there are,
	 * we shall just return and allow them to be cleaned.
	 */
	for (vfd = 0; vfd <= global_max_vfd; vfd++)
		if (io_rec[vfd] && !io_rec[vfd]->clean)
			return 1;

	/*
	 * Now we go to sleep!  kdoit() doesn't return until either
	 * 	1) The timeout expires, or
	 *	2) Some fd is dirty
	 */
	return kdoit(timeout);
}

/*
 * do_filedesc -- called when fds are dirty, and we want to process them 
 * until they are clean!
 */
void	do_filedesc (void)
{
	int	vfd;

	for (vfd = 0; vfd <= global_max_vfd; vfd++)
	{
		/* Then tell the user they have data ready for them. */
		while (io_rec[vfd] && !io_rec[vfd]->clean)
			io_rec[vfd]->callback(vfd);
	}
}


/***********************************************************************/
void	init_newio (void)
{
	int	vfd;
	int	max_fd = IO_ARRAYLEN;

	if (io_rec)
		panic("init_newio() called twice.");

	io_rec = (MyIO **)new_malloc(sizeof(MyIO *) * max_fd);
	for (vfd = 0; vfd < max_fd; vfd++)
		io_rec[vfd] = NULL;

	kinit();
}

/*
 * Get_pending_bytes: What do you think it does?
 */
size_t 	get_pending_bytes (int vfd)
{
	if (vfd >= 0 && io_rec[vfd] && io_rec[vfd]->buffer)
		return io_rec[vfd]->write_pos - io_rec[vfd]->read_pos;

	return 0;
}

/****************************************************************************/
/*
 * Register a filedesc for readable events
 * Set up its input buffer
 * Returns an vfd!
 */
int 	new_open (int channel, void (*callback) (int), int io_type)
{
	MyIO *ioe;
	int	vfd;

	if (channel < 0)
		return channel;		/* Invalid */

	vfd = get_new_vfd(channel);

	/*
	 * Keep track of the highest fd in use.
	 */
	if (vfd > global_max_vfd)
		global_max_vfd = vfd;
	if (channel > global_max_channel)
		global_max_channel = channel;

	if (!(ioe = io_rec[vfd]))
	{
		ioe = io_rec[vfd] = (MyIO *)new_malloc(sizeof(MyIO));
		ioe->buffer_size = IO_BUFFER_SIZE;
		ioe->buffer = (char *)new_malloc(ioe->buffer_size + 2);
	}

	ioe->channel = channel;
	ioe->read_pos = ioe->write_pos = 0;
	ioe->segments = 0;
	ioe->error = 0;
	ioe->clean = 1;
	ioe->held = 0;

	if (io_type == NEWIO_READ)
		ioe->io_callback = unix_read;
	else if (io_type == NEWIO_ACCEPT)
		ioe->io_callback = unix_accept;
#ifdef HAVE_SSL
	else if (io_type == NEWIO_SSL_READ)
	{
		ioe->io_callback = ssl_read;
		startup_ssl(vfd, channel);
	}
#endif
	else if (io_type == NEWIO_CONNECT)
		ioe->io_callback = unix_connect;
	else if (io_type == NEWIO_RECV)
		ioe->io_callback = unix_recv;
	else
		panic("New_open doesn't recognize io type %d", io_type);

	ioe->callback = callback;

	if (io_type == NEWIO_CONNECT)
	{
		knowrite(vfd);
		knoread(vfd);
		kwrite(vfd);
		kcleaned(vfd);
	}
	else
	{
		knoread(vfd);
		knowrite(vfd);
		kread(vfd);
		kcleaned(vfd);
	}

	return vfd;
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
int	new_hold_fd (int vfd)
{
	if (vfd >= 0 && vfd <= global_max_vfd && io_rec[vfd]->held == 0)
	{
		io_rec[vfd]->held = 1;
		kholdread(vfd);
	}

	return vfd;
}

/*
 * Add the fd again.
 */
int	new_unhold_fd (int vfd)
{
	if (vfd >= 0 && vfd <= global_max_vfd && io_rec[vfd]->held == 1)
	{
		kunholdread(vfd);
		io_rec[vfd]->held = 0;
	}

	return vfd;
}

/*
 * Unregister a filedesc for readable events 
 * and close it down and free its input buffer
 */
int	new_close (int vfd)
{
	MyIO *	ioe;
	int	channel;

	if (vfd < 0 || vfd > global_max_vfd)
		return -1;

	knoread(vfd);
	knowrite(vfd);

	if ((ioe = io_rec[vfd]))
	{
#ifdef HAVE_SSL
		if (ioe->io_callback == ssl_read)	/* XXX */
			shutdown_ssl(ioe->channel);
#endif
		unix_close(ioe->channel);
		new_free(&ioe->buffer); 
		new_free((char **)&(io_rec[vfd]));
	}
	else
		unix_close(vfd);		/* XXX */

	/*
	 * If we're closing the highest fd in use, then we
	 * want to adjust global_max_vfd downward to the next highest fd.
	 */
	if (vfd == global_max_vfd)
	    while (global_max_vfd >= 0 && !io_rec[global_max_vfd])
		global_max_vfd--;

	return -1;
}


/***********************************************************************/
/******************** Start of unix-specific code here ******************/
/************************************************************************/
static int	unix_close (int channel)
{
	return close(channel);
}

static int	unix_read (int channel)
{
	ssize_t	c;
	char	buffer[8192];

	c = read(channel, buffer, sizeof buffer);
	if (c == 0)
		errno = -100;		/* -100 is EOF */
	else if (c > 0)
		dgets_buffer(channel, buffer, c);
	return c;
}

static int	unix_recv (int channel)
{
	ssize_t	c;
	char	buffer[8192];

	c = recv(channel, buffer, sizeof buffer, 0);
	if (c == 0)
		errno = -100;		/* -100 is EOF */
	else if (c > 0)
		dgets_buffer(channel, buffer, c);
	return c;
}

static int	unix_accept (int channel)
{
	int	newfd;
	SS	addr;
	socklen_t len;

#ifdef USE_PTHREADS
	/* XXX I hate this, but pthreads don't play nice with nonblocking. */
	{
		fd_set	fdset;
		FD_ZERO(&fdset);
		FD_SET(channel, &fdset);
		select(channel + 1, &fdset, NULL, NULL, NULL);
	}
#endif

	len = sizeof(addr);
	newfd = Accept(channel, (SA *)&addr, &len);
	if (newfd < 0)
		yell("Accept() failed: %s", strerror(errno));
	dgets_buffer(channel, &newfd, sizeof(newfd));
	dgets_buffer(channel, &addr, sizeof(addr));
	return sizeof(newfd) + sizeof(addr);
}

static int	unix_connect (int channel)
{
	int	gsn_result;
	SS	localaddr;
	int	gpn_result;
	SS	remoteaddr;
	socklen_t len;

#ifdef USE_PTHREADS
	/* XXX I hate this, but pthreads don't play nice with nonblocking. */
	{
		fd_set	fdset;
		FD_ZERO(&fdset);
		FD_SET(channel, &fdset);
		select(channel + 1, NULL, &fdset, NULL, NULL);
	}
#endif

	len = sizeof(localaddr);
	errno = 0;
	getsockname(channel, (SA *)&localaddr, &len);
	gsn_result = errno;

	dgets_buffer(channel, &gsn_result, sizeof(gsn_result));
	dgets_buffer(channel, &localaddr, sizeof(localaddr));

	len = sizeof(remoteaddr);
	errno = 0;
	getpeername(channel, (SA *)&remoteaddr, &len);
	gpn_result = errno;

	dgets_buffer(channel, &gpn_result, sizeof(gpn_result));
	dgets_buffer(channel, &remoteaddr, sizeof(localaddr));

	return (sizeof(int) + sizeof(SS)) * 2;
}


/*
 * Perform a synchronous i/o operation on a file descriptor.  This should
 * result in a call to dgets_buffer() (c > 0) or some sort of error condition
 * (c <= 0).
 */
static void	new_io_event (int vfd)
{
	MyIO *ioe;
	int	c;

	if (!(ioe = io_rec[vfd]))
		panic("new_io_event: vfd [%d] isn't set up!", vfd);

	/* If it's dirty, something is very wrong. */
	if (!ioe->clean)
		panic("new_io_event: vfd [%d] hasn't been cleaned yet", vfd);

	if ((c = ioe->io_callback(vfd)) <= 0)
	{
		ioe->error = errno;
		ioe->clean = 0;
		if (x_debug & DEBUG_INBOUND) 
			yell("VFD [%d] FAILED [%d:%s]", 
				vfd, c, my_strerror(vfd, ioe->error));
		return;
	}

	if (x_debug & DEBUG_INBOUND) 
		yell("VFD [%d], did [%d]", vfd, c);
}


/************************************************************************/
/************************************************************************/
/*
 * Implementation of select() front-end to synchronous unix system calls
 */
#ifdef USE_SELECT
fd_set	readables, writables;

static void kinit (void)
{ 
	FD_ZERO(&readables);
	FD_ZERO(&writables);
}
 
static  void    kread (int vfd) { FD_SET(CHANNEL(vfd), &readables); }
static  void    knoread (int vfd) { FD_CLR(CHANNEL(vfd), &readables); } 
static  void    kholdread (int vfd) { FD_CLR(CHANNEL(vfd), &readables); }
static  void    kunholdread (int vfd) { FD_SET(CHANNEL(vfd), &readables); }
static  void    kwrite (int vfd) { FD_SET(CHANNEL(vfd), &writables); }
static  void    knowrite (int vfd) { FD_CLR(CHANNEL(vfd), &writables); }
static	void	kcleaned (int vfd) { return; }

static	int	kdoit (Timeval *timeout)
{
	fd_set	working_rd, working_wd;
	int	retval, channel;

	working_rd = readables;
	working_wd = writables;
	errno = 0;
	retval = select(global_max_channel + 1, &working_rd, &working_wd, 
			NULL, timeout);
	if (retval <= 0)
	{
	    if (errno == EBADF)
	    {
		struct timeval t = {0, 0};

		for (channel = 0; channel <= global_max_channel; channel++)
		{
		    FD_ZERO(&working_rd);
		    FD_SET(channel, &working_rd);
		    errno = 0;
		    retval = select(channel + 1, &working_rd, NULL, NULL, &t);
		    if (retval < 0)
		    {
			yell("Closing channel %d because: %s", 
					channel, strerror(errno));
			FD_CLR(channel, &readables);
			FD_CLR(channel, &writables);
			new_close(channel);
		    }
		}
	    }

	    return retval;
	}

	for (channel = 0; channel <= global_max_channel; channel++)
	{
		if (FD_ISSET(channel, &working_rd) ||
		    FD_ISSET(channel, &working_wd))
			new_io_event(VFD(channel));
	}

	return retval;
}

static	void	klock (void) { return; }
static	void	kunlock (void) { return; }
#endif

/************************************************************************/
/*
 * Implementation of kqueue() front-end to synchronous unix system calls
 */
#ifdef USE_FREEBSD_KQUEUE
#include <sys/event.h>

static int              kqueue_fd = -1;
static struct timespec  kqueue_poll = { 0, 0 };

static void kinit (void)
{ 
        kqueue_fd = kqueue();
}
 
static  void    kread (int vfd)
{
	struct kevent ev;
	EV_SET(&ev, CHANNEL(vfd), EVFILT_READ, EV_ADD|EV_ENABLE, 0, 0, NULL);
	kevent(kqueue_fd, &ev, 1, NULL, 0, &kqueue_poll);
}
 
static  void    knoread (int vfd)
{
	struct kevent ev;
	EV_SET(&ev, CHANNEL(vfd), EVFILT_READ, EV_DELETE, 0, 0, NULL);
	kevent(kqueue_fd, &ev, 1, NULL, 0, &kqueue_poll);
}
 
static  void    kholdread (int vfd)
{
	struct kevent ev;
	EV_SET(&ev, CHANNEL(vfd), EVFILT_READ, EV_DISABLE, 0, 0, NULL);
	kevent(kqueue_fd, &ev, 1, NULL, 0, &kqueue_poll);
}

static  void    kunholdread (int vfd)
{
	struct kevent ev;
	EV_SET(&ev, CHANNEL(vfd), EVFILT_READ, EV_ENABLE, 0, 0, NULL);
	kevent(kqueue_fd, &ev, 1, NULL, 0, &kqueue_poll);
}

static  void    kwrite (int vfd)
{
	struct kevent ev;
	EV_SET(&ev, CHANNEL(vfd), EVFILT_WRITE, EV_ADD|EV_ENABLE, 0, 0, NULL);
	kevent(kqueue_fd, &ev, 1, NULL, 0, &kqueue_poll);
}

static  void    knowrite (int vfd)
{
	struct kevent ev;
	EV_SET(&ev, CHANNEL(vfd), EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
	kevent(kqueue_fd, &ev, 1, NULL, 0, &kqueue_poll);
}

static	void	kcleaned (int vfd) { return; }

static	int	kdoit (Timeval *timeout)
{
	struct timespec	to;
	struct kevent	event;
	int		retval, channel;

	to.tv_sec = timeout->tv_sec;
	to.tv_nsec = timeout->tv_usec * 1000;
	retval = kevent(kqueue_fd, NULL, 0, &event, 1, &to);
	if (retval <= 0)
		return retval;

	channel = event.ident;
	new_io_event(VFD(channel));
	return retval;
}

static	void	klock (void) { return; }
static	void	kunlock (void) { return; }
#endif

/************************************************************************/
/*
 * Implementation of poll() front-end to synchronous unix system calls
 */
#ifdef USE_POLL
#include <poll.h>

struct pollfd *	polls = NULL;

static void kinit (void)
{ 
	int	vfd;
	int	max_fd = IO_ARRAYLEN;

	polls = (struct pollfd *)new_malloc(sizeof(struct pollfd) * max_fd);
	for (vfd = 0; vfd < max_fd; vfd++)
	{
		polls[vfd].fd = -1;
		polls[vfd].events = 0;
		polls[vfd].revents = 0;
	}
}
 
static  void    kread (int vfd)
{
	polls[vfd].fd = CHANNEL(vfd);
	polls[vfd].events |= POLLIN;
}
 
static  void    knoread (int vfd)
{
	polls[vfd].events &= ~(POLLIN);
	if (polls[vfd].events == 0)
		polls[vfd].fd = -1;
}
 
static  void    kholdread (int vfd)
{
	polls[vfd].events &= ~(POLLIN);
}

static  void    kunholdread (int vfd)
{
	polls[vfd].events |= POLLIN;
}

static  void    kwrite (int vfd)
{
	polls[vfd].fd = CHANNEL(vfd);
	polls[vfd].events |= POLLOUT;
}

static  void    knowrite (int vfd)
{
	polls[vfd].events &= ~(POLLOUT);
	if (polls[vfd].events == 0)
		polls[vfd].fd = -1;
}

static	void	kcleaned (int vfd) { return; }

static	int	kdoit (Timeval *timeout)
{
	int	ms;
	int	retval, vfd;

	ms = timeout->tv_sec * 1000;
	ms += (timeout->tv_usec / 1000);
	retval = poll(polls, global_max_vfd + 1, ms);
	if (retval <= 0)
		return retval;

	for (vfd = 0; vfd <= global_max_vfd; vfd++)
	{
	    if (polls[vfd].revents)
		    new_io_event(vfd);
	}

	return retval;
}

static	void	klock (void) { return; }
static	void	kunlock (void) { return; }
#endif


/************************************************************************/
/*
 * Implementation of pthread front-end to synchronous unix system calls
 */
#ifdef USE_PTHREADS
#include <pthread.h>

struct pthread_stuff {
	char 		buffer[8192];
	pthread_t	pthread;
	int		active;
};
typedef struct pthread_stuff PS;

PS *			pfd = NULL;
pthread_mutex_t		mutex;
pthread_cond_t		cond;

pthread_t		global;

static	void *	pthread_io_event (void *vvfd)
{
	int	vfd;

	vfd = *(int *)vvfd;
	new_free(&vvfd);

	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	new_io_event(vfd);
	klock();
	pthread_cond_signal(&cond);
	kunlock();
	pthread_exit(NULL);
}

static void	kinit (void)
{ 
	int	vfd;
	int	max_fd = IO_ARRAYLEN;

	global = pthread_self();
	pfd = (PS *)new_malloc(sizeof(PS) * max_fd);
	for (vfd = 0; vfd < max_fd; vfd++)
	{
	    pfd[vfd].buffer[0] = 0;
	    memset(&pfd[vfd].pthread, 0, sizeof(pfd[vfd].pthread));
	    pfd[vfd].active = 0;
	}

	pthread_mutex_init(&mutex, NULL);
	klock();
	pthread_cond_init(&cond, NULL);
}
 
static  void    kread (int vfd)
{
	if (pfd[vfd].active)
		panic("vfd [%d] already is active (reading)");
}
 
static  void    knoread (int vfd)
{
	if (pfd[vfd].active == 0)
		return;

	pthread_cancel(pfd[vfd].pthread);
	pthread_join(pfd[vfd].pthread, NULL);
	pfd[vfd].active = 0;
}
 
static  void    kholdread (int vfd)
{
	if (pfd[vfd].active == 0)
		return;

/*
	pthread_cancel(pfd[vfd].pthread);
*/
}

static  void    kunholdread (int vfd)
{
	if (pfd[vfd].active == 0)
		return;

/*
	pthread_kill(pfd[vfd].pthread, 19);
*/
}

static  void    kwrite (int vfd)
{
	if (pfd[vfd].active)
		panic("vfd [%d] already is active (writing)", vfd);
}

static  void    knowrite (int vfd)
{
	if (pfd[vfd].active == 0)
		return;

	pthread_cancel(pfd[vfd].pthread);
	pthread_join(pfd[vfd].pthread, NULL);
	pfd[vfd].active = 0;
}

static	void	kcleaned (int vfd) 
{
	int *mvfd;

	if (!pthread_equal(pthread_self(), global))
		panic("kcleaned not called from global thread");

	if (pfd[vfd].active == 1)
		panic("vfd [%d] is already active (kcleaned)", vfd);

	if (io_rec[vfd]->clean == 0)
		panic("vfd [%d] is not really clean.", vfd);

	mvfd = new_malloc(sizeof *mvfd);
	*mvfd = vfd;
	pthread_create(&pfd[vfd].pthread, NULL, pthread_io_event, mvfd);
	vfd[pfd].active = 1;
}

static	int	kdoit (Timeval *timeout)
{
	struct timespec	to;
	int	retval;
	int	vfd;
	int	saved_errno = 0;
	int	ready = 0;
	struct	timespec right_now;

	if (!pthread_equal(pthread_self(), global))
		panic("kdoit not called from global thread");

	clock_gettime(CLOCK_REALTIME, &right_now);
	right_now.tv_nsec += (timeout->tv_usec * 1000);
	if (right_now.tv_nsec > 1000000000)
	{
		right_now.tv_nsec -= 1000000000;
		right_now.tv_sec++;
	}
	right_now.tv_sec += timeout->tv_sec;

	retval = pthread_cond_timedwait(&cond, &mutex, &right_now);
	if (retval == 0)
		retval = 1;
	else if (retval == ETIMEDOUT)
		retval = 0;
	else
	{
		retval = -1;
		saved_errno = retval;
	}

	/* Reap any child threads that are finished. */
	for (vfd = 0; vfd <= global_max_vfd; vfd++)
	{
	    if (io_rec[vfd] && io_rec[vfd]->clean == 0 && pfd[vfd].active)
	    {
		int c;

		if ((c = pthread_join(pfd[vfd].pthread, NULL)))
			yell("pthread_join returned [%c]", c);
		pfd[vfd].active = 0;
		ready++;
	    }
	}

	errno = saved_errno;
	if (ready)
		return ready;

	return retval;
}

static	void	klock	(void)
{
	int	c;

	if ((c = pthread_mutex_lock(&mutex)))
		panic("pthread_mutex_lock: %s", strerror(c));
}

static	void	kunlock (void)
{
	int	c;

	if ((c = pthread_mutex_unlock(&mutex)))
		panic("pthread_mutex_unlock: %s", strerror(c));
}

#endif


