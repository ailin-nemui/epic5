/*
 * funny.h: header for funny.c
 *
 * Copyright 1990 Michael Sandrof
 * Copyright 1994 Matthew Green
 * Copyright 1997 EPIC Software Labs
 * see the copyright file, or do a help ircii copyright 
 */

#ifndef __funny_h__
#define __funny_h__

#define FUNNY_PUBLIC		1 << 0
#define FUNNY_PRIVATE		1 << 1
#define FUNNY_TOPIC		1 << 2
#define FUNNY_WIDE		1 << 3
#define FUNNY_USERS		1 << 4
#define FUNNY_NAME		1 << 5

	int	funny_is_ignore_channel 	(void);
	void	funny_list 			(char *, char **);
	void	funny_match 			(char *);
	void	funny_mode 			(char *, char **);
	void	funny_namreply 			(char *, char **);
	void	funny_print_widelist 		(void);
	void	funny_set_ignore_channel 	(char *);
	void	funny_set_ignore_mode 		(void);
	void	reinstate_user_modes 		(void);
	void	set_funny_flags 		(int, int, int);
	void	update_user_mode 		(char *);

#endif /* _FUNNY_H_ */
