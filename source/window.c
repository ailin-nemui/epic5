/*
 * window.c: Handles the organzation of the logical viewports (``windows'')
 * for irc.  This includes keeping track of what windows are open, where they
 * are, and what is on them.
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright 1997, 2014 EPIC Software Labs.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notices, the above paragraph (the one permitting redistribution),
 *    this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the author(s) may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* Sigh */
#define __need_putchar_x__
#define NEED_WINDOWSTRU

#include "irc.h"
#include "screen.h"
#include "window.h"
#include "vars.h"
#include "server.h"
#include "list.h"
#include "lastlog.h"
#include "termx.h"
#include "names.h"
#include "ircaux.h"
#include "input.h"
#include "status.h"
#include "output.h"
#include "log.h"
#include "hook.h"
#include "parse.h"
#include "commands.h"
#include "exec.h"
#include "functions.h"
#include "reg.h"
#include "timer.h"
#include <math.h>

typedef	struct	WindowStru
{
	/* List stuff */
	int		_next_;
	int		_prev_;

	int		screen_;		/* The screen we belong to */
	short		deceased;		/* Set when the window is killed */

	unsigned 	refnum;			/* Unique refnum for window */
	unsigned 	user_refnum;		/* Sequencing number used by the user */
	char *		name;			/* Logical name for window */
	char *		uuid;			/* UUID4 for window (never changes) */
	unsigned 	priority;		/* "Current window Priority" */

	/* Output rule stuff */
	int		server;			/* Server that win is connected to */
	Mask		window_mask;		/* Window level for the window */
	List *		waiting_chans;		/*
					 	 * When you JOIN or reconnect, if this
					 	 * is set, a JOIN to that channel will
					 	 * put that channel into this win.
					 	 */
	List *		nicks;			/* List of nick-queries for this win */
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

	Display *	clear_point;

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


static const char *onoff[] = { "OFF", "ON" };

/* Resize relatively or absolutely? */
#define RESIZE_REL 1
#define RESIZE_ABS 2

/* used by the update flag to determine what needs updating */
#define REDRAW_DISPLAY     (1 << 0)
#define UPDATE_STATUS      (1 << 1)
#define REDRAW_STATUS      (1 << 2)
#define FORCE_STATUS	   (1 << 3)

/*
 * The current window.  This replaces the old notion of "curr_scr_win" 
 * which had the arbitrary restriction that you could not easily operate
 * on a window that was not on the current screen, and switching between
 * screens (or to a hidden window) was impossible.  We no longer have any
 * concept of "current screen", opting instead to use "last_input_screen"
 * for handling input events, and "current_window" for everything else.
 * current_window is set when you do an input event, so all those places
 * that presumed curr_scr_win still work by referencing current_window.
 */
static	int	current_window_ = -1;

/*
 * All of the hidden windows.  These windows are not on any screen, and
 * therefore are not visible.
 */
static	int	_invisible_list = -1;

/*
 * Each time a window is made the current window, it grabs this value and
 * increments it.  We use this value to track "current window"-ness in
 * various contexts.
 */
	unsigned current_window_priority = 1;

/*
 * Ditto for queries
 */
static  int     current_query_counter = 0;

/*
 * Ditto for scrollback items
 */
static	int	current_display_counter = 1;

#define INTERNAL_REFNUM_CUTOVER		1001

static	Window *	windows[INTERNAL_REFNUM_CUTOVER * 2 + 1] = { NULL };

typedef struct  WNickListStru
{
        int             counter;
}       WNickList;

static	Window *get_window_by_refnum_direct 	(int refnum);
static	int	get_invisible_list 		(void) ;
static	int	unlink_window 			(int window_);
static	void	delete_window_contents 		(int window_);
static	int	traverse_all_windows 		(int *window_);
static	int 	traverse_all_windows_by_priority (int *refnum_);
static	void 	remove_from_invisible_list 	(int window_);
static	int	add_to_window_list 		(int screennum, int window_);
static	void 	remove_window_from_screen 	(int window_, int hide, int recalc);
static	void	recalculate_window_positions 	(int screennum);
static	void	swap_window 			(int v_window_, int window_);
static	void 	move_window_to 			(int window_, int offset);
static	void 	move_window 			(int window_, int offset);
static 	void 	resize_window 			(int how, int window_, int offset);
static	void	window_statusbar_needs_redraw 	(int refnum);
static	void	window_body_needs_redraw 	(int refnum);
static	void	rebalance_windows 		(int screennum);
static	void	window_check_columns 		(int refnum);
static	void	rebuild_scrollback 		(int refnum);
static	void	save_window_positions 		(int window_, intmax_t *, intmax_t *, intmax_t *, intmax_t *);
static	void	restore_window_positions 	(int window_, intmax_t, intmax_t, intmax_t, intmax_t);
static	void 	my_goto_window 			(int screennum, int which);
static	int 	hide_window 			(int window_);
static	void 	show_window 			(int window_);
static	int	get_window_by_desc 		(const char *stuff);
static	int	get_next_window  		(int window_);
static	int	get_previous_window 		(int window_);
#if 0
static	void	set_window_screen 		(int refnum, int screennum);
#endif
static	void	recheck_queries 		(int window_);
static	int	check_window_target 		(int window_, int server, const char *nick);
static	int	add_window_target 		(int window_, int server, const char *target, int as_current);
static	void    destroy_window_waiting_channels (int refnum);
static	int	add_waiting_channel 		(int refnum, const char *chan);
static	void 	revamp_window_masks 		(int refnum);
static	void	adjust_context_windows 		(int old_win, int new_win);
static	void 	clear_window 			(int window_);
static	void	unclear_window 			(int window_);
static	int	ensure_window_priority 		(int refnum);
static	int 	is_window_name_unique 		(char *name);
static	char *	get_waiting_channels_by_window 	(int window_);
static	char *	get_nicklist_by_window 		(int window_);
static	void 	list_a_window 			(int window_, int len);
static	int	get_window_display_counter_incr (int window);
static	int	get_window2 			(const char *name, char **args);
static	int	get_invisible_window 		(const char *name, char **args);
static	int 	get_number 			(const char *name, char **args, int *var);
static	int 	get_boolean 			(const char *name, char **args, short *var);
static	int	new_search_term 		(const char *arg);
static	void	update_scrollback_indicator 	(int window_);
static	void	remove_scrollback_indicator 	(int window_);
static	void	window_indicator_is_visible 	(int window_);
static	void	cleanse_indicator 		(int window_);
static	void	indicator_needs_update 		(int window_);
static	void	go_back_to_indicator 		(int window_);
static	void 	delete_display_line 		(Display *stuff);
static	Display *new_display_line 		(Display *prev, int window_);
static	int	add_to_display 			(int window_, const char *str, intmax_t refnum);
static	int	flush_scrollback 		(int window_, int abandon);
static	int	flush_scrollback_after 		(int window_, int abandon);
static	void	window_scrollback_backwards 	(int window_, int skip_lines, int abort_if_not_found, int (*test)(int, Display *, void *), void *meta);
static	void	window_scrollback_forwards 	(int window_, int skip_lines, int abort_if_not_found, int (*test)(int, Display *, void *), void *meta);
static	int	window_scroll_lines_tester 	(int window_, Display *line, void *meta);
static	void 	window_scrollback_backwards_lines (int window_, int my_lines);
static	void 	window_scrollback_forwards_lines (int window_, int my_lines);
static	int	window_scroll_regex_tester 	(int window_, Display *line, void *meta);
static	void 	window_scrollback_to_string 	(int window_, regex_t *preg);
static	void 	window_scrollforward_to_string 	(int window_, regex_t *preg);
static	int	window_scroll_time_tester 	(int window_, Display *line, void *meta);
static	void	window_scrollback_start 	(int window_);
static	void	window_scrollback_end 		(int window_);
static	void	window_scrollback_forward 	(int window_);
static	void	window_scrollback_backward 	(int window_);
static	void 	recalculate_window_cursor_and_display_ip (int window_);
static	void 	set_screens_current_window 	(int screennum, int window);
static	int	change_line 			(int window_, const char *str);
static	int	count_fixed_windows 		(int screennum);
static	void	window_change_server 		(int window_, int server) ;
static	void    resize_window_display 		(int window_);
static	int	get_window_deceased 		(int window);
static	void	set_window_deceased 		(int window, int value);
static	int 	windowcmd_next 			(int refnum, char **args);
static	int 	windowcmd_previous 		(int refnum, char **args);
static 	Display *get_window_clear_point         (int window);
static	int	reset_window_clear_point	(int window);
static	int	set_window_list_check		(int window, int value);

static	int	get_invisible_list 		(void) { return _invisible_list; }

/* * * * * * * * * * * CONSTRUCTOR AND DESTRUCTOR * * * * * * * * * * * */
/*
 * new_window: This creates a new window on the screen.  It does so by either
 * splitting the current window, or if it can't do that, it splits the
 * largest window.  The new window is added to the window list and made the
 * current window 
 */
int	new_window (int screen_)
{
	Window	*	new_w;
	int		new_refnum, new_user_refnum;
	int		i;
	int		window_;

	if (dumb_mode && current_window_ >= 1)
		return -1;

	/*
	 * STAGE 1 -- Ensuring all values are set to default values
	 */
	/* Meta stuff */
	new_refnum = INTERNAL_REFNUM_CUTOVER;
	window_ = 0;
	while (traverse_all_windows2(&window_))
	{
		if (get_window_refnum(window_) == new_refnum)
		{
			new_refnum++;
			window_ = 0;
		}
	}

	new_user_refnum = 1;
	window_ = 0;
	while (traverse_all_windows2(&window_))
	{
		if (get_window_user_refnum(window_) == new_user_refnum)
		{
			if (new_user_refnum >= INTERNAL_REFNUM_CUTOVER)
			{
				yell("window new: All refnums in use, sorry");
				return -1;
			}
			new_user_refnum++;
			window_ = 0;
		}
	}

	new_w = (Window *) new_malloc(sizeof(Window));
	new_w->refnum = new_refnum;
	new_w->user_refnum = new_user_refnum;

	windows[new_w->refnum] = new_w;
	windows[new_w->user_refnum] = new_w;

	new_w->name = NULL;
	new_w->uuid = uuid4_generate_no_dashes();	/* THIS NEVER CHANGES */
	new_w->priority = 0;		/* Filled in later */

	/* Output rule stuff */
	if (current_window_ >= 1)
		new_w->server = get_window_server(current_window_);
	else
		new_w->server = NOSERV;
	new_w->original_server_string = NULL;

	if (current_window_ < 1)		/* First window ever */
		mask_setall(&new_w->window_mask);
	else
		mask_unsetall(&new_w->window_mask);
	new_w->waiting_chans = NULL;
	new_w->nicks = NULL;
	new_w->query_counter = 0;

	/* Internal flags */
	new_w->top = 0;			/* Filled in later */
	new_w->bottom = 0;		/* Filled in later */
	new_w->cursor = -1;		/* Force a clear-screen */
	new_w->change_line = -1;
	new_w->update = 0;

	/* User-settable flags */
	new_w->notify_when_hidden = 0;
	new_w->notified = 0;
	new_w->notify_name = NULL;
	new_w->beep_always = 0;
	new_w->notify_mask = real_notify_mask();
	new_w->skip = 0;
	new_w->swappable = 1;
	new_w->scrolladj = 1;
	new_w->killable = 1;
	new_w->scroll_lines = -1;

	/* Input prompt and status bar stuff */
	for (i = 0; i < 3; i++)
	{
		new_w->status.line[i].raw = NULL;
		new_w->status.line[i].format = NULL;
		new_w->status.line[i].count = 0;
		new_w->status.line[i].result = NULL;
	}
	new_w->status.number = 1;
	new_w->status.special = NULL;
	compile_status(new_w->refnum, &new_w->status);
	make_status(new_w->refnum, &new_w->status);
	window_statusbar_needs_update(new_w->refnum);
	window_statusbar_needs_redraw(new_w->refnum);
	new_w->status.prefix_when_current = NULL;
	new_w->status.prefix_when_not_current = NULL;

	/* Scrollback stuff */
	new_w->top_of_scrollback = NULL;	/* Filled in later */
	new_w->display_ip = NULL;		/* Filled in later */
	new_w->display_buffer_size = 0;
	new_w->display_buffer_max = get_int_var(SCROLLBACK_VAR);
	new_w->scrolling_top_of_display = NULL;		/* Filled in later */
	new_w->scrolling_distance_from_display_ip = -1;	/* Filled in later */
	new_w->holding_top_of_display = NULL;		/* Filled in later */
	new_w->holding_distance_from_display_ip = -1;	/* Filled in later */
	new_w->scrollback_top_of_display = NULL;	/* Filled in later */
	new_w->scrollback_distance_from_display_ip = -1; /* Filled in later */
	new_w->display_counter = 1;
	new_w->hold_slider = get_int_var(HOLD_SLIDER_VAR);
	new_w->clear_point = NULL;			/* Filled in later */

	/* The scrollback indicator */
	new_w->scrollback_indicator = (Display *)new_malloc(sizeof(Display));
	new_w->scrollback_indicator->line = NULL;
	new_w->scrollback_indicator->count = 0;
	new_w->scrollback_indicator->prev = NULL;
	new_w->scrollback_indicator->next = NULL;
	new_w->scrollback_indicator->when = time(NULL);

	/* Window geometry stuff */
	new_w->my_columns = 0;			/* Filled in later? */
	new_w->display_lines = 24;		/* Filled in later */
	new_w->logical_size = 100;		/* XXX Implement this */
	new_w->fixed_size = 0;
	new_w->old_display_lines = 1;	/* Filled in later */
	new_w->indent = get_int_var(INDENT_VAR);

	/* Hold mode stuff */
	new_w->hold_interval = 10;

	/* LASTLOG stuff */
	new_w->lastlog_mask = real_lastlog_mask();
	new_w->lastlog_size = 0;
	new_w->lastlog_max = get_int_var(LASTLOG_VAR);

	/* LOGFILE stuff */
	new_w->log = 0;
	new_w->logfile = NULL;
	new_w->log_fp = NULL;
	new_w->log_rewrite = NULL;
	new_w->log_mangle = 0;
	new_w->log_mangle_str = NULL;

	/* TOPLINE stuff */
	new_w->toplines_wanted = 0;		/* Filled in later? */
	new_w->toplines_showing = 0;		/* Filled in later? */
	for (i = 0; i < 10; i++)
		new_w->topline[i] = NULL;

	/* ACTIVITY stuff */
	new_w->current_activity = 0;
	for (i = 0; i < 11; i++)
	{
		new_w->activity_data[i] = NULL;
		new_w->activity_format[i] = NULL;
	}

	/* Screen list stuff */
	/* new_w->screen = screen; */	/* add_to_window_list() does this */
	new_w->_next_ = -1;
	new_w->_prev_ = -1;
	new_w->deceased = 0;

	/*
	 * STAGE 2 -- Bringing the window to life 
	 */
	/* Initialize the scrollback */
	new_w->rebuild_scrollback = 0;
	new_w->top_of_scrollback = new_display_line(NULL, new_w->refnum);
	new_w->top_of_scrollback->line = NULL;
	new_w->top_of_scrollback->next = NULL;
	new_w->display_buffer_size = 1;
	new_w->display_ip = new_w->top_of_scrollback;
	new_w->scrolling_top_of_display = new_w->top_of_scrollback;
	new_w->old_display_lines = 1;

	/* Make the window visible (or hidden) to set its geometry */
	if (screen_ >= 0 && add_to_window_list(screen_, new_w->refnum) >= 1)
		set_screens_current_window(screen_, new_w->refnum);
	else
	{
#if 0
		new_w->screen = NULL;
#endif
		new_w->screen_ = -1;
		add_to_invisible_list(new_w->refnum);
	}

	/* Finally bootstrap the visible part of the window */
	resize_window_display(new_w->refnum);
	window_statusbar_needs_redraw(new_w->refnum);

	/*
	 * Offer it to the user.  I dont know if this will break stuff
	 * or not.
	 */
	do_hook(WINDOW_CREATE_LIST, "%d", new_w->user_refnum);

	return new_w->refnum;
}


/*
 * unlink_window: There are two important aspects to deleting a window.
 * The first aspect is window management.  We must release the window
 * from its screen (or from the invisible list) so that it is not possible
 * for the user to reference the window in any way.  We also want to 
 * re-apportion the window's visible area to other windows on the screen.
 * The second aspect is purging the window's private data.  Ideally, we 
 * want these things to take place in this order.
 */
static int	unlink_window (int window_)
{
	int	invisible = 0;
	int	fixed_wins;
	int	fixed;

	if (!window_is_valid(window_))
		return 0;

	if (get_window_screennum(window_) < 0)
	{
		invisible = 1;
		fixed_wins = 0;
	}
	else
	{
		invisible = 0;
		fixed_wins = count_fixed_windows(get_window_screennum(window_));
	}

	if (get_window_fixed_size(window_) && get_window_skip(window_))
		fixed = 1;
	else
		fixed = 0;

	/*
	 * If this is a hidden window and the client is not going down,
	 * you cannot kill this window if:
	 *
	 * 1) This is a fixed window and it is the only window.
	 * 2) This is the only non-fixed window (unless a swap can occur)
	 */
	if (dead == 0 && get_window_screennum(window_) >= 0)
	{
	    if ((fixed && get_screen_visible_windows(get_window_screennum(window_)) == 1) ||
	        (!fixed && get_screen_visible_windows(get_window_screennum(window_)) - fixed_wins <= 1
			&& get_invisible_list() < 1))
	    {
		say("You can't kill the only window!");
		return 0;
	    }
	}

	/* Let the script have a stab at this first. */
	do_hook(WINDOW_BEFOREKILL_LIST, "%d", get_window_user_refnum(window_));

	/*
	 * Mark this window as deceased.  This is important later.
	 */
	set_window_deceased(window_, 1);

	/*
	 * If the client is exiting and this is the last window on the
	 * screen, we need to do some extra cleanup on the screen that
	 * otherwise we would not dare to perform.  We also want to do some
	 * extra sanity checking to make sure nothing bad has happened
	 * elsewhere.
	 */
	if ((dead == 1) && get_window_screennum(window_) >= 0 &&
	    (get_screen_visible_windows(get_window_screennum(window_)) == 1))
	{
		if ((get_screen_window_list(get_window_screennum(window_)) != window_) ||
			 get_window_next(window_) != -1 ||
			 (get_screen_input_window(get_window_screennum(window_)) > 0 && 
			    get_screen_input_window(get_window_screennum(window_)) != window_))
		{
			panic(1, "unlink_window: My screen says there is only one window on it, and I don't agree.");
		}
		else
		{
			set_window_deceased(window_, 1);
			set_screen_window_list(get_window_screennum(window_), -1);
			set_screen_visible_windows(get_window_screennum(window_), 0);
			set_screen_input_window(get_window_screennum(window_), -1);
#if 0
			set_window_screen(window_, NULL);
#else
			set_window_screennum(window_, -1);
#endif
			if (current_window_ == window_)
				current_window_ = -1;
		}

		/*
		 * This 'goto' saves me from making the next 75 lines part
		 * of a big (ultimately unnecesary) 'else' clause, requiring
		 * me to indent it yet again and break up the lines and make
		 * it less readable.  Don't bug me about this.
		 */
		return 1;
	}


	/* Move this window's channels anywhere else. */
	/* 
	 * Ugh. We've already marked the window as deceased, so it
	 * officially no longer exists; on top of this, 'swap_window'
	 * is a sequence point, so channels must be capable of syncing
	 * up before that.  The ref checks will fail if there are any
	 * channels on this window, because the window is dead, and so
	 * we need to move the channels away before the sequence point.
	 * I hope this explanation makes sense. ;-)
	 */
	reassign_window_channels(window_);

	/*
	 * At this point, we know there must be one of three cases:
	 * 1) The window is an invisible window
	 * 2) The window is on a screen with other windows.
	 * 3) The window is last window on screen, with an invisible window.
	 *
	 * We handle each of these three cases seperately.  If any other
	 * situation arises, we panic, because that means I forgot something
	 * and that *is* a bug.
	 */
	if (invisible)
		remove_from_invisible_list(window_);
	else if (fixed || get_screen_visible_windows(get_window_screennum(window_)) > fixed_wins + 1)
		remove_window_from_screen(window_, 0, 1);
	else if (get_invisible_list() >= 1)
	{
		set_window_swappable(window_, 1);
		swap_window(window_, -1);
	}
	else
	{
		yell("I don't know how to kill window [%d]", get_window_user_refnum(window_));
		return 0;
	}

	/*
	 * This is done for the sake of invisible windows; but it is a safe
	 * sanity check and can be done for any window, visible or invisible.
	 * Basically, we have to be sure that we find some way to make sure
	 * that the 'current_window' pointer is not pointing at what we are
	 * about to delete (or else the client will crash.)
	 */
	if (!dead && (current_window_ < 1 || window_ == current_window_))
		make_window_current_by_refnum(0);
	if (!dead && (current_window_ < 1 || window_ == current_window_))
		panic(1, "unlink_window: window == current_window -- I was unable to find another window, but I already checked that, so this is a bug.");

	return 1;
}

static void	delete_window_contents (int window_)
{
	Window *window = get_window_by_refnum_direct(window_);
	char 	buffer[BIG_BUFFER_SIZE + 1];
	int	oldref;
	int	i;

	/*
	 * OK!  Now we have completely unlinked this window from whatever
	 * window chain it was on before, be it a screen, or be it the
	 * invisible window list.  The screens have been updated, and the
	 * only place this window exists is in our 'window' pointer.  We
	 * can now safely go about the business of eliminating what it is
	 * pointing to.
	 */
	/* Save a copy of the refnum for /on window_kill later. */
	if (window->name)
		strlcpy(buffer, window->name, sizeof buffer);
	else
		strlcpy(buffer, ltoa(window->user_refnum), sizeof buffer);
	oldref = window->user_refnum;

	/*
	 * Clean up after the window's internal data.
	 */
	/* Status bars... */
	for (i = 0; i < 3; i++)
	{
		new_free(&window->status.line[i].raw);
		new_free(&window->status.line[i].format);
		new_free(&window->status.line[i].result);
		window->status.number = 1;
		new_free(&window->status.special);
	}

	/* Various things... */
	new_free(&window->logfile);
	new_free(&window->name);
	new_free(&window->uuid);

	/* Delete the indicator if it's not already in the logical display */
	if (window->scrollback_indicator->prev == NULL &&
	    window->scrollback_indicator->next == NULL)
	{
		new_free(&window->scrollback_indicator->line);
		new_free((char **)&window->scrollback_indicator);
	}

	/* The logical display */
	{ 
		Display *next;
		while (window->top_of_scrollback)
		{
			/* XXXX This should use delete_display_line! */
			next = window->top_of_scrollback->next;
			new_free(&window->top_of_scrollback->line);
			new_free((char **)&window->top_of_scrollback);
			window->display_buffer_size--;
			window->top_of_scrollback = next;
		}
		window->display_ip = NULL;
		if (window->display_buffer_size != 0)
			panic(1, "delete_window_contents: display_buffer_size is %d, should be 0", window->display_buffer_size);
	}

	/* The lastlog... */
	window->lastlog_max = 0;
	truncate_lastlog(window->refnum);

	/* The nick list... */
	{
		List *next;

		while (window->nicks)
		{
			next = window->nicks->next;
			new_free(&window->nicks->name);
			new_free((char **)&window->nicks->d);
			new_free((char **)&window->nicks);
			window->nicks = next;
		}
	}

	destroy_window_waiting_channels(window->refnum);

	/* Adjust any active output contexts pointing at this window to point
	 * somewhere sensible instead. */
	if (current_window_ < 1)
		adjust_context_windows(window->refnum, current_window_);

	/* 
	 * XXX It might be appropriate to sanity check this;
	 * however, sanity checking is only useful if there
	 * is some recovery mechanism that makes sense.
	 */
	if (windows[window->refnum] == window)
		windows[window->refnum] = NULL;
	if (windows[window->user_refnum] == window)
		windows[window->user_refnum] = NULL;

	/*
	 * Nuke the window, check server connections, and re-adjust window
	 * levels for whoever is left.  Don't check the levels if we are
	 * going down, as its a wasted point.
	 */
	{Window *owd = window; new_free((char **)&owd);}

	if (!dead)
		do_hook(WINDOW_KILL_LIST, "%d %s", oldref, buffer);
}

/*
 * This should only ever be called by irc_exit().  DONT CALL THIS ELSEWHERE!
 */
void 	delete_all_windows (void)
{
	int	refnum = 0;

	for (refnum = 0; traverse_all_windows2(&refnum); refnum = 0)
	{
		if (unlink_window(refnum))
			delete_window_contents(refnum);
	}
}

/* * * * * * * * * * * ITERATE OVER WINDOWS * * * * * * * * * * * * * * * */
/*
 * traverse_all_windows: Based on the old idea by phone that there should 
 * be a way to iterate the window list without having to keep a static
 * data member in the function.  So now this is "thread safe".
 *
 * To initialize, *ptr should be NULL.  The function will return 1 each time
 * *ptr is set to the next valid window.  When the function returns 0, then
 * you have iterated all windows.
 */
static int	traverse_all_windows (int *window_)
{
	/*
	 * If this is the first time through...
	 */
	if (*window_ == 0)
	{
		int	s = 0;

		/* XXX This is wrong and buggy */
		for (s = 0; traverse_all_screens(&s);)
		{
			if (get_screen_alive(s) && get_screen_window_list(s) != -1)
				break;
		}

		if (s < 0 && get_invisible_list() < 1)
			return 0;
		else if (s < 0)
			*window_ = get_invisible_list();
		else
			*window_ = get_screen_window_list(s);
	}

	/*
	 * As long as there is another window on this screen, keep going.
	 */
	else if (get_window_next(*window_) != -1)
		*window_ = get_window_next(*window_);

	/*
	 * If there are no more windows on this screen, but we do belong to
	 * a screen (eg, we're not invisible), try the next screen
	 */
	else if (get_window_screennum(*window_) >= 0)
	{
		/*
		 * Skip any dead screens
		 */
		int ns_ = get_screen_next(get_window_screennum(*window_));
		while (ns_ >= 0 && (!get_screen_alive(ns_) || get_screen_window_list(ns_) < 0))
			ns_ = get_screen_next(ns_);

		/*
		 * If there are no other screens, then if there is a list
		 * of hidden windows, try that.  Otherwise we're done.
		 */
		if (ns_ < 0 && get_invisible_list() < 1)
			return 0;
		else if (ns_ < 0)
			*window_ = get_invisible_list();
		else
			*window_ = get_screen_window_list(ns_);
	}

	/*
	 * Otherwise there are no other windows, and we're not on a screen
	 * (eg, we're hidden), so we're all done here.
	 */
	else
		return 0;

	/*
	 * If we get here, we're in business!
	 */
	return 1;
}

/*
 * traverse_all_windows_by_priority: 
 * This iterates over all windows, except it starts with the global current
 * window (the one with the highest "priority" and then each time will 
 * return the window with the next lower priority.)
 *
 * To initialize, *refnum_ should be 0.  The function will return 1 each time
 * *ptr is set to the next valid window.  When the function returns 0, then
 * you have iterated all windows.
 */
static int 	traverse_all_windows_by_priority (int *refnum_)
{
	int		winner = 0;
	unsigned	ceiling;
	int		window_;

	/*
	 * If this is the first time through...
	 */
	if (*refnum_)
		ceiling = get_window_priority(*refnum_);
	else
		ceiling = UINT_MAX;

	for (window_ = 0; traverse_all_windows2(&window_); )
	{
		/* 
		 * Does this candidate (window_) have a higher
		 * priority than our current "winner" but a 
		 * lower priority than 'refnum'?
		 * If so, it becomes the "winner"
		 */
		if ((winner == 0 || get_window_priority(window_) > get_window_priority(winner))
				    && get_window_priority(window_) < ceiling)
			winner = window_;
	}

	/* If there was a winner, return 1 */
	if ((*refnum_ = winner) >= 1)
		return 1;

	/* No Winner?  Then we're done. */
	return 0;
}

/*
 * traverse_all_windows_on_screen2: 
 * This function shall return all windows on 'screen' in the order that
 * they are on the screen, top to bottom.
 *
 * To initialize, *refnum_ should be NULL.  The function will return 1 each time
 * *refnum_ is set to the next valid window.  When the function returns 0, then
 * you have iterated all windows.
 */
static int 	traverse_all_windows_on_screen2 (int *refnum_, int screen_)
{
	int	s_;

	s_ = screen_;

	/*
	 * If this is the first time through...
	 * 's_' < 0 means traverse all invisible windows.
	 */
	if (*refnum_ == 0)
	{
		if (s_ < 0)
			*refnum_ = get_invisible_list();
		else
			*refnum_ = get_screen_window_list(s_);
	}

	/*
	 * As long as there is another window on this screen, keep going.
	 */
	else 
		*refnum_ = get_window_next(*refnum_);

	if (*refnum_ >= 1)
		return 1;
	else
		return 0;
}

int	traverse_all_windows2 (int *refnum_)
{
	if (refnum_ == NULL)
		panic(1, "traverse_all_windows2: refnum_ is NULL!");

	if (*refnum_ < 1)
		*refnum_ = 0;

	if (!traverse_all_windows(refnum_))
		*refnum_ = 0;

	if (*refnum_ >= 1)
		return 1;
	else
		return 0;
}

/* * * * * * * * * * * * * * * * WINDOW LISTS * * * * * * * * * * * * * * * */

/*
 * Handle the client's list of invisible windows.
 */
static void 	remove_from_invisible_list (int window_)
{
	int	refnum;

	if (!window_is_valid(window_))
		return;

	/* Purely a sanity check */
	for (refnum = _invisible_list; refnum >= 1; refnum = get_window_next(refnum))
		if (get_window_refnum(refnum) == get_window_refnum(window_))
			break;
	if (refnum < 1)
		panic(1, "remove_from_invisible_list: This window is _not_ invisible");

	/*
	 * Unlink it from the list
	 */
	if (get_window_prev(window_) != -1)
		set_window_next(get_window_prev(window_), get_window_next(window_));
	else
		_invisible_list = get_window_next(window_);		/* 123456 */
	if (get_window_next(window_) != -1)
		set_window_prev(get_window_next(window_), get_window_prev(window_));
}

void 	add_to_invisible_list (int window_)
{
	int	old_invisible_list;
	int	refnum;

	/*
	 * XXX Sanity check -- probably unnecessary
	 * If the window is already invisible, do nothing
	 */
	for (refnum = _invisible_list; refnum >= 1; refnum = get_window_next(refnum))
		if (get_window_refnum(refnum) == get_window_refnum(window_))
			return;

	/*
	 * Because this blows away window->_next, it is implicitly
	 * assumed that you have already removed the window from
	 * its screen.
	 */
	old_invisible_list = _invisible_list;
	if (old_invisible_list != -1)
		set_window_prev(old_invisible_list, window_);
	set_window_prev(window_, -1);
	set_window_next(window_, old_invisible_list);
	_invisible_list = window_;

	if (get_window_screennum(window_) >= 0)
		set_window_my_columns(window_, get_screen_columns(get_window_screennum(window_)));
	else
		set_window_my_columns(window_, current_term->TI_cols);	/* Whatever */
#if 0
	set_window_screen(window_, NULL);
#else
	set_window_screennum(window_, -1);
#endif
}


/*
 * add_to_window_list - Make a window visible on a screen.
 *
 * Argument:
 *	screen	- The screen to which the window will be visible
 *	window 	- A window to be made visible (must be hidden/new).
 *
 * Notes:
 *	Adding a window to a screen involves two decisions
 *	  1. Where to put it?
 *	  2. How big to make it?
 *
 *	A new window is usually put above the current window, unless the 
 *	current window is < 4 lines, or /set always_split_biggest is ON.
 *
 *	We usually make the new window half the size of the split window.
 *	However, this is strictly nominal, because a window's "size" is 
 *	actually three parts:
 *	  1. Toplines
 *	  2. The scrollable area
 *	  3. The status bar(s).
 *	the addition of the new window will make the total size of all
 *	windows greater than the size of the screen.
 *	The recalculate_windows() function, which is called whenever you
 *	resize the screen, will figure out how to make everything fit.
 */
static int	add_to_window_list (int screen_, int window_)
{
	Window	*biggest = (Window *) 0,
		*tmp,
		*winner;
	int	size, need;
	int     skip_fixed;
	Window *new_w;

	if (!window_is_valid(window_))
		return 0;
	new_w = get_window_by_refnum_direct(window_);

	if (screen_ < 0)
		panic(1, "add_to_window_list: Cannot add window [%d] to NULL screen.", get_window_user_refnum(window_));

	/*
	 * XXX There should be sanity checks here:
	 *   1. "new_w->screen" should be NULL
	 *	1b. If it is not NULL, it should be in screen->_window_list.
	 *	1c. If it is NULL, it should be on hidden_list.
	 */

	/*
	 * Hidden windows "notify" (%F) or "activity" (%E) when they
	 * have output while hidden; but when they're made visible,
	 * they're no longer eligible for notification.
	 */
	new_w->notified = 0;
	new_w->current_activity = 0;

	/*
	 * If this is the first window to go on the screen
	 */
	if (get_screen_window_list(screen_) == -1)
	{
		set_screen_visible_windows_incr(screen_);
		new_w->screen_ = screen_;
		set_screen_window_list(screen_, new_w->refnum);
		new_w->my_columns = get_screen_columns(screen_);
		if (screen_add_window_first(screen_, new_w->refnum) == 0)
			yell("screen_add_window_first(%d, %d) failed!", screen_, new_w->refnum);

		if (dumb_mode)
		{
			new_w->display_lines = 24;
			set_screens_current_window(screen_, new_w->refnum);
			return new_w->refnum;
		}
		recalculate_windows(screen_);
		return new_w->refnum;
	}

	/*
	 * This is not the first window on this screen.
	 */

	/*
	 * DECISION 1 -- Which window to "split" ?
	 */

	/*
	 * Determine the "BIGGEST WINDOW"
	 */

	/*
	 * Find the smallest nonfixed window and hide it.
	 */
	biggest = NULL; 
	size = -1;
	for (skip_fixed = 1; skip_fixed >= 0; skip_fixed--)
	{
		for ((tmp = get_window_by_refnum_direct(get_screen_window_list(screen_))); tmp; tmp = get_window_by_refnum_direct(get_window_next(tmp->refnum)))
		{
			if (skip_fixed && tmp->fixed_size)
				continue;
			if (tmp->display_lines > size)
			{
				size = tmp->display_lines;
				biggest = tmp;
			}
		}
		if (biggest)
			break;
	}

	if (!biggest)
	{
		say("I couldn't find a window to split -- sorry");
		return 0;
	}

	/*
	 * Determine if the biggest window is big enough to hold
	 * the new window.  If it is not, then we have to fail.
	 */
	need = new_w->toplines_showing + new_w->status.number;
	if (biggest->display_lines < need)
	{
		say("Not enough room for another window!");
		/* remove_window_from_screen(new_w, 1); */
		return 0;
	}

	/*
	 * Use the biggest window -- unless the current window can hold
	 * it and /set always_split_biggest is OFF.
	 */
	winner = biggest;
	if (get_window_by_refnum_direct(get_screen_input_window(screen_))->display_lines > need)
		if (get_int_var(ALWAYS_SPLIT_BIGGEST_VAR) == 0)
			winner = get_window_by_refnum_direct(get_screen_input_window(screen_));

	/*
	 * The window is always put ABOVE the winner!
	 */
	int	winner_refnum = winner->refnum;
	int	old_winner_prev;

	old_winner_prev = get_window_prev(winner_refnum);

	if (!screen_add_window_before(get_window_screennum(winner_refnum), winner_refnum, new_w->refnum))
		yell("screen_add_window_before(%d, %d, %d) failed", get_window_screennum(winner_refnum), winner_refnum, new_w->refnum);
	set_window_prev(new_w->refnum, old_winner_prev);
	set_window_next(new_w->refnum, winner_refnum);

	if (old_winner_prev == -1)
		set_screen_window_list(screen_, new_w->refnum);
	else
		set_window_next(old_winner_prev, new_w->refnum);

	set_window_prev(winner_refnum, new_w->refnum);


	/*
	 * Figure out how to share the space.
	 * Start with the display_lines of the split window.
	 * Then subtract the overhead lines of the new window.
	 * Then whatever's left, split in half.
	 */
	need = biggest->display_lines;
	need -= new_w->toplines_showing;
	need -= new_w->status.number;

	/* Need had better not be negative -- we checked for that above */
	if (need < 0)
		panic(1, "add_to_window_list: orig_size is %d, need is %d", 
				biggest->display_lines, need);

	/* Split the remainder among the two windows */
	winner->display_lines = need / 2;
	new_w->display_lines = need - (need / 2);

	/* Now point it to the screen.... */
	new_w->screen_ = screen_;
	set_screen_visible_windows_incr(screen_);
	new_w->my_columns = get_screen_columns(screen_);

	/* Now let recalculate_windows() handle any overages */
	recalculate_windows(screen_);

	return new_w->refnum;
}

/*
 * remove_window_from_screen: this removes the given window from the list of
 * visible windows.  It closes up the hole created by the windows abnsense in
 * a nice way.  The window passed to this function *must* be visible.
 *
 * If 'hide' is 1, then the window is added to the invisible list before
 * the current window is reset -- this avoids a possible panic in the
 * /on switch_windows thrown there.  If 'hide' is 0, then the window is
 * just unlinked and we assume the caller will gc it.
 */
static void 	remove_window_from_screen (int window_, int hide, int recalc)
{
	int	s_;
	int	old_next, old_prev;

	if (!window_is_valid(window_))
		return;

	if (get_window_screennum(window_) < 0)
		panic(1, "remove_window_from_screen: This window is not on a screen");
	s_ = get_window_screennum(window_);

	/*
	 * We  used to go to greath lengths to figure out how to fill
	 * in the space vacated by this window.  Now we dont sweat that.
	 * we just blow away the window and then recalculate the entire
	 * screen.
	 */
	old_next = get_window_next(window_);
	old_prev = get_window_prev(window_);

	if (screen_remove_window(s_, window_) == 0)
		yell("screen_remove_window(%d, %d) failed!", get_window_screennum(window_), window_);

	if (old_prev != -1)
		set_window_next(old_prev, old_next);
	else
		set_screen_window_list(s_, old_next);

	if (old_next != -1)
		set_window_prev(old_next, old_prev);

	set_screen_visible_windows_dec(s_);
	if (get_screen_visible_windows(s_) == 0)
		return;

	if (hide)
		add_to_invisible_list(window_);

	if (get_screen_input_window(s_) == window_)
	{
		set_screen_input_window(s_, -1);
		set_screens_current_window(s_, 0);
	}

	if (get_screen_last_window_refnum(s_) == window_)
		set_screen_last_window_refnum(s_, get_screen_input_window(s_));

	if (get_screen_input_window(s_) == window_)
		make_window_current_by_refnum(get_screen_window_list(last_input_screen));
	else
		make_window_current_by_refnum(0);

	if (recalc)
		recalculate_windows(s_);
}


/* * * * * * * * * * * * SIZE AND LOCATION PRIMITIVES * * * * * * * * * * * */
/*
 * recalculate_window_positions: This runs through the window list and
 * re-adjusts the top and bottom fields of the windows according to their
 * current positions in the window list.  This doesn't change any sizes of
 * the windows 
 */
static void	recalculate_window_positions (int screen_)
{
	int	window_ = 0;
	short	top;

	if (screen_ < 0)
		return;		/* Window is hidden.  Dont bother */

	top = 0;
	while (traverse_all_windows_on_screen2(&window_, screen_))
	{
		Window	*w = NULL;

		w = get_window_by_refnum_direct(window_);
		top += w->toplines_showing;
		w->top = top;
		w->bottom = top + w->display_lines;
		top += w->display_lines + w->status.number;

		window_body_needs_redraw(w->refnum);
		window_statusbar_needs_redraw(w->refnum);
	}
}

/*
 * swap_window: Replace a visible window with an invisible window
 *
 * Arguments:
 *	v_window_	- The refnum of a visible window.
 *	window_		- A valid hidden window refnum 
 *			  If not a valid hidden window refnum (either <= 0 or 
 *			  just invalid), silently replaced with an unspecified
 *			  hidden window (usually the LAST window)
 *
 * Caveats:
 *	This function tries to avoid recoverable failures.  If the window
 *	to be swapped in is unacceptable, an acceptable alternative will
 *	be chosen rather than failing the operation.
 *	
 * Errors:
 *	- If there are no hidden windows, obviously you can't swap one in
 *	- If v_window_ is not visible
 *	- If window_ is not invisible (it tries to avoid this)
 *	- If v_window_ is not swappable
 *	- If window_ is not swappable and there are other hidden windows
 *		Note that in situations where you didn't select window_,
 *		the client may select an unswappable window and then fail on 
 *		it, rather than looking for a hidden window that wouldn't fail.
 *		Allowing this to fail could be considered a bug
 *
 *
 *	If "window_" is not a valid hidden window, then a hidden window
 *	will be selected to avoid failure.
 *
 * swap_window: This swaps the given window with the current window.  The
 * window passed must be invisible.  Swapping retains the positions of both
 * windows in their respective window lists, and retains the dimensions of
 * the windows as well 
 */
static	void	swap_window (int v_window_, int window_)
{
	int	check_hidden = 1;
	int	recalculate_everything = 0;
	Window *v_window, *window;
	int	screen_;
	int	v_window_current;
	int	v_window_prev, v_window_next;

	v_window = get_window_by_refnum_direct(v_window_);

	/*
	 * v_window -- window to be swapped out
	 * window -- window to be swapped in
	 */

	/* Find any invisible window to swap in.  Prefer swappable ones */
	if (window_ <= 0)
	{
		window = NULL;
		window_ = 0;
		while (traverse_all_windows_on_screen2(&window_, -1))
		{
			if (get_window_swappable(window_))
			{
				window = get_window_by_refnum_direct(window_);
				break;
			}
		}
	}
	else
		window = get_window_by_refnum_direct(window_);

	if (!window && get_invisible_list() >= 1)
	{
		check_hidden = 0;
		window_ = get_invisible_list();
		window = get_window_by_refnum_direct(window_);
	}
	if (!window)
	{
		say("The window to be swapped in does not exist.");
		return;
	}

	screen_ = get_window_screennum(v_window_);
	if (v_window_ == current_window_)
		v_window_current = 1;
	else
		v_window_current = 0;

	v_window_prev = get_window_prev(v_window_);
	v_window_next = get_window_next(v_window_);

	/*
	 * THE VISIBLE WINDOW MUST BE VISIBLE
	 * THE HIDDEN WINDOW MUST BE INVISIBLE
	 */
	if (get_window_screennum(window_) >= 0 || get_window_screennum(v_window_) < 0)
	{
		say("You can only SWAP a hidden window with a visible window.");
		return;
	}

	/*
	 * THE TWO WINDOWS MUST BE SWAPPABLE 
	 */
	if (!v_window->swappable)
	{
		if (v_window->name)
			say("Window %s is not swappable", v_window->name);
		else
			say("Window %d is not swappable", v_window->user_refnum);
		return;
	}
	if (check_hidden && !window->swappable)
	{
		if (window->name)
			say("Window %s is not swappable", window->name);
		else
			say("Window %d is not swappable", window->user_refnum);
		return;
	}

	/*
	 * ADD THE CURRENTLY VISIBLE WINDOW TO THE INVISIBLE LIST
	 * REMOVE THE CURRENTLY HIDDEN WINDOW FROM THE INVISIBLE LIST
	 */
	set_screen_last_window_refnum(screen_, v_window_);
	remove_from_invisible_list(window_);

	/*
	 * Give the window to be swapped in the same geometry as the window
	 * to be swapped out, mark it as being visible, give it its screen,
	 * and if the window being swapped out is curr_win, then the window
	 * to be swapped in will be curr_win.
	 */
	window->top = v_window->top - v_window->toplines_showing + 
					window->toplines_showing;
	/* XXX Manually resetting window's size? Ugh */
	window->display_lines = v_window->display_lines + 
			          v_window->status.number - 
				    window->status.number +
			          v_window->toplines_showing - 
				    window->toplines_showing;
	window->bottom = window->top + window->display_lines;
	window->screen_ = get_window_screennum(v_window->refnum);

	if (window->display_lines < 0)
	{
		window->display_lines = 0;
		recalculate_everything = 1;
	}

	/*
	 * IF THE CURRENTLY VISIBLE WINDOW IS THE LAST INPUT WINDOW
	 * Make the currently hidden window the last input window
	 */
	if (get_screen_input_window(screen_) == (int)v_window_)
	{
		set_screen_input_window(screen_, window_);
		ensure_window_priority(window_);
	}

	/*
	 * Hook 'window' into the screen, first set window's prev
	 */
	if (v_window_prev != -1)
	{
		set_window_prev(window_, v_window_prev);
		set_window_next(v_window_prev, window_);
	}
	else
	{
		set_window_prev(window_, -1);
		set_screen_window_list(screen_, window_);
	}

	/* 
	 * Then set window's next
	 */
	if (v_window_next != -1)
	{
		set_window_next(window_, v_window_next);
		set_window_prev(v_window_next, window_);
	}
	else
		set_window_next(window_, -1);

	/*
	 * Hide the window to be swapped out
	 */
	if (!v_window->deceased)
		add_to_invisible_list(v_window_);

	screen_window_swap(screen_, v_window_, window_);

	if (recalculate_everything)
		recalculate_windows(screen_);
	recalculate_window_cursor_and_display_ip(window_);
	resize_window_display(window_);
	window_check_columns(window_);

	/*
	 * And recalculate the window's positions.
	 */
	window_body_needs_redraw(window_);
	window_statusbar_needs_redraw(window_);
	window->notified = 0;
	window->current_activity = 0;

	/*
	 * Transfer current_window if the current window is being swapped out
	 */
	if (v_window_current)
		make_window_current_by_refnum(window_);
}

/*
 * move_window_to: This moves a given window to the Nth absolute position 
 * on the screen.  All the other windows move accordingly.
 *
 * XXX I really do apologize for this being a mess.  Clang insists that
 * there is a null deref if i do it the simple/clever way, and so in order
 * to prove that everything is on the up-and-up, i did it this way instead.
 * I left lots of comments to convince myself it was correct.  Once this 
 * code has proven itself, I can probably clean it up by removing the most
 * obvious ones.
 */
static void 	move_window_to (int window_, int offset)
{
	int	s_;

	if (!window_is_valid(window_))
		return;

	/* /window move_to -1 is bogus -- but maybe it shouldn't be.  */
	if (offset <= 0)
		return;

	/* You can't /window move_to on an invisible window */
	s_ = get_window_screennum(window_);
	if (!screen_is_valid(s_))
		return;		/* Whatever */

	/* This is impossible -- just a sanity check for clang's benefit */
	if (get_screen_window_list(s_) < 0)
		panic(1, "window_move_to: Screen for window %d has no windows?", get_window_user_refnum(window_));

	/* You can't /window move_to if there are no split windows */
	if (get_screen_visible_windows(s_) == 1)
		return;

	/* This is impossible -- just a sanity check for clang's benefit */
	if (get_window_prev(window_) == -1 && get_window_next(window_) == -1)
		panic(1, "window_move_to: window %d is has no prev or next; but its screen says it has %d windows", 
			get_window_user_refnum(window_), get_screen_visible_windows(s_));

	/* Move the window to the top of the screen */
	if (offset == 1)
	{
		/* If it's already at the top, we're done. */
		if (get_window_refnum(get_screen_window_list(s_)) == get_window_refnum(window_))
			return;

		if (get_window_prev(window_) == -1)
			panic(1, "window_move_to(top): Window %d prev is NULL, "
				"but s->_window_list is %d", 
				get_window_user_refnum(window_), get_window_user_refnum(get_screen_window_list(s_)));

		set_window_list_check(window_, 0);

		set_window_next(get_window_prev(window_), get_window_next(window_));
		if (get_window_next(window_) != -1)
			set_window_prev(get_window_next(window_), get_window_prev(window_));

		set_window_prev(window_, -1);
		set_window_next(window_, get_screen_window_list(s_));
		set_window_prev(get_screen_window_list(s_), window_);
		set_screen_window_list(s_, window_);

		set_window_list_check(window_, 1);
	}

	/* Move the window to the bottom of the screen */
	else if (offset >= get_screen_visible_windows(s_))
	{
		/* If it's already at the bottom, we're done. */
		if (get_window_next(window_) == -1)
			return;

		set_window_list_check(window_, 0);

		if (get_window_prev(window_) != -1)
			set_window_next(get_window_prev(window_), get_window_next(window_));
		else
			set_screen_window_list(s_, get_window_next(window_));
		set_window_prev(get_window_next(window_), get_window_prev(window_));

		set_window_prev(window_, get_screen_bottom_window(s_));
		set_window_next(get_window_prev(window_), window_);
		set_window_next(window_, -1);

		set_window_list_check(window_, 1);
	}

	/* Otherwise it's moving somewhere in the middle */
	else
	{
		int	w_ = 0;
		int	i;

		/* In order to make the window the Nth window,
		 * We need to have a pointer to the N-1th window.
		 * We know that it won't be the top or bottom.
		 */
		i = 0;
		while (traverse_all_windows_on_screen2(&w_, s_))
		{
			if (get_window_refnum(w_) != get_window_refnum(window_))
				i++;		/* This is the I'th window */
			if (i + 1 == offset)
				break;		/* This is the "prev" window! */
		}

		/* XXX This is an error and I should do something here */
		if (w_ < 1)
			return;

		/* 
		 * 'w_' is our "prev" window.
		 * So if window is already in the correct place, we're done 
		 */
		if (get_window_refnum(get_window_next(w_)) == get_window_refnum(window_))
			return;

		set_window_list_check(window_, 0);

		/* Unlink our target window first */
		if (get_window_prev(window_) != -1)
			set_window_next(get_window_prev(window_), get_window_next(window_));
		else
			set_screen_window_list(s_, get_window_next(window_));

		if (get_window_next(window_) != -1)
			set_window_prev(get_window_next(window_), get_window_prev(window_));

		set_window_prev(window_, w_);
		set_window_next(window_, get_window_next(w_));

#if 0
		/* One last sanity check */
		if (get_window_prev(window_) == -1 || get_window_next(window_) == -1)
			panic(1, "window_move_to(%d): Window %d's prev and "
				"next are both null, but that's impossible", 
				offset, get_window_user_refnum(window_));
#endif

		set_window_prev(get_window_next(window_), window_);
		set_window_next(get_window_prev(window_), window_);

		set_window_list_check(window_, 1);
	}

#if 0
	yell("screen_window_place(%d, %d, %d)", s_, offset, window_);
#endif
	screen_window_place(s_, offset, window_);
	set_screens_current_window(s_, window_);
	make_window_current_by_refnum(window_);
	recalculate_window_positions(s_);
}

/*
 * move_window: This moves a window offset positions in the window list. This
 * means, of course, that the window will move on the screen as well 
 */
static void 	move_window (int window_, int offset)
{
	int	s_;
	int	location;
	int	w_ = 0;

	if (!window_is_valid(window_))
		return;

	if ((s_ = get_window_screennum(window_)) < 0)
		return;
	if (get_screen_visible_windows(s_) == 0)
		return;		/* Sigh */

	offset = offset % get_screen_visible_windows(s_);
	if (offset == 0)
		return;

	location = 1;
	while (traverse_all_windows_on_screen2(&w_, s_))
	{
		if (get_window_refnum(w_) == get_window_refnum(window_))
			break;
		else
			location++;
	}
	if (w_ < 1)
		panic(1, "move_window: I couldn't find window %d on its "
			"own screen!", get_window_user_refnum(window_));

	/* OK, so 'window' is the 'location'th window. */
	location += offset;
	if (location < 1)
		location += get_screen_visible_windows(s_);
	if (location > get_screen_visible_windows(s_))
		location -= get_screen_visible_windows(s_);
	
	move_window_to(window_, location);
}

/*
 * resize_window: if 'how' is RESIZE_REL, then this will increase or decrease
 * the size of the given window by offset lines (positive offset increases,
 * negative decreases).  If 'how' is RESIZE_ABS, then this will set the 
 * absolute size of the given window.
 * Obviously, with a fixed terminal size, this means that some other window
 * is going to have to change size as well.  Normally, this is the next
 * window in the window list (the window below the one being changed) unless
 * the window is the last in the window list, then the previous window is
 * changed as well 
 */
static 	void 	resize_window (int how, int window_, int offset)
{
	int	other_;
	int	window_size,
		other_size;

	if (!window_is_valid(window_))
		return;

	if (get_window_screennum(window_) < 0)
	{
		say("You cannot change the size of hidden windows!");
		return;
	}

	if (how == RESIZE_ABS)
		offset -= get_window_display_lines(window_);

	other_ = window_;
	do
	{
		if (get_window_next(other_) != -1)
			other_ = get_window_next(other_);
		else
			other_ = get_screen_window_list(get_window_screennum(window_));

		if (other_ == window_)
		{
			say("Can't change the size of this window!");
			return;
		}

		if (get_window_fixed_size(other_))
			continue;
	}
	while (get_window_display_lines(other_) < offset);

	window_size = get_window_display_lines(window_) + offset;
	other_size = get_window_display_lines(other_) - offset;

	if ((window_size < 0) || (other_size < 0))
	{
		say("Not enough room to resize this window!");
		return;
	}

	set_window_display_lines(window_, window_size);
	set_window_display_lines(other_, other_size);
	recalculate_windows(get_window_screennum(window_));
}

/*
 * resize_display: This is called any time "window->display_lines" and
 *   "window->old_display_lines" disagree.  You are supposed to be able to
 *   change "window->display_lines" and then update_all_windows() comes along
 *   later and notices that display_lines and old_display_lines disagree and
 *   calls us to straighten things out.
 *
 * So what we do here is two-fold.  We make sure that the top of the window's
 * "normal" view is moved back (or left alone if /window scrolladj off), or
 * forward if the window is grown or shrunk, respectively.  Then we check to 
 * make sure the scrollback buffer is reset to be at least twice the window's 
 * new size. 
 * 
 * The entire window gets redrawn after we're done, no matter what.
 */
static void	resize_window_display (int window_)
{
	int		cnt = 0, i;
	Display 	*tmp;
	Window *	window;

	if (dumb_mode)
		return;

	if (!window_is_valid(window_))
		return;

	window = get_window_by_refnum_direct(window_);

	/*
	 * Find out how much the window has changed by
	 */
	cnt = window->display_lines - window->old_display_lines;
	tmp = window->scrolling_top_of_display;

	/*
	 * If it got bigger, move the scrolling_top_of_display back.
	 */
	if (cnt > 0)
	{
	    /* 
	     * If "SCROLLADJUST" is off, then do not push back the top of
	     * display to reveal what has previously scrolled off (for
	     * ircII compatability)
	     */
	    if (window->scrolladj)
	    {
		for (i = 0; i < cnt; i++)
		{
			Display *cp;

			if (!tmp || !tmp->prev)
				break;
			cp = get_window_clear_point(window_);
			if (tmp == cp)
				break;
			tmp = tmp->prev;
		}
	    }
	}

	/*
	 * If it got smaller, then move the scrolling_top_of_display up
	 */
	else if (cnt < 0)
	{
		/* Use any whitespace we may have lying around */
		cnt += (window->old_display_lines - 
			window->scrolling_distance_from_display_ip);
		for (i = 0; i > cnt; i--)
		{
			if (tmp == window->display_ip)
				break;
			tmp = tmp->next;
		}
	}
	window->scrolling_top_of_display = tmp;
	if (window->display_buffer_max < window->display_lines * 2)
		window->display_buffer_max = window->display_lines * 2;
	recalculate_window_cursor_and_display_ip(window->refnum);

	/* XXX - This is a temporary hack, but it works. */
	if (window->scrolling_distance_from_display_ip >= window->display_lines)
		unclear_window(window->refnum);

	/*
	 * Mark the window for redraw and store the new window size.
	 */
	window_body_needs_redraw(window->refnum);
	window_statusbar_needs_redraw(window->refnum);
	window->old_display_lines = window->display_lines;
	return;
}


/* * * * * * * * * * * * WINDOW UPDATING AND RESIZING * * * * * * * * * */
/*
 * THese three functions are the one and only functions that are authorized
 * to be used to declare that something needs to be updated on the screen.
 */

void	window_scrollback_needs_rebuild (int window)
{
	Window *w = get_window_by_refnum_direct(window);
	debuglog("window_scrollback_needs_rebuild(%d)", w->user_refnum);
	w->rebuild_scrollback = 1;
}

/*
 * statusbar_needs_update
 */
void	window_statusbar_needs_update (int refnum)
{
	Window *w = get_window_by_refnum_direct(refnum);
	debuglog("window_statusbar_needs_update(%d)", w->user_refnum);
	w->update |= UPDATE_STATUS;
}

/*
 * statusbar_needs_redraw
 */
static void	window_statusbar_needs_redraw (int refnum)
{
	Window *w = get_window_by_refnum_direct(refnum);
	debuglog("window_statusbar_needs_redraw(%d)", w->user_refnum);
	w->update |= REDRAW_STATUS;
}

/*
 * window_body_needs_redraw
 */
static void	window_body_needs_redraw (int refnum)
{
	Window *w = get_window_by_refnum_direct(refnum);
	debuglog("window_body_needs_redraw(%d)", w->user_refnum);
	w->cursor = -1;
}

/*
 * redraw_all_windows: This basically clears and redraws the entire display
 * portion of the screen.  All windows and status lines are draws.  This does
 * nothing for the input line of the screen.  Only visible windows are drawn 
 */
void 	redraw_all_windows (void)
{
	int	refnum = 0;

	if (dumb_mode)
		return;

	while (traverse_all_windows2(&refnum)) {
		window_body_needs_redraw(refnum);
		window_statusbar_needs_redraw(refnum);
	}
}

/*
 * update_all_status_kb: calls update_all_status as a keybinding
 */
BUILT_IN_KEYBINDING(update_all_status_kb)
{
	update_all_status();
}

/*
 * update_all_status: This performs a logical "update_window_status" on
 * every window for the current screen.
 */
void 	update_all_status (void)
{
	int	refnum = 0;

	while (traverse_all_windows2(&refnum)) {
		window_statusbar_needs_update(refnum);
	}
}

/*
 * update_all_windows: This goes through each visible window and draws the
 * necessary portions according the the update field of the window. 
 */
void 	update_all_windows (void)
{
static	int	recursion = 0;
static	int	do_input_too = 0;
static	int	restart = 0;
	int	window_;

	if (recursion)
	{
		debuglog("update_all_windows: recursion");
		restart = 1;
		return;
	}

	recursion++;
	for (window_ = 0; traverse_all_windows2(&window_); )
	{
		Window *tmp = get_window_by_refnum_direct(window_);

		/* 
		 * "Window updates" come in two flavors, which we don't 
		 * differentiate between
		 *  1. Logical changes
		 *  2. Physical changes
		 * Some updates implicate both.  
		 *
		 * We cannot do physical updates on hidden windows (since 
		 * they are not visible), so this function first does the
		 * logical updates and conditionally the physical updates.
		 */
		if (restart)
		{
			debuglog("update_all_windows: restarting");
			restart = 0;
			tmp = NULL;
			continue;
		}

		/*
		 * Logical update Type 1 - Scrollback rebuilds
		 */
		if (tmp->rebuild_scrollback)
		{
			debuglog("update_all_windows(%d), rebuild scrollback",
					tmp->user_refnum);
			rebuild_scrollback(window_);
		}

		/* 
		 * Logical update Type 2 - Adjust the window's views
		 * (when the number of rows changes)
		 */
		if (tmp->display_lines != tmp->old_display_lines)
		{
			debuglog("update_all_windows(%d), resize window display",
					tmp->user_refnum);
			resize_window_display(window_);
		}

		/*
		 * Logical update Type 3 - Recalculating the status bar
		 */
		if ((tmp->update & REDRAW_STATUS) || (tmp->update & UPDATE_STATUS))
		{
		    do {
			int	status_changed;

			debuglog("update_all_windows(%d), regen status", tmp->user_refnum);
			status_changed = make_status(tmp->refnum, &tmp->status);

			/* If they are both set, REDRAW takes precedence. */
			if ((tmp->update & REDRAW_STATUS) && (tmp->update & UPDATE_STATUS))
				tmp->update &= (~UPDATE_STATUS);

			/* 
			 * The difference between UPDATE_STATUS and REDRAW_STATUS
			 * is that UPDATE_STATUS does not force the status to be 
			 * redrawn unless it has changed.   REDRAW_STATUS will 
			 * always do the output even if nothing has changed.
			 */
			if ((tmp->update & UPDATE_STATUS) && status_changed <= 0)
			{
				debuglog("update_all_windows(%d), UPDATE_STATUS -> no action taken", tmp->user_refnum);
				break;
			}

			debuglog("update_all_windows(%d): FORCE_STATUS set", tmp->user_refnum);
			tmp->update |= FORCE_STATUS;
			tmp->update &= (~UPDATE_STATUS);
			tmp->update &= (~REDRAW_STATUS);
		    } while (0);
		}

		/* * * * * * * */
		/*
		 * This now gates on visible/physical windows.
		 * Everything after this point should be only physical updates
		 */
		if (get_window_screennum(tmp->refnum) < 0)
		{
			tmp->update &= (~FORCE_STATUS);
			tmp->update &= (~REDRAW_STATUS);
			tmp->update &= (~UPDATE_STATUS);
			debuglog("update_all_windows(%d), hidden (not redrawn)", tmp->user_refnum);
			continue;
		}

		/*
		 * Physical update #1 - Redraw the entire window
		 */
		if (tmp->cursor == -1 ||
		   (tmp->cursor < tmp->scrolling_distance_from_display_ip  &&
			 tmp->cursor < tmp->display_lines))
		{
			debuglog("update_all_windows(%d), window repainted", tmp->user_refnum);
			repaint_window_body(tmp->refnum);
		}

		/*
		 * Physical update #2 - Redraw the status bar
		 *  -- Note: FORCE_STATUS handles only the physical redraw
		 *     of the status bar.  This can be manually triggered
		 *     or automatically (via (UPDATE|REDRAW)_STATUS)
		 */
		if (tmp->update & FORCE_STATUS)
		{
			/* If redrawing failed this time, try next time */
			if (redraw_status(tmp->refnum, &tmp->status) < 0)
			{
				debuglog("update_all_windows(%d) (redraw_status), OK, status not redrawn -- lets try update later",
					tmp->user_refnum);
			}
			else
			{
				debuglog("update_all_windows(%d) status redrawn successfully", 
					tmp->user_refnum);
				tmp->update &= ~FORCE_STATUS;
			}
			do_input_too = 1;
		}
	}

	if (do_input_too)
	{
		do_input_too = 0;
		update_input(-1, UPDATE_ALL);
	}

	for (window_ = 0; traverse_all_windows2(&window_); )
	{
		Window *tmp = get_window_by_refnum_direct(window_);

		if (tmp->cursor > tmp->display_lines)
			panic(1, "uaw: window [%d]'s cursor [%hd] is off the display [%d]", tmp->user_refnum, tmp->cursor, tmp->display_lines);
	}

	recursion--;
}

/****************************************************************************/
/*
 * Rebalance_windows: this is called when you want all the windows to be
 * rebalanced, except for those who have a set size.
 */
static void	rebalance_windows (int screen_)
{
	int	tmp_ = 0;
	int each, extra;
	int window_resized = 0, window_count = 0;

	if (dumb_mode)
		return;

	/*
	 * Two passes -- first figure out how much we need to balance,
	 * and how many windows there are to balance
	 */
	while (traverse_all_windows_on_screen2(&tmp_, screen_))
	{
		if (get_window_fixed_size(tmp_))
			continue;
		window_resized += get_window_display_lines(tmp_);
		window_count++;
	}

	if (window_count == 0)
	{
		yell("All the windows on this screen are fixed!");
		return;
	}

	each = window_resized / window_count;
	extra = window_resized % window_count;

	/*
	 * And then go through and fix everybody
	 */
	tmp_ = 0;
	while (traverse_all_windows_on_screen2(&tmp_, screen_))
	{
		if (get_window_fixed_size(tmp_))
			;
		else
		{
			set_window_display_lines(tmp_, each);
			if (extra)
			{
				set_window_display_lines(tmp_, each + 1);
				extra--;
			}
		}
	}
	recalculate_window_positions(screen_);
}

/*
 * recalculate_windows - Ensure that all the windows on a screen take up
 *			exactly the size of the screen
 *
 * Arguments:
 *	screen	- A screen to validate
 *
 * Notes:
 *	Whenever the terminal size changes (such as an xterm), or whenever
 * 	you add or remove a window from a screen, you should call this 
 *	function to ensure the windows on the screen take all the lines.
 *
 *	This function will do whatever is necessary to make that happen,
 *	even if it means ejecting windows from the screen to make room.
 *
 *	As much as possible, this function tries to keep windows the same
 *	relative size to each other when it is done.
 */
void 	recalculate_windows (int screen_)
{
	int	old_li = 1;
	int	required_li = 0;
	int	excess_li = 0;
	Window	*tmp, *winner;
	int	window_count = 0;
	int	tmp_;

	if (dumb_mode)
		return;

	/*
	 * If its a new screen, just set it and be done with it.
	 * XXX This seems heinously bogus.
	 */
	if (get_screen_input_window(screen_) < 1)
	{
		Window *wl;
		wl = get_window_by_refnum_direct(get_screen_window_list(screen_));
		wl->top = 0;
		wl->toplines_showing = 0;
		wl->toplines_wanted = 0;
		wl->display_lines = get_screen_lines(screen_) - 1 - wl->status.number;
		wl->bottom = get_screen_lines(screen_) - 1 - wl->status.number;
		wl->my_columns = get_screen_columns(screen_);
		return;
	}

	/*
	 * This has to be done first -- if the number of columns of
	 * the screen has changed, the window needs to be told; this
	 * will provoke a full redraw (later).
	 */
	tmp_ = 0;
	while (traverse_all_windows_on_screen2(&tmp_, screen_))
		window_check_columns(tmp_);

	/*
	 * This is much more complicated than it needs to be, and I've
	 * anguished over a good algorithm that will resize the windows
	 * so that they "feel" right.  I feel strongly that windows 
	 * should stay about the same size after a resize.
	 *
	 * But there are situations where things break down, and I am
	 * not sure I have good solutions for all those.  So then things
	 * look screwy and the user distrusts epic.
	 */

	/**********************************************************
	 * TEST #1 -- Has the screen actually changed size?
	 */

	/*
	 * First, let's figure out how big the screen was before.
	 * This will allow us to calculate how many lines to give/steal.
	 */
	old_li = 0;
	tmp_ = 0;
	while (traverse_all_windows_on_screen2(&tmp_, screen_))
	{
		tmp = get_window_by_refnum_direct(tmp_);
		old_li += tmp->status.number + tmp->toplines_showing + tmp->display_lines;
	}

	/* How many lines did the screen change by? */
	excess_li = (get_screen_lines(screen_) - 1) - old_li;

	/* 
	 * If the size of the screen hasn't changed,
	 * Re-enumerate the windows, and go on our way.
	 */
	if (excess_li == 0)
	{
		recalculate_window_positions(screen_);
		return;
	}


	/**********************************************************
	 * TEST #2 -- Can we fit all the windows on the screen?
	 */

	/* 
	 * Next, let's figure out how many lines MUST be shown.
	 */
	required_li = 0;
	tmp_ = 0;
	while (traverse_all_windows_on_screen2(&tmp_, screen_))
	{
		tmp = get_window_by_refnum_direct(tmp_);
		required_li += tmp->status.number + tmp->toplines_showing +
				(tmp->fixed_size ? tmp->display_lines : 0);
	}

	/*
	 * If the number of required lines exceeds what we have on hand,
	 * then we've got problems.  We fix this by removing one window,
	 * then recursively "fixing" the screen from there. 
	 */
	if (required_li > get_screen_lines(screen_) - 1)
	{
		int     skip_fixed;

		/*
		 * Find the smallest nonfixed window and hide it.
		 */
		winner = NULL;     /* Winner? ha~! */
		for (skip_fixed = 1; skip_fixed >= 0; skip_fixed--)
		{
			tmp_ = 0;
			while (traverse_all_windows_on_screen2(&tmp_, screen_))
			{
				tmp = get_window_by_refnum_direct(tmp_);
				if (skip_fixed && tmp->fixed_size)
					continue;
				if (!winner || 
					(tmp->status.number + 
					 tmp->toplines_showing + 
					 tmp->display_lines > 
						winner->status.number +
						winner->toplines_showing +
						winner->display_lines))
					winner = tmp;
			}

			if (winner)
				break;
		}

		if (!winner)
			panic(1, "recalc_windows: Could not find window to hide");

		remove_window_from_screen(winner->refnum, 1, 0);
		recalculate_windows(screen_);
		return;
	}


	/***********************************************************
	 * TEST #3 -- Has the screen grown?
	 */
	/* Count the non-fixed windows */
	window_count = 0;
	tmp_ = 0;
	while (traverse_all_windows_on_screen2(&tmp_, screen_))
	{
		if (get_window_fixed_size(tmp_))
			continue;
		window_count++;
	}

	/*
	 * If the screen is growing, dole out lines
	 */
	if (excess_li > 0)
	{
		int	assigned_lines;

		assigned_lines = 0;
		while (assigned_lines < excess_li)
		{
			/*
			 * XXX TODO - Placeholder algorithm
			 * For now, add one line to each nonfixed window,
			 * one at a time, until we've given them all out.
			 */
			tmp_ = 0;
			while (traverse_all_windows_on_screen2(&tmp_, screen_))
			{
				tmp = get_window_by_refnum_direct(tmp_);

				/* 
				 * If this is a fixed window, and there is 
				 * another window, then skip it.
				 */
				if (tmp->fixed_size && window_count)
					continue;

				tmp->display_lines++;
				assigned_lines++;

				if (assigned_lines >= excess_li)
					break;
			}
		}
	}

	else	/* excess_li < 0 */
	{
		int	deducted_lines;

		deducted_lines = 0;
		while (deducted_lines > excess_li)
		{
			/*
			 * XXX TODO - Placeholder algorithm
			 * For now, steal one line to each nonfixed window,
			 * one at a time, until we've given them all out.
			 */
			tmp_ = 0;
			while (traverse_all_windows_on_screen2(&tmp_, screen_))
			{
				tmp = get_window_by_refnum_direct(tmp_);

				/* 
				 * If this is a fixed window, and there is 
				 * another window, then skip it.
				 */
				if (tmp->fixed_size && window_count)
					continue;

				if (tmp->display_lines == 0)
					continue;

				tmp->display_lines--;
				deducted_lines--;

				if (deducted_lines <= excess_li)
					break;
			}
		}
	}

	/* Re-enumerate the windows and go on with life. */
	recalculate_window_positions(screen_);
}

static	void	window_check_columns (int refnum)
{
	Window *w;

	if (!window_is_valid(refnum))
		return;

	w = get_window_by_refnum_direct(refnum);
	if (get_window_screennum(refnum) >= 0 && w->my_columns != get_screen_columns(get_window_screennum(refnum)))
	{
		w->my_columns = get_screen_columns(get_window_screennum(refnum));
		w->rebuild_scrollback = 1;
	}
}

static	void	rebuild_scrollback (int refnum)
{
	intmax_t	scrolling, holding, scrollback, clearpoint;
	Window *w;

	if (!window_is_valid(refnum))
		return;
	w = get_window_by_refnum_direct(refnum);

	save_window_positions(refnum, &scrolling, &holding, &scrollback, &clearpoint);
	flush_scrollback(refnum, 0);
	reconstitute_scrollback(refnum);
	restore_window_positions(refnum, scrolling, holding, scrollback, clearpoint);
	w->rebuild_scrollback = 0;
	do_hook(WINDOW_REBUILT_LIST, "%d", get_window_user_refnum(refnum));
}

static void	save_window_positions (int window_, intmax_t *scrolling, intmax_t *holding, intmax_t *scrollback, intmax_t *clearpoint)
{
	Window *w;

	if (!window_is_valid(window_))
	{
		*scrolling = -1;
		*holding = -1;
		*scrollback = -1;
		*clearpoint = -1;
		return;
	}

	w = get_window_by_refnum_direct(window_);

	if (w->scrolling_top_of_display)
		*scrolling = w->scrolling_top_of_display->linked_refnum;
	else
		*scrolling = -1;

	if (w->holding_top_of_display)
		*holding = w->holding_top_of_display->linked_refnum;
	else
		*holding = -1;

	if (w->scrollback_top_of_display)
		*scrollback = w->scrollback_top_of_display->linked_refnum;
	else
		*scrollback = -1;

	if (w->clear_point)
		*clearpoint = w->clear_point->linked_refnum;
	else
		*clearpoint = -1;
}

static void	restore_window_positions (int window_, intmax_t scrolling, intmax_t holding, intmax_t scrollback, intmax_t clearpoint)
{
	Display *d;
	Window *w;

	if (!window_is_valid(window_))
		return;

	w = get_window_by_refnum_direct(window_);

	/* First, we cancel all three views. */
	w->scrolling_top_of_display = NULL;
	w->holding_top_of_display = NULL;
	w->scrollback_top_of_display = NULL;
	w->clear_point = NULL;

	/* 
	 * Then we find the FIRST scrollback item that is linked to the
	 * corresponding lastlog saved position.  The lastlog refnum -1 is
	 * guaranteed never to match any valid scrollback item, so -1 is used
	 * to ensure we do not set the corresponding view.
	 */
	for (d = w->top_of_scrollback; d != w->display_ip; d = d->next)
	{
	    if (d->linked_refnum == scrolling && !w->scrolling_top_of_display)
		w->scrolling_top_of_display = d;
	    if (d->linked_refnum == holding && !w->holding_top_of_display)
		w->holding_top_of_display = d;
	    if (d->linked_refnum == scrollback && !w->scrollback_top_of_display)
		w->scrollback_top_of_display = d;
	    if (d->linked_refnum == clearpoint && !w->clear_point)
		w->clear_point = d;
	}

	/*
	 * If we didn't restore a view, and we were expecting to (we expect
	 * to if the refnum is not -1), then forcibly reset it to the top of
	 * the scrollback since that is as far back as we can go.
	 */
	if (!w->scrolling_top_of_display && scrolling != -1)
		w->scrolling_top_of_display = w->top_of_scrollback;
	if (!w->holding_top_of_display && holding != -1)
		w->holding_top_of_display = w->top_of_scrollback;
	if (!w->scrollback_top_of_display && scrollback != -1)
		w->scrollback_top_of_display = w->top_of_scrollback;
	if (!w->clear_point && clearpoint != -1)
		w->clear_point = w->top_of_scrollback;

	/* 
	 * We must _NEVER_ allow scrolling_top_of_display to be NULL.
	 * If it is, recalculate_window_cursor_and_display_ip() will null deref.
	 * I don't know what the right thing to do is, so I choose to
	 * move the scrolling display to the bottom (ie, /unclear).
	 * This seems the least worst hack.
	 *
	 * I changed my mind in July 2022 after talking to skered.
	 * This can happen if you resize a window that is /clear, so the 
	 * correct behavior is to _clear_ the window, not _unclear_ it.
	 */
	if (!w->scrolling_top_of_display)
		clear_window(w->refnum);

	recalculate_window_cursor_and_display_ip(w->refnum);
	if (w->scrolling_distance_from_display_ip >= w->display_lines)
		unclear_window(w->refnum);
	else if (w->scrolling_distance_from_display_ip <= w->display_lines && w->scrolladj)
		unclear_window(w->refnum);
	else
	{
		window_body_needs_redraw(w->refnum);
		window_statusbar_needs_redraw(w->refnum);
	}
}

/* * * * * * * * LOCATION AND COMPOSITION OF WINDOWS ON SCREEN * * * * * * */
/*
 * my_goto_window: This will switch the current window to the N'th window 
 * from the top of the screen.  The "which" has nothing  to do with the 
 * window's refnum, only its location on the screen.
 */
static void 	my_goto_window (int screen_, int which)
{
	int	tmp_;
	int	i;

	if (screen_ < 0 || which == 0)
		return;

	if ((which < 0) || (which > get_screen_visible_windows(screen_)))
	{
		say("GOTO: Illegal value");
		return;
	}
	i = 1;
	tmp_ = 0;
	while (traverse_all_windows_on_screen2(&tmp_, screen_))
		if (i >= which)
			break;

	set_screens_current_window(screen_, tmp_);
	make_window_current_by_refnum(tmp_);
}

/*
 * hide_window: sets the given window to invisible and recalculates remaing
 * windows to fill the entire screen 
 *
 * Returns 0 on failure, and 1 on success
 */
static int 	hide_window (int window_)
{
	if (get_window_screennum(window_) < 0)
	{
		if (get_window_name(window_))
			say("Window %s is already hidden", get_window_name(window_));
		else
			say("Window %d is already hidden", get_window_user_refnum(window_));
		return 0;
	}

	if (!get_window_swappable(window_))
	{
		if (get_window_name(window_))
			say("Window %s can't be hidden", get_window_name(window_));
		else
			say("Window %d can't be hidden", get_window_user_refnum(window_));
		return 0;
	}

	if (get_screen_visible_windows(get_window_screennum(window_)) - 
	    count_fixed_windows(get_window_screennum(window_)) <= 1)
	{
		say("You can't hide the last window.");
		return 0;
	}

	remove_window_from_screen(window_, 1, 1);
	return 1;
}

/*
 * swap_last_window:  This swaps the current window with the last window
 * that was hidden.
 * This is a keybinding.
 */
BUILT_IN_KEYBINDING(swap_last_window)
{
	if (get_invisible_list() < 1 || get_window_screennum(current_window_) < 0)
		return;

	swap_window(current_window_, get_invisible_list());
	update_all_windows();
}

/*
 * next_window: This switches the current window to the next visible window 
 * This is a keybinding.
 */
BUILT_IN_KEYBINDING(next_window)
{
	int	refnum;

	if (last_input_screen < 0)
		return;
	if (get_screen_visible_windows(last_input_screen) == 1)
		return;

	refnum = get_next_window(get_screen_input_window(last_input_screen));
	make_window_current_by_refnum(refnum);
	set_screens_current_window(last_input_screen, refnum);
	update_all_windows();
}

/*
 * swap_next_window:  This swaps the current window with the next hidden 
 * window.
 * This is a keybinding.
 */
BUILT_IN_KEYBINDING(swap_next_window)
{
	windowcmd_next(0, NULL);
	update_all_windows();
}

/*
 * previous_window: This switches the current window to the previous visible
 * window 
 * This is a keybinding
 */
BUILT_IN_KEYBINDING(previous_window)
{
	int	refnum;

	if (last_input_screen < 0)
		return;
	if (get_screen_visible_windows(last_input_screen) == 1)
		return;

	refnum = get_previous_window(get_screen_input_window(last_input_screen));
	make_window_current_by_refnum(refnum);
	set_screens_current_window(last_input_screen, refnum);
	update_all_windows();
}

/*
 * swap_previous_window:  This swaps the current window with the next 
 * hidden window.
 * This is a keybinding
 */
BUILT_IN_KEYBINDING(swap_previous_window)
{
	windowcmd_previous(0, NULL);
	update_all_windows();
}

/* show_window: This makes the given window visible.  */
static void 	show_window (int window_)
{
	int	s_;

	if (!get_window_swappable(window_))
	{
		if (get_window_name(window_))
			say("Window %s can't be made visible", get_window_name(window_));
		else
			say("Window %d can't be made visible", get_window_user_refnum(window_));
		return;
	}

	if (get_window_screennum(window_) < 0)
	{
		remove_from_invisible_list(window_);
		if ((s_ = get_window_screennum(current_window_)) < 0)
			s_ = last_input_screen;		/* What the hey */

		if (!add_to_window_list(s_, window_))
		{
			/* Ooops. this is an error. ;-) */
			add_to_invisible_list(window_);
			return;
		}
	}

	s_ = get_window_screennum(window_);
	make_window_current_by_refnum(window_);
	set_screens_current_window(s_, window_);
}




/* * * * * * * * * * * * * GETTING WINDOWS AND WINDOW INFORMATION * * * * */
/*
 * get_window_by_desc: Given either a refnum or a name, find that window
 */
static int	get_window_by_desc (const char *stuff)
{
	Window	*w = NULL;	/* bleh */
	int	window_;

	for (window_ = 0; traverse_all_windows2(&window_); )
	{
		Window *w = get_window_by_refnum_direct(window_);
		if (w->uuid && !my_stricmp(w->uuid, stuff))
			return w->refnum;
	}

	for (window_ = 0; traverse_all_windows2(&window_); )
	{
		Window *w = get_window_by_refnum_direct(window_);
		if (w->name && !my_stricmp(w->name, stuff))
			return w->refnum;
	}

	if (is_number(stuff) && (w = get_window_by_refnum_direct(my_atol(stuff))))
		return w->refnum;

	return -1;
}

static Window *get_window_by_refnum_direct (int refnum)
{
	if (refnum == 0)
		refnum = current_window_;

	if (refnum >= 1 && refnum <= INTERNAL_REFNUM_CUTOVER * 2)
	{
		if (refnum >= 1 && refnum < INTERNAL_REFNUM_CUTOVER)
		{
			if (windows[refnum] && (refnum != (int)windows[refnum]->user_refnum))
			{
				yell("Refnum (direct) %d points at window %d. fixing", refnum, windows[refnum]->user_refnum);
				windows[refnum] = NULL;
			}
		}
		else
		{
			if (windows[refnum] && (refnum != (int)windows[refnum]->refnum))
			{
				yell("Refnum (direct) %d points at window %d. fixing", windows[refnum]->refnum, refnum);
				windows[refnum] = NULL;
			}
		}
		return windows[refnum];
	}

	return NULL;
}

int	get_server_current_window (int server)
{
	int	refnum = 0;
	int	best = -1;

	while (traverse_all_windows2(&refnum))
	{
		if (get_window_server(refnum) != server)
			continue;
		if (best == -1 || get_window_priority(refnum) > get_window_priority(best))
			best = refnum;
	}
	return best;
}


/*
 * get_next_window: This overly complicated function attempts to find the
 * next non "skippable" window.  The reason for the complication is that it
 * needs to be able to deal with wrapping over to the top of the screen,
 * if the next window is at the bottom, or isnt selectable, YGTI.
 */
static	int	get_next_window  (int window_)
{
	Window *w = get_window_by_refnum_direct(window_);
	Window *last = w;
	Window *new_w = w;

	if (!w || get_window_screennum(w->refnum) < 0)
		last = new_w = w = get_window_by_refnum_direct(current_window_);

	do
	{
		if (get_window_next(new_w->refnum) != -1)
			new_w = get_window_by_refnum_direct(get_window_next(new_w->refnum));
		else
			new_w = get_window_by_refnum_direct(get_screen_window_list(get_window_screennum(w->refnum)));
	}
	while (new_w && new_w->skip && new_w != last);

	if (new_w)
		return new_w->refnum;
	else
		return window_;
}

/*
 * get_previous_window: this returns the previous *visible* window in the
 * window list.  This automatically wraps to the last window in the window
 * list 
 */
static	int	get_previous_window (int window_)
{
	Window *w = get_window_by_refnum_direct(window_);
	Window *last = w;
	Window *new_w = w;

	if (!w || get_window_screennum(w->refnum) < 0)
		last = new_w = get_window_by_refnum_direct(current_window_);

	do
	{
		if (get_window_prev(new_w->refnum) != -1)
			new_w = get_window_by_refnum_direct(get_window_prev(new_w->refnum));
		else if (get_window_screennum(new_w->refnum) >= 0)
			new_w = get_window_by_refnum_direct(get_screen_bottom_window(get_window_screennum(new_w->refnum)));
		else
			/* XXX */ (void) 0;
	}
	while (new_w->skip && new_w != last);

	if (new_w)
		return new_w->refnum;
	else
		return window_;
}

/* 
 * XXXX i have no idea if this belongs here.
 */
char *	get_window_status_line (int refnum, int line)
{
	Window *the_window;

	if ((the_window = get_window_by_refnum_direct(refnum)))
	{
		if (line > the_window->status.number)
			return NULL;

		return denormalize_string(the_window->status.line[line].result);
	}
	else
		return NULL;
}


/* * * */
int	get_window_screennum (int refnum)
{
	Window *tmp;

	if (!(tmp = get_window_by_refnum_direct(refnum)))
		tmp = get_window_by_refnum_direct(current_window_);
	return tmp->screen_;
}

int	set_window_screennum (int refnum, int screennum)
{
	Window *tmp;

	if ((tmp = get_window_by_refnum_direct(refnum)))
	{
		if (screennum == -1)
		{
			tmp->screen_ = -1;
			return 1;
		}
		else
		{
			if (screen_is_valid(screennum))
			{
				tmp->screen_ = screennum;
				return 1;
			}
		}
	}
	return 0;
}


int	get_window_refnum (int refnum)
{
	Window *tmp;

	if (!(tmp = get_window_by_refnum_direct(refnum)))
		tmp = get_window_by_refnum_direct(current_window_);
	return tmp->refnum;
}

int	get_window_user_refnum (int refnum)
{
	Window *tmp;

	if (!(tmp = get_window_by_refnum_direct(refnum)))
		tmp = get_window_by_refnum_direct(current_window_);
	if (!tmp)
		return 0;
	return tmp->user_refnum;
}

int	window_is_valid (int refnum)
{
	if (get_window_by_refnum_direct(refnum))
		return 1;
	return 0;
}

/* * * * * * * * * * * * * * * TARGETS AND QUERIES * * * * * * * * * * * */

const char *	get_target_special (int window, int server, const char *target)
{
	if (!strcmp(target, "."))
	{
		if (!(target = get_server_sent_nick(server)))
			say("You have not messaged anyone yet");
	}
	else if (!strcmp(target, ","))
	{
		if (!(target = get_server_recv_nick(server)))
			say("You have not recieved a message yet");
	}
	else if (!strcmp(target, "*") && 
		!(target = get_window_echannel(window)))
	{
		say("You are not on a channel");
	}
	else if (*target == '%')
	{
		/* 
		 * You are allowed to query non-existant /exec's.
		 * This allows you to capture all output from the
		 * /exec by redirecting it to the window before it
		 * is launched.  Be careful not to send a message
		 * to a non-existing process!
		 */
	}

	return target;
}

/*
 * get_window_target: returns the target for the window with the given
 * refnum (or for the current window).  The target is either the query nick
 * or current channel for the window 
 */
const char 	*get_window_target (int refnum)
{
	const char *	cc;

	if (!get_window_by_refnum_direct(refnum))
		if (get_screen_input_window(last_input_screen) < 0)
			return NULL;

	if ((cc = get_window_equery(refnum)))
		return cc;
	if ((cc = get_window_echannel(refnum)))
		return cc;
	return NULL;
}

const char *	get_window_equery (int refnum)
{
	List *nick;
	Window *win;

	if ((win = get_window_by_refnum_direct(refnum)) == NULL)
		return NULL;
	for (nick = win->nicks; nick; nick = nick->next)
	{
		if (((WNickList *)(nick->d))->counter == win->query_counter)
			return nick->name;
	}

	return NULL;
}

BUILT_IN_KEYBINDING(switch_query)
{
        int     lowcount;
	int	highcount = -1;
        List *	winner = NULL,
		  *nick;
	Window  *win;
	const char *	old_query;

	win = get_window_by_refnum_direct(0);
	lowcount = win->query_counter;

	old_query = get_window_equery(0);
	for (nick = win->nicks; nick; nick = nick->next)
	{
		if (((WNickList *)(nick->d))->counter > highcount)
			highcount = ((WNickList *)(nick->d))->counter;
		if (((WNickList *)(nick->d))->counter < lowcount)
		{
			lowcount = ((WNickList *)(nick->d))->counter;
			winner = nick;
		}
	}

        /*
         * If there are no queries on this window, punt.
         * If there is only one query on this window, punt.
         */
        if (winner == NULL || highcount == -1 || highcount == lowcount)
                return;

	/* Make the oldest query the newest. */
	((WNickList *)(winner->d))->counter = current_query_counter++;
	win->query_counter = ((WNickList *)(winner->d))->counter;
	window_statusbar_needs_update(win->refnum);

	/* Tell the user */
	do_hook(SWITCH_QUERY_LIST, "%d %s %s",
		win->user_refnum, old_query, winner->name);
}

static void	recheck_queries (int window_)
{
	Window *win = get_window_by_refnum_direct(window_);
	List *nick;

	if (!win)
		return;

	win->query_counter = 0;

	/* Find the winner and reset query_counter */
	for (nick = win->nicks; nick; nick = nick->next)
	{
		if (((WNickList *)(nick->d))->counter > win->query_counter)
			win->query_counter = ((WNickList *)(nick->d))->counter;
	}

	window_statusbar_needs_update(win->refnum);
}

/*
 * check_window_target -- is this target owned by this window?
 */
static	int	check_window_target (int window_, int server, const char *nick)
{
	Window *	w = get_window_by_refnum_direct(window_);

	if (!nick)
		return 0;

	/* Hack to work around global targets (do this better, later) */
	if (*nick == '=' || *nick == '%' || *nick == '@' || *nick == '/')
		if (find_in_list(w->nicks, nick))
			return 1;

	if (get_window_server(window_) != server)
		return 0;

	if (find_in_list(w->nicks, nick))
		return 1;

	return 0;
}

/*
 * get_window_for_target -- given a target, which window (if any) owns it?
 */
int	get_window_for_target (int server, const char *nick)
{
	int	window = 0;

	while (traverse_all_windows2(&window))
		if (check_window_target(window, server, nick))
			return window;

	return 0;
}

int	remove_window_target (int window_, int server, const char *nick)
{
	Window *	w = get_window_by_refnum_direct(window_);
	List *	item;

	if (get_window_server(window_) != server)
		return 0;

	if ((item = remove_from_list(&w->nicks, nick)))
	{
		int	l;

		l = message_setall(w->refnum, get_who_from(), get_who_level());
		say("Removed %s from window target list", item->name);
		pop_message_from(l);

		new_free(&item->name);
		new_free((char **)&item->d);
		new_free((char **)&item);

		recheck_queries(window_);
		return 1;
	}

	return 0;
}

/* 
 * add_window_target -- assure the target 'nick' on server 'server' belongs to 'window_'
 *
 * This forces any window for the server to release its claim upon
 * a nickname (so it can be claimed by another window)
 */
static int	add_window_target (int window_, int server, const char *target, int as_current)
{
	int		other_window;
	int		l;
	Window *	w;
	List *		new_w;

	if (!(w = get_window_by_refnum_direct(window_)))
		return 0;

	if (get_window_server(window_) != server)
		return 0;

	/*
	 * Do I already own this target?  If not, go looking for it elsewhere
	 */
	if ((new_w = find_in_list(w->nicks, target)) == NULL)
	{
		int	need_create;

		if ((other_window = get_window_for_target(server, target)))
		{
			if (other_window == window_)
			{
				yell("add_window_target: window %d, server %d, target %s, as_current %d: find_in_list says the target is not on this window but get_window_for_target says it is! -- Punting", window_, server, target, as_current);
				return 0;		/* XXX I just checked for this! */
			}
			else
			{
				remove_window_target(other_window, server, target);
				need_create = 1;
			}
		}
		else
			need_create = 1;

		if (need_create)
		{
			l = message_setall(window_, get_who_from(), get_who_level());
			say("Added %s to window target list", target);
			pop_message_from(l);

			new_w = (List *)new_malloc(sizeof(List));
			new_w->name = malloc_strdup(target);
			new_w->d = (WNickList *)new_malloc(sizeof(WNickList));
			((WNickList *)(new_w->d))->counter = 0;
			add_item_to_list(&w->nicks, new_w);
		}
	}

	if (as_current)
	{
		((WNickList *)(new_w->d))->counter = current_query_counter++;
		w->query_counter = ((WNickList *)(new_w->d))->counter;
		recheck_queries(window_);
	}
	else
		((WNickList *)(new_w->d))->counter = 0;

	return 1;
}


/* * * * * * * * * * * * * * CHANNELS * * * * * * * * * * * * * * * * * */
/* get_window_echannel: returns the current channel for window refnum */
const char 	*get_window_echannel (int refnum)
{
	Window	*tmp;

	if ((tmp = get_window_by_refnum_direct(refnum)) == (Window *) 0)
		panic(1, "get_echannel_by_refnum: invalid window [%d]", refnum);
	return window_current_channel(tmp->refnum, tmp->server);
}

/*
 * This is called whenever you're not going to reconnect and
 * destroy_server_channels() is called.
 */
void    destroy_waiting_channels (int server)
{
	List *next;
	int	window_;

	for (window_ = 0; traverse_all_windows2(&window_); )
	{
		Window *tmp = get_window_by_refnum_direct(window_);

                if (tmp->server != server)
                        continue;

		while (tmp->waiting_chans)
		{
			next = tmp->waiting_chans->next;
			new_free(&tmp->waiting_chans->name);
			new_free((char **)&tmp->waiting_chans->d);
			new_free((char **)&tmp->waiting_chans);
			tmp->waiting_chans =  next;
		}
	}
}

/*
 * This is called whenever you're not going to reconnect and
 * destroy_server_channels() is called.
 */
static void    destroy_window_waiting_channels (int refnum)
{
        Window *tmp = NULL;
	List *next;

	if (!(tmp = get_window_by_refnum_direct(refnum)))
		return;

	while (tmp->waiting_chans)
	{
		next = tmp->waiting_chans->next;
		new_free(&tmp->waiting_chans->name);
		new_free((char **)&tmp->waiting_chans->d);
		new_free((char **)&tmp->waiting_chans);
		tmp->waiting_chans =  next;
	}
}

static	int	add_waiting_channel (int refnum, const char *chan)
{
	Window *win;
	List *	tmp;
	int	window_;

	if (!window_is_valid(refnum))
		return -1;
	win = get_window_by_refnum_direct(refnum);

	for (window_ = 0; traverse_all_windows2(&window_); )
	{
	    Window *w = get_window_by_refnum_direct(window_);

	    if (w == win || w->server != win->server)
		continue;

	    if ((tmp = remove_from_list(&w->waiting_chans, chan)))
	    {
		new_free(&tmp->name);
		new_free((char **)&tmp->d);
		new_free((char **)&tmp);
	    }
	}

	if (find_in_list(win->waiting_chans, chan))
		return -1;		/* Already present. */

	tmp = (List *)new_malloc(sizeof(List));
	tmp->name = malloc_strdup(chan);
	tmp->d = (WNickList *)new_malloc(sizeof(WNickList *));
	((WNickList *)(tmp->d))->counter = 0;
	add_item_to_list(&win->waiting_chans, tmp);
	return 0;			/* Added */
}

int	claim_waiting_channel (const char *chan, int servref)
{
	List *	tmp;
	int	retval = -1;
	int	window_;

	/* Do a full traversal, just to make sure no channels stay behind */
	for (window_ = 0; traverse_all_windows2(&window_); )
	{
	    Window *w = get_window_by_refnum_direct(window_);

	    if (w->server != servref)
		continue;

	    if ((tmp = remove_from_list(&w->waiting_chans, chan)))
	    {
		new_free(&tmp->name);
		new_free((char **)&tmp->d);
		new_free((char **)&tmp);
		retval = w->refnum;
	    }
	}

	return retval;		/* Not found */
}


/* * * * * * * * * * * * * * * * * * SERVERS * * * * * * * * * * * * * */
/*
 * get_window_server: returns the server index for the window with the given
 * refnum 
 */
int 	get_window_server (int refnum)
{
	Window	*tmp;

	if ((tmp = get_window_by_refnum_direct(refnum)) == (Window *) 0)
		tmp = get_window_by_refnum_direct(current_window_);
	return (tmp->server);
}

/*
 * set_window_server: sets the server index for the given window refnum
 */
int 	set_window_server (int refnum, int servref)
{
	Window	*tmp;

	if ((tmp = get_window_by_refnum_direct(refnum)))
	{
		tmp->server = servref;
		return 0;
	}
	return -1;
}


/*
 * Changes (in bulk) all of the windows pointing at "old_server" to 
 * "new_server".  This implements the back-end of the /SERVER command.
 * When this returns, no servers will be pointing at "old_server", and 
 * so at the next sequence point it will be closed.
 * 
 * This is used by the /SERVER command (via /SERVER +, /SERVER -, or 
 * /SERVER <name>), and by the 465 numeric (YOUREBANNEDCREEP).
 */
void	change_window_server (int old_server, int new_server)
{
	int	window_;

	for (window_ = 0; traverse_all_windows2(&window_); )
	{
		Window *tmp = get_window_by_refnum_direct(window_);
		if (tmp->server == old_server)
			window_change_server(window_, new_server);
	}

	if (old_server == primary_server)
		primary_server = new_server;

	/*
	 * At the next sequence point, old_server will be disconnected.
	 * We need not worry about that here.
	 */
}

/*
 * window_check_servers: this checks the validity of the open servers vs the
 * current window list.  Every open server must have at least one window
 * associated with it.  If a window is associated with a server that's no
 * longer open, that window's server is set to the primary server.  If an
 * open server has no assicatiate windows, that server is closed.  If the
 * primary server is no more, a new primary server is picked from the open
 * servers 
 */
void 	window_check_servers (void)
{
	int	window;
	int	max, i;
	int	prime = NOSERV;
	int	status;
	int	l;

	connected_to_server = 0;
	max = server_list_size();
	for (i = 0; i < max; i++)
	{
	    status = get_server_state(i);

	    if ((window = get_server_current_window(i)) < 1)
	    {
		if (status > SERVER_RECONNECT && status < SERVER_CLOSING)
            	{
		    if (get_server_autoclose(i))
			close_server(i, "No windows for this server");
	        }
		continue;		/* Move on to next server */
	    }

	    connected_to_server++;
	    l = message_setall(window, NULL, LEVEL_OTHER);

	    if (status == SERVER_RECONNECT)
	    {
		if (x_debug & DEBUG_SERVER_CONNECT)
		    yell("window_check_servers() is bringing up server %d", i);

		/* This bootstraps the reconnect process */
		/* XXX - I should create a shim with a better name */
		grab_server_address(i);
	    }
	    else if (status == SERVER_ACTIVE)
	    {
		if (prime == NOSERV)
		    prime = i;
	    }
	    else if (status == SERVER_CLOSED && server_more_addrs(i))
	    {
		if (x_debug & DEBUG_SERVER_CONNECT)
		    yell("window_check_servers() is restarting server %d", i);
		connect_to_server(i);
	    }

	    pop_message_from(l);
	}

	if (!is_server_open(primary_server))
		primary_server = prime;
}

/*
 * This is a debugging function that is used to determine the referential
 * integrity of all of the channels to all of the windows.  The basic notion
 * is that it is a bug if any of the following conditions exist:
 *
 * 1)	There exists some window W, such that its named current channel E
 *		does not exist.
 * 2)	There exists some window W, such that its named current channel E
 *		is not connected to window W.
 * 3)	There exists some channel E, such that it is not connected to any
 *		window. (repairable)
 * 4)	There exists some channel E, such that its connected window W is
 *		not connected to the same server as E.
 * 5)	There exists some channel E, such that its connected window W 
 *		does not exist.
 * 6)	There exists some channel E, connected to window W, such that W
 *		has no current channel. (repairable)
 * 7)	There exists some channel E, such that it's server is not open and
 *		its "saved" option is not asserted.
 */
void 	window_check_channels (void)
{
	/* Tests #3 through #5 are done in names.c */
	channel_check_windows();
}


/* * * * * * * * * * LEVELS * * * * * * * * * */
/*
 * This is what makes /set new_server_lastlog_level work.  Previously,
 * each time you connected to a server (got a 001 numeric) those levels
 * were unconditionally assigned to the server's current window.  That was
 * actually a bad thing for reconnectors who had already got their levels
 * set up and didn't want them reassigned thank-you-very-much.  So now 
 * what it'll do is leave alone any levels that are already set and just set
 * the ones that are totaly unused. 
 * 
 * Naturally if you're one of those goofballs who sets all your window levels
 * to NONE you're definitely gonna want to /set new_server_lastlog_level NONE
 * if you haven't done that already.
 */
int	renormalize_window_levels (int refnum, Mask mask)
{
	Window	*win;
	int	i, claimed;
	int	window_;

	if (!(win = get_window_by_refnum_direct(refnum)))
		return -1;

	/* Test each level in the reclaimable bitmask */
	for (i = 1; BIT_VALID(i); i++)
	{
	    /* If this level isn't reclaimable, skip it */
	    if (!mask_isset(&mask, i))
		continue;

	    /* Now look for any window (including us) that already claims it */
	    claimed = 0;
	    for (window_ = 0; traverse_all_windows2(&window_); )
	    {
		Window *tmp = get_window_by_refnum_direct(window_);
		if (mask_isset(&tmp->window_mask, i))
		    claimed = 1;
	    }

	    /* If no window claims it (including us), then we will. */
	    if (!claimed)
		mask_set(&win->window_mask, i);
	}

	revamp_window_masks(refnum);
	return 0;
}


/*
 * revamp_window_masks: Given a level setting for the current window, this
 * makes sure that that level setting is unused by any other window. Thus
 * only one window in the system can be set to a given level.  This only
 * revamps levels for windows with servers matching the given window 
 * it also makes sure that only one window has the level `DCC', as this is
 * not dependant on a server.
 */
static void 	revamp_window_masks (int refnum)
{
	Window *window;
	int	i;
	int	window_;

	if (!window_is_valid(refnum))
		return;

	window = get_window_by_refnum_direct(refnum);

	for (i = 1; BIT_VALID(i); i++)
	{
	    if (!mask_isset(&window->window_mask, i))
		continue;

	    for (window_ = 0; traverse_all_windows2(&window_); )
	    {
		Window *tmp = get_window_by_refnum_direct(window_);

		if (tmp == window)
		    continue;
		if (mask_isset(&tmp->window_mask, i))
		    if (i == LEVEL_DCC || tmp->server == window->server)
			mask_unset(&tmp->window_mask, i);
	    }
	}
}

struct output_context {
	const char *	who_from;
	int		who_level;
	const char *	who_file;
	int		who_line;
	int		to_window;
};
struct output_context *	contexts = NULL;
int			context_max = -1;
int 			context_counter = -1;

int	real_message_setall (int refnum, const char *who, int level, const char *file, int line)
{
	if (context_max < 0)
	{
		context_max = 32;
		context_counter = 0;
		RESIZE(contexts, struct output_context, context_max);
	}
	else if (context_counter >= context_max - 20)
	{
		context_max *= 2;
		RESIZE(contexts, struct output_context, context_max);
	}

	if (x_debug & DEBUG_MESSAGE_FROM)
		yell("Setting context %d [%d:%s:%s] {%s:%d}", context_counter, refnum, who ? who : "<null>", level_to_str(level), file, line);

#ifdef NO_CHEATING
	malloc_strcpy(&contexts[context_counter].who_from, who);
#else
	contexts[context_counter].who_from = who;
#endif
	contexts[context_counter].who_level = level;
	contexts[context_counter].who_file = file;
	contexts[context_counter].who_line = line;
	contexts[context_counter].to_window = refnum;

	return context_counter++;
}

/*
 * adjust_context_windows: This function goes through the context
 * stack and rewrites any instances of 'old_win' to 'new_win'.
 * This is needed when a window is killed, so that further output
 * in any contexts using that window has somewhere to go.
 */
static void	adjust_context_windows (int old_win, int new_win)
{
	int context;

	for (context = 0; context < context_counter; context++)
	{
		if (contexts[context].to_window == old_win)
		{
			contexts[context].to_window = new_win; 
			/* contexts[context].to_window = -1; */
		}
	}
}

/*
 * message_from: With this you can set the who_from variable and the 
 * who_mask variable, used by the display routines to decide which 
 * window messages should go to.
 */
int	real_message_from (const char *who, int level, const char *file, int line)
{
	/* XXX - Ideally we wouldn't do this here. */
	if (who && *who == '=')
		level = LEVEL_DCC;

	if (context_max < 0)
	{
		context_max = 32;
		context_counter = 0;
		RESIZE(contexts, struct output_context, context_max);
	}
	else if (context_counter >= context_max - 20)
	{
		context_max *= 2;
		RESIZE(contexts, struct output_context, context_max);
	}

	if (x_debug & DEBUG_MESSAGE_FROM)
		yell("Setting context %d [-:%s:%s] {%s:%d}", context_counter, who ? who : "<null>", level_to_str(level), file, line);

#ifdef NO_CHEATING
	malloc_strcpy(&contexts[context_counter].who_from, who);
#else
	contexts[context_counter].who_from = who;
#endif
	contexts[context_counter].who_level = level;
	contexts[context_counter].who_file = file;
	contexts[context_counter].who_line = line;
	contexts[context_counter].to_window = -1;
	return context_counter++;
}

void	pop_message_from (int context)
{
	if (x_debug & DEBUG_MESSAGE_FROM)
		yell("popping message context %d", context);

	if (context != context_counter - 1)
		panic(1, "Output context from %s:%d was not released", 
			contexts[context_counter-1].who_file,
			contexts[context_counter-1].who_line);

	context_counter--;
#ifdef NO_CHEATING
	new_free(&contexts[context_counter].who_from);
#endif
	contexts[context_counter].who_level = LEVEL_NONE;
	contexts[context_counter].who_file = NULL;
	contexts[context_counter].who_line = -1;
	contexts[context_counter].to_window = -1;
}

/* 
 * This is called from io() when the main loop has fully unwound.
 * in theory, there shouldn't be ANY contexts left on the stack.
 * if there are, maybe we should panic here?  Or maybe just delete them.
 */
void	check_message_from_queue (int doing_reset)
{
	int	i;

	/* Well, there's always one context left... */
	if (context_counter != 1)
	{
	    /* Alert to the problem... */
	    for (i = 1; i < context_counter; i++)
	    {
		if (!doing_reset)
			yell("Warning: Output context from %s:%d was not released",
				contexts[i].who_file,
				contexts[i].who_line);

	    }

	    /* And then clean up the mess. */
	    while (context_counter > 1)
		pop_message_from(context_counter - 1);
	}
}

const char *	get_who_from(void)
{
	return contexts[context_counter - 1].who_from;
}

int	get_who_level (void)
{
	return contexts[context_counter - 1].who_level;
}

const char *	get_who_file (void)
{
	return contexts[context_counter - 1].who_file;
}

int	get_who_line (void)
{
	return contexts[context_counter - 1].who_line;
}

int	get_to_window (void)
{
	return contexts[context_counter - 1].to_window;
}

/* * * * * * * * * * * CLEARING WINDOWS * * * * * * * * * * */
static void 	clear_window (int window_)
{
	Window *window;

	if (dumb_mode)
		return;

	if (!(window = get_window_by_refnum_direct(window_)))
		return;

	debuglog("clearing window: clearing window %d", window->user_refnum);
	window->scrolling_top_of_display = window->display_ip;
	window->clear_point = window->display_ip;
	if (window->notified)
	{
		window->notified = 0;
		update_all_status();
	}
	window->current_activity = 0;
	recalculate_window_cursor_and_display_ip(window->refnum);

	window_body_needs_redraw(window->refnum);
	window_statusbar_needs_redraw(window->refnum);
}

void 	clear_all_windows (int visible, int hidden)
{
	int	window_;

	for (window_ = 0; traverse_all_windows2(&window_); )
	{
		if (visible && !hidden && get_window_screennum(window_) < 0)
			continue;
		if (!visible && hidden && get_window_screennum(window_) >= 0)
			continue;

		clear_window(window_);
	}
}

/*
 * clear_window_by_refnum: just like clear_window(), but it uses a refnum. If
 * the refnum is invalid, nothing is cleared (so there)
 */
void 	clear_window_by_refnum (int refnum)
{
	if (!window_is_valid(refnum))
		return;
	clear_window(refnum);
}

static void	unclear_window (int window_)
{
	Window *window;
	int i;

	if (dumb_mode)
		return;

	if (!(window = get_window_by_refnum_direct(window_)))
		return;

	window->scrolling_top_of_display = window->display_ip;
	for (i = 0; i < window->display_lines; i++)
	{
		if (window->scrolling_top_of_display == window->top_of_scrollback)
			break;
		if (window->scrolling_top_of_display == window->clear_point)
			break;
		window->scrolling_top_of_display = window->scrolling_top_of_display->prev;
	}

	recalculate_window_cursor_and_display_ip(window->refnum);
	window_body_needs_redraw(window->refnum);
	window_statusbar_needs_redraw(window->refnum);
}

void	unclear_all_windows (int visible, int hidden, int force)
{
	int	window_;

	for (window_ = 0; traverse_all_windows2(&window_); )
	{
		if (visible && !hidden && get_window_screennum(window_) < 0)
			continue;
		if (!visible && hidden && get_window_screennum(window_) >= 0)
			continue;
		if (force)
			reset_window_clear_point(window_);

		unclear_window(window_);
	}
}

void	unclear_window_by_refnum (int refnum, int force)
{
	if (!window_is_valid(refnum))
		refnum = 0;
	if (force)
		reset_window_clear_point(refnum);
	unclear_window(refnum);
}

/*
 * This returns 1 if 'w' is holding something.  This means the
 * window has output that has never been displayed ("held").
 * For compatability with ircII, we "unhold" windows that are 
 * "holding", but we do the check before running a command and
 * do the unhold after running it.  This is confusing, but it's 
 * the way ircII has always done it, so there you have it.
 */
int	window_is_holding (int window_)
{
	Window *w = get_window_by_refnum_direct(window_);

	if (w->holding_distance_from_display_ip > w->display_lines)
		return 1;
	else
		return 0;
}

/*
 * This returns 1 if 'w' is in scrollback view.
 */
int	window_is_scrolled_back (int window_)
{
	Window *w = get_window_by_refnum_direct(window_);

	if (w->scrollback_distance_from_display_ip > w->display_lines)
		return 1;
	else
		return 0;
}


/*
 * After running a command (from the SEND_LINE keybinding), if the window
 * had output that was never displayed ("holding"), then display the next
 * screenfull of output.  Unholding should only be done on windows that
 * were holding *before* the command is run in SEND_LINE.
 */
int	unhold_a_window (int window_)
{
	Window *w = get_window_by_refnum_direct(window_);
	int	slider, i;

	if (!get_window_hold_mode(window_))
		return 0;				/* ok, whatever */

	slider = ((int)w->hold_slider * w->display_lines) / 100;
	for (i = 0; i < slider; i++)
	{
		if (w->holding_top_of_display == w->display_ip)
			break;
		w->holding_top_of_display = w->holding_top_of_display->next;
	}
	recalculate_window_cursor_and_display_ip(w->refnum);
	window_body_needs_redraw(w->refnum);
	window_statusbar_needs_update(w->refnum);
	return 0;
}



/* * * * * * * * * * * * * * * SCROLLING * * * * * * * * * * * * * * */
/*
 * set_scroll_lines: called by /SET SCROLL_LINES to check the scroll lines
 * value 
 */
void 	set_scroll_lines (void *stuff)
{
	VARIABLE *v;
	int	size;

	v = (VARIABLE *)stuff;
	size = v->integer;

	if (size == 0)
	{
		say("You cannot turn SCROLL off.  Gripe at me.");
		return;
	}

	else if (size > get_window_display_lines(current_window_))
	{
		say("Maximum lines that may be scrolled is %d [%d]", 
			get_window_display_lines(current_window_), size);
		v->integer = get_window_display_lines(current_window_);
	}
}





/* * * * * * * * * UNSORTED * * * * * * * */
int	lookup_window (const char *desc)
{
        int window_ = get_window_by_desc(desc);

	if (window_ >= 1)
		return window_;
	else
		return -1;
}

int	lookup_any_visible_window (void)
{
	int	window_;

	for (window_ = 0; traverse_all_windows2(&window_); )
	{
		Window *win = get_window_by_refnum_direct(window_);
		if (get_window_screennum(win->refnum) >= 0)
			return win->refnum;
	}
	return -1;
}

/* Don't forget to call recalculate_windows() after calling this! */
void	set_window_display_lines (int refnum, int value)
{
	Window	*w = get_window_by_refnum_direct(refnum);

	if (w)
		w->display_lines = value;
}

int	get_window_display_lines (int refnum)
{
	Window	*w = get_window_by_refnum_direct(refnum);

	if (w)
		return w->display_lines;
	else
		return -1;
}

int	get_window_change_line (int refnum)
{
	Window	*w = get_window_by_refnum_direct(refnum);

	if (w)
		return w->change_line;
	return -1;
}


int	set_window_change_line (int refnum, int value)
{
	Window	*w = get_window_by_refnum_direct(refnum);

	if (w)
	{
		w->change_line = value;
		return 0;
	}
	else
		return -1;
}

int	set_window_lastlog_mask (int refnum, Mask mask)
{
	Window	*w = get_window_by_refnum_direct(refnum);

	if (w)
	{
		w->lastlog_mask = mask;
		return 0;
	}
	else
		return -1;
}

int	get_window_lastlog_mask (int refnum, Mask *retval)
{
	Window	*w = get_window_by_refnum_direct(refnum);

	if (w)
	{
		*retval = w->lastlog_mask;
		return 0;
	}
	else
		return -1;
}

int	clear_window_lastlog_mask (int refnum)
{
	Window	*w = get_window_by_refnum_direct(refnum);

	if (w)
	{
		mask_unsetall(&w->lastlog_mask);
		return 0;
	}
	else
		return -1;

}

int	get_window_mask (int refnum, Mask *retval)
{
	Window	*w = get_window_by_refnum_direct(refnum);

	if (w)
	{
		*retval = w->window_mask;
		return 0;
	}
	else
		return -1;
}

int	set_window_notify_mask (int refnum, Mask mask)
{
	Window	*w = get_window_by_refnum_direct(refnum);

	if (w)
	{
		w->notify_mask = mask;
		return 0;
	}
	else
		return -1;
}

int	get_window_lastlog_size (int refnum)
{
	Window	*w = get_window_by_refnum_direct(refnum);

	if (w)
		return w->lastlog_size;
	else
		return -1;
}

int	set_window_priority (int refnum, int priority)
{
	Window	*w = get_window_by_refnum_direct(refnum);

	if (w)
	{
		w->priority = priority;
		return 0;
	}
	else
		return -1;
}

static int	ensure_window_priority (int refnum)
{
	Window	*w = get_window_by_refnum_direct(refnum);

	if (w)
	{
		w->priority = current_window_priority++;
		return 0;
	}
	else
		return -1;
}


FILE *	get_window_log_fp (int refnum)
{
	Window	*w = get_window_by_refnum_direct(refnum);

	if (w)
		return w->log_fp;
	else
		return NULL;
}

int	get_window_notified (int refnum)
{
	Window *w = get_window_by_refnum_direct(refnum);

	if (w)
		return w->notified;
	else
		return 0;
}

int	set_window_notified (int refnum, int value)
{
	Window *w = get_window_by_refnum_direct(refnum);

	if (w)
	{
		w->notified = value;
		return 0;
	}
	else
		return -1;
}

unsigned	get_window_priority (int refnum)
{
	Window *w = get_window_by_refnum_direct(refnum);

	if (w)
		return w->priority;
	else
		return 0;
}

int	get_window_skip (int refnum)
{
	Window *w = get_window_by_refnum_direct(refnum);

	if (w)
		return w->skip;
	else
		return 0;
}

int	get_window_fixed_size (int refnum)
{
	Window *w = get_window_by_refnum_direct(refnum);

	if (w)
		return w->fixed_size;
	else
		return 0;
}

int	set_window_indent (int refnum, int value)
{
	Window *w = get_window_by_refnum_direct(refnum);

	if (w)
	{
		w->indent = value;
		return 0;
	}
	else
		return -1;
}

List *	get_window_nicks (int refnum)
{
	Window *w = get_window_by_refnum_direct(refnum);

	return (List *)w->nicks;
}

int	get_window_hold_mode (int refnum)
{
	Window *w = get_window_by_refnum_direct(refnum);

	if (w && w->holding_top_of_display)
		return 1;
	else
		return 0;
}

int	get_window_hold_interval (int refnum)
{
	Window *w = get_window_by_refnum_direct(refnum);

	if (w)
		return w->hold_interval;
	else
		return 0;
}

int	get_window_holding_distance_from_display_ip (int refnum)
{
	Window *w = get_window_by_refnum_direct(refnum);

	if (w)
		return w->holding_distance_from_display_ip;
	else
		return 0;
}

int	get_window_scrollback_distance_from_display_ip (int refnum)
{
	Window *w = get_window_by_refnum_direct(refnum);

	if (w)
		return w->scrollback_distance_from_display_ip;
	else
		return 0;
}

const char *	get_window_notify_name (int refnum)
{
	Window *w = get_window_by_refnum_direct(refnum);

	if (w)
		return w->notify_name;
	else
		return 0;
}

const char *	get_window_name (int refnum)
{
	Window *w = get_window_by_refnum_direct(refnum);

	if (w)
		return w->name;
	else
		return 0;
}

int	get_window_scrolling_distance_from_display_ip (int refnum)
{
	Window *w = get_window_by_refnum_direct(refnum);

	if (w)
		return w->scrolling_distance_from_display_ip;
	else
		return 0;
}

int	get_window_cursor (int refnum)
{
	Window *w = get_window_by_refnum_direct(refnum);

	if (w)
		return w->cursor;
	else
		return 0;
}

int	get_window_scrollback_top_of_display_exists (int refnum)
{
	Window *w = get_window_by_refnum_direct(refnum);

	if (w && w->scrollback_top_of_display)
		return 1;
	else
		return 0;
}

int	get_window_display_buffer_size (int refnum)
{
	Window *w = get_window_by_refnum_direct(refnum);

	if (w)
		return w->display_buffer_size;
	else
		return 0;
}

Status *get_window_status (int refnum)
{
	Window *w = get_window_by_refnum_direct(refnum);

	if (w)
		return &w->status;
	else
		return NULL;
}

int	get_window_swappable (int refnum)
{
	Window *w = get_window_by_refnum_direct(refnum);

	if (w)
		return w->swappable;
	else
		return 0;
}

void	set_window_swappable (int refnum, int value)
{
	Window *w = get_window_by_refnum_direct(refnum);

	if (w)
		w->swappable = value;
}


int	get_window_current_activity (int refnum)
{
	Window *w = get_window_by_refnum_direct(refnum);

	if (w)
		return w->current_activity;
	else
		return 0;
}

const char *	get_window_current_activity_format (int refnum)
{
	Window *w = get_window_by_refnum_direct(refnum);
	const char *format;

        format = w->activity_format[w->current_activity];
        if (!format || !*format)
                format = w->activity_format[0];
        if (!format || !*format)
                return empty_string;

	return format;
}

const char *	get_window_current_activity_data (int refnum)
{
	Window *w = get_window_by_refnum_direct(refnum);
	const char *data;

        data = w->activity_data[w->current_activity];
        if (!data || !*data)
                data = w->activity_data[0];
        if (!data || !*data)
                data = empty_string;

	return data;
}

int	get_window_bottom (int refnum)
{
	Window *w = get_window_by_refnum_direct(refnum);

	if (w)
		return w->bottom;
	else
		return 0;
}

Display *	get_window_scrollback_top_of_display (int refnum)
{
	Window *w = get_window_by_refnum_direct(refnum);

	if (w)
		return w->scrollback_top_of_display;
	else
		return NULL;
}

Display *	get_window_scrolling_top_of_display (int refnum)
{
	Window *w = get_window_by_refnum_direct(refnum);

	if (w)
		return w->scrolling_top_of_display;
	else
		return NULL;
}

Display *	get_window_display_ip (int refnum)
{
	Window *w = get_window_by_refnum_direct(refnum);

	if (w)
		return w->display_ip;
	else
		return NULL;
}

Display *	get_window_holding_top_of_display (int refnum)
{
	Window *w = get_window_by_refnum_direct(refnum);

	if (w)
		return w->holding_top_of_display;
	else
		return NULL;
}

int	get_window_toplines_showing (int refnum)
{
	Window *w = get_window_by_refnum_direct(refnum);

	if (w)
		return w->toplines_showing;
	else
		return 0;
}

const char *	get_window_topline (int refnum, int topline)
{
	Window *w = get_window_by_refnum_direct(refnum);

	if (w)
		return w->topline[topline];
	else
		return NULL;
}

int	get_window_top (int refnum)
{
	Window *w = get_window_by_refnum_direct(refnum);

	if (w)
		return w->top;
	else
		return 0;
}

int	set_window_my_columns (int refnum, int value)
{
	Window *w = get_window_by_refnum_direct(refnum);

	if (w)
	{
		w->my_columns = value;
		return w->my_columns;
	}
	return 0;

}

int	get_window_my_columns (int refnum)
{
	Window *w = get_window_by_refnum_direct(refnum);

	if (w)
		return w->my_columns;
	else
		return 0;
}


int	set_window_cursor (int refnum, int value)
{
	Window *w = get_window_by_refnum_direct(refnum);

	if (w)
	{
		w->cursor = value;
		return w->cursor;
	}
	return 0;
}

int	get_window_scroll_lines (int refnum)
{
	Window *w = get_window_by_refnum_direct(refnum);

	if (w)
		return w->scroll_lines;
	else
		return 0;
}

int	set_window_scroll_lines (int refnum, int value)
{
	Window *w = get_window_by_refnum_direct(refnum);

	if (w)
	{
		w->scroll_lines = value;
		return w->scroll_lines;
	}
	return 0;
}

int	set_window_cursor_decr (int refnum)
{
	Window *w = get_window_by_refnum_direct(refnum);

	if (w)
	{
		w->cursor--;
		return w->cursor;
	}
	return 0;
}

int	set_window_cursor_incr (int refnum)
{
	Window *w = get_window_by_refnum_direct(refnum);

	if (w)
	{
		w->cursor++;
		return w->cursor;
	}
	return 0;
}

/*
 * set_lastlog_size: sets up a lastlog buffer of size given.  If the lastlog
 * has gotten larger than it was before, all previous lastlog entry remain.
 * If it get smaller, some are deleted from the end. 
 */
void    set_scrollback_size (void *stuff)
{
	VARIABLE *v;
	int size;
	int	window_;

	v = (VARIABLE *)stuff;
	size = v->integer;

	for (window_ = 0; traverse_all_windows2(&window_); )
        {
		Window *window = get_window_by_refnum_direct(window_);

		if (size < window->display_lines * 2)
			window->display_buffer_max = window->display_lines * 2;
		else
			window->display_buffer_max = size;
        }
}


/*
 * is_window_name_unique: checks the given name vs the names of all the
 * windows and returns true if the given name is unique, false otherwise 
 */
static int 	is_window_name_unique (char *name)
{
	int	window_;

	if (name)
	{
		for (window_ = 0; traverse_all_windows2(&window_); )
		{
			Window *tmp = get_window_by_refnum_direct(window_);
			if (tmp->name && (my_stricmp(tmp->name, name) == 0))
				return (0);
		}
	}
	return (1);
}

static char	*get_waiting_channels_by_window (int window_)
{
	List *nick;
	char *stuff = NULL;
	size_t stuffclue = 0;
	Window *win;

	if (!window_is_valid(window_))
		return malloc_strdup(empty_string);

	win = get_window_by_refnum_direct(window_);
	for (nick = win->waiting_chans; nick; nick = nick->next)
		malloc_strcat_wordlist_c(&stuff, space, nick->name, &stuffclue);

	if (!stuff)
		return malloc_strdup(empty_string);
	else
		return stuff;
}

static char	*get_nicklist_by_window (int window_)
{
	List *nick;
	char *stuff = NULL;
	size_t stuffclue = 0;
	Window *win;

	if (!window_is_valid(window_))
		return malloc_strdup(empty_string);

	win = get_window_by_refnum_direct(window_);
	for (nick = win->nicks; nick; nick = nick->next)
		malloc_strcat_wordlist_c(&stuff, space, nick->name, &stuffclue);

	if (!stuff)
		return malloc_strdup(empty_string);
	else
		return stuff;
}

#define WIN_FORM "%-4s %*.*s %*.*s %*.*s %-9.9s %-10.10s %s%s"
static void 	list_a_window (int window_, int len)
{
	Window *window;

	if (!(window = get_window_by_refnum_direct(window_)))
		say("Window %d does not exist", window_);
	else
	{
		int		cnw  = get_int_var(CHANNEL_NAME_WIDTH_VAR);
		const char *	chan = get_window_echannel(window->refnum);
		const char *	q    = get_window_equery(window->refnum);

		if (cnw == 0)
			cnw = 12;	/* Whatever */

		say(WIN_FORM,           ltoa(window->user_refnum),
			      12, 12,   get_server_nickname(window->server),
			      len, len, window->name ? window->name : "<None>",
			      cnw, cnw, chan ? chan : "<None>",
					q ? q : "<None>",
					get_server_itsname(window->server),
					mask_to_str(&window->window_mask),
					get_window_screennum(window->refnum) >= 0 ? empty_string : " Hidden");
	}
}

int     get_window_geometry (int refnum, int *co, int *li)
{
        Window  *win = get_window_by_refnum_direct(refnum);

        if (!win || get_window_screennum(win->refnum) < 0)
                return -1;
        *co = get_screen_columns(get_window_screennum(win->refnum));
        *li = get_screen_lines(get_window_screennum(win->refnum));
        return 0;
}

int	get_window_indent (int window)
{
	Window *win;

	if (!(win = get_window_by_refnum_direct(window)))
		return get_int_var(INDENT_VAR);

	return win->indent;
}

const char *	get_window_uuid (int window)
{
	Window *win;

	if (!(win = get_window_by_refnum_direct(window)))
		return NULL;

	return win->uuid;
}

const char *	get_window_log_rewrite (int window)
{
	Window *win;

	if (!(win = get_window_by_refnum_direct(window)))
		return NULL;

	return win->log_rewrite;
}

void	set_window_log_rewrite (int window, const char *value)
{
	Window *win;

	if (!(win = get_window_by_refnum_direct(window)))
		return;
	malloc_strcpy(&win->log_rewrite, value);
}

int	get_window_log_mangle (int window)
{
	Window *win;

	if (!(win = get_window_by_refnum_direct(window)))
		return 0;

	return win->log_mangle;
}

void	set_window_log_mangle (int window, int value)
{
	Window *win;

	if (!(win = get_window_by_refnum_direct(window)))
		return;
	win->log_mangle = value;
}

int	get_window_beep_always (int window)
{
	Window *win;

	if (!(win = get_window_by_refnum_direct(window)))
		return 0;
	return win->beep_always;
}

Mask *	get_window_notify_mask (int window)
{
	Window *win;

	if (!(win = get_window_by_refnum_direct(window)))
		return NULL;
	return &win->notify_mask;
}

int	get_window_notify_when_hidden (int window)
{
	Window *win;

	if (!(win = get_window_by_refnum_direct(window)))
		return 0;
	return win->notify_when_hidden;
}

static int	get_window_display_counter_incr (int window)
{
	Window *win;

	if (!(win = get_window_by_refnum_direct(window)))
		return 0;
	return win->display_counter++;
}

static int	get_window_deceased (int window)
{
	Window *win;

	if (!(win = get_window_by_refnum_direct(window)))
		return 0;
	return win->deceased;
}

static void	set_window_deceased (int window, int value)
{
	Window *win;

	if (!(win = get_window_by_refnum_direct(window)))
		return;
	win->deceased = value;
}

/*
 * get_window2: this parses out any window (visible or not) and returns 
 * its refnum
 */
static	int	get_window2 (const char *name, char **args)
{
	char	*arg;
	int	window_;

	if ((arg = next_arg(*args, args)))
	{
		if ((window_ = get_window_by_desc(arg)) >= 1)
			return window_;
		say("%s: No such window: %s", name, arg);
	}
	else
		say("%s: Please specify a window refnum or name", name);

	return 0;
}


/*
 * get_invisible_window: parses out an invisible window by reference number.
 * Returns the pointer to the window, or null.  The args can also be "LAST"
 * indicating the top of the invisible window list (and thus the last window
 * made invisible) 
 */
static int	get_invisible_window (const char *name, char **args)
{
	char	*arg;
	int	tmp_;

	if ((arg = next_arg(*args, args)) != NULL)
	{
		if (my_strnicmp(arg, "LAST", strlen(arg)) == 0)
		{
			if (get_invisible_list() < 1)
				say("%s: There are no hidden windows", name);
			return get_invisible_list();
		}
		if ((tmp_ = get_window2(name, &arg)) >= 1)
		{
			if (get_window_screennum(tmp_) < 0)
				return tmp_;
			else
			{
				if (get_window_name(tmp_))
					say("%s: Window %s is not hidden!",
						name, get_window_name(tmp_));
				else
					say("%s: Window %d is not hidden!",
						name, get_window_user_refnum(tmp_));
			}
		}
	}
	else
		say("%s: Please specify a window refnum or LAST", name);
	return -1;
}


/* get_number: parses out an integer number and returns it */
static int 	get_number (const char *name, char **args, int *var)
{
	char	*arg;

	if ((arg = next_arg(*args, args)) != NULL)
	{
		if (is_number(arg))
		{
			*var = my_atol(arg);
			return 1;
		}
	}
	say("%s: You must specify a number", name);
	return 0;
}

/*
 * get_boolean: parses either ON, OFF, or TOGGLE and sets the var
 * accordingly.  Returns 0 if all went well, -1 if a bogus or missing value
 * was specified 
 */
static int 	get_boolean (const char *name, char **args, short *var)
{
	char	*arg;
	int	newval;

	if (!args)
		return 0;

	newval = *var;
	if (!(arg = next_arg(*args, args)))
	{
		say("Window %s is %s", name, onoff[newval]);
		return 0;
	}

	if (do_boolean(arg, &newval))
	{
		say("Value for %s must be ON, OFF, or TOGGLE", name);
		return 0;
	}

	/* The say() MUST BE DONE BEFORE THE ASSIGNMENT! */
	say("Window %s is %s", name, onoff[newval]);
	*var = newval;
	return 1;
}

/* * * * * * * * * * * WINDOW OPERATIONS * * * * * * * * * */
#define WINDOWCMD(x) static int windowcmd_ ## x (int refnum, char **args)
#define EXT_WINDOWCMD(x) int windowcmd_ ## x (int refnum, char **args)

/*
 * Usage:	/WINDOW ADD <target>		Add target to window
 *		/WINDOW ADD <target>[,<target>]	Add multiple targets to window
 * 
 * Adds one or more non-current targets to this window.  
 * (The "current target" is also known as the "query").
 *
 * Messages to or from a target associated with a window are routed to that 
 * window rather than to the output level they would otherwise go to.
 *
 * Warnings:
 *	/WINDOW -ADD		is a no-op
 *	/WINDOW ADD		is a no-op
 */
WINDOWCMD(add)
{
	char 		*arg;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	if (!(arg = next_arg(*args, args)))
		say("ADD: Add nicknames to be redirected to this window");
	else 
	{
		char *	a = LOCAL_COPY(arg);
		const char *	target;

		while (a && *a)
		{
			if ((target = next_in_comma_list(a, &a)))
				if ((target = get_target_special(refnum, get_window_server(refnum), target)))
					add_window_target(refnum, get_window_server(refnum), target, 0);
		}
	}

	return refnum;
}

/*
 * Usage:	/WINDOW BACK 	Switch current window to previous current window
 * 
 * Switches the screen's current window to the window that was previously
 * the current window.  
 *  - If the previous window does not exist, the top window on the screen is used.
 *  - If the previous window is visible, then it behaves as /WINDOW REFNUM.   
 *  - If the previous window is hidden, then it behaves as /WINDOW SWAP 
 *
 * Caveats:
 *  - If the previous window is on a different screen, then it is made the
 *    current window of whatever screen it is on.
 *
 * Side effects:
 *	The previous window is made the current window
 *	If the previous window is hidden, it is made visible and the current window
 *		is made hidden.
 *	If the previous window is visible, both will remain visible.
 *	If the previous window is on a different screen, the current window of the
 *		current screen will remain unchanged,
 *
 * Return value:
 *	The previous window is returned (see above)
 *
 * Warnings:
 *	/WINDOW -BACK		is a no-op
 */
WINDOWCMD(back)
{
	Window *window = get_window_by_refnum_direct(refnum);
	int	other_refnum;

	if (!args)
		return refnum;

	if ((other_refnum = get_screen_last_window_refnum(last_input_screen)) < 1)
		other_refnum = get_screen_window_list(last_input_screen);

	make_window_current_by_refnum(other_refnum);

	if (get_window_screennum(other_refnum) >= 0)
		set_screens_current_window(get_window_screennum(other_refnum), other_refnum);
	else
		swap_window(window->refnum, other_refnum);

	return get_window_refnum(other_refnum);
}

/*
 * Usage:	/WINDOW BALANCE    Make all non-fixed windows the same size
 *
 * Every non-fixed window on the screen is changed to have the same number of
 * scrolling (discretionary) lines.
 *
 * Caveats:
 * This is more complicated than it used to be.  Every window has some number
 * of mandatory lines
 *   - toplines
 *   - status bars
 *   - the whole window (if FIXED is ON)
 * and the rest is discretionary (scrolling).
 *
 * Warnings:
 *	/WINDOW BALANCE 	on a hidden window is a no-op.
 *	/WINDOW -BALANCE	is a no-op
 */
WINDOWCMD(balance)
{
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	if (get_window_screennum(window->refnum) >= 0)
		rebalance_windows(get_window_screennum(window->refnum));
	else
		yell("Cannot balance invisible windows!");

	return refnum;
}

/*
 * Usage:	/WINDOW BEEP_ALWAYS ON		Sound beeps, even when hidden
 *		/WINDOW BEEP_ALWAYS OFF		Don't sound beeps when hidden
 *	Controls whether you hear beeps in this window when it's hidden
 * 
 * Caveats:
 *  - Whenever a beep happens in a hidden window with BEEP_ALWAYS ON,
 *    A message will appear in the current window telling you what hidden
 *    window had the beep
 *  - Beeping is supplementary to notification (%F)
 *
 * Warnings:
 *	/WINDOW -BEEP_ALWAYS		is a no-op
 */
WINDOWCMD(beep_always)
{
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	if (!get_boolean("BEEP_ALWAYS", args, &window->beep_always))
		return 0;
	return refnum;
}

/*
 * /WINDOW CHANNEL		show me what the channels are in this window
 * /WINDOW CHANNEL -invite
 * /WINDOW CHANNEL "-invite key"
 * /WINDOW CHANNEL #channel
 * /WINDOW CHANNEL #channel1,#channel2
 * /WINDOW CHANNEL "#channel key"
 * /WINDOW CHANNEL "#channel1,#channel2 key1,key2"
 *
 * - The window must be connected to a server
 * - If #channel is "-invite", join the last invited channel
 *
 * For every channel:
 *	If we are on the channel, it is moved to this window
 *	If we are not on the channel, it is added to "waiting channels"
 * At the end we send out a JOIN if there are any channels to join
 *
 * 
 * /WINDOW CHANNEL ["]<#channel>[,<#channel>][ <pass>[,<pass>]]["]
 * Directs the client to make a specified channel the current channel for
 * the window -- it will JOIN the channel if you are not already on it.
 * If the channel you wish to activate is bound to a different window, you
 * will be notified.  If you are already on the channel in another window,
 * then the channel's window will be switched.  If you do not specify a
 * channel, or if you specify the channel "0", then the window will drop its
 * connection to whatever channel it is in.  You can only join a channel if
 * that channel is not bound to a different window.
 */
WINDOWCMD(channel)
{
	char		*arg;
	char 		*chans, *passwds;
	char 		*chan, *pass;
	const char 	*c;
	char 		*cl;
	char 		*chans_to_join, *passes_to_use;
	int		l;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	/* Fix by Jason Brand, Nov 6, 2000 */
	if (window->server == NOSERV)
	{
		say("This window is not connected to a server.");
		return 0;
	}

	if (!(passwds = new_next_arg(*args, args)))
	{
	    if ((c = get_window_echannel(window->refnum)))
	    {
		say("The current channel is %s", c);
		if ((cl = window_all_channels(window->refnum, window->server)))
		{
		    say("All channels in this window: %s", cl);
		    new_free(&cl);
		}
	    }
	    else
		say("There are no channels in this window");
	    return refnum;
	}

	if (!(chans = next_arg(passwds, &passwds)))
	{
		say("Huh?");
		return refnum;
	}

	if (!my_strnicmp(chans, "-invite", 2))
	{
		if (!(c = get_server_invite_channel(window->server)))
		{
			say("You have not been invited to a channel!");
			return refnum;
		}
		chans = LOCAL_COPY(c);		/* Whatever */
	}

	chans_to_join = passes_to_use = NULL;
	while (*chans && (chan = next_in_comma_list(chans, &chans)))
	{
	    pass = NULL;
	    if (passwds && *passwds)
		pass = next_in_comma_list(passwds, &passwds);

	    if (!is_channel(chan))
	    {
		say("CHANNEL: %s is not a valid channel name", chan);
		continue;
	    }

	    /*
	     * We do some chicanery here. :/
	     * For some complicated reasons, new_next_arg skips over
	     * backslashed quotation marks.  Usually what happens down
	     * the line is that the backslash eventually gets unescaped.
	     * Well, not here.  So we have to do this manually.  This is
	     * a quick, and nasty hack, but i will re-assess the
	     * situation later and improve what is being done here.
	     */
	    arg = NULL;
	    malloc_strcat_ues(&arg, chan, empty_string);
	    chan = arg;
	    arg = NULL;
	    malloc_strcat_ues(&arg, pass, empty_string);
	    pass = arg;

	    if (im_on_channel(chan, window->server))
	    {
		move_channel_to_window(chan, window->server, 0, window->refnum);
		say("You are now talking to channel %s", 
				check_channel_type(chan));
	    }
	    else
	    {
		add_waiting_channel(window->refnum, chan);
		malloc_strcat_word(&chans_to_join, comma, chan, DWORD_NO);
		if (pass)
			malloc_strcat_word(&passes_to_use, comma, pass, DWORD_NO);
	    }

	    new_free(&chan);
	    new_free(&pass);
	}

	l = message_from(chans_to_join, LEVEL_OTHER);
	if (chans_to_join && passes_to_use)
		send_to_aserver(window->server,"JOIN %s %s", chans_to_join, passes_to_use);
	else if (chans_to_join)
		send_to_aserver(window->server,"JOIN %s", chans_to_join);

	new_free(&chans_to_join);
	new_free(&passes_to_use);
	pop_message_from(l);

	return refnum;
}

WINDOWCMD(check)
{
	int	lastlog_count;
	int	window_;

	if (!args)
		return refnum;

	for (window_ = 0; traverse_all_windows2(&window_); )
	{
		Window *tmp = get_window_by_refnum_direct(window_);

		lastlog_count = recount_window_lastlog(tmp->refnum);
		if (lastlog_count != tmp->lastlog_size)
			yell("window [%d]'s lastlog is wrong: should be [%d], is [%d]",
				tmp->user_refnum, lastlog_count, tmp->lastlog_size);
	}
	return refnum;
}


/* WINDOW CLAIM - claim a channel should go "here" */
WINDOWCMD(claim)
{
	Window *	window = get_window_by_refnum_direct(refnum);
	char *channel;

	if (!window)
		return 0;
	if (!args)
		return refnum;

	channel = new_next_arg(*args, args);
	if (window_claims_channel(window->refnum, window->server, channel))
		yell("Window %d could not successfully claim channel %s", window->user_refnum, channel);

	return refnum;
}

/* WINDOW CLEAR -- should be obvious, right? */
WINDOWCMD(clear)
{
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	clear_window(refnum);
	return refnum;
}

/*
 * /WINDOW CREATE
 * This directs the client to open up a new physical screen and create a
 * new window in it.  This feature depends on the external "wserv" utility
 * and requires a multi-processing system, since it actually runs the new
 * screen in a seperate process.  Please note that the external screen is
 * not actually controlled by the client, but rather by "wserv" which acts
 * as a pass-through filter on behalf of the client.
 */
WINDOWCMD(create)
{
	int	new_refnum;

	if (!args)
		return refnum;

	if ((new_refnum = create_additional_screen()) < 1)
		say("Cannot create new screen!");
	else
		refnum = new_refnum;

	return refnum;
}

/*
 * /WINDOW DELETE
 * This directs the client to close the current external physical screen
 * and to re-parent any windows onto other screens.  You are not allowed
 * to delete the "main" window because that window belongs to the process
 * group of the client itself.
 */
WINDOWCMD(delete)
{
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	kill_screen(get_window_screennum(window->refnum));
	return current_window_;
}

/*
 * /WINDOW DELETE_KILL
 * This does a /window delete (destroy screen) plus a /window kill 
 * (destroy window) as one atomic operation.  Normally you cannot
 * kill the last window on a screen; and destroying a screen orphans
 * that last window.  But what if you wnat them to go away together?
 */
WINDOWCMD(delete_kill)
{
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	kill_screen(get_window_screennum(window->refnum));
	if (unlink_window(refnum))
		delete_window_contents(refnum);
	return current_window_;
}

/*
 * /WINDOW DESCRIBE
 * Directs the client to tell you a bit about the current window.
 * This is the 'default' argument to the /window command.
 */
WINDOWCMD(describe)
{
	const char *chan;
	char *c;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;

        if (window->name)
	    say("Window %s (%u)", 
				window->name, window->user_refnum);
        else
	    say("Window %u", window->user_refnum);

	say("\tServer: %d - %s",
				window->server, 
				get_server_name(window->server));
	say("\tScreen: %d", get_window_screennum(window->refnum));
	say("\tGeometry Info: [%hd %hd %hd %d %d %hd %d %d %d]", 
				window->toplines_showing, 
				window->top, window->bottom, 
				0, window->display_lines,
				window->cursor, 
				window->scrolling_distance_from_display_ip,
				window->holding_distance_from_display_ip,
				window->scrollback_distance_from_display_ip);
	say("\tCO, LI are [%d %d]", 
				current_term->TI_cols, current_term->TI_lines);

	chan = get_window_echannel(window->refnum);
	say("\tCurrent channel: %s", chan ? chan : "<None>");
	c = window_all_channels(window->refnum, window->server);
	say("\tChannels in this window: %s", c ? c : "<None>");
	new_free(&c);

	if (window->waiting_chans)
	{
	    List *tmp;
	    size_t clue = 0;

	    c = NULL;
	    for (tmp = window->waiting_chans; tmp; tmp = tmp->next)
		malloc_strcat_word_c(&c, space, tmp->name, DWORD_NO, &clue);

	    say("\tWaiting channels list: %s", c);
	    new_free(&c);
	}

	chan = get_window_equery(window->refnum);
	say("\tQuery User: %s", chan ? chan : "<None>");

	if (window->nicks)
	{
	    List *tmp;
	    size_t clue = 0;

	    c = NULL;
	    for (tmp = window->nicks; tmp; tmp = tmp->next)
		malloc_strcat_word_c(&c, space, tmp->name, DWORD_NO, &clue);

	    say("\tName list: %s", c);
	    new_free(&c);
	}

	say("\tStatus bars: %d", 
				window->status.number);

	say("\tLogging is %s", 
				onoff[window->log]);
        if (window->logfile)
	    say("\tLogfile is %s", window->logfile);
        else
	    say("\tNo logfile given");

	say("\tNotification is %s", 
				onoff[window->notify_when_hidden]);
	say("\tNotify level is %s", 
				mask_to_str(&window->notify_mask));

	say("\tWindow Level is %s", 
				mask_to_str(&window->window_mask));
	say("\tLastlog level is %s", 
				mask_to_str(&window->lastlog_mask));
	say("\tLastlog current size is %d", 
				window->lastlog_size);
	say("\tLastlog maximum size is %d", 
				window->lastlog_max);

	say("\tHold mode is %s", 
				onoff[window->holding_top_of_display ? 1 : 0]);
	say("\tHold Slider percentage is %hd",
				window->hold_slider);
	say("\tUpdate %%B on status bar every %hd lines",
				window->hold_interval);

	say("\tFixed mode is %s", 
				onoff[window->fixed_size ? 1 : 0]);
	say("\tSkipped mode is %s", 
				onoff[window->skip ? 1 : 0]);
	say("\tSwappable mode is %s", 
				onoff[window->swappable ? 1 : 0]);
	say("\tBeep always mode is %s",
				onoff[window->beep_always ? 1 : 0]);
	say("\tAdjust Scrollback on Resize mode is %s",
				onoff[window->scrolladj ? 1 : 0]);

	return refnum;
}

/*
 * /WINDOW DISCON
 * This disassociates a window with all servers.
 */
WINDOWCMD(discon)
{
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	reassign_window_channels(window->refnum);
	destroy_window_waiting_channels(window->refnum);
	window_change_server(refnum, NOSERV); /* XXX This shouldn't be set here. */
	window_statusbar_needs_update(window->refnum);
	return refnum;
}


/*
 * /WINDOW DOUBLE ON|OFF
 * This directs the client to enable or disable the supplimentary status bar.
 * When the "double status bar" is enabled, the status formats are taken from
 * /set STATUS_FORMAT1 or STATUS_FORMAT2.  When it is disabled, the format is
 * taken from /set STATUS_FORMAT.
 */
WINDOWCMD(double)
{
	Window *	window = get_window_by_refnum_direct(refnum);
	short	newval = 0;
	int	current = window->status.number;

	if (!window)
		return 0;
	if (!args)
		return refnum;

	newval = window->status.number - 1;
	if (!get_boolean("DOUBLE", args, &newval))
		return 0;

	if (newval == 0)
		window->status.number = 1;
	else
		window->status.number = 2;

	window->display_lines += current - window->status.number;
	if (window->display_lines < 0)
	{
		window->display_lines = 0;
		if (get_window_screennum(window->refnum) >= 0)
			recalculate_windows(get_window_screennum(window->refnum));
	}
	recalculate_window_positions(get_window_screennum(window->refnum));
	return refnum;
}

/*
 * WINDOW ECHO <text>
 *
 * Text must either be surrounded with double-quotes (")'s or it is assumed
 * to terminate at the end of the argument list.  This sends the given text
 * to the current window.
 */
WINDOWCMD(echo)
{
	const char *to_echo;
	int	l;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	if (**args == '"')
		to_echo = new_next_arg(*args, args);
	else
		to_echo = *args, *args = NULL;

	l = message_setall(window->refnum, get_who_from(), get_who_level());
	put_echo(to_echo);
	pop_message_from(l);

	return refnum;
}

/*
 * /WINDOW FIXED (ON|OFF)
 *
 * When this is ON, then this window will never be used as the implicit
 * partner for a window resize.  That is to say, if you /window grow another
 * window, this window will not be considered for the corresponding shrink.
 * You may /window grow a fixed window, but if you do not have other nonfixed
 * windows, the grow will fail.
 */
WINDOWCMD(fixed)
{
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	if (!get_boolean("FIXED", args, &window->fixed_size))
		return 0;
	return refnum;
}

/*
 * /WINDOW FLUSH
 *
 * Does the window part of the /flush command.
 */
WINDOWCMD(flush)
{
	if (!window_is_valid(refnum))
		return 0;
	if (!args)
		return refnum;

	flush_scrollback_after(refnum, 1);
	return refnum;
}

/*
 * /WINDOW FLUSH_SCROLLBACK
 */
WINDOWCMD(flush_scrollback)
{
	if (!window_is_valid(refnum))
		return 0;
	if (!args)
		return refnum;

	flush_scrollback(refnum, 1);
	return refnum;
}

/*
 * /WINDOW GOTO refnum
 * This switches the current window selection to the window as specified
 * by the numbered refnum.
 */
WINDOWCMD(goto)
{
	Window *	window = get_window_by_refnum_direct(refnum);
	int	value;

	if (!window)
		return 0;
	if (!args)
		return refnum;

	if (get_number("GOTO", args, &value))
	{
		my_goto_window(get_window_screennum(window->refnum), value);
		from_server = get_window_server(0);
	}
	return current_window_;
}


/*
 * /WINDOW GROW lines
 * This directs the client to expand the specified window by the specified
 * number of lines.  The number of lines should be a positive integer, and
 * the window's growth must not cause another window to be smaller than
 * the minimum of 3 lines.
 */
WINDOWCMD(grow)
{
	int	value;

	if (!window_is_valid(refnum))
		return 0;
	if (!args)
		return refnum;

	if (get_number("GROW", args, &value))
		resize_window(RESIZE_REL, refnum, value);
	return refnum;
}

/*
 * /WINDOW HIDE
 * This directs the client to remove the specified window from the current
 * (visible) screen and place the window on the client's invisible list.
 * A hidden window has no "screen", and so can not be seen, and does not
 * have a size.  It can be unhidden onto any screen.
 */
WINDOWCMD(hide)
{
	if (!window_is_valid(refnum))
		return 0;
	if (!args)
		return refnum;

	hide_window(refnum);
	return current_window_;
}

/*
 * /WINDOW HIDE_OTHERS
 * This directs the client to place *all* windows on the current screen,
 * except for the current window, onto the invisible list.
 */
WINDOWCMD(hide_others)
{
	int	tmp_;
	int	s_;

	if (!window_is_valid(refnum))
		return 0;
	if (!args)
		return refnum;

	/* There are no "others" to hide if the window isn't visible. */
	if (get_window_screennum(refnum) < 0)
		return refnum;

	s_ = get_window_screennum(refnum);
	tmp_ = 0;
	while (traverse_all_windows_on_screen2(&tmp_, s_))
	{
		if (get_window_refnum(tmp_) != get_window_refnum(refnum) && get_window_swappable(tmp_))
		{
			if (hide_window(tmp_))
				tmp_ = 0;
		}
	}

	return refnum;
}

/*
 * /WINDOW HOLD_INTERVAL
 * This determines how frequently the status bar should update the "HELD"
 * value when you are in holding mode.  The default is 10, so that your
 * status bar isn't constantly flickering every time a new line comes in.
 * But if you want better responsiveness, this is the place to change it.
 */
WINDOWCMD(hold_interval)
{
	char *arg;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	arg = next_arg(*args, args);
	if (arg)
	{
		int	size = my_atol(arg);

		if (size <= 0)
		{
			say("Hold interval must be a positive value!");
			return window->refnum;
		}
		window->hold_interval = size;
	}
	say("Window hold interval notification is %d", window->hold_interval);
	return refnum;
}

/*
 * /WINDOW HOLD_MODE
 * This arranges for the window to "hold" any output bound for it once
 * a full page of output has been completed.  Setting the global value of
 * HOLD_MODE is truly bogus and should be changed. XXXX
 */
WINDOWCMD(hold_mode)
{
	short	hold_mode;
	int	slider;
	int	i;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	if (window->holding_top_of_display)
		hold_mode = 1;
	else
		hold_mode = 0;

	if (!get_boolean("HOLD_MODE", args, &hold_mode))
		return 0;

	if (hold_mode && !window->holding_top_of_display)
	{
		window->holding_top_of_display = window->scrolling_top_of_display;
		slider = (window->hold_slider * window->display_lines) / 100;
		for (i = 0; i < slider; i++)
		{
			if (window->holding_top_of_display == window->display_ip)
				break;
			window->holding_top_of_display = window->holding_top_of_display->next;
		}
		recalculate_window_cursor_and_display_ip(window->refnum);
		window_body_needs_redraw(window->refnum);
		window_statusbar_needs_update(window->refnum);
	}
	if (!hold_mode && window->holding_top_of_display)
	{
		window->holding_top_of_display = NULL;
		recalculate_window_cursor_and_display_ip(window->refnum);
		window_body_needs_redraw(window->refnum);
		window_statusbar_needs_update(window->refnum);
	}

	return refnum;
}

/*
 * /WINDOW HOLD_SLIDER
 * This determines how far up the hold pointer should move when you hit
 * the return key (unhold action)
 */
WINDOWCMD(hold_slider)
{
	char *arg;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	arg = next_arg(*args, args);

	if (arg)
	{
		int	size = my_atol(arg);

		if (size < 0 || size > 100)
		{
			say("Hold slider must be 0 to 100!");
			return window->refnum;
		}
		window->hold_slider = size;
	}
	say("Window hold slider is %d", window->hold_slider);
	return refnum;
}

/*
 * /WINDOW INDENT (ON|OFF)
 *
 * When this is ON, 2nd and subsequent physical lines of display per logical
 * line of output are indented to the start of the second word on the first 
 * line.  This is essentially to the /set indent value.
 */
WINDOWCMD(indent)
{
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	if (!get_boolean("INDENT", args, &window->indent))
		return 0;
	return refnum;
}

/*
 * /WINDOW KILL
 * This arranges for the current window to be destroyed.  Once a window
 * is killed, it cannot be recovered.  Because every server must have at
 * least one window "connected" to it, if you kill the last window for a
 * server, the client will drop your connection to that server automatically.
 */
WINDOWCMD(kill)
{
	if (!args)
		return refnum;

	if (get_window_killable(refnum) != 1)
	{
		say("You cannot kill an unkillable window");
		return 0;
	}
	if (unlink_window(refnum))
		delete_window_contents(refnum);
	return current_window_;
}

/*
 * /WINDOW KILL_ALL_HIDDEN
 * This kills all of the hidden windows.  If the current window is hidden,
 * then the current window will probably change to another window.
 */
WINDOWCMD(kill_all_hidden)
{
	int	tmp_;
	int	reset_current_window = 0;

	if (!args)
		return refnum;

	tmp_ = get_invisible_list();
	while (tmp_ >= 1)
	{
		int	next = get_window_next(tmp_);

		if (get_window_killable(tmp_) == 1)
		{
		    if (get_window_refnum(tmp_) == refnum)
			reset_current_window = 1;
		    if (unlink_window(tmp_))
			    delete_window_contents(tmp_);
		}
		tmp_ = next;
	}

	if (reset_current_window)
		return current_window_;
	else
		return refnum;
}


/*
 * /WINDOW KILL_OTHERS
 * This arranges for all windows on the current screen, other than the 
 * current window to be destroyed.  Obviously, the current window will be
 * the only window left on the screen.  Connections to servers other than
 * the server for the current window will be implicitly closed.
 */
WINDOWCMD(kill_others)
{
	int	tmp_;

	if (!window_is_valid(refnum))
		return 0;
	if (!args)
		return refnum;

	if (get_window_screennum(refnum) < 0)
		tmp_ = get_screen_window_list(get_window_screennum(refnum));
	else
		tmp_ = get_invisible_list();

	while (tmp_ >= 1)
	{
		int	next = get_window_next(tmp_);

		if (get_window_killable(tmp_) == 1)
		{
		    if (get_window_refnum(tmp_) != get_window_refnum(refnum))
		    {
			if (unlink_window(tmp_))
				delete_window_contents(tmp_);
		    }
		}
		tmp_ = next;
	}

	return refnum;
}

WINDOWCMD(killable)
{
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window_is_valid(refnum))
		return 0;
	if (!args)
		return refnum;

	if (!get_boolean("KILLABLE", args, &window->killable))
		return 0;

	return refnum;
}

/*
 * /WINDOW KILLSWAP
 * This arranges for the current window to be replaced by the last window
 * to be hidden, and also destroys the current window.
 */
WINDOWCMD(killswap)
{
	if (!window_is_valid(refnum))
		return 0;
	if (!args)
		return refnum;

	if (get_window_killable(refnum) == 0)
	{
		say("You cannot KILLSWAP an unkillable window");
		return 0;
	}
	if (get_invisible_list() >= 1)
	{
		swap_window(refnum, get_invisible_list());
		if (unlink_window(refnum))
			delete_window_contents(refnum);
	}
	else
		say("There are no hidden windows!");

	return current_window_;
}

/*
 * /WINDOW LAST
 * This changes the current window focus to the window that was most recently
 * the current window *but only if that window is still visible*.  If the 
 * window is no longer visible (having been HIDDEN), then the next window
 * following the current window will be made the current window.
 */
WINDOWCMD(last)
{
	if (!window_is_valid(refnum))
		return 0;
	if (!args)
		return refnum;

	set_screens_current_window(get_window_screennum(refnum), 0);
	return current_window_;
}

/*
 * /WINDOW LASTLOG <size>
 * This changes the size of the window's lastlog buffer.  The default value
 * for a window's lastlog is the value of /set LASTLOG, but each window may
 * be independantly tweaked with this command.
 */
WINDOWCMD(lastlog)
{
	char *arg = next_arg(*args, args);
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	if (arg)
	{
		int size;

		if (!is_number(arg))
		{
		    say("/WINDOW LASTLOG takes a number (you said %s)", arg);
		    return 0;
		}

		if ((size = my_atol(arg)) < 0)
		{
			say("Lastlog size must be non-negative");
			return window->refnum;
		}
		window->lastlog_max = size;
		trim_lastlog(window->refnum);
		window_scrollback_needs_rebuild(window->refnum);
	}
	if (window->lastlog_max < window->display_lines * 2)
		window->lastlog_max = window->display_lines * 2;

	say("Lastlog size is %d", window->lastlog_max);
	return refnum;
}

/*
 * /WINDOW LASTLOG_LEVEL <level-description>
 * This changes the types of lines that will be placed into this window's
 * lastlog.  It is useful to note that the window's lastlog will contain
 * a subset (possibly a complete subset) of the lines that have appeared
 * in the window.  This setting allows you to control which lines are
 * "thrown away" by the window.
 */
WINDOWCMD(lastlog_mask)
{
	char *arg = next_arg(*args, args);;
	char *rejects = NULL;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	if (arg)
	{
	    if (str_to_mask(&window->lastlog_mask, arg, &rejects))
		standard_level_warning("/WINDOW LASTLOG_LEVEL", &rejects);
	}
	say("Lastlog level is %s", mask_to_str(&window->lastlog_mask));
	return refnum;
}

/*
 * /WINDOW LEVEL <level-description>
 * This changes the types of output that will appear in the specified window.
 * Note that for the given set of windows connected to a server, each level
 * may only appear once in that set.  When you add a level to a given window,
 * then it will be removed from whatever window currently holds it.  The
 * exception to this is the "DCC" level, which may only be set to one window
 * for the entire client.
 */
WINDOWCMD(level)
{
	char 	*arg;
	int	add = 0;
	Mask	mask;
	int	i;
	char *	rejects = NULL;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	mask_unsetall(&mask);
	if ((arg = next_arg(*args, args)))
	{
	    if (*arg == '+')
		add = 1, arg++;
	    else if (*arg == '-')
		add = -1, arg++;

	    if (str_to_mask(&mask, arg, &rejects))
		standard_level_warning("/WINDOW LEVEL", &rejects);
	    if (add == 0)
	    {
		mask_unsetall(&window->window_mask);
		add = 1;
	    }

	    for (i = 1; BIT_VALID(i); i++)
	    {
		if (add == 1 && mask_isset(&mask, i))
			mask_set(&window->window_mask, i);
		if (add == -1 && mask_isset(&mask, i))
			mask_unset(&window->window_mask, i);
	    }
	    revamp_window_masks(refnum);
	}
	say("Window level is %s", mask_to_str(&window->window_mask));
	return refnum;
}

/*
 * /WINDOW LIST
 * This lists all of the windows known to the client, and a breif summary
 * of their current state.
 */
WINDOWCMD(list)
{
	int	len = 6;
	int	cnw = get_int_var(CHANNEL_NAME_WIDTH_VAR);
	int	window_;

	if (!args)
		return refnum;

	if (cnw == 0)
		cnw = 12;	/* Whatever */

	for (window_ = 0; traverse_all_windows2(&window_); )
	{
		Window	*tmp = get_window_by_refnum_direct(window_);
		if (tmp->name && (strlen(tmp->name) > (size_t)len))
			len = strlen(tmp->name);
	}

	say(WIN_FORM,      	"Ref",
		      12, 12,	"Nick",	
		      len, len, "Name",
		      cnw, cnw, "Channel",
				"Query",
				"Server",
				"Level",
				empty_string);

	for (window_ = 0; traverse_all_windows2(&window_); )
		list_a_window(window_, len);

	return refnum;
}

/*
 * /WINDOW LOG ON|OFF
 * This sets the current state of the logfile for the given window.  When the
 * logfile is on, then any lines that appear on the window are written to the
 * logfile 'as-is'.  The name of the logfile can be controlled with
 * /WINDOW LOGFILE.  The default logfile name is <windowname>.<target|refnum>
 */
WINDOWCMD(log)
{
	const char *logfile;
	int add_ext = 1;
	char buffer[BIG_BUFFER_SIZE + 1];
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	if (!get_boolean("LOG", args, &window->log))
		return 0;

	if ((logfile = window->logfile))
		add_ext = 0;
	else if (!(logfile = get_string_var(LOGFILE_VAR)))
		logfile = empty_string;

	strlcpy(buffer, logfile, sizeof buffer);

	if (add_ext)
	{
		const char *title = empty_string;

		strlcat(buffer, ".", sizeof buffer);
		if ((title = get_window_echannel(window->refnum)))
			strlcat(buffer, title, sizeof buffer);
		else if ((title = get_window_equery(window->refnum)))
			strlcat(buffer, title, sizeof buffer);
		else
		{
			strlcat(buffer, "Window_", sizeof buffer);
			strlcat(buffer, ltoa(window->user_refnum), sizeof buffer);
		}
	}

	do_log(window->log, buffer, &window->log_fp);
	if (!window->log_fp)
		window->log = 0;

	return refnum;
}

/*
 * /WINDOW LOGFILE <filename>
 * This sets the current value of the log filename for the given window.
 * When you activate the log (with /WINDOW LOG ON), then any output to the
 * window also be written to the filename specified.
 */
WINDOWCMD(logfile)
{
	char *arg;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
	{
		new_free(&window->logfile);
		say("Window LOGFILE unset");
		return window->refnum;
	}

	arg = next_arg(*args, args);
	if (arg)
	{
		malloc_strcpy(&window->logfile, arg);
		say("Window LOGFILE set to %s", arg);
	}
	else if (window->logfile)
		say("Window LOGFILE is %s", window->logfile);
	else
		say("Window LOGFILE is not set.");

	/* 
	 * If the window is logging, close the old logfile,
	 * and re-open under the new name.
	 */
	if (window->log_fp)
	{
		if (!window->logfile)
		{
			say("Unsetting WINDOW LOGFILE turns off logging");
			do_log(0, NULL, &window->log_fp);
		}
		else
		{
			do_log(0, NULL, &window->log_fp);
			do_log(1, window->logfile, &window->log_fp);
		}
	}
	return refnum;
}

/*
 * /WINDOW LOG_REWRITE <newval>
 * If you have /window log on, you can set this to overrule the global
 * /set log_rewrite value for just this window's log.
 */
WINDOWCMD(log_rewrite)
{
	char *arg;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	arg = new_next_arg(*args, args);
	malloc_strcpy(&window->log_rewrite, arg);

	return refnum;
}

/*
 * /WINDOW LOG_MANGLE <newval>
 * If you have /window log on, you can set this to overrule the global
 * /set mangle_logfiles value for just this window's log.
 */
WINDOWCMD(log_mangle)
{
	char *	arg;
	char *	nv;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	arg = new_next_arg(*args, args);
	window->log_mangle = parse_mangle(arg, window->log_mangle, &nv);
	malloc_strcpy(&window->log_mangle_str, nv);
	new_free(&nv);

	return refnum;
}


/*
 * Usage:	/WINDOW MERGE <window>		Merge this window into <window>
 * 		/WINDOW NAME "one two"		Double quoted names are OK
 *		/WINDOW NAME -			Unset (clear) the window's name
 *		/WINDOW -NAME			Unset (clear) the window's name
 *		/WINDOW NAME			Output the window's name
 * 
 * The window's NAME is used for status expando %R and can be used to refer to
 * a window (along side its refnum) in the /WINDOW command.
 *
 * Warnings:
 *	/WINDOW -MERGE		is a no-op
 *
 * Return value:
 *	The window represented by <window>
 *
 * Errors:
 *	If the target window <window> is not a valid window, it is an error.
 *	If the target window <window> is connected to a different server, it is an error
 *
 * Side effects:
 *	Upon success, the current window is killed
 *	Upon success, the current window is changed to <window>
 *	The following things in the current window are moved to <window>:
 *	 * All lastlog items (which will cascade to scrollback)
 *	 * All scrollback items
 *	 * All channels
 *	 * All /LOG files
 *	 * All /TIMERs
 *	 * All /WINDOW ADDs (nicklists)
 *	The scrollback for <window> will be rebuilt
 *	The following things in the current window will be closed:
 *	 * /WINDOW QUERY
 *	 * /WINDOW LOG/LOGFILE
 *
 * Problems:
 *	There are probably other things that track windows that 
 *	need to be migrated/handled
 */
WINDOWCMD(merge)
{
	int	newref;

	if (!window_is_valid(refnum))
		return 0;
	if (!args)
		return refnum;

	if ((newref = get_window2("MERGE", args)))
	{
		if (get_window_server(refnum) != get_window_server(newref))
		{
			say("Cannot merge window %d into window %d - different server", 
				get_window_user_refnum(refnum),
				get_window_user_refnum(newref));
			return 0;
		}

		move_all_lastlog(refnum, newref);
		channels_merge_windows(refnum, newref);
		logfiles_merge_windows(refnum, newref);
		timers_merge_windows(refnum, newref);

		Window *window = get_window_by_refnum_direct(refnum);
		Window *tmp = get_window_by_refnum_direct(newref);
		while (window->nicks)
		{
			List *h;

			h = window->nicks;
			window->nicks = h->next;
			add_item_to_list(&tmp->nicks, h);
		}

		windowcmd_kill(refnum, args);
		make_window_current_by_refnum(newref);
		return newref;
	}

	return 0;
}

/*
 * Usage:	/WINDOW MOVE <number>	Move window up or down <number> positions
 *
 * Move a visible (split) window up or down <number> spots on the screen.
 * <number> may wrap both directions.  That is to say, if you go to the bottom
 * window on a screen and /WINDOW MOVE 2, it will move to be the 2nd window from
 * the top.  Similarly, if you /WINDOW MOVE -2 from the top window it will become
 * the 2nd window from the bottom.
 *
 * Warnings:
 *	When <number> == 0			is a no-op
 *	When window is hidden			is a no-op
 *	When there is only one window on screen	is a no-op
 *	/WINDOW -MOVE				is a no-op
 *
 * Side effects:
 *	The order of the windows will be changed
 */
WINDOWCMD(move)
{
	int	value;

	if (!window_is_valid(refnum))
		return 0;
	if (!args)
		return refnum;

	if (get_number("MOVE", args, &value))
		move_window(refnum, value);
	return refnum;
}

/*
 * Usage:	/WINDOW MOVE_TO <number>	Move window to <number>th from the top
 *
 * Move a visible (split) window to an absolute order position on the screen 
 * (ie, /WINDOW MOVE_TO 1 makes it the top window; 2 the second-to-top window, etc)
 *
 * If <number> is greater than the number of windows on the screen, then it is moved
 * to the bottom place (ie, /window move_to 999 moves window to bottom)
 *
 * Warnings:
 *	When <number> < 1			is a no-op
 *	When window is hidden			is a no-op
 *	When there is only one window on screen	is a no-op
 *	/WINDOW -MOVE_TO			is a no-op
 *
 * Side effects:
 *	The order of the windows will be changed
 */
WINDOWCMD(move_to)
{
	int	value;

	if (!window_is_valid(refnum))
		return 0;
	if (!args)
		return refnum;

	if (get_number("MOVE_TO", args, &value))
		move_window_to(refnum, value);
	return refnum;
}

/*
 * Usage:	/WINDOW NAME <string>		Set the window's name
 * 		/WINDOW NAME "one two"		Double quoted names are OK
 *		/WINDOW NAME -			Unset (clear) the window's name
 *		/WINDOW -NAME			Unset (clear) the window's name
 *		/WINDOW NAME			Output the window's name
 * 
 * The window's NAME is used for status expando %R and can be used to refer to
 * a window (along side its refnum) in the /WINDOW command.
 *
 * Warnings:
 *	/WINDOW NAME ""		is a no-op
 *	/WINDOW NAME <number>	is a no-op
 *	/WINDOW NAME <name>	where <name> is already in use is a no-op
 */
WINDOWCMD(name)
{
	char *arg;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
	{
windowcmd_name__unset_name:
		new_free(&window->name);
		window_statusbar_needs_update(window->refnum);
		return refnum;
	}

	if ((arg = new_next_arg(*args, args)))
	{
		/* /window name -  is the same as /window -name */
		if (!strcmp(arg, "-"))
			goto windowcmd_name__unset_name;

		/* 
		 * /window name to existing name -- permit this, to allow
		 * the user to change case of characters in the name.
		 */
		else if (window->name && (my_stricmp(window->name, arg) == 0))
		{
			malloc_strcpy(&window->name, arg);
			window_statusbar_needs_update(window->refnum);
		}
		else if (is_number(arg))
			say("You cannot name your window a number, that could confuse your script.");
		else if (!is_window_name_unique(arg))
			say("%s is not unique!", arg);
		else
		{
			malloc_strcpy(&window->name, arg);
			window_statusbar_needs_update(window->refnum);
		}
	}
	else
		say("You must specify a name for the window!");

	return refnum;
}

/*
 * Usage:	/WINDOW NEW
 * 		/WINDOW -NEW	(is permitted but not recommended)
 *
 * Create a new visible ("split") window, connected to the same server
 * as the current window.
 *
 * Return value:
 *	The new window is returned
 *
 * Warnings:
 *	/WINDOW NEW in dumb mode	is a no-op
 *
 * Side effects;
 *	The new window is made the current window
 *	The new window is made the input window
 *	Other windows on the screen will be resized
 */
WINDOWCMD(new)
{
	int		new_refnum;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if ((new_refnum = new_window(get_window_screennum(window->refnum))) < 0)
		return 0;

	make_window_current_by_refnum(new_refnum);
	return new_refnum;
}

/*
 * Usage:	/WINDOW NEW_HIDE
 * 		/WINDOW -NEW_HIDE	(is permitted but not recommended)
 *
 * Create a new window that is hidden from its inception.
 * This is an atomic version of /WIDNDOW NEW HIDE.
 */
WINDOWCMD(new_hide)
{
	new_window(-1);
	return refnum;
}

/*
 * Usage:	/WINDOW NEXT
 * 		/WINDOW -NEXT	(is permitted but not recommended)
 *
 * Do a /WINDOW SWAP with the hidden window whose user refnum is 
 * immediately after this window's.
 *
 * Conceptually, every window is sorted according to its "user_refnum".
 * You can use /WINDOW NEXT to cycle through your invisible windows
 * in user_refnum sort order.  It will wrap around to the highest refnum
 * when you reach the lowest hidden window.
 *
 * Return value:
 *	The next higher hidden window is returned
 *
 * Errors:
 *	No hidden windows		is an error
 *
 * Warnings:
 *	No warnings
 *
 * Side effects;
 *	The current window is hidden
 *	The next higher hidden window is made visible
 *	The next higher hidden window is made the current window
 *	The next higher hidden window is made the current input window
 */
WINDOWCMD(next)
{
	Window *tmp = NULL;
	Window *next = NULL;
	Window *smallest = NULL;
	Window *window = get_window_by_refnum_direct(refnum);
	int	tmp_;

	if (!window)
		return 0;
	smallest = window;
	tmp_ = 0;
	while (traverse_all_windows_on_screen2(&tmp_, -1))
	{
		tmp = get_window_by_refnum_direct(tmp_);
		if (!tmp->swappable || tmp->skip)
			continue;
		if (tmp->user_refnum < smallest->user_refnum)
			smallest = tmp;
		if ((tmp->user_refnum > window->user_refnum)
		    && (!next || (tmp->user_refnum < next->user_refnum)))
			next = tmp;
	}

	if (!next)
		next = smallest;

	if (next == NULL || next == window)
	{
		say("There are no hidden windows");
		return 0;
	}

	swap_window(window->refnum, next->refnum);
	return current_window_;
}

/*
 * Usage:	/WINDOW NOTIFY		Display current value
 *		/WINDOW NOTIFY ON	Turn on 
 *		/WINDOW NOTIFY OFF	Turn off
 *		/WINDOW -NOTIFY		Turn off (silently)
 *
 * When this is on, a window will "notify" (show up in %F)
 * automatically when it is:
 *   1) Hidden, and has
 *   2) Output to /window notify_level, and
 *   3. This setting (/window notify) is ON.
 * 
 * Warnings:
 *	Invalid value		is a no-op
 */
WINDOWCMD(notify)
{
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;

	if (!args)
		window->notify_when_hidden = 0;
	else
	{
		if (!get_boolean("NOTIFY", args, &window->notify_when_hidden))
			return 0;
	}

	return refnum;
}

/*
 * Usage:	/WINDOW NOTIFIED	Display current value
 *		/WINDOW NOTIFIED ON	Turn on 
 *		/WINDOW NOTIFIED OFF	Turn off
 *		/WINDOW -NOTIFIED	Turn off (silently)
 *
 * Normally a window "notifies" when it is
 *  1) Hidden, and has
 *  2) Output to /window notifiy_level, and
 *  3) /window notify is ON
 * You can manually notify or un-notify a window with this verb.
 *
 * Warnings:
 *	Invalid value		is a no-op
 */
WINDOWCMD(notified)
{
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;

	if (!args)
		window->notified = 0;
	else 
	{
		if (!get_boolean("NOTIFIED", args, &window->notified))
			return 0;
	}

	return refnum;
}

/*
 * Usage:	/WINDOW NOTIFY_LEVEL <levels> 
 *		/WINDOW -NOTIFY_LEVEL		(Reset to /set notify_level)
 *
 * Set the per-window's notify level mask.
 * When a window is invisible, output to that window can cause the window to
 * "notify" you via the status bar %F expando.  
 * This mask controls which levels qualify as worthy of "notification".
 * (For example, you may only want to notify on MSGS,PUBLICS)
 *
 * When you create a new window, it copies the then-current /set notify_level.
 * When you /set notify_level
 *	1. It changes the default value for new windows
 *	2. It behaves _as if_ you did /WINDOW NOTIFY_LEVEL <newval>
 * that is to say, it only updates the _current window_.
 *
 * Thus if you want to update other windows, you need to set them yourself.
 *
 * Warnings:
 *	Any invalid levels in <levels> is a no-op
 *	/WINDOW NOTIFY_LEVEL "" is a no-op
 *
 * Side effects:
 *	The command always outputs the final notify level
 */
WINDOWCMD(notify_mask)
{
	char *arg;
	char *rejects = NULL;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;

	if (!args)
		window->notify_mask = real_notify_mask();
	else
	{
		if ((arg = next_arg(*args, args)))
		    if (str_to_mask(&window->notify_mask, arg, &rejects))
			standard_level_warning("/WINDOW NOTIFY_LEVEL", &rejects);
	}

	say("Window notify level is %s", mask_to_str(&window->notify_mask));
	return refnum;
}

/*
 * Usage:	/WINDOW NOTIFY_NAME <string>	Set the notify name
 * 		/WINDOW NOTIFY_NAME "one two"	Double quoted names are OK
 *		/WINDOW NOTIFY_NAME -		Unset (clear) the notify name
 *		/WINDOW -NOTIFY_NAME		Unset (clear) the notify name
 *		/WINDOW NOTIFY_NAME		Output the notify name
 * 
 * The NOTIFY_NAME overrides what shows up in the %F status expando.
 *
 * Errors:
 *	No errors
 *
 * Warnings:
 *	No warnings
 *
 * Side effects;
 *	The operation always outputs something
 */
WINDOWCMD(notify_name)
{
	char *arg;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
	{
		new_free(&window->notify_name);
		window_statusbar_needs_update(window->refnum);
		say("Window NOTIFY NAME unset");
	}

	else if ((arg = new_next_arg(*args, args)))
	{
		/* /window name -  unsets the window name */
		if (!strcmp(arg, "-"))
		{
			new_free(&window->notify_name);
			window_statusbar_needs_update(window->refnum);
			say("Window NOTIFY NAME unset");
		}

		/* /window name to existing name -- allow to change case */
		else if (window->notify_name && (my_stricmp(window->notify_name, arg) == 0))
		{
			malloc_strcpy(&window->notify_name, arg);
			window_statusbar_needs_update(window->refnum);
			say("Window NOTIFY NAME is %s", window->notify_name);
		}

		else
		{
			malloc_strcpy(&window->notify_name, arg);
			window_statusbar_needs_update(window->refnum);
			say("Window NOTIFY NAME changed to %s", window->notify_name);
		}
	}
	else if (window->notify_name)
		say("Window NOTIFY NAME is %s", window->notify_name);
	else
		say("Window NOTIFY NAME is unset");

	return refnum;
}

/*
 * Usage:	/WINDOW NUMBER <refnum>
 * 
 * Change the window's user refnum to <refnum>.
 * If <refnum> is already in use in another window, swap <refnum>s with that other window.
 *
 * Errors:
 *	No errors
 *
 * Warnings:
 *	/WINDOW -NUMBER		is a no-op
 *	/WINDOW NUMBER ""	is a no-op
 *	<refnum> < 1		is a no-op
 *	<refnum> > 1000		is a no-op
 *
 * Side effects;
 *	Success causes a status bar update
 */
WINDOWCMD(number)
{
	char 	*arg;
	int 	i, oldref, newref;
	Window *window = get_window_by_refnum_direct(refnum);
	Window *other;

	if (!window)
		return 0;
	if (!args)
		return refnum;

	if ((arg = next_arg(*args, args)))
	{
		i = my_atol(arg);
		if (i >= 1 && i < INTERNAL_REFNUM_CUTOVER)
		{
			oldref = window->user_refnum;
			newref = i;

			if ((other = get_window_by_refnum_direct(i)))
			{
				other->user_refnum = oldref;
				windows[oldref] = other;
			}
			else
				windows[oldref] = NULL;

			window->user_refnum = newref;
			windows[newref] = window;

			if (other)
				window_statusbar_needs_update(other->refnum);
			window_statusbar_needs_update(window->refnum);
		}
		else
			say("Window number must be between 0 and %d", INTERNAL_REFNUM_CUTOVER);
	}
	else
		say("Window number missing");

	return refnum;
}

/*
 * Usage:	/WINDOW POP
 * 
 * Make the window most recently /WINDOW PUSHed the current window
 * If that window no longer exists, the one PUSHed before that is used.
 * If that window is hidden, behave as though you did /WINDOW SHOW 
 * If that window is not hidden, behave as though you did /WINDOW REFNUM
 *
 * Return value:
 *	The previously PUSHed window is returned
 *
 * Errors:
 *	No errors
 *
 * Warnings:
 *	/WINDOW -POP						is a no-op
 *	/WINDOW POP in a hidden window 				is a no-op
 *	/WINDOW POP when no previously PUSHes have occurred	is a no-op
 *	/WINDOW POP when all previous PUSHes have been KILLed	is a no-op
 *
 * Side effects;
 *	The previously PUSHed window is made the current window
 *	The previously PUSHed window is made the current input window
 */
WINDOWCMD(pop)
{
	int 		stack_refnum;
	WindowStack *	to_delete, *next;
	Window *	win = NULL;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	if (get_window_screennum(window->refnum) < 0)
	{
		say("Cannot pop the window stack from a hidden window");
		return refnum;
	}

	while (get_screen_window_stack(get_window_screennum(window->refnum)))
	{
		to_delete = get_screen_window_stack(get_window_screennum(window->refnum));
		stack_refnum = to_delete->refnum;
		next = to_delete->next;
		new_free((char **)&to_delete);
		set_screen_window_stack(get_window_screennum(window->refnum), next);

		if (!(win = get_window_by_refnum_direct(stack_refnum)))
			continue;

		if (get_window_screennum(win->refnum) < 0)
			set_screens_current_window(get_window_screennum(win->refnum), win->refnum);
		else
			show_window(win->refnum);
	}

	if (!get_screen_window_stack(get_window_screennum(window->refnum)) && !win)
		say("The window stack is empty!");

	if (win)
		return win->refnum;
	else
		return 0;
}

/*
 * Usage:	/WINDOW PREVIOUS
 * 		/WINDOW -PREVIOUS	(is permitted but not recommended)
 *
 * Do a /WINDOW SWAP with the hidden window whose user refnum is 
 * immediately before this window's.
 *
 * Conceptually, every window is sorted according to its "user_refnum".
 * You can use /WINDOW PREVIOUS to cycle through your invisible windows
 * in user_refnum sort order.  It will wrap around to the highest refnum
 * when you reach the lowest hidden window.
 *
 * Return value:
 *	The next lower hidden window is returned
 *
 * Errors:
 *	No hidden windows		is an error
 *
 * Warnings:
 *	No warnings
 *
 * Side effects;
 *	The current window is hidden
 *	The next lower hidden window is made visible
 *	The next lower hidden window is made the current window
 *	The next lower hidden window is made the current input window
 */
WINDOWCMD(previous)
{
	Window 	*tmp = NULL,
		*previous = NULL,
		*largest,
		*window = get_window_by_refnum_direct(refnum);
	int	tmp_;

	if (!window)
		return 0;

	/* It is ok for 'args' to be NULL -- SWAP_PREVIOUS_WINDOW uses this. */

	largest = window;
	tmp_ = 0;
	while (traverse_all_windows_on_screen2(&tmp_, -1))
	{
		tmp = get_window_by_refnum_direct(tmp_);

		if (!tmp->swappable || tmp->skip)
			continue;
		if (tmp->user_refnum > largest->user_refnum)
			largest = tmp;
		if ((tmp->user_refnum < window->user_refnum)
		    && (!previous || tmp->user_refnum > previous->user_refnum))
			previous = tmp;
	}

	if (!previous)
		previous = largest;

	if (previous == NULL || previous == window)
	{
		say("There are no hidden windows to swap in");
		return 0;
	}

	swap_window(window->refnum, previous->refnum);
	return previous->refnum;
}

/*
 * Usage:	/WINDOW PUSH
 * 
 * Save the refnum of the current window to its screen's saved window stack.
 * Every screen has a stack of refnums which have been PUSHed which can then
 * later be POPped.  Because the stack of refnums uses the internal refnums,
 * you can renumber windows without corrupting the stack.
 *
 * Errors:
 *	No errors
 *
 * Warnings:
 *	/WINDOW -PUSH						is a no-op
 *	/WINDOW PUSH in a hidden window 			is a no-op
 */
WINDOWCMD(push)
{
	WindowStack *	new_ws;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	if (get_window_screennum(window->refnum) < 0)
	{
		say("Cannot push a hidden window onto the window stack");
		return refnum;
	}

	new_ws = (WindowStack *) new_malloc(sizeof(WindowStack));
	new_ws->refnum = window->refnum;
	new_ws->next = get_screen_window_stack(get_window_screennum(window->refnum));
	set_screen_window_stack(get_window_screennum(window->refnum), new_ws);
	return refnum;
}

/*
 * Usage:	/WINDOW QUERY target	Add 'target' to target list and make current query
 *		/WINDOW QUERY		Remove the current query target (If there are other
 *					targets, they become the new current query)
 * 		/WINDOW -QUERY		Remove all targets for the window
 *
 * Add a target to the window and ensure it is the current query
 */
EXT_WINDOWCMD(query)
{
	const char *	targets;
	int		l;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;

	/*
	 * /WINDOW -QUERY
	 *	Remove all items from the target list, and there will be no
	 * 	query after this.
	 */
	if (!args)
	{
		const char *	oldnick;

		while ((oldnick = get_window_equery(refnum)))
			remove_window_target(refnum, get_window_server(refnum), oldnick);

		l = message_setall(refnum, get_who_from(), get_who_level());
		say("All conversations in this window ended");
		pop_message_from(l);
		
		recheck_queries(refnum);
		window_statusbar_needs_update(refnum);
	}

	/*
 	 * /WINDOW QUERY <target>
	 *	Add (target> to the target list and make it the current query
	 */
	else if ((targets = new_next_arg(*args, args)))
	{
		char *	a = LOCAL_COPY(targets);
		const char *	target;

		while (a && *a)
		{
			if ((target = next_in_comma_list(a, &a)))
				if ((target = get_target_special(refnum, get_window_server(refnum), target)))
					add_window_target(refnum, get_window_server(refnum), target, 1);
		}
	}

	/*
	 * /WINDOW QUERY
	 * 	Remove the current query from the target list (whatever it is)
	 *	If there are other targets, they will become the new /query
	 */
	else
	{
		const char *	oldnick;

		if ((oldnick = get_window_equery(refnum)))
			remove_window_target(refnum, get_window_server(refnum), oldnick);
	}

	return refnum;
}

/*
 * Usage:	/WINDOW REBUILD_SCROLLBACK
 *
 * Request an unconditional rebuild of the window's scrollback.
 * Normally you would never need to do this.
 * The rebuild doesn't occur until the next update point.
 * The rebuild happens "later"
 *
 * Errors:
 *	No hidden windows		is an error
 *
 * Warnings:
 *	/WINDOW -REBUILD_SCROLLBACK	is a no-op
 */
WINDOWCMD(rebuild_scrollback)
{
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	window->rebuild_scrollback = 1;
	return refnum;
}


/*
 * /WINDOW REJOIN #channel
 * /WINDOW REJOIN #channel key
 * /WINDOW REJOIN #channel1,#channel2
 * /WINDOW REJOIN #channel1,#channel2 key1,key2
 *
 * - This is the back-end of the JOIN command
 * - The current server must be connected
 * - Opposed to /window channel, this never "steals" channels that we're not yet on
 *   (that makes it suitable for use in /on connect)
 * 
 * For every channel
 * - If it is -invite, join the last invited channel
 * - If you are on the channel, it is moved to this window
 * - If you are not on the channel, and a window is waiting for it, it will go to that window
 * - If you are not on the channel, and nobody is waiting for it, it will go to this window
 * At the end we send out a JOIN if there are any channels to join
 *
 * 
 * /WINDOW REJOIN <#channel>[,<#channel>]
 * Here's the plan:
 *
 * For each channel, assuming from_server:
 * - If we are already on the channel:
 *   - If the current window is connected to from_server:
 *     -> Move the channel to the current window.
 *   - If the current window is NOT connected to from_server:
 *     -> Do nothing.
 * - If we are NOT already on the channel:
 *   - If there is a window that looks like it owns this channel:
 *     -> Join the channel in that window.
 *   - If there is NOT a window that looks like it owns this channel:
 *     - If the current window is connected to from_server:
 *       -> Join the channel in the current window
 *     - If the current window is NOT connected to from_server:
 *       -> Find a window connected to from_server and join channel there.
 * -endif
 *
 * /WINDOW REJOIN is a terminal verb: it always slurps up all of the rest
 * of the command as it's arguments and returns NULL and you cannot do any
 * more operations after it is finished.
 *
 * If this function looks insane, it's because I wrote it after a long
 * day of coding in java, and so I am, in fact, commitable right now.
 */
EXT_WINDOWCMD(rejoin)
{
	char *		channels;
	const char *	chan;
	char *		keys = NULL;
	char *		newchan = NULL;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	/* First off, we have to be connected to join */
	if (from_server == NOSERV || !is_server_registered(from_server))
	{
		say("You are not connected to a server.");
		return refnum;
	}

	/* And the user must want to join something */
	/* Get the channels, and the keys. */
	if (!(channels = new_next_arg(*args, args)))
	{
		say("REJOIN: Must provide a channel argument");
		return refnum;
	}
	keys = new_next_arg(*args, args);

	/* Iterate over each channel name in the list. */
	while (*channels && (chan = next_in_comma_list(channels, &channels)))
	{
		/* Handle /join -i, which joins last invited channel */
		if (!my_strnicmp(chan, "-invite", 2))
                {
                        if (!(chan = get_server_invite_channel(window->server)))
			{
                                say("You have not been invited to a channel!");
				continue;
			}
                }

		/* Handle /join 0, which parts all current channels */
		if (!strcmp(chan, "0"))
		{
			send_to_server("JOIN 0");
			continue;
		}

		/* Sanity check the channel for the user */
                if (!is_channel(chan))
		{
                        say("CHANNEL: %s is not a valid channel name", chan);
			continue;
		}

		/*
		 * Now comes all the fun!  THere are a whole bunch of 
		 * cases we could deal with right now -- see the comment
		 * block above the function for the particulars, but 
		 * basically if we are ON the channel, we want to move it
		 * to the current window for the user.  This lets /join
		 * act like /window channel.  But if we are NOT on the
		 * channel, then we want to find if there is any window 
		 * that thinks it owns it -- this lets the user do a /join
		 * in /on connect, and have everything go to the right window.
		 * If nobody owns the channel, then we take it.  If we can't
		 * take it (wrong server) then we find somebody else to take
		 * it.  If we can't find somebody else, then we're screwed.
		 */
		if (im_on_channel(chan, window->server))
		{
			move_channel_to_window(chan, window->server, 
						0, window->refnum);
			say("You are now talking to channel %s", 
				check_channel_type(chan));
		}

		/* I am NOT on the channel. */
		else
		{
			Window *owner = NULL;
			Window *anybody = NULL;
			int	window_;

			/* Go hunt for the owner. */
			for (window_ = 0; traverse_all_windows2(&window_); )
			{
			    Window *w = get_window_by_refnum_direct(window_);

			    if (w->server != from_server)
				continue;
			    if (find_in_list(window->waiting_chans, chan))
			    {
				owner = w;
				break;
			    }

			    /* Take anybody on this server... */
			    if (anybody == NULL)
				anybody = w;
			}

			/* If there is no owner, then we get first crack. */
			if (!owner && window->server == from_server)
				owner = window;

			/* If there is still no owner, take anybody. */
			if (!owner && anybody)
				owner = anybody;

			/* If there is still no owner, we messed up. */
			if (!owner)
				panic(1, "window_rejoin: There are no windows for this server, and there should be.");

			add_waiting_channel(owner->refnum, chan);
			malloc_strcat_wordlist(&newchan, ",", chan);
		}
	}
	if (newchan)
	{
		if (keys)
			send_to_server("JOIN %s %s", newchan, keys);
		else
			send_to_server("JOIN %s", newchan);
		new_free(&newchan);
	}

	return refnum;
}

WINDOWCMD(refnum)
{
	int	tmp_;

	if (!args)
		return refnum;

	if ((tmp_ = get_window2("REFNUM", args)) < 1)
		return 0;

	make_window_current_by_refnum(tmp_);
	if (get_window_screennum(tmp_) >= 0)
		set_screens_current_window(get_window_screennum(tmp_), tmp_);
	return tmp_;
}

WINDOWCMD(refnum_or_swap)
{
	int	tmp_;

	if (!window_is_valid(refnum))
		return 0;
	if (!args)
		return refnum;

	if ((tmp_ = get_window2("REFNUM_OR_SWAP", args)) < 1)
		return 0;

	if (get_window_screennum(tmp_) >= 0)
	{
		make_window_current_by_refnum(tmp_);
		set_screens_current_window(get_window_screennum(tmp_), tmp_);
	}
	else
		swap_window(refnum, tmp_);

	return tmp_;
}

WINDOWCMD(refresh)
{
	if (!args)
		return refnum;

	update_all_status();
	update_all_windows();
	return refnum;
}

WINDOWCMD(remove)
{
	char *arg;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	if ((arg = next_arg(*args, args)))
	{
		char *	a = LOCAL_COPY(arg);
		char *	target;

		while (a && *a)
		{
			target = next_in_comma_list(a, &a);
			remove_window_target(refnum, window->server, target);
		}
	        window_statusbar_needs_update(window->refnum);
	}

	return refnum;
}

/* This is a NO-OP now -- every window is a scratch window */
WINDOWCMD(scratch)
{
	short scratch = 0;
#if 0
	Window *	window = get_window_by_refnum_direct(refnum);
#endif

	if (!args)
		return refnum;

	if (!get_boolean("SCRATCH", args, &scratch))
		return 0;

	return refnum;
}

WINDOWCMD(screen_debug)
{
	int	screen;

	if (!window_is_valid(refnum))
		return 0;

	if ((screen = get_window_screennum(refnum)))
		screen_window_dump(screen);
	else
		yell("invisible window screen dump not supported (yet)");
	return refnum;
}

/* XXX - Need to come back and fix this... */
WINDOWCMD(scroll)
{
	short 	scroll = 0;
#if 0
	Window *	window = get_window_by_refnum_direct(refnum);
#endif

	if (!args)
		return refnum;

	if (!get_boolean("SCROLL", args, &scroll))
		return 0;

	return refnum;
}

WINDOWCMD(scrolladj)
{
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	if (!get_boolean("SCROLLADJ", args, &window->scrolladj))
		return 0;

	return refnum;
}

WINDOWCMD(scroll_lines)
{
	int new_value = 0;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	if (!args || !*args || !**args)
	{
		if (window->scroll_lines < 0)
			say("Window SCROLL_LINES is <default>");
		else
			say("Window SCROLL_LINES is %d", window->scroll_lines);
		return 0;
	}

	if (get_number("SCROLL_LINES", args, &new_value))
	{
		if (new_value == 0 || new_value < -1)
		{
			say("Window SCROLL_LINES must be -1 or a positive value");
			return 0;
		}
		else if (new_value > window->display_lines)
		{
			say("Maximum lines that may be scrolled is %d [%d]", 
				window->display_lines, new_value);
			new_value = get_window_display_lines(current_window_);
		}

		window->scroll_lines = new_value;
		if (window->scroll_lines < 0)
			say("Window SCROLL_LINES is <default>");
		else
			say("Window SCROLL_LINES is %d", window->scroll_lines);
	}

	return refnum;
}

WINDOWCMD(scrollback)
{
	int val;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	if (args && *args && **args)
	{
		if (get_number("SCROLLBACK", args, &val))
		{
			if (val < window->display_lines * 2)
				window->display_buffer_max = window->display_lines * 2;
			else
				window->display_buffer_max = val;
		}
	}

	say("Window scrollback size set to %d", window->display_buffer_max);
	return refnum;
}


WINDOWCMD(scroll_backward)
{
	int	val;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	if (args && *args && **args)
	{
		if (get_number("SCROLL_BACKWARD", args, &val))
		{
			if (val == 0)
				window_scrollback_backward(refnum);
			else
				window_scrollback_backwards_lines(refnum, val);
		}
	}
	else
		window_scrollback_backward(refnum);

	return refnum;
}

WINDOWCMD(scroll_end)
{
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	window_scrollback_end(refnum);
	return refnum;
}

WINDOWCMD(scroll_forward)
{
	int	val;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	if (args && *args && **args)
	{
		if (get_number("SCROLL_FORWARD", args, &val))
		{
			if (val == 0)
				window_scrollback_forward(refnum);
			else
				window_scrollback_forwards_lines(refnum, val);
		}
	}
	else
		window_scrollback_forward(refnum);

	return refnum;
}

WINDOWCMD(scroll_start)
{
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	window_scrollback_start(refnum);
	return refnum;
}

regex_t *last_regex = NULL;

static int	new_search_term (const char *arg)
{
	int	errcode;

	if (last_regex)
	{
		debuglog("clearing last search term");
		regfree(last_regex);
	}
	else
	{
		debuglog("Creating first search term");
		last_regex = new_malloc(sizeof(*last_regex));
	}

	memset(last_regex, 0, sizeof(*last_regex));
	debuglog("compiling regex: %s", arg);
	errcode = regcomp(last_regex, arg, 
				REG_EXTENDED | REG_ICASE | REG_NOSUB);
	if (errcode != 0)
	{
		char	errstr[1024];

		regerror(errcode, last_regex, errstr, sizeof(errstr));
		debuglog("regex compile failed: %s : %s", arg, errstr);
		say("The regex [%s] isn't acceptable because [%s]", 
				arg, errstr);
		new_free((char **)&last_regex);
		return -1;
	}
	debuglog("regex appeared to compile successfully");
	return 0;
}

WINDOWCMD(search_back)
{
	char *arg;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	if ((arg = new_next_arg(*args, args)))
	{
		if (new_search_term(arg))
			return refnum;
	}

	if (last_regex)
		window_scrollback_to_string(refnum, last_regex);
	else
		say("Need to know what to search for");

	return refnum;
}

WINDOWCMD(search_forward)
{
	char *arg;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	if ((arg = new_next_arg(*args, args)))
	{
		if (new_search_term(arg))
			return refnum;
	}

	if (last_regex)
		window_scrollforward_to_string(refnum, last_regex);
	else
		say("Need to know what to search for");

	return refnum;
}

static	int	last_scroll_seconds_interval = 0;

WINDOWCMD(scroll_seconds)
{
	int	val;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	if (args && *args && **args)
	{
		if (!get_number("SCROLL_SECONDS", args, &val))
			return refnum;
	}
	else
		val = last_scroll_seconds_interval;

	last_scroll_seconds_interval = val;

	if (val < 0)
	{
		/* Scroll back "-val" seconds */
	}
	else if (val == 0)
		return refnum;
	else
	{
		/* Scroll forward "val" seconds */
	}

	return refnum;
}

#if 0
WINDOWCMD(scroll_toseconds)
{
	int	val;
	time_t	right_now, when;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	if (args && *args && **args)
	{
		if (!get_number("SCROLL_TOSECONDS", args, &val))
			return refnum;
	}
	else
		return refnum;

	right_now = time(NULL);
	if (val <= 0)
		return refnum;

	when = right_now - val;
	/* Scroll to the first entry newer than 'when' */

	return refnum;
}
#endif


WINDOWCMD(skip)
{
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	if (!get_boolean("SKIP", args, &window->skip))
		return 0;

	return refnum;
}

WINDOWCMD(server)
{
	char *	arg;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window_is_valid(refnum))
		return 0;
	if (!args)
		return refnum;

	if ((arg = next_arg(*args, args)))
	{
		int i;
		int status;

		if ((i = str_to_servref_with_update(arg)) == NOSERV)
			i = str_to_newserv(arg);
		from_server = i;
		status = get_server_state(i);

		/*
		 * This stuff only happens if the window is changing server.  
		 * If you're not changing server, that's a no-op.
		 */
		if (get_window_server(refnum) != i)
		{
			/*
			 * Lose our channels
			 */
			destroy_window_waiting_channels(refnum);
			reassign_window_channels(refnum);

			/*
			 * Associate ourselves with the new server.
			 */
			window_change_server(refnum, i);

			if (status > SERVER_RECONNECT && status < SERVER_EOF)
			{
			    if (old_server_lastlog_mask) {
				renormalize_window_levels(refnum, *old_server_lastlog_mask);
				revamp_window_masks(refnum);
			    }
			}
			else if (status > SERVER_ACTIVE)
				disconnectcmd("RECONNECT", NULL, NULL);
				/* set_server_status(i, SERVER_RECONNECT); */

			malloc_strcpy(&window->original_server_string, arg);
		}
		else
		{
			if (status == SERVER_CLOSED)
				disconnectcmd("RECONNECT", NULL, NULL);
		}
	}
	else
		display_server_list();

	return refnum;
}

WINDOWCMD(show)
{
	int	tmp_;

	if (!args)
		return refnum;

	if ((tmp_ = get_window2("SHOW", args)) > 1)
	{
		show_window(tmp_);
		return tmp_;
	}
	return refnum;
}

WINDOWCMD(show_all)
{
	int	governor = 0;
	int	w_;

	if (!args)
		return refnum;

	for (w_ = 0; traverse_all_windows_on_screen2(&w_, -1); w_ = 0)
	{
		show_window(w_);
		/* If for some reason we get stuck in a loop, bail. */
		if (governor++ > 100)
			return refnum;
	}

	return refnum;
}

WINDOWCMD(shrink)
{
	int	value;

	if (!window_is_valid(refnum))
		return 0;
	if (!args)
		return refnum;

	if (get_number("SHRINK", args, &value))
		resize_window(RESIZE_REL, refnum, -value);

	return refnum;
}

WINDOWCMD(size)
{
	char *		ptr = *args;
	int		number;

	if (!window_is_valid(refnum))
		return 0;
	if (!args)
		return refnum;

	number = parse_number(args);
	if (ptr == *args) 
		say("Window size is %d", get_window_display_lines(refnum));
	else
		resize_window(RESIZE_ABS, refnum, number);

	return refnum;
}

/*
 * This lists the windows that are on the stack, cleaning up any
 * bogus entries on the way.
 */
WINDOWCMD(stack)
{
	WindowStack *	tmp;
	size_t		len = 4;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window || get_window_screennum(window->refnum) < 0)
		return 0;
	if (!args)
		return refnum;

	for (tmp = get_screen_window_stack(get_window_screennum(window->refnum)); tmp; tmp = tmp->next)
	{
		const char *n = get_window_name(tmp->refnum);
		if (n && strlen(n) > len)
			len = strlen(n);
	}

	say("Window stack:");
	for (tmp = get_screen_window_stack(get_window_screennum(window->refnum)); tmp; tmp = tmp->next)
		list_a_window(tmp->refnum, len);

	return refnum;
}

WINDOWCMD(status_format)
{
	char *		arg;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	arg = new_next_arg(*args, args);
	malloc_strcpy(&window->status.line[0].raw, arg);
	compile_status(refnum, &window->status);
	window_statusbar_needs_redraw(refnum);

	return refnum;
}

WINDOWCMD(status_format1)
{
	char *		arg;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	arg = new_next_arg(*args, args);
	malloc_strcpy(&window->status.line[1].raw, arg);
	compile_status(refnum, &window->status);
	window_statusbar_needs_redraw(refnum);

	return refnum;
}

WINDOWCMD(status_format2)
{
	char *		arg;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	arg = new_next_arg(*args, args);
	malloc_strcpy(&window->status.line[2].raw, arg);
	compile_status(refnum, &window->status);
	window_statusbar_needs_redraw(refnum);

	return refnum;
}

WINDOWCMD(status_prefix_when_current)
{
	char *		arg;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	arg = new_next_arg(*args, args);
	malloc_strcpy(&window->status.prefix_when_current, arg);
	window_statusbar_needs_redraw(window->refnum);

	return refnum;
}

WINDOWCMD(status_prefix_when_not_current)
{
	char *		arg;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	arg = new_next_arg(*args, args);
	malloc_strcpy(&window->status.prefix_when_not_current, arg);
	window_statusbar_needs_redraw(window->refnum);

	return refnum;
}


WINDOWCMD(status_special)
{
	char *		arg;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	arg = new_next_arg(*args, args);
	malloc_strcpy(&window->status.special, arg);
	window_statusbar_needs_redraw(window->refnum);

	return refnum;
}

WINDOWCMD(swap)
{
	int	tmp;

	if (!args)
		return refnum;

	if ((tmp = get_invisible_window("SWAP", args)) >= 1)
		swap_window(refnum, tmp);

	return current_window_;
}

WINDOWCMD(swappable)
{
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	if (!get_boolean("SWAPPABLE", args, &window->swappable))
		return 0;

	window_statusbar_needs_update(window->refnum);
	return refnum;
}

/*
 * /WINDOW TOPLINE "...."
 * This "saves" the top line of the window from being scrollable.  It sets 
 * the data in this line to whatever the argument is, or if the argument is
 * just a hyphen, it "unsaves" the top line.
 */
WINDOWCMD(topline)
{
	int		line;
	const char *	linestr;
	const char *	topline;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	if (!(linestr = new_next_arg(*args, args)))
		return refnum;
	if (!is_number(linestr))
	{
		say("Usage: /WINDOW TOPLINE <number> \"<string>\"");
		return refnum;
	}

	line = my_atol(linestr);
	if (line <= 0 || line >= 10)
	{
		say("/WINDOW TOPLINE number must be 1 to 9.");
		return refnum;
	}

	if (!(topline = new_next_arg(*args, args)))
		malloc_strcpy(&window->topline[line - 1], empty_string);
	else if (!strcmp(topline, "-"))
		new_free(&window->topline[line - 1]);
	else
		malloc_strcpy(&window->topline[line - 1], topline);

	window_body_needs_redraw(window->refnum);
	return refnum;
}

WINDOWCMD(toplines)
{
	Window *	window = get_window_by_refnum_direct(refnum);
	char *		ptr = *args;
	int		number;
	int		saved = window->toplines_wanted;

	if (!window)
		return 0;
	if (!args)
		return refnum;

	number = parse_number(args);
	if (ptr == *args) 
	{
		say("Window saved lines is %d", window->toplines_wanted);
		return refnum;
	}
	if (number < 0 || number >= 10)
	{
		say("Window saved lines must be < 10 for now.");
		return refnum;
	}

	window->toplines_wanted = number;
	window->display_lines += saved - window->toplines_wanted;
	window->top += window->toplines_wanted - saved;
	window->toplines_showing = window->toplines_wanted;  /* XXX */

	if (window->display_lines < 0)
	{
		window->display_lines = 0;
		if (get_window_screennum(window->refnum) >= 0)
			recalculate_windows(get_window_screennum(window->refnum));
	}
	if (window->top < window->toplines_showing)
	{
		window->top = window->toplines_showing;
		if (get_window_screennum(window->refnum) >= 0)
			recalculate_windows(get_window_screennum(window->refnum));
	}
	recalculate_window_positions(get_window_screennum(window->refnum));
	return refnum;
}

/* WINDOW UNCLEAR -- pull down the scrollback to fill the window */
WINDOWCMD(unclear)
{
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	unclear_window(refnum);
	return refnum;
}

/* WINDOW CLEARLEVEL - Remove all lastlog items of certain level(s) */
WINDOWCMD(clearlevel)
{
	Mask		mask;
	char *		rejects = NULL;
	char *		arg;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	arg = next_arg(*args, args);;
	if (str_to_mask(&mask, arg, &rejects))
		standard_level_warning("/WINDOW CLEARLEVEL", &rejects);
	clear_level_from_lastlog(window->refnum, &mask);
	return refnum;
}

/* WINDOW CLEARREGEX - Remove all lastlog items matching a regex */
WINDOWCMD(clearregex)
{
	char *		arg;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!args)
		return refnum;

	arg = new_next_arg(*args, args);;
	clear_regex_from_lastlog(window->refnum, arg);
	return refnum;
}


typedef int (*window_func) (int refnum, char **args);

typedef struct window_ops_T {
	const char 	*command;
	window_func 	func;
} window_ops;

static const window_ops options [] = {
	{ "ADD",		windowcmd_add 			},
	{ "BACK",		windowcmd_back 			},
	{ "BALANCE",		windowcmd_balance 		},
	{ "BEEP_ALWAYS",	windowcmd_beep_always 		},
	{ "CHANNEL",		windowcmd_channel 		},
	{ "CHECK",		windowcmd_check			},
	{ "CLAIM",		windowcmd_claim			},
	{ "CLEAR",		windowcmd_clear			},
	{ "CLEARLEVEL",		windowcmd_clearlevel		},
	{ "CLEARREGEX",		windowcmd_clearregex		},
	{ "CREATE",		windowcmd_create 		},
	{ "DELETE",		windowcmd_delete 		},
	{ "DELETE_KILL",	windowcmd_delete_kill		},
	{ "DESCRIBE",		windowcmd_describe		}, /* * */
	{ "DISCON",		windowcmd_discon		},
	{ "DOUBLE",		windowcmd_double 		},
	{ "ECHO",		windowcmd_echo			},
	{ "FIXED",		windowcmd_fixed			},
	{ "FLUSH",		windowcmd_flush			},
	{ "FLUSH_SCROLLBACK",	windowcmd_flush_scrollback	},
	{ "GOTO",		windowcmd_goto 			},
	{ "GROW",		windowcmd_grow 			},
	{ "HIDE",		windowcmd_hide 			},
	{ "HIDE_OTHERS",	windowcmd_hide_others 		},
	{ "HOLD_INTERVAL",	windowcmd_hold_interval		},
	{ "HOLD_MODE",		windowcmd_hold_mode 		},
	{ "HOLD_SLIDER",	windowcmd_hold_slider		},
	{ "INDENT",		windowcmd_indent 		},
	{ "KILL",		windowcmd_kill 			},
	{ "KILL_ALL_HIDDEN",	windowcmd_kill_all_hidden	},
	{ "KILL_OTHERS",	windowcmd_kill_others 		},
	{ "KILLABLE",		windowcmd_killable		},
	{ "KILLSWAP",		windowcmd_killswap 		},
	{ "LAST", 		windowcmd_last 			},
	{ "LASTLOG",		windowcmd_lastlog 		},
	{ "LASTLOG_LEVEL",	windowcmd_lastlog_mask 		},
	{ "LEVEL",		windowcmd_level			},
	{ "LIST",		windowcmd_list 			},
	{ "LOG",		windowcmd_log 			},
	{ "LOGFILE",		windowcmd_logfile 		},
	{ "LOG_MANGLE",		windowcmd_log_mangle		},
	{ "LOG_REWRITE",	windowcmd_log_rewrite		},
	{ "MERGE",		windowcmd_merge			},
	{ "MOVE",		windowcmd_move 			},
	{ "MOVE_TO",		windowcmd_move_to		},
	{ "NAME",		windowcmd_name 			},
	{ "NEW",		windowcmd_new 			},
	{ "NEW_HIDE",		windowcmd_new_hide		}, /* * */
	{ "NEXT",		windowcmd_next 			},
	{ "NOSERV",		windowcmd_discon		},
	{ "NOTIFIED",		windowcmd_notified 		},
	{ "NOTIFY",		windowcmd_notify 		},
	{ "NOTIFY_LEVEL",	windowcmd_notify_mask 		},
	{ "NOTIFY_NAME",	windowcmd_notify_name 		},
	{ "NUMBER",		windowcmd_number 		},
	{ "POP",		windowcmd_pop 			},
	{ "PREVIOUS",		windowcmd_previous	 	},
	{ "PUSH",		windowcmd_push 			},
	{ "QUERY",		windowcmd_query			},
	{ "REBUILD_SCROLLBACK",	windowcmd_rebuild_scrollback 	},
	{ "REFNUM",		windowcmd_refnum 		},
	{ "REFNUM_OR_SWAP",	windowcmd_refnum_or_swap	},
	{ "REFRESH",		windowcmd_refresh		},
	{ "REJOIN",		windowcmd_rejoin		},
	{ "REMOVE",		windowcmd_remove 		},
	{ "SCRATCH",		windowcmd_scratch		},
	{ "SCREEN_DEBUG",	windowcmd_screen_debug		},
	{ "SCROLL",		windowcmd_scroll		},
	{ "SCROLLADJ",		windowcmd_scrolladj		},
	{ "SCROLLBACK",		windowcmd_scrollback		}, /* * */
	{ "SCROLL_BACKWARD",	windowcmd_scroll_backward	},
	{ "SCROLL_END",		windowcmd_scroll_end		},
	{ "SCROLL_FORWARD",	windowcmd_scroll_forward	},
	{ "SCROLL_LINES",	windowcmd_scroll_lines		},
	{ "SCROLL_START",	windowcmd_scroll_start		},
	{ "SEARCH_BACK",	windowcmd_search_back		},
	{ "SEARCH_FORWARD",	windowcmd_search_forward	},
	{ "SERVER",		windowcmd_server 		},
	{ "SHOW",		windowcmd_show 			},
	{ "SHOW_ALL",		windowcmd_show_all		}, /* * */
	{ "SHRINK",		windowcmd_shrink 		},
	{ "SIZE",		windowcmd_size 			},
	{ "SKIP",		windowcmd_skip			},
	{ "STACK",		windowcmd_stack 		},
	{ "STATUS_FORMAT",	windowcmd_status_format		},
	{ "STATUS_FORMAT1",	windowcmd_status_format1	},
	{ "STATUS_FORMAT2",	windowcmd_status_format2	},
	{ "STATUS_PREFIX_WHEN_CURRENT",		windowcmd_status_prefix_when_current	},
	{ "STATUS_PREFIX_WHEN_NOT_CURRENT",	windowcmd_status_prefix_when_not_current	},
	{ "STATUS_SPECIAL",	windowcmd_status_special	},
	{ "SWAP",		windowcmd_swap 			},
	{ "SWAPPABLE",		windowcmd_swappable		},
	{ "TOPLINE",		windowcmd_topline		},
	{ "TOPLINES",		windowcmd_toplines		},
	{ "UNCLEAR",		windowcmd_unclear		},
	{ NULL,			NULL 				}
};

BUILT_IN_COMMAND(windowcmd)
{
	char *	arg;
	int 	nargs = 0;
	int	refnum;
	int	old_status_update, 
		old_from_server;
	int	old_current_window;
	int	l;
	char *	original_args = NULL;

	old_from_server = from_server;
	old_current_window = current_window_;
 	old_status_update = permit_status_update(0); 
	refnum = current_window_;

	original_args = LOCAL_COPY(args);

	while ((arg = next_arg(args, &args)))
	{
		int	i;
		int	len = strlen(arg);
		int	pass_null;

		if (*arg == '-')
			arg++, len--, pass_null = 1;
		else
			pass_null = 0;

		l = message_setall(refnum > 0 ? refnum : -1, 
					get_who_from(), get_who_level());

		for (i = 0; options[i].func ; i++)
		{
			if (!my_strnicmp(arg, options[i].command, len))
			{
				int	window;

				if (refnum > 0)
					from_server = get_window_server(refnum);
				window = refnum > 0 ? get_window_user_refnum(refnum) : -1;
				if (pass_null)
					refnum = options[i].func(refnum, NULL);
				else
					refnum = options[i].func(refnum, &args); 
				nargs++;

				if (refnum < 1)
					args = NULL;
				do_hook(WINDOW_COMMAND_LIST, "%d %d %s", 
					window, 
					refnum > 0 ? (int)get_window_user_refnum(refnum) : -1,
					arg);
				break;
			}
		}

		if (!options[i].func)
		{
			int	s_window;
			nargs++;

			if ((s_window = lookup_window(arg)) > 0)
				refnum = s_window;
			else
			{
				yell("WINDOW: Invalid window or option: [%s] (from [%s])", arg, original_args);
				/* XXX Maybe this should fail? */
				args = NULL;
			}
		}

		pop_message_from(l);
	}

	if (!nargs)
		windowcmd_describe(0, NULL);

	/*
	 * "from_server" changes only if "current_window" changes.  So if
	 * the latter has not changed, then reset from_server to its orig
	 * value.  Otherwise, set it to the new current window's server!
	 */
	if (current_window_ >= 1 && current_window_ != old_current_window)
		from_server = get_window_server(current_window_);
	else
		from_server = old_from_server;

	permit_status_update(old_status_update); 
	/* pop_message_from(l); */
	window_check_channels();
	update_all_windows();
}

/************************** INDICATOR MAINTAINANCE **************************/
/*
 * update_scrollback_indicator puts the scrollback indicator after the last 
 * line that is visible on the screen right now.  
 *  1) Figure out which view is being used
 *  2) Figure out what the final line is visible is,
 *  3) Put the indicator after it (but before display_ip if needed)
 *  4) Reset the value of the indicator from /set scrollback_indicator
 * You should not call this function unless indicator_needs_update() returns 1.
 */
static void	update_scrollback_indicator (int window_)
{
}

/*
 * remove_scrollback_indicator removes the indicator from the scrollback, 
 * making it available for placement somewhere else.  This does the whole
 * shebang: finding it, removing it, and cleansing it.
 */
static void	remove_scrollback_indicator (int window_)
{
}

/*
 * window_indicator_is_visible returns 1 if the window's indicator is in
 * the scrollback and 0 if it is not in use.  It's important to call
 * cleanse_indicator() when the indicator is removed from scrollback or 
 * this function will break.
 */
static void	window_indicator_is_visible (int window_)
{
}

/*
 * cleanse_indicator clears the values in the indicator and makes it available
 * for reuse.  If you haven't unlinked it from the scrollback it does that 
 * for you.  It's important to cleanse the indicator because it does things
 * that are used by window_indicator_is_visible().
 */
static void	cleanse_indicator (int window_)
{
}

/*
 * indicator_needs_update tells you when you do a scrollback whether the 
 * indicator needs to be moved further down the scrollback or not.  If the
 * indicator is not being used or if it is above your current view, then it
 * does need to be moved down.  Otherwise it does not need to be moved.
 */ 
static void	indicator_needs_update (int window_)
{
}

/*
 * go_back_to_indicator will return the scrollback view to where the
 * indicator is and then do a full recalculation.
 */
static void	go_back_to_indicator (int window_)
{
}

/********************** SCROLLBACK BUFFER MAINTAINANCE **********************/
/* 
 * XXXX Dont you DARE touch this XXXX 
 *
 * Most of the time, a delete_display_line() is followed somewhat
 * immediately by a new_display_line().  So most of the time we just
 * cache that one item and re-use it.  That saves us thousands of
 * malloc()s.  In the cases where its not, then we just do things the
 * normal way.
 */
static Display *recycle = NULL;

static void 	delete_display_line (Display *stuff)
{
	if (recycle == stuff)
		panic(1, "delete_display_line: Deleting the same line twice is invalid.");

	if (recycle)
	{
		new_free((char **)&recycle->line);
		new_free((char **)&recycle);
	}
	recycle = stuff;

	/* 
	 * Don't de-allocate the string; our consumer will call
	 * malloc_strcpy() and they will appreciate being able to
	 * cheaply re-use this string.
	 */
	*(stuff->line) = 0;
}

/*
 * CAUTION: The return value of this function may or may not have 'line'
 * set to NULL.  Therefore, you MUST call malloc_strcpy() to initialize 
 * the 'line' field.  You will cause memory leaks if you fail to do this!
 * By not free()ing line, we save a free()/malloc() if the new original line
 * had enough space to hold the new line.  We let malloc_strcpy() do the
 * dirty work there.
 */
static Display *new_display_line (Display *prev, int window_)
{
	Display *stuff;

	if (recycle)
	{
		stuff = recycle;
		recycle = NULL;
	}
	else
	{
		stuff = (Display *)new_malloc(sizeof(Display));
		stuff->line = NULL;
	}

	/*
	 * Note that we don't destroy 'line' because it's either been 
	 * set to NULL for a new line (above) or it's been zeroed out by
	 * delete_display_line() and can be malloc_strcpy()d over thus
	 * saving a free/new cycle.
	 */

	stuff->count = get_window_display_counter_incr(window_);
	stuff->unique_refnum = ++current_display_counter;
	stuff->prev = prev;
	stuff->next = NULL;
	stuff->when = time(NULL);
	stuff->linked_refnum = -1;
	return stuff;
}

/*
 * This function adds an item to the window's scrollback.  If the item
 * should be displayed on the screen, then 1 is returned.  If the item is
 * not to be displayed, then 0 is returned.  This function handles all
 * the hold_mode stuff.
 */
int 	add_to_scrollback (int window_, const char *str, intmax_t refnum)
{
	/*
	 * If this is a scratch window, do that somewhere else
	 */
	if (get_window_change_line(window_) != -1)
	{
		debuglog("add_to_scrollback: change_line: window %d line %d str %s", 
				window_, get_window_change_line(window_), str);
		return change_line(window_, str);
	}

	return add_to_display(window_, (const char *)str, refnum);
}

/*
 * add_to_display -- add a line to the scrollback buffer, and adjust all
 * three views accordingly.
 *
 * This is a sub-function of add_to_scrollback that was created when epic
 * grew scratch windows.  It is used to add a new line of output to the
 * bottom of the scrollback buffer.  'str' is a "normalized" string that 
 * should have been returned by prepare_display() and in any circumstance 
 * must not be wider than the window.
 * 
 * The return value of this function has historically been used to determine
 * whether the output is "display-worthy".  With the advent of the three-view
 * system, this functionality has been superceded by ok_to_output() and this 
 * function should always return 1.
 *
 * This function may be called many times between calls to trim_scrollback().
 */
static int	add_to_display (int window_, const char *str, intmax_t refnum)
{
	int	scroll;
	int	i;
	Window *window;

	if (!window_is_valid(window_))
	{
		debuglog("add_to_display: window_ %d is invalid", window_);
		return 0;
	}

	window = get_window_by_refnum_direct(window_);

	/* 
	 * Add to the bottom of the scrollback buffer, and move the 
	 * bottom of scrollback (display_ip) after it. 
	 *
	 * The "display_ip" is always BLANK -- so first we create a new
	 * blank display line (display_ip->next) then we fill in the previous
	 * blank display line (display_ip)
	 */
	window->display_ip->next = new_display_line(window->display_ip, window_);
	malloc_strcpy(&window->display_ip->line, str);
	window->display_ip->linked_refnum = refnum;
	window->display_ip = window->display_ip->next;
	window->display_buffer_size++;
	debuglog("add_to_display: Window %d, lastlog refnum %lu, display refnum %lu, str: %s", 
			window_, (unsigned long)refnum, (unsigned long)window->display_ip->unique_refnum, str);
	debuglog("add_to_display: saniy check - this should be the same: (%lu) %s",
			(unsigned long)window->display_ip->prev->linked_refnum, window->display_ip->prev->line);

	/*
	 * Mark that the scrollable view, the scrollback view, and the hold
	 * view have grown by one line.
	 */
	window->scrolling_distance_from_display_ip++;
	if (window->scrollback_top_of_display)
		window->scrollback_distance_from_display_ip++;
	if (window->holding_top_of_display)
		window->holding_distance_from_display_ip++;

	/* 
 	 * Handle overflow in the held view -- If the held view has
	 * overflowed update the status bar in case %B changes.
	 */
	if (window->holding_top_of_display)
	{
	    if (window->holding_distance_from_display_ip > 
					window->display_lines)
		window_statusbar_needs_update(window->refnum);
	}

	/*
	 * Handle overflow in the scrollback view -- If the scrollback view
	 * has overflowed, update the status bar in case %K changes.
	 */
	if (window->scrollback_top_of_display)
	{
	    if (window->scrollback_distance_from_display_ip > 
					window->display_lines)
		window_statusbar_needs_update(window->refnum);
	}

	/*
	 * XXX I don't like this very much, but this is a placeholder
	 * for a superior solution someday.
	 *
	 * The problem is how to handle output in zero-height windows.
	 * I think maybe we should pretend they're 1 line high.
	 * But for our purposes here, output to a zero-height window
	 * does not push forward the top of display.  This would affect
	 * the user later if they make the window bigger.
	 *
	 * The return value 0 tells the caller "don't do any output"
	 */
	if (window->display_lines == 0)
	{
		debuglog("window_ %d's display_lines was 0 -- punting", window_);
		return 0;		/* XXX Do soemthing better, someday */
	}

	/*
	 * Handle overflow in the scrollable view -- If the scrollable view
	 * has overflowed, logically scroll down the scrollable view by 
	 * /SET SCROLL_LINES lines.
	 */
	while (window->scrolling_distance_from_display_ip > 
				window->display_lines)
	{
		if ((scroll = window->scroll_lines) <= 0)
		    if ((scroll = get_int_var(SCROLL_LINES_VAR)) <= 0)
			scroll = 1;
		if (scroll > window->display_lines)
			scroll = window->display_lines;

		for (i = 0; i < scroll; i++)
		{
			if (window->scrolling_top_of_display == NULL)
				panic(1, "add_to_display, Window %d tried to scroll %d lines but it is only %d lines tall", window->user_refnum, scroll, i);

			window->scrolling_top_of_display = 
				window->scrolling_top_of_display->next;
			window->scrolling_distance_from_display_ip--;
		}
	}

	return 1;
}

/*
 * trim_scrollback - Remove excess entries from window's scrollback buffer
 * 
 * When the window is using the scrollable view (scrollback is not on, and
 * hold mode is not holding anything), whenever any new line is added to the
 * window's scrollback, we must check to see if the scrollback is larger than
 * the user requested size; and garbage collect any excess lines.
 * 
 * We never trim the scrollback when the user is holding or scrolling back,
 * because the visible part of the window is drawn directly out of the scroll-
 * back buffer, and deleting what is visible would crash epic.  This is also
 * what allows the hold mode user to hold forever without risk of losing any-
 * thing.
 *
 * This is always called after add_to_scrollback().
 */
int	trim_scrollback (int window_)
{
	Window *	window = get_window_by_refnum_direct(window_);

	/* Do not trim the scrollback if we are in scrollback mode */
	if (window->scrollback_top_of_display)
		return 0;

	/* Do not trim the scrollback if we are holding stuff. */
	if (window->holding_distance_from_display_ip > window->display_lines)
		return 0;

	/*
	 * Before outputting the line, we snip back the top of the
	 * display buffer.  (The fact that we do this only when we get
	 * to here is what keeps whats currently on the window always
	 * active -- once we get out of hold mode or scrollback mode, then
	 * we truncate the display buffer at that point.)
	 */
	while (window->display_buffer_size > window->display_buffer_max)
	{
		Display *next = window->top_of_scrollback->next;

		/*
		 * XXX Pure, unmitigated paranoia -- if the only thing in
		 * the scrollback buffer is the display_ip, then the buffer
		 * is actually completely empty.  WE MUST NEVER DELETE THE
		 * DISPLAY_IP EVER EVER EVER.  So we just bail here.
		 */
		if (window->top_of_scrollback == window->display_ip)
			break;

		delete_display_line(window->top_of_scrollback);
		window->top_of_scrollback = next;
		window->display_buffer_size--;
	}

	/* Ok.  Go ahead and print it */
	return 1;
}

/*
 * flush_scrollback -- Flush a window's scrollback.  This forces a /clear.
 * XXX This is cut and pasted from new_window() and clear_window().  That
 * is a horrible abuse. this needs to be refactored someday.
 */
static int	flush_scrollback (int window_, int abandon)
{
	Display *holder, *curr_line;
	Window *w;

	if (!window_is_valid(window_))
		return 0;
	w = get_window_by_refnum_direct(window_);

	/* Save the old scrollback buffer */
	holder = w->top_of_scrollback;

	/* Reset all of the scrollback values */
        w->top_of_scrollback = NULL;        /* Filled in later */
        w->display_ip = NULL;               /* Filled in later */
        w->display_buffer_size = 0;
        w->scrolling_top_of_display = NULL;         /* Filled in later */
        w->scrolling_distance_from_display_ip = -1; /* Filled in later */
        w->holding_top_of_display = NULL;           /* Filled in later */
        w->holding_distance_from_display_ip = -1;   /* Filled in later */
        w->scrollback_top_of_display = NULL;        /* Filled in later */
        w->scrollback_distance_from_display_ip = -1; /* Filled in later */
	w->clear_point = NULL;			    /* Filled in later? */
        w->display_counter = 1;

	/* Reconstitute a new scrollback buffer */
        w->top_of_scrollback = new_display_line(NULL, w->refnum);
        w->top_of_scrollback->line = NULL;
        w->top_of_scrollback->next = NULL;
	w->top_of_scrollback->prev = NULL;
        w->display_buffer_size = 1;
        w->display_ip = w->top_of_scrollback;
        w->scrolling_top_of_display = w->top_of_scrollback;

	/* Delete the old scrollback */
	/* XXXX - this should use delete_display_line! */ 
	while ((curr_line = holder))
	{
		holder = curr_line->next;
		if (abandon)
			dont_need_lastlog_item(w->refnum, curr_line->linked_refnum);
		new_free(&curr_line->line);
		new_free((char **)&curr_line);
	}

	/* Recalculate and redraw the window. */
	recalculate_window_cursor_and_display_ip(w->refnum);
	window_body_needs_redraw(w->refnum);
	window_statusbar_needs_update(w->refnum);
	return 1;
}

/*
 * flush_scrollback_after - Delete everything in the "never seen" segment
 * of the scrollback (below the current hold view).  
 * 		This is the /WINDOW FLUSH command.
 *
 * If a user holds for a very long time, they could have many hundreds or
 * thousands of lines that they have never seen.  Perhaps the user chooses
 * not to see those lines at all, and uses /WINDOW FLUSH to throw them away.
 * We figure out what things the user has never seen (below the bottom of
 * the window in the current hold view), and throw them away.  This is done
 * by resetting the end of the scrollback (display_ip) to the line below what
 * is at the bottom of the window, and GCing everything below that.
 *
 * The scrollable view is unconditionally reset to the hold view by this 
 * operation to avoid a crash.  The hold counter is not, so no new lines
 * will be seen until you hit <enter>.  
 * 
 * Note: If you run /WINDOW FLUSH from the input line, then you have hit
 * <enter> and epic will unhold another window of output.  If you run the 
 * /WINDOW FLUSH from within a keybinding, then <enter> will NOT be forth-
 * coming and epic will keep holding new output.  This is not really a bug,
 * it's just the way things work.
 */
static int	flush_scrollback_after (int window_, int abandon)
{
	Display *curr_line, *next_line;
	int	count;
	Window *window;

	if (!window_is_valid(window_))
		return 0;
	window = get_window_by_refnum_direct(window_);

	/* Determine what is currently visible in the hold view */
	if (!(curr_line = window->holding_top_of_display))
	{
		say("/WINDOW FLUSH doesn't do anything unless you're in hold mode");
		return 0;
	}

	/*
	 * Move "curr_line" to the first line below the bottom of the window.
	 * We do this by moving forward one line at a time for the size of 
	 * the window.  If we bump into the display_ip, then that means there
	 * is nothing to flush (since the nothing "below" the bottom of the 
	 * window.)
	 */
	for (count = 1; count < window->display_lines; count++)
	{
		if (curr_line == window->display_ip)
			return 0;
		curr_line = curr_line->next;
	}

	/* 
	 * Reset the bottom of the scrollback (display_ip) to just below
	 * the bottom of the hold view window.
	 */
	next_line = curr_line->next;
	curr_line->next = window->display_ip;
	window->display_ip->prev = curr_line;

	/*
	 * Now GC all of the lines below the hold view until we get to the
	 * end of the scrollback (display_ip).
	 */
	curr_line = next_line;
	while (curr_line != window->display_ip)
	{
		next_line = curr_line->next;
		if (abandon)
			dont_need_lastlog_item(window->refnum, curr_line->linked_refnum);
		delete_display_line(curr_line);
		window->display_buffer_size--;
		curr_line = next_line;
	}

	/* And reset the scrollable view so it points to the hold view. */
	window->scrolling_top_of_display = window->holding_top_of_display;

	/* 
	 * Since we moved the end of scrollback, we have to recalculate the
	 * distances to the end of scrollback for everything.
	 */
	recalculate_window_cursor_and_display_ip(window->refnum);
	return 1;
}



/********************** Scrollback functionality ***************************/
/*
 * window_scrollback_backwards: Generalized scrollback (up/older)
 * Resets the scrollback view to some place older than the top of the
 * current view (the last scrollback point, the last hold point, or the
 * standard scrolling view).
 *
 * window - The window that we are scrolling back on
 * skip_lines - Automatically scroll up this many lines, without testing them
 * abort_if_not_found - If we reach the top of the scrollback without finding
 *			the line we're looking for, treat as an error and do
 *			not change anything
 * test - A callback function that will tell us if this is the line we are
 *	  interested in or not.  Returns 0 for "keep going" and -1 for "stop"
 * meta - A private value to pass to the tester.
 */
static void	window_scrollback_backwards (int window_, int skip_lines, int abort_if_not_found, int (*test)(int, Display *, void *), void *meta)
{
	Display *new_top;
	Window *window;

	if (!window_is_valid(window_))
		return;
	window = get_window_by_refnum_direct(window_);

	if (window->scrollback_top_of_display == window->top_of_scrollback)
	{
		term_beep();
		return;
	}

	if (window->scrollback_top_of_display)
		new_top = window->scrollback_top_of_display;
	else if (window->holding_distance_from_display_ip > 
				window->scrolling_distance_from_display_ip)
		new_top = window->holding_top_of_display;
	else
		new_top = window->scrolling_top_of_display;

	for (;;)
	{
		/* Always stop when we reach the top */
		if (new_top == window->top_of_scrollback)
		{
			if (abort_if_not_found)
			{
				term_beep();
				return;
			}
			break;
		}

		if (skip_lines > 0)
			skip_lines--;
		/* This function returns -1 when it wants us to stop. */
		else if ((*test)(window->refnum, new_top, meta))
			break;

		new_top = new_top->prev;
	}

	window->scrollback_top_of_display = new_top;
	recalculate_window_cursor_and_display_ip(window->refnum);
	window_body_needs_redraw(window->refnum);
	window_statusbar_needs_update(window->refnum);
}

/*
 * window_scrollback_forwards: Generalized scrollforward (down/newer)
 * Resets the scrollback view to some place newer than the top of the
 * current view (the last scrollback point, the last hold point, or the
 * standard scrolling view).  Note that no matter what happens, if the 
 * scrollback point ends up being "newer" (further down) than the normal
 * scrolling view, it will be cancelled by recalculate_window_cursor().
 *
 * window - The window that we are scrolling forward on
 * skip_lines - Automatically scroll down this many lines, w/o testing them
 * abort_if_not_found - If we reach the bottom of scrollback without finding
 *			the line we're looking for, treat as an error and do
 *			not change anything
 * test - A callback function that will tell us if this is the line we are
 *	  interested in or not.  Returns 0 for "keep going" and -1 for "stop"
 * meta - A private value to pass to the tester.
 */
static void	window_scrollback_forwards (int window_, int skip_lines, int abort_if_not_found, int (*test)(int, Display *, void *), void *meta)
{
	Display *new_top;
	int	unholding;
	Window	*window;

	if (!window_is_valid(window_))
		return;
	window = get_window_by_refnum_direct(window_);

	if (window->scrollback_top_of_display)
	{
		new_top = window->scrollback_top_of_display;
		unholding = 0;
	}
	else if (window->holding_distance_from_display_ip >
			window->scrolling_distance_from_display_ip)
	{
		new_top = window->holding_top_of_display;
		unholding = 1;
	}
	else
	{
		term_beep();
		return;
	}

	for (;;)
	{
		/* Always stop when we reach the bottom */
		if (new_top == window->display_ip)
		{
			if (abort_if_not_found)
			{
				term_beep();
				return;
			}
			break;
		}

		if (skip_lines > 0)
			skip_lines--;
		/* This function returns -1 when it wants us to stop. */
		else if ((*test)(window->refnum, new_top, meta))
			break;

		new_top = new_top->next;
	}

	/* Set the top of scrollback to wherever we landed */
	if (!unholding)
		window->scrollback_top_of_display = new_top;
	else
		window->holding_top_of_display = new_top;

	recalculate_window_cursor_and_display_ip(window->refnum);
	window_body_needs_redraw(window->refnum);
	window_statusbar_needs_update(window->refnum);
	return;
}

/* * * */
/*
 * A scrollback tester that counts off the number of lines to move.
 * Returns -1 when the count has been reached.
 */
static	int	window_scroll_lines_tester (int window_, Display *line, void *meta)
{
	if (*(int *)meta > 0)
	{
		(*(int *)meta)--;
		return 0;		/* keep going */
	}
	else
		return -1;		/* We're done. stop! */
}

/* Scroll up "my_lines" on "window".  Will stop if it reaches top */
static void 	window_scrollback_backwards_lines (int window_, int my_lines)
{
	/* Do not skip line, Move even if not found, don't leave blank space */
	window_scrollback_backwards(window_, 0, 0,
			window_scroll_lines_tester, 
			(void *)&my_lines);
}

/* Scroll down "my_lines" on "window".  Will stop if it reaches bottom */
static void 	window_scrollback_forwards_lines (int window_, int my_lines)
{
	/* Do not skip line, Move even if not found, don't leave blank space */
	window_scrollback_forwards(window_, 0, 0,
			window_scroll_lines_tester, 
			(void *)&my_lines);
}

/* * * */
/*
 * A scrollback tester that looks for a line that matches a regex.
 * Returns -1 when the line is found, and 0 if this line does not match.
 */
static	int	window_scroll_regex_tester (int window_, Display *line, void *meta)
{
	char *	denormal;

	denormal = normalized_string_to_plain_text(line->line);

	debuglog("window_scroll_regex_tester: window %d, display (ur %lld, cnt %lld, lr %lld, when %lld, txt %s",
			get_window_user_refnum(window_), 
			(long long)line->unique_refnum, 
			(long long)line->count, 
			(long long)line->linked_refnum,
			(long long)line->when, denormal);

	/* If it matches, stop here */
	if (regexec((regex_t *)meta, denormal, 0, NULL, 0) == 0)
	{
		new_free(&denormal);
		debuglog("regexec succeeded, found what i am looking for");
		return -1;	/* Stop right here. */
	}
	else
	{
		new_free(&denormal);
		debuglog("regexec failed, will keep looking");
		return 0;	/* Just keep going. */
	}
}

static void 	window_scrollback_to_string (int window_, regex_t *preg)
{
	/* Skip one line, Don't move if not found, don't leave blank space */
	window_scrollback_backwards(window_, 1, 1,
			window_scroll_regex_tester, 
			(void *)preg);
}

static void 	window_scrollforward_to_string (int window_, regex_t *preg)
{
	/* Skip one line, Don't move if not found, blank space is ok */
	window_scrollback_forwards(window_, 1, 1,
			window_scroll_regex_tester, 
			(void *)preg);
}

/* * * */
/*
 * A scrollback tester that looks for the final line that is newer than the
 * given time.
 */
static	int	window_scroll_time_tester (int window_, Display *line, void *meta)
{
	/* If this is the oldest line, then just stop here */
	if (line->prev == NULL)
		return -1;		/* Stop right here */

	/* 
	 * If this line is newer than 'meta' but the previous line is
	 * older than 'meta' then we stop here.
	 */
	if (line->when >= *(time_t *)meta && 
	    line->prev->when < *(time_t *)meta)
		return -1;		/* Stop right here */

	return 0;	/* Keep going */
}

/* * * */
/*
 * Functions that implement keybindings (and corresponding /WINDOW ops)
 */

/*
 * Keybinding: SCROLL_START
 * Command: /WINDOW SCROLL_START
 */
static void	window_scrollback_start (int window_)
{
	/* XXX Ok.  So maybe 999999 *is* a magic number. */
	window_scrollback_backwards_lines(window_, 999999);
}

/*
 * Keybinding: SCROLL_END
 * Command: /WINDOW SCROLL_END
 * Please note that this doesn't turn hold_mode off, obviously! 
 */
static void	window_scrollback_end (int window_)
{
	window_scrollback_forwards_lines(window_, 999999);
}

/*
 * Keybinding: SCROLL_FORWARD
 * Command: /WINDOW SCROLL_FORWARD
 * Scrolls down the "default" amount (usually half a screenful) 
 */
static void	window_scrollback_forward (int window_)
{
	int 	ratio = get_int_var(SCROLLBACK_RATIO_VAR);
	int	my_lines;

	if (!window_is_valid(window_))
		return;

	if (ratio < 1) 
		ratio = 1;
	if (ratio > 100) 
		ratio = 100;

	if ((my_lines = get_window_display_lines(window_) * ratio / 100) < 1)
		my_lines = 1;
	window_scrollback_forwards_lines(window_, my_lines);
}

/*
 * Keybinding: SCROLL_BACKWARD
 * Command: /WINDOW SCROLL_BACKWARD
 * Scrolls up the "default" amount (usually half a screenful) 
 */
static void	window_scrollback_backward (int window_)
{
	int 	ratio = get_int_var(SCROLLBACK_RATIO_VAR);
	int	my_lines;

	if (!window_is_valid(window_))
		return;

	if (ratio < 1) 
		ratio = 1;
	if (ratio > 100) 
		ratio = 100;

	if ((my_lines = get_window_display_lines(window_) * ratio / 100) < 1)
		my_lines = 1;
	window_scrollback_backwards_lines(window_, my_lines);
}

/* These are the actual keybinding functions, they're just shims */
BUILT_IN_KEYBINDING(scrollback_forwards)
{
	window_scrollback_forward(current_window_);
}

BUILT_IN_KEYBINDING(scrollback_backwards)
{
	window_scrollback_backward(current_window_);
}

BUILT_IN_KEYBINDING(scrollback_end)
{
	window_scrollback_end(current_window_);
}

BUILT_IN_KEYBINDING(scrollback_start)
{
	window_scrollback_start(current_window_);
}


/******************* Hold Mode functionality *******************************/
/*
 * UNSTOP_ALL_WINDOWS does a /WINDOW HOLD_MODE OFF on all windows.
 */
BUILT_IN_KEYBINDING(unstop_all_windows)
{
	char	my_off[4];
	char *	ptr;
	int	window_;

	for (window_ = 0; traverse_all_windows2(&window_); )
	{
		strlcpy(my_off, "OFF", sizeof(my_off));
		ptr = my_off;
		windowcmd_hold_mode(window_, (char **)&ptr);
	}
	update_all_windows();
}

/* toggle_stop_screen: the BIND function TOGGLE_STOP_SCREEN */
BUILT_IN_KEYBINDING(toggle_stop_screen)
{
	char toggle[7], *p = toggle;

	strlcpy(toggle, "TOGGLE", sizeof toggle);
	windowcmd_hold_mode(0, (char **)&p);
	update_all_windows();
}

/**************************************************************************/
/*
 * Miscelaneous -- these probably belong somewhere else, but until then,
 * theyre here.
 */


/*
 * The "Window Cursor" tells the output routines where the next line of
 * output to this window should go.  "Window Top" is offset of the window's
 * first line on the physical screen.  So by adding "Window Top" to "Window
 * Cursor", we know where to put the next line.
 *
 * However, as a special idiom, if "Window Cursor" is equal to the "Window 
 * Display Size", it points at the status bar; this is a special case which
 * is always to be interpreted in one of two ways:
 *
 *	"Window Scrollback Point" is not set:	Scroll window before output
 *	"Window Scrollback Point" is set:	Do not do any on output.
 *
 * In all cases where "Window Cursor" is less than "Window Display Size",
 * output will occur.
 *
 * This function also recalculates "Window Distance From Top Of Display To
 * the Insertion Point" which is used to decide whether to do output 
 * supression in hold mode and other places.
 */
static void 	recalculate_window_cursor_and_display_ip (int window_)
{
	Display *tmp;
	Window *window;

	if (!window_is_valid(window_))
		return;
	window = get_window_by_refnum_direct(window_);

	/* XXX - This is "impossible", but it's here to satisfy clang's analyzer */
	if (window->display_ip == NULL)
		panic(1, "recalculate_window_cursor_and_display_ip: window %d's display_ip is NULL",
			window->user_refnum);

	window->cursor = 0;
	window->display_buffer_size = 0;
	window->scrolling_distance_from_display_ip = -1;
	window->holding_distance_from_display_ip = -1;
	window->scrollback_distance_from_display_ip = -1;
	window->display_counter = 1;

	/* Recount and resequence the scrollback */
	for (tmp = window->top_of_scrollback;; tmp = tmp->next)
	{
		window->display_buffer_size++;
		tmp->count = window->display_counter++;
		if (tmp == window->display_ip)
			break;
	}

	/* Calculate the distances to the bottom of scrollback */
	if (window->holding_top_of_display)
		window->holding_distance_from_display_ip = 
			window->display_ip->count - 
				window->holding_top_of_display->count;
	if (window->scrollback_top_of_display)
		window->scrollback_distance_from_display_ip = 
			window->display_ip->count - 
				window->scrollback_top_of_display->count;

	/* XXX This is a sanity check hack. */
	if (window->scrolling_top_of_display == NULL)
		window->scrolling_top_of_display = window->display_ip;

	window->scrolling_distance_from_display_ip = 
		window->display_ip->count - 
			window->scrolling_top_of_display->count;

	/* Auto-detect when scrollback should be turned off */
	if (window->scrollback_distance_from_display_ip <= 
		window->holding_distance_from_display_ip || 
	    window->scrollback_distance_from_display_ip <= 
		window->scrolling_distance_from_display_ip)
	{
		window->scrollback_top_of_display = NULL;
		window->scrollback_distance_from_display_ip = -1;
	}

	/* Figure out where the cursor is */
	if (window->holding_distance_from_display_ip >= window->display_lines)
		window->cursor = window->display_lines;
	else if (window->scrolling_distance_from_display_ip >= window->display_lines)
		window->cursor = window->display_lines;
	else
		window->cursor = window->scrolling_distance_from_display_ip;
}

/*
 * This is used to set the current_window for the given screen.  It handles
 * last_window_refnum and making sure the window's status bar is updated
 * (for the STATUS_WINDOW thing), and arranging for the windows to be updated.
 * Its a bad idea to directly assign screen->input_window!
 */
static void 	set_screens_current_window (int screen_, int window)
{
	Window	*w;

	if (screen_ < 0)
		return;

	if (window == 0 || ((w = get_window_by_refnum_direct(window)) == NULL))
	{
		if (get_screen_last_window_refnum(screen_) >= 1)
			w = get_window_by_refnum_direct(get_screen_last_window_refnum(screen_));
		else
			w = NULL;

		/* Cant use a window that is now on a different screen */
		/* Check check a window that doesnt exist, too! */
		if (w && get_window_screennum(w->refnum) != screen_)
			w = NULL;
		if (w && w->deceased)
			w = NULL;
	}
	if (!w)
		w = get_window_by_refnum_direct(get_screen_window_list(screen_));
	if (!w)
		panic(1, "sccw: The screen has no windows.");

	if (w->deceased)
		panic(1, "sccw: This window is dead.");
	if (get_window_screennum(w->refnum) != screen_)
		panic(1, "sccw: The window is not on that screen.");
	if (screen_ < 0)
		panic(1, "sccw: Cannot set the invisible screen's current window.");

	if (get_screen_input_window(screen_) != (int)w->refnum)
	{
		if (get_screen_input_window(screen_) > 0)
		{
			window_statusbar_needs_update(get_screen_input_window(screen_));
			set_screen_last_window_refnum(screen_, get_screen_input_window(screen_));
		}
		set_screen_input_window(screen_, w->refnum);
		window_statusbar_needs_update(w->refnum);
	}
	if (current_window_ != (int)w->refnum)
		make_window_current_by_refnum(w->refnum);

	w->priority = current_window_priority++;
}

/*
 * This is used to make the specified window the current window.  This
 * is preferable to directly doing the assign, because it can deal with
 * finding a current window if the old one has gone away.
 */
void	make_window_current_by_refnum (int refnum)
{
	Window *window;
	int	old_current_window_ = current_window_;
	int	old_screen, old_window, old_window_user_refnum;
	int	new_screen, new_window, new_window_user_refnum;

	if (refnum == -1)
		return;
	else if (refnum == 0)
		window = NULL;
	else if (window_is_valid(refnum))
		window = get_window_by_refnum_direct(refnum);
	else
	{
		say("Window [%d] doesnt exist any more.  Punting.", refnum);
		return;
	}

	if (!window)
		current_window_ = get_screen_input_window(last_input_screen);
	else if (current_window_ != (int)window->refnum)
		current_window_ = window->refnum;

	if (current_window_ < 1)
		current_window_ = get_screen_window_list(last_input_screen);

	if (current_window_ < 1)
		current_window_ = get_screen_window_list(main_screen);

	if (current_window_ < 1)
		panic(1, "make_window_current_by_refnum(NULL) -- can't find another window");

	if (get_window_deceased(current_window_))
		panic(1, "This window is dead and cannot be made current");

	if (current_window_ == old_current_window_)
		return;

	if (old_current_window_ < 1)
		old_screen = old_window = -1;
	else if (get_window_screennum(old_current_window_) < 0)
		old_screen = -1, old_window = old_current_window_;
	else
		old_screen = get_window_screennum(old_current_window_),
		old_window = old_current_window_;

	new_window = current_window_;
	if (get_window_screennum(current_window_) < 0)
		new_screen = -1;
	else
		new_screen = get_window_screennum(current_window_);

	old_window_user_refnum = get_window_user_refnum(old_window);
	new_window_user_refnum = get_window_user_refnum(new_window);

	do_hook(SWITCH_WINDOWS_LIST, "%d %d %d %d",
		old_screen, old_window_user_refnum,
		new_screen, new_window_user_refnum);
}

int	make_window_current_informally (int refnum)
{
	if (window_is_valid(refnum))
	{
		current_window_ = refnum;
		return 0;
	}
	else
		return -1;
}

/**************************************************************************/
/*
 * This puts the given string into a scratch window.  It ALWAYS suppresses
 * any further action (by returning a FAIL, so rite() is not called).
 */
static int	change_line (int window_, const char *str)
{
	Display *my_line;
	int 	cnt;
	int	chg_line;
	Window *window;

	if (!window_is_valid(window_))
		return 0;

	window = get_window_by_refnum_direct(window_);

	chg_line = window->change_line;
	window->change_line = -1;

	/* Must have been asked to change a line */
	if (chg_line == -1)
		panic(1, "This is not a scratch window.");

	/* Outputting to a 0 size window is a no-op. */
	if (window->display_lines == 0)
		return 0;

	/* Must be within the bounds of the size of the window */
	if (chg_line >= window->display_lines)
		panic(1, "change_line is too big.");

	/* Make sure that the line exists that we want to change */
	while (window->scrolling_distance_from_display_ip <= chg_line)
		add_to_display(window_, empty_string, -1);

	/* Now find the line we want to change */
	my_line = window->scrolling_top_of_display;
	if (my_line == window->display_ip)
		panic(1, "Can't change line [%d] -- doesn't exist", 
			chg_line);
	for (cnt = 0; cnt < chg_line; cnt++)
	{
		my_line = my_line->next;
		if (my_line == window->display_ip)
			panic(1, "Can't change line [%d] -- doesn't exist", 
				chg_line);
	}

	/*
	 * Now change the line, move the logical cursor, and then let
	 * the caller (window_disp) output the new line.
	 */
	malloc_strcpy(&my_line->line, (const char *)str);
	window->cursor = chg_line;
	return 1;		/* Express a success */
}

/* Used by function_windowctl */
/*
 * $windowctl(REFNUMS)
 * $windowctl(REFNUM window-desc)
 * $windowctl(MAX)
 * $windowctl(GET 0 [LIST])
 * $windowctl(SET 0 [ITEM] [VALUE])
 *
 * [LIST] and [ITEM] are one of the following:
 *	REFNUM			Unique integer that identifies one window
 *	NAME			A name given to the window by user
 *	SERVER			The server this window sends msgs to
 *	LAST_SERVER		If disconnected, the last server connected.
 *	PRIORITY		The "current window" priority on this server.
 *	VISIBLE			Whether this window is on a screen or not
 *	TOP
 *	BOTTOM
 *	CURSOR
 *	NOSCROLLCURSOR
 *	ABSOLUTE_SIZE
 *	SCROLL
 *	CHANGE_LINE
 *	OLD_SIZE
 *	UPDATE
 *	MISCFLAGS
 *	BEEP_ALWAYS
 *	NOTIFY_LEVEL
 *	WINDOW_LEVEL
 *	SKIP
 *	COLUMNS
 *	PROMPT
 *	STATUS_FORMAT
 *	STATUS_FORMAT1
 *	STATUS_FORMAT2
 *	DISPLAY_BUFFER_SIZE
 *	DISPLAY_BUFFER_MAX
 *	DISPLAY_SIZE
 *	HOLD_MODE
 *	AUTOHOLD
 *	LINES_HELD
 *	HOLD_INTERVAL
 *	LAST_LINES_HELD
 *	DISTANCE_FROM_DISPLAY_IP
 *	WAITING_CHANNEL
 *	BIND_CHANNEL
 *	QUERY_NICK
 *	NICKLIST
 *	LASTLOG_LEVEL
 *	LASTLOG_SIZE
 *	LASTLOG_MAX
 *	LOGGING
 *	LOGFILE
 *	DECEASED
 */
char 	*windowctl 	(char *input)
{
	int	refnum, len;
	char	*listc;
	char 	*ret = NULL;
	Window	*w;
	int	old_status_update;


	GET_FUNC_ARG(listc, input);
	len = strlen(listc);
	if (!my_strnicmp(listc, "REFNUM", len)) {
	    char *windesc;

	    GET_FUNC_ARG(windesc, input);
	    if ((refnum = get_window_by_desc(windesc)) < 1)
		RETURN_EMPTY;
	    RETURN_INT(get_window_user_refnum(refnum));
	} else if (!my_strnicmp(listc, "REFNUMS", len)) {
		refnum = 0;
		while (traverse_all_windows2(&refnum))
		    malloc_strcat_wordlist(&ret, space, ltoa(get_window_user_refnum(refnum)));
		RETURN_MSTR(ret);
	} else if (!my_strnicmp(listc, "REFNUMS_BY_PRIORITY", len)) {
		refnum = 0;
		while (traverse_all_windows_by_priority(&refnum))
		    malloc_strcat_wordlist(&ret, space, ltoa(get_window_user_refnum(refnum)));
		RETURN_MSTR(ret);
	} else if (!my_strnicmp(listc, "REFNUMS_ON_SCREEN", len)) {
		int s_;

		/* First, get a window so we can get its screen */
		GET_INT_ARG(refnum, input);
		if (!(w = get_window_by_refnum_direct(refnum)))
			RETURN_EMPTY;
		s_ = get_window_screennum(w->refnum);

		/* Then, walk that screen */
		refnum = 0;
		while (traverse_all_windows_on_screen2(&refnum, s_))
		    malloc_strcat_wordlist(&ret, space, ltoa(get_window_user_refnum(refnum)));
		RETURN_MSTR(ret);
	} else if (!my_strnicmp(listc, "NEW", len)) {
	    int new_refnum;

	    old_status_update = permit_status_update(0);
	    new_refnum = new_window(get_window_screennum(current_window_));
	    permit_status_update(old_status_update);
	    if (new_refnum > 0) {
	        make_window_current_by_refnum(new_refnum);
		RETURN_INT(get_window_user_refnum(new_refnum));
	    }
	    else
		RETURN_INT(-1);
	} else if (!my_strnicmp(listc, "NEW_HIDE", len)) {
	    int new_refnum;

	    if ((new_refnum = new_window(-1)) > 0)
		RETURN_INT(get_window_user_refnum(new_refnum));
	    else
		RETURN_INT(-1);
	} else if (!my_strnicmp(listc, "GET", len)) {
	    GET_INT_ARG(refnum, input);
	    if (!(w = get_window_by_refnum_direct(refnum)))
		RETURN_EMPTY;

	    GET_FUNC_ARG(listc, input);
	    len = strlen(listc);

	    if (!my_strnicmp(listc, "REFNUM", len)) {
		RETURN_INT(w->user_refnum);
	    } else if (!my_strnicmp(listc, "NAME", len)) {
		RETURN_STR(w->name);
	    } else if (!my_strnicmp(listc, "SERVER", len)) {
		RETURN_INT(w->server);
	    } else if (!my_strnicmp(listc, "LAST_SERVER", len)) {
		RETURN_INT(NOSERV);
	    } else if (!my_strnicmp(listc, "PRIORITY", len)) {
		RETURN_INT(w->priority);
	    } else if (!my_strnicmp(listc, "VISIBLE", len)) {
		RETURN_INT(get_window_screennum(w->refnum) >= 0 ? 1 : 0);
	    } else if (!my_strnicmp(listc, "SAVED", len)) {
		RETURN_INT(w->toplines_wanted);
	    } else if (!my_strnicmp(listc, "TOP", len)) {
		RETURN_INT(w->top);
	    } else if (!my_strnicmp(listc, "BOTTOM", len)) {
		RETURN_INT(w->bottom);
	    } else if (!my_strnicmp(listc, "CURSOR", len)) {
		RETURN_INT(w->cursor);
	    } else if (!my_strnicmp(listc, "NOSCROLLCURSOR", len)) {
		RETURN_INT(-1);
	    } else if (!my_strnicmp(listc, "FIXED", len)) {
		RETURN_INT(w->fixed_size);
	    } else if (!my_strnicmp(listc, "SCROLL", len)) {
		RETURN_INT(-1);
	    } else if (!my_strnicmp(listc, "CHANGE_LINE", len)) {
		RETURN_INT(w->change_line);
	    } else if (!my_strnicmp(listc, "OLD_SIZE", len)) {
		RETURN_INT(w->old_display_lines);
	    } else if (!my_strnicmp(listc, "UPDATE", len)) {
		RETURN_INT(w->update);
	    } else if (!my_strnicmp(listc, "MISCFLAGS", len)) {
		RETURN_INT(0);
	    } else if (!my_strnicmp(listc, "NOTIFY", len)) {
		RETURN_INT(w->notify_when_hidden);
	    } else if (!my_strnicmp(listc, "NOTIFY_NAME", len)) {
		RETURN_STR(w->notify_name);
	    } else if (!my_strnicmp(listc, "NOTIFIED", len)) {
		RETURN_INT(w->notified);
	    } else if (!my_strnicmp(listc, "BEEP_ALWAYS", len)) {
		RETURN_INT(w->beep_always);
	    } else if (!my_strnicmp(listc, "NOTIFY_LEVEL", len)) {
		RETURN_STR(mask_to_str(&w->notify_mask));
	    } else if (!my_strnicmp(listc, "WINDOW_LEVEL", len)) {
		RETURN_STR(mask_to_str(&w->window_mask));
	    } else if (!my_strnicmp(listc, "SKIP", len)) {
		RETURN_INT(w->skip);
	    } else if (!my_strnicmp(listc, "COLUMNS", len)) {
		RETURN_INT(w->my_columns);
	    } else if (!my_strnicmp(listc, "DOUBLE", len)) {
		RETURN_INT(w->status.number - 1);
	    } else if (!my_strnicmp(listc, "STATUS_FORMAT", len)) {
		RETURN_STR(w->status.line[0].raw);
	    } else if (!my_strnicmp(listc, "STATUS_FORMAT1", len)) {
		RETURN_STR(w->status.line[1].raw);
	    } else if (!my_strnicmp(listc, "STATUS_FORMAT2", len)) {
		RETURN_STR(w->status.line[2].raw);
	    } else if (!my_strnicmp(listc, "STATUS_LINE", len)) {
		RETURN_STR(w->status.line[0].result);
	    } else if (!my_strnicmp(listc, "STATUS_LINE1", len)) {
		RETURN_STR(w->status.line[1].result);
	    } else if (!my_strnicmp(listc, "STATUS_LINE2", len)) {
		RETURN_STR(w->status.line[2].result);
	    } else if (!my_strnicmp(listc, "DISPLAY_BUFFER_SIZE", len)) {
		RETURN_INT(w->display_buffer_size);
	    } else if (!my_strnicmp(listc, "DISPLAY_BUFFER_MAX", len)) {
		RETURN_INT(w->display_buffer_max);
	    } else if (!my_strnicmp(listc, "SCROLLING_DISTANCE", len)) {
		RETURN_INT(w->scrolling_distance_from_display_ip);
	    } else if (!my_strnicmp(listc, "HOLDING_DISTANCE", len)) {
		RETURN_INT(w->holding_distance_from_display_ip);
	    } else if (!my_strnicmp(listc, "SCROLLBACK_DISTANCE", len)) {
		RETURN_INT(w->scrollback_distance_from_display_ip);
	    } else if (!my_strnicmp(listc, "DISPLAY_COUNTER", len)) {
		RETURN_INT(w->display_counter);
	    } else if (!my_strnicmp(listc, "HOLD_SLIDER", len)) {
		RETURN_INT(w->hold_slider);
	    } else if (!my_strnicmp(listc, "HOLD_INTERVAL", len)) {
		RETURN_INT(w->hold_interval);
	    } else if (!my_strnicmp(listc, "INDENT", len)) {
		RETURN_INT(w->indent);
	    } else if (!my_strnicmp(listc, "LAST_LINES_HELD", len)) {
		RETURN_INT(-1);
	    } else if (!my_strnicmp(listc, "CHANNELS", len)) {
		RETURN_MSTR(window_all_channels(w->refnum, w->server));
	    } else if (!my_strnicmp(listc, "CURRENT_CHANNEL", len)) {
		RETURN_STR(window_current_channel(w->refnum, w->server));
	    } else if (!my_strnicmp(listc, "WAITING_CHANNEL", len)) {
		RETURN_STR(get_waiting_channels_by_window(w->refnum));
	    } else if (!my_strnicmp(listc, "BIND_CHANNEL", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "QUERY_NICK", len)) {
		const char *cc = get_window_equery(w->refnum);
		RETURN_STR(cc);
	    } else if (!my_strnicmp(listc, "NICKLIST", len)) {
		RETURN_MSTR(get_nicklist_by_window(w->refnum));
	    } else if (!my_strnicmp(listc, "LASTLOG_LEVEL", len)) {
		RETURN_STR(mask_to_str(&w->lastlog_mask));
	    } else if (!my_strnicmp(listc, "LASTLOG_SIZE", len)) {
		RETURN_INT(w->lastlog_size);
	    } else if (!my_strnicmp(listc, "LASTLOG_MAX", len)) {
		RETURN_INT(w->lastlog_max);
	    } else if (!my_strnicmp(listc, "LOGGING", len)) {
		RETURN_INT(w->log);
	    } else if (!my_strnicmp(listc, "LOGFILE", len)) {
		RETURN_STR(w->logfile);
	    } else if (!my_strnicmp(listc, "LOG_MANGLE", len)) {
		RETURN_STR(w->log_mangle_str);
	    } else if (!my_strnicmp(listc, "LOG_REWRITE", len)) {
		RETURN_STR(w->log_rewrite);
	    } else if (!my_strnicmp(listc, "SWAPPABLE", len)) {
		RETURN_INT(w->swappable);
	    } else if (!my_strnicmp(listc, "SCROLLADJ", len)) {
		RETURN_INT(w->swappable);
	    } else if (!my_strnicmp(listc, "SCROLL_LINES", len)) {
		RETURN_INT(w->scroll_lines);
	    } else if (!my_strnicmp(listc, "DECEASED", len)) {
		RETURN_INT(w->deceased);
	    } else if (!my_strnicmp(listc, "TOPLINE", len)) {
		int	i;
		GET_INT_ARG(i, input);
		if (i <= 0 || i >= 10)
			RETURN_EMPTY;
		RETURN_STR(w->topline[i-1]);
	    } else if (!my_strnicmp(listc, "TOPLINES", len)) {
		RETURN_INT(w->toplines_wanted);
	    } else if (!my_strnicmp(listc, "ACTIVITY_FORMAT", len)) {
		int	i;
		GET_INT_ARG(i, input);
		if (i < 0 || i > 10)
			RETURN_EMPTY;
		RETURN_STR(w->activity_format[i]);
	    } else if (!my_strnicmp(listc, "ACTIVITY_DATA", len)) {
		int	i;
		GET_INT_ARG(i, input);
		if (i < 0 || i > 10)
			RETURN_EMPTY;
		RETURN_STR(w->activity_data[i]);
	    } else if (!my_strnicmp(listc, "CURRENT_ACTIVITY", len)) {
		RETURN_INT(w->current_activity);
	    } else if (!my_strnicmp(listc, "DISPLAY_SIZE", len)) {
		RETURN_INT(w->display_lines);
	    } else if (!my_strnicmp(listc, "SCREEN", len)) {
		RETURN_INT(get_window_screennum(w->refnum));
	    } else if (!my_strnicmp(listc, "LINE", len)) {
		Display *Line;
		int	line;

		GET_INT_ARG(line, input);
		Line = w->display_ip;
		for (; line > 0 && Line; line--)
			Line = Line->prev;

		if (Line && Line->line) {
			char *ret2 = denormalize_string(Line->line);
			RETURN_MSTR(ret2);
		}
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "SERVER_STRING", len)) {
		RETURN_STR(w->original_server_string);
	    } else if (!my_strnicmp(listc, "UUID", len)) {
		RETURN_STR(w->uuid);
	    } else
		RETURN_EMPTY;
	} else if (!my_strnicmp(listc, "SET", len)) {
	    GET_INT_ARG(refnum, input);
	    if (!(w = get_window_by_refnum_direct(refnum)))
		RETURN_EMPTY;

	    GET_FUNC_ARG(listc, input);
	    len = strlen(listc);

	    if (!my_strnicmp(listc, "REFNUM", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "NAME", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "SERVER", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "LAST_SERVER", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "PRIORITY", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "VISIBLE", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "SAVED", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "TOP", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "BOTTOM", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "CURSOR", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "NOSCROLLCURSOR", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "FIXED", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "SCROLL", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "CHANGE_LINE", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "OLD_SIZE", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "UPDATE", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "MISCFLAGS", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "NOTIFY", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "NOTIFY_NAME", len)) {
		if (input && *input)
			malloc_strcpy(&w->notify_name, input);
		else
			new_free(&w->notify_name);
		RETURN_INT(1);
	    } else if (!my_strnicmp(listc, "NOTIFIED", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "BEEP_ALWAYS", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "NOTIFY_LEVEL", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "WINDOW_LEVEL", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "SKIP", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "COLUMNS", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "INDENT", len)) {
		GET_INT_ARG(w->indent, input);
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "PROMPT", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "DOUBLE", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "STATUS_FORMAT", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "STATUS_FORMAT1", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "STATUS_FORMAT2", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "STATUS_LINE", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "STATUS_LINE1", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "STATUS_LINE2", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "DISPLAY_BUFFER_SIZE", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "DISPLAY_BUFFER_MAX", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "SCROLLING_DISTANCE", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "HOLDING_DISTANCE", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "SCROLLBACK_DISTANCE", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "DISPLAY_COUNTER", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "HOLD_SLIDER", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "HOLD_INTERVAL", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "LAST_LINES_HELD", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "WAITING_CHANNEL", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "BIND_CHANNEL", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "QUERY_NICK", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "NICKLIST", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "LASTLOG_LEVEL", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "LASTLOG_SIZE", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "LASTLOG_MAX", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "LOGGING", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "LOGFILE", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "LOG_MANGLE", len)) {
		if (empty(input))
		{
			new_free(&w->log_mangle_str);
			w->log_mangle = 0;
		}
		else
		{
			char *nv = NULL;
			w->log_mangle = parse_mangle(input, w->log_mangle, &nv);
			malloc_strcpy(&w->log_mangle_str, nv);
			new_free(&nv);
		}
	    } else if (!my_strnicmp(listc, "LOG_REWRITE", len)) {
		if (empty(input))
			new_free(&w->log_rewrite);
		else
			malloc_strcpy(&w->log_rewrite, input);
	    } else if (!my_strnicmp(listc, "SWAPPABLE", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "SCROLLADJ", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "DECEASED", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "TOPLINE", len)) {
		int line;

		GET_INT_ARG(line, input)
		if (line <= 0 || line >= 10)
			RETURN_INT(0);
		malloc_strcpy(&w->topline[line-1], input);
		window_body_needs_redraw(w->refnum);
		RETURN_INT(1);
	    } else if (!my_strnicmp(listc, "TOPLINES", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "ACTIVITY_FORMAT", len)) {
		int line;

		GET_INT_ARG(line, input)
		if (line < 0 || line > 10)
			RETURN_EMPTY;
		malloc_strcpy(&w->activity_format[line], input);
		window_statusbar_needs_update(w->refnum);
		RETURN_INT(1);
	    } else if (!my_strnicmp(listc, "ACTIVITY_DATA", len)) {
		int line;

		GET_INT_ARG(line, input)
		if (line < 0 || line > 10)
			RETURN_EMPTY;
		malloc_strcpy(&w->activity_data[line], input);
		window_statusbar_needs_update(w->refnum);
		RETURN_INT(1);
	    } else if (!my_strnicmp(listc, "CURRENT_ACTIVITY", len)) {
		int	line;

		GET_INT_ARG(line, input)
		if (line < 0 || line > 10)
			RETURN_EMPTY;
		w->current_activity = line;
		window_statusbar_needs_update(w->refnum);
		RETURN_INT(1);
	    } else if (!my_strnicmp(listc, "DISPLAY_SIZE", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "SCREEN", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "LINE", len)) {
		RETURN_EMPTY;
	    } else
		RETURN_EMPTY;
	} else
		RETURN_EMPTY;

	update_all_status();
	update_all_windows();
	RETURN_EMPTY;
}

static int	count_fixed_windows (int screen_)
{
	int	count = 0;
	int	w_ = 0;

	while (traverse_all_windows_on_screen2(&w_, screen_))
		if (get_window_fixed_size(w_) && get_window_skip(w_))
			count++;

	return count;
}

static void	window_change_server (int window_, int server) 
{
	Window *window = get_window_by_refnum_direct(window_);
	int oldserver;

	if (!window)
		return;

	oldserver = window->server; 
	window->server = server;
	do_hook(WINDOW_SERVER_LIST, "%u %d %d", window->user_refnum, oldserver, server);
	update_all_status();
}

int     get_window_lastlog_max          (int window)
{
	Window *w = get_window_by_refnum_direct(window);

	return w->lastlog_max;
}

int	set_window_lastlog_max		(int window, int value)
{
	Window *w = get_window_by_refnum_direct(window);

	w->lastlog_max = value;
	return 0;
}

int     set_window_lastlog_size_decr    (int window)
{
	Window *w = get_window_by_refnum_direct(window);

	w->lastlog_size--;
	return 0;
}

int     set_window_lastlog_size_incr    (int window)
{
	Window *w = get_window_by_refnum_direct(window);

	w->lastlog_size++;
	return 0;
}

static	int	list_check = 1;

static int	set_window_list_check		(int window, int value)
{
	int	old_value = list_check;

	list_check = value;
	return old_value;
}

int	get_window_prev			(int window)
{
	Window *w = get_window_by_refnum_direct(window);
	int	sp;

	if (w->screen_ >= 0)
	{
	    if (list_check)
	    {
		sp = screen_get_window_prev(w->screen_, w->refnum);
		if (sp != w->_prev_)
			yell("for window %d, screen says prev is %d, window thinks it's %d", window, sp, w->_prev_);
	    }
	}
	return w->_prev_;
}

int	get_window_next			(int window)
{
	Window *w = get_window_by_refnum_direct(window);
	int	sp;

	if (w->screen_ >= 0)
	{
	    if (list_check)
	    {
		sp = screen_get_window_next(w->screen_, w->refnum);
		if (sp != w->_next_)
			yell("for window %d, screen says next is %d, window thinks it's %d", window, sp, w->_next_);
	    }
	}
	return w->_next_;
}

int	set_window_prev			(int window, int prev)
{
	Window *w = get_window_by_refnum_direct(window);

	if (!w)
		return -1;

	if (window == prev)
		panic(1, "I can't set window->prev to myself");

	w->_prev_ = prev;
	return 0;
}

int	set_window_next			(int window, int next)
{
	Window *w = get_window_by_refnum_direct(window);

	if (!w)
		return -1;

	if (window == next)
		panic(1, "I can't set window->next to myself");

	w->_next_ = next;
	return 0;
}

int	get_window_killable		(int window)
{
	Window *w = get_window_by_refnum_direct(window);

	if (!w)
		return -1;

	return w->killable;
}

int	set_window_killable		(int window, int value)
{
	Window *w = get_window_by_refnum_direct(window);

	if (!w)
		return -1;
	if (value != 0 && value != 1)
		return -1;

	w->killable = value;
	return 0;
}

static Display *get_window_clear_point		(int window)
{
	Window *w = get_window_by_refnum_direct(window);

	if (!w)
		return NULL;
	return w->clear_point;
}

static int	reset_window_clear_point	(int window)
{
	Window *w = get_window_by_refnum_direct(window);

	if (!w)
		return 0;
	w->clear_point = NULL;
	return 1;
}


#if 0
void    help_topics_window (FILE *f)
{
        int     x;

        for (x = 0; options[x].func; x++)
                fprintf(f, "window %s\n", options[x].command);
}
#endif
