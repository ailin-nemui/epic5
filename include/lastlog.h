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

extern	Mask	current_window_mask;
extern	Mask *	new_server_lastlog_mask;
extern	Mask *	old_server_lastlog_mask;
extern	double	output_expires_after;

	BUILT_IN_COMMAND(lastlog);

	Mask	real_lastlog_mask 		(void);
	Mask	real_notify_mask 		(void);
	void	set_lastlog_mask 		(void *);
	void	set_lastlog_size 		(void *);
	void	set_notify_mask 		(void *);
	int	recount_window_lastlog		(int);
	void	trim_lastlog			(int);
	void	dont_need_lastlog_item		(int, intmax_t);
	void	set_current_window_mask 	(void *);
	intmax_t add_to_lastlog 		(int, const char *);
	char *	function_line			(char *);
	char *	function_lastlog		(char *);
	void	set_new_server_lastlog_mask	(void *);
	void	set_old_server_lastlog_mask	(void *);
	void	reconstitute_scrollback		(int);
	int	do_expire_lastlog_entries	(void *);
	void	truncate_lastlog		(int);

	void	move_all_lastlog		(int, int);
	void	move_lastlog_item_by_string	(int, int, Char *);
	void	move_lastlog_item_by_target	(int, int, Char *);
	void	move_lastlog_item_by_level	(int, int, Mask *);
	void	move_lastlog_item_by_regex	(int, int, Char *);

	void    clear_level_from_lastlog 	(int, Mask *);
	void    clear_regex_from_lastlog 	(int, const char *);

#endif /* __lastlog_h_ */
