/*
 * notify.h: header for notify.c
 *
 * Copyright 1990 Michael Sandrof
 * Copyright 1997 EPIC Software Labs
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 */

#ifndef __notify_h__
#define __notify_h__

#include "alist.h"
struct notify_stru;

extern	char	notify_timeref[];

	BUILT_IN_COMMAND(notify);
	void	do_notify 		(void);
	void	notify_mark 		(int, const char *, int, int);
	void	save_notify 		(FILE *);
	void	set_notify_handler 	(char *);
	void	make_notify_list 	(int);
	char *	get_notify_nicks 	(int, int);
	void	destroy_notify_list	(int);

	void	notify_systimer		(void);
	void	set_notify_interval	(void *);
	void	set_notify		(void *);

#endif /* _NOTIFY_H_ */
