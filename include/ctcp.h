/*
 * ctcp.h: header file for ctcp.c
 *
 * Copyright 1990 Michael Sandrof
 * Copyright 1997 EPIC Software Labs
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 */

#ifndef __ctcp_h__
#define __ctcp_h__

#define CTCP_PRIVMSG 		0
#define CTCP_NOTICE 		1

#define CTCP_SED		0
#define CTCP_UTC		1
#define CTCP_ACTION		2
#define CTCP_DCC		3
#define CTCP_VERSION		4
#define CTCP_CLIENTINFO		5
#define CTCP_USERINFO		6
#define CTCP_ERRMSG		7
#define CTCP_FINGER		8
#define CTCP_TIME		9
#define CTCP_PING		10
#define	CTCP_ECHO		11
#define CTCP_CUSTOM		12
#define NUMBER_OF_CTCPS		CTCP_CUSTOM

extern	int	sed;
extern	int	in_ctcp_flag;

	char *	do_ctcp 	(const char *, const char *, char *);
	char *	do_notice_ctcp 	(const char *, const char *, char *);
	int	in_ctcp 	(void);
	void	send_ctcp 	(int, const char *, int, const char *, ...) /*__A(4)*/;
	int	get_ctcp_val 	(char *);

#endif /* _CTCP_H_ */
