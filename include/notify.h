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

typedef struct notify_alist
{
	struct notify_stru **	list;
	int			max;
	int			max_alloc;
	alist_func 		func;
	hash_type		hash;
	char *			ison;
} NotifyList;

	BUILT_IN_COMMAND(notify);
	void	do_notify 		(void);
	void	notify_mark 		(int, const char *, int, int);
	void	save_notify 		(FILE *);
	void	set_notify_handler 	(char *);
	void	make_notify_list 	(int);
	char *	get_notify_nicks 	(int, int);
	void	destroy_notify_list	(int);

#endif /* _NOTIFY_H_ */
