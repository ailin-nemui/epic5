/*
 * history.h: header for history.c 
 *
 * Copyright 1990 Michael Sandrof
 * Copyright 1997 EPIC Software Labs
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 */

#ifndef __history_h__
#define __history_h__

	BUILT_IN_COMMAND(history);
	void	set_history_size 	(const void *);
	void	add_to_history 		(char *);
	void	get_history 		(int);
	char *	do_history 		(char *, char *);
	void	shove_to_history 	(char, char *);
	void    abort_history_browsing	(int);

/* used by get_history */
#define NEWER	0
#define OLDER	1

#endif /* _HISTORY_H_ */
