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
#include "irc_std.h"
#include "gailib.h"

#undef INET6
#ifndef INET6

/*
 * getaddrinfo.c
 */
#define SUCCESS 0
#define ANY 0
#define YES 1
#define NO  0

static const char in_addrany[] = { 0, 0, 0, 0 };
static const char in_loopback[] = { 127, 0, 0, 1 }; 

struct sockinet {
	u_char	si_len;
	u_char	si_family;
	unsigned short	si_port;
};

static struct afd {
	int a_af;
	int a_addrlen;
	int a_socklen;
	int a_off;
	const char *a_addrany;
	const char *a_loopback;	
} afdl [] = {
#define N_INET  0
	{PF_INET, sizeof(struct in_addr),
	 sizeof(struct sockaddr_in),
	 offsetof(struct sockaddr_in, sin_addr),
	 in_addrany, in_loopback},
	{0, 0, 0, 0, NULL, NULL},
};

#define PTON_MAX	4

static int get_name (const char *, struct afd *, AI **, char *, AI *, int);
static int get_addr (const char *, int, AI **, AI *, int);
static int str_isnumber (const char *);
	
static char *ai_errlist[] = {
	"success.",
	"address family for hostname not supported.",	/* EAI_ADDRFAMILY */
	"temporary failure in name resolution.",	/* EAI_AGAIN      */
	"invalid value for ai_flags.",		       	/* EAI_BADFLAGS   */
	"non-recoverable failure in name resolution.", 	/* EAI_FAIL       */
	"ai_family not supported.",			/* EAI_FAMILY     */
	"memory allocation failure.", 			/* EAI_MEMORY     */
	"no address associated with hostname.", 	/* EAI_NODATA     */
	"hostname nor servname provided, or not known.",/* EAI_NONAME     */
	"servname not supported for ai_socktype.",	/* EAI_SERVICE    */
	"ai_socktype not supported.", 			/* EAI_SOCKTYPE   */
	"system error returned in errno.", 		/* EAI_SYSTEM     */
	"invalid value for hints.",			/* EAI_BADHINTS	  */
	"resolved protocol is unknown.",		/* EAI_PROTOCOL   */
	"unknown error.", 				/* EAI_MAX        */
};

#define GET_CANONNAME(ai, str) \
if (pai->ai_flags & AI_CANONNAME) {\
	if (((ai)->ai_canonname = (char *)malloc(strlen(str) + 1)) != NULL) {\
		strlcpy((ai)->ai_canonname, (str), strlen(str) + 1);\
	} else {\
		error = EAI_MEMORY;\
		goto free;\
	}\
}

#define GET_AI(ai, afd, addr, port) {\
	char *p;\
	if (((ai) = (AI *)malloc(sizeof(AI) + ((afd)->a_socklen))) == NULL) {\
		error = EAI_MEMORY;\
		goto free;\
	}\
	memcpy(ai, pai, sizeof(AI));\
	(ai)->ai_addr = (struct sockaddr *)((ai) + 1);\
	memset((ai)->ai_addr, 0, (afd)->a_socklen);\
	SET_SA_LEN((ai)->ai_addr, (ai)->ai_addrlen = (afd)->a_socklen);\
	(ai)->ai_addr->sa_family = (ai)->ai_family = (afd)->a_af;\
	((struct sockinet *)(ai)->ai_addr)->si_port = port;\
	p = (char *)((ai)->ai_addr);\
	memcpy(p + (afd)->a_off, (addr), (afd)->a_addrlen);\
}

#define ERR(err) { error = (err); goto bad; }

static int inet_pton__compat (int af, const char *hostname, void *pton)
{
	struct in_addr in;
	int d1, d2, d3, d4;
	char ch;

	if (sscanf(hostname, "%d.%d.%d.%d%c", &d1, &d2, &d3, &d4, &ch) == 4 &&
	    0 <= d1 && d1 <= 255 && 0 <= d2 && d2 <= 255 &&
	    0 <= d3 && d3 <= 255 && 0 <= d4 && d4 <= 255) {
	    in.s_addr = htonl(
		((long) d1 << 24) | ((long) d2 << 16) |
		((long) d3 << 8) | ((long) d4 << 0));
	}
	else {
	    return 0;
	}
	memcpy(pton, &in, sizeof(in));
	return 1;
}

static const char * inet_ntop__compat (int af, const void *addr, char *numaddr, size_t numaddr_len)
{
	unsigned long x = ntohl(*(unsigned long*)addr);
	snprintf(numaddr, numaddr_len, "%d.%d.%d.%d",
		 (int) (x>>24) & 0xff, (int) (x>>16) & 0xff,
		 (int) (x>> 8) & 0xff, (int) (x>> 0) & 0xff);
	return numaddr;
}


const char * gai_strerror__compat (int ecode)
{
	if (ecode < 0 || ecode > EAI_MAX)
		ecode = EAI_MAX;
	return ai_errlist[ecode];
}

void freeaddrinfo__compat (AI *ai)
{
	AI *next;

	do {
		next = ai->ai_next;
		if (ai->ai_canonname)
			free(ai->ai_canonname);
		/* no need to free(ai->ai_addr) */
		free(ai);
	} while ((ai = next) != NULL);
}

static int str_isnumber (const char *p)
{
	char *q = (char *)p;
	while (*q) {
		if (! isdigit(*q))
			return NO;
		q++;
	}
	return YES;
}


/***************************************************************************/
int getaddrinfo__compat (const char *hostname, const char *servname, const AI *hints, AI **res)
{
	AI sentinel;
	AI *top = NULL;
	AI *cur;
	int i, error = 0;
	char pton[PTON_MAX];
	AI ai;
	AI *pai;
	unsigned short port;

	/* initialize file static vars */
	sentinel.ai_next = NULL;
	cur = &sentinel;
	pai = &ai;
	pai->ai_flags = 0;
	pai->ai_family = PF_UNSPEC;
	pai->ai_socktype = ANY;
	pai->ai_protocol = ANY;
	pai->ai_addrlen = 0;
	pai->ai_canonname = NULL;
	pai->ai_addr = NULL;
	pai->ai_next = NULL;
	port = ANY;
	
	if (hostname == NULL && servname == NULL)
		return EAI_NONAME;
	if (hints) {
		/* error check for hints */
		if (hints->ai_addrlen || hints->ai_canonname ||
		    hints->ai_addr || hints->ai_next)
			ERR(EAI_BADHINTS); /* xxx */
		if (hints->ai_flags & ~AI_MASK)
			ERR(EAI_BADFLAGS);
		switch (hints->ai_family) {
		case PF_UNSPEC:
		case PF_INET:
			break;
		default:
			ERR(EAI_FAMILY);
		}
		memcpy(pai, hints, sizeof(*pai));
		switch (pai->ai_socktype) {
		case ANY:
			switch (pai->ai_protocol) {
			case ANY:
				break;
			case IPPROTO_UDP:
				pai->ai_socktype = SOCK_DGRAM;
				break;
			case IPPROTO_TCP:
				pai->ai_socktype = SOCK_STREAM;
				break;
			default:
#if defined(SOCK_RAW)
				pai->ai_socktype = SOCK_RAW;
#endif
				break;
			}
			break;
#if defined(SOCK_RAW)
		case SOCK_RAW:
			break;
#endif
		case SOCK_DGRAM:
			if (pai->ai_protocol != IPPROTO_UDP &&
			    pai->ai_protocol != ANY)
				ERR(EAI_BADHINTS);	/*xxx*/
			pai->ai_protocol = IPPROTO_UDP;
			break;
		case SOCK_STREAM:
			if (pai->ai_protocol != IPPROTO_TCP &&
			    pai->ai_protocol != ANY)
				ERR(EAI_BADHINTS);	/*xxx*/
			pai->ai_protocol = IPPROTO_TCP;
			break;
		default:
			ERR(EAI_SOCKTYPE);
			break;
		}
	}

	/*
	 * service port
	 */
	if (servname) {
		if (str_isnumber(servname)) {
			if (pai->ai_socktype == ANY) {
				/* caller accept *ANY* socktype */
				pai->ai_socktype = SOCK_DGRAM;
				pai->ai_protocol = IPPROTO_UDP;
			}
			port = htons((unsigned short)atoi(servname));
		} else {
			struct servent *sp;
			char *proto;

			proto = NULL;
			switch (pai->ai_socktype) {
			case ANY:
				proto = NULL;
				break;
			case SOCK_DGRAM:
				proto = "udp";
				break;
			case SOCK_STREAM:
				proto = "tcp";
				break;
			default:
				fprintf(stderr, "panic!\n");
				break;
			}
			if ((sp = getservbyname(servname, proto)) == NULL)
				ERR(EAI_SERVICE);
			port = sp->s_port;
			if (pai->ai_socktype == ANY) {
				if (strcmp(sp->s_proto, "udp") == 0) {
					pai->ai_socktype = SOCK_DGRAM;
					pai->ai_protocol = IPPROTO_UDP;
				} else if (strcmp(sp->s_proto, "tcp") == 0) {
					pai->ai_socktype = SOCK_STREAM;
					pai->ai_protocol = IPPROTO_TCP;
				} else {
					ERR(EAI_PROTOCOL);	/*xxx*/
				}
			}
		}
	}
	
	/*
	 * hostname == NULL.
	 * passive socket -> anyaddr (0.0.0.0 or ::)
	 * non-passive socket -> localhost (127.0.0.1 or ::1)
	 */
	if (hostname == NULL) {
		struct afd *afd;
		int s;

		for (afd = &afdl[0]; afd->a_af; afd++) {
			if (!(pai->ai_family == PF_UNSPEC
			   || pai->ai_family == afd->a_af)) {
				continue;
			}

			/*
			 * filter out AFs that are not supported by the kernel
			 * XXX errno?
			 */
			s = socket(afd->a_af, SOCK_DGRAM, 0);
			if (s < 0)
				continue;
			close(s);

			if (pai->ai_flags & AI_PASSIVE) {
				GET_AI(cur->ai_next, afd, afd->a_addrany, port);
				/* xxx meaningless?
				 * GET_CANONNAME(cur->ai_next, "anyaddr");
				 */
			} else {
				GET_AI(cur->ai_next, afd, afd->a_loopback,
					port);
				/* xxx meaningless?
				 * GET_CANONNAME(cur->ai_next, "localhost");
				 */
			}
			cur = cur->ai_next;
		}
		top = sentinel.ai_next;
		if (top)
			goto good;
		else
			ERR(EAI_FAMILY);
	}
	
	/* hostname as numeric name */
	for (i = 0; afdl[i].a_af; i++) {
		if (inet_pton__compat(afdl[i].a_af, hostname, pton)) {
			unsigned long v4a;

			switch (afdl[i].a_af) {
			case AF_INET:
				v4a = ((struct in_addr *)pton)->s_addr;
				if (IN_MULTICAST(v4a) || IN_EXPERIMENTAL(v4a))
					pai->ai_flags &= ~AI_CANONNAME;
				v4a >>= IN_CLASSA_NSHIFT;
				if (v4a == 0 || v4a == IN_LOOPBACKNET)
					pai->ai_flags &= ~AI_CANONNAME;
				break;
			}
			
			if (pai->ai_family == afdl[i].a_af ||
			    pai->ai_family == PF_UNSPEC) {
				if (! (pai->ai_flags & AI_CANONNAME)) {
					GET_AI(top, &afdl[i], pton, port);
					goto good;
				}
				/*
				 * if AI_CANONNAME and if reverse lookup
				 * fail, return ai anyway to pacify
				 * calling application.
				 *
				 * XXX getaddrinfo() is a name->address
				 * translation function, and it looks strange
				 * that we do addr->name translation here.
				 */
				get_name(pton, &afdl[i], &top, pton, pai, port);
				goto good;
			} else 
				ERR(EAI_FAMILY);	/*xxx*/
		}
	}

	if (pai->ai_flags & AI_NUMERICHOST)
		ERR(EAI_NONAME);

	/* hostname as alphabetical name */
	error = get_addr(hostname, pai->ai_family, &top, pai, port);
	if (error == 0) {
		if (top) {
 good:
			*res = top;
			return SUCCESS;
		} else
			error = EAI_FAIL;
	}
 free:
	if (top)
		freeaddrinfo(top);
 bad:
	*res = NULL;
	return error;
}

static int get_name (const char *addr, struct afd *afd, AI **res, char *numaddr, AI *pai, int port0)
{
	unsigned short port = port0 & 0xffff;
	struct hostent *hp;
	AI *cur;
	int error = 0;

	hp = gethostbyaddr(addr, afd->a_addrlen, AF_INET);
	if (hp && hp->h_name && hp->h_name[0] && hp->h_addr_list[0]) {
		GET_AI(cur, afd, hp->h_addr_list[0], port);
		GET_CANONNAME(cur, hp->h_name);
	} else
		GET_AI(cur, afd, numaddr, port);
	
	*res = cur;
	return SUCCESS;
 free:
	if (cur)
		freeaddrinfo(cur);
 /* bad: */
	*res = NULL;
	return error;
}

static int get_addr (const char *hostname, int af, AI **res, AI *pai, int port0)
{
	unsigned short port = port0 & 0xffff;
	AI sentinel;
	struct hostent *hp;
	AI *top, *cur;
	struct afd *afd;
	int i, error = 0, h_error;
	char *ap;

	top = NULL;
	sentinel.ai_next = NULL;
	cur = &sentinel;
	hp = gethostbyname(hostname);
	h_error = h_errno;
	if (hp == NULL) {
		switch (h_error) {
		case HOST_NOT_FOUND:
		case NO_DATA:
			error = EAI_NODATA;
			break;
		case TRY_AGAIN:
			error = EAI_AGAIN;
			break;
		case NO_RECOVERY:
		default:
			error = EAI_FAIL;
			break;
		}
		goto bad;
	}

	if ((hp->h_name == NULL) || (hp->h_name[0] == 0) ||
	    (hp->h_addr_list[0] == NULL))
		ERR(EAI_FAIL);
	
	for (i = 0; (ap = hp->h_addr_list[i]) != NULL; i++) {
		switch (af) {
		default:	/* AF_UNSPEC */
		case AF_INET:
			afd = &afdl[N_INET];
			break;
		}
		GET_AI(cur->ai_next, afd, ap, port);
		if (cur == &sentinel) {
			top = cur->ai_next;
			GET_CANONNAME(top, hp->h_name);
		}
		cur = cur->ai_next;
	}
	*res = top;
	return SUCCESS;
 free:
	if (top)
		freeaddrinfo(top);
 bad:
	*res = NULL;
	return error;
}

/****************************************************************************/
/* getnameinfo.c */
#define SUCCESS 0
#define ANY 0
#define YES 1
#define NO  0

#define ENI_NOSOCKET 	0
#define ENI_NOSERVNAME	1
#define ENI_NOHOSTNAME	2
#define ENI_MEMORY	3
#define ENI_SYSTEM	4
#define ENI_FAMILY	5
#define ENI_SALEN	6



int getnameinfo__compat (const struct sockaddr *sa, size_t salen, char *host, size_t hostlen, char *serv, size_t servlen, int flags)
{
	struct afd *afd;
#if defined(HAVE_GETSERVBYPORT)
	struct servent *sp;
#endif
	struct hostent *hp;
	unsigned short port;
	int family, len, i;
	char *addr, *p;
	unsigned long v4a;
	int h_error;
	char numserv[512];
	char numaddr[512];

	if (sa == NULL)
		return ENI_NOSOCKET;

	len = SA_LEN(sa);
	if (len != salen) return ENI_SALEN;
	
	family = sa->sa_family;
	for (i = 0; afdl[i].a_af; i++)
		if (afdl[i].a_af == family) {
			afd = &afdl[i];
			goto found;
		}
	return ENI_FAMILY;
	
 found:
	if (len != afd->a_socklen) return ENI_SALEN;
	
	port = ((struct sockinet *)sa)->si_port; /* network byte order */
	addr = (char *)sa + afd->a_off;

	if (serv == NULL || servlen == 0) {
		/* what we should do? */
	} else if (flags & NI_NUMERICSERV) {
		snprintf(numserv, sizeof(numserv), "%d", ntohs(port));
		if (strlen(numserv) > servlen)
			return ENI_MEMORY;
		strlcpy(serv, numserv, servlen);
	} else {
#if defined(HAVE_GETSERVBYPORT)
		sp = getservbyport(port, (flags & NI_DGRAM) ? "udp" : "tcp");
		if (sp) {
			if (strlen(sp->s_name) > servlen)
				return ENI_MEMORY;
			strlcpy(serv, sp->s_name, servlen);
		} else
			return ENI_NOSERVNAME;
#else
		return ENI_NOSERVNAME;
#endif
	}

	switch (sa->sa_family) {
	case AF_INET:
		v4a = ((struct sockaddr_in *)sa)->sin_addr.s_addr;
		if (IN_MULTICAST(v4a) || IN_EXPERIMENTAL(v4a))
			flags |= NI_NUMERICHOST;
		v4a >>= IN_CLASSA_NSHIFT;
		if (v4a == 0 || v4a == IN_LOOPBACKNET)
			flags |= NI_NUMERICHOST;			
		break;
	}
	if (host == NULL || hostlen == 0) {
		/* what should we do? */
	} else if (flags & NI_NUMERICHOST) {
		if (inet_ntop__compat(afd->a_af, addr, numaddr, sizeof(numaddr))
		    == NULL)
			return ENI_SYSTEM;
		if (strlen(numaddr) > hostlen)
			return ENI_MEMORY;
		strlcpy(host, numaddr, hostlen);
	} else {
		hp = gethostbyaddr(addr, afd->a_addrlen, afd->a_af);
		h_error = h_errno;

		if (hp) {
			if (flags & NI_NOFQDN) {
				p = strchr(hp->h_name, '.');
				if (p) *p = '\0';
			}
			if (strlen(hp->h_name) > hostlen) {
				return ENI_MEMORY;
			}
			strlcpy(host, hp->h_name, hostlen);
		} else {
			if (flags & NI_NAMEREQD)
				return ENI_NOHOSTNAME;
			if (inet_ntop__compat(afd->a_af, addr, numaddr, sizeof(numaddr))
			    == NULL)
				return ENI_NOHOSTNAME;
			if (strlen(numaddr) > hostlen)
				return ENI_MEMORY;
			strlcpy(host, numaddr, hostlen);
		}
	}
	return SUCCESS;
}
#endif
