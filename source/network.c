/*
 * network.c -- handles stuff dealing with connecting and name resolving
 *
 * Written by Jeremy Nelson
 * Copyright 1995, 2002 Jeremy Nelson
 * See the COPYRIGHT file or do /help ircii copyright
 */

#include "irc.h"
#include "ircaux.h"
#include "vars.h"
#include "newio.h"
#include "output.h"
#include <sys/ioctl.h>

#ifdef HAVE_SYS_UN_H
#include <sys/un.h>
typedef struct sockaddr_un USA;
#endif

static int	get_low_portnum (void);
static int	get_high_portnum (void);
static int	set_non_blocking (int fd);
static int	set_blocking (int fd);
static int	inet_remotesockaddr (int family, const char *host, const char *port, SS *storage, socklen_t *len);
static int	inet_vhostsockaddr (int family, SS *storage, socklen_t *len);
static int	Connect (int fd, SA *addr);
static int	Getaddrinfo (const char *nodename, const char *servname, const AI *hints, AI **res);
static void	Freeaddrinfo (AI *ai);
static socklen_t	socklen (SA *sockaddr);

/*
   Retval    Meaning
   --------  -------------------------------------------
   -1        System call error occured (check errno)
   -2        The operation isn't supported for the requested family.
   -3        The hostname has an address, but not in the family you want.
   -4        The "address presentation" hostname can't be converted.
   -5        The hostname does not resolve.
   -6        The requested family does not have a virtual host.
   -7        The remote sockaddr to connect to was not provided.
   -8        The local sockaddr and remote sockaddr are different families.
   -9        The timeout expired before the connection was established.
   -10       The requested local address is in use or not available.
   -11       The connect()ion failed (at connect() time)
   -12       The connection could not be established (after connect() time)
   -13       The local sockaddr to bind was not provided.
   -14       The family request does not make sense.
*/

/*****************************************************************************/
/*
 * NAME: connectory
 * USAGE: Connect to a given "host" and "port" with the given "family"
 * ARGS: family - AF_INET is the only supported argument.
 *       host - A hostname or "dotted quad" (or equivalent).
 *       port - The remote port to connect to in *HOST ORDER*.
 *
 * XXX - This so violates everything I really wanted this function to be,
 * but I changed it to call getaddrinfo() directly instead of calling
 * inet_vhostsockaddr() because I wanted it to be able to take advantage
 * of connecting to multiple protocols and multiple ip addresses (for things
 * like ``us.undernet.org'') without having to do multiple calls to
 * getaddrinfo() which could be quite costly.
 */
int	connectory (int family, const char *host, const char *port)
{
	AI	hints, *results, *ai;
	int	error;
	int	fd;
	SS	  localaddr;
	socklen_t locallen;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = family;
	hints.ai_socktype = SOCK_STREAM;
	if ((error = Getaddrinfo(host, port, &hints, &results)))
		return -5;

	fd = -1;
	for (ai = results; ai; ai = ai->ai_next) 
	{
	    /* First, look up the virtual host we use for this protocol. */
	    error = inet_vhostsockaddr(ai->ai_family, &localaddr, &locallen);
	    if (error < 0)
		continue;

	    /* Now try to do the connection. */
	    fd = client_connect((SA *)&localaddr, locallen, ai->ai_addr, ai->ai_addrlen);
	    if (fd < 0)
	    {
		error = fd;
		fd = -1;
		continue;
	    }
	}

	Freeaddrinfo(results);
	if (fd < 0)
		return error;
	return fd;
}

/*
 * NAME: client_connect
 * USAGE: Create a new socket and establish both endpoints with the 
 *        arguments given.
 * ARGS: l - A local sockaddr structure representing the local side of
 *           the connection.  NULL is permitted.  If this value is NULL,
 *           then the local side of the connection will not be bind()ed.
 *       ll - The sizeof(l) -- if 0, then 'l' is treated as a NULL value.
 *       r - A remote sockaddr structure representing the remote side of
 *           the connection.  NULL is not permitted.
 *       rl - The sizeof(r) -- if 0, then 'r' is treated as a NULL value.
 *            Therefore, 0 is not permitted.
 */
int	client_connect (SA *l, socklen_t ll, SA *r, socklen_t rl)
{
	int	fd = -1;
	int	family = AF_UNSPEC;
	int	err;
	fd_set	set;
	Timeval	to;

	if (ll == 0)
		l = NULL;
	if (rl == 0)
		r = NULL;

	if (!r)
		return -7;

	if (l && r && l->sa_family != r->sa_family)
		return -8;

	if (l)
		family = l->sa_family;
	if (r)
		family = r->sa_family;

	if ((fd = socket(family, SOCK_STREAM, 0)) < 0)
		return -1;

	set_socket_options(fd);

	/* Unix domain server */
	if (family == AF_UNIX)
	{
		alarm(get_int_var(CONNECT_TIMEOUT_VAR));
		if (Connect(fd, r) < 0)
		{
			alarm(0);
			return close(fd), -1;
		}
		alarm(0);
	}
	else if (family == AF_INET || family == AF_INET6)
	{
		if (l && bind(fd, l, ll))
		{
		    if (errno == EADDRINUSE || errno == EADDRNOTAVAIL)
			return close(fd), -10;
		    else
			return close(fd), -1;
		}

		/*
		 * Simulate a blocking connect, using a nonblocking connect.
		 * Due to logic flow issues, we cannot just start a non-
		 * blocking connect and return.  Bad things happen.  But 
		 * what we can do is do a nonblocking connect and then wait
		 * here until it is finished.  This lets us graciously 
		 * trap ^C's from the user without resorting to alarm()s.
		 */
		set_non_blocking(fd);

		/* Star the connect.  If any bad error occurs, punt. */
		errno = 0;
		err = Connect(fd, r);
		if (err < 0 && errno != EAGAIN && errno != EINPROGRESS)
			return close(fd), -11;

		/* Now stall for a while until it succeeds or times out. */
		errno = 0;
		FD_ZERO(&set);
		FD_SET(fd, &set);
		to.tv_sec = get_int_var(CONNECT_TIMEOUT_VAR);
		to.tv_usec = 0;
		switch (select(fd + 1, NULL, &set, NULL, &to))
		{
			case 0:
				errno = ECONNABORTED;
				/* FALLTHROUGH */
			case -1:
				return close(fd), -9;
			default:
			{
				SS	peer;
				int	peerlen;

				peerlen = sizeof(peer);
				if (getpeername(fd, (SA *)&peer, &peerlen))
					return close(fd), -12;
				set_blocking(fd);
			}
		}
	}
	else
		return -2;

	return fd;
}


/*****************************************************************************/
/*
 * NAME: ip_bindery
 * USAGE: Establish a local passive (listening) socket at the given port.
 * ARGS: family - AF_INET is the only supported argument.
 *       port - The port to establish the connection upon.  May be 0, 
 *		which requests any open port.  /SET RANDOM_LOCAL_PORTS
 *		is honored only if port is 0.
 *	 storage - Pointer to a sockaddr structure big enough for the
 *		   specified family.  Upon success, it will be filled with
 *		   the local sockaddr of the new connection.
 * NOTES: In most cases, the local address for 'storage' will be INADDR_ANY
 *        which doesn't really give you any useful information.  It is not
 *        possible to authoritatively figure out the local IP address
 *        without using ioctl().  However, we guess the best we may.
 *
 * NOTES: This function lacks IPv6 support.
 *        This function lacks Unix Domain Socket support.
 */
int	ip_bindery (int family, unsigned short port, SS *storage)
{
	if (family == AF_INET)
	{
		ISA *	name = (ISA *)storage;
		int	i;
		int	fd;
		socklen_t len;
		int	err;

		if ((err = inet_vhostsockaddr(family, storage, &len)) < 0)
			return err;

		if (len == 0) 
		{
			name->sin_family = AF_INET;
			name->sin_addr.s_addr = htonl(INADDR_ANY);
		}

		/*
		 * Make five stabs at trying to set up a passive socket.
		 */
		for (i = 0; i < 5; i++)
		{
			/*
			 * Fill in the port number...
			 * If the user wants a specific port, use it.
			 * If they don't care, then if they want us to
			 * give them a random port, grab a port at 
			 * random in the port range being used (system
			 * dependant, natch).  If the user doesn't care
			 * and doesn't want a random port, just ask the
			 * OS to assign us a port.  Some systems (OpenBSD)
			 * hand out ports at random, which is a good thing.
			 */
			if (port == 0)
			{
			    if (get_int_var(RANDOM_LOCAL_PORTS_VAR))
			    {
				int	lowport, highport;

				lowport = get_low_portnum();
				highport = get_high_portnum();
				name->sin_port = htons(random_number(0) % 
						(highport - lowport) + lowport);
			    }
			    else
				name->sin_port = htons(0);
			}
			else
			    name->sin_port = htons(port);

			/*
			 * Try to establish the local side of our passive
			 * socket with our sockaddr.  If it fails because
			 * the port is in use or not available, try again.
			 * If it fails five tims, bummer for the user.
			 */
			fd = client_bind((SA *)name, sizeof(*name));
			if (fd < 0)
			{
			    if (fd == -10 && port == 0 && i < 5)
				continue;
			    return fd;
			}
			return fd;
		}
		return -10;
	}

	return -2;
}

/*
 * NAME: client_bind
 * USAGE: Create a new socket and establish one endpoint with the arguments
 *        given.
 * ARGS: local - A local sockaddr structure representing the local side of
 *               the connection.  NULL is not permitted.
 *       local_len - The sizeof(l) -- if 0, then 'l' is treated as a NULL 
 *		     value.  Therefore, 0 is not permitted.
 */
int	client_bind (SA *local, socklen_t local_len)
{
	int	fd = -1;
	int	family = AF_UNSPEC;

	if (local_len == 0)
		local = NULL;
	if (!local)
		return -13;

	family = local->sa_family;

	if ((fd = socket(family, SOCK_STREAM, 0)) < 0)
		return -1;

	set_socket_options (fd);
#ifdef IP_PORTRANGE
	/*
	 * On 4.4BSD systems, this socket option asks the
	 * operating system to select a port from the "high
	 * port range" when we ask for port 0 (any port).
	 * Maybe some day Linux will support this.
	 */
	if (family == AF_INET && getenv("EPIC_USE_HIGHPORTS"))
	{
		int ports = IP_PORTRANGE_HIGH;
		setsockopt(fd, IPPROTO_IP, IP_PORTRANGE, 
				(char *)&ports, sizeof(ports));
	}
#endif

	if (bind(fd, local, local_len))
	{
	    if (errno == EADDRINUSE || errno == EADDRNOTAVAIL)
		return close(fd), -10;
	    else
		return close(fd), -1;
	}

	if (family != AF_UNIX)
	{
		/*
		 * Get the local sockaddr of the passive socket,
		 * specifically the port number, and stash it in
		 * 'port' which is the return value to the caller.
		 */	
		if (getsockname(fd, (SA *)local, &local_len))
			return close(fd), -1;
	}


	if (listen(fd, 4) < 0)
		return close(fd), -1;

	return fd;
}

/*****************************************************************************/
/*
 * NAME: inet_vhostsockaddr
 * USAGE: Get the sockaddr of the current virtual host, if one is in use.
 * ARGS: family - The family whose sockaddr info is to be retrieved
 *       storage - Pointer to a sockaddr structure appropriate for family.
 *       len - This will be set to the size of the sockaddr structure that
 *             was copied, or set to 0 if no virtual host is available in
 *             the given family.
 * NOTES: If "len" is set to 0, do not attempt to bind() 'storage'!
 */
static int	inet_vhostsockaddr (int family, SS *storage, socklen_t *len)
{
	if (family == AF_INET)
	{
		if (LocalIPv4Addr)
		{
			*(ISA *)storage = *LocalIPv4Addr;
			((ISA *)storage)->sin_port = htonl(0);
			*len = sizeof(ISA);
			return 0;
		}
		*len = 0;
		return 0;
	}
	else if (family == AF_INET6)
	{
		if (LocalIPv6Addr)
		{
			*(ISA6 *)storage = *LocalIPv6Addr;
			((ISA6 *)storage)->sin6_port = htonl(0);
			*len = sizeof(ISA6);
			return 0;
		}
		*len = 0;
		return 0;
	}
	else if (family == AF_UNIX)
	{
		*len = 0;
		return 0;		/* No vhost needed */
	}

	return -2;
}

/*
 * NAME: inet_remotesockaddr
 * USAGE: Get a sockaddr of the specified host and port.
 * ARGS: family - The family whose sockaddr info is to be retrieved
 *       host - The host whose address shall be put in the sockaddr
 *       port - The port to put into the sockaddr -- MUST BE IN HOST ORDER!
 *       storage - Pointer to a sockaddr structure appropriate for family.
 */
static int	inet_remotesockaddr (int family, const char *host, const char *port, SS *storage, socklen_t *len)
{
	int	err;

	((SA *)storage)->sa_family = family;

	if ((err = inet_strton(host, port, (SA *)storage, 0)))
		return err;

	if ((*len = socklen((SA *)storage)) == 0)
		return -2;

	return 0;
}


/************************************************************************/
/*
 * NAME: inet_strton
 * USAGE: Convert "any" kind of address represented by a string into
 *	  a socket address suitable for connect()ing or bind()ing with.
 * ARGS: hostname - The address to convert.  It may be any of the following:
 *		IPv4 "Presentation Address"	(A.B.C.D)
 *		IPv6 "Presentation Address"	(A:B::C:D)
 *		Hostname			(foo.bar.com)
 *		32 bit host order ipv4 address	(2134546324)
 *	 storage - A pointer to a (struct sockaddr_storage) with the 
 *		"family" argument filled in (AF_INET or AF_INET6).  
 *		If "hostname" is a p-addr, then the form of the p-addr
 *		must agree with the family in 'storage'.
 */
int	inet_strton (const char *host, const char *port, SA *storage, int flags)
{
	int family = storage->sa_family;

        /* First check for legacy 32 bit integer DCC addresses */
	if ((family == AF_INET || family == AF_UNSPEC) && host && is_number(host))
	{
		((ISA *)storage)->sin_family = AF_INET;
#ifdef HAVE_SA_LEN
		((ISA *)storage)->sin_len = sizeof(ISA);
#endif
		((ISA *)storage)->sin_addr.s_addr = htonl(strtoul(host, NULL, 10));
		if (port)
			((ISA *)storage)->sin_port = htons((u_short)strtoul(port, NULL, 10));
		return 0;
	}
	else
	{
		AI hints;
		AI *results;
		int retval;

		memset(&hints, 0, sizeof(hints));
		hints.ai_flags = flags;
		hints.ai_family = family;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = 0;

		if ((retval = Getaddrinfo(host, port, &hints, &results))) {
		    yell("getaddrinfo(%s): %s", host, gai_strerror(retval));
		    return -5;
		}

		/* memcpy can bite me. */
		memcpy(storage, results->ai_addr, results->ai_addrlen);

		Freeaddrinfo(results);
		return 0;
	}

	return -2;
}

/*
 * NAME: inet_ntostr
 * USAGE: Convert a "sockaddr name" (SA) into a Hostname/p-addr
 * PLAIN ENGLISH: Convert getpeername() into "foo.bar.com"
 * ARGS: name - The socket address, possibly returned by getpeername().
 *       retval - A string to store the hostname/paddr (RETURN VALUE)
 *       size - The length of 'retval' in bytes
 * RETURN VALUE: "retval" is returned upon success
 *		 "empty_string" is returned for any error.
 *
 * NOTES: 'flags' should be set to NI_NAMEREQD if you don't want the remote
 *        host's p-addr if it does not have a DNS hostname.
 */
char *	inet_ntostr (SA *name, char *host, int hsize, char *port, int psize, int flags)
{
	int	retval;
	socklen_t len;

	len = socklen(name);
	if ((retval = getnameinfo(name, len, host, hsize, port, psize, flags))) {
		yell("getnameinfo(%s): %s", host, gai_strerror(retval));
		return NULL;
	}

	return host;
}

/* * * * * * * * * */
/*
 * NAME: inet_hntop
 * USAGE: Convert a Hostname into a "presentation address" (p-addr)
 * PLAIN ENGLISH: Convert "A.B.C.D" into "foo.bar.com"
 * ARGS: family - The family whose presesentation address format to use
 *	 host - The hostname to convert
 *       retval - A string to store the p-addr (RETURN VALUE)
 *       size - The length of 'retval' in bytes
 * RETURN VALUE: "retval" is returned upon success
 *		 "empty_string" is returned for any error.
 */
char *	inet_hntop (int family, const char *host, char *retval, int size)
{
	int	err;
	SS	buffer;

	((SA *)&buffer)->sa_family = family;
	if ((err = inet_strton(host, NULL, (SA *)&buffer, 0)))
		return empty_string;

	if (!inet_ntostr((SA *)&buffer, retval, size, NULL, 0, NI_NUMERICHOST))
		return empty_string;

	return retval;
}

/*
 * NAME: inet_ptohn
 * USAGE: Convert a "presentation address" (p-addr) into a Hostname
 * PLAIN ENGLISH: Convert "foo.bar.com" into "A.B.C.D"
 * ARGS: family - The family whose presesentation address format to use
 *	 ip - The presentation-address to look up
 *       retval - A string to store the hostname (RETURN VALUE)
 *       size - The length of 'retval' in bytes
 * RETURN VALUE: "retval" is returned upon success
 *		 "empty_string" is returned for any error.
 */
char *	inet_ptohn (int family, const char *ip, char *retval, int size)
{
	int	err;
	SS	buffer;

	((SA *)&buffer)->sa_family = family;
	if ((err = inet_strton(ip, NULL, (SA *)&buffer, AI_NUMERICHOST)))
		return empty_string;

	if (!inet_ntostr((SA *)&buffer, retval, size, NULL, 0, NI_NAMEREQD))
		return empty_string;

	return retval;
}

/*
 * NAME: one_to_another
 * USAGE: Convert a p-addr to a Hostname, or a Hostname to a p-addr.
 * PLAIN ENGLISH: Convert "A.B.C.D" to "foo.bar.com" or convert "foo.bar.com"
 *                into "A.B.C.D"
 * ARGS: family - The address family in which to convert (AF_INET/AF_INET6)
 *	 what - Either a Hostname or a p-addr.
 *       retval - If "what" is a Hostname, a place to store the p-addr
 *                If "what" is a p-addr, a place to store the Hostname
 *       size - The length of 'retval' in bytes
 * RETURN VALUE: "retval" is returned upon success
 *		 "empty_string" is returned for any error.
 *
 * NOTES: If "what" is a p-addr and there is no hostname associated with that 
 *        address, that is considered an error and empty_string is returned.
 */
char *	one_to_another (int family, const char *what, char *retval, int size)
{
	if (inet_ptohn(family, what, retval, size) == empty_string)
		inet_hntop(family, what, retval, size);
	return retval;
}

/****************************************************************************/
static int	set_non_blocking (int fd)
{
	int	flag, rval;

#if defined(O_NONBLOCK)
	flag = O_NONBLOCK;
#elif defined(O_NDELAY)
	flag = O_NDELAY;
#elif defined(FIONBIO)
	flag = 1;
#else
	yell("Sorry!  Can't set nonblocking on this system!");
#endif

#if defined(O_NONBLOCK) || defined(O_NDELAY)
	if ((rval = fcntl(fd, F_GETFL, 0)) == -1)
		return -1;
	if (fcntl(fd, F_SETFL, rval | flag) == -1)
		return -1;
#else
	if (ioctl(fd, FIONBIO, &flag) < 0)
		return -1;
#endif
	return 0;
}

static int	set_blocking (int fd)
{
	int	flag, rval;

#if defined(O_NONBLOCK)
	flag = O_NONBLOCK;
#elif defined(O_NDELAY)
	flag = O_NDELAY;
#elif defined(FIONBIO)
	flag = 0;
#else
	yell("Sorry!  Can't set nonblocking on this system!");
#endif

#if defined(O_NONBLOCK) || defined(O_NDELAY)
	if ((rval = fcntl(fd, F_GETFL, 0)) == -1)
		return -1;
	if (fcntl(fd, F_SETFL, rval & ~flag) == -1)
		return -1;
#else
	if (ioctl(fd, FIONBIO, &flag) < 0)
		return -1;
#endif
	return 0;
}

/****************************************************************************/
#define FALLBACK_LOWPORT 1024
#define FALLBACK_HIGHPORT 65535

static int	get_low_portnum (void)
{
	if (file_exists("/proc/sys/net/ipv4/ip_local_port_range"))
	{
		char	buffer[80];
		int	fd;
		int	first, second;

		fd = open("/proc/sys/net/ipv4/ip_local_port_range", O_RDONLY);
		if (fd < 0)
			return FALLBACK_LOWPORT;
		read(fd, buffer, 80);
		close(fd);
		sscanf(buffer, "%d %d", &first, &second);
		return first;
	}

#ifdef IP_PORTRANGE
	if (getenv("EPIC_USE_HIGHPORTS"))
	{
#ifdef HAVE_SYSCTLBYNAME
		char	buffer[1024];
		size_t	bufferlen = 1024;

		if (sysctlbyname("net.inet.ip.portrange.hifirst", buffer, 
				&bufferlen, NULL, 0))
			return *(int *)buffer;
		else if (sysctlbyname("net.inet.ip.porthifirst", buffer, 
				&bufferlen, NULL, 0))
			return *(int *)buffer;
		else
#endif
			return FALLBACK_LOWPORT;
	}
	else
	{
#ifdef HAVE_SYSCTLBYNAME
		char	buffer[1024];
		size_t	bufferlen = 1024;

		if (sysctlbyname("net.inet.ip.portrange.first", buffer, 
					&bufferlen, NULL, 0))
			return *(int *)buffer;
		else if (sysctlbyname("net.inet.ip.portfirst", buffer, 
					&bufferlen, NULL, 0))
			return *(int *)buffer;
		else
#endif
			return FALLBACK_LOWPORT;
	}
#else
	return FALLBACK_LOWPORT;
#endif
}

static int	get_high_portnum (void)
{
	if (file_exists("/proc/sys/net/ipv4/ip_local_port_range"))
	{
		char	buffer[80];
		int	fd;
		int	first, second;

		fd = open("/proc/sys/net/ipv4/ip_local_port_range", O_RDONLY);
		if (fd < 0)
			return FALLBACK_HIGHPORT;
		read(fd, buffer, 80);
		close(fd);
		sscanf(buffer, "%d %d", &first, &second);
		return second;
	}

#ifdef IP_PORTRANGE
	if (getenv("EPIC_USE_HIGHPORTS"))
	{
#ifdef HAVE_SYSCTLBYNAME
		char	buffer[1024];
		size_t	bufferlen = 1024;

		if (sysctlbyname("net.inet.ip.portrange.hilast", buffer, 
				&bufferlen, NULL, 0))
			return *(int *)buffer;
		else if (sysctlbyname("net.inet.ip.porthilast", buffer, 
				&bufferlen, NULL, 0))
			return *(int *)buffer;
		else
#endif
			return FALLBACK_HIGHPORT;
	}
	else
	{
#ifdef HAVE_SYSCTLBYNAME
		char	buffer[1024];
		size_t	bufferlen = 1024;

		if (sysctlbyname("net.inet.ip.portrange.last", buffer, 
					&bufferlen, NULL, 0))
			return *(int *)buffer;
		else if (sysctlbyname("net.inet.ip.portlast", buffer, 
					&bufferlen, NULL, 0))
			return *(int *)buffer;
		else
#endif
			return FALLBACK_HIGHPORT;
	}
#else
	return FALLBACK_HIGHPORT;
#endif
}

/****************************************************************************/
/*
 * It is possible for a race condition to exist; such that select()
 * indicates that a listen()ing socket is able to recieve a new connection
 * and that a later accept() call will still block because the connection
 * has been closed in the interim.  This wrapper for accept() attempts to
 * defeat this by making the accept() call nonblocking.
 */
int	Accept (int s, SA *addr, int *addrlen)
{
	int	retval;

	set_non_blocking(s);
	retval = accept(s, addr, addrlen);
	set_blocking(s);
	return retval;
}

static int Connect (int fd, SA *addr)
{
	return connect(fd, addr, socklen(addr));
}

static int	Getaddrinfo (const char *nodename, const char *servname, const AI *hints, AI **res)
{
#ifdef GETADDRINFO_DOES_NOT_DO_AF_UNIX
	int	do_af_unix = 0;
	USA 	storage;
	AI *	results;
	int	len;

	if (strchr(nodename, '/'))
		do_af_unix = 1;
	if (hints && hints->ai_family == AF_UNIX)
		do_af_unix = 1;

	if (do_af_unix)
	{
                memset(&storage, 0, sizeof(storage));
                storage.sun_family = AF_UNIX;
                strlcpy(storage.sun_path, nodename, sizeof(storage.sun_path));
#ifdef HAVE_SUN_LEN
# ifdef SUN_LEN
                storage.sun_len = SUN_LEN(&storage);
# else
                storage.sun_len = strlen(nodename) + 1;
# endif
#endif
                len = strlen(storage.sun_path) + 3;

		(*res) = new_malloc(sizeof(*results));
		(*res)->ai_flags = 0;
		(*res)->ai_family = AF_UNIX;
		(*res)->ai_socktype = SOCK_STREAM;
		(*res)->ai_protocol = 0;
		(*res)->ai_addrlen = len;
		(*res)->ai_canonname = m_strdup(nodename);
		(*res)->ai_addr = new_malloc(sizeof(storage));
		*(USA *)((*res)->ai_addr) = storage;
		(*res)->ai_next = 0;

                return 0;
	}
#endif

	return getaddrinfo(nodename, servname, hints, res);
}

static void	Freeaddrinfo (AI *ai)
{
#ifdef GETADDRINFO_DOES_NOT_DO_AF_UNIX
	if (ai->ai_family == AF_UNIX)
	{
		new_free(&ai->ai_canonname);
		new_free(&ai->ai_addr);
		new_free(&ai);
		return;
	}
#endif

	freeaddrinfo(ai);
}

static socklen_t	socklen (SA *sockaddr)
{
	if (sockaddr->sa_family == AF_INET)
		return sizeof(ISA);
	else if (sockaddr->sa_family == AF_INET6)
		return sizeof(ISA6);
	else if (sockaddr->sa_family == AF_UNIX)
		return strlen(((USA *)sockaddr)->sun_path) + 2;
	else
		return 0;
}

