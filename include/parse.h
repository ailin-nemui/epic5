/*
 * parse.h
 *
 * Copyright 1993 Matthew Green
 * Copyright 1997 EPIC Software Labs
 * See the Copyright file for license information.
 */

#ifndef __parse_h__
#define __parse_h__

	void    rfc1459_odd 	(const char *, const char *, const char **);
const 	char	*PasteArgs 	(const char **, int);
	void	parse_server 	(const char *, size_t);
	int	is_channel	(const char *);
	void    rfc1459_any_to_utf8 (char *, size_t, char **);

extern	const char	*FromUserHost;
extern	const char	*Tags;

#endif

