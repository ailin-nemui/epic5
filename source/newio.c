/*
 * newio.c: This is some handy stuff to deal with file descriptors in a way
 * much like stdio's FILE pointers 
 *
 * IMPORTANT NOTE:  If you use the routines here-in, you shouldn't switch to
 * using normal reads() on the descriptors cause that will cause bad things
 * to happen.  If using any of these routines, use them all 
 *
 * Copyright 1990 Michael Sandrof
 * Copyright 1995 Matthew Green
 * Copyright 1997 EPIC Software Labs
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
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
int 	dgets (char *str, int des, int buffer)
{
	int	cnt = 0, c;
	MyIO	*ioe;
	int	nbytes;

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

#ifdef HAVE_USEFUL_FIONREAD
		/*
		 * Check to see if any bytes are ready.  If this fails,
		 * then its almost always due to the filedesc being 
		 * bogus.  Thats a fatal error.
		 */
		if (ioctl(des, FIONREAD, &nbytes) == -1)
		{
			*str = 0;
			dgets_errno = errno;
			return -1;
		} 

		/*
		 * Check for a quasi-EOF condition.  If we get to this
		 * point, then new_select() indicated that this fd is ready.
		 * The fd is ready if either:
		 *	1) A newline is in the buffer
		 *	2) select(2) returned ready for the fd.
		 *
		 * If 1) is true, then write_pos will not be zero.  So we can
		 * use that as a cheap way to check for #1.  If #1 is false,
		 * then #2 must have been true, and if nbytes is 0, then 
		 * that indicates an EOF condition.
		 */
		else if (!nbytes && ioe->write_pos == 0)
		{
			*str = 0;
			dgets_errno = -1;	/* End of file */
			return -1;
		}

		/*
		 * nbytes shouldnt ever be zero at this point.  Id be
		 * interested in knowing if it ever was.
		 */
		else if (nbytes)
#else
		nbytes == -1;
#endif
		{
			/* Better safe than sorry... */
			c = read(des, ioe->buffer + ioe->write_pos,
				      ioe->buffer_size - ioe->write_pos - 1);
			if (x_debug & DEBUG_INBOUND) 
			    yell("FD [%d], should [%d] did [%d]", 
					des, nbytes, c);

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

#ifdef HAVE_USEFUL_FIONREAD
		else
		{
			/*
			 * At this point nbytes is 0, and it doesnt
			 * appear the socket is at EOF or ready to read.
			 * Very little to do at this point but force the
			 * issue and figure out what the heck went wrong.
			 */
			struct timeval t = { 0, 0 };
			fd_set testing;

			FD_ZERO(&testing);
			FD_SET(des, &testing);
			switch (select(des + 1, &testing, NULL, NULL, &t))
			{
	case -1:	/* Serious error condition */
	{
		yell("Fatal error on des [%d]", des);
		*str = 0;
		dgets_errno = errno;
		return -1;
	}
	case 0:		/* Not ready to read */
	{
		if (ioe->write_pos == 0)
			panic("des [%d] isnt ready, nothing buffered", des);
		else		/* Flush the buffer to see if that helps */
		{
			ioe->buffer[ioe->write_pos++] = '\n';
			break;
		}
	}
	case 1:		/* Supposedly ready to read */
	{
		c = read(des, ioe->buffer + ioe->write_pos,
			      ioe->buffer_size - ioe->write_pos);
		if (c == -1)				/* Error. */
		{
			*str = 0;
			dgets_errno = errno;
			return -1;
		}
		else if (c == 0)			/* EOF */
		{
			/* I hope this is safe... */
			strcpy(str, ioe->buffer + ioe->read_pos);
			ioe->read_pos = ioe->write_pos = 0;
			ioe->buffer[0] = 0;
			dgets_errno = -1;		/* EOF */
			return 0;
		}
		else					/* Success */
		{
			ioe->buffer[ioe->write_pos + c] = 0;
			ioe->write_pos += c;
			ioe->segments++;
			break;
		}

	}
			}
		}
#endif
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
int 	new_select (fd_set *rd, fd_set *wd, struct timeval *timeout)
{
static	int		polls = 0;
	struct timeval	thetimeout;
	struct timeval *newtimeout = &thetimeout;
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
		 * being ready.  No sweat.
		 */
		if (io_rec[i])
		{
		    if (io_rec[i]->read_pos < io_rec[i]->write_pos &&
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
