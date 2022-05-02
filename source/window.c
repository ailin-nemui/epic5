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

#include "irc.h"
#include "screen.h"
#include "window.h"
#include "vars.h"
#include "server.h"
#include "list.h"
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
static	Window	*current_window = NULL;

/*
 * All of the hidden windows.  These windows are not on any screen, and
 * therefore are not visible.
 */
static	Window	*invisible_list = (Window *) 0;

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


static 	void 	revamp_window_masks 		(Window *);
static	void 	clear_window 			(Window *);
static	void	resize_window_display 		(Window *);
static 	int	windowcmd_next 			(int, char **);
static 	int	windowcmd_previous 		(int, char **);
static 	void	window_scrollback_start 	(Window *window);
static 	void	window_scrollback_end 		(Window *window);
static void	window_scrollback_backward 	(Window *window);
static void	window_scrollback_forward 	(Window *window);
static void	window_scrollback_backwards_lines 	(Window *window, int);
static void	window_scrollback_forwards_lines	(Window *window, int);
static 	void 	window_scrollback_to_string 	(Window *window, regex_t *str);
static 	void 	window_scrollforward_to_string 	(Window *window, regex_t *str);
static	int	change_line 			(Window *, const unsigned char *);
static	int	add_to_display 			(Window *, const unsigned char *, intmax_t);
static	Display *new_display_line 		(Display *prev, Window *w);
static	int	add_waiting_channel 		(Window *, const char *);
static 	void   	destroy_window_waiting_channels	(int);
static 	int	flush_scrollback_after		(Window *);
static 	int	flush_scrollback		(Window *);
static void	unclear_window 			(Window *window);
static	void	rebuild_scrollback 		(Window *w);
static	void	window_check_columns 		(Window *w);
static void	restore_window_positions 	(Window *w, intmax_t scrolling, intmax_t holding, intmax_t scrollback);
static void	save_window_positions 		(Window *w, intmax_t *scrolling, intmax_t *holding, intmax_t *scrollback);
static void	adjust_context_windows 		(int old_win, int new_win);
static void	window_statusbar_needs_redraw 	(int);
static void	window_change_server 		(Window * win, int server) ;
static void	window_body_needs_redraw 	(int refnum);
static void 	make_window_current 		(Window *window);
static void 	recalculate_window_cursor_and_display_ip (Window *window);

static Window *	add_to_window_list 		(Screen *screen, Window *new_w);
static 	void 	remove_from_invisible_list 	(Window *);
static	void 	remove_window_from_screen 	(Window *window, int hide, int recalc);
static 	int	count_fixed_windows 		(Screen *s);
static	void 	set_screens_current_window 	(Screen *, int);
static	int	get_next_window  		(int);
static	int	get_previous_window 		(int);
static 	void 	swap_window 			(Window *, Window *);


/* * * * * * * * * * * CONSTRUCTOR AND DESTRUCTOR * * * * * * * * * * * */
/*
 * new_window: This creates a new window on the screen.  It does so by either
 * splitting the current window, or if it can't do that, it splits the
 * largest window.  The new window is added to the window list and made the
 * current window 
 */
int	new_window (Screen *screen)
{
	Window	*	new_w;
	Window	*	tmp = NULL;
	unsigned	new_refnum;
	int		i;

	if (dumb_mode && current_window)
		return -1;

	new_w = (Window *) new_malloc(sizeof(Window));

	/*
	 * STAGE 1 -- Ensuring all values are set to default values
	 */
	/* Meta stuff */
	new_refnum = INTERNAL_REFNUM_CUTOVER;
	tmp = NULL;
	while (traverse_all_windows(&tmp))
	{
		if (tmp->refnum == new_refnum)
		{
			new_refnum++;
			tmp = NULL;
		}
	}
	new_w->refnum = new_refnum;
	windows[new_w->refnum] = new_w;

	new_refnum = 1;
	tmp = NULL;
	while (traverse_all_windows(&tmp))
	{
		if (tmp->user_refnum == new_refnum)
		{
			new_refnum++;
			tmp = NULL;
		}
	}
	new_w->user_refnum = new_refnum;
	windows[new_w->user_refnum] = new_w;

	new_w->name = NULL;
	new_w->uuid = uuid4_generate_no_dashes();	/* THIS NEVER CHANGES */
	new_w->priority = -1;		/* Filled in later */

	/* Output rule stuff */
	if (current_window)
		new_w->server = current_window->server;
	else
		new_w->server = NOSERV;
	new_w->original_server_string = NULL;

	if (!current_window)		/* First window ever */
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
#if 0
	new_w->prompt = NULL;		/* Filled in later */
#endif
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

	/* The scrollback indicator */
	new_w->scrollback_indicator = (Display *)new_malloc(sizeof(Display));
	new_w->scrollback_indicator->line = NULL;
	new_w->scrollback_indicator->count = -1;
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
	new_w->_next = new_w->_prev = NULL;
	new_w->deceased = 0;

	/*
	 * STAGE 2 -- Bringing the window to life 
	 */
	/* Initialize the scrollback */
	new_w->rebuild_scrollback = 0;
	new_w->top_of_scrollback = new_display_line(NULL, new_w);
	new_w->top_of_scrollback->line = NULL;
	new_w->top_of_scrollback->next = NULL;
	new_w->display_buffer_size = 1;
	new_w->display_ip = new_w->top_of_scrollback;
	new_w->scrolling_top_of_display = new_w->top_of_scrollback;
	new_w->old_display_lines = 1;

	/* Make the window visible (or hidden) to set its geometry */
	if (screen && add_to_window_list(screen, new_w))
		set_screens_current_window(screen, new_w->refnum);
	else
	{
		new_w->screen = NULL;
		add_to_invisible_list(new_w->refnum);
	}

	/* Finally bootstrap the visible part of the window */
	resize_window_display(new_w);
	window_statusbar_needs_redraw(new_w->refnum);

	/*
	 * Offer it to the user.  I dont know if this will break stuff
	 * or not.
	 */
	do_hook(WINDOW_CREATE_LIST, "%d", new_w->user_refnum);

	return new_w->refnum;
}


/*
 * delete_window: There are two important aspects to deleting a window.
 * The first aspect is window management.  We must release the window
 * from its screen (or from the invisible list) so that it is not possible
 * for the user to reference the window in any way.  We also want to 
 * re-apportion the window's visible area to other windows on the screen.
 * The second aspect is purging the window's private data.  Ideally, we 
 * want these things to take place in this order.
 */
static void 	delete_window (Window *window)
{
	char 	buffer[BIG_BUFFER_SIZE + 1];
	int	oldref;
	int	i;
	int	invisible = 0;
	int	fixed_wins;
	int	fixed;

	if (!window)
		window = current_window;

	if (!window->screen)
		invisible = 1;

	if (window->screen)
		fixed_wins = count_fixed_windows(window->screen);
	else
		fixed_wins = 0;

	if (window->fixed_size && window->skip)
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
	if (dead == 0 && window->screen)
	{
	    if ((fixed && window->screen->visible_windows == 1) ||
	        (!fixed && window->screen->visible_windows - fixed_wins <= 1
			&& !invisible_list))
	    {
		say("You can't kill the only window!");
		return;
	    }
	}

	/* Let the script have a stab at this first. */
	do_hook(WINDOW_BEFOREKILL_LIST, "%d", window->user_refnum);

	/*
	 * Mark this window as deceased.  This is important later.
	 */
	window->deceased = 1;

	/*
	 * If the client is exiting and this is the last window on the
	 * screen, we need to do some extra cleanup on the screen that
	 * otherwise we would not dare to perform.  We also want to do some
	 * extra sanity checking to make sure nothing bad has happened
	 * elsewhere.
	 */
	if ((dead == 1) && window->screen &&
	    (window->screen->visible_windows == 1))
	{
		if (window->screen->_window_list != window ||
			 window->_next != NULL ||
			 (window->screen->input_window > 0 && 
			    window->screen->input_window != (int)window->refnum))
		{
			panic(1, "delete_window: My screen says there is only one window on it, and I don't agree.");
		}
		else
		{
			window->deceased = 1;
			window->screen->_window_list = NULL;
			window->screen->visible_windows = 0;
			window->screen->input_window = -1;
			window->screen = NULL;
			if (current_window == window)
				current_window = NULL;
		}

		/*
		 * This 'goto' saves me from making the next 75 lines part
		 * of a big (ultimately unnecesary) 'else' clause, requiring
		 * me to indent it yet again and break up the lines and make
		 * it less readable.  Don't bug me about this.
		 */
		goto delete_window_contents;
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
	reassign_window_channels(window->refnum);

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
		remove_from_invisible_list(window);
	else if (fixed || window->screen->visible_windows > fixed_wins + 1)
		remove_window_from_screen(window, 0, 1);
	else if (invisible_list)
	{
		window->swappable = 1;
		swap_window(window, NULL);
	}
	else
	{
		yell("I don't know how to kill window [%d]", window->user_refnum);
		return;
	}

	/*
	 * This is done for the sake of invisible windows; but it is a safe
	 * sanity check and can be done for any window, visible or invisible.
	 * Basically, we have to be sure that we find some way to make sure
	 * that the 'current_window' pointer is not pointing at what we are
	 * about to delete (or else the client will crash.)
	 */
	if (window == current_window)
		make_window_current_by_refnum(0);
#if 0
	if ((int)window->refnum == last_input_screen->input_window)
	{
	    if (window->screen != last_input_screen)
		panic(1, "delete_window: I am not on that screen");
	    else
		make_window_current_by_refnum(last_input_screen->_window_list);
	}
#endif
	if (window == current_window)
		panic(1, "delete_window: window == current_window -- I was unable to find another window, but I already checked that, so this is a bug.");

	/*
	 * OK!  Now we have completely unlinked this window from whatever
	 * window chain it was on before, be it a screen, or be it the
	 * invisible window list.  The screens have been updated, and the
	 * only place this window exists is in our 'window' pointer.  We
	 * can now safely go about the business of eliminating what it is
	 * pointing to.
	 */
delete_window_contents:

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
			panic(1, "delete_window: display_buffer_size is %d, should be 0", window->display_buffer_size);
	}

	/* The lastlog... */
	window->lastlog_max = 0;
	truncate_lastlog(window->refnum);

	/* The nick list... */
	{
		WNickList *next;

		while (window->nicks)
		{
			next = window->nicks->next;
			new_free(&window->nicks->nick);
			new_free((char **)&window->nicks);
			window->nicks = next;
		}
	}

	destroy_window_waiting_channels(window->refnum);

	/* Adjust any active output contexts pointing at this window to point
	 * somewhere sensible instead. */
	if (current_window)
		adjust_context_windows(window->refnum, current_window->refnum);

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
#if 1
	{Window *owd = window; new_free((char **)&owd);}
#else
	new_free((char **)&window);
#endif

	if (!dead)
		do_hook(WINDOW_KILL_LIST, "%d %s", oldref, buffer);
}

/*
 * This should only ever be called by irc_exit().  DONT CALL THIS ELSEWHERE!
 */
void 	delete_all_windows (void)
{
	Window *win;

	for (win = NULL; traverse_all_windows(&win); win = NULL)
		delete_window(win);
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
int 	traverse_all_windows (Window **ptr)
{
	/*
	 * If this is the first time through...
	 */
	if (!*ptr)
	{
		Screen *screen = screen_list;
		while (screen && (!screen->alive || !screen->_window_list))
			screen = screen->next;

		if (!screen && !invisible_list)
			return 0;
		else if (!screen)
			*ptr = invisible_list;
		else
			*ptr = screen->_window_list;
	}

	/*
	 * As long as there is another window on this screen, keep going.
	 */
	else if ((*ptr)->_next)
		*ptr = (*ptr)->_next;

	/*
	 * If there are no more windows on this screen, but we do belong to
	 * a screen (eg, we're not invisible), try the next screen
	 */
	else if ((*ptr)->screen)
	{
		/*
		 * Skip any dead screens
		 */
		Screen *ns = (*ptr)->screen->next;
		while (ns && (!ns->alive || !ns->_window_list))
			ns = ns->next;

		/*
		 * If there are no other screens, then if there is a list
		 * of hidden windows, try that.  Otherwise we're done.
		 */
		if (!ns && !invisible_list)
			return 0;
		else if (!ns)
			*ptr = invisible_list;
		else
			*ptr = ns->_window_list;
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
 * return the window with the next lower priority.
 *
 * To initialize, *ptr should be NULL.  The function will return 1 each time
 * *ptr is set to the next valid window.  When the function returns 0, then
 * you have iterated all windows.
 */
static int 	traverse_all_windows_by_priority (Window **ptr)
{
	Window 		*w, *winner = NULL;
	unsigned	ceiling;

	/*
	 * If this is the first time through...
	 */
	if (*ptr)
		ceiling = (*ptr)->priority;
	else
		ceiling = (unsigned)-1;

	for (w = NULL; traverse_all_windows(&w); )
	{
		if ( (!winner || w->priority > winner->priority)
				&& w->priority < ceiling)
			winner = w;
	}

	/* If there was a winner, return 1 */
	if ((*ptr = winner))
		return 1;

	/* No Winner?  Then we're done. */
	return 0;
}

/*
 * traverse_all_windows_on_screen: 
 * This function shall return all windows on 'screen' in the order that
 * they are on the screen, top to bottom.
 *
 * To initialize, *ptr should be NULL.  The function will return 1 each time
 * *ptr is set to the next valid window.  When the function returns 0, then
 * you have iterated all windows.
 */
static int 	traverse_all_windows_on_screen (Window **ptr, Screen *s)
{
	/*
	 * If this is the first time through...
	 * 's' == NULL means traverse all invisible windows.
	 */
	if (!*ptr)
	{
		if (!s)
			*ptr = invisible_list;
		else
			*ptr = s->_window_list;
	}

	/*
	 * As long as there is another window on this screen, keep going.
	 */
	else if ((*ptr)->_next)
		*ptr = (*ptr)->_next;

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

int	traverse_all_windows2 (int *refnum)
{
	Window *w = NULL;

	if (refnum && *refnum >= 1)
		w = get_window_by_refnum_direct(*refnum);

	if (traverse_all_windows(&w))
		*refnum = w->refnum;
	else
		*refnum = 0;
	return *refnum;
}

/* * * * * * * * * * * * * * * * WINDOW LISTS * * * * * * * * * * * * * * * */

/*
 * Handle the client's list of invisible windows.
 */
static void 	remove_from_invisible_list (Window *window)
{
	Window *w;

	/* Purely a sanity check */
	for (w = invisible_list; w && w != window; w = w->_next)
		;
	if (!w)
		panic(1, "remove_from_invisible_list: This window is _not_ invisible");

	/*
	 * Unlink it from the list
	 */
	if (window->_prev)
		window->_prev->_next = window->_next;
	else
		invisible_list = window->_next;
	if (window->_next)
		window->_next->_prev = window->_prev;
}

void 	add_to_invisible_list (int window_)
{
	Window *window = get_window_by_refnum_direct(window_);
	Window *w;

	/*
	 * XXX Sanity check -- probably unnecessary
	 * If the window is already invisible, do nothing
	 */
	for (w = invisible_list; w; w = w->_next)
		if (w == window)
			return;

	/*
	 * Because this blows away window->_next, it is implicitly
	 * assumed that you have already removed the window from
	 * its screen.
	 */
	if ((window->_next = invisible_list) != NULL)
		invisible_list->_prev = window;

	invisible_list = window;
	window->_prev = (Window *) 0;
	if (window->screen)
		window->my_columns = window->screen->co;
	else
		window->my_columns = current_term->TI_cols;	/* Whatever */
	window->screen = (Screen *) 0;
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
static Window *add_to_window_list (Screen *screen, Window *new_w)
{
	Window	*biggest = (Window *) 0,
		*tmp,
		*winner;
	int	orig_size;
	int	size, need;
	int     skip_fixed;

	if (screen == NULL)
		panic(1, "add_to_window_list: Cannot add window [%d] to NULL screen.", new_w->user_refnum);

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
	if (!screen->_window_list)
	{
		screen->visible_windows++;
		new_w->screen = screen;
		screen->_window_list_end = screen->_window_list = new_w;
		new_w->my_columns = screen->co;	/* Whatever */

		if (dumb_mode)
		{
			new_w->display_lines = 24;
			set_screens_current_window(screen, new_w->refnum);
			return new_w;
		}
		recalculate_windows(screen);
		return new_w;
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
		for (tmp = screen->_window_list; tmp; tmp = tmp->_next)
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
		return NULL;
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
		return NULL;
	}

	/*
	 * Use the biggest window -- unless the current window can hold
	 * it and /set always_split_biggest is OFF.
	 */
	winner = biggest;
	if (get_window_by_refnum_direct(screen->input_window)->display_lines > need)
		if (get_int_var(ALWAYS_SPLIT_BIGGEST_VAR) == 0)
			winner = get_window_by_refnum_direct(screen->input_window);


	/*
	 * Link the new window into the list.
	 */
	if ((new_w->_prev = winner->_prev) != NULL)
		new_w->_prev->_next = new_w;
	else
		screen->_window_list = new_w;

	new_w->_next = winner;
	winner->_prev = new_w;

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
	new_w->screen = screen;
	screen->visible_windows++;
	new_w->my_columns = screen->co;		/* Whatever */

	/* Now let recalculate_windows() handle any overages */
	recalculate_windows(screen);

	return (new_w);
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
static void 	remove_window_from_screen (Window *window, int hide, int recalc)
{
	Screen *s;

	if (!((s = window->screen)))
		panic(1, "remove_window_from_screen: This window is not on a screen");

	/*
	 * We  used to go to greath lengths to figure out how to fill
	 * in the space vacated by this window.  Now we dont sweat that.
	 * we just blow away the window and then recalculate the entire
	 * screen.
	 */
	if (window->_prev)
		window->_prev->_next = window->_next;
	else
		s->_window_list = window->_next;

	if (window->_next)
		window->_next->_prev = window->_prev;
	else
		s->_window_list_end = window->_prev;

	if (!--s->visible_windows)
		return;

	if (hide)
		add_to_invisible_list(window->refnum);

	if (s->input_window == (int)window->refnum)
	{
		s->input_window = -1;
		set_screens_current_window(s, 0);
	}

	if (s->last_window_refnum == window->refnum)
		s->last_window_refnum = s->input_window;

	if (s->input_window == (int)window->refnum)
		make_window_current_by_refnum(last_input_screen->_window_list->refnum);
	else
		make_window_current_by_refnum(0);

	if (recalc)
		recalculate_windows(s);
}


/* * * * * * * * * * * * SIZE AND LOCATION PRIMITIVES * * * * * * * * * * * */
/*
 * recalculate_window_positions: This runs through the window list and
 * re-adjusts the top and bottom fields of the windows according to their
 * current positions in the window list.  This doesn't change any sizes of
 * the windows 
 */
static void	recalculate_window_positions (Screen *screen)
{
	Window	*w;
	short	top;

	if (!screen)
		return;		/* Window is hidden.  Dont bother */

	top = 0;
	for (w = screen->_window_list; w; w = w->_next)
	{
		top += w->toplines_showing;
		w->top = top;
		w->bottom = top + w->display_lines;
		top += w->display_lines + w->status.number;

		window_body_needs_redraw(w->refnum);
		window_statusbar_needs_redraw(w->refnum);
	}
}

/*
 * swap_window: This swaps the given window with the current window.  The
 * window passed must be invisible.  Swapping retains the positions of both
 * windows in their respective window lists, and retains the dimensions of
 * the windows as well 
 */
static void 	swap_window (Window *v_window, Window *window)
{
	int	check_hidden = 1;
	int	recalculate_everything = 0;

	/*
	 * v_window -- window to be swapped out
	 * window -- window to be swapped in
	 */

	/* Find any invisible window to swap in.  Prefer swappable ones */
	if (!window)
	{
		for (window = invisible_list; window; window = window->_next)
			if (window->swappable)
				break;
	}
	if (!window && invisible_list)
	{
		check_hidden = 0;
		window = invisible_list;
	}
	if (!window)
	{
		say("The window to be swapped in does not exist.");
		return;
	}

	if (window->screen || !v_window->screen)
	{
		say("You can only SWAP a hidden window with a visible window.");
		return;
	}

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
	 * Put v_window on invisible list
	 */
	v_window->screen->last_window_refnum = v_window->refnum;

	/*
	 * Take window off invisible list
	 */
	remove_from_invisible_list(window);

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
	window->screen = v_window->screen;

	if (window->display_lines < 0)
	{
		window->display_lines = 0;
		recalculate_everything = 1;
	}

	if (v_window->screen->input_window == (int)v_window->refnum)
	{
		v_window->screen->input_window = window->refnum;
		window->priority = current_window_priority++;
	}

	/*
	 * Put the window to be swapped into the screen list
	 */
	if ((window->_prev = v_window->_prev))
		window->_prev->_next = window;
	else
		window->screen->_window_list = window;

	if ((window->_next = v_window->_next))
		window->_next->_prev = window;
	else
		window->screen->_window_list_end = window;


	/*
	 * Hide the window to be swapped out
	 */
	if (!v_window->deceased)
		add_to_invisible_list(v_window->refnum);

	if (recalculate_everything)
		recalculate_windows(window->screen);
	recalculate_window_cursor_and_display_ip(window);
	resize_window_display(window);

	/* XXX Should I do this before, or after, for efficiency? */
	window_check_columns(window);

	/*
	 * And recalculate the window's positions.
	 */
	window_body_needs_redraw(window->refnum);
	window_statusbar_needs_redraw(window->refnum);
	window->notified = 0;
	window->current_activity = 0;

	/*
	 * Transfer current_window if the current window is being swapped out
	 */
	if (v_window == current_window)
		make_window_current_by_refnum(window->refnum);
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
static void 	move_window_to (Window *window, int offset)
{
	Screen *s;

	if (!window)
		return;

	/* /window move_to -1 is bogus -- but maybe it shouldn't be.  */
	if (offset <= 0)
		return;

	/* You can't /window move_to on an invisible window */
	if (!(s = window->screen))
		return;		/* Whatever */

	/* This is impossible -- just a sanity check for clang's benefit */
	if (!s->_window_list || !s->_window_list_end)
		panic(1, "window_move_to: Screen for window %d has no windows?", window->user_refnum);

	/* You can't /window move_to if there are no split windows */
	if (s->visible_windows == 1)
		return;

	/* This is impossible -- just a sanity check for clang's benefit */
	if (window->_prev == NULL && window->_next == NULL)
		panic(1, "window_move_to: window %d is has no prev or next; but its screen says it has %d windows", window->user_refnum, s->visible_windows);

	/* Move the window to the top of the screen */
	if (offset == 1)
	{
		/* If it's already at the top, we're done. */
		if (s->_window_list == window)
			return;

		if (window->_prev == NULL)
			panic(1, "window_move_to(top): Window %d prev is NULL, "
				"but s->_window_list is %d", 
				window->user_refnum, s->_window_list->user_refnum);

		window->_prev->_next = window->_next;
		if (window->_next)
			window->_next->_prev = window->_prev;
		else
			s->_window_list_end = window->_prev;

		window->_prev = NULL;
		window->_next = s->_window_list;
		window->_next->_prev = window;
		s->_window_list = window;
	}

	/* Move the window to the bottom of the screen */
	else if (offset >= s->visible_windows)
	{
		/* If it's already at the bottom, we're done. */
		if (s->_window_list_end == window)
			return;

		if (!window->_next)
		   panic(1, "window_move_to(bottom): Window %d next is NULL, "
				"but s->_window_list_end is %d", 
				window->user_refnum, s->_window_list_end->user_refnum);

		if (window->_prev)
			window->_prev->_next = window->_next;
		else
			s->_window_list = window->_next;
		window->_next->_prev = window->_prev;

		window->_prev = s->_window_list_end;
		window->_prev->_next = window;
		window->_next = NULL;
		s->_window_list_end = window;
	
	}

	/* Otherwise it's moving somewhere in the middle */
	else
	{
		Window *w;
		int	i;

		/* In order to make the window the Nth window,
		 * We need to have a pointer to the N-1th window.
		 * We know that it won't be the top or bottom.
		 */
		for (i = 0, w = s->_window_list; w; w = w->_next)
		{
			if (w != window)
				i++;		/* This is the I'th window */
			if (i + 1 == offset)
				break;		/* This is the "prev" window! */
		}

		/* XXX This is an error and I should do something here */
		if (!w)
			return;

		/* 
		 * 'w' is our "prev" window.
		 * So if window is already in the correct place, we're done 
		 */
		if (w->_next == window)
			return;

		/* Unlink our target window first */
		if (window->_prev)
			window->_prev->_next = window->_next;
		else
			s->_window_list = window->_next;

		if (window->_next)
			window->_next->_prev = window->_prev;
		else
			s->_window_list_end = window->_prev;

		window->_prev = w;
		window->_next = w->_next;

		/* One last sanity check */
		if (window->_prev == NULL || window->_next == NULL)
			panic(1, "window_move_to(%d): Window %d's prev and "
				"next are both null, but that's impossible", 
				offset, window->user_refnum);

		window->_next->_prev = window;
		window->_prev->_next = window;
	}

	set_screens_current_window(s, window->refnum);
	make_window_current_by_refnum(window->refnum);
	recalculate_window_positions(s);
}

/*
 * move_window: This moves a window offset positions in the window list. This
 * means, of course, that the window will move on the screen as well 
 *
 * Isn't it easier if I just redefine this in terms of move_to?
 */
static void 	move_window (Window *window, int offset)
{
	Screen *s;
	Window *w;
	int	location;

	if (!(s = window->screen))
		return;
	if (s->visible_windows == 0)
		return;		/* Sigh */

	offset = offset % s->visible_windows;
	if (offset == 0)
		return;

	for (location = 1, w = s->_window_list; w; location++, w = w->_next)
		if (w == window)
			break;
	if (!w)
		panic(1, "move_window: I couldn't find window %d on its "
			"own screen!", window->user_refnum);

	/* OK, so 'window' is the 'location'th window. */
	location += offset;
	if (location < 1)
		location += s->visible_windows;
	if (location > s->visible_windows)
		location -= s->visible_windows;
	
	move_window_to(w, location);
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
static 	void 	resize_window (int how, Window *window, int offset)
{
	Window	*other;
	int	window_size,
		other_size;

	if (!window)
		window = current_window;

	if (!window->screen)
	{
		say("You cannot change the size of hidden windows!");
		return;
	}

	if (how == RESIZE_ABS)
		offset -= window->display_lines;

	other = window;

	do
	{
		if (other->_next)
			other = other->_next;
		else
			other = window->screen->_window_list;

		if (other == window)
		{
			say("Can't change the size of this window!");
			return;
		}

		if (other->fixed_size)
			continue;
	}
	while (other->display_lines < offset);

	window_size = window->display_lines + offset;
	other_size = other->display_lines - offset;

	if ((window_size < 0) || (other_size < 0))
	{
		say("Not enough room to resize this window!");
		return;
	}

	window->display_lines = window_size;
	other->display_lines = other_size;
	recalculate_windows(window->screen);
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
void	resize_window_display (Window *window)
{
	int		cnt = 0, i;
	Display 	*tmp;

	if (dumb_mode)
		return;

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
	     * ircII compatability
	     */
	    if (window->scrolladj)
	    {
		for (i = 0; i < cnt; i++)
		{
			if (!tmp || !tmp->prev)
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
	recalculate_window_cursor_and_display_ip(window);

	/* XXX - This is a temporary hack, but it works. */
	if (window->scrolling_distance_from_display_ip >= window->display_lines)
		unclear_window(window);

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
	Window	*tmp = NULL;
static	int	recursion = 0;
static	int	do_input_too = 0;
static	int	restart = 0;

	if (recursion)
	{
		debuglog("update_all_windows: recursion");
		restart = 1;
		return;
	}

	recursion++;
	while (traverse_all_windows(&tmp))
	{
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
			rebuild_scrollback(tmp);
		}

		/* 
		 * Logical update Type 2 - Adjust the window's views
		 * (when the number of rows changes)
		 */
		if (tmp->display_lines != tmp->old_display_lines)
		{
			debuglog("update_all_windows(%d), resize window display",
					tmp->user_refnum);
			resize_window_display(tmp);
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
		if (!tmp->screen)
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
			repaint_window_body(tmp);
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
		update_input(NULL, UPDATE_ALL);
	}

	tmp = NULL;
	while (traverse_all_windows(&tmp))
	{
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
static void	rebalance_windows (Screen *screen)
{
	Window *tmp;
	int each, extra;
	int window_resized = 0, window_count = 0;

	if (dumb_mode)
		return;

	/*
	 * Two passes -- first figure out how much we need to balance,
	 * and how many windows there are to balance
	 */
	for (tmp = screen->_window_list; tmp; tmp = tmp->_next)
	{
		if (tmp->fixed_size)
			continue;
		window_resized += tmp->display_lines;
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
	for (tmp = screen->_window_list; tmp; tmp = tmp->_next)
	{
		if (tmp->fixed_size)
			;
		else
		{
			tmp->display_lines = each;
			if (extra)
				tmp->display_lines++, extra--;
		}
	}
	recalculate_window_positions(screen);
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
void 	recalculate_windows (Screen *screen)
{
	int	old_li = 1;
	int	required_li = 0;
	int	excess_li = 0;
	int	assignable_li = 0;
	Window	*tmp, *winner;;
	int	window_count = 0;
	int	offset;
	int	lin = 0;
	int	force;

	if (dumb_mode)
		return;

	/*
	 * If its a new screen, just set it and be done with it.
	 * XXX This seems heinously bogus.
	 */
	if (screen->input_window < 1)
	{
		screen->_window_list->top = 0;
		screen->_window_list->toplines_showing = 0;
		screen->_window_list->toplines_wanted = 0;
		screen->_window_list->display_lines = screen->li - 1 - screen->_window_list->status.number;
		screen->_window_list->bottom = screen->li - 1 - screen->_window_list->status.number;
		screen->_window_list->my_columns = screen->co;
		return;
	}

	/*
	 * This has to be done first -- if the number of columns of
	 * the screen has changed, the window needs to be told; this
	 * will provoke a full redraw (later).
	 */
	for (tmp = screen->_window_list; tmp; tmp = tmp->_next)
		window_check_columns(tmp);

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
	for (tmp = screen->_window_list; tmp; tmp = tmp->_next)
		old_li += tmp->status.number + tmp->toplines_showing + tmp->display_lines;

	/* How many lines did the screen change by? */
	excess_li = (screen->li - 1) - old_li;

	/* 
	 * If the size of the screen hasn't changed,
	 * Re-enumerate the windows, and go on our way.
	 */
	if (excess_li == 0)
	{
		recalculate_window_positions(screen);
		return;
	}


	/**********************************************************
	 * TEST #2 -- Can we fit all the windows on the screen?
	 */

	/* 
	 * Next, let's figure out how many lines MUST be shown.
	 */
	required_li = 0;
	for (tmp = screen->_window_list; tmp; tmp = tmp->_next)
		required_li += tmp->status.number + tmp->toplines_showing +
				(tmp->fixed_size ? tmp->display_lines : 0);

	/*
	 * If the number of required lines exceeds what we have on hand,
	 * then we've got problems.  We fix this by removing one window,
	 * then recursively "fixing" the screen from there. 
	 */
	if (required_li > screen->li - 1)
	{
		int     skip_fixed;

		/*
		 * Find the smallest nonfixed window and hide it.
		 */
		winner = NULL;     /* Winner? ha~! */
		for (skip_fixed = 1; skip_fixed >= 0; skip_fixed--)
		{
			for (tmp = screen->_window_list; tmp; tmp = tmp->_next)
			{
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

		remove_window_from_screen(winner, 1, 0);
		recalculate_windows(screen);
		return;
	}


	/***********************************************************
	 * TEST #3 -- Has the screen grown?
	 */
	/* Count the non-fixed windows */
	window_count = 0;
	for (tmp = screen->_window_list; tmp; tmp = tmp->_next)
	{
		if (tmp->fixed_size)
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
			for (tmp = screen->_window_list; tmp; tmp = tmp->_next)
			{
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
			for (tmp = screen->_window_list; tmp; tmp = tmp->_next)
			{
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
	recalculate_window_positions(screen);
}

static	void	window_check_columns (Window *w)
{
	if (w->screen && w->my_columns != w->screen->co)
	{
		w->my_columns = w->screen->co;
		w->rebuild_scrollback = 1;
		/* rebuild_scrollback(w); */
	}
}

static	void	rebuild_scrollback (Window *w)
{
	intmax_t	scrolling, holding, scrollback;

	save_window_positions(w, &scrolling, &holding, &scrollback);
	flush_scrollback(w);
	reconstitute_scrollback(w->refnum);
	restore_window_positions(w, scrolling, holding, scrollback);
	w->rebuild_scrollback = 0;
	do_hook(WINDOW_REBUILT_LIST, "%d", w->user_refnum);
}

static void	save_window_positions (Window *w, intmax_t *scrolling, intmax_t *holding, intmax_t *scrollback)
{
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
}

static void	restore_window_positions (Window *w, intmax_t scrolling, intmax_t holding, intmax_t scrollback)
{
	Display *d;

	/* First, we cancel all three views. */
	w->scrolling_top_of_display = NULL;
	w->holding_top_of_display = NULL;
	w->scrollback_top_of_display = NULL;

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

	/* 
	 * We must _NEVER_ allow scrolling_top_of_display to be NULL.
	 * If it is, recalculate_window_cursor_and_display_ip() will null deref.
	 * I don't know what the right thing to do is, so I choose to
	 * move the scrolling display to the bottom (ie, /unclear).
	 * This seems the least worst hack.
	 */
	if (!w->scrolling_top_of_display)
		unclear_window(w);

	recalculate_window_cursor_and_display_ip(w);
	if (w->scrolling_distance_from_display_ip >= w->display_lines)
		unclear_window(w);
	else if (w->scrolling_distance_from_display_ip <= w->display_lines && w->scrolladj)
		unclear_window(w);
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
static void 	my_goto_window (Screen *s, int which)
{
	Window	*tmp;
	int	i;

	if (!s || which == 0)
		return;

	if ((which < 0) || (which > s->visible_windows))
	{
		say("GOTO: Illegal value");
		return;
	}
	tmp = s->_window_list;
	for (i = 1; i < which; i++)
		tmp = tmp->_next;

	set_screens_current_window(s, tmp->refnum);
	make_window_current_by_refnum(tmp->refnum);
}

/*
 * hide_window: sets the given window to invisible and recalculates remaing
 * windows to fill the entire screen 
 */
static void 	hide_window (Window *window)
{
	if (!window->screen)
	{
		if (window->name)
			say("Window %s is already hidden", window->name);
		else
			say("Window %d is already hidden", window->user_refnum);
		return;
	}

	if (!window->swappable)
	{
		if (window->name)
			say("Window %s can't be hidden", window->name);
		else
			say("Window %d can't be hidden", window->user_refnum);
		return;
	}

	if (window->screen->visible_windows - 
			count_fixed_windows(window->screen) <= 1)
	{
		say("You can't hide the last window.");
		return;
	}

	remove_window_from_screen(window, 1, 1);
}

/*
 * swap_last_window:  This swaps the current window with the last window
 * that was hidden.
 * This is a keybinding.
 */
BUILT_IN_KEYBINDING(swap_last_window)
{
	if (!invisible_list || !current_window->screen)
		return;

	swap_window(current_window, invisible_list);
	update_all_windows();
}

/*
 * next_window: This switches the current window to the next visible window 
 * This is a keybinding.
 */
BUILT_IN_KEYBINDING(next_window)
{
	int	refnum;

	if (!last_input_screen)
		return;
	if (last_input_screen->visible_windows == 1)
		return;

	refnum = get_next_window(last_input_screen->input_window);
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

	if (!last_input_screen)
		return;
	if (last_input_screen->visible_windows == 1)
		return;

	refnum = get_previous_window(last_input_screen->input_window);
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
static void 	show_window (Window *window)
{
	Screen *s;
	int	refnum;

	if (!window->swappable)
	{
		if (window->name)
			say("Window %s can't be made visible", window->name);
		else
			say("Window %d can't be made visible", window->user_refnum);
		return;
	}

	if (!window->screen)
	{
		Screen *s;

		remove_from_invisible_list(window);
		if (!(s = current_window->screen))
			s = last_input_screen; /* What the hey */
		if (!add_to_window_list(s, window))
		{
			/* Ooops. this is an error. ;-) */
			add_to_invisible_list(window->refnum);
			return;
		}
	}

	s = window->screen;
	refnum = window->refnum;
	make_window_current_by_refnum(refnum);
	set_screens_current_window(s, refnum);
}




/* * * * * * * * * * * * * GETTING WINDOWS AND WINDOW INFORMATION * * * * */
/*
 * get_window_by_desc: Given either a refnum or a name, find that window
 */
static Window *	get_window_by_desc (const char *stuff)
{
	Window	*w = NULL;	/* bleh */

	w = NULL;
	while (traverse_all_windows(&w))
	{
		if (w->uuid && !my_stricmp(w->uuid, stuff))
			return w;
	}

	w = NULL;
	while (traverse_all_windows(&w))
	{
		if (w->name && !my_stricmp(w->name, stuff))
			return w;
	}

	if (is_number(stuff) && (w = get_window_by_refnum_direct(my_atol(stuff))))
		return w;

	return NULL;
}

/*
 * get_window_by_uuid: Given either a refnum or a name, find that window
 */
Window *get_window_by_uuid (const char *uuid)
{
	Window	*w = NULL;	/* bleh */

	while (traverse_all_windows(&w))
	{
		if (w->uuid && !my_stricmp(w->uuid, uuid))
			return w;
	}

	return NULL;
}

#if 0
/*
 * get_window_by_refnum: Given a reference number to a window, this returns a
 * pointer to that window if a window exists with that refnum, null is
 * returned otherwise.  The "safe" way to reference a window is throught the
 * refnum, since a window might be delete behind your back and and Window
 * pointers might become invalid.
 */
Window *get_window_by_refnum (int refnum)
{
	Window	*tmp = NULL;

	if (refnum == 0)
		return current_window;

	while (traverse_all_windows(&tmp))
	{
		if (tmp->user_refnum == refnum)
			return tmp;
		if (tmp->refnum == refnum)
			return tmp;
	}

	return NULL;
}
#endif

Window *get_window_by_refnum_direct (int refnum)
{
	if (refnum == 0)
		return current_window;

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

	if (!w || !w->screen)
		last = new_w = w = current_window;

	do
	{
		if (new_w->_next)
			new_w = new_w->_next;
		else
			new_w = w->screen->_window_list;
	}
	while (new_w && new_w->skip && new_w != last);

	return new_w->refnum;
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

	if (!w || !w->screen)
		last = new_w = w = current_window;

	do
	{
		if (new_w->_prev)
			new_w = new_w->_prev;
		else
			new_w = w->screen->_window_list_end;
	}
	while (new_w->skip && new_w != last);

	return new_w->refnum;
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
Screen *	get_window_screen (int refnum)
{
	Window *tmp;

	if (!(tmp = get_window_by_refnum_direct(refnum)))
		tmp = current_window;
	return tmp->screen;
}

int	get_window_refnum (int refnum)
{
	Window *tmp;

	if (!(tmp = get_window_by_refnum_direct(refnum)))
		tmp = current_window;
	return tmp->refnum;
}

int	get_window_user_refnum (int refnum)
{
	Window *tmp;

	if (!(tmp = get_window_by_refnum_direct(refnum)))
		tmp = current_window;
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

#if 0
/* * * * * * * * * * * * * INPUT PROMPT * * * * * * * * * * * * * * */
/*
 * set_window_prompt: changes the prompt for the given window.  A window
 * prompt will be used as the target in place of the query user or current
 * channel if it is set 
 */
void 	set_window_prompt (int refnum, const char *prompt)
{
	Window	*tmp;

	if (!(tmp = get_window_by_refnum_direct(refnum)))
		tmp = current_window;
	malloc_strcpy(&tmp->prompt, prompt);
	update_input(NULL, UPDATE_ALL);
}

/* get_window_prompt: returns the prompt for the given window refnum */
const char 	*get_window_prompt (int refnum)
{
	Window	*tmp;

	if (!(tmp = get_window_by_refnum_direct(refnum)))
		tmp = current_window;

	return tmp->prompt ? tmp->prompt : empty_string;
}
#endif

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
		if (last_input_screen->input_window < 0)
			return NULL;

	if ((cc = get_window_equery(refnum)))
		return cc;
	if ((cc = get_window_echannel(refnum)))
		return cc;
	return NULL;
}

const char *	get_window_equery (int refnum)
{
	WNickList *nick;
	Window *win;

	if ((win = get_window_by_refnum_direct(refnum)) == NULL)
		return NULL;
	for (nick = win->nicks; nick; nick = nick->next)
	{
		if (nick->counter == win->query_counter)
			return nick->nick;
	}

	return NULL;
}

BUILT_IN_KEYBINDING(switch_query)
{
        int     lowcount;
	int	highcount = -1;
        WNickList *winner = NULL,
		  *nick;
	Window  *win;
	const char *	old_query;

	win = get_window_by_refnum_direct(0);
	lowcount = win->query_counter;

	old_query = get_window_equery(0);
	for (nick = win->nicks; nick; nick = nick->next)
	{
		if (nick->counter > highcount)
			highcount = nick->counter;
		if (nick->counter < lowcount)
		{
			lowcount = nick->counter;
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
	winner->counter = current_query_counter++;
	win->query_counter = winner->counter;
	window_statusbar_needs_update(win->refnum);

	/* Tell the user */
	do_hook(SWITCH_QUERY_LIST, "%d %s %s",
		win->user_refnum, old_query, winner->nick);
}

static void	recheck_queries (int window_)
{
	Window *win = get_window_by_refnum_direct(window_);
	WNickList *nick;

	if (!win)
		return;

	win->query_counter = 0;

	/* Find the winner and reset query_counter */
	for (nick = win->nicks; nick; nick = nick->next)
	{
		if (nick->counter > win->query_counter)
			win->query_counter = nick->counter;
	}

	window_statusbar_needs_update(win->refnum);
}

/*
 * check_window_target -- is this target owned by this window?
 */
static	int	check_window_target (int window_, int server, const char *nick)
{
	Window *	w = get_window_by_refnum_direct(window_);

	if (get_window_server(window_) != server)
		return 0;

	if (EXISTS_IN_LIST_(w->nicks, nick, !USE_WILDCARDS))
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
	WNickList *	item;

	if (get_window_server(window_) != server)
		return 0;

	if (REMOVE_FROM_LIST_(item, &w->nicks, nick))
	{
		int	l;

		l = message_setall(w->refnum, get_who_from(), get_who_level());
		say("Removed %s from window target list", item->nick);
		pop_message_from(l);

		new_free(&item->nick);
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
	WNickList *	new_w;

	if (!(w = get_window_by_refnum_direct(window_)))
		return 0;

	if (get_window_server(window_) != server)
		return 0;

	/*
	 * Do I already own this target?  If not, go looking for it elsewhere
	 */
	FIND_IN_LIST_(new_w, w->nicks, target, !USE_WILDCARDS);
	if (!new_w)
	{
		int	need_create;

		if ((other_window = get_window_for_target(server, target)))
		{
			if (other_window == window_)
				need_create = 0;	/* XXX I just checked for this! */
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

			new_w = (WNickList *)new_malloc(sizeof(WNickList));
			new_w->nick = malloc_strdup(target);
			ADD_TO_LIST_(&w->nicks, new_w);
		}
	}

	if (as_current)
	{
		new_w->counter = current_query_counter++;
		w->query_counter = new_w->counter;
		recheck_queries(window_);
	}
	else
		new_w->counter = 0;

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
        Window *tmp = NULL;
	WNickList *next;

        while (traverse_all_windows(&tmp))
        {
                if (tmp->server != server)
                        continue;

		while (tmp->waiting_chans)
		{
			next = tmp->waiting_chans->next;
			new_free(&tmp->waiting_chans->nick);
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
	WNickList *next;

	if (!(tmp = get_window_by_refnum_direct(refnum)))
		return;

	while (tmp->waiting_chans)
	{
		next = tmp->waiting_chans->next;
		new_free(&tmp->waiting_chans->nick);
		new_free((char **)&tmp->waiting_chans);
		tmp->waiting_chans =  next;
	}
}

static	int	add_waiting_channel (Window *win, const char *chan)
{
	Window *w = NULL;
	WNickList *tmp;

	while (traverse_all_windows(&w))
	{
	    if (w == win || w->server != win->server)
		continue;

	    if (REMOVE_FROM_LIST_(tmp, &w->waiting_chans, chan))
	    {
		new_free(&tmp->nick);
		new_free((char **)&tmp);
	    }
	}

	if (EXISTS_IN_LIST_(win->waiting_chans, chan, !USE_WILDCARDS))
		return -1;		/* Already present. */

	tmp = (WNickList *)new_malloc(sizeof(WNickList));
	tmp->nick = malloc_strdup(chan);
	ADD_TO_LIST_(&win->waiting_chans, tmp);
	return 0;			/* Added */
}

int	claim_waiting_channel (const char *chan, int servref)
{
	Window *w = NULL;
	WNickList *tmp;
	int	retval = -1;

	/* Do a full traversal, just to make sure no channels stay behind */
	while (traverse_all_windows(&w))
	{
	    if (w->server != servref)
		continue;

	    if (REMOVE_FROM_LIST_(tmp, &w->waiting_chans, chan))
	    {
		new_free(&tmp->nick);
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
		tmp = current_window;
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
	Window *tmp = NULL;
	while (traverse_all_windows(&tmp))
	{
	    if (tmp->server == old_server)
		window_change_server(tmp, new_server);
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
	Window	*win, *tmp;
	int	i, claimed;

	if (!(win = get_window_by_refnum_direct(refnum)))
		return -1;

	/* Test each level in the reclaimable bitmask */
	for (i = 1; BIT_VALID(i); i++)
	{
	    /* If this level isn't reclaimable, skip it */
	    if (!mask_isset(&mask, i))
		continue;

	    /* Now look for any window (including us) that already claims it */
	    tmp = NULL;
	    claimed = 0;
	    while (traverse_all_windows(&tmp))
	    {
		if (mask_isset(&tmp->window_mask, i))
		    claimed = 1;
	    }

	    /* If no window claims it (including us), then we will. */
	    if (!claimed)
		mask_set(&tmp->window_mask, i);
	}

	revamp_window_masks(win);
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
static void 	revamp_window_masks (Window *window)
{
	Window	*tmp = NULL;
	int	i;

	for (i = 1; BIT_VALID(i); i++)
	{
	    if (!mask_isset(&window->window_mask, i))
		continue;

	    tmp = NULL;
	    while (traverse_all_windows(&tmp))
	    {
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

#if 0
	who_from = who;
	who_level = level;
	to_window = get_window_by_refnum_direct(refnum);
#endif
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
static void 	clear_window (Window *window)
{
	if (dumb_mode)
		return;

	debuglog("clearing window: clearing window %d", window->user_refnum);
	window->scrolling_top_of_display = window->display_ip;
	if (window->notified)
	{
		window->notified = 0;
		update_all_status();
	}
	window->current_activity = 0;
	recalculate_window_cursor_and_display_ip(window);

	window_body_needs_redraw(window->refnum);
	window_statusbar_needs_redraw(window->refnum);
}

void 	clear_all_windows (int visible, int hidden, int unhold)
{
	Window *tmp = NULL;

	while (traverse_all_windows(&tmp))
	{
		if (visible && !hidden && !tmp->screen)
			continue;
		if (!visible && hidden && tmp->screen)
			continue;

		clear_window(tmp);
	}
}

/*
 * clear_window_by_refnum: just like clear_window(), but it uses a refnum. If
 * the refnum is invalid, the current window is cleared. 
 */
void 	clear_window_by_refnum (int refnum, int unhold)
{
	Window	*tmp;

	if (!(tmp = get_window_by_refnum_direct(refnum)))
		tmp = current_window;
	clear_window(tmp);
}

static void	unclear_window (Window *window)
{
	int i;

	if (dumb_mode)
		return;

	window->scrolling_top_of_display = window->display_ip;
	for (i = 0; i < window->display_lines; i++)
	{
		if (window->scrolling_top_of_display == window->top_of_scrollback)
			break;
		window->scrolling_top_of_display = window->scrolling_top_of_display->prev;
	}

	recalculate_window_cursor_and_display_ip(window);
	window_body_needs_redraw(window->refnum);
	window_statusbar_needs_redraw(window->refnum);
}

void	unclear_all_windows (int visible, int hidden, int unhold)
{
	Window *tmp = NULL;

	while (traverse_all_windows(&tmp))
	{
		if (visible && !hidden && !tmp->screen)
			continue;
		if (!visible && hidden && tmp->screen)
			continue;

		unclear_window(tmp);
	}
}

void	unclear_window_by_refnum (int refnum, int unhold)
{
	Window *tmp;

	if (!(tmp = get_window_by_refnum_direct(refnum)))
		tmp = current_window;
	unclear_window(tmp);
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
	recalculate_window_cursor_and_display_ip(w);
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

	else if (size > current_window->display_lines)
	{
		say("Maximum lines that may be scrolled is %d [%d]", 
			current_window->display_lines, size);
		v->integer = current_window->display_lines;
	}
}





/* * * * * * * * * UNSORTED * * * * * * * */
int	lookup_window (const char *desc)
{
        Window  *w = get_window_by_desc(desc);

	if (w)
		return w->refnum;
	else
		return -1;
}

int	lookup_any_visible_window (void)
{
	Window *win = NULL;

	while ((traverse_all_windows(&win)))
	{
		if (win->screen)
			return win->refnum;
	}
	return -1;
}

int	get_window_display_lines (int refnum)
{
	Window	*w = get_window_by_refnum_direct(refnum);

	if (w)
		return w->display_lines;
	else
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

int	get_window_priority (int refnum)
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
		return w->cursor;
	else
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
	Window *window = NULL;

	v = (VARIABLE *)stuff;
	size = v->integer;

        while (traverse_all_windows(&window))
        {
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
	Window	*tmp = NULL;

	if (name)
	{
		while (traverse_all_windows(&tmp))
		{
			if (tmp->name && (my_stricmp(tmp->name, name) == 0))
				return (0);
		}
	}
	return (1);
}

static char	*get_waiting_channels_by_window (Window *win)
{
	WNickList *nick = win->waiting_chans;
	char *stuff = NULL;
	size_t stuffclue = 0;

	for (; nick; nick = nick->next)
		malloc_strcat_wordlist_c(&stuff, space, nick->nick, &stuffclue);

	if (!stuff)
		return malloc_strdup(empty_string);
	else
		return stuff;
}

static char	*get_nicklist_by_window (Window *win)
{
	WNickList *nick = win->nicks;
	char *stuff = NULL;
	size_t stuffclue = 0;

	for (; nick; nick = nick->next)
		malloc_strcat_wordlist_c(&stuff, space, nick->nick, &stuffclue);

	if (!stuff)
		return malloc_strdup(empty_string);
	else
		return stuff;
}

#define WIN_FORM "%-4s %*.*s %*.*s %*.*s %-9.9s %-10.10s %s%s"
static void 	list_a_window (Window *window, int len)
{
	int	cnw = get_int_var(CHANNEL_NAME_WIDTH_VAR);
	const char *chan = get_window_echannel(window->refnum);
	const char *q = get_window_equery(window->refnum);

	if (cnw == 0)
		cnw = 12;	/* Whatever */

	say(WIN_FORM,           ltoa(window->user_refnum),
		      12, 12,   get_server_nickname(window->server),
		      len, len, window->name ? window->name : "<None>",
		      cnw, cnw, chan ? chan : "<None>",
		                q ? q : "<None>",
		                get_server_itsname(window->server),
		                mask_to_str(&window->window_mask),
		                window->screen ? empty_string : " Hidden");
}

int     get_window_geometry (int refnum, int *co, int *li)
{
        Window  *win = get_window_by_refnum_direct(refnum);

        if (!win || !win->screen)
                return -1;
        *co = win->screen->co;
        *li = win->screen->li;
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

/* below is stuff used for parsing of WINDOW command */


/*
 * get_window: this parses out any window (visible or not) and returns a
 * pointer to it 
 */
static Window *get_window (const char *name, char **args)
{
	char	*arg;
	Window	*win;

	if ((arg = next_arg(*args, args)))
	{
		if ((win = get_window_by_desc(arg)))
			return win;
		say("%s: No such window: %s", name, arg);
	}
	else
		say("%s: Please specify a window refnum or name", name);

	return NULL;
}

/*
 * get_invisible_window: parses out an invisible window by reference number.
 * Returns the pointer to the window, or null.  The args can also be "LAST"
 * indicating the top of the invisible window list (and thus the last window
 * made invisible) 
 */
static Window *get_invisible_window (const char *name, char **args)
{
	char	*arg;
	Window	*tmp;

	if ((arg = next_arg(*args, args)) != NULL)
	{
		if (my_strnicmp(arg, "LAST", strlen(arg)) == 0)
		{
			if (invisible_list == (Window *) 0)
				say("%s: There are no hidden windows", name);
			return (invisible_list);
		}
		if ((tmp = get_window(name, &arg)) != NULL)
		{
			if (!tmp->screen)
			{
				return (tmp);
			}
			else
			{
				if (tmp->name)
					say("%s: Window %s is not hidden!",
						name, tmp->name);
				else
					say("%s: Window %d is not hidden!",
						name, tmp->user_refnum);
			}
		}
	}
	else
		say("%s: Please specify a window refnum or LAST", name);
	return ((Window *) 0);
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
	Window *tmp;
	Window *window = get_window_by_refnum_direct(refnum);
	int	other_refnum;

	if (!args)
		return refnum;

	if (!(other_refnum = last_input_screen->last_window_refnum))
		other_refnum = last_input_screen->_window_list->refnum;

	make_window_current_by_refnum(other_refnum);

	if (get_window_screen(other_refnum))
		set_screens_current_window(get_window_screen(other_refnum), other_refnum);
	else
		swap_window(window, get_window_by_refnum_direct(other_refnum));

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

	if (window->screen)
		rebalance_windows(window->screen);
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
		add_waiting_channel(window, chan);
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
	Window *tmp;
	int	lastlog_count;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!args)
		return refnum;

	tmp = NULL;
	while (traverse_all_windows(&tmp))
	{
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

	clear_window(window);
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
	Window *	window = get_window_by_refnum_direct(refnum);

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

	kill_screen(window->screen);
	return current_window->refnum;
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

	kill_screen(window->screen);
	delete_window(window);
	return current_window->refnum;
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
	say("\tScreen: %p",	window->screen);
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
	    WNickList *tmp;
	    size_t clue = 0;

	    c = NULL;
	    for (tmp = window->waiting_chans; tmp; tmp = tmp->next)
		malloc_strcat_word_c(&c, space, tmp->nick, DWORD_NO, &clue);

	    say("\tWaiting channels list: %s", c);
	    new_free(&c);
	}

	chan = get_window_equery(window->refnum);
	say("\tQuery User: %s", chan ? chan : "<None>");

	if (window->nicks)
	{
	    WNickList *tmp;
	    size_t clue = 0;

	    c = NULL;
	    for (tmp = window->nicks; tmp; tmp = tmp->next)
		malloc_strcat_word_c(&c, space, tmp->nick, DWORD_NO, &clue);

	    say("\tName list: %s", c);
	    new_free(&c);
	}

#if 0
	say("\tPrompt: %s", 
				window->prompt ? 
				window->prompt : "<None>");
#endif
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
	window_change_server(window, NOSERV); /* XXX This shouldn't be set here. */
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
		if (window->screen)
			recalculate_windows(window->screen);
	}
	recalculate_window_positions(window->screen);
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
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	flush_scrollback_after(window);
	return refnum;
}

/*
 * /WINDOW FLUSH_SCROLLBACK
 */
WINDOWCMD(flush_scrollback)
{
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	flush_scrollback(window);
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
		my_goto_window(window->screen, value);
		from_server = get_window_server(0);
	}
	return current_window->refnum;
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
	Window *	window = get_window_by_refnum_direct(refnum);
	int	value;

	if (!window)
		return 0;
	if (!args)
		return refnum;

	if (get_number("GROW", args, &value))
		resize_window(RESIZE_REL, window, value);
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
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	hide_window(window);
	return current_window->refnum;
}

/*
 * /WINDOW HIDE_OTHERS
 * This directs the client to place *all* windows on the current screen,
 * except for the current window, onto the invisible list.
 */
WINDOWCMD(hide_others)
{
	Window *tmp, *next;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	/* There are no "others" to hide if the window isn't visible. */
	if (!window->screen)
		return window->refnum;

	for (tmp = window->screen->_window_list; tmp; tmp = next)
	{
		next = tmp->_next;
		if (tmp != window)
			hide_window(tmp);
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
		recalculate_window_cursor_and_display_ip(window);
		window_body_needs_redraw(window->refnum);
		window_statusbar_needs_update(window->refnum);
	}
	if (!hold_mode && window->holding_top_of_display)
	{
		window->holding_top_of_display = NULL;
		recalculate_window_cursor_and_display_ip(window);
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
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	if (!window->killable)
	{
		say("You cannot kill an unkillable window");
		return 0;
	}
	delete_window(window);
	return current_window->refnum;
}

/*
 * /WINDOW KILL_ALL_HIDDEN
 * This kills all of the hidden windows.  If the current window is hidden,
 * then the current window will probably change to another window.
 */
WINDOWCMD(kill_all_hidden)
{
	Window *tmp, *next;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	tmp = invisible_list;
	while (tmp)
	{
		next = tmp->_next;
		if (tmp->killable)
		{
		    if (tmp == window)
			window = NULL;
		    delete_window(tmp);
		}
		tmp = next;
	}

	if (!window)
		window = current_window;
	return window->refnum;
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
	Window *tmp, *next;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	if (window->screen)
		tmp = window->screen->_window_list;
	else
		tmp = invisible_list;

	while (tmp)
	{
		next = tmp->_next;
		if (tmp->killable)
		{
		    if (tmp != window)
			delete_window(tmp);
		}
		tmp = next;
	}

	return refnum;
}

WINDOWCMD(killable)
{
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
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
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	if (!window->killable)
	{
		say("You cannot KILLSWAP an unkillable window");
		return 0;
	}
	if (invisible_list)
	{
		swap_window(window, invisible_list);
		delete_window(window);
	}
	else
		say("There are no hidden windows!");

	return current_window->refnum;
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
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	set_screens_current_window(window->screen, 0);
	return current_window->refnum;
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
	    revamp_window_masks(window);
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
	Window	*tmp = NULL;
	int	len = 6;
	int	cnw = get_int_var(CHANNEL_NAME_WIDTH_VAR);
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!args)
		return refnum;

	if (cnw == 0)
		cnw = 12;	/* Whatever */

	tmp = NULL;
	while (traverse_all_windows(&tmp))
	{
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

	tmp = NULL;
	while (traverse_all_windows(&tmp))
		list_a_window(tmp, len);

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
	int	mangle;
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
	Window *tmp;
	Window *window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	if ((tmp = get_window("MERGE", args)))
	{
		int	oldref, newref;

		oldref = window->refnum;
		newref = tmp->refnum;

		if (get_window_server(oldref) != get_window_server(newref))
		{
			say("Cannot merge window %d into window %d - different server", 
				get_window_user_refnum(oldref),
				get_window_user_refnum(newref));
			return 0;
		}

		move_all_lastlog(oldref, newref);
		channels_merge_windows(oldref, newref);
		logfiles_merge_windows(oldref, newref);
		timers_merge_windows(oldref, newref);

		while (window->nicks)
		{
			WNickList *h;

			h = window->nicks;
			window->nicks = h->next;

			ADD_TO_LIST_(&tmp->nicks, h);
		}

		windowcmd_kill(window->refnum, args);
		make_window_current_by_refnum(tmp->refnum);
		return tmp->refnum;
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
	Window *	window = get_window_by_refnum_direct(refnum);
	int	value;

	if (!window)
		return 0;
	if (!args)
		return refnum;

	if (get_number("MOVE", args, &value))
		move_window(window, value);
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
	Window *	window = get_window_by_refnum_direct(refnum);
	int	value;

	if (!window)
		return 0;
	if (!args)
		return refnum;

	if (get_number("MOVE_TO", args, &value))
		move_window_to(window, value);
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
	if ((new_refnum = new_window(window->screen)) < 0)
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
	new_window(NULL);
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

	if (!window)
		return 0;
	smallest = window;
	for (tmp = invisible_list; tmp; tmp = tmp->_next)
	{
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

	swap_window(window, next);
	return current_window->refnum;
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
	WindowStack *	tmp;
	Window *	win = NULL;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	if (!window->screen)
	{
		say("Cannot pop the window stack from a hidden window");
		return refnum;
	}

	while (window->screen->window_stack)
	{
		stack_refnum = window->screen->window_stack->refnum;
		tmp = window->screen->window_stack->next;
		new_free((char **)&window->screen->window_stack);
		window->screen->window_stack = tmp;

		if (!(win = get_window_by_refnum_direct(stack_refnum)))
			continue;

		if (win->screen)
			set_screens_current_window(win->screen, win->refnum);
		else
			show_window(win);
	}

	if (!window->screen->window_stack && !win)
		say("The window stack is empty!");

	return win->refnum;
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

	if (!window)
		return 0;

	/* It is ok for 'args' to be NULL -- SWAP_PREVIOUS_WINDOW uses this. */

	largest = window;
	for (tmp = invisible_list; tmp; tmp = tmp->_next)
	{
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

	swap_window(window, previous);
	return previous->refnum;
}

#if 0
WINDOWCMD(prompt)
{
	char *		arg;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	if ((arg = next_arg(*args, args)))
	{
		malloc_strcpy(&window->prompt, arg);
		window_statusbar_needs_update(window->refnum);
	}
	else
		say("You must specify a prompt for the window!");

	return refnum;
}
#endif

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

	if (!window->screen)
	{
		say("Cannot push a hidden window onto the window stack");
		return refnum;
	}

	new_ws = (WindowStack *) new_malloc(sizeof(WindowStack));
	new_ws->refnum = window->refnum;
	new_ws->next = window->screen->window_stack;
	window->screen->window_stack = new_ws;
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
			Window *w = NULL;
			Window *anybody = NULL;

			/* Go hunt for the owner. */
			while (traverse_all_windows(&w))
			{
			    if (w->server != from_server)
				continue;
			    if (EXISTS_IN_LIST_(window->waiting_chans, chan, !USE_WILDCARDS))
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

			add_waiting_channel(owner, chan);
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
	Window *tmp;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	if (!(tmp = get_window("REFNUM", args)))
		return 0;

	window = tmp;
	make_window_current_by_refnum(tmp->refnum);
	/* XXX This is dangerous -- 'make_window_current' might nuke 'tmp' */
	if (tmp->screen)
	{
		set_screens_current_window(tmp->screen, tmp->refnum);
		window = tmp;
	}
	return window->refnum;
}

WINDOWCMD(refnum_or_swap)
{
	Window  *tmp;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	if (!(tmp = get_window("REFNUM_OR_SWAP", args)))
		return 0;

	if (tmp->screen)
	{
		make_window_current_by_refnum(tmp->refnum);
		/* XXX This is dangerous -- 'make_window_current' might nuke 'tmp' */
		set_screens_current_window(tmp->screen, tmp->refnum);
	}
	else
		swap_window(window, tmp);

	return tmp->refnum;
}

WINDOWCMD(refresh)
{
	Window *	window = get_window_by_refnum_direct(refnum);

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
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!args)
		return refnum;

	if (!get_boolean("SCRATCH", args, &scratch))
		return 0;

	return refnum;
}

/* XXX - Need to come back and fix this... */
WINDOWCMD(scroll)
{
	short 	scroll = 0;
	Window *	window = get_window_by_refnum_direct(refnum);

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
			new_value = current_window->display_lines;
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
				window_scrollback_backward(window);
			else
				window_scrollback_backwards_lines(window, val);
		}
	}
	else
		window_scrollback_backward(window);

	return refnum;
}

WINDOWCMD(scroll_end)
{
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	window_scrollback_end(window);
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
				window_scrollback_forward(window);
			else
				window_scrollback_forwards_lines(window, val);
		}
	}
	else
		window_scrollback_forward(window);

	return refnum;
}

WINDOWCMD(scroll_start)
{
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	window_scrollback_start(window);
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
		window_scrollback_to_string(window, last_regex);
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
		window_scrollforward_to_string(window, last_regex);
	else
		say("Need to know what to search for");

	return refnum;
}

static	int	last_scroll_seconds_interval = 0;

WINDOWCMD(scroll_seconds)
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

	if (!window)
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
		if (window->server != i)
		{
			/*
			 * Lose our channels
			 */
			destroy_window_waiting_channels(window->refnum);
			reassign_window_channels(window->refnum);

			/*
			 * Associate ourselves with the new server.
			 */
			window_change_server(window, i);

			if (status > SERVER_RECONNECT && status < SERVER_EOF)
			{
			    if (old_server_lastlog_mask) {
				renormalize_window_levels(window->refnum, 
						*old_server_lastlog_mask);
				revamp_window_masks(window);
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
	Window *tmp;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	if ((tmp = get_window("SHOW", args)))
	{
		show_window(tmp);
		window = current_window;
	}
	return window->refnum;
}

WINDOWCMD(show_all)
{
	int	governor = 0;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	while (invisible_list)
	{
		show_window(invisible_list);
		/* If for some reason we get stuck in a loop, bail. */
		if (governor++ > 100)
			return refnum;
	}

	return refnum;
}

WINDOWCMD(shrink)
{
	Window *	window = get_window_by_refnum_direct(refnum);
	int	value;

	if (!window)
		return 0;
	if (!args)
		return refnum;

	if (get_number("SHRINK", args, &value))
		resize_window(RESIZE_REL, window, -value);

	return refnum;
}

WINDOWCMD(size)
{
	char *		ptr = *args;
	int		number;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	number = parse_number(args);
	if (ptr == *args) 
		say("Window size is %d", window->display_lines);
	else
		resize_window(RESIZE_ABS, window, number);

	return refnum;
}

/*
 * This lists the windows that are on the stack, cleaning up any
 * bogus entries on the way.
 */
WINDOWCMD(stack)
{
	WindowStack *	tmp;
	Window *	win = NULL;
	size_t		len = 4;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window || !window->screen)
		return 0;
	if (!args)
		return refnum;

	while (traverse_all_windows2(&refnum))
	{
		const char *n = get_window_name(refnum);
		if (n && strlen(n) > len)
			len = strlen(n);
	}

	say("Window stack:");
	for (tmp = window->screen->window_stack; tmp; tmp = tmp->next)
	{
		if ((win = get_window_by_refnum_direct(tmp->refnum)) != NULL)
			list_a_window(win, len);
	}

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
	Window *	tmp;
	Window *	window = get_window_by_refnum_direct(refnum);

	if (!window)
		return 0;
	if (!args)
		return refnum;

	if ((tmp = get_invisible_window("SWAP", args)))
		swap_window(window, tmp);

	return current_window->refnum;
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
		if (window->screen)
			recalculate_windows(window->screen);
	}
	if (window->top < window->toplines_showing)
	{
		window->top = window->toplines_showing;
		if (window->screen)
			recalculate_windows(window->screen);
	}
	recalculate_window_positions(window->screen);
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

	unclear_window(window);
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
	Mask		mask;
	char *		rejects = NULL;
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
#if 0
	{ "PROMPT",		windowcmd_prompt 		},
#endif
	{ "PUSH",		windowcmd_push 			},
	{ "QUERY",		windowcmd_query			},
	{ "REBUILD_SCROLLBACK",	windowcmd_rebuild_scrollback 	},
	{ "REFNUM",		windowcmd_refnum 		},
	{ "REFNUM_OR_SWAP",	windowcmd_refnum_or_swap	},
	{ "REFRESH",		windowcmd_refresh		},
	{ "REJOIN",		windowcmd_rejoin		},
	{ "REMOVE",		windowcmd_remove 		},
	{ "SCRATCH",		windowcmd_scratch		},
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
#if 0
	Window *window;
#endif
	int	refnum;
	int	old_status_update, 
		old_from_server;
	int	old_current_window;
	int	window;
	int	l;
	char *	original_args = NULL;

	old_from_server = from_server;
	old_current_window = current_window->refnum;
 	old_status_update = permit_status_update(0); 
	refnum = current_window->refnum;

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

		l = message_setall(refnum > 0 ? refnum : (unsigned)-1, 
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
					refnum < 1 ? (int)get_window_user_refnum(refnum) : -1,
					arg);
				break;
			}
		}

		if (!options[i].func)
		{
#if 0
			Window *s_window;
#endif
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
	if (current_window && (int)current_window->refnum != old_current_window)
		from_server = current_window->server;
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
static void	update_scrollback_indicator (Window *w)
{
}

/*
 * remove_scrollback_indicator removes the indicator from the scrollback, 
 * making it available for placement somewhere else.  This does the whole
 * shebang: finding it, removing it, and cleansing it.
 */
static void	remove_scrollback_indicator (Window *w)
{
}

/*
 * window_indicator_is_visible returns 1 if the window's indicator is in
 * the scrollback and 0 if it is not in use.  It's important to call
 * cleanse_indicator() when the indicator is removed from scrollback or 
 * this function will break.
 */
static void	window_indicator_is_visible (Window *w)
{
}

/*
 * cleanse_indicator clears the values in the indicator and makes it available
 * for reuse.  If you haven't unlinked it from the scrollback it does that 
 * for you.  It's important to cleanse the indicator because it does things
 * that are used by window_indicator_is_visible().
 */
static void	cleanse_indicator (Window *w)
{
}

/*
 * indicator_needs_update tells you when you do a scrollback whether the 
 * indicator needs to be moved further down the scrollback or not.  If the
 * indicator is not being used or if it is above your current view, then it
 * does need to be moved down.  Otherwise it does not need to be moved.
 */ 
static void	indicator_needs_update (Window *w)
{
}

/*
 * go_back_to_indicator will return the scrollback view to where the
 * indicator is and then do a full recalculation.
 */
static void	go_back_to_indicator (Window *w)
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
static Display *new_display_line (Display *prev, Window *w)
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

	stuff->count = w->display_counter++;
	stuff->unique_refnum = ++current_display_counter;
	stuff->prev = prev;
	stuff->next = NULL;
	stuff->when = time(NULL);
	return stuff;
}

/*
 * This function adds an item to the window's scrollback.  If the item
 * should be displayed on the screen, then 1 is returned.  If the item is
 * not to be displayed, then 0 is returned.  This function handles all
 * the hold_mode stuff.
 */
int 	add_to_scrollback (int window_, const unsigned char *str, intmax_t refnum)
{
	Window *window = get_window_by_refnum_direct(window_);

	/*
	 * If this is a scratch window, do that somewhere else
	 */
	if (window->change_line != -1)
		return change_line(window, str);

	return add_to_display(window, str, refnum);
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
static int	add_to_display (Window *window, const unsigned char *str, intmax_t refnum)
{
	int	scroll;
	int	i;

	/* 
	 * Add to the bottom of the scrollback buffer, and move the 
	 * bottom of scrollback (display_ip) after it. 
	 */
	window->display_ip->next = new_display_line(window->display_ip, window);
	malloc_strcpy(&window->display_ip->line, str);
	window->display_ip->linked_refnum = refnum;
	window->display_ip = window->display_ip->next;
	window->display_buffer_size++;

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
		return 0;		/* XXX Do soemthing better, someday */

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
static int	flush_scrollback (Window *w)
{
	Display *holder, *curr_line;

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
        w->display_counter = 1;

	/* Reconstitute a new scrollback buffer */
        w->top_of_scrollback = new_display_line(NULL, w);
        w->top_of_scrollback->line = NULL;
        w->top_of_scrollback->next = NULL;
        w->display_buffer_size = 1;
        w->display_ip = w->top_of_scrollback;
        w->scrolling_top_of_display = w->top_of_scrollback;

	/* Delete the old scrollback */
	/* XXXX - this should use delete_display_line! */ 
	while ((curr_line = holder))
	{
		holder = curr_line->next;
		new_free(&curr_line->line);
		new_free((char **)&curr_line);
	}

	/* Recalculate and redraw the window. */
	recalculate_window_cursor_and_display_ip(w);
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
static int	flush_scrollback_after (Window *window)
{
	Display *curr_line, *next_line;
	int	count;

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
	recalculate_window_cursor_and_display_ip(window);
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
static void	window_scrollback_backwards (Window *window, int skip_lines, int abort_if_not_found, int (*test)(Window *, Display *, void *), void *meta)
{
	Display *new_top;
	int	new_lines;

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
		else if ((*test)(window, new_top, meta))
			break;

		new_top = new_top->prev;
	}

	window->scrollback_top_of_display = new_top;
	recalculate_window_cursor_and_display_ip(window);
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
static void	window_scrollback_forwards (Window *window, int skip_lines, int abort_if_not_found, int (*test)(Window *, Display *, void *), void *meta)
{
	Display *new_top;
	int	unholding;
	int	new_lines = 0;

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
		else if ((*test)(window, new_top, meta))
			break;

		new_top = new_top->next;
	}

	/* Set the top of scrollback to wherever we landed */
	if (!unholding)
		window->scrollback_top_of_display = new_top;
	else
		window->holding_top_of_display = new_top;

	recalculate_window_cursor_and_display_ip(window);
	window_body_needs_redraw(window->refnum);
	window_statusbar_needs_update(window->refnum);
	return;
}

/* * * */
/*
 * A scrollback tester that counts off the number of lines to move.
 * Returns -1 when the count has been reached.
 */
static	int	window_scroll_lines_tester (Window *window, Display *line, void *meta)
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
static void 	window_scrollback_backwards_lines (Window *window, int my_lines)
{
	/* Do not skip line, Move even if not found, don't leave blank space */
	window_scrollback_backwards(window, 0, 0,
			window_scroll_lines_tester, 
			(void *)&my_lines);
}

/* Scroll down "my_lines" on "window".  Will stop if it reaches bottom */
static void 	window_scrollback_forwards_lines (Window *window, int my_lines)
{
	/* Do not skip line, Move even if not found, don't leave blank space */
	window_scrollback_forwards(window, 0, 0,
			window_scroll_lines_tester, 
			(void *)&my_lines);
}

/* * * */
/*
 * A scrollback tester that looks for a line that matches a regex.
 * Returns -1 when the line is found, and 0 if this line does not match.
 */
static	int	window_scroll_regex_tester (Window *window, Display *line, void *meta)
{
	char *	denormal;

	denormal = normalized_string_to_plain_text(line->line);

	debuglog("window_scroll_regex_tester: window %d, display (ur %lld, cnt %lld, lr %lld, when %lld, txt %s",
			window->user_refnum, (long long)line->unique_refnum, 
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

static void 	window_scrollback_to_string (Window *window, regex_t *preg)
{
	/* Skip one line, Don't move if not found, don't leave blank space */
	window_scrollback_backwards(window, 1, 1,
			window_scroll_regex_tester, 
			(void *)preg);
}

static void 	window_scrollforward_to_string (Window *window, regex_t *preg)
{
	/* Skip one line, Don't move if not found, blank space is ok */
	window_scrollback_forwards(window, 1, 1,
			window_scroll_regex_tester, 
			(void *)preg);
}

/* * * */
/*
 * A scrollback tester that looks for the final line that is newer than the
 * given time.
 */
static	int	window_scroll_time_tester (Window *window, Display *line, void *meta)
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
static void	window_scrollback_start (Window *window)
{
	/* XXX Ok.  So maybe 999999 *is* a magic number. */
	window_scrollback_backwards_lines(window, 999999);
}

/*
 * Keybinding: SCROLL_END
 * Command: /WINDOW SCROLL_END
 * Please note that this doesn't turn hold_mode off, obviously! 
 */
static void	window_scrollback_end (Window *window)
{
	window_scrollback_forwards_lines(window, 999999);
}

/*
 * Keybinding: SCROLL_FORWARD
 * Command: /WINDOW SCROLL_FORWARD
 * Scrolls down the "default" amount (usually half a screenful) 
 */
static void	window_scrollback_forward (Window *window)
{
	int 	ratio = get_int_var(SCROLLBACK_RATIO_VAR);
	int	my_lines;

	if (ratio < 1) 
		ratio = 1;
	if (ratio > 100) 
		ratio = 100;

	if ((my_lines = window->display_lines * ratio / 100) < 1)
		my_lines = 1;
	window_scrollback_forwards_lines(window, my_lines);
}

/*
 * Keybinding: SCROLL_BACKWARD
 * Command: /WINDOW SCROLL_BACKWARD
 * Scrolls up the "default" amount (usually half a screenful) 
 */
static void	window_scrollback_backward (Window *window)
{
	int 	ratio = get_int_var(SCROLLBACK_RATIO_VAR);
	int	my_lines;

	if (ratio < 1) 
		ratio = 1;
	if (ratio > 100) 
		ratio = 100;

	if ((my_lines = window->display_lines * ratio / 100) < 1)
		my_lines = 1;
	window_scrollback_backwards_lines(window, my_lines);
}

/* These are the actual keybinding functions, they're just shims */
BUILT_IN_KEYBINDING(scrollback_forwards)
{
	window_scrollback_forward(current_window);
}

BUILT_IN_KEYBINDING(scrollback_backwards)
{
	window_scrollback_backward(current_window);
}

BUILT_IN_KEYBINDING(scrollback_end)
{
	window_scrollback_end(current_window);
}

BUILT_IN_KEYBINDING(scrollback_start)
{
	window_scrollback_start(current_window);
}


/******************* Hold Mode functionality *******************************/
/*
 * UNSTOP_ALL_WINDOWS does a /WINDOW HOLD_MODE OFF on all windows.
 */
BUILT_IN_KEYBINDING(unstop_all_windows)
{
	Window	*tmp = NULL;
	char	my_off[4];
	char *	ptr;

	while (traverse_all_windows(&tmp))
	{
		strlcpy(my_off, "OFF", sizeof(my_off));
		ptr = my_off;
		windowcmd_hold_mode(tmp->refnum, (char **)&ptr);
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
static void 	recalculate_window_cursor_and_display_ip (Window *window)
{
	Display *tmp;

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
static void 	set_screens_current_window (Screen *screen, int window)
{
	Window	*w;

	if (window == 0 || ((w = get_window_by_refnum_direct(window)) == NULL))
	{
		w = get_window_by_refnum_direct(screen->last_window_refnum);

		/* Cant use a window that is now on a different screen */
		/* Check check a window that doesnt exist, too! */
		if (w && w->screen != screen)
			w = NULL;
	}
	if (!w)
		w = screen->_window_list;
	if (!w)
		panic(1, "sccw: The screen has no windows.");

	if (w->deceased)
		panic(1, "sccw: This window is dead.");
	if (w->screen != screen)
		panic(1, "sccw: The window is not on that screen.");
	if (!screen)
		panic(1, "sccw: Cannot set the invisible screen's current window.");

	if (screen->input_window != (int)w->refnum)
	{
		if (screen->input_window > 0)
		{
			window_statusbar_needs_update(screen->input_window);
			screen->last_window_refnum = screen->input_window;
		}
		screen->input_window = w->refnum;
		window_statusbar_needs_update(screen->input_window);
	}
	if (current_window != w)
		make_window_current_by_refnum(w->refnum);

	w->priority = current_window_priority++;
}

void	make_window_current_by_refnum (int refnum)
{
	Window	*new_win;

	if (refnum == -1)
		return;

	if (refnum == 0)
		make_window_current(NULL);
	else if ((new_win = get_window_by_refnum_direct(refnum)))
		make_window_current(new_win);
	else
		say("Window [%d] doesnt exist any more.  Punting.", refnum);
}


/*
 * This is used to make the specified window the current window.  This
 * is preferable to directly doing the assign, because it can deal with
 * finding a current window if the old one has gone away.
 */
static void 	make_window_current (Window *window)
{
	Window *old_current_window = current_window;
	int	old_screen, old_window, old_window_user_refnum;
	int	new_screen, new_window, new_window_user_refnum;

	if (!window)
		current_window = get_window_by_refnum_direct(last_input_screen->input_window);
	else if (current_window != window)
		current_window = window;

	if (current_window == NULL)
		current_window = last_input_screen->_window_list;

	if (current_window == NULL)
		current_window = main_screen->_window_list;

	if (current_window == NULL)
		panic(1, "make_window_current(NULL) -- can't find another window");

	if (current_window->deceased)
		panic(1, "This window is dead and cannot be made current");

	if (current_window == old_current_window)
		return;

	if (!old_current_window)
		old_screen = old_window = -1;
	else if (!old_current_window->screen)
		old_screen = -1, old_window = old_current_window->refnum;
	else
		old_screen = old_current_window->screen->screennum,
		old_window = old_current_window->refnum;

	new_window = current_window->refnum;
	if (!current_window->screen)
		new_screen = -1;
	else
		new_screen = current_window->screen->screennum;

	old_window_user_refnum = get_window_user_refnum(old_window);
	new_window_user_refnum = get_window_user_refnum(new_window);

	do_hook(SWITCH_WINDOWS_LIST, "%d %d %d %d",
		old_screen, old_window_user_refnum,
		new_screen, new_window_user_refnum);
}

int	make_window_current_informally (int refnum)
{
	Window *w;

	if ((w = get_window_by_refnum_direct(refnum)))
	{
		current_window = w;
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
static int	change_line (Window *window, const unsigned char *str)
{
	Display *my_line;
	int 	cnt;
	int	chg_line;

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
		add_to_display(window, empty_string, -1);

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
	malloc_strcpy(&my_line->line, str);
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
	    if (!(w = get_window_by_desc(windesc)))
		RETURN_EMPTY;
	    RETURN_INT(w->user_refnum);
	} else if (!my_strnicmp(listc, "REFNUMS", len)) {
		w = NULL;
		while (traverse_all_windows(&w))
		    malloc_strcat_wordlist(&ret, space, ltoa(w->user_refnum));
		RETURN_MSTR(ret);
	} else if (!my_strnicmp(listc, "REFNUMS_BY_PRIORITY", len)) {
		w = NULL;
		while (traverse_all_windows_by_priority(&w))
		    malloc_strcat_wordlist(&ret, space, ltoa(w->user_refnum));
		RETURN_MSTR(ret);
	} else if (!my_strnicmp(listc, "REFNUMS_ON_SCREEN", len)) {
		Screen *s;

		GET_INT_ARG(refnum, input);
		if (!(w = get_window_by_refnum_direct(refnum)))
			RETURN_EMPTY;

		s = w->screen;
		w = NULL;
		while (traverse_all_windows_on_screen(&w, s))
		    malloc_strcat_wordlist(&ret, space, ltoa(w->user_refnum));
		RETURN_MSTR(ret);
	} else if (!my_strnicmp(listc, "NEW", len)) {
	    int new_refnum;

	    old_status_update = permit_status_update(0);
	    new_refnum = new_window(current_window->screen);
	    permit_status_update(old_status_update);
	    if (new_refnum > 0) {
	        make_window_current_by_refnum(new_refnum);
		RETURN_INT(get_window_user_refnum(new_refnum));
	    }
	    else
		RETURN_INT(-1);
	} else if (!my_strnicmp(listc, "NEW_HIDE", len)) {
	    int new_refnum;

	    if ((new_refnum = new_window(NULL)) > 0)
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
		RETURN_INT(w->screen ? 1 : 0);
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
#if 0
	    } else if (!my_strnicmp(listc, "PROMPT", len)) {
		RETURN_STR(w->prompt);
#endif
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
	    } else if (!my_strnicmp(listc, "WAITING_CHANNEL", len)) {
		RETURN_STR(get_waiting_channels_by_window(w));
	    } else if (!my_strnicmp(listc, "BIND_CHANNEL", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "QUERY_NICK", len)) {
		const char *cc = get_window_equery(w->refnum);
		RETURN_STR(cc);
	    } else if (!my_strnicmp(listc, "NICKLIST", len)) {
		RETURN_MSTR(get_nicklist_by_window(w));
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
		RETURN_INT(w->screen ? w->screen->screennum : -1);
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

static int	count_fixed_windows (Screen *s)
{
	int	count = 0;
	Window *w = NULL;

	while (traverse_all_windows_on_screen(&w, s))
		if (w->fixed_size && w->skip)
			count++;

	return count;
}

static void	window_change_server (Window * win, int server) 
{
	int oldserver;

	oldserver = win->server; 
	win->server = server;
	do_hook(WINDOW_SERVER_LIST, "%u %d %d", win->user_refnum, oldserver, server);
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


#if 0
void    help_topics_window (FILE *f)
{                                                                               
        int     x;
                                                                                
        for (x = 0; options[x].func; x++)
                fprintf(f, "window %s\n", options[x].command);
}
#endif
