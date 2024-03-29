/*
 * window.h: header file for window.c 
 *
 * Copyright 1990 Michael Sandrof
 * Copyright 1997 EPIC Software Labs
 *
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 */

#ifndef __window_h__
#define __window_h__

/* To get the definition of Mask */
#include "levels.h"

/* To get the definition of Status */
#include "status.h"

/* To get the definition of List */
#include "list.h"

/* Should be a way to make static to window.c */
typedef	struct	DisplayStru
{
struct	DisplayStru *	prev;
struct	DisplayStru *	next;

	size_t		count;
	char *		line;
	intmax_t	linked_refnum;
	ssize_t		unique_refnum;
	time_t		when;
}	Display;

/*
 * WindowStack: The structure for the POP, PUSH, and STACK functions. A
 * simple linked list with window refnums as the data 
 */
typedef	struct	window_stack_stru
{
struct	window_stack_stru *	next;
	int			refnum;
}	WindowStack;

extern	unsigned 	current_window_priority;

	BUILT_IN_COMMAND(windowcmd);
	int		new_window 			(int);
	int		window_is_holding		(int);
	int		unhold_a_window			(int);
	int		window_is_scrolled_back		(int);
	int		trim_scrollback			(int);
	int		add_to_scrollback		(int, const char *, intmax_t);

	void		add_to_invisible_list		(int);
	void		delete_all_windows		(void);
	int     	traverse_all_windows2 		(int *);
	void		window_statusbar_needs_update	(int);
	void		redraw_all_windows		(void);
	void		recalculate_windows		(int);
	void		update_all_windows		(void);

	BUILT_IN_KEYBINDING(swap_last_window);
	BUILT_IN_KEYBINDING(next_window);
	BUILT_IN_KEYBINDING(swap_next_window);
	BUILT_IN_KEYBINDING(previous_window);
	BUILT_IN_KEYBINDING(swap_previous_window);
	BUILT_IN_KEYBINDING(update_all_status_kb);
	BUILT_IN_KEYBINDING(switch_query);

	char *		get_window_status_line		(int, int);
	void		update_all_status		(void);
	Char *		get_window_target		(int);
	Char *		get_window_equery		(int);
	int		is_current_channel		(const char *, int);
	Char *		get_window_echannel		(int);
	void		destroy_waiting_channels	(int);
	int     	claim_waiting_channel 		(const char *chan, int servref);
	int		get_window_server		(int);
	int		set_window_server		(int, int);
	void		change_window_server		(int, int);
	void		window_check_servers		(void);
	int		renormalize_window_levels	(int, Mask);
#define message_from(x, y) real_message_from(x, y, __FILE__, __LINE__)
	int		real_message_from		(const char *, int, const char *, int);
#define message_setall(x, y, z) real_message_setall(x, y, z, __FILE__, __LINE__)
	int     	real_message_setall		(int , const char *, int, const char *, int);
	void		pop_message_from		(int);
	Char *		get_who_from			(void);
	int		get_who_level			(void);
	Char *		get_who_file			(void);
	int		get_who_line			(void);
	int		get_to_window			(void);

	void		clear_all_windows		(int, int);
	void		clear_window_by_refnum		(int);
	void		unclear_all_windows		(int, int, int);
	void		unclear_window_by_refnum	(int, int);
	void		set_scrollback_size		(void *);
	void		set_scroll_lines		(void *);
	void		set_continued_line		(void *);

	BUILT_IN_KEYBINDING(scrollback_backwards);
	BUILT_IN_KEYBINDING(scrollback_forwards);
	BUILT_IN_KEYBINDING(scrollback_end);
	BUILT_IN_KEYBINDING(scrollback_start);
	BUILT_IN_KEYBINDING(unstop_all_windows);
	BUILT_IN_KEYBINDING(toggle_stop_screen);

	void		make_window_current_by_refnum		(int);
	int		make_window_current_informally		(int);
	int		windowcmd_query				(int, char **);
	int		windowcmd_rejoin			(int, char **);
	void		window_check_channels			(void);

	char *		windowctl				(char *);
	void    	window_scrollback_needs_rebuild 	(int);
	void		check_message_from_queue 		(int);

	/* * * * */
	int		clear_window_lastlog_mask		(int);
	int     	lookup_window 				(const char *);
	int		lookup_any_visible_window		(void);
	int		lookup_window_by_server			(int);

	int     	get_server_current_channel		(int);
	int     	get_server_current_window		(int);

	int		get_window_bottom			(int);
	int		get_window_current_activity		(int);
	Char *		get_window_current_activity_data 	(int);
	Char *		get_window_current_activity_format	(int);
	int     	get_window_cursor 			(int);
	int     	get_window_display_buffer_size 		(int);
	Display *	get_window_display_ip 			(int);
	int		get_window_display_lines		(int);
	int		get_window_fixed_size			(int);
	int     	get_window_geometry 			(int, int *, int *);
	int     	get_window_hold_interval 		(int);
	int		get_window_hold_mode			(int);
	int     	get_window_holding_distance_from_display_ip 	(int);
	Display *	get_window_holding_top_of_display 	(int);
	int		get_window_killable			(int);
	int		get_window_lastlog_mask			(int, Mask *);
	int		get_window_lastlog_max			(int);
	int		get_window_lastlog_size			(int);
	int    		get_window_indent 			(int);
	FILE *		get_window_log_fp			(int);
	int		get_window_mask				(int, Mask *);
	int		get_window_my_columns 			(int);
	Char *		get_window_name 			(int);
	List *		get_window_nicks			(int);
	int		get_window_notified			(int);
	Char *		get_window_notify_name 			(int);
	unsigned 	get_window_priority			(int);
	int		get_window_refnum			(int);
	int		get_window_screennum			(int);
	int     	get_window_scrollback_distance_from_display_ip 	(int);
	Display *	get_window_scrollback_top_of_display 		(int);
	int     	get_window_scrollback_top_of_display_exists 	(int);
	Display *	get_window_scrolling_top_of_display 		(int);
	int     	get_window_scrolling_distance_from_display_ip 	(int);
	int		get_window_skip				(int);
	Status *	get_window_status 			(int);
	int     	get_window_swappable 			(int);
	int		get_window_top 				(int);
	Char *		get_window_topline 			(int, int);
	int		get_window_toplines_showing 		(int);
	int		get_window_user_refnum			(int);
	Char *		get_window_uuid				(int);
	int		get_window_scroll_lines			(int);
	Char *		get_window_log_rewrite 			(int);
	int		get_window_log_mangle 			(int);
	int     	get_window_beep_always 			(int);
	Mask *  	get_window_notify_mask 			(int);
	int     	get_window_notify_when_hidden 		(int);

	int		set_window_change_line			(int, int);
	int		set_window_cursor 			(int, int);
	int		set_window_cursor_decr 			(int);
	int		set_window_cursor_incr 			(int);
	void		set_window_display_lines		(int, int);
	int		set_window_indent			(int, int);
	int		set_window_killable			(int, int);
	int		set_window_lastlog_mask			(int, Mask);
	int     	set_window_lastlog_max         		(int, int);
	int		set_window_lastlog_size_incr		(int);
	int		set_window_lastlog_size_decr		(int);
	int		set_window_my_columns 			(int, int);
	int		set_window_notified			(int, int);
	int		set_window_notify_mask			(int, Mask);
	int		set_window_priority			(int, int);
	int		set_window_scroll_lines			(int, int);
	void		set_window_log_rewrite 			(int, const char *);
	void		set_window_log_mangle 			(int, int);
	void		set_window_swappable 			(int, int);
	int		set_window_screennum			(int, int);

	int		get_window_prev				(int);
	int		get_window_next				(int);
	int		set_window_prev				(int, int);
	int		set_window_next				(int, int);

	int		window_is_valid				(int);


#endif /* __window_h__ */


