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

static Hostent *resolv (const char *);
static Hostent *lookup_host (const char *);
static Hostent *lookup_ip (const char *);
static int	get_low_portnum (void);
static int	get_high_portnum (void);

/*
	#define SERVICE_SERVER 0
	#define SERVICE_CLIENT 1
	#define PROTOCOL_TCP 0
	#define PROTOCOL_UDP 1
*/
/*
 * connect_by_number:  Wheeeee. Yet another monster function i get to fix
 * for the sake of it being inadequate for extension.
 *
 * we now take four arguments:
 *
 *	- hostname - name of the host (pathname) to connect to (if applicable)
 *	- portnum - port number to connect to or listen on (0 if you dont care)
 *		    IMPORTANT! PORTNUM IS IN HOST ORDER! IMPORTANT!
 *	- service -	0 - set up a listening socket
 *			1 - set up a connecting socket
 *	- protocol - 	0 - use the TCP protocol
 *			1 - use the UDP protocol
 *
 *
 * Returns:
 *	Non-negative number -- new file descriptor ready for use
 *	-1 -- could not open a new file descriptor or 
 *		an invalid value for the protocol was specified
 *	-2 -- call to bind() failed
 *	-3 -- call to listen() failed.
 *	-4 -- call to connect() failed
 *	-5 -- call to getsockname() failed
 *	-6 -- the name of the host could not be resolved
 *	-7 -- invalid or unsupported request
 *
 *
 * Credit: I couldnt have put this together without the help of 4.4BSD
 * User Supplimentary Document #20 (Inter-process Communications tutorial)
 */
int	connect_by_number (char *host, unsigned short *port, int service, int protocol)
{
	int	fd = -1;
	int	is_unix,
		sock_type, 
		proto_type;

	is_unix = (host && *host == '/');
	sock_type = (is_unix) ? AF_UNIX : AF_INET;
	proto_type = (protocol == PROTOCOL_TCP) ? SOCK_STREAM : SOCK_DGRAM;

	if ((fd = socket(sock_type, proto_type, 0)) < 0)
		return -1;

	set_socket_options (fd);

	/* Unix domain server */
#ifdef HAVE_SYS_UN_H
	if (is_unix)
	{
		USA	name;

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

		if (is_unix && (service == SERVICE_SERVER))
		{
			if (bind(fd, (SA *)&name, strlen(name.sun_path) + 2))
				return close(fd), -2;

			if (protocol == PROTOCOL_TCP)
				if (listen(fd, 4) < 0)
					return close(fd), -3;
		}

		/* Unix domain client */
		else if (service == SERVICE_CLIENT)
		{
			alarm(get_int_var(CONNECT_TIMEOUT_VAR));
			if (connect(fd, (SA *)&name, strlen(name.sun_path) + 2) < 0)
			{
				alarm(0);
				return close(fd), -4;
			}
			alarm(0);
		}
	}
	else
#endif

	/* Inet domain server */
	if (!is_unix && (service == SERVICE_SERVER))
	{
		int 	length;
		ISA	name;
		int	i;

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

		/*
		 * Make five stabs at trying to set up a passive socket.
		 */
		for (i = 0; i < 5; i++)
		{
			/* 
			 * Reset the local sockaddr...
			 * Use the address stipulated by the user
			 * via /hostname if there is one, otherwise
			 * just ask for INADDR_ANY.
			 */
			memset(&name, 0, sizeof(name));
			if (LocalHostName)
				name = LocalIPv4Addr;
			else
			{
				name.sin_family = AF_INET;
				name.sin_addr.s_addr = htonl(INADDR_ANY);
			}

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
			if (*port == 0)
			{
			    if (get_int_var(RANDOM_LOCAL_PORTS_VAR))
			    {
				int	lowport, highport;

				lowport = get_low_portnum();
				highport = get_high_portnum();
				name.sin_port = htons(random_number(0) % 
						(highport - lowport) + lowport);
			    }
			    else
				name.sin_port = htons(0);
			}
			else
			    name.sin_port = htons(*port);

			/*
			 * Try to establish the local side of our passive
			 * socket with our sockaddr.  If it fails because
			 * the port is in use or not available, try again.
			 * If it fails five tims, bummer for the user.
			 */
			if (bind(fd, (SA *)&name, sizeof(name)))
			{
			    if (errno == EADDRINUSE || errno == EADDRNOTAVAIL)
			    {
				if (i >= 5)
					return close(fd), -2;
				else
					continue;
			    }
			    else 
				return close(fd), -2;
			}
			break;
		}

		/*
		 * Get the local sockaddr of the passive socket,
		 * specifically the port number, and stash it in
		 * 'port' which is the return value to the caller.
		 */	
		length = sizeof(name);
		if (getsockname(fd, (SA *)&name, &length))
			return close(fd), -5;

		*port = ntohs(name.sin_port);

		if (protocol == PROTOCOL_TCP)
			if (listen(fd, 4) < 0)
				return close(fd), -3;
	}

	/* Inet domain client */
	else if (!is_unix && (service == SERVICE_CLIENT))
	{
		ISA	server;
		ISA	localaddr;

		/*
		 * Doing this bind is bad news unless you are sure that
		 * the hostname is valid.  This is not true for me at home,
		 * since i dynamic-ip it.
		 */
		if (LocalHostName)
		{
			localaddr = LocalIPv4Addr;
			localaddr.sin_port = htonl(0);
			if (bind(fd, (SA *)&localaddr, sizeof(localaddr)))
				return close(fd), -2;
		}

		/*
		 * If this is an IP we're looking up, dont do a full resolve,
		 * as it doesnt matter anyways.
		 *
		 * XXXXX THIS IS PROBABLY A HACK! XXXXX
		 * At least dcc_open calls inet_ntoa, only to have us
		 * immediately call inet_aton again.
		 * Does anyone call us here with a hostname?
		 */
		memset(&server, 0, sizeof(server));
		server.sin_family = AF_INET;
		server.sin_port = htons(*port);

		if (inet_anyton(host, &server.sin_addr))
		{
			errno = -1;
			return close(fd), -6;
		}

		alarm(get_int_var(CONNECT_TIMEOUT_VAR));
		if (connect(fd, (SA *)&server, sizeof(server)) < 0)
		{
			alarm(0);
			return close(fd), -4;
		}
		alarm(0);
	}

	/* error */
	else
		return close(fd), -7;

	return fd;
}


int	inet_anyton (const char *hostname, IA *addr)
{
	Hostent 	*hp;

	if (isdigit(last_char(hostname)))
	{
		if (inet_aton(hostname, addr) == 1)
			return 0;
		return -1;
	}

	if (!(hp = resolv(hostname)))
		return -1;

	*addr = *(IA *)hp->h_addr;
	return 0;
}

static Hostent *resolv (const char *stuff)
{
	Hostent *hep;

	if (!*stuff)
		return NULL;

	if (isdigit(last_char(stuff)))
		hep = lookup_ip(stuff);
	else
		hep = lookup_host(stuff);

	return hep;
}

static Hostent *lookup_host (const char *host)
{
	Hostent *hep;

	alarm(1);
	hep = gethostbyname(host);
	alarm(0);
	return hep;
}

static Hostent *lookup_ip (const char *ip)
{
	IA addr;
	Hostent *hep;

	if (inet_aton(ip, &addr) == 0)
		return NULL;

	alarm(1);
	hep = gethostbyaddr((const char *)&addr, 4, AF_INET);
	alarm(0);

	return hep;
}

char *	host_to_ip (const char *host)
{
	Hostent *hep;
	static char ip[256];

	if (!(hep = lookup_host(host)))
		return empty_string;

	strlcpy(ip, inet_ntoa(*(IA *)hep->h_addr), sizeof(ip));
	return ip;
}

char *	ip_to_host (const char *ip)
{
	Hostent *hep;
	static char host[256];		/* maximum hostname size. */

	if (!(hep = lookup_ip(ip)))
		return empty_string;

	strlcpy(host, hep->h_name, sizeof(host));
	return host;
}

char *	one_to_another (const char *what)
{
	/* if the last char is a digit, it's an ip, else a hostname. */
	if (isdigit(last_char(what)))
		return ip_to_host(what);
	else
		return host_to_ip(what);
}

int	set_non_blocking (int fd)
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

int	set_blocking (int fd)
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
int	my_accept (int s, struct sockaddr *addr, int *addrlen)
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

