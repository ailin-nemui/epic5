/*
 * network.c -- handles stuff dealing with connecting and name resolving
 *
 * Written by Jeremy Nelson
 * Copyright 1995 Jeremy Nelson
 * See the COPYRIGHT file or do /help ircii copyright
 */

#include "irc.h"
#include "ircaux.h"
#include "vars.h"
#include "newio.h"

#ifdef HAVE_SYS_UN_H
#include <sys/un.h>
typedef struct sockaddr_un USA;
#endif

static Hostent *resolv (int, const char *);
static Hostent *lookup_host (int, const char *);
static Hostent *lookup_addr (int family, const char *ip);
static int	get_low_portnum (void);
static int	get_high_portnum (void);
static int	set_non_blocking (int fd);
static int	set_blocking (int fd);
static int	inet_remotesockaddr (int family, const char *host, u_short port, SS *storage, socklen_t *len);
static int	inet_vhostsockaddr (int family, SS *storage, socklen_t *len);


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
*/

/* The comments for this function are gone. :d */
/* NOTES: This function must die. */
int	connect_by_number (char *host, unsigned short *port, int family, int service)
{
	if (family == AF_UNSPEC)
	{
		if (host && *host == '/')
			family = AF_UNIX;
		else if (host && strchr(host, ':'))
			family = AF_INET6;		/* IPv6 */
		else
			family = AF_INET;
	}

	/* Unix domain server */
	if (family == AF_UNIX)
	{
#ifndef HAVE_SYS_UN_H
		yell("Unix Domain sockets are not supported on your system.");
		return -2;
#else
		USA	name;
		int	socklen;

		memset(&name, 0, sizeof(name));
		name.sun_family = AF_UNIX;
		strlcpy(name.sun_path, host, sizeof(name.sun_path));
#ifdef HAVE_SUN_LEN
# ifdef SUN_LEN
		name.sun_len = SUN_LEN(&name);
# else
		name.sun_len = strlen(host) + 1;
# endif
#endif
		socklen = strlen(name.sun_path) + 2;


		if (service == SERVICE_SERVER)
		    return client_bind((SA *)&name, socklen);
		else 	/* Unix domain client */
		    return client_connect(NULL, 0, (SA *)&name, socklen);
#endif
	}

	/* Inet domain client */
	else if (service == SERVICE_CLIENT)
		return connectory(family, host, *port);

	/* error */
	return -2;
}


/*
 * NAME: inet_vhostsockaddr
 * USAGE: Get the sockaddr of the current virtual host, if one is in use.
 * ARGS: family - The family whose sockaddr info is to be retrieved
 *       storage - Pointer to a sockaddr structure appropriate for family.
 *       len - This will be set to the size of the sockaddr structure that
 *             was copied, or set to 0 if no virtual host is available in
 *             the given family.
 * NOTES: If "len" is set to 0, do not attempt to bind() 'storage'!
 * NOTES: This function is protocol independant but lacks IPv6 support.
 */
static int	inet_vhostsockaddr (int family, SS *storage, socklen_t *len)
{
	if (family == AF_INET)
	{
		if (LocalHostName)
		{
			*(ISA *)storage = LocalIPv4Addr;
			((ISA *)storage)->sin_port = htonl(0);
			*len = sizeof(ISA);
			return 0;
		}
		*len = 0;
		return 0;
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
 * NOTES: This function is protocol independant but lacks IPv6 support.
 */
static int	inet_remotesockaddr (int family, const char *host, u_short port, SS *storage, socklen_t *len)
{
	int	err;

	if (family == AF_INET)
	{
		memset(storage, 0, sizeof(ISA));
		((ISA *)storage)->sin_family = family;
		((ISA *)storage)->sin_port = htons(port);

		/*
		 * Call inet_anyton() to get a (struct in_addr) of the
		 * hostname.  inet_anyton() does not do a DNS lookup for
		 * dotted quad IPv4 addresses, so it's "cheap" for that
		 * purpose.
		 */
		if ((err = inet_anyton(host, (SA *)storage)))
			return err;

		*len = sizeof(ISA);
		return 0;
	}

	return -2;
}


/* NOTES: This function is protocol independant but lacks IPv6 support. */
int	inet_anyton (const char *hostname, SA *storage)
{
	int family = storage->sa_family;

	if (family == AF_INET)
	{
		Hostent	*hp;

		if (isdigit(last_char(hostname)))
		{
			if (strchr(hostname, ':'))
				return -4;
			else if (strchr(hostname, '.'))
			{
				if (inet_pton(AF_INET, hostname, &((ISA *)storage)->sin_addr) == 1)
					return 0;
				return -4;
			}
			else
			{
				((ISA *)storage)->sin_addr.s_addr = htonl(strtoul(hostname, NULL, 10));
				return 0;
			}
		}
		else
		{
			if (!(hp = resolv(family, hostname)))
				return -5;

			if (hp->h_addrtype != family)
				return -3;		/* IPv6 */

			((ISA *)storage)->sin_addr = *(IA *)hp->h_addr;
			return 0;
		}
	}

	return -2;
}

/* NOTES: This function is protocol independant but lacks IPv6 support. */
static Hostent *resolv (int family, const char *stuff)
{
	Hostent *hep;

	if (!*stuff)
		return NULL;

	if (family == AF_INET)
	{
		if (isdigit(last_char(stuff)))
			hep = lookup_addr(family, stuff);
		else
			hep = lookup_host(family, stuff);
	}

	return hep;
}

/* NOTES: This function is protocol independant */
static Hostent *lookup_host (int family, const char *host)
{
	Hostent *hep = NULL;

	alarm(1);
#ifdef HAVE_GETHOSTBYNAME2
	hep = gethostbyname2(host, family);
#else
	if (family == AF_INET)
		hep = gethostbyname(host);
#endif
	alarm(0);
	return hep;
}

/* NOTES: This function is protocol independant but lacks IPv6 support. */
static Hostent *lookup_addr (int family, const char *ip)
{
	char	addr[256];
	Hostent *hep = NULL;

	if (inet_pton(family, ip, addr) <= 0)
		return NULL;

	alarm(1);
	if (family == AF_INET)
		hep = gethostbyaddr(addr, 4, family);
	alarm(0);

	return hep;
}

/* NOTES: This function is protocol independant */
char *	inet_hntop (int family, const char *host, char *retval, int size)
{
	Hostent *hep;

	if (!(hep = lookup_host(family, host)))
		return empty_string;

	if (!inet_ntop(family, hep->h_addr, retval, size))
		return empty_string;

	return retval;
}

/* NOTES: This function is protocol independant */
char *	inet_ptohn (int family, const char *ip, char *retval, int size)
{
	Hostent *hep;

	if (!(hep = lookup_addr(family, ip)))
		return empty_string;

	strlcpy(retval, hep->h_name, size);
	return retval;
}

/* NOTES: This function is protocol independant */
char *	one_to_another (int family, const char *what, char *retval, int size)
{
	if ((retval = inet_ptohn(family, what, retval, size)) == empty_string)
		retval = inet_hntop(family, what, retval, size);
	return retval;
}

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


/*
 * It is possible for a race condition to exist; such that select()
 * indicates that a listen()ing socket is able to recieve a new connection
 * and that a later accept() call will still block because the connection
 * has been closed in the interim.  This wrapper for accept() attempts to
 * defeat this by making the accept() call nonblocking.
 */
int	my_accept (int s, SA *addr, int *addrlen)
{
	int	retval;

	set_non_blocking(s);
	retval = accept(s, addr, addrlen);
	set_blocking(s);
	return retval;
}

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


/*****************************************************************************/
/*
 * NAME: connectory
 * USAGE: Connect to a given "host" and "port" with the given "family"
 * ARGS: family - AF_INET is the only supported argument.
 *       host - A hostname or "dotted quad" (or equivalent).
 *       port - The remote port to connect to in *HOST ORDER*.
 * NOTES: This function is family independant.
 */
int	connectory (int family, const char *host, unsigned short port)
{
	SS	  localaddr;
	socklen_t locallen;
	SS	  remaddr;
	socklen_t remlen;
	int	  err;

	if (family == AF_UNSPEC)
	{
		if (host && *host == '/')
			family = AF_UNIX;
		else if (host && strchr(host, ':'))
			family = AF_INET6;
		else
			family = AF_INET;
	}

	if ((err = inet_vhostsockaddr(family, (SS *)&localaddr, &locallen)))
		return err;

	if ((err = inet_remotesockaddr(family, host, port, 
					(SS *)&remaddr, &remlen)))
		return err;

	return client_connect((SA *)&localaddr, locallen, 
			      (SA *)&remaddr, remlen);
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
 * NOTES: This function is protocol independant but lacks IPv6 support
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
		if (connect(fd, r, rl) < 0)
		{
			alarm(0);
			return close(fd), -9;
		}
		alarm(0);
	}
	else if (family == AF_INET)
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
		err = connect(fd, r, rl);
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
				ISA	peer;
				int	peerlen;

				peerlen = sizeof(peer);
				if (getpeername(fd, (SA *)&peer, &peerlen))
					return close(fd), -12;
				set_blocking(fd);
			}
		}
	}

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
 * NOTES: This function is protocol independant but lacks IPv6 support.
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
 * NOTES: This function is protocol independant.
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
	if (getenv("EPIC_USE_HIGHPORTS"))
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

