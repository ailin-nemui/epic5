/*
 * status.h: header for status.c
 *
 * Copyright 1990 Michael Sandrof
 * Copyright 1997 EPIC Software Labs
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 */

#ifndef __status_h__
#define __status_h__

struct 	WindowStru;

#define MAX_FUNCTIONS 40

typedef struct  status_line {
        char *		raw;
        char *		format;
        const char *	(*func[MAX_FUNCTIONS]) (struct WindowStru *, short, char);
	short		map[MAX_FUNCTIONS];
	char		key[MAX_FUNCTIONS];
        int   		count;
        char *		result;
} Status_line;

typedef struct  status_stuff {          
        Status_line     line[3];
        short           double_status;
        char            *special;
} Status;

extern	Status	main_status;

	char *	convert_sub_format (const char *, char);
	int	make_status 	(struct WindowStru *, int);	/* Don't call */
	void	build_status 	(void *);
	int	permit_status_update	(int);
	void	rebuild_a_status (struct WindowStru *);		/* Don't call */

#endif /* _STATUS_H_ */
