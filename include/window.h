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
	size_t			count;
	char			*line;
	struct	DisplayStru	*prev;
	struct	DisplayStru	*next;
}	Display;

typedef struct	WNickListStru
{
struct WNickListStru	*next;
	char		*nick;
	int		counter;
} WNickList;


typedef	struct	WindowStru
{
	unsigned refnum;		/* Unique refnum for window */
	char	*name;			/* Logical name for window */
	int	server;			/* Server that win is connected to */
	int	last_server;		/* Last server connected to */
	int	priority;		/* "Current window Priority" */
	short	top;			/* SCREEN line for top of window */
	short	bottom;			/* SCREEN line for bottom of window */
	short	cursor;			/* WINDOW line where the cursor is */
	short	noscrollcursor;		/* Where the next line goes */
	short	absolute_size;		/* True if window doesnt rebalance */
	short	scroll;			/* True if the window scrolls */
	short	change_line;		/* True if this is a scratch window */
	short	old_size;		/* 
					 * Usu. same as display_size except
					 * right after a screen resize, and
					 * is used as a flag for resize_display
					 */
	short	update;			/* True if window display is dirty */
	short	notify_when_hidden;	/* True to notify for hidden output */
	short	notified;		/* True if we have notified */
	char *	notify_name;		/* The name for %{1}F */
	short	beep_always;		/* True if a beep to win always beeps */
	Mask	notify_mask;		/* the notify mask.. */
	Mask	window_mask;		/* Lastlog level for the window */
	short	skip;			/* Whether window should be skipped */
	short	columns;		/* How wide we are when hidden */
	short	swappable;		/* Can it be swapped in or out? */
	short	scrolladj;		/* Push back top-of-win on grow? */

	/* Input and Status stuff */
	char *	prompt;			/* Current EXEC prompt for window */
	Status	status;			/* Current status line info */

	/* Display stuff */
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
	 * up until display_ip is the top_of_display.  "display_size" is the
	 * number of rows that can appear on the screen at a given time.
	 */
	Display *top_of_scrollback;	/* Start of the scrollback buffer */
	Display *display_ip;		/* End of the scrollback buffer */
	Display *scroll_marker;		/* Set to display_ip when sb'ing */
	int	display_buffer_size;	/* How big the scrollback buffer is */
	int	display_buffer_max;	/* How big its supposed to be */
	short	display_size;		/* How big the window is - status */

	Display *scrolling_top_of_display;
	int	scrolling_distance_from_display_ip;

	Display *holding_top_of_display;
	int	holding_distance_from_display_ip;

	Display *scrollback_top_of_display;
	int	scrollback_distance_from_display_ip;

	int	display_counter;
	short	hold_slider;

	short	hold_interval;		/* How often to update status bar */
	int	last_lines_held;	/* Last time we updated "lines held" */

	/* Channel stuff */
	WNickList *waiting_chans;	/*
					 * When you JOIN or reconnect, if this
					 * is set, a JOIN to that channel will
					 * put that channel into this win.
					 */
	WNickList *nicks;		/* List of nick-queries for this win */
	int	query_counter;		/* Is there a query anyways? */

	/* /LASTLOG stuff */
struct lastlog_stru *lastlog_newest;	/* pointer to top of lastlog list */
struct lastlog_stru *lastlog_oldest;	/* pointer to bottom of lastlog list */
	Mask	lastlog_mask;		/* The LASTLOG_LEVEL, determines what
					 * messages go to lastlog */
	int	lastlog_size;		/* number of messages in lastlog. */
	int	lastlog_max;		/* Max number of messages in lastlog */


	/* /WINDOW LOG stuff */
	short	log;			/* True if file logging is on */
	char	*logfile;		/* window's logfile name */
	FILE	*log_fp;		/* file pointer for the log file */

	/* List stuff */
struct	ScreenStru	*screen;	/* The screen we belong to */
struct	WindowStru	*next;		/* Window below us on screen */
struct	WindowStru	*prev;		/* Window above us on screen */

	/* XXX - Help stuff */
struct	HelpStru	*helper;	/* Info about current /help */

	short		deceased;	/* Set when the window is killed */
}	Window;

/*
 * WindowStack: The structure for the POP, PUSH, and STACK functions. A
 * simple linked list with window refnums as the data 
 */
typedef	struct	window_stack_stru
{
	unsigned int	refnum;
	struct	window_stack_stru	*next;
}	WindowStack;

extern	Window	*current_window;
extern	Window	*to_window;
extern	Window	*invisible_list;
extern	int	who_level;
extern	const char	*who_from;
extern	unsigned window_display;
extern	unsigned current_window_priority;


	BUILT_IN_COMMAND(windowcmd);

	Window 	*new_window 			(struct ScreenStru *);
	void	delete_window			(Window *);
	void	delete_all_windows		(void);
	int	traverse_all_windows		(Window **);
	void	add_to_invisible_list		(Window *);
	Window	*add_to_window_list		(struct ScreenStru *, Window *);
	void	recalculate_window_positions	(struct ScreenStru *);
	void	window_statusbar_needs_update	(Window *);
	void	window_statusbar_needs_redraw	(Window *);
	void	window_body_needs_redraw	(Window *);
	void	redraw_all_windows		(void);
	void	recalculate_windows		(struct ScreenStru *);
	void	rebalance_windows		(struct ScreenStru *);
	void	update_all_windows		(void);
	void	set_current_window		(Window *);
	void	hide_window			(Window *);
	BUILT_IN_KEYBINDING(swap_last_window);
	BUILT_IN_KEYBINDING(next_window);
	BUILT_IN_KEYBINDING(swap_next_window);
	BUILT_IN_KEYBINDING(previous_window);
	BUILT_IN_KEYBINDING(swap_previous_window);
	void	back_window			(void);
	Window 	*get_window_by_refnum		(unsigned);
	Window	*get_window_by_name		(const char *);
	Window  *get_window_by_desc		(const char *);
	char	*get_refnum_by_window		(const Window *);
	int	is_window_visible		(char *);
	char	*get_status_by_refnum		(unsigned, int);
	void	update_all_status		(void);
	void	set_prompt_by_refnum		(unsigned, const char *);
const	char 	*get_prompt_by_refnum		(unsigned);
const	char	*get_target_by_refnum		(unsigned);
const 	char	*query_nick			(void);
const 	char *	get_equery_by_refnum		(int);
	BUILT_IN_KEYBINDING(switch_query);
	int	is_current_channel		(const char *, int);
const 	char *	set_channel_by_refnum		(unsigned, const char *);
const	char	*get_echannel_by_refnum		(unsigned);
	char	*get_channel_by_refnum		(unsigned);
	void	destroy_waiting_channels	(int);
	int     claim_waiting_channel (const char *chan, int servref);
	int	get_window_server		(unsigned);
	int	get_window_oldserver		(unsigned);
	void	set_window_server		(int, int, int);
	void	change_window_server		(int, int);
	void	reclaim_windows			(int, int);
	void	window_check_servers		(void);
	int	turn_on_level			(unsigned, int);
	int	turn_off_level			(int);
	void	message_to			(int);
	void	save_message_from		(const char **, int *);
	void	restore_message_from		(const char *, int);
#define message_from(x, y) real_message_from(x, y, __FILE__, __LINE__)
	int	real_message_from		(const char *, int, const char *, int);
	void	pop_message_from		(int);

	void	clear_all_windows		(int, int, int);
	void	clear_window_by_refnum		(unsigned, int);
	void	unclear_all_windows		(int, int, int);
	void	unclear_window_by_refnum	(unsigned, int);
	void	set_scrollback_size		(const void *);
	void	set_scroll_lines		(const void *);
	void	set_continued_line		(const void *);
	unsigned current_refnum			(void);
	int	number_of_windows_on_screen	(Window *);
	int	add_to_scrollback		(Window *, const unsigned char *);
	int	trim_scrollback			(Window *);
	int	flush_scrollback_after		(Window *);
	BUILT_IN_KEYBINDING(scrollback_backwards);
	BUILT_IN_KEYBINDING(scrollback_forwards);
	BUILT_IN_KEYBINDING(scrollback_end);
	BUILT_IN_KEYBINDING(scrollback_start);
	BUILT_IN_KEYBINDING(unstop_all_windows);
	BUILT_IN_KEYBINDING(toggle_stop_screen);
	int	window_is_holding		(Window *);
	int	unhold_a_window			(Window *);
	void	recalculate_window_cursor_and_display_ip	(Window *);
	char	*get_nicklist_by_window		(Window *); /* XXX */
	void	make_window_current_by_refnum	(int);
	void	make_window_current		(Window *);
	Window  *window_query			(Window *, char **);
	Window	*window_rejoin			(Window *, char **);
	Window	*window_scroll			(Window *, char **);
	Window *window_server			(Window *, char **);
	void	window_check_channels		(void);

	void	check_window_cursor		(Window *);
	int     get_geom_by_winref 		(const char *, int *, int *);
	int	get_winref_by_servref		(int);

	int    is_window_waiting_for_channel (unsigned, const char *);
	void   move_waiting_channel (unsigned oldref, unsigned newref);
	int    get_winref_by_bound_channel (const char *channel, int server);
	const char *   get_bound_channel_by_refnum (unsigned refnum);

	char *	windowctl			(char *);

#endif /* __window_h__ */
