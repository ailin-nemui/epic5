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

extern int
	CTCP_ACTION,
	CTCP_DCC,
	CTCP_VERSION,
	CTCP_AESSHA256,
	CTCP_AES256,
	CTCP_CAST5,
	CTCP_BLOWFISH,
	CTCP_FISH,
	CTCP_SED,
	CTCP_SEDSHA,
	CTCP_PING,
	CTCP_ECHO,
	CTCP_UTC,
	CTCP_CLIENTINFO	,
	CTCP_USERINFO,
	CTCP_ERRMSG,
	CTCP_FINGER,
	CTCP_TIME,
	CTCP_CUSTOM;
/* NUMBER_OF_CTCPS		CTCP_CUSTOM */

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
	int     init_ctcp 	(void);

#endif /* _CTCP_H_ */
