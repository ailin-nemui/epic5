/*
 * lastlog.h: header for lastlog.c 
 *
 * Copyright 1990 Michael Sandrof
 * Copyright 1997 EPIC Software Labs
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 */

#ifndef __lastlog_h__
#define __lastlog_h__

typedef struct	lastlog_stru
{
	int	level;
	char	*target;
	char	*msg;
	struct	lastlog_stru	*older;
	struct	lastlog_stru	*newer;
}	Lastlog;

#define LOG_NONE	0x000000
#define LOG_CURRENT	0x000000
#define LOG_CRAP	0x000001
#define LOG_PUBLIC	0x000002
#define LOG_MSG		0x000004
#define LOG_NOTICE	0x000008
#define LOG_WALL	0x000010
#define LOG_WALLOP	0x000020
#define LOG_NOTES	0x000040
#define LOG_OPNOTE	0x000080
#define	LOG_SNOTE	0x000100
#define	LOG_ACTION	0x000200
#define	LOG_DCC		0x000400
#define LOG_CTCP	0x000800
#define	LOG_USER1	0x001000
#define LOG_USER2	0x002000
#define LOG_USER3	0x004000
#define LOG_USER4	0x008000
#define LOG_HELP	0x010000
/* LOG_HELP is reserved */

/* Idea to have LOG_ALL include LOG_USER? from Genesis K. */
#define LOG_ALL (LOG_CRAP | LOG_PUBLIC | LOG_MSG | LOG_NOTICE | LOG_WALL | \
		LOG_WALLOP | LOG_NOTES | LOG_OPNOTE | LOG_SNOTE | LOG_ACTION | \
		LOG_CTCP | LOG_DCC | LOG_USER1 | LOG_USER2 | LOG_USER3 | LOG_USER4)

/* 
 * Window and Lastlog are mutually referential.  So we cant include
 * window.h here. so to break the loop we forward declare Window here.
 */
struct WindowStru;

extern	int	beep_on_level;		/* XXXX */
extern	int	current_window_level;
extern	int	new_server_lastlog_level;

	BUILT_IN_COMMAND(lastlog);

	void	set_lastlog_level 		(char *);
	int	set_lastlog_msg_level 		(int);
	void	set_lastlog_size 		(int);
	void	set_notify_level 		(char *);
	void	add_to_lastlog 		(struct WindowStru *, const char *);
	char	*bits_to_lastlog_level 		(int);
	int	real_lastlog_level 		(void);
	int	real_notify_level 		(void);
	int	parse_lastlog_level 		(char *);
	void	set_beep_on_msg			(char *);
	void	remove_from_lastlog		(struct WindowStru *);
	void	set_current_window_level 	(char *);
	char	*function_line			(char *);
	char	*function_lastlog		(char *);
	void	set_new_server_lastlog_level	(char *);

#endif /* __lastlog_h_ */
