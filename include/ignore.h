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
#define IGNORE_MSGS	1 << 0
#define IGNORE_PUBLIC	1 << 1
#define IGNORE_WALLS	1 << 2
#define IGNORE_WALLOPS	1 << 3
#define IGNORE_INVITES	1 << 4
#define IGNORE_NOTICES	1 << 5
#define IGNORE_NOTES	1 << 6
#define IGNORE_CTCPS	1 << 7
#define IGNORE_TOPICS   1 << 8
#define IGNORE_NICKS    1 << 9
#define IGNORE_JOINS    1 << 10
#define IGNORE_PARTS	1 << 11
#define IGNORE_CRAP	1 << 12
#define IGNORE_ALL 	(IGNORE_MSGS | IGNORE_PUBLIC | IGNORE_WALLS | \
			 IGNORE_WALLOPS | IGNORE_INVITES | IGNORE_NOTICES | \
			 IGNORE_NOTES | IGNORE_CTCPS | IGNORE_TOPICS | \
			 IGNORE_NICKS | IGNORE_JOINS | IGNORE_PARTS | \
			 IGNORE_CRAP)

#define IGNORED 	1
#define DONT_IGNORE 	2
#define HIGHLIGHTED 	-1

extern	int	ignore_usernames;
extern	char	*highlight_char;

	BUILT_IN_COMMAND(ignore);
	int	check_ignore 		(const char *, const char *, int);
	int	check_ignore_channel 	(const char *, const char *, const char *, int);
	char *	get_ignores_by_pattern	    (char *, int);
	char *	get_ignore_types_by_pattern  (char *);
	char *	get_ignore_patterns_by_type (char *);

#endif /* _IGNORE_H_ */
