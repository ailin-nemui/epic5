/*
 * funny.h: header for funny.c
 *
 * Copyright 1990 Michael Sandrof
 * Copyright 1994 Matthew Green
 * Copyright 1997, 2003 EPIC Software Labs
 * see the copyright file, or do a help ircii copyright 
 */

#ifndef __funny_h__
#define __funny_h__

#define FUNNY_PUBLIC		1 << 0
#define FUNNY_PRIVATE		1 << 1
#define FUNNY_TOPIC		1 << 2
#define FUNNY_USERS		1 << 4
#define FUNNY_NAME		1 << 5

	void	set_funny_flags 	(int, int, int, const char *);

	void	list_reply 		(const char *, const char *, char **);
	void	mode_reply 		(const char *, const char *, char **);
	void	names_reply 		(const char *, const char *, char **);

#endif /* _FUNNY_H_ */
