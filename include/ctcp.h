/*
 * ctcp.h: header file for ctcp.c
 *
 * Copyright 1990 Michael Sandrof
 * Copyright 1997 EPIC Software Labs
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 */

#ifndef __ctcp_h__
#define __ctcp_h__

#define CTCP_DELIM_CHAR         '\001' 
#define CTCP_DELIM_STR          "\001" 
#define CTCP_QUOTE_CHAR         '\\'
#define CTCP_QUOTE_STR          "\\"
#define CTCP_QUOTE_EM           "\r\n\001\\"

extern	int	sed;
extern	int	in_ctcp_flag;

	char *	do_ctcp 	(int, const char *, const char *, char *);
	void	send_ctcp 	(int, const char *, const char *, const char *, ...) /*__A(4)*/;
	int     init_ctcp 	(void);

#endif /* _CTCP_H_ */
