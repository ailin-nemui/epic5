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

/* Flags for "misc" */
#define WINDOW_NOTIFY		1 << 0
#define WINDOW_NOTIFIED		1 << 1


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
} WNickList;


typedef	struct	WindowStru
{
	unsigned refnum;		/* Unique refnum for window */
	char	*name;			/* Logical name for window */
	int	server;			/* Server that win is connected to */
	int	last_server;		/* Last server connected to */
	int	priority;		/* "Current window Priority" */
	int	top;			/* SCREEN line for top of window */
	int	bottom;			/* SCREEN line for bottom of window */
	int	cursor;			/* WINDOW line where the cursor is */
	int	noscrollcursor;		/* Where the next line goes */
	int	absolute_size;		/* True if window doesnt rebalance */
	int	scroll;			/* True if the window scrolls */
	int	change_line;		/* True if this is a scratch window */
	int	old_size;		/* 
					 * Usu. same as display_size except
					 * right after a screen resize, and
					 * is used as a flag for resize_display
					 */
	int	update;			/* True if window display is dirty */
	unsigned miscflags;		/* Miscellaneous flags. */
	int	beep_always;		/* True if a beep to win always beeps */
	int	notify_level;		/* the notify level.. */
	int	window_level;		/* Lastlog level for the window */
	int	skip;			/* Whether window should be skipped */
	int	columns;		/* How wide we are when hidden */
	int	swappable;		/* Can it be swapped in or out? */
	int	scrolladj;		/* Push back top-of-win on grow? */

	/* Input and Status stuff */
	char	*prompt;		/* Current EXEC prompt for window */
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
	int	display_buffer_size;	/* How big the scrollback buffer is */
	int	display_buffer_max;	/* How big its supposed to be */
	int	display_size;		/* How big the window is - status */

	Display *scrolling_top_of_display;
	int	scrolling_distance_from_display_ip;

	Display *holding_top_of_display;
	int	holding_distance_from_display_ip;

	Display *scrollback_top_of_display;
	int	scrollback_distance_from_display_ip;

	int	display_counter;
	int	hold_slider;

#if 0
		*top_of_display, 	/* Where the viewport starts */
		*display_ip,		/* Where next line goes in rite() */
		*scrollback_point;	/* Where we went into scrollback */

	int	hold_mode;		/* True if we want to hold stuff */
	int	autohold;		/* True if we are in temp hold mode */

	int	lines_held;		/* Lines currently being held */
	int	distance_from_display_ip; /* How far t_o_d is from d_ip */
#endif

	int	hold_interval;		/* How often to update status bar */
	int	last_lines_held;	/* Last time we updated "lines held" */

	/* Channel stuff */
        char    *waiting_channel;       /*
					 * When you JOIN or reconnect, if this
					 * is set, a JOIN to that channel will
					 * put that channel into this win.
					 */
        char    *bind_channel;          /* Current bound channel for win */
	char	*query_nick;		/* Current default target for win */
	WNickList *nicks;		/* List of nick-queries for this win */


	/* /LASTLOG stuff */
	Lastlog	*lastlog_newest;	/* pointer to top of lastlog list */
	Lastlog	*lastlog_oldest;	/* pointer to bottom of lastlog list */
	int	lastlog_level;		/* The LASTLOG_LEVEL, determines what
					 * messages go to lastlog */
	int	lastlog_size;		/* number of messages in lastlog. */
	int	lastlog_max;		/* Max number of messages in lastlog */


	/* /WINDOW LOG stuff */
	int	log;			/* True if file logging is on */
	char	*logfile;		/* window's logfile name */
	FILE	*log_fp;		/* file pointer for the log file */

	/* List stuff */
struct	ScreenStru	*screen;	/* The screen we belong to */
struct	WindowStru	*next;		/* Window below us on screen */
struct	WindowStru	*prev;		/* Window above us on screen */

	/* XXX - Help stuff */
struct	HelpStru	*helper;	/* Info about current /help */

	int		deceased;	/* Set when the window is killed */
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
#if 0
extern	int	in_window_command;
#endif
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
	void	swap_last_window		(char, char *);
	void	next_window			(char, char *);
	void	swap_next_window		(char, char *);
	void	previous_window			(char, char *);
	void	swap_previous_window		(char, char *);
	void	back_window			(void);
	Window 	*get_window_by_refnum		(unsigned);
	Window	*get_window_by_name		(const char *);
	Window  *get_window_by_desc		(const char *);
	char	*get_refnum_by_window		(const Window *);
	int	is_window_visible		(char *);
	char	*get_status_by_refnum		(unsigned, int);
#if 0
	void	redraw_window_statusbar		(Window *);
	void	update_window_statusbar		(Window *);
#endif
	void	update_all_status		(void);
	void	set_prompt_by_refnum		(unsigned, char *);
	char 	*get_prompt_by_refnum		(unsigned);
const	char	*get_target_by_refnum		(unsigned);
const char	*query_nick			(void);
	void	set_query_nick			(char *);
	int	is_current_channel		(const char *, int);
const 	char *	set_channel_by_refnum		(unsigned, const char *);
const	char	*get_echannel_by_refnum		(unsigned);
	char	*get_channel_by_refnum		(unsigned);
	int	is_bound_to_window		(const Window *, const char *);
	Window	*get_window_bound_channel	(const char *);
	int	is_bound_anywhere		(const char *);
	int	is_bound			(const char *, int);
	void    unbind_channel 			(const char *, int);
	char	*get_bound_channel		(Window *);
	void	destroy_waiting_channels	(int);
	int	get_window_server		(unsigned);
	int	get_window_oldserver		(unsigned);
	void	set_window_server		(int, int, int);
	void	change_window_server		(int, int);
	void	reclaim_windows			(int, int);
	void	window_check_servers		(void);
	void	set_level_by_refnum		(unsigned, int);
	void	message_to			(int);
	void	save_message_from		(const char **, int *);
	void	restore_message_from		(const char *, int);
	void	message_from			(const char *, int);
	int	message_from_level		(int);
	void	clear_all_windows		(int, int, int);
	void	clear_window_by_refnum		(unsigned, int);
	void	unclear_all_windows		(int, int, int);
	void	unclear_window_by_refnum	(unsigned, int);
	void	set_scrollback_size		(const void *);
	void	set_scroll_lines		(const void *);
	void	set_continued_line		(const void *);
	unsigned current_refnum			(void);
	int	number_of_windows_on_screen	(Window *);
	void	delete_display_line		(Display *);
	int	add_to_scrollback		(Window *, const unsigned char *);
	int	trim_scrollback			(Window *);
	int	flush_scrollback_after		(Window *);
	void	scrollback_backwards		(char, char *);
	void	scrollback_forwards		(char, char *);
	void	scrollback_end			(char, char *);
	void	scrollback_start		(char, char *);
	void	unstop_all_windows		(char, char *);
	void	toggle_stop_screen		(char, char *);
	void	flush_everything_being_held	(Window *);
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
	int	unhold_windows			(void);
	int	channel_going_away		(Window *, const char *);
	void	window_check_channels		(void);

	int	add_to_scratch_window_scrollback (Window *, const unsigned char *);
	void	check_window_cursor		(Window *);
	void	unhold_window			(Window *);
	int     get_geom_by_winref 		(const char *, int *, int *);
	int	get_winref_by_servref		(int);

	int    is_window_waiting_for_channel (unsigned, const char *);
	void   move_waiting_channel (unsigned oldref, unsigned newref);
	int    get_winref_by_bound_channel (const char *channel, int server);
	const char *   get_bound_channel_by_refnum (unsigned refnum);

	char *	windowctl			(char *);

#endif /* __window_h__ */
