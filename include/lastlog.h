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

/* 
 * Window and Lastlog are mutually referential.  So we cant include
 * window.h here. so to break the loop we forward declare Window here.
 */
struct WindowStru;
struct lastlog_stru;

extern	Mask	current_window_mask;
extern	Mask	new_server_lastlog_mask;
extern	Mask	old_server_lastlog_mask;

	BUILT_IN_COMMAND(lastlog);

	Mask	real_lastlog_mask 		(void);
	Mask	real_notify_mask 		(void);
	void	set_lastlog_mask 		(void *);
	void	set_lastlog_size 		(void *);
	void	set_notify_mask 		(void *);
	const char *	level_to_str 		(int);
	char *	mask_to_str 			(const Mask *);
	int	str_to_mask	 		(Mask *, const char *);
	int	str_to_level			(const char *);
	void	remove_from_lastlog		(struct WindowStru *);
	void	set_current_window_mask 	(void *);
	void	add_to_lastlog 		(struct WindowStru *, const char *);
	char *	function_line			(char *);
	char *	function_lastlog		(char *);
	void	set_new_server_lastlog_mask	(void *);
	void	set_old_server_lastlog_mask	(void *);

#endif /* __lastlog_h_ */
