/*
 * numbers.h: header for numbers.c
 *
 * Copyright 1990 Michael Sandrof
 * Copyright 1997 EPIC Software Labs
 * see the copyright file, or do a help ircii copyright 
 */

#ifndef __numbers_h__
#define __numbers_h__

const	char *	banner 			(void);
	void	display_msg 		(const char *, const char **);
	void	numbered_command 	(const char *, const char *, const char **);
	void	nickname_sendline 	(char *, char *);

#endif /* _NUMBERS_H_ */
