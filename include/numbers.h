/*
 * numbers.h: header for numbers.c
 *
 * Copyright 1990 Michael Sandrof
 * Copyright 1997 EPIC Software Labs
 * see the copyright file, or do a help ircii copyright 
 */

#ifndef __numbers_h__
#define __numbers_h__

	char	*numeric_banner 	(void);
	void	display_msg 		(const char *, char **);
	void	numbered_command 	(const char *, const char *, char **);
	void	nickname_sendline 	(char *, char *);

#endif /* _NUMBERS_H_ */
