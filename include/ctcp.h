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

#define CTCP_ACTION		0
#define CTCP_DCC		1
#define CTCP_VERSION		2
#define CTCP_AESSHA256		3
#define CTCP_AES256		4
#define CTCP_CAST5		5
#define CTCP_BLOWFISH		6
#define CTCP_SED		7
#define CTCP_SEDSHA		8
#define CTCP_PING		9
#define	CTCP_ECHO		10
#define CTCP_UTC		11
#define CTCP_CLIENTINFO		12
#define CTCP_USERINFO		13
#define CTCP_ERRMSG		14
#define CTCP_FINGER		15
#define CTCP_TIME		16
#define CTCP_CUSTOM		17
#define NUMBER_OF_CTCPS		CTCP_CUSTOM

#define CTCP_DELIM_CHAR         '\001' 
#define CTCP_DELIM_STR          "\001" 
#define CTCP_QUOTE_CHAR         '\\'
#define CTCP_QUOTE_STR          "\\"
#define CTCP_QUOTE_EM           "\r\n\001\\"

extern	int	sed;
extern	int	in_ctcp_flag;

	char *	do_ctcp 	(const char *, const char *, char *);
	char *	do_notice_ctcp 	(const char *, const char *, char *);
	int	in_ctcp 	(void);
	void	send_ctcp 	(int, const char *, int, const char *, ...) /*__A(4)*/;
	int	get_ctcp_val 	(char *);

#endif /* _CTCP_H_ */
