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
        const char *	(*func[MAX_FUNCTIONS]) (int, short, char);
	short		map[MAX_FUNCTIONS];
	char		key[MAX_FUNCTIONS];
        int   		count;
        char *		result;
} Status_line;

typedef struct  status_stuff {          
        Status_line     line[3];
        short           double_status;
        short           number;
        char *		special;
        char *		prefix_when_current;
        char *		prefix_when_not_current;
} Status;

extern	Status	main_status;

	char *	convert_sub_format (const char *, char);
	void	compile_status 	(int, struct status_stuff *);
	int	make_status 	(struct WindowStru *, struct status_stuff *);
	int	redraw_status	(struct WindowStru *, struct status_stuff *);
	void	build_status 	(void *);
	int	permit_status_update	(int);
	char *	function_status_oneoff	(char *);

#endif /* _STATUS_H_ */
