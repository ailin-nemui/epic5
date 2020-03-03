/*
 * newio.c:  Passive, callback-driven IO handling for sockets-n-stuff.
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright 1997, 2007 EPIC Software Labs.
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
#ifdef USE_PTHREAD
#include <pthread.h>
#endif

/* This is still an experimental feature. */
/* #define VIRTUAL_FILEDESCRIPTORS */

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
	/* 
	 * On Unix, "Channel" means "fd".
	 * On VMS, "Channel" is a random 32 bit int.
	 */
	int	channel;	
	char *	buffer;
	size_t	buffer_size,
		read_pos,
		write_pos;
	short	segments,
		error,
		clean,
		held;
	void	(*callback) (int vfd);
	int	(*io_callback) (int vfd, int quiet);
	void	(*failure_callback) (int channel, int error);
	int	quiet;
	int	server;			/* For message routing */
}           MyIO;

static	MyIO **	io_rec = NULL;
static	int	global_max_vfd = -1;
static	int	global_max_channel = -1;

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

static	int	ksleep (double dur);
static	int	kreadable (int vfd, double);
static	int	kwritable (int vfd, double);

/* These functions implement basic i/o operations for unix */
static int	unix_read (int channel, int);
static int	unix_recv (int channel, int);
static int	unix_accept (int channel, int);
static int	unix_connect (int channel, int);
static int	unix_close (int channel, int);

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
		panic(1, "get_channel_by_vfd(%d): vfd is not set up!");
	return io_rec[vfd]->channel;
}
#define CHANNEL(vfd) get_channel_by_vfd(vfd);
#else
#define VFD(channel) channel
#define CHANNEL(vfd) vfd
#define get_new_vfd(channel) channel
#endif

int	get_server_by_vfd (int vfd)
{
	if (!io_rec[vfd])
		panic(1, "get_server_by_vfd(%d): vfd is not set up!", vfd);
	return io_rec[vfd]->server;
}
/* #define SRV(vfd) get_server_by_vfd(vfd) */ /* Defined in newio.h */
#define CSRV(channel) get_server_by_vfd(VFD(channel))

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
		panic(1, "dgets called on unsetup channel %d", channel);

	/* 
	 * An old exploit just sends us characters every .8 seconds without
	 * ever sending a newline.  Cut off anyone who tries that.
	 */
	if (ioe->segments > MAX_SEGMENTS)
	{
		if (!ioe->quiet)
		    syserr(ioe->server, 
			"dgets_buffer: Too many read()s on channel [%d] "
			"without a newline -- shutting off bad peer", channel);
		ioe->error = -1;
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
 *	-2	Fully buffered.  Don't return anything if there aren't buflen
		bytes free.
 *	-1	No line buffering, return everything.
 *	 0	Partial line buffering.  Return a line of data if there is
 *		one; Return everything if there isn't one.
 *	 1	Full line buffering.  Return a line of data if there is one,
 *		Return nothing if there isn't one.
 *
 * Return values:
 *	buffer == -2		(The results are NOT null terminated)
 *		-1	The file descriptor is dead
 *		 0	There is not "buflen" bytes available to be read
 *		>0	The number of bytes returned.
 *	buffer == -1		(The results are NOT null terminated)
 *		-1	The file descriptor is dead
 *		 0	There is no data available to read.
 *		>0	The number of bytes returned.
 *	buffer == 0		(The results are null terminated)
 *		-1	The file descriptor is dead
 *		 0	Some data was returned, but it was an incomplete line.
 *		>0	A full line of data was returned.
 *	buffer == 1		(The results are null terminated)
 *		-1	The file descriptor is dead
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
	    syserr(SRV(vfd),
			"dgets: Destination buffer for vfd [%d] is zero length."
			" This is surely a bug.", vfd);
	    return -1;
	}

	if (!(ioe = io_rec[vfd]))
		panic(1, "dgets called on unsetup vfd %d", vfd);

	if (ioe->error)
	{
	    if (!ioe->quiet)
	       syserr(SRV(vfd), "dgets: fd [%d] must be closed", vfd);
	    return -1;
	}


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
	 * So if the caller wants 'buflen' bytes, and we don't have it,
	 * then mark the buffer clean and wait for more.
	 */
	if (buffer == -2 && ioe->write_pos - ioe->read_pos < buflen)
	{
		yell("dgets: Wanted %ld bytes, have %ld bytes", 
			(long)(ioe->write_pos - ioe->read_pos), (long)buflen);
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
			yell("VFD [%d], Truncated (did [%ld], max [%ld])", 
					vfd, (long)consumed, (long)cnt);

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
	if (buffer < 0 || (cnt > 0 && buf[cnt - 1] == '\n'))
	    return cnt;
	else
	    return 0;
}

/*************************************************************************/
/*
 * do_wait -- The main sleeping routine.  When all of the fd's are clean,
 *	      go to sleep until an fd is dirty or a /TIMER goes off.
 *
 * Arguments:
 *	timeout	- A value previously returned by TimerTimeout().
 *		  'Timeout' is decremented by the time spent waiting.
 *
 * Return Value:
 *	-1	Interrupted System Call (ie, EINTR caused by ^C)
 *	 0	The timeout has expired (ie, call ExecuteTimers())
 *	 1	An fd is dirty (ie, call do_filedesc())
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
		    panic(1, "Stuck in a polling loop. Help!");
		}
		return 0;		/* Timers are more important */
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
		panic(1, "init_newio() called twice.");

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

int	my_sleep (double seconds)
{
	return ksleep(seconds);
}

int	my_isreadable (int vfd, double seconds)
{
	return kreadable(vfd, seconds);
}

int	my_iswritable (int vfd, double seconds)
{
	return kwritable(vfd, seconds);
}

/****************************************************************************/
/*
 * Register a filedesc for readable events
 * Set up its input buffer
 * Returns an vfd!
 */
int 	new_open (int channel, void (*callback) (int), int io_type, int quiet, int server)
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
	ioe->quiet = quiet;
	ioe->server = server;

	if (io_type == NEWIO_READ)
		ioe->io_callback = unix_read;
	else if (io_type == NEWIO_ACCEPT)
		ioe->io_callback = unix_accept;
	else if (io_type == NEWIO_SSL_READ)
		ioe->io_callback = ssl_read;
	else if (io_type == NEWIO_CONNECT)
		ioe->io_callback = unix_connect;
	else if (io_type == NEWIO_RECV)
		ioe->io_callback = unix_recv;
	else if (io_type == NEWIO_NULL)
		ioe->io_callback = NULL;
#ifdef HAVE_SSL
	else if (io_type == NEWIO_SSL_CONNECT)
		ioe->io_callback = ssl_connect;
#endif
	else
		panic(1, "New_open doesn't recognize io type %d", io_type);

	ioe->callback = callback;
	ioe->failure_callback = NULL;

	if (io_type == NEWIO_CONNECT)
	{
		knowrite(vfd);
		knoread(vfd);
		kwrite(vfd);
		kcleaned(vfd);
	}
	else if (io_type == NEWIO_NULL)
	{
		knowrite(vfd);
		knoread(vfd);
		knowrite(vfd);
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
 * On a VFD registered with new_open(), you may want a callback when the
 * fd has gone bad, so you can do your own cleanup.
 * This is intended for the Python support.
 * 
 * The callback provides two arguments:
 *	1 - channel - the channel (fd) passed to new_open()
 *	2 - error - this is reserved for future expansion
 */
int 	new_open_failure_callback (int channel, void (*failure_callback) (int, int))
{
	MyIO *	ioe;
	int	vfd;

	vfd = VFD(channel);
	if (vfd >= 0 && vfd <= global_max_vfd && (ioe = io_rec[vfd]))
	{
		ioe->failure_callback = failure_callback;
		return 0;
	}

	syserr(-1, "new_open_failure_callback: Called for Channel %d that is not set up", channel);
	return -1;		/* Oh well. */
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
int	new_close_with_option (int vfd, int virtual)
{
	MyIO *	ioe;

	if (vfd == -1)
		return -1;		/* Oh well. */

	if (vfd >= 0 && vfd <= global_max_vfd && (ioe = io_rec[vfd]))
	{
		if (ioe->io_callback == ssl_read)	/* XXX */
			ssl_shutdown(ioe->channel);

		knoread(vfd);
		knowrite(vfd);

		/* 
		 * If virtual == 1, then the caller is managing the 
		 * fd and must close(2) it themselves.
		 * (ie, fd's from external languages)
		 */
		if (virtual == 0)
			unix_close(ioe->channel, ioe->quiet);

		new_free(&ioe->buffer); 
		new_free((char **)&(io_rec[vfd]));

		/*
		 * If we're closing the highest fd in use, then we
		 * want to adjust global_max_vfd downward to the next 
		 * highest fd.
		 */
		if (vfd == global_max_vfd)
		    while (global_max_vfd >= 0 && !io_rec[global_max_vfd])
			global_max_vfd--;
	}
	else if (virtual == 0)
		unix_close(vfd, 0);

	return -1;
}

/* 
 * The lower level IO functions call us when an channel (fd) is found dead,
 * and has been untracked (FD_CLR) and unregistered (new_close()), so we
 * may tell any owner of this.
 */
static	void	fd_is_invalid (int channel)
{
	int	vfd;
	MyIO *	ioe;

	vfd = VFD(channel);
	if (vfd >= 0 && vfd <= global_max_vfd && (ioe = io_rec[vfd]))
	{
		if (ioe->failure_callback)
			ioe->failure_callback(channel, 0);
	}
	else
	{
		syserr(-1, "fd_is_invalid called on unsetup channel %d", channel);
		return;
	}


}


/***********************************************************************/
/******************** Start of unix-specific code here ******************/
/************************************************************************/
static int	unix_close (int channel, int quiet)
{
	if (close(channel))
	{
		if (!quiet)
		   syserr(CSRV(channel), "unix_close: close(%d) failed: %s", 
			channel, strerror(errno));
		return -1;
	}
	return 0;
}

static int	unix_read (int channel, int quiet)
{
	ssize_t	c;
	char	buffer[8192];

	c = read(channel, buffer, sizeof buffer);
	if (c == 0)
	{
		if (!quiet)
		   syserr(CSRV(channel), "unix_read: EOF for fd %d ", channel);
		return 0;
	}
	else if (c < 0)
	{
		if (!quiet)
		   syserr(CSRV(channel), "unix_read: read(%d) failed: %s", 
				channel, strerror(errno));
		return -1;
	}

	if (dgets_buffer(channel, buffer, c))
	{
		if (!quiet)
		   syserr(CSRV(channel), "unix_read: dgets_buffer(%d, %*s) failed",
				channel, (int)c, buffer);
		return -1;
	}

	return c;
}

static int	unix_recv (int channel, int quiet)
{
	ssize_t	c;
	char	buffer[8192];

	c = recv(channel, buffer, sizeof buffer, 0);
	if (c == 0)
	{
		if (!quiet)
		   syserr(CSRV(channel), "unix_recv: EOF for fd %d ", channel);
		return 0;
	}
	else if (c < 0)
	{
		if (!quiet)
		   syserr(CSRV(channel), "unix_recv: read(%d) failed: %s", 
				channel, strerror(errno));
		return -1;
	}

	if (dgets_buffer(channel, buffer, c))
	{
		if (!quiet)
		   syserr(CSRV(channel), "unix_recv: dgets_buffer(%d, %*s) failed",
				channel, (int)c, buffer);
		return -1;
	}

	return c;
}

static int	unix_accept (int channel, int quiet)
{
	int	newfd;
	SS	addr;
	socklen_t len;

#ifdef USE_PTHREAD
	/* XXX I hate this, but pthreads don't play nice with nonblocking. */
	{
		fd_set	fdset;
		FD_ZERO(&fdset);
		FD_SET(channel, &fdset);
		if (select(channel + 1, &fdset, NULL, NULL, NULL) < 0)
		{
		    if (!quiet)
			syserr(CSRV(channel), "unix_accept: select(%d) failed"
				": %s", channel, strerror(errno));
		}
	}
#endif

	len = sizeof(addr);
	if ((newfd = my_accept(channel, (SA *)&addr, &len)) < 0)
	{
	    if (!quiet)
		syserr(CSRV(channel), 
			"unix_accept: my_accept(%d) failed: %s", channel, strerror(errno));
	}

	dgets_buffer(channel, &newfd, sizeof(newfd));
	dgets_buffer(channel, &addr, sizeof(addr));
	return sizeof(newfd) + sizeof(addr);
}

static int	unix_connect (int channel, int quiet)
{
	int	sockerr;
	int	gso_result;
	int	gsn_result;
	SS	localaddr;
	int	gpn_result;
	SS	remoteaddr;
	socklen_t len;

#ifdef USE_PTHREAD
	/* XXX I hate this, but pthreads don't play nice with nonblocking. */
	{
		fd_set	fdset;
		FD_ZERO(&fdset);
		FD_SET(channel, &fdset);
		if (select(channel + 1, NULL, &fdset, NULL, NULL) < 0)
		{
			if (!quiet)
			   syserr(CSRV(channel), 
				"unix_connect: select(%d) failed: %s",
				channel, strerror(errno));
		}
	}
#endif

	/* * */
	len = sizeof(sockerr);
	errno = 0;
	getsockopt(channel, SOL_SOCKET, SO_ERROR, &sockerr, &len);
	gso_result = errno;

	dgets_buffer(channel, &gso_result, sizeof(gso_result));
	dgets_buffer(channel, &sockerr, sizeof(sockerr));

	/* * */
	len = sizeof(localaddr);
	errno = 0;
	getsockname(channel, (SA *)&localaddr, &len);
	gsn_result = errno;

	dgets_buffer(channel, &gsn_result, sizeof(gsn_result));
	dgets_buffer(channel, &localaddr, sizeof(localaddr));

	/* * */
	len = sizeof(remoteaddr);
	errno = 0;
	getpeername(channel, (SA *)&remoteaddr, &len);
	gpn_result = errno;

	dgets_buffer(channel, &gpn_result, sizeof(gpn_result));
	dgets_buffer(channel, &remoteaddr, sizeof(localaddr));

	return (sizeof(int) + sizeof(SS)) * 2;
}


/*
 * Perform a synchronous i/o operation on a file descriptor.  
 * This function is called by kdoit(), which is called by do_wait().
 *
 * The way I/O works in EPIC:
 *	+ main loop calls do_wait()
 *	  + do_wait() calls kdoit() [there are several of these]
 *	  + If kdoit() decides an fd is ready, it looks at all fd's
 *		and for any fd's that are "ready", calls new_io_event().
 *	    + new_io_event() [us] does an I/O operation and queues up some
 *		data using dgets_buffer(), and marks the fd as "dirty"
 *	+ main loop calls do_filedesc() is called, and that calls the 
 *		user's callback, which uses dgets() to return 
 *		whatever was queued up here, and marks the fd as "clean".
 */
static void	new_io_event (int vfd)
{
	MyIO *ioe;
	int	c;

	if (!(ioe = io_rec[vfd]))
		panic(1, "new_io_event: vfd [%d] isn't set up!", vfd);

	/* If it's dirty, something is very wrong. */
	if (!ioe->clean)
		panic(1, "new_io_event: vfd [%d] hasn't been cleaned yet", vfd);

	if (ioe->io_callback)
	{
		/* 
		 * We may expect ioe->io_callback() to either call dgets_buffer
		 * (which sets ioe->clean = 0) or to return an error (in which
		 * case we do it ourselves right here)
		 */
		if ((c = ioe->io_callback(vfd, ioe->quiet)) <= 0)
		{
			ioe->error = -1;
			ioe->clean = 0;
			if (!ioe->quiet)
			   syserr(SRV(vfd), "new_io_event: fd %d must be closed", vfd);

			if (x_debug & DEBUG_INBOUND) 
				yell("VFD [%d] FAILED [%d]", vfd, c);
			return;
		}

		if (x_debug & DEBUG_INBOUND) 
			yell("VFD [%d], did [%d]", vfd, c);
	}
	else
	{
		/* 
		 * XXX It might have been more elegant to create a passthrough
		 * callback that just sets ioe->clean instead of having special
		 * handling here.  Oh well.
		 */
		ioe->clean = 0;
		if (x_debug & DEBUG_INBOUND) 
			yell("VFD [%d], did pass-through", vfd);
	}
}

static	int	is_fd_valid (int fd)
{
	int	retval;

	/* 
	 * This may be too conservative -- 
	 * if F_GETFL fails, maybe it's just bad 
	 */
	retval = fcntl(fd, F_GETFL);
	if (retval == -1 && errno == EBADF)
		return 0;
	else
		return 1;
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

	if (retval < 0 && errno != EINTR)
	{
	    if (errno == EBADF)
	    {
	        int foundit = 0;

		for (channel = 0; channel <= global_max_channel; channel++)
		{
			/* Skip fd's that weren't in the original set */
			if (!FD_ISSET(channel, &readables) &&
			    !FD_ISSET(channel, &writables))
				continue;

			if (!is_fd_valid(channel))
			{
				syserr(-1, "kdoit(select): "
						"Closing channel %d because: %s", 
						channel, strerror(errno));
				FD_CLR(channel, &readables);
				FD_CLR(channel, &writables);
				new_close(channel);

				/* If this is a python fd, we need to callback */
				fd_is_invalid(channel);
				foundit = 1;
			}
		}

		/* XXX -- This is bogus -- we should do something better */
		/* Like, maybe have everything enumerate its fd's, and start over */
		if (!foundit)
			panic(1, "kdoit(select): Select says I have a bad file "
				"descriptor but I can't find it!");
	    }
	    else
		syserr(-1, "kdoit(select): select() failed: %s", 
				strerror(errno));
	}
	else if (retval > 0)
	{
	    for (channel = 0; channel <= global_max_channel; channel++)
	    {
		/* 
		 * We formerly only ever did ONE event at a time, because
		 * new_io_event could have any effect, including closing other
		 * fds!
		 *
		 * This is a performance issue when epic is under load though
		 * and realistically will only ever occur if a script
		 * disconnects a server from within a hook generated by another
		 * server.
		 *
		 * In this case, putting the code in question in a /defer or
		 * /timer is probably a better solution than uncommenting the
		 * break.
		 */
		if (FD_ISSET(channel, &working_rd) ||
		    FD_ISSET(channel, &working_wd))
		{
			new_io_event(VFD(channel));
			/* break; */
		}
	    }
	}

	return retval;
}

static	void	klock (void) { return; }
static	void	kunlock (void) { return; }

static	int	ksleep (double timeout)
{
	Timeval interval;

	interval.tv_sec = (time_t)timeout;
	interval.tv_usec = (timeout - interval.tv_sec) * 1000000;
	return select(0, NULL, NULL, NULL, &interval);
}

static	int	kreadable (int vfd, double timeout)
{
	fd_set	fd_read;
	Timeval	interval;

	FD_ZERO(&fd_read);
	FD_SET(CHANNEL(vfd), &fd_read);
	interval.tv_sec = (time_t)timeout;
	interval.tv_usec = (timeout - interval.tv_sec) * 1000000;
	return select(CHANNEL(vfd) + 1, &fd_read, NULL, NULL, &interval);
}

static	int	kwritable (int vfd, double timeout)
{
	fd_set	fd_read;
	Timeval	interval;

	FD_ZERO(&fd_read);
	FD_SET(CHANNEL(vfd), &fd_read);
	interval.tv_sec = (time_t)timeout;
	interval.tv_usec = (timeout - interval.tv_sec) * 1000000;
	return select(CHANNEL(vfd) + 1, NULL, &fd_read, NULL, &interval);
}

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
        if ((kqueue_fd = kqueue()) < 0)
	{
		syserr(-1, "kinit(kqueue): kqueue() failed: %s", 
				strerror(errno));
		irc_exit(1, "Your system doesn't support kqueue(2)");
	}
}
 
static  void    kread (int vfd)
{
	struct kevent ev;
	EV_SET(&ev, CHANNEL(vfd), EVFILT_READ, EV_ADD|EV_ENABLE, 0, 0, NULL);
	if (kevent(kqueue_fd, &ev, 1, NULL, 0, &kqueue_poll))
	{
		syserr(SRV(vfd), "kread(kqueue): kevent(%d) failed: %s", 
				CHANNEL(vfd), strerror(errno));
	}
}
 
static  void    knoread (int vfd)
{
	struct kevent ev;
	EV_SET(&ev, CHANNEL(vfd), EVFILT_READ, EV_DELETE, 0, 0, NULL);
	if (kevent(kqueue_fd, &ev, 1, NULL, 0, &kqueue_poll))
	{
	    if (errno != ENOENT)
		syserr(SRV(vfd), "knoread(kqueue): kevent(%d) failed: %s", 
				CHANNEL(vfd), strerror(errno));
	}
}
 
static  void    kholdread (int vfd)
{
	struct kevent ev;
	EV_SET(&ev, CHANNEL(vfd), EVFILT_READ, EV_DISABLE, 0, 0, NULL);
	if (kevent(kqueue_fd, &ev, 1, NULL, 0, &kqueue_poll))
	{
	    if (errno != ENOENT)
		syserr(SRV(vfd), "kholdread(kqueue): kevent(%d) failed: %s", 
				CHANNEL(vfd), strerror(errno));
	}
}

static  void    kunholdread (int vfd)
{
	struct kevent ev;
	EV_SET(&ev, CHANNEL(vfd), EVFILT_READ, EV_ENABLE, 0, 0, NULL);
	if (kevent(kqueue_fd, &ev, 1, NULL, 0, &kqueue_poll))
	{
		syserr(SRV(vfd), "kunholdread(kqueue): kevent(%d) failed: %s", 
				CHANNEL(vfd), strerror(errno));
	}
}

static  void    kwrite (int vfd)
{
	struct kevent ev;
	EV_SET(&ev, CHANNEL(vfd), EVFILT_WRITE, EV_ADD|EV_ENABLE, 0, 0, NULL);
	if (kevent(kqueue_fd, &ev, 1, NULL, 0, &kqueue_poll))
	{
		syserr(SRV(vfd), "kwrite(kqueue): kevent(%d) failed: %s", 
				CHANNEL(vfd), strerror(errno));
	}
}

static  void    knowrite (int vfd)
{
	struct kevent ev;
	EV_SET(&ev, CHANNEL(vfd), EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
	if (kevent(kqueue_fd, &ev, 1, NULL, 0, &kqueue_poll))
	{
	    if (errno != ENOENT)
		syserr(SRV(vfd), "knowrite(kqueue): kevent(%d) failed: %s", 
				CHANNEL(vfd), strerror(errno));
	}
}

static	void	kcleaned (int vfd) { return; }

static	int	kdoit (Timeval *timeout)
{
	struct timespec	to;
	struct kevent	event;
	int		channel;
	int		retval;

	to.tv_sec = timeout->tv_sec;
	to.tv_nsec = timeout->tv_usec * 1000;
	retval = kevent(kqueue_fd, NULL, 0, &event, 1, &to);

	if (retval < 0 && errno == ETIMEDOUT)
	{
		retval = 0;
		errno = 0;
	}

	if (retval < 0 && errno != EINTR)
		syserr(-1, "kdoit(kqueue): kevent(NULL) failed: %s", 
					strerror(errno));
	else if (retval > 0)
	{
		channel = event.ident;
		new_io_event(VFD(channel));
	}

	return retval;
}

static	void	klock (void) { return; }
static	void	kunlock (void) { return; }

static	int	ksleep (double timeout)
{
	Timeval interval;

	interval.tv_sec = (time_t)timeout;
	interval.tv_usec = (timeout - interval.tv_sec) * 1000000;
	return select(0, NULL, NULL, NULL, &interval);
}

static	int	kreadable (int vfd, double timeout)
{
	fd_set	fd_read;
	Timeval	interval;

	FD_ZERO(&fd_read);
	FD_SET(CHANNEL(vfd), &fd_read);
	interval.tv_sec = (time_t)timeout;
	interval.tv_usec = (timeout - interval.tv_sec) * 1000000;
	return select(CHANNEL(vfd) + 1, &fd_read, NULL, NULL, &interval);
}

static	int	kwritable (int vfd, double timeout)
{
	fd_set	fd_read;
	Timeval	interval;

	FD_ZERO(&fd_read);
	FD_SET(CHANNEL(vfd), &fd_read);
	interval.tv_sec = (time_t)timeout;
	interval.tv_usec = (timeout - interval.tv_sec) * 1000000;
	return select(CHANNEL(vfd) + 1, NULL, &fd_read, NULL, &interval);
}

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
	int	vfd;
	int	retval;

	ms = timeout->tv_sec * 1000;
	ms += (timeout->tv_usec / 1000);
	retval = poll(polls, global_max_vfd + 1, ms);

	if (retval < 0 && errno != EINTR)
		syserr(-1, "kdoit(poll): poll() failed: %s", strerror(errno));
	else if (retval > 0)
	{
		for (vfd = 0; vfd <= global_max_vfd; vfd++)
		{
		    if (polls[vfd].revents)
		    {
			new_io_event(vfd);
			break;
		    }
		}
	}

	return retval;
}

static	void	klock (void) { return; }
static	void	kunlock (void) { return; }

static	int	ksleep (double timeout)
{
	Timeval interval;

	interval.tv_sec = (time_t)timeout;
	interval.tv_usec = (timeout - interval.tv_sec) * 1000000;
	return select(0, NULL, NULL, NULL, &interval);
}

static	int	kreadable (int vfd, double timeout)
{
	fd_set	fd_read;
	Timeval	interval;

	FD_ZERO(&fd_read);
	FD_SET(CHANNEL(vfd), &fd_read);
	interval.tv_sec = (time_t)timeout;
	interval.tv_usec = (timeout - interval.tv_sec) * 1000000;
	return select(CHANNEL(vfd) + 1, &fd_read, NULL, NULL, &interval);
}

static	int	kwritable (int vfd, double timeout)
{
	fd_set	fd_read;
	Timeval	interval;

	FD_ZERO(&fd_read);
	FD_SET(CHANNEL(vfd), &fd_read);
	interval.tv_sec = (time_t)timeout;
	interval.tv_usec = (timeout - interval.tv_sec) * 1000000;
	return select(CHANNEL(vfd) + 1, NULL, &fd_read, NULL, &interval);
}

#endif


/************************************************************************/
/*
 * Implementation of pthread front-end to synchronous unix system calls
 */
#ifdef USE_PTHREAD
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
	int	err;

	vfd = *(int *)vvfd;
	new_free(&vvfd);

	if ((err = pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)))
		syserr(SRV(vfd), "pthread_io_event: pthread_setcancelstate(%d, PTHREAD_CANCEL_ENABLE) failed: %s", vfd, strerror(err));

	if ((err = pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL)))
		syserr(SRV(vfd), "pthread_io_event: pthread_setcanceltype(%d, PTHREAD_CANCEL_ASYNCHRONOUS) failed: %s", vfd, strerror(err));

	new_io_event(vfd);
	klock();
	if ((err = pthread_cond_signal(&cond)))
		syserr(SRV(vfd), "pthread_io_event: pthread_cond_signal(%d) failed: %s", 
				vfd, strerror(err));
	kunlock();
	pthread_exit(NULL);
	return NULL;
}

static void	kinit (void)
{ 
	int	vfd;
	int	max_fd = IO_ARRAYLEN;
	int	err;

	global = pthread_self();
	pfd = (PS *)new_malloc(sizeof(PS) * max_fd);
	for (vfd = 0; vfd < max_fd; vfd++)
	{
	    pfd[vfd].buffer[0] = 0;
	    memset(&pfd[vfd].pthread, 0, sizeof(pfd[vfd].pthread));
	    pfd[vfd].active = 0;
	}

	if ((err = pthread_mutex_init(&mutex, NULL)))
		syserr(-1, "kinit(pthread): pthread_mutex_init() failed: %s",
				strerror(err));
	klock();
	if ((pthread_cond_init(&cond, NULL)))
		syserr(-1, "kinit(pthread): pthread_cond_init() failed: %s",
				strerror(err));
}
 
static  void    kread (int vfd)
{
	if (pfd[vfd].active)
		panic(1, "vfd [%d] already is active (reading)", vfd);
}
 
static  void    knoread (int vfd)
{
	int	err;

	if (pfd[vfd].active == 0)
		return;

	if ((err = pthread_cancel(pfd[vfd].pthread)))
		syserr(SRV(vfd), "knoread(pthread): pthread_cancel(%d) failed: %s",
				vfd, strerror(err));

	if ((err = pthread_join(pfd[vfd].pthread, NULL)))
		syserr(SRV(vfd), "knoread(pthread): pthread_join(%d) failed: %s",
				vfd, strerror(err));

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
		panic(1, "vfd [%d] already is active (writing)", vfd);
}

static  void    knowrite (int vfd)
{
	int	err;

	if (pfd[vfd].active == 0)
		return;

	if ((err = pthread_cancel(pfd[vfd].pthread)))
		syserr(SRV(vfd), "knowrite: pthread_cancel(%d) failed: %s",
				vfd, strerror(err));
	if ((err = pthread_join(pfd[vfd].pthread, NULL)))
		syserr(SRV(vfd), "knowrite: pthread_join(%d) failed: %s",
				vfd, strerror(err));

	pfd[vfd].active = 0;
}

static	void	kcleaned (int vfd) 
{
	int	err;
	int *	mvfd;

	if (!pthread_equal(pthread_self(), global))
		panic(1, "kcleaned not called from global thread");

	if (pfd[vfd].active == 1)
		panic(1, "vfd [%d] is already active (kcleaned)", vfd);

	if (io_rec[vfd]->clean == 0)
		panic(1, "vfd [%d] is not really clean.", vfd);

	mvfd = new_malloc(sizeof *mvfd);
	*mvfd = vfd;

	if ((err = pthread_create(&pfd[vfd].pthread, NULL, 
					pthread_io_event, mvfd)))
		syserr(SRV(vfd), "kcleaned: pthread_create(%d) failed: %s",
				vfd, strerror(err));

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
	int	err;

	if (!pthread_equal(pthread_self(), global))
		panic(1, "kdoit not called from global thread");

	clock_gettime(CLOCK_REALTIME, &right_now);
	right_now.tv_nsec += (timeout->tv_usec * 1000);
	if (right_now.tv_nsec > 1000000000)
	{
		right_now.tv_nsec -= 1000000000;
		right_now.tv_sec++;
	}
	right_now.tv_sec += timeout->tv_sec;

	retval = pthread_cond_timedwait(&cond, &mutex, &right_now);
	if (retval && retval != ETIMEDOUT)
	{
		syserr(-1, "kdoit(pthread): pthread_cond_timedwait() failed: %s",
				strerror(retval));
		saved_errno = retval;
	}

	/* Reap any child threads that are finished. */
	for (vfd = 0; vfd <= global_max_vfd; vfd++)
	{
	    if (io_rec[vfd] && io_rec[vfd]->clean == 0 && pfd[vfd].active)
	    {
		int c;

		if ((c = pthread_join(pfd[vfd].pthread, NULL)))
			syserr(SRV(vfd), "kdoit(pthread): pthread_join(%d) failed: %s",
					vfd, strerror(c));
		pfd[vfd].active = 0;
		ready++;
	    }
	}

	if (ready)
		return ready;
	else if (retval == ETIMEDOUT)
		return 0;
	else
		return -1;
}

static	void	klock	(void)
{
	int	c;

	if ((c = pthread_mutex_lock(&mutex)))
		panic(1, "pthread_mutex_lock: %s", strerror(c));
}

static	void	kunlock (void)
{
	int	c;

	if ((c = pthread_mutex_unlock(&mutex)))
		panic(1, "pthread_mutex_unlock: %s", strerror(c));
}

static	int	ksleep (double timeout)
{
	Timeval interval;

	interval.tv_sec = (time_t)timeout;
	interval.tv_usec = (timeout - interval.tv_sec) * 1000000;
	return select(0, NULL, NULL, NULL, &interval);
}

static	int	kreadable (int vfd, double timeout)
{
	fd_set	fd_read;
	Timeval	interval;

	FD_ZERO(&fd_read);
	FD_SET(CHANNEL(vfd), &fd_read);
	interval.tv_sec = (time_t)timeout;
	interval.tv_usec = (timeout - interval.tv_sec) * 1000000;
	return select(CHANNEL(vfd) + 1, &fd_read, NULL, NULL, &interval);
}

static	int	kwritable (int vfd, double timeout)
{
	fd_set	fd_read;
	Timeval	interval;

	FD_ZERO(&fd_read);
	FD_SET(CHANNEL(vfd), &fd_read);
	interval.tv_sec = (time_t)timeout;
	interval.tv_usec = (timeout - interval.tv_sec) * 1000000;
	return select(CHANNEL(vfd) + 1, NULL, &fd_read, NULL, &interval);
}

#endif

/************************************************************************/
/*
 * Implementation of solaris ports front-end to synchronous unix calls
 * Graciously provided by larne (ejb), a distinguished server coder.
 * I made a few minor changes, so any fault with this falls to me, not him!
 */
#ifdef USE_SOLARIS_PORTS
#include <port.h>

static int	port_fd = -1;
static int *	events;

static void	kinit (void)
{ 
	int	i;

	if ((port_fd = port_create()) < 0) 
	{
	    syserr(-1, "kinit(ports): port_create() failed: %s", strerror(errno));
	    irc_exit(1, "Your system doesn't support ports");
	}

	events = (int *)new_malloc(sizeof(int) * IO_ARRAYLEN);
	for (i = 0; i < IO_ARRAYLEN; i++)
		events[i] = 0;
}
 
static void	ksetflag (int fd, int flag)
{
	if (events[fd])
	{
	    if (port_dissociate(port_fd, PORT_SOURCE_FD, fd) < 0)
		syserr(SRV(fd), "ksetflag: port_dissociate(%d) failed: %s", 
			fd, strerror(errno));
	}
	events[fd] |= flag;
	if (events[fd])
	{
	    if (port_associate(port_fd, PORT_SOURCE_FD, fd, 
				events[fd], NULL) < 0)
		syserr(SRV(fd), "ksetflag: port_associate(%d) failed: %s",
			fd, strerror(errno));
	}
}

static void	kunsetflag (int fd, int flag)
{
	if (events[fd])
	{
	    if (port_dissociate(port_fd, PORT_SOURCE_FD, fd) < 0)
		syserr(SRV(fd), "kunsetflag: port_dissociate(%d) failed: %s", 
			fd, strerror(errno));
	}
	events[fd] &= ~flag;
	if (events[fd])
	{
	    if (port_associate(port_fd, PORT_SOURCE_FD, fd, 
				events[fd], NULL) < 0)
		syserr(SRV(fd), "kunsetflag: port_associate(%d) failed: %s",
			fd, strerror(errno));
	}
}

static  void    kread (int vfd)	      { ksetflag(CHANNEL(vfd), POLLRDNORM); }
static  void    knoread (int vfd)     { kunsetflag(CHANNEL(vfd), POLLRDNORM); }
static  void    kholdread (int vfd)   { kunsetflag(CHANNEL(vfd), POLLRDNORM); }
static  void    kunholdread (int vfd) { ksetflag(CHANNEL(vfd), POLLRDNORM); }
static  void    kwrite (int vfd)      { ksetflag(CHANNEL(vfd), POLLWRNORM); }
static  void    knowrite (int vfd)    { kunsetflag(CHANNEL(vfd), POLLWRNORM); }
static	void	kcleaned (int vfd)
{
	if (events[CHANNEL(vfd)] & POLLRDNORM)
		kread(vfd);
	if (events[CHANNEL(vfd)] & POLLWRNORM)
		kwrite(vfd);
}

static	int	kdoit (Timeval *timeout)
{
	struct timespec	to;
	port_event_t	pe;
	int		channel, retval;

	to.tv_sec = timeout->tv_sec;
	to.tv_nsec = timeout->tv_usec * 1000;

	errno = 0;
	retval = port_get(port_fd, &pe, &to);

	/*
	 * Solaris might return a negative value than -1 from port_getn()
	 * and this is apparantly not an error, so we must check only for
	 * -1 to indicate an error.
	 */
	if (retval == -1 && errno == ETIME)
	{
		errno = 0;
		return 0;
	}
	else if (retval == -1 && errno != EINTR)
		syserr(-1, "kdoit(ports): port_getn(NULL) failed: %s", 
					strerror(errno));
	else if (retval == 0)
	{
		channel = pe.portev_object;
		new_io_event(VFD(channel));
	}

	return retval;
}

static	void	klock (void) { return; }
static	void	kunlock (void) { return; }

static	int	ksleep (double timeout)
{
	Timeval interval;

	interval.tv_sec = (time_t)timeout;
	interval.tv_usec = (timeout - interval.tv_sec) * 1000000;
	return select(0, NULL, NULL, NULL, &interval);
}

static	int	kreadable (int vfd, double timeout)
{
	fd_set	fd_read;
	Timeval	interval;

	FD_ZERO(&fd_read);
	FD_SET(CHANNEL(vfd), &fd_read);
	interval.tv_sec = (time_t)timeout;
	interval.tv_usec = (timeout - interval.tv_sec) * 1000000;
	return select(CHANNEL(vfd) + 1, &fd_read, NULL, NULL, &interval);
}

static	int	kwritable (int vfd, double timeout)
{
	fd_set	fd_read;
	Timeval	interval;

	FD_ZERO(&fd_read);
	FD_SET(CHANNEL(vfd), &fd_read);
	interval.tv_sec = (time_t)timeout;
	interval.tv_usec = (timeout - interval.tv_sec) * 1000000;
	return select(CHANNEL(vfd) + 1, NULL, &fd_read, NULL, &interval);
}

#endif

