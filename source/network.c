/* $EPIC: network.c,v 1.60 2005/02/10 05:10:57 jnelson Exp $ */
/*
 * network.c -- handles stuff dealing with connecting and name resolving
 *
 * Copyright © 1995, 2003 Jeremy Nelson
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
#include "vars.h"
#include "newio.h"
#include "output.h"
#include <sys/ioctl.h>

#ifdef HAVE_SYS_UN_H
#include <sys/un.h>
typedef struct sockaddr_un USA;
#endif

static int	set_non_blocking (int fd);
static int	set_blocking (int fd);
static int	inet_remotesockaddr (int family, const char *host, const char *port, SS *storage, socklen_t *len);
int	inet_vhostsockaddr (int family, int port, SS *storage, socklen_t *len);
static int	Connect (int fd, SA *addr);
static socklen_t	socklen (SA *sockaddr);
static int	Getnameinfo(const SA *sa, socklen_t salen, char *host, size_t hostlen, char *serv, size_t servlen, int flags);
static void    set_socket_options (int s);

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
	else if (family == AF_INET 
#ifdef INET6
				   || family == AF_INET6
#endif
							 )
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
		set_blocking(fd);
	}
	else
		return -2;

	if (x_debug & DEBUG_SERVER_CONNECT)
		yell("Connected on des [%d]", fd);
	return fd;
}


/*****************************************************************************/
/*
 * NAME: ip_bindery
 * USAGE: Establish a local passive (listening) socket at the given port.
 * ARGS: family - AF_INET is the only supported argument.
 *       port - The port to establish the connection upon.  May be 0, 
 *		which requests any open port.
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
	int	err;
	socklen_t len;

	if ((err = inet_vhostsockaddr(family, port, storage, &len)))
		return err;

	if (!len)
		return -6;

	return client_bind((SA *)storage, len);
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
int	inet_vhostsockaddr (int family, int port, SS *storage, socklen_t *len)
{
	char	p_port[12];
	char	*p = NULL;
	AI	hints, *res;
	int	err;
	const char *lhn;

	/*
	 * If port == -1, then this is a client connection, so we punt
	 * if there is no virtual host name.  But if port is NOT zero, then
	 * the caller expects us to return a sockaddr they can bind() to, 
	 * so we need to use LocalHostName, even if it's NULL.  If you 
	 * return *len == 0 for port != -1, then /dcc breaks.
	 */
	if ((family == AF_UNIX) || 
            (family == AF_INET && port == -1 && LocalIPv4HostName == NULL) 
#ifdef INET6
	 || (family == AF_INET6 && port == -1 && LocalIPv6HostName == NULL)
#endif
									   )
	{
		*len = 0;
		return 0;		/* No vhost needed */
	}

	if (family == AF_INET)
		lhn = LocalIPv4HostName;
#ifdef INET6
	else if (family == AF_INET6)
		lhn = LocalIPv6HostName;
#endif
	else
		lhn = NULL;

	/*
	 * Can it really be this simple?
	 */
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = family;
	hints.ai_socktype = SOCK_STREAM;
	if (port != -1) 
	{
		hints.ai_flags = AI_PASSIVE;
		snprintf(p_port, 12, "%u", port);
		p = p_port;
	}

	if ((err = Getaddrinfo(lhn, p, &hints, &res)))
		return -10;
	memcpy(storage, res->ai_addr, res->ai_addrlen);
	*len = socklen((SA *)storage);
	return 0;
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
			((ISA *)storage)->sin_port = htons((unsigned short)strtoul(port, NULL, 10));
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
int	inet_ntostr (SA *name, char *host, int hsize, char *port, int psize, int flags)
{
	int	retval;
	socklen_t len;

	len = socklen(name);
	if ((retval = Getnameinfo(name, len, host, hsize, port, psize, flags | NI_NUMERICSERV))) {
		yell("Getnameinfo (sockaddr->p_addr): %s", gai_strerror(retval));
		return retval;
	}

	return 0;
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
const char *	inet_hntop (int family, const char *host, char *retval, int size)
{
	int	err;
	SS	buffer;

	((SA *)&buffer)->sa_family = family;
	if ((err = inet_strton(host, NULL, (SA *)&buffer, 0)))
		return empty_string;

	if (inet_ntostr((SA *)&buffer, retval, size, NULL, 0, NI_NUMERICHOST))
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
const char *	inet_ptohn (int family, const char *ip, char *retval, int size)
{
	int	err;
	SS	buffer;

	((SA *)&buffer)->sa_family = family;
	if ((err = inet_strton(ip, NULL, (SA *)&buffer, AI_NUMERICHOST)))
		return empty_string;

	if (inet_ntostr((SA *)&buffer, retval, size, NULL, 0, NI_NAMEREQD))
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

/*
 * XXX - Ugh!  Some getaddrinfo()s take AF_UNIX paths as the 'servname'
 * instead of as the 'nodename'.  How heinous!
 */
int	Getaddrinfo (const char *nodename, const char *servname, const AI *hints, AI **res)
{
#ifdef GETADDRINFO_DOES_NOT_DO_AF_UNIX
	int	do_af_unix = 0;
	USA 	storage;
	AI *	results;
	int	len;

	if (nodename && strchr(nodename, '/'))
		do_af_unix = 1;
	if (hints && hints->ai_family == AF_UNIX)
		do_af_unix = 1;

	if (do_af_unix)
	{
                memset(&storage, 0, sizeof(storage));
                storage.sun_family = AF_UNIX;
                strlcpy(storage.sun_path, nodename, sizeof(storage.sun_path));
#ifdef HAVE_SA_LEN
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
		(*res)->ai_canonname = malloc_strdup(nodename);
		(*res)->ai_addr = new_malloc(sizeof(storage));
		*(USA *)((*res)->ai_addr) = storage;
		(*res)->ai_next = 0;

                return 0;
	}
#endif

	/*
	 * XXX -- Support getaddrinfo()s that want an AF_UNIX path to
	 * be the second argument and not the first one.  Bleh.
	 */
	if ((nodename && strchr(nodename, '/')) || 
	    (hints && hints->ai_family == AF_UNIX))
		return getaddrinfo(NULL, nodename, hints, res);
	else
		return getaddrinfo(nodename, servname, hints, res);
}

void	Freeaddrinfo (AI *ai)
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

static int	Getnameinfo(const SA *sa, socklen_t salen, char *host, size_t hostlen, char *serv, size_t servlen, int flags)
{
	if ((flags & GNI_INTEGER) && sa->sa_family == AF_INET) {
		snprintf(host, hostlen, "%lu", 
			(unsigned long)ntohl(((const ISA *)sa)->sin_addr.s_addr));
		host = NULL;
		hostlen = 0;
	}
	flags = flags & ~(GNI_INTEGER);
	return getnameinfo(sa, salen, host, hostlen, serv, servlen, flags);
}

static socklen_t	socklen (SA *sockaddr)
{
	if (sockaddr->sa_family == AF_INET)
		return sizeof(ISA);
#ifdef INET6
	else if (sockaddr->sa_family == AF_INET6)
		return sizeof(ISA6);
#endif
	else if (sockaddr->sa_family == AF_UNIX)
		return strlen(((USA *)sockaddr)->sun_path) + 2;
	else
		return 0;
}

/* set's socket options */
static void    set_socket_options (int s)
{
        int     opt = 1;
        int     optlen = sizeof(opt);
#ifndef NO_STRUCT_LINGER
        struct linger   lin;

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

