/*
 * wserv.c - little program to be a pipe between a screen or
 * xterm window to the calling ircII process.
 *
 * Written by Troy Rollo, Copyright 1992 Troy Rollo
 * Finished by Matthew Green, Copyright 1993 Matthew Green
 * Original support for 4.4BSD by Jeremy Nelson
 * Almost total rewrite to use INET sockets instead by Jeremy Nelson
 *
 * Copyright 1997 EPIC Software Labs
 * See the COPYRIGHT file or do /help ircii copyright.
 */

#define WSERV_C

#include "defs.h"
#include "config.h"
#include "irc.h"
#include "term.h"
#include "ircaux.h"
#include <errno.h>
#include <sys/uio.h>
#include <sys/stat.h>

static 	int 	s;
static	char	buffer[256];

void 	my_exit(int);
void 	ignore (int value);

int	main (int argc, char **argv)
{
	fd_set		reads;
	int		nread;
	unsigned short 	port;
	char 		*host;
	char		*tmp;
	int		t;
	char		stuff[100];

	my_signal(SIGWINCH, SIG_IGN);
	my_signal(SIGHUP, SIG_IGN);
	my_signal(SIGQUIT, SIG_IGN);
	my_signal(SIGINT, ignore);

	if (argc != 3)    /* no socket is passed */
		my_exit(1);

	host = argv[1];
	port = (unsigned short)atoi(argv[2]);
	if (!port)
		my_exit(2);		/* what the hey */

	s = connect_by_number(host, &port, SERVICE_CLIENT, PROTOCOL_TCP);
	if (s < 0)
		my_exit(23);

	/*
	 * first line from a wserv program is the tty.  ircii doesnt
	 * actually do anything with it, but perhaps its useful just
	 * to confirm we're not out of our minds.
	 *
	 * Note that im using sprintf() now because writev() was proving
	 * to not be reliable enough.  Stupid linux. :p  All this stuff
	 * must be sent with one write() because the client is expecting
	 * it to get a full line, with a newline, in one segment.  To do
	 * otherwise will lead to failure.
	 */
	tmp = ttyname(0);
	sprintf(stuff, "%s\n", tmp);
	t = write(s, stuff, strlen(stuff));
	term_init();
	printf("t is %d", t);

	/*
	 * The select call..  reads from the socket, and from the window..
	 * and pipes the output from out to the other..  nice and simple
	 */
	for (;;)
	{
		FD_ZERO(&reads);
		FD_SET(0, &reads);
		FD_SET(s, &reads);
		if (select(s + 1, &reads, NULL, NULL, NULL) <= 0)
			if (errno == EINTR)
				continue;

		if (FD_ISSET(0, &reads))
		{
			if ((nread = read(0, buffer, sizeof(buffer))))
				write(s, buffer, nread);
			else
				my_exit(3);
		}
		if (FD_ISSET(s, &reads))
		{
			if ((nread = read(s, buffer, sizeof(buffer))))
				write(1, buffer, nread);
			else
				my_exit(4);
		}
	}

	my_exit(8);
}

void 	ignore (int value)
{
	/* send a ^C */
	char foo = 3;
	write(s, &foo, 1);
}

void 	my_exit(int value)
{
	printf("exiting with %d!\n", value);
	printf("errno is %d (%s)\n", errno, strerror(errno));
	exit(value);
}

/* These are needed to avoid having to link with compat.o! */
#ifndef HAVE_STRLCPY
size_t	strlcpy (char *dst, const char *src, size_t siz)
{
        char *d = dst;
        const char *s = src;
        size_t n = siz;

	if (n > 0)
		n--;		/* Save space for the NUL */

        /* Copy as many bytes as will fit */
	while (n > 0)
	{
		if (!(*d = *s))
			break;
		d++;
		s++;
		n--;
        }

        /* Not enough room in dst, add NUL and traverse rest of src */
        if (n == 0 && siz)
                *d = 0;              /* NUL-terminate dst */

	return strlen(src);
}
#endif

#ifndef HAVE_INET_ATON
/*
 * Copyright (c) 1983, 1990, 1993
 *    The Regents of the University of California.  All rights reserved.
 * Portions Copyright (c) 1993 by Digital Equipment Corporation.
 * Licensed under the 3-clause BSD license, see compat.c for text.
 */

/* 
 * Check whether "cp" is a valid ascii representation
 * of an Internet address and convert to a binary address.
 * Returns 1 if the address is valid, 0 if not.
 * This replaces inet_addr, the return value from which
 * cannot distinguish between failure and a local broadcast address.
 */
int inet_aton(const char *cp, struct in_addr *addr)
{
	unsigned long	val;
	int		base, n;
	char		c;
	unsigned	parts[4];
	unsigned	*pp = parts;

	c = *cp;
	for (;;) {
		/*
		 * Collect number up to ``.''.
		 * Values are specified as for C:
		 * 0x=hex, 0=octal, isdigit=decimal.
		 */
		if (!isdigit(c))
			return (0);
		val = 0; base = 10;
		if (c == '0') {
			c = *++cp;
			if (c == 'x' || c == 'X')
				base = 16, c = *++cp;
			else
				base = 8;
		}
		for (;;) {
			if (isascii(c) && isdigit(c)) {
				val = (val * base) + (c - '0');
				c = *++cp;
			} else if (base == 16 && isascii(c) && isxdigit(c)) {
				val = (val << 4) |
					(c + 10 - (islower(c) ? 'a' : 'A'));
				c = *++cp;
			} else
				break;
		}
		if (c == '.') {
			/*
			 * Internet format:
			 *	a.b.c.d
			 *	a.b.c	(with c treated as 16 bits)
			 *	a.b	(with b treated as 24 bits)
			 */
			if (pp >= parts + 3)
				return (0);
			*pp++ = val;
			c = *++cp;
		} else
			break;
	}
	/*
	 * Check for trailing characters.
	 */
	if (c != '\0' && (!isascii(c) || !isspace(c)))
		return (0);
	/*
	 * Concoct the address according to
	 * the number of parts specified.
	 */
	n = pp - parts + 1;
	switch (n) {

	case 0:
		return (0);		/* initial nondigit */

	case 1:				/* a -- 32 bits */
		break;

	case 2:				/* a.b -- 8.24 bits */
		if (val > 0xffffff)
			return (0);
		val |= parts[0] << 24;
		break;

	case 3:				/* a.b.c -- 8.8.16 bits */
		if (val > 0xffff)
			return (0);
		val |= (parts[0] << 24) | (parts[1] << 16);
		break;

	case 4:				/* a.b.c.d -- 8.8.8.8 bits */
		if (val > 0xff)
			return (0);
		val |= (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8);
		break;
	}
	if (addr)
		addr->s_addr = htonl(val);
	return (1);
}
#endif


/* These are here so we can link with network.o */
char *		LocalHostName = NULL;
struct in_addr 	LocalHostAddr;
char 		empty_string[] = "";
enum VAR_TYPES { unused };
int 		get_int_var (enum VAR_TYPES unused) { return 5; }
void 		set_socket_options (int des) { }
u_long		random_number (u_long unused) { return random(); }
/* swift and easy -- returns the size of the file */
off_t 	file_size (const char *filename)
{
	struct stat statbuf;

	if (!stat(filename, &statbuf))
		return (off_t)(statbuf.st_size);
	else
		return -1;
}

int	file_exists (const char *filename)
{
	if (file_size(filename) == -1)
		return 0;
	else
		return 1;
}
/* End of file */
