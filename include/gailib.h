/*
 * Copyright (C) 1995, 1996, 1997, 1998, and 1999 WIDE Project.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef __gailib_h__
#define __gailib_h__

/* special compatibility hack */
#undef EAI_ADDRFAMILY
#undef EAI_AGAIN
#undef EAI_BADFLAGS
#undef EAI_FAIL
#undef EAI_FAMILY
#undef EAI_MEMORY
#undef EAI_NODATA
#undef EAI_NONAME
#undef EAI_SERVICE
#undef EAI_SOCKTYPE
#undef EAI_SYSTEM
#undef EAI_BADHINTS
#undef EAI_PROTOCOL
#undef EAI_MAX

#undef AI_PASSIVE
#undef AI_CANONNAME
#undef AI_NUMERICHOST
#undef AI_ALL
#undef AI_MASK
#undef AI_ADDRCONFIG
#undef AI_V4MAPPED
#undef AI_DEFAULT

#undef NI_MAXHOST
#undef NI_MAXSERV

#undef NI_NOFQDN
#undef NI_NUMERICHOST
#undef NI_NAMEREQD
#undef NI_NUMERICSERV
#undef NI_DGRAM

/*
 * Error return codes from getaddrinfo()
 */
#define	EAI_ADDRFAMILY	 1	/* address family for hostname not supported */
#define	EAI_AGAIN	 2	/* temporary failure in name resolution */
#define	EAI_BADFLAGS	 3	/* invalid value for ai_flags */
#define	EAI_FAIL	 4	/* non-recoverable failure in name resolution */
#define	EAI_FAMILY	 5	/* ai_family not supported */
#define	EAI_MEMORY	 6	/* memory allocation failure */
#define	EAI_NODATA	 7	/* no address associated with hostname */
#define	EAI_NONAME	 8	/* hostname nor servname provided, or not known */
#define	EAI_SERVICE	 9	/* servname not supported for ai_socktype */
#define	EAI_SOCKTYPE	10	/* ai_socktype not supported */
#define	EAI_SYSTEM	11	/* system error returned in errno */
#define EAI_BADHINTS	12
#define EAI_PROTOCOL	13
#define EAI_MAX		14

/*
 * Flag values for getaddrinfo()
 */
#define	AI_PASSIVE	0x00000001 /* get address to use bind() */
#define	AI_CANONNAME	0x00000002 /* fill ai_canonname */
#define	AI_NUMERICHOST	0x00000004 /* prevent name resolution */
/* valid flags for addrinfo */
#define	AI_MASK		(AI_PASSIVE | AI_CANONNAME | AI_NUMERICHOST)

#define	AI_ALL		0x00000100 /* IPv6 and IPv4-mapped (with AI_V4MAPPED) */
#define	AI_V4MAPPED_CFG	0x00000200 /* accept IPv4-mapped if kernel supports */
#define	AI_ADDRCONFIG	0x00000400 /* only if any address is assigned */
#define	AI_V4MAPPED	0x00000800 /* accept IPv4-mapped IPv6 address */
/* special recommended flags for getipnodebyname */
#define	AI_DEFAULT	(AI_V4MAPPED_CFG | AI_ADDRCONFIG)

/*
 * Constants for getnameinfo()
 */
#define	NI_MAXHOST	1025
#define	NI_MAXSERV	32

/*
 * Flag values for getnameinfo()
 */
#define	NI_NOFQDN	0x00000001
#define	NI_NUMERICHOST	0x00000002
#define	NI_NAMEREQD	0x00000004
#define	NI_NUMERICSERV	0x00000008
#define	NI_DGRAM	0x00000010

extern int getaddrinfo__compat (
	const char *hostname, const char *servname,
	const struct addrinfo *hints,
	struct addrinfo **res);

extern int getnameinfo__compat (
	const struct sockaddr *sa,
	size_t salen,
	char *host,
	size_t hostlen,
	char *serv,
	size_t servlen,
	int flags);

extern void freeaddrinfo__compat (struct addrinfo *);
extern const char *gai_strerror__compat (int);

/* In case there is no definition of offsetof() provided - though any proper
Standard C system should have one. */

#ifndef offsetof
#define offsetof(p_type,field) ((size_t)&(((p_type *)0)->field))
#endif

/************************************************

  sockport.h -

  created at: Fri Apr 30 23:19:34 JST 1999

************************************************/

#ifndef SA_LEN
# ifdef HAVE_SA_LEN
#  define SA_LEN(sa) (sa)->sa_len
# else
#  ifdef INET6
#   define SA_LEN(sa) \
	(((sa)->sa_family == AF_INET6) ? sizeof(struct sockaddr_in6) \
				       : sizeof(struct sockaddr))
#  else
    /* by tradition, sizeof(struct sockaddr) covers most of the sockaddrs */
#   define SA_LEN(sa)	(sizeof(struct sockaddr))
#  endif
# endif
#endif

#ifdef HAVE_SA_LEN
# define SET_SA_LEN(sa, len) (sa)->sa_len = (len)
#else
# define SET_SA_LEN(sa, len) (len)
#endif

#ifdef HAVE_SIN_LEN
# define SIN_LEN(si) (si)->sin_len
# define SET_SIN_LEN(si,len) (si)->sin_len = (len)
#else
# define SIN_LEN(si) sizeof(struct sockaddr_in)
# define SET_SIN_LEN(si,len)
#endif

#ifndef IN_MULTICAST
# define IN_CLASSD(i)	(((long)(i) & 0xf0000000) == 0xe0000000)
# define IN_MULTICAST(i)	IN_CLASSD(i)
#endif

#ifndef IN_EXPERIMENTAL
# define IN_EXPERIMENTAL(i) ((((long)(i)) & 0xe0000000) == 0xe0000000)
#endif

#ifndef IN_CLASSA_NSHIFT
# define IN_CLASSA_NSHIFT 24
#endif

#ifndef IN_LOOPBACKNET
# define IN_LOOPBACKNET 127
#endif

#ifndef AF_UNSPEC
# define AF_UNSPEC 0
#endif

#ifndef PF_UNSPEC
# define PF_UNSPEC AF_UNSPEC
#endif

#ifndef PF_INET
# define PF_INET AF_INET
#endif

#if defined(HOST_NOT_FOUND) && !defined(h_errno) && !defined(__CYGWIN__)
extern int h_errno;
#endif

#endif
