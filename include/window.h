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

/* To get the definition of Lastlog */
#include "lastlog.h"

/* To get the definition of Status */
#include "status.h"

/* To get the definition of List */
#include "list.h"

/* 
 * Screen and Window are mutually referencial.
 * That means we cant include "screen.h", so we kind of fake it.
 */
struct	ScreenStru;

/* var_settings indexes */
#define OFF 			0
#define ON 			1
#define TOGGLE 			2

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

typedef struct	WNickListStru
{
struct WNickListStru *	next;
	char *		nick;
	int		counter;
} 	MAY_ALIAS WNickList;


#define NEED_WINDOWSTRU
#ifdef NEED_WINDOWSTRU
typedef	struct	WindowStru
{
	/* List stuff */
struct	WindowStru *	_next;			/* Window below us on screen */
struct	WindowStru *	_prev;			/* Window above us on screen */
struct	ScreenStru *	screen;			/* The screen we belong to */
	short		deceased;		/* Set when the window is killed */

	unsigned 	refnum;			/* Unique refnum for window */
	unsigned 	user_refnum;		/* Sequencing number used by the user */
	char *		name;			/* Logical name for window */
	char *		uuid;			/* UUID4 for window (never changes) */
	unsigned 	priority;		/* "Current window Priority" */

	/* Output rule stuff */
	int		server;			/* Server that win is connected to */
	Mask		window_mask;		/* Window level for the window */
	WNickList *	waiting_chans;		/*
					 	 * When you JOIN or reconnect, if this
					 	 * is set, a JOIN to that channel will
					 	 * put that channel into this win.
					 	 */
	WNickList *	nicks;			/* List of nick-queries for this win */
	int		query_counter;		/* Is there a query anyways? */
	char *		claimed_channel;	/* A /WINDOW CLAIM claim */

	/* Internal flags */
	short		top;			/* SCREEN line for top of window */
	short		bottom;			/* SCREEN line for bottom of window */
	short		cursor;			/* WINDOW line where the cursor is */
	short		change_line;		/* True if this is a scratch window */
	short		update;			/* True if window display is dirty */
	short		rebuild_scrollback;	/* True if scrollback needs rebuild */

	/* User-settable flags */
	short		notify_when_hidden;	/* True to notify for hidden output */
	short		notified;		/* True if we have notified */
	char *		notify_name;		/* The name for %{1}F */
	short		beep_always;		/* True if a beep to win always beeps */
	Mask		notify_mask;		/* the notify mask.. */
	short		skip;			/* Whether window should be skipped */
	short		old_co;			/* .... */
	short		my_columns;		/* How wide we are when hidden */
	short		indent;			/* How far /set indent goes */
	short		swappable;		/* Can it be swapped in or out? */
	short		scrolladj;		/* Push back top-of-win on grow? */
	short		killable;		/* Can it be killed? */
	short		scroll_lines;		/* How many lines scroll at a time? */
	char *		original_server_string;

	/* Input and Status stuff */
	char *		prompt;			/* Current EXEC prompt for window */
	Status		status;			/* Current status line info */

	/* SCROLLBACK stuff */
	/*
	 * The "scrollback" buffer is a linked list of lines that have
	 * appeared, are appearing, or will appear on the visible window.
	 * The "top" of the scrollback buffer usually floats forward in
	 * the line as items are added.  The visible part of the screen is
	 * always somewhere in the buffer (usually at the bottom), unless
	 * the user is in "scrollback mode" or "hold mode".  When the user
	 * is in either of these modes, then the top of the scrollback buffer
	 * is frozen (so as not to disrupt the current contents of the screen)
	 * and the bottom of the buffer floats as is neccesary.  When the user
	 * ends these modes, the buffer is reset to the "scrollback_point" and
	 * then is floated to its original dimensions from there.  The lastlog
	 * has nothing to do with scrollback any longer.
	 *
	 * The "display_ip" is always a blank line where the NEXT displayable
	 * line will go.  When the user does a /clear, the screen is scrolled
	 * up until display_ip is the top_of_display.  
	 */
	Display *	top_of_scrollback;	/* Start of the scrollback buffer */
	Display *	display_ip;		/* End of the scrollback buffer */
	int		display_buffer_size;	/* How big the scrollback buffer is */
	int		display_buffer_max;	/* How big its supposed to be */

	Display *	scrolling_top_of_display;
	int		scrolling_distance_from_display_ip;

	Display *	holding_top_of_display;
	int		holding_distance_from_display_ip;

	Display *	scrollback_top_of_display;
	int		scrollback_distance_from_display_ip;

	int		display_counter;
	short		hold_slider;

	Display *	scrollback_indicator;	/* The === thing */

	/*
	 * Window geometry stuff
	 *
	 * The scrollable part of the window starts at the "top" value and 
	 * continues on for "display_lines" lines.  The scrollable part does
	 * not include the toplines, and does not include the status bar(s).
	 * Each window also has a shadow "logical size" which is a unitless
	 * number used to calculate the relative size of each window.  When
	 * the screen size changes, we subtract all of the non-negotiable 
	 * parts (toplines, status bars, fixed windows) and then distribute
	 * what is left to each window.
	 */
	short		display_lines;		/* How many lines window size is */
	short		logical_size;		/* How many units window size is */
	short		fixed_size;		/* True if window doesnt rebalance */
	short		old_display_lines;	/* How big window was on last resize */

	/* HOLD_MODE stuff */
	short		hold_interval;		/* How often to update status bar */

	/* /LASTLOG stuff */
	Mask		lastlog_mask;		/* The LASTLOG_LEVEL, determines what
						 * messages go to lastlog */
	int		lastlog_size;		/* number of messages in lastlog. */
	int		lastlog_max;		/* Max number of messages in lastlog */

	/* /WINDOW LOG stuff */
	short		log;			/* True if file logging is on */
	char *		logfile;		/* window's logfile name */
	FILE *		log_fp;			/* file pointer for the log file */
	char *		log_rewrite;		/* Overrules /set log_rewrite */
	int		log_mangle;		/* Overrules /set mangle_logfiles */
	char *		log_mangle_str;		/* String version of log_mangle */

	/* TOPLINES stuff */
	short		toplines_wanted;
	short		toplines_showing;
	char *		topline[10];

	/* ACTIVITY stuff */
	short		current_activity;
	char *		activity_data[11];
	char *		activity_format[11];

}	Window;

/*
 * WindowStack: The structure for the POP, PUSH, and STACK functions. A
 * simple linked list with window refnums as the data 
 */
typedef	struct	window_stack_stru
{
struct	window_stack_stru *	next;
	int			refnum;
}	MAY_ALIAS WindowStack;


	int	traverse_all_windows		(Window **);
#endif

	int	new_window 			(struct ScreenStru *);
	Window *get_window_by_refnum_direct	(int refnum);

extern	unsigned 	current_window_priority;

	BUILT_IN_COMMAND(windowcmd);
	int	window_is_holding		(int);
	int	unhold_a_window			(int);
	int	window_is_scrolled_back		(int);
	int	trim_scrollback			(int);
	int	add_to_scrollback		(int, const unsigned char *, intmax_t);

	void	add_to_invisible_list		(int);
	void	delete_all_windows		(void);
	int     traverse_all_windows2 		(int *refnum);
	void	window_statusbar_needs_update	(int);
	void	redraw_all_windows		(void);
	void	recalculate_windows		(struct ScreenStru *);
	void	update_all_windows		(void);
	BUILT_IN_KEYBINDING(swap_last_window);
	BUILT_IN_KEYBINDING(next_window);
	BUILT_IN_KEYBINDING(swap_next_window);
	BUILT_IN_KEYBINDING(previous_window);
	BUILT_IN_KEYBINDING(swap_previous_window);
	char *	get_window_status_line		(int, int);
	void	update_all_status		(void);
#if 0
	void	set_window_prompt		(int, const char *);
	Char *	get_window_prompt		(int);
#endif
	Char *	get_window_target		(int);
	Char *	get_window_equery		(int);
	BUILT_IN_KEYBINDING(switch_query);
	int	is_current_channel		(const char *, int);
const	char	*get_window_echannel		(int);
	void	destroy_waiting_channels	(int);
	int     claim_waiting_channel 		(const char *chan, int servref);
	int	get_window_server		(int);
	int	set_window_server		(int, int);
	void	change_window_server		(int, int);
	void	window_check_servers		(void);
	int	renormalize_window_levels	(int, Mask);
#define message_from(x, y) real_message_from(x, y, __FILE__, __LINE__)
	int	real_message_from		(const char *, int, const char *, int);
#define message_setall(x, y, z) real_message_setall(x, y, z, __FILE__, __LINE__)
	int     real_message_setall		(int , const char *, int, const char *, int);
	void	pop_message_from		(int);
	Char *	get_who_from			(void);
	int	get_who_level			(void);
	Char *	get_who_file			(void);
	int	get_who_line			(void);
	int	get_to_window			(void);

	void	clear_all_windows		(int, int, int);
	void	clear_window_by_refnum		(int, int);
	void	unclear_all_windows		(int, int, int);
	void	unclear_window_by_refnum	(int, int);
	void	set_scrollback_size		(void *);
	void	set_scroll_lines		(void *);
	void	set_continued_line		(void *);
	BUILT_IN_KEYBINDING(scrollback_backwards);
	BUILT_IN_KEYBINDING(scrollback_forwards);
	BUILT_IN_KEYBINDING(scrollback_end);
	BUILT_IN_KEYBINDING(scrollback_start);
	BUILT_IN_KEYBINDING(unstop_all_windows);
	BUILT_IN_KEYBINDING(toggle_stop_screen);
	void	make_window_current_by_refnum	(int);
	int	make_window_current_informally	(int);
	int	windowcmd_query			(int, char **);
	int	windowcmd_rejoin		(int, char **);
	void	window_check_channels		(void);

	char *	windowctl			(char *);
	void    window_scrollback_needs_rebuild (int);
	void	check_message_from_queue 	(int);

	/* * * * */
	int	clear_window_lastlog_mask	(int);
	int     lookup_window 			(const char *desc);
	int	lookup_any_visible_window	(void);
	int	lookup_window_by_server		(int);

	int     get_server_current_channel	(int);
	int     get_server_current_window	(int);

	int	get_window_bottom			(int);
	int	get_window_current_activity		(int);
	Char *	get_window_current_activity_data 	(int);
	Char *	get_window_current_activity_format	(int);
	int     get_window_cursor 			(int);
	int     get_window_display_buffer_size 		(int);
	Display *get_window_display_ip 			(int);
	int	get_window_display_lines		(int);
	int	get_window_fixed_size			(int);
	int     get_window_geometry 			(int, int *, int *);
	int     get_window_hold_interval 		(int);
	int	get_window_hold_mode			(int);
	int     get_window_holding_distance_from_display_ip 	(int);
	Display *get_window_holding_top_of_display 	(int);
	int	get_window_lastlog_mask			(int, Mask *);
	int	get_window_lastlog_max			(int);
	int	get_window_lastlog_size			(int);
	int    	get_window_indent 			(int);
	FILE *	get_window_log_fp			(int);
	int	get_window_mask				(int, Mask *);
	int	get_window_my_columns 			(int refnum);
	Char *	get_window_name 			(int);
	List *	get_window_nicks			(int);
	int	get_window_notified			(int);
	Char *	get_window_notify_name 			(int);
	int     get_window_priority			(int);
	int	get_window_refnum			(int);
struct ScreenStru *get_window_screen			(int);
	int     get_window_scrollback_distance_from_display_ip 	(int);
	Display *get_window_scrollback_top_of_display 		(int);
	int     get_window_scrollback_top_of_display_exists 	(int);
	Display *get_window_scrolling_top_of_display 		(int);
	int     get_window_scrolling_distance_from_display_ip 	(int);
	int	get_window_skip				(int);
	Status *get_window_status 			(int);
	int     get_window_swappable 			(int);
	int	get_window_top 				(int);
	Char *	get_window_topline 			(int, int);
	int	get_window_toplines_showing 		(int);
	int	get_window_user_refnum			(int);
	Char *	get_window_uuid				(int);

	int	set_window_change_line			(int, int);
	int	set_window_cursor 			(int, int);
	int	set_window_cursor_decr 			(int);
	int	set_window_cursor_incr 			(int);
	int	set_window_indent			(int, int);
	int	set_window_lastlog_mask			(int, Mask);
	int     set_window_lastlog_max         		(int, int);
	int	set_window_lastlog_size_incr		(int);
	int	set_window_lastlog_size_decr		(int);
	int     set_window_notified			(int, int);
	int	set_window_notify_mask			(int, Mask);
	int	set_window_priority			(int, int);

	int	window_is_valid				(int);


#endif /* __window_h__ */


