/*
 * ignore.h: header for ignore.c 
 *
 * Copyright 1990 Michael Sandrof
 * Copyright 1997 EPIC Software Labs
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 */

#ifndef __ignore_h__
#define __ignore_h__

/* Type of ignored nicks */
#include "levels.h"

#define	NOT_IGNORED	0
#define IGNORED 	1
#define HIGHLIGHTED 	-1

	BUILT_IN_COMMAND(ignore);
	int	do_expire_ignores	(void *);
	int	check_ignore 		(const char *, const char *, int);
	int	check_ignore_channel 	(const char *, const char *, const char *, int);
	char *	get_ignores_by_pattern	    (char *, int);
	const char *	get_ignore_types_by_pattern  (char *);
	char *	get_ignore_patterns_by_type (char *);
	char *	ignorectl		(char *);

#endif /* _IGNORE_H_ */
