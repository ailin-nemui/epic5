/*
 * notice.h -- header file for notice.c
 *
 * Copyright 1990, 1995 Michael Sandrof, Matthew Green
 * Copyright 1997 EPIC Software Labs
 */

#ifndef __notice_h__
#define __notice_h__

	void 	parse_notice 		(char *, char **);
	void 	got_initial_version_28 	(char **);
	void	load_ircrc		(void);

extern	int 	doing_notice;

#endif
