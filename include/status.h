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
        const char *	(*func[MAX_FUNCTIONS]) (struct WindowStru *, int, int);
	int		map[MAX_FUNCTIONS];
	int		key[MAX_FUNCTIONS];
        int   		count;
        char *		result;
} Status_line;

typedef struct  status_stuff {          
        Status_line     line[3];
        int             double_status;
        char            *special;
} Status;

extern	Status	main_status;

	char *	convert_sub_format (const char *, char);
	void	make_status 	(struct WindowStru *, int);	/* Don't call */
	char	*update_clock 	(int);
	void	reset_clock 	(char *);
	void	build_status 	(char *);
	int	permit_status_update	(int);
	void	rebuild_a_status (struct WindowStru *);		/* Don't call */

#define GET_TIME 	1
#define RESET_TIME 	2

#endif /* _STATUS_H_ */
