/*
 * lastlog.h: header for lastlog.c 
 *
 * Copyright 1990 Michael Sandrof
 * Copyright 1997 EPIC Software Labs
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 */

#ifndef __lastlog_h__
#define __lastlog_h__

#include "levels.h"

typedef struct	lastlog_stru
{
	Mask	level;
	char	*target;
	char	*msg;
	struct	lastlog_stru	*older;
	struct	lastlog_stru	*newer;
}	Lastlog;

/* 
 * Window and Lastlog are mutually referential.  So we cant include
 * window.h here. so to break the loop we forward declare Window here.
 */
struct WindowStru;

extern	Mask	current_window_mask;
extern	Mask	new_server_lastlog_mask;

	BUILT_IN_COMMAND(lastlog);

	Mask	real_lastlog_mask 		(void);
	Mask	real_notify_mask 		(void);
	void	set_lastlog_mask 		(const void *);
	void	set_lastlog_size 		(const void *);
	void	set_notify_mask 		(const void *);
	char *	mask_to_str 			(Mask);
	Mask	str_to_mask	 		(const char *);
	int	str_to_level			(const char *);
	void	remove_from_lastlog		(struct WindowStru *);
	void	set_current_window_mask 	(const void *);
	void	add_to_lastlog 		(struct WindowStru *, const char *);
	char *	function_line			(char *);
	char *	function_lastlog		(char *);
	void	set_new_server_lastlog_mask	(const void *);

#endif /* __lastlog_h_ */
