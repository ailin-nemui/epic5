/*
 * ssl.h -- header file for ssl.c
 *
 * Original framework written by Juraj Bednar
 * Modified by B. Thomas Frazier
 *
 * Copyright 2000, 2002 EPIC Software Labs
 *
 */

#ifndef __ssl_h__
#define __ssl_h__

	int	startup_ssl (int nfd, int channel);
	int	shutdown_ssl (int nfd);
	int	write_ssl (int nfd, const void *, size_t);
	int	ssl_read (int nfd);
	const char *get_ssl_cipher (int nfd);
	int	is_ssl_enabled (int nfd);

#endif
