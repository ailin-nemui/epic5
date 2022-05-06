/*
 * Copyright 2003, 2005 Jeremy Nelson
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

#ifndef __NETWORK_H__
#define __NETWORK_H__

/* Used for connect_by_number */
#define SERVICE_SERVER  0
#define SERVICE_CLIENT  1

#if 0
/* Used from network.c */
#define V0(x) ((struct sockaddr *)&(x))
#define FAMILY(x) (V0(x)->sa_family)

#define V4(x) ((struct sockaddr_in *)&(x))
#define V4FAM(x) (V4(x)->sin_family)
#define V4ADDR(x) (V4(x)->sin_addr)
#define V4PORT(x) (V4(x)->sin_port)

#define V6(x) ((struct sockaddr_in6 *)&(x))
#define V6FAM(x) (V6(x)->sin6_family)
#define V6ADDR(x) (V6(x)->sin6_addr)
#define V6PORT(x) (V6(x)->sin6_port)
#endif

int     inet_strton             (const char *, const char *, SSu *, int);
int     inet_ntostr             (SSu *, char *, int, char *, int, int);
char *  inet_ssu_to_paddr	(SSu *name, int flags);
int	inet_hntop             	(int, const char *, char *, int);
int	inet_ptohn             	(int, const char *, char *, int);
int	one_to_another         	(int, const char *, char *, int);
int     my_accept              	(int, SSu *, socklen_t *);
char *	switch_hostname        	(const char *);
int     ip_bindery              (int family, unsigned short port, SSu *storage);
int     client_bind             (SSu *, socklen_t);
int     client_connect          (SSu *, socklen_t, SSu *, socklen_t);
int     inet_vhostsockaddr 	(int, int, const char *, SSu *, socklen_t *);
int	my_getaddrinfo		(const char *, const char *, const AI *, AI **);
void	my_freeaddrinfo		(AI *);
pid_t	async_getaddrinfo	(const char *, const char *, const AI *, int);
void	marshall_getaddrinfo	(int, AI *results);
void	unmarshall_getaddrinfo	(AI *results);
int	set_non_blocking	(int);
int	set_blocking		(int);
int	family			(SSu *);

#define GNI_INTEGER 0x4000

#endif
