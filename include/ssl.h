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

	int	startup_ssl (int fd);
	int	shutdown_ssl (int fd);
	int	write_ssl (int fd, const void *, size_t);
	int	ssl_reader (int fd, char **, size_t *, size_t *);
	const char *get_ssl_cipher (int);
	int	is_ssl_enabled (int);

#endif
