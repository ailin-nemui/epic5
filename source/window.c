/* $EPIC: window.c,v 1.201 2008/11/27 03:44:11 jnelson Exp $ */
/*
 * window.c: Handles the organzation of the logical viewports (``windows'')
 * for irc.  This includes keeping track of what windows are open, where they
 * are, and what is on them.
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright © 1997, 2007 EPIC Software Labs.
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

static const char *onoff[] = { "OFF", "ON" };

/* Resize relatively or absolutely? */
#define RESIZE_REL 1
#define RESIZE_ABS 2

/* used by the update flag to determine what needs updating */
#define REDRAW_DISPLAY     1 << 0
#define UPDATE_STATUS      1 << 1
#define REDRAW_STATUS      1 << 2

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
	Window	*current_window = NULL;

/*
 * All of the hidden windows.  These windows are not on any screen, and
 * therefore are not visible.
 */
	Window	*invisible_list = (Window *) 0;

/*
 * This is used to note who the currently processed message was from.
 * Since each window has (through /window add) the ability to "grab" all
 * of the output from a given nickname, we store the nickname here and then
 * refer to it when anything is outputted.
 */
const	char	*who_from = (char *) 0;	

/*
 * This is the lastlog level that any output should be sent out at.  This
 * determines what window output ultimately ends up in.
 */
	int	who_level;

/*
 * This is set to 1 if output is to be dispatched normally.  This is set to
 * 0 if all output is to be suppressed (such as when the system wants to add
 * and alias and doesnt want to blab to the user, or when you use ^ to
 * suppress the output of a command.)
 */
	unsigned window_display = 1;

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


static 	void 	remove_from_invisible_list 	(Window *);
static 	void 	swap_window 			(Window *, Window *);
static	Window	*get_next_window  		(Window *);
static	Window	*get_previous_window 		(Window *);
static 	void 	revamp_window_masks 		(Window *);
static	void 	clear_window 			(Window *);
static	void	resize_window_display 		(Window *);
static 	Window *window_next 			(Window *, char **);
static 	Window *window_previous 		(Window *, char **);
static	void 	set_screens_current_window 	(Screen *, Window *);
static	void 	remove_window_from_screen 	(Window *window, int hide);
static 	Window *window_discon 			(Window *window, char **args);
static 	void	window_scrollback_start 	(Window *window);
static 	void	window_scrollback_end 		(Window *window);
static void	window_scrollback_backward 	(Window *window);
static void	window_scrollback_forward 	(Window *window);
static void	window_scrollback_backwards_lines (Window *window, int);
static void	window_scrollback_forwards_lines (Window *window, int);
static 	void 	window_scrollback_to_string 	(Window *window, regex_t *str);
static 	void 	window_scrollforward_to_string 	(Window *window, regex_t *str);
static	int	change_line 			(Window *, const unsigned char *);
static	int	add_to_display 			(Window *, const unsigned char *, intmax_t);
static	Display *new_display_line 		(Display *prev, Window *w);
static 	int	count_fixed_windows 		(Screen *s);
static	int	add_waiting_channel 		(Window *, const char *);
static 	void   	destroy_window_waiting_channels	(int);
static 	int	flush_scrollback_after		(Window *);
static 	int	flush_scrollback		(Window *);
static void	unclear_window (Window *window);
static	void	rebuild_scrollback (Window *w);
static	void	window_check_columns (Window *w);
static void	restore_window_positions (Window *w, intmax_t scrolling, intmax_t holding, intmax_t scrollback);
static void	save_window_positions (Window *w, intmax_t *scrolling, intmax_t *holding, intmax_t *scrollback);


/* * * * * * * * * * * CONSTRUCTOR AND DESTRUCTOR * * * * * * * * * * * */
/*
 * new_window: This creates a new window on the screen.  It does so by either
 * splitting the current window, or if it can't do that, it splits the
 * largest window.  The new window is added to the window list and made the
 * current window 
 */
Window	*new_window (Screen *screen)
{
	Window	*	new_w;
	Window	*	tmp = NULL;
	unsigned	new_refnum = 1;
	int		i;

	if (dumb_mode && current_window)
		return NULL;

	new_w = (Window *) new_malloc(sizeof(Window));

	/*
	 * STAGE 1 -- Ensuring all values are set to default values
	 */

	/* Meta stuff */
	tmp = NULL;
	while (traverse_all_windows(&tmp))
	{
		if (tmp->refnum == new_refnum)
		{
			new_refnum++;
			tmp = NULL;
		}
	}
	/* XXX refnum is changed here XXX */
	new_w->refnum = new_refnum;
	new_w->name = NULL;
	new_w->priority = -1;		/* Filled in later */

	/* Output rule stuff */
	if (current_window)
		new_w->server = current_window->server;
	else
		new_w->server = NOSERV;
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
	new_w->prompt = NULL;		/* Filled in later */
	for (i = 0; i < 3; i++)
	{
		new_w->status.line[i].raw = NULL;
		new_w->status.line[i].format = NULL;
		new_w->status.line[i].count = 0;
		new_w->status.line[i].result = NULL;
	}
	new_w->status.number = 1;
	new_w->status.special = NULL;
	rebuild_a_status(new_w);

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

	/* Window geometry stuff */
	new_w->my_columns = 0;			/* Filled in later? */
	new_w->display_lines = 1;		/* Filled in later */
	new_w->logical_size = 100;		/* XXX Implement this */
	new_w->fixed_size = 0;
	new_w->old_display_lines = 1;	/* Filled in later */
	new_w->indent = get_int_var(INDENT_VAR);

	/* Hold mode stuff */
	new_w->hold_interval = 10;

	/* LASTLOG stuff */
#if 0
	new_w->lastlog_oldest = NULL;
	new_w->lastlog_newest = NULL;
#endif
	new_w->lastlog_mask = real_lastlog_mask();
	new_w->lastlog_size = 0;
	new_w->lastlog_max = get_int_var(LASTLOG_VAR);

	/* LOGFILE stuff */
	new_w->log = 0;
	new_w->logfile = NULL;
	new_w->log_fp = NULL;

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
	new_w->screen = screen;
	new_w->next = new_w->prev = NULL;
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
		set_screens_current_window(screen, new_w);
	else
		add_to_invisible_list(new_w);

	/* Finally bootstrap the visible part of the window */
	resize_window_display(new_w);

	/*
	 * Offer it to the user.  I dont know if this will break stuff
	 * or not.
	 */
	do_hook(WINDOW_CREATE_LIST, "%d", new_w->refnum);

	return (new_w);
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
void 	delete_window (Window *window)
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
		say("You can't kill the last window!");
		return;
	    }
	}

	/* Let the script have a stab at this first. */
	do_hook(WINDOW_BEFOREKILL_LIST, "%d", window->refnum);

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
		if (window->screen->window_list != window ||
			 window->next != NULL ||
			 (window->screen->current_window && 
			    window->screen->current_window != window))
		{
			panic(1, "My screen says there is only one "
				"window on it, and I don't agree.");
		}
		else
		{
			window->deceased = 1;
			window->screen->window_list = NULL;
			window->screen->visible_windows = 0;
			window->screen->current_window = NULL;
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
		remove_window_from_screen(window, 0);
	else if (invisible_list)
	{
		window->swappable = 1;
		swap_window(window, NULL);
	}
	else
	{
		yell("I don't know how to kill window [%d]", window->refnum);
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
	{
		if (window == last_input_screen->current_window)
		{
		    if (window->screen != last_input_screen)
			panic(1, "I am not on that screen");
		    else
			make_window_current(last_input_screen->window_list);
		}
		else
			make_window_current(NULL);
	}
	if (window == current_window)
		panic(1, "window == current_window -- this is wrong.");

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
		strlcpy(buffer, ltoa(window->refnum), sizeof buffer);
	oldref = window->refnum;

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
			panic(1, "display_buffer_size is %d, should be 0", 
				window->display_buffer_size);
	}

	/* The lastlog... */
	window->lastlog_max = 0;
	trim_lastlog(window);

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
		while (screen && (!screen->alive || !screen->window_list))
			screen = screen->next;

		if (!screen && !invisible_list)
			return 0;
		else if (!screen)
			*ptr = invisible_list;
		else
			*ptr = screen->window_list;
	}

	/*
	 * As long as there is another window on this screen, keep going.
	 */
	else if ((*ptr)->next)
		*ptr = (*ptr)->next;

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
		while (ns && (!ns->alive || !ns->window_list))
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
			*ptr = ns->window_list;
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


/* * * * * * * * * * * * * * * * WINDOW LISTS * * * * * * * * * * * * * * * */

/*
 * Handle the client's list of invisible windows.
 */
static void 	remove_from_invisible_list (Window *window)
{
	Window *w;

	/* Purely a sanity check */
	for (w = invisible_list; w && w != window; w = w->next)
		;
	if (!w)
		panic(1, "This window is _not_ invisible");

	/*
	 * Unlink it from the list
	 */
	if (window->prev)
		window->prev->next = window->next;
	else
		invisible_list = window->next;
	if (window->next)
		window->next->prev = window->prev;
}

void 	add_to_invisible_list (Window *window)
{
	/*
	 * Because this blows away window->next, it is implicitly
	 * assumed that you have already removed the window from
	 * its screen.
	 */
	if ((window->next = invisible_list) != NULL)
		invisible_list->prev = window;

	invisible_list = window;
	window->prev = (Window *) 0;
	if (window->screen)
		window->my_columns = window->screen->co;
	else
		window->my_columns = current_term->TI_cols;	/* Whatever */
	window->screen = (Screen *) 0;
}


/*
 * add_to_window_list: This inserts the given window into the visible window
 * list (and thus adds it to the displayed windows on the screen).  The
 * window is added by splitting the current window.  If the current window is
 * too small, the next largest window is used.  The added window is returned
 * as the function value or null is returned if the window couldn't be added 
 */
Window *add_to_window_list (Screen *screen, Window *new_w)
{
	Window	*biggest = (Window *) 0,
		*tmp;

	if (screen == NULL)
		panic(1, "Cannot add window [%d] to NULL screen.", new_w->refnum);

	screen->visible_windows++;
	new_w->screen = screen;
	new_w->notified = 0;

	/*
	 * If this is the first window to go on the screen
	 */
	if (!screen->current_window)
	{
		screen->window_list_end = screen->window_list = new_w;
		if (dumb_mode)
		{
			new_w->display_lines = 24;
			set_screens_current_window(screen, new_w);
			return new_w;
		}
		recalculate_windows(screen);
	}

	/*
	 * This is not the first window on this screen.
	 */
	else
	{
		/* split current window, or find a better window to split */
		if ((screen->current_window->display_lines < 4) ||
				get_int_var(ALWAYS_SPLIT_BIGGEST_VAR))
		{
			int	size = 0;

			for (tmp = screen->window_list; tmp; tmp = tmp->next)
			{
				if (tmp->fixed_size)
					continue;
				if (tmp->display_lines > size)
				{
					size = tmp->display_lines;
					biggest = tmp;
				}
			}
			if (!biggest /* || size < 4 */)
			{
				say("Not enough room for another window!");
				screen->visible_windows--;
				return NULL;
			}
		}
		else
			biggest = screen->current_window;

		if ((new_w->prev = biggest->prev) != NULL)
			new_w->prev->next = new_w;
		else
			screen->window_list = new_w;

		new_w->next = biggest;
		biggest->prev = new_w;
		biggest->display_lines /= 2;
		/* XXX Manually resetting window's size?  Ugh */
		new_w->display_lines = biggest->display_lines;
		recalculate_windows(screen);
	}
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
static void 	remove_window_from_screen (Window *window, int hide)
{
	Screen *s;

	if (!((s = window->screen)))
		panic(1, "This window is not on a screen");

	/*
	 * We  used to go to greath lengths to figure out how to fill
	 * in the space vacated by this window.  Now we dont sweat that.
	 * we just blow away the window and then recalculate the entire
	 * screen.
	 */
	if (window->prev)
		window->prev->next = window->next;
	else
		s->window_list = window->next;

	if (window->next)
		window->next->prev = window->prev;
	else
		s->window_list_end = window->prev;

	if (!--s->visible_windows)
		return;

	if (hide)
		add_to_invisible_list(window);

	if (s->current_window == window)
		set_screens_current_window(s, NULL);

	if (s->last_window_refnum == window->refnum)
		s->last_window_refnum = s->current_window->refnum;

	if (s->current_window == window)
		make_window_current(last_input_screen->window_list);
	else
		make_window_current(NULL);

	recalculate_windows(s);
}


/* * * * * * * * * * * * SIZE AND LOCATION PRIMITIVES * * * * * * * * * * * */
/*
 * recalculate_window_positions: This runs through the window list and
 * re-adjusts the top and bottom fields of the windows according to their
 * current positions in the window list.  This doesn't change any sizes of
 * the windows 
 */
void	recalculate_window_positions (Screen *screen)
{
	Window	*w;
	short	top;

	if (!screen)
		return;		/* Window is hidden.  Dont bother */

	top = 0;
	for (w = screen->window_list; w; w = w->next)
	{
		top += w->toplines_showing;
		w->top = top;
		w->bottom = top + w->display_lines;
		top += w->display_lines + w->status.number;

		window_body_needs_redraw(w);
		window_statusbar_needs_redraw(w);
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
		for (window = invisible_list; window; window = window->next)
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
			say("Window %d is not swappable", v_window->refnum);
		return;
	}
	if (check_hidden && !window->swappable)
	{
		if (window->name)
			say("Window %s is not swappable", window->name);
		else
			say("Window %d is not swappable", window->refnum);
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

	if (v_window->screen->current_window == v_window)
	{
		v_window->screen->current_window = window;
		window->priority = current_window_priority++;
	}

	/*
	 * Put the window to be swapped into the screen list
	 */
	if ((window->prev = v_window->prev))
		window->prev->next = window;
	else
		window->screen->window_list = window;

	if ((window->next = v_window->next))
		window->next->prev = window;
	else
		window->screen->window_list_end = window;


	/*
	 * Hide the window to be swapped out
	 */
	if (!v_window->deceased)
		add_to_invisible_list(v_window);

	if (recalculate_everything)
		recalculate_windows(window->screen);
	recalculate_window_cursor_and_display_ip(window);
	resize_window_display(window);

	/* XXX Should I do this before, or after, for efficiency? */
	window_check_columns(window);

	/*
	 * And recalculate the window's positions.
	 */
	window_body_needs_redraw(window);
	window_statusbar_needs_redraw(window);
	window->notified = 0;

	/*
	 * Transfer current_window if the current window is being swapped out
	 */
	if (v_window == current_window)
		make_window_current(window);
}

/*
 * move_window: This moves a window offset positions in the window list. This
 * means, of course, that the window will move on the screen as well 
 */
static void 	move_window (Window *window, int offset)
{
	Window	*tmp,
		*last;
	int	win_pos,
		pos;

	if (offset == 0)
		return;
	last = (Window *) 0;
	if (!window->screen)
		return;		/* Whatever */

	for (win_pos = 0, tmp = window->screen->window_list; tmp;
	    tmp = tmp->next, win_pos++)
	{
		if (window == tmp)
			break;
		last = tmp;
	}

	if (!tmp)
		return;

	if (!last)
		window->screen->window_list = tmp->next;
	else
		last->next = tmp->next;

	if (tmp->next)
		tmp->next->prev = last;
	else
		window->screen->window_list_end = last;

	win_pos = (offset + win_pos) % window->screen->visible_windows;
	if (win_pos < 0)
		win_pos = window->screen->visible_windows + win_pos;

	last = NULL;
	for (pos = 0, tmp = window->screen->window_list;
			    pos != win_pos; tmp = tmp->next, pos++)
		last = tmp;

	if (!last)
		window->screen->window_list = window;
	else
		last->next = window;

	if (tmp)
		tmp->prev = window;
	else
		window->screen->window_list_end = window;

	window->prev = last;
	window->next = tmp;
	recalculate_window_positions(window->screen);
}

/*
 * move_window_to: This moves a given window to the Nth absolute position 
 * on the screen.  All the other windows move accordingly.
 */
static void 	move_window_to (Window *window, int offset)
{
	Window *w;
	Screen *s;
	int	i;

	if (offset <= 0)
		return;

	if (!(s = window->screen))
		return;		/* Whatever */

	if (s->visible_windows == 1)
		return;		/* Whatever */

	if (offset > s->visible_windows)
		offset = s->visible_windows;

	/* Unlink the window from the screen */
	if (window->prev)
		window->prev->next = window->next;
	else
		s->window_list = window->next;

	if (window->next)
		window->next->prev = window->prev;
	else
		s->window_list_end = window->prev;

	/* Now figure out where it goes */
	for (w = s->window_list, i = 1; i < offset; i++)
		w = w->next;

	/* Now relink it where it belongs */
	if (w)
	{
		if (w->prev)
		{
			w->prev->next = window;
			window->prev = w->prev;
		}
		else
		{
			s->window_list = window;
			window->prev = NULL;
		}
	}
	else
	{
		s->window_list_end->next = window;
		window->prev = s->window_list_end;
		s->window_list_end = window;
	}

	window->next = w;

	set_screens_current_window(s, window);
	make_window_current(window);
	recalculate_window_positions(s);
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
	int	after,
		window_size,
		other_size;

	if (!window)
		window = current_window;

	if (!window->screen)
	{
		say("You cannot change the size of hidden windows!");
		return;
	}

	if (how == RESIZE_ABS)
	{
		offset -= window->display_lines;
		how = RESIZE_REL;
	}

	after = 1;
	other = window;

	do
	{
		if (other->next)
			other = other->next;
		else
		{
			other = window->screen->window_list;
			after = 0;
		}

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

	/*
	 * Mark the window for redraw and store the new window size.
	 */
	window_body_needs_redraw(window);
	window_statusbar_needs_redraw(window);
	window->old_display_lines = window->display_lines;
	return;
}


/* * * * * * * * * * * * WINDOW UPDATING AND RESIZING * * * * * * * * * */
/*
 * THese three functions are the one and only functions that are authorized
 * to be used to declare that something needs to be updated on the screen.
 */

void	window_scrollback_needs_rebuild (int winref)
{
	Window *the_window;

	if ((the_window = get_window_by_refnum(winref)))
		the_window->rebuild_scrollback = 1;
}

/*
 * statusbar_needs_update
 */
void	window_statusbar_needs_update (Window *w)
{
	w->update |= UPDATE_STATUS;
}

/*
 * statusbar_needs_redraw
 */
void	window_statusbar_needs_redraw (Window *w)
{
	w->update |= REDRAW_STATUS;
}

/*
 * window_body_needs_redraw
 */
void	window_body_needs_redraw (Window *w)
{
	w->cursor = -1;
}

/*
 * redraw_all_windows: This basically clears and redraws the entire display
 * portion of the screen.  All windows and status lines are draws.  This does
 * nothing for the input line of the screen.  Only visible windows are drawn 
 */
void 	redraw_all_windows (void)
{
	Window	*tmp = NULL;

	if (dumb_mode)
		return;

	while (traverse_all_windows(&tmp)) {
		window_body_needs_redraw(tmp);
		window_statusbar_needs_redraw(tmp);
	}
}

/*
 * update_all_status: This performs a logical "update_window_status" on
 * every window for the current screen.
 */
void 	update_all_status (void)
{
	Window	*window;

	window = NULL;
	while (traverse_all_windows(&window))
		window_statusbar_needs_update(window);
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
static	int	restart;

	if (recursion)
	{
		restart = 1;
		return;
	}

	recursion++;
	while (traverse_all_windows(&tmp))
	{
		if (restart)
		{
			restart = 0;
			tmp = NULL;
			continue;
		}

		if (tmp->rebuild_scrollback)
			rebuild_scrollback(tmp);

		/* 
		 * This should always be done, even for hidden windows
		 * ... i think.
		 */
		if (tmp->display_lines != tmp->old_display_lines)
			resize_window_display(tmp);

		/* Never try to update/redraw an invisible window */
		if (!tmp->screen)
			continue;

		if (tmp->cursor == -1 ||
		   (tmp->cursor < tmp->scrolling_distance_from_display_ip  &&
			 tmp->cursor < tmp->display_lines))
			repaint_window_body(tmp);

		if (tmp->update & REDRAW_STATUS)
		{
			if (!make_status(tmp, 1))
			    tmp->update &= ~REDRAW_STATUS;
			do_input_too = 1;
		}
		else if (tmp->update & UPDATE_STATUS)
		{
			if (!make_status(tmp, 0))
			    tmp->update &= ~UPDATE_STATUS;
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
			panic(1, "uaw: window [%d]'s cursor [%hd] is off the display [%d]", tmp->refnum, tmp->cursor, tmp->display_lines);
	}

	recursion--;
}

/****************************************************************************/
/*
 * Rebalance_windows: this is called when you want all the windows to be
 * rebalanced, except for those who have a set size.
 */
void	rebalance_windows (Screen *screen)
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
	for (tmp = screen->window_list; tmp; tmp = tmp->next)
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
	for (tmp = screen->window_list; tmp; tmp = tmp->next)
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
 * recalculate_windows: this is called when the terminal size changes (as
 * when an xterm window size is changed).  It recalculates the sized and
 * positions of all the windows.  The net change in space is distributed
 * proportionally across the windows as closely as possible.
 */
void 	recalculate_windows (Screen *screen)
{
	int	old_li = 1;
	int	excess_li = 0;
	Window	*tmp;
	int	window_count = 0;
	int	window_resized = 0;
	int	offset;

	if (dumb_mode)
		return;

	/*
	 * If its a new window, just set it and be done with it.
	 */
	if (!screen->current_window)
	{
		screen->window_list->top = 0;
		screen->window_list->toplines_showing = 0;
		screen->window_list->toplines_wanted = 0;
		screen->window_list->display_lines = screen->li - 2;
		screen->window_list->bottom = screen->li - 2;
		screen->window_list->my_columns = screen->co;
		old_li = screen->li;
		return;
	}

	/* 
	 * Expanding the screen takes two passes.  In the first pass,
	 * We figure out how many windows will be resized.  If none can
	 * be rebalanced, we add the whole shebang to the last one.
	 */
	for (tmp = screen->window_list; tmp; tmp = tmp->next)
	{
		old_li += tmp->display_lines + tmp->status.number
				+ tmp->toplines_showing;
		if (tmp->fixed_size && (window_count || tmp->next))
			continue;
		window_resized += tmp->display_lines;
		window_count++;
	}

	excess_li = screen->li - old_li;

	for (tmp = screen->window_list; tmp; tmp = tmp->next)
	{
		if (tmp->fixed_size && tmp->next)
			;
		else
		{
			/*
			 * The number of lines this window gets is:
			 * The number of lines available for resizing times 
			 * the percentage of the resizeable screen the window 
			 * covers.
			 */
			if (tmp->next && window_resized)
				offset = (tmp->display_lines * excess_li) / 
						window_resized;
			else
				offset = excess_li;

			tmp->display_lines += offset;
			if (tmp->display_lines < 0)
				tmp->display_lines = 1;
			excess_li -= offset;
			resize_window_display(tmp);
			recalculate_window_cursor_and_display_ip(tmp);
		}

		/* XXX This is just temporary */
		window_check_columns(tmp);
	}

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
	reconstitute_scrollback(w);
	restore_window_positions(w, scrolling, holding, scrollback);
	w->rebuild_scrollback = 0;
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

	recalculate_window_cursor_and_display_ip(w);
	if (w->scrolling_distance_from_display_ip >= w->display_lines)
		unclear_window(w);
	else
	{
		window_body_needs_redraw(w);
		window_statusbar_needs_redraw(w);
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
	tmp = s->window_list;
	for (i = 1; i < which; i++)
		tmp = tmp->next;

	set_screens_current_window(s, tmp);
	make_window_current(tmp);
}

/*
 * hide_window: sets the given window to invisible and recalculates remaing
 * windows to fill the entire screen 
 */
void 	hide_window (Window *window)
{
	if (!window->screen)
	{
		if (window->name)
			say("Window %s is already hidden", window->name);
		else
			say("Window %d is already hidden", window->refnum);
		return;
	}
	if (!window->swappable)
	{
		if (window->name)
			say("Window %s can't be hidden", window->name);
		else
			say("Window %d can't be hidden", window->refnum);
		return;
	}
	if (window->screen->visible_windows - 
			count_fixed_windows(window->screen) <= 1)
	{
		say("You can't hide the last window.");
		return;
	}
	if (window->screen)
		remove_window_from_screen(window, 1);
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
	Window *w;

	if (!last_input_screen)
		return;
	if (last_input_screen->visible_windows == 1)
		return;

	w = get_next_window(last_input_screen->current_window);
	make_window_current(w);
	/* XXX This is dangerous -- 'make_window_current' might nuke 'w'! */
	set_screens_current_window(last_input_screen, w);
	update_all_windows();
}

/*
 * swap_next_window:  This swaps the current window with the next hidden 
 * window.
 * This is a keybinding.
 */
BUILT_IN_KEYBINDING(swap_next_window)
{
	window_next(current_window, NULL);
	update_all_windows();
}

/*
 * previous_window: This switches the current window to the previous visible
 * window 
 * This is a keybinding
 */
BUILT_IN_KEYBINDING(previous_window)
{
	Window *w;

	if (!last_input_screen)
		return;
	if (last_input_screen->visible_windows == 1)
		return;

	w = get_previous_window(last_input_screen->current_window);
	make_window_current(w);
	/* XXX This is dangerous -- 'make_window_current' might nuke 'w'! */
	set_screens_current_window(last_input_screen, w);
	update_all_windows();
}

/*
 * swap_previous_window:  This swaps the current window with the next 
 * hidden window.
 * This is a keybinding
 */
BUILT_IN_KEYBINDING(swap_previous_window)
{
	window_previous(current_window, NULL);
	update_all_windows();
}

/* show_window: This makes the given window visible.  */
static void 	show_window (Window *window)
{
	if (!window->swappable)
	{
		if (window->name)
			say("Window %s can't be made visible", window->name);
		else
			say("Window %d can't be made visible", window->refnum);
		return;
	}

	if (!window->screen)
	{
		remove_from_invisible_list(window);
		if (!(window->screen = current_window->screen))
			window->screen = last_input_screen; /* What the hey */
		if (!add_to_window_list(window->screen, window))
		{
			/* Ooops. this is an error. ;-) */
			add_to_invisible_list(window);
			return;
		}
	}

	make_window_current(window);
	/* XXX This is dangerous -- 'make_window_current' might nuke 'w'! */
	set_screens_current_window(window->screen, window);
	return;
}




/* * * * * * * * * * * * * GETTING WINDOWS AND WINDOW INFORMATION * * * * */
/*
 * get_window_by_desc: Given either a refnum or a name, find that window
 */
Window *get_window_by_desc (const char *stuff)
{
	Window	*w = NULL;	/* bleh */

	while (traverse_all_windows(&w))
	{
		if (w->name && !my_stricmp(w->name, stuff))
			return w;
	}

	if (is_number(stuff) && (w = get_window_by_refnum(my_atol(stuff))))
		return w;

	return NULL;
}


/*
 * get_window_by_refnum: Given a reference number to a window, this returns a
 * pointer to that window if a window exists with that refnum, null is
 * returned otherwise.  The "safe" way to reference a window is throught the
 * refnum, since a window might be delete behind your back and and Window
 * pointers might become invalid.
 */
Window *get_window_by_refnum (unsigned refnum)
{
	Window	*tmp = NULL;

	if (refnum == 0)
		return current_window;

	while (traverse_all_windows(&tmp))
	{
		if (tmp->refnum == refnum)
			return tmp;
	}

	return NULL;
}

static Window *get_window_by_servref (int servref)
{
	Window *tmp = NULL;
	Window *best = NULL;

	while (traverse_all_windows(&tmp))
	{
	    if (tmp->server != servref)
		continue;
	    if (best == NULL || best->priority < tmp->priority)
		best = tmp;
	}

	return best;
}

int	get_winref_by_servref (int servref)
{
	Window *best = get_window_by_servref(servref);

	if (best)
		return best->refnum;
	else
		return -1;
}

/*
 * get_next_window: This overly complicated function attempts to find the
 * next non "skippable" window.  The reason for the complication is that it
 * needs to be able to deal with wrapping over to the top of the screen,
 * if the next window is at the bottom, or isnt selectable, YGTI.
 */
static	Window	*get_next_window  (Window *w)
{
	Window *last = w;
	Window *new_w = w;

	if (!w || !w->screen)
		last = new_w = w = current_window;

	do
	{
		if (new_w->next)
			new_w = new_w->next;
		else
			new_w = w->screen->window_list;
	}
	while (new_w && new_w->skip && new_w != last);

	return new_w;
}

/*
 * get_previous_window: this returns the previous *visible* window in the
 * window list.  This automatically wraps to the last window in the window
 * list 
 */
static	Window	*get_previous_window (Window *w)
{
	Window *last = w;
	Window *new_w = w;

	if (!w || !w->screen)
		last = new_w = w = current_window;

	do
	{
		if (new_w->prev)
			new_w = new_w->prev;
		else
			new_w = w->screen->window_list_end;
	}
	while (new_w->skip && new_w != last);

	return new_w;
}


/*
 * get_visible_by_refnum: Returns 1 if the specified window is visible.
 */
int 	is_window_visible (char *arg)
{
	Window	*win;

	if ((win = get_window_by_desc(arg)))
	{
		if (win->screen)
			return 1;
		else
			return 0;
	}

	return -1;
}

/* 
 * XXXX i have no idea if this belongs here.
 */
char *	get_status_by_refnum (unsigned refnum, int line)
{
	Window *the_window;

	if ((the_window = get_window_by_refnum(refnum)))
	{
		if (line > the_window->status.number)
			return NULL;

		return denormalize_string(the_window->status.line[line].result);
	}
	else
		return NULL;
}




/* * * * * * * * * * * * * INPUT PROMPT * * * * * * * * * * * * * * */
/*
 * set_prompt_by_refnum: changes the prompt for the given window.  A window
 * prompt will be used as the target in place of the query user or current
 * channel if it is set 
 */
void 	set_prompt_by_refnum (unsigned refnum, const char *prompt)
{
	Window	*tmp;

	if (!(tmp = get_window_by_refnum(refnum)))
		tmp = current_window;
	malloc_strcpy(&tmp->prompt, prompt);
	update_input(NULL, UPDATE_ALL);
}

/* get_prompt_by_refnum: returns the prompt for the given window refnum */
const char 	*get_prompt_by_refnum (unsigned refnum)
{
	Window	*tmp;

	if (!(tmp = get_window_by_refnum(refnum)))
		tmp = current_window;

	return tmp->prompt ? tmp->prompt : empty_string;
}

/* * * * * * * * * * * * * * * TARGETS AND QUERIES * * * * * * * * * * * */
/*
 * get_target_by_refnum: returns the target for the window with the given
 * refnum (or for the current window).  The target is either the query nick
 * or current channel for the window 
 */
const char 	*get_target_by_refnum (unsigned refnum)
{
	Window	*tmp;
	const char *	cc;

	if (!(tmp = get_window_by_refnum(refnum)))
		if (!(tmp = last_input_screen->current_window))
			return NULL;

	if ((cc = get_equery_by_refnum(refnum)))
		return cc;
	if ((cc = get_echannel_by_refnum(refnum)))
		return cc;
	return NULL;
}

/* query_nick: Returns the query nick for the current channel */
const char	*query_nick (void)
{
	return get_equery_by_refnum(0);
}

const char *	get_equery_by_refnum (int refnum)
{
	WNickList *nick;
	Window *win;

	if ((win = get_window_by_refnum(refnum)) == NULL)
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

	win = get_window_by_refnum(0);
	lowcount = win->query_counter;

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
         * If there are no channels on this window, punt.
         * If there is only one channel on this window, punt.
         */
        if (winner == NULL || highcount == -1 || highcount == lowcount)
                return;

	/* Make the oldest query the newest. */
	winner->counter = current_query_counter++;
	win->query_counter = winner->counter;
	window_statusbar_needs_update(win);
}

static void	recheck_queries (Window *win)
{
	WNickList *nick;

	win->query_counter = 0;

	/* Find the winner and reset query_counter */
	for (nick = win->nicks; nick; nick = nick->next)
	{
		if (nick->counter > win->query_counter)
			win->query_counter = nick->counter;
	}

	window_statusbar_needs_update(win);
}

/* 
 * This forces any window for the server to release its claim upon
 * a nickname (so it can be claimed by another window)
 */
static int	window_claims_nickname (unsigned winref, int server, const char *nick)
{
        Window *window = NULL;
	WNickList *item;
	int 	l;
	int	already_claimed = 0;

        while (traverse_all_windows(&window))
        {
            if (window->server != server)
		continue;

	    if (window->refnum != winref)
	    {
		if ((item = (WNickList *)remove_from_list(
				(List **)&window->nicks, nick)))
		{
			l = message_setall(window->refnum, who_from, who_level);
			say("Removed %s from window name list", item->nick);
			pop_message_from(l);

			new_free(&item->nick);
			new_free((char **)&item);
		}
	    }
	    else
	    {
		if (find_in_list((List **)&window->nicks, nick, !USE_WILDCARDS))
			already_claimed = 1;
	    }
	}

	if (already_claimed)
		return -1;
	else
		return 0;
}


/* * * * * * * * * * * * * * CHANNELS * * * * * * * * * * * * * * * * * */
/* get_echannel_by_refnum: returns the current channel for window refnum */
const char 	*get_echannel_by_refnum (unsigned refnum)
{
	Window	*tmp;

	if ((tmp = get_window_by_refnum(refnum)) == (Window *) 0)
		panic(1, "get_echannel_by_refnum: invalid window [%d]", refnum);
	return window_current_channel(tmp->refnum, tmp->server);
}

int	is_window_waiting_for_channel (unsigned refnum, const char *chan)
{
	Window *tmp;
	if (!(tmp = get_window_by_refnum(refnum)))
		return 0;

	if (chan == NULL)
	{
		if (tmp->waiting_chans)
			return 1;
		else
			return 0;
	}

	if (find_in_list((List **)&tmp->waiting_chans, chan, !USE_WILDCARDS))
		return 1;		/* Already present. */

	return 0;
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

	if (!(tmp = get_window_by_refnum(refnum)))
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

	    if ((tmp = (WNickList *)remove_from_list((List **)&w->waiting_chans, chan)))
	    {
		new_free(&tmp->nick);
		new_free((char **)&tmp);
	    }
	}

	if (find_in_list((List **)&win->waiting_chans, chan, !USE_WILDCARDS))
		return -1;		/* Already present. */

	tmp = (WNickList *)new_malloc(sizeof(WNickList));
	tmp->nick = malloc_strdup(chan);
	add_to_list((List **)&win->waiting_chans, (List *)tmp);
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

	    if ((tmp = (WNickList *)remove_from_list((List **)&w->waiting_chans, chan)))
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
int 	get_window_server (unsigned int refnum)
{
	Window	*tmp;

	if ((tmp = get_window_by_refnum(refnum)) == (Window *) 0)
		tmp = current_window;
	return (tmp->server);
}

/*
 * Changes (in bulk) all of the windows pointing at "old_server" to 
 * "new_server".  This implements the back-end of the /SERVER command.
 * When this returns, no servers will be pointing at "old_server", and 
 * so at the next sequence point it will be closed.
 * 
 * This is used by the /SERVER command (via /SERVER +, /SERVER -, or 
 * /SERVER <name>), and by the 465 numeric (YOUREBANNEDCREPP).
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
	Window	*tmp;
	int	cnt, max, i;
	int	prime = NOSERV;
	int	status;
	int	l;

	connected_to_server = 0;
	max = server_list_size();
	for (i = 0; i < max; i++)
	{
	    status = get_server_status(i);
	    cnt = 0;

	    if (!(tmp = get_window_by_servref(i)))
	    {
		if (status > SERVER_RECONNECT && status < SERVER_CLOSING)
            	{
		    if (get_server_autoclose(i))
			close_server(i, "No windows for this server");
	        }
		continue;		/* Move on to next server */
	    }

	    connected_to_server++;
	    l = message_setall(tmp->refnum, NULL, LEVEL_OTHER);

	    if (status == SERVER_RECONNECT)
	    {
		if (x_debug & DEBUG_SERVER_CONNECT)
		    yell("window_check_servers() is bringing up server %d", i);

		grab_server_address(i);
		/* connect_to_server(i); */
	    }
	    else if (status == SERVER_ACTIVE)
	    {
		if (prime == NOSERV)
		    prime = tmp->server;
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

/*
	update_all_status();
	cursor_to_input();
*/
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
int	set_mask_by_winref (unsigned refnum, Mask mask)
{
	Window *win;

	if (!(win = get_window_by_refnum(refnum)))
		return -1;

	win->window_mask = mask;
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
		yell("Setting context %d [%d] {%s:%d}", context_counter, refnum, file, line);

#ifdef NO_CHEATING
	malloc_strcpy(&contexts[context_counter].who_from, who);
#else
	contexts[context_counter].who_from = who;
#endif
	contexts[context_counter].who_level = level;
	contexts[context_counter].who_file = file;
	contexts[context_counter].who_line = line;
	contexts[context_counter].to_window = refnum;

	who_from = who;
	who_level = level;
	to_window = get_window_by_refnum(refnum);
	return context_counter++;
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
		yell("Setting context %d [%s:%d] {%s:%d}", context_counter, who?who:"NULL", level, file, line);

#ifdef NO_CHEATING
	malloc_strcpy(&contexts[context_counter].who_from, who);
#else
	contexts[context_counter].who_from = who;
#endif
	contexts[context_counter].who_level = level;
	contexts[context_counter].who_file = file;
	contexts[context_counter].who_line = line;
	contexts[context_counter].to_window = -1;

	who_from = who;
	who_level = level;
	to_window = NULL;
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

	who_from = contexts[context_counter - 1].who_from;
	who_level = contexts[context_counter - 1].who_level;
	to_window = get_window_by_refnum(contexts[context_counter - 1].to_window);
}

/* * * * * * * * * * * CLEARING WINDOWS * * * * * * * * * * */
static void 	clear_window (Window *window)
{
	if (dumb_mode)
		return;

	window->scrolling_top_of_display = window->display_ip;
	window->notified = 0;
	recalculate_window_cursor_and_display_ip(window);

	window_body_needs_redraw(window);
	window_statusbar_needs_redraw(window);
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
void 	clear_window_by_refnum (unsigned refnum, int unhold)
{
	Window	*tmp;

	if (!(tmp = get_window_by_refnum(refnum)))
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
	window_body_needs_redraw(window);
	window_statusbar_needs_redraw(window);
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

void	unclear_window_by_refnum (unsigned refnum, int unhold)
{
	Window *tmp;

	if (!(tmp = get_window_by_refnum(refnum)))
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
int	window_is_holding (Window *w)
{
	if (w->holding_distance_from_display_ip > w->display_lines)
		return 1;
	else
		return 0;
}

/*
 * This returns 1 if 'w' is in scrollback view.
 */
int	window_is_scrolled_back (Window *w)
{
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
int	unhold_a_window (Window *w)
{
	int	slider, i;

	if (!w->holding_top_of_display)
		return 0;				/* ok, whatever */

	slider = ((int)w->hold_slider * w->display_lines) / 100;
	for (i = 0; i < slider; i++)
	{
		if (w->holding_top_of_display == w->display_ip)
			break;
		w->holding_top_of_display = w->holding_top_of_display->next;
	}
	recalculate_window_cursor_and_display_ip(w);
	window_body_needs_redraw(w);
	window_statusbar_needs_update(w);
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

/* current_refnum: returns the reference number for the current window */
unsigned	current_refnum (void)
{
	return current_window->refnum;
}

int 	number_of_windows_on_screen (Window *w)
{
	return w->screen->visible_windows;
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

char	*get_nicklist_by_window (Window *win)
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
	const char *chan = get_echannel_by_refnum(window->refnum);
	const char *q = get_equery_by_refnum(window->refnum);

	if (cnw == 0)
		cnw = 12;	/* Whatever */

	say(WIN_FORM,           ltoa(window->refnum),
		      12, 12,   get_server_nickname(window->server),
		      len, len, window->name ? window->name : "<None>",
		      cnw, cnw, chan ? chan : "<None>",
		                q ? q : "<None>",
		                get_server_itsname(window->server),
		                mask_to_str(&window->window_mask),
		                window->screen ? empty_string : " Hidden");
}

int     get_geom_by_winref (const char *desc, int *co, int *li)
{
        Window  *win = get_window_by_desc(desc);

        if (!win || !win->screen)
                return -1;
        *co = win->screen->co;
        *li = win->screen->li;
        return 0;
}

int	get_indent_by_winref (int winref)
{
	Window *win;

	if (winref == -1 || (!(win = get_window_by_refnum(winref))))
		return get_int_var(INDENT_VAR);

	return win->indent;
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
						name, tmp->refnum);
			}
		}
	}
	else
		say("%s: Please specify a window refnum or LAST", name);
	return ((Window *) 0);
}


/* get_number: parses out an integer number and returns it */
static int 	get_number (const char *name, char **args)
{
	char	*arg;

	if ((arg = next_arg(*args, args)) != NULL)
		return (my_atol(arg));
	else
		say("%s: You must specify the number of lines", name);
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

	newval = *var;
	if (!(arg = next_arg(*args, args)))
	{
		say("Window %s is %s", name, onoff[newval]);
		return -1;
	}

	if (do_boolean(arg, &newval))
	{
		say("Value for %s must be ON, OFF, or TOGGLE", name);
		return -1;
	}

	/* The say() MUST BE DONE BEFORE THE ASSIGNMENT! */
	say("Window %s is %s", name, onoff[newval]);
	*var = newval;
	return (0);
}

/*
 * /WINDOW ADD nick<,nick>
 * Adds a list of one or more nicknames to the current list of usupred
 * targets for the current window.  These are matched up with the nick
 * argument for message_from().
 */
static Window *window_add (Window *window, char **args)
{
	char		*ptr;
	WNickList 	*new_w;
	char 		*arg = next_arg(*args, args);

	if (!arg)
		say("ADD: Add nicknames to be redirected to this window");

	else while (arg)
	{
		if ((ptr = strchr(arg, ',')))
			*ptr++ = 0;
		if (!window_claims_nickname(window->refnum, window->server, arg))
		{
			say("Added %s to window name list", arg);
			new_w = (WNickList *)new_malloc(sizeof(WNickList));
			new_w->nick = malloc_strdup(arg);
			new_w->counter = 0;
			add_to_list((List **)&(window->nicks), (List *)new_w);
		}
		else
			say("%s already on window name list", arg);

		arg = ptr;
	}

	return window;
}

/*
 * /WINDOW BACK
 * Changes the current window pointer to the window that was most previously
 * the current window.  If that window is now hidden, then it is swapped with
 * the current window.
 */
static Window *window_back (Window *window, char **args)
{
	Window *tmp;

	tmp = get_window_by_refnum(last_input_screen->last_window_refnum);
	if (!tmp)
		tmp = last_input_screen->window_list;

	make_window_current(tmp);
	/* XXX This is dangerous, 'make_window_current' might nuke 'tmp' */
	if (tmp->screen)
		set_screens_current_window(tmp->screen, tmp);
	else
		swap_window(window, tmp);

	return window;
}

/*
 * /WINDOW BALANCE
 * Causes all of the windows on the current screen to be adjusted so that 
 * the largest window on the screen is no more than one line larger than
 * the smallest window on the screen.
 */
static Window *window_balance (Window *window, char **args)
{
	if (window->screen)
		rebalance_windows(window->screen);
	else
		yell("Cannot balance invisible windows!");

	return window;
}

/*
 * /WINDOW BEEP_ALWAYS ON|OFF
 * Indicates that when this window is HIDDEN (sorry, thats not what it seems
 * like it should do, but that is what it does), beeps to this window should
 * not be suppressed like they normally are for hidden windows.  In all cases,
 * the current window is notified when a beep occurs if this window is hidden.
 */
static Window *window_beep_always (Window *window, char **args)
{
	if (get_boolean("BEEP_ALWAYS", args, &window->beep_always))
		return NULL;
	return window;
}

/*
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
static Window *window_channel (Window *window, char **args)
{
	char		*arg;
	char 		*chans, *passwds;
	char 		*chan, *pass;
	const char 	*c;
	char 		*chans_to_join, *passes_to_use;
	int		l;

	/* Fix by Jason Brand, Nov 6, 2000 */
	if (window->server == NOSERV)
	{
		say("This window is not connected to a server.");
		return NULL;
	}

	if (!(passwds = new_next_arg(*args, args)))
	{
	    if ((c = get_echannel_by_refnum(window->refnum)))
		say("The current channel is %s", c);
	    else
		say("There are no channels in this window");
	    return window;
	}

	if (!(chans = next_arg(passwds, &passwds)))
	{
		say("Huh?");
		return window;
	}

	if (!my_strnicmp(chans, "-i", 2))
	{
		if (!(c = get_server_invite_channel(window->server)))
		{
			say("You have not been invited to a channel!");
			return window;
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
		send_to_aserver(window->server,"JOIN %s %s", chans_to_join, 
								passes_to_use);
	else if (chans_to_join)
		send_to_aserver(window->server,"JOIN %s", chans_to_join);

	new_free(&chans_to_join);
	new_free(&passes_to_use);
	pop_message_from(l);

	return window;
}

/* WINDOW CLEAR -- should be obvious, right? */
static Window *window_clear (Window *window, char **args)
{
	clear_window(window);
	return window;
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
static Window *window_create (Window *window, char **args)
{
	Window *tmp;
	if ((tmp = (Window *)create_additional_screen()))
		window = tmp;
	else
		say("Cannot create new screen!");

	return window;
}

/*
 * /WINDOW DELETE
 * This directs the client to close the current external physical screen
 * and to re-parent any windows onto other screens.  You are not allowed
 * to delete the "main" window because that window belongs to the process
 * group of the client itself.
 */
static Window *window_delete (Window *window, char **args)
{
	kill_screen(window->screen);
	return current_window;
}

/*
 * /WINDOW DESCRIBE
 * Directs the client to tell you a bit about the current window.
 * This is the 'default' argument to the /window command.
 */
static Window *window_describe (Window *window, char **args)
{
	const char *chan;
	char *c;

if (window->name)
	say("Window %s (%u)", 
				window->name, window->refnum);
else
	say("Window %u", window->refnum);

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

	chan = get_echannel_by_refnum(window->refnum);
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

	chan = get_equery_by_refnum(window->refnum);
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

	say("\tPrompt: %s", 
				window->prompt ? 
				window->prompt : "<None>");
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

	return window;
}

/*
 * /WINDOW DISCON
 * This disassociates a window with all servers.
 */
static Window *window_discon (Window *window, char **args)
{
	reassign_window_channels(window->refnum);
	destroy_window_waiting_channels(window->refnum);
	window_change_server(window, NOSERV); /* XXX This shouldn't be set here. */
	window_statusbar_needs_update(window);
	return window;
}


/*
 * /WINDOW DOUBLE ON|OFF
 * This directs the client to enable or disable the supplimentary status bar.
 * When the "double status bar" is enabled, the status formats are taken from
 * /set STATUS_FORMAT1 or STATUS_FORMAT2.  When it is disabled, the format is
 * taken from /set STATUS_FORMAT.
 */
static Window *window_double (Window *window, char **args)
{
	short	newval = 0;
	int	current = window->status.number;

	if (get_boolean("DOUBLE", args, &newval))
		return NULL;

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
	return window;
}

/*
 * WINDOW ECHO <text>
 *
 * Text must either be surrounded with double-quotes (")'s or it is assumed
 * to terminate at the end of the argument list.  This sends the given text
 * to the current window.
 */
static	Window *window_echo (Window *window, char **args)
{
	const char *to_echo;
	int	l;

	if (**args == '"')
		to_echo = new_next_arg(*args, args);
	else
		to_echo = *args, *args = NULL;

	l = message_setall(window->refnum, who_from, who_level);
	put_echo(to_echo);
	pop_message_from(l);

	return window;
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
static	Window *window_fixed (Window *window, char **args)
{
	if (get_boolean("FIXED", args, &window->fixed_size))
		return NULL;
	return window;
}

/*
 * /WINDOW FLUSH
 *
 * Does the window part of the /flush command.
 */
static	Window *window_flush (Window *window, char **args)
{
	flush_scrollback_after(window);
	return window;
}

/*
 * /WINDOW FLUSH_SCROLLBACK
 */
static	Window *window_flush_scrollback (Window *window, char **args)
{
	flush_scrollback(window);
	return window;
}

/*
 * /WINDOW GOTO refnum
 * This switches the current window selection to the window as specified
 * by the numbered refnum.
 */
static Window *window_goto (Window *window, char **args)
{
	my_goto_window(window->screen, get_number("GOTO", args));
	from_server = get_window_server(0);
	return current_window;
}


/*
 * /WINDOW GROW lines
 * This directs the client to expand the specified window by the specified
 * number of lines.  The number of lines should be a positive integer, and
 * the window's growth must not cause another window to be smaller than
 * the minimum of 3 lines.
 */
static Window *window_grow (Window *window, char **args)
{
	resize_window(RESIZE_REL, window, get_number("GROW", args));
	return window;
}

/*
 * /WINDOW HIDE
 * This directs the client to remove the specified window from the current
 * (visible) screen and place the window on the client's invisible list.
 * A hidden window has no "screen", and so can not be seen, and does not
 * have a size.  It can be unhidden onto any screen.
 */
static Window *window_hide (Window *window, char **args)
{
	hide_window(window);
	return current_window;
}

/*
 * /WINDOW HIDE_OTHERS
 * This directs the client to place *all* windows on the current screen,
 * except for the current window, onto the invisible list.
 */
static Window *window_hide_others (Window *window, char **args)
{
	Window *tmp, *next;

	if (window->screen)
		tmp = window->screen->window_list;
	else
		tmp = invisible_list;

	while (tmp)
	{
		next = tmp->next;
		if (tmp != window)
			hide_window(tmp);
		tmp = next;
	}

	return window;
}

/*
 * /WINDOW HOLD_INTERVAL
 * This determines how frequently the status bar should update the "HELD"
 * value when you are in holding mode.  The default is 10, so that your
 * status bar isn't constantly flickering every time a new line comes in.
 * But if you want better responsiveness, this is the place to change it.
 */
static Window *window_hold_interval (Window *window, char **args)
{
	char *arg = next_arg(*args, args);

	if (arg)
	{
		int	size = my_atol(arg);

		if (size <= 0)
		{
			say("Hold interval must be a positive value!");
			return window;
		}
		window->hold_interval = size;
	}
	say("Window hold interval notification is %d", window->hold_interval);
	return window;
}

/*
 * /WINDOW HOLD_MODE
 * This arranges for the window to "hold" any output bound for it once
 * a full page of output has been completed.  Setting the global value of
 * HOLD_MODE is truly bogus and should be changed. XXXX
 */
static Window *window_hold_mode (Window *window, char **args)
{
	short	hold_mode;
	int	slider;
	int	i;

	if (window->holding_top_of_display)
		hold_mode = 1;
	else
		hold_mode = 0;

	if (get_boolean("HOLD_MODE", args, &hold_mode))
		return NULL;

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
		window_body_needs_redraw(window);
		window_statusbar_needs_update(window);
	}
	if (!hold_mode && window->holding_top_of_display)
	{
		window->holding_top_of_display = NULL;
		recalculate_window_cursor_and_display_ip(window);
		window_body_needs_redraw(window);
		window_statusbar_needs_update(window);
	}

	return window;
}

/*
 * /WINDOW HOLD_SLIDER
 * This determines how far up the hold pointer should move when you hit
 * the return key (unhold action)
 */
static Window *window_hold_slider (Window *window, char **args)
{
	char *arg = next_arg(*args, args);

	if (arg)
	{
		int	size = my_atol(arg);

		if (size < 0 || size > 100)
		{
			say("Hold slider must be 0 to 100!");
			return window;
		}
		window->hold_slider = size;
	}
	say("Window hold slider is %d", window->hold_slider);
	return window;
}

/*
 * /WINDOW INDENT (ON|OFF)
 *
 * When this is ON, 2nd and subsequent physical lines of display per logical
 * line of output are indented to the start of the second word on the first 
 * line.  This is essentially to the /set indent value.
 */
static	Window *window_indent (Window *window, char **args)
{
	if (get_boolean("INDENT", args, &window->indent))
		return NULL;
	return window;
}

/*
 * /WINDOW KILL
 * This arranges for the current window to be destroyed.  Once a window
 * is killed, it cannot be recovered.  Because every server must have at
 * least one window "connected" to it, if you kill the last window for a
 * server, the client will drop your connection to that server automatically.
 */
static Window *window_kill (Window *window, char **args)
{
	if (!window->killable)
	{
		say("You cannot kill an unkillable window");
		return NULL;
	}
	delete_window(window);
	return current_window;
}

/*
 * /WINDOW KILL_ALL_HIDDEN
 * This kills all of the hidden windows.  If the current window is hidden,
 * then the current window will probably change to another window.
 */
static Window *window_kill_all_hidden (Window *window, char **args)
{
	Window *tmp, *next;

	tmp = invisible_list;
	while (tmp)
	{
		next = tmp->next;
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
	return window;
}


/*
 * /WINDOW KILL_OTHERS
 * This arranges for all windows on the current screen, other than the 
 * current window to be destroyed.  Obviously, the current window will be
 * the only window left on the screen.  Connections to servers other than
 * the server for the current window will be implicitly closed.
 */
static Window *window_kill_others (Window *window, char **args)
{
	Window *tmp, *next;

	if (window->screen)
		tmp = window->screen->window_list;
	else
		tmp = invisible_list;

	while (tmp)
	{
		next = tmp->next;
		if (tmp->killable)
		{
		    if (tmp != window)
			delete_window(tmp);
		}
		tmp = next;
	}

	return window;
}

static Window *window_killable (Window *window, char **args)
{
	if (get_boolean("KILLABLE", args, &window->killable))
		return NULL;

	return window;
}

/*
 * /WINDOW KILLSWAP
 * This arranges for the current window to be replaced by the last window
 * to be hidden, and also destroys the current window.
 */
static Window *window_killswap (Window *window, char **args)
{
	if (!window->killable)
	{
		say("You cannot KILLSWAP an unkillable window");
		return NULL;
	}
	if (invisible_list)
	{
		swap_window(window, invisible_list);
		delete_window(window);
	}
	else
		say("There are no hidden windows!");

	return current_window;
}

/*
 * /WINDOW LAST
 * This changes the current window focus to the window that was most recently
 * the current window *but only if that window is still visible*.  If the 
 * window is no longer visible (having been HIDDEN), then the next window
 * following the current window will be made the current window.
 */
static Window *window_last (Window *window, char **args)
{
	set_screens_current_window(window->screen, NULL);
	return current_window;
}

/*
 * /WINDOW LASTLOG <size>
 * This changes the size of the window's lastlog buffer.  The default value
 * for a window's lastlog is the value of /set LASTLOG, but each window may
 * be independantly tweaked with this command.
 */
static Window *window_lastlog (Window *window, char **args)
{
	char *arg = next_arg(*args, args);

	if (arg)
	{
		int size;

		if (!is_number(arg))
		{
		    say("/WINDOW LASTLOG takes a number (you said %s)", arg);
		    return NULL;
		}

		if ((size = my_atol(arg)) < 0)
		{
			say("Lastlog size must be non-negative");
			return window;
		}
		window->lastlog_max = size;
		trim_lastlog(window);
	}
	say("Lastlog size is %d", window->lastlog_max);
	return window;
}

/*
 * /WINDOW LASTLOG_LEVEL <level-description>
 * This changes the types of lines that will be placed into this window's
 * lastlog.  It is useful to note that the window's lastlog will contain
 * a subset (possibly a complete subset) of the lines that have appeared
 * in the window.  This setting allows you to control which lines are
 * "thrown away" by the window.
 */
static Window *window_lastlog_mask (Window *window, char **args)
{
	char *arg = next_arg(*args, args);;
	char *rejects = NULL;

	if (arg)
	{
	    if (str_to_mask(&window->lastlog_mask, arg, &rejects))
		standard_level_warning("/WINDOW LASTLOG_LEVEL", &rejects);
	}
	say("Lastlog level is %s", mask_to_str(&window->lastlog_mask));
	return window;
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
static Window *window_level (Window *window, char **args)
{
	char 	*arg;
	int	add = 0;
	Mask	mask;
	int	i;
	char *	rejects = NULL;

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
	return window;
}

/*
 * /WINDOW LIST
 * This lists all of the windows known to the client, and a breif summary
 * of their current state.
 */
static Window *window_list (Window *window, char **args)
{
	Window	*tmp = NULL;
	int	len = 6;
	int	cnw = get_int_var(CHANNEL_NAME_WIDTH_VAR);

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

	return window;
}

/*
 * /WINDOW LOG ON|OFF
 * This sets the current state of the logfile for the given window.  When the
 * logfile is on, then any lines that appear on the window are written to the
 * logfile 'as-is'.  The name of the logfile can be controlled with
 * /WINDOW LOGFILE.  The default logfile name is <windowname>.<target|refnum>
 */
static Window *window_log (Window *window, char **args)
{
	const char *logfile;
	int add_ext = 1;
	char buffer[BIG_BUFFER_SIZE + 1];

	if (get_boolean("LOG", args, &window->log))
		return NULL;

	if ((logfile = window->logfile))
		add_ext = 0;
	else if (!(logfile = get_string_var(LOGFILE_VAR)))
		logfile = empty_string;

	strlcpy(buffer, logfile, sizeof buffer);

	if (add_ext)
	{
		const char *title = empty_string;

		strlcat(buffer, ".", sizeof buffer);
		if ((title = get_echannel_by_refnum(window->refnum)))
			strlcat(buffer, title, sizeof buffer);
		else if ((title = get_equery_by_refnum(window->refnum)))
			strlcat(buffer, title, sizeof buffer);
		else
		{
			strlcat(buffer, "Window_", sizeof buffer);
			strlcat(buffer, ltoa(window->refnum), sizeof buffer);
		}
	}

	do_log(window->log, buffer, &window->log_fp);
	if (!window->log_fp)
		window->log = 0;

	return window;
}

/*
 * /WINDOW LOGFILE <filename>
 * This sets the current value of the log filename for the given window.
 * When you activate the log (with /WINDOW LOG ON), then any output to the
 * window also be written to the filename specified.
 */
static Window *window_logfile (Window *window, char **args)
{
	char *arg = next_arg(*args, args);

	if (arg)
	{
		malloc_strcpy(&window->logfile, arg);
		say("Window LOGFILE set to %s", arg);
	}
	else if (window->logfile)
		say("Window LOGFILE is %s", window->logfile);
	else
		say("Window LOGFILE is not set.");

	return window;
}

static Window *window_move (Window *window, char **args)
{
	move_window(window, get_number("MOVE", args));
	return window;
}

static Window *window_move_to (Window *window, char **args)
{
	move_window_to(window, get_number("MOVE", args));
	return window;
}

static Window *window_name (Window *window, char **args)
{
	char *arg;

	if ((arg = new_next_arg(*args, args)))
	{
		/* /window name -  unsets the window name */
		if (!strcmp(arg, "-"))
		{
			new_free(&window->name);
			window_statusbar_needs_update(window);
		}

		/* 
		 * /window name to existing name -- permit this, to allow
		 * the user to change case of characters in the name.
		 */
		else if (window->name && (my_stricmp(window->name, arg) == 0))
		{
			malloc_strcpy(&window->name, arg);
			window_statusbar_needs_update(window);
		}

		else if (is_window_name_unique(arg))
		{
			malloc_strcpy(&window->name, arg);
			window_statusbar_needs_update(window);
		}

		else
			say("%s is not unique!", arg);
	}
	else
		say("You must specify a name for the window!");

	return window;
}

static Window *window_new (Window *window, char **args)
{
	Window *tmp;
	if ((tmp = new_window(window->screen)))
		window = tmp;

	make_window_current(window);
	/* XXX This is dangerous -- 'make_window_current' might nuke 'window' */
	return window;
}

static Window *window_new_hide (Window *window, char **args)
{
	new_window(NULL);
	return window;
}

static Window *window_next (Window *window, char **args)
{
	Window	*tmp;
	Window	*next = NULL;
	Window	*smallest = NULL;

	smallest = window;
	for (tmp = invisible_list; tmp; tmp = tmp->next)
	{
		if (!tmp->swappable || tmp->skip)
			continue;
		if (tmp->refnum < smallest->refnum)
			smallest = tmp;
		if ((tmp->refnum > window->refnum)
		    && (!next || (tmp->refnum < next->refnum)))
			next = tmp;
	}

	if (!next)
		next = smallest;

	if (next == NULL || next == window)
	{
		say("There are no hidden windows");
		return NULL;
	}

	swap_window(window, next);
	return current_window;
}

static Window *window_notify (Window *window, char **args)
{
	if (get_boolean("NOTIFY", args, &window->notify_when_hidden))
		return NULL;

	return window;
}

static Window *window_notify_list (Window *window, char **args)
{
	if (get_boolean("NOTIFIED", args, &window->notified))
		return NULL;

	return window;
}

static Window *window_notify_mask (Window *window, char **args)
{
	char *arg;
	char *rejects = NULL;

	if ((arg = next_arg(*args, args)))
	    if (str_to_mask(&window->notify_mask, arg, &rejects))
		standard_level_warning("/WINDOW NOTIFY_LEVEL", &rejects);

	say("Window notify level is %s", mask_to_str(&window->notify_mask));
	return window;
}

static Window *window_notify_name (Window *window, char **args)
{
	char *arg;

	if ((arg = new_next_arg(*args, args)))
	{
		/* /window name -  unsets the window name */
		if (!strcmp(arg, "-"))
		{
			new_free(&window->notify_name);
			window_statusbar_needs_update(window);
			say("Window NOTIFY NAME unset");
			return window;
		}

		/* /window name to existing name -- ignore this. */
		else if (window->notify_name && (my_stricmp(window->notify_name, arg) == 0))
		{
			say("Window NOTIFY NAME is %s", window->notify_name);
			return window;
		}

		else
		{
			malloc_strcpy(&window->notify_name, arg);
			window_statusbar_needs_update(window);
			say("Window NOTIFY NAME changed to %s", 
					window->notify_name);
			return window;
		}
	}
	else
		say("Window NOTIFY NAME is %s", window->notify_name);

	return window;
}


static Window *window_number (Window *window, char **args)
{
	Window 	*tmp;
	char 	*arg;
	int 	i, oldref, newref;

	if ((arg = next_arg(*args, args)))
	{
#if 0
		if (window_current_channel(window->refnum, window->server))
		{
			say("You cannot change the number of a window with a channel");
			return window;
		}
#endif

		if ((i = my_atol(arg)) > 0)
		{
			oldref = window->refnum;
			newref = i;

			if ((tmp = get_window_by_refnum(i)))
				/* XXX refnum is changed here XXX */
				tmp->refnum = oldref;

			/* XXX refnum is changed here XXX */
			window->refnum = newref;

			lastlog_swap_winrefs(oldref, newref);
			channels_swap_winrefs(oldref, newref);
			logfiles_swap_winrefs(oldref, newref);
			timers_swap_winrefs(oldref, newref);

			if (tmp)
				window_statusbar_needs_update(tmp);
			window_statusbar_needs_update(window);
		}
		else
			say("Window number must be greater than 0");
	}
	else
		say("Window number missing");

	return window;
}

/*
 * /WINDOW POP
 * This changes the current window focus to the most recently /WINDOW PUSHed
 * window that still exists.  If the window is hidden, then it will be made
 * visible.  Any windows that are found along the way that have been since
 * KILLed will be ignored.
 */
static Window *window_pop (Window *window, char **args)
{
	int 		refnum;
	WindowStack 	*tmp;
	Window		*win = NULL;

	while (window->screen->window_stack)
	{
		refnum = window->screen->window_stack->refnum;
		tmp = window->screen->window_stack->next;
		new_free((char **)&window->screen->window_stack);
		window->screen->window_stack = tmp;

		win = get_window_by_refnum(refnum);
		if (!win)
			continue;

		if (win->screen)
			set_screens_current_window(win->screen, win);
		else
			show_window(win);
	}

	if (!window->screen->window_stack && !win)
		say("The window stack is empty!");

	return win;
}

static Window *window_previous (Window *window, char **args)
{
	Window	*tmp;
	Window	*previous = NULL, *largest;

	largest = window;
	for (tmp = invisible_list; tmp; tmp = tmp->next)
	{
		if (!tmp->swappable || tmp->skip)
			continue;
		if (tmp->refnum > largest->refnum)
			largest = tmp;
		if ((tmp->refnum < window->refnum)
		    && (!previous || tmp->refnum > previous->refnum))
			previous = tmp;
	}

	if (!previous)
		previous = largest;

	if (previous == NULL || previous == window)
	{
		say("There are no hidden windows to swap in");
		return NULL;
	}

	swap_window(window, previous);
	return current_window;
}

static Window *window_prompt (Window *window, char **args)
{
	char *arg;

	if ((arg = next_arg(*args, args)))
	{
		malloc_strcpy(&window->prompt, arg);
		window_statusbar_needs_update(window);
	}
	else
		say("You must specify a prompt for the window!");

	return window;
}

static Window *window_push (Window *window, char **args)
{
	WindowStack *new_ws;

	new_ws = (WindowStack *) new_malloc(sizeof(WindowStack));
	/* XXX refnum is changed here XXX */
	new_ws->refnum = window->refnum;
	new_ws->next = window->screen->window_stack;
	window->screen->window_stack = new_ws;
	return window;
}

Window *window_query (Window *window, char **args)
{
	WNickList *tmp;
	const char *oldnick, *nick;
	char	  *a;
	int	l;

	nick = new_next_arg(*args, args);

	/*
	 * Nuke the old query list
	 */
	if ((oldnick = get_equery_by_refnum(window->refnum)))
	{
	    l = message_setall(window->refnum, who_from, who_level);
	    say("Ending conversation with %s", oldnick);
	    pop_message_from(l);

	    /* Only remove from nick lists if canceling the query */
	    if (!nick)
	    {
		a = LOCAL_COPY(oldnick);
		while (a && *a)
		{
			oldnick = next_in_comma_list(a, &a);
			if ((tmp = (WNickList *)remove_from_list(
					(List **)&window->nicks, oldnick)))
			{
				new_free(&tmp->nick);
				new_free((char **)&tmp);
			}
		}
	    }

	    recheck_queries(window);
	    window_statusbar_needs_update(window);
	}

	/* If we're not assigning a new query, then just punt here. */
	if (!nick)
		return window;

	if (!strcmp(nick, "."))
	{
		if (!(nick = get_server_sent_nick(window->server)))
			say("You have not messaged anyone yet");
	}
	else if (!strcmp(nick, ","))
	{
		if (!(nick = get_server_recv_nick(window->server)))
			say("You have not recieved a message yet");
	}
	else if (!strcmp(nick, "*") && 
		!(nick = get_echannel_by_refnum(0)))
	{
		say("You are not on a channel");
	}
	else if (*nick == '%')
	{
		if (is_valid_process(nick) == -1)
			nick = NULL;
	}

	if (!nick)
		return window;

	/*
	 * Create the new query list
	 * Ugh.  Make sure this goes to the RIGHT WINDOW!
	 */
	window_statusbar_needs_update(window);
	a = LOCAL_COPY(nick);
	while (a && *a)
	{
		nick = next_in_comma_list(a, &a);
		if (!window_claims_nickname(window->refnum, window->server, nick))
		{
			tmp = (WNickList *)new_malloc(sizeof(WNickList));
			tmp->nick = malloc_strdup(nick);
			add_to_list((List **)&window->nicks, (List *)tmp);
		}
		else
			tmp = (WNickList *)find_in_list((List **)&window->nicks, nick, !USE_WILDCARDS);

		tmp->counter = current_query_counter++;
		window->query_counter = tmp->counter;
	}

	l = message_setall(window->refnum, who_from, who_level);
	say("Starting conversation with %s", nick);
	pop_message_from(l);

	return window;
}

static Window *window_rebuild_scrollback (Window *window, char **args)
{
	window->rebuild_scrollback = 1;
	/* rebuild_scrollback(window); */
	return window;
}


/*
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
Window *window_rejoin (Window *window, char **args)
{
	char *	channels;
	const char *	chan;
	char *	keys = NULL;
	char *	newchan = NULL;

	/* First off, we have to be connected to join */
	if (from_server == NOSERV || !is_server_registered(from_server))
	{
		say("You are not connected to a server.");
		return window;
	}

	/* And the user must want to join something */
	/* Get the channels, and the keys. */
	if (!(channels = new_next_arg(*args, args)))
	{
		say("REJOIN: Must provide a channel argument");
		return window;
	}
	keys = new_next_arg(*args, args);

	/* Iterate over each channel name in the list. */
	while (*channels && (chan = next_in_comma_list(channels, &channels)))
	{
		/* Handle /join -i, which joins last invited channel */
		if (!my_strnicmp(chan, "-i", 2))
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
			    if (find_in_list((List **)&window->waiting_chans, chan, !USE_WILDCARDS))
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
				panic(1, "There are no windows for this server, "
				      "and there should be.");

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

	return window;
}

static Window *window_refnum (Window *window, char **args)
{
	Window *tmp;
	if ((tmp = get_window("REFNUM", args)))
	{
		window = tmp;
		make_window_current(tmp);
	/* XXX This is dangerous -- 'make_window_current' might nuke 'tmp' */
		if (tmp->screen)
		{
			set_screens_current_window(tmp->screen, tmp);
			window = tmp;
		}
	}
	else
		window = NULL;
	return window;
}

static Window *window_refnum_or_swap (Window *window, char **args)
{
	Window  *tmp;

	if (!(tmp = get_window("REFNUM_OR_SWAP", args)))
		return NULL;

	if (tmp->screen)
	{
		make_window_current(tmp);
	/* XXX This is dangerous -- 'make_window_current' might nuke 'tmp' */
		set_screens_current_window(tmp->screen, tmp);
	}
	else
		swap_window(window, tmp);

	return tmp;
}

static Window *window_refresh (Window *window, char **args)
{
	update_all_status();
	update_all_windows();
	return window;
}

static Window *window_remove (Window *window, char **args)
{
	char *arg;

	if ((arg = next_arg(*args, args)))
	{
		char		*ptr;
		WNickList 	*new_nl;

		while (arg)
		{
			if ((ptr = strchr(arg, ',')) != NULL)
				*ptr++ = 0;

			if ((new_nl = (WNickList *)remove_from_list((List **)&(window->nicks), arg)))
			{
				say("Removed %s from window name list", new_nl->nick);
				new_free(&new_nl->nick);
				new_free((char **)&new_nl);
			}
			else
				say("%s is not on the list for this window!", arg);

			arg = ptr;
		}
	        window_statusbar_needs_update(window);
	}
	else
		say("REMOVE: Do something!  Geez!");

	return window;
}

/* This is a NO-OP now -- every window is a scratch window */
static	Window *window_scratch (Window *window, char **args)
{
	short scratch = 0;

	if (get_boolean("SCRATCH", args, &scratch))
		return NULL;

	return window;
}

/* XXX - Need to come back and fix this... */
Window *window_scroll (Window *window, char **args)
{
	short 	scroll = 0;

	if (get_boolean("SCROLL", args, &scroll))
		return NULL;

	return window;
}

static Window *window_scrolladj (Window *window, char **args)
{
	if (get_boolean("SCROLLADJ", args, &window->scrolladj))
		return NULL;

	return window;
}

static Window *window_scroll_lines (Window *window, char **args)
{
	int new_value = 0;

	if (!args || !*args || !**args)
	{
		if (window->scroll_lines < 0)
			say("Window SCROLL_LINES is <default>");
		else
			say("Window SCROLL_LINES is %d", window->scroll_lines);
		return NULL;
	}

	new_value = get_number("SCROLL_LINES", args);
	if (new_value == 0 || new_value < -1)
	{
		say("Window SCROLL_LINES must be -1 or a positive value");
		return NULL;
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

	return window;
}

static	Window *window_scrollback (Window *window, char **args)
{
	int val;

	if (args && *args && **args)
	{
		val = get_number("SCROLLBACK", args);

		if (val < window->display_lines * 2)
			window->display_buffer_max = window->display_lines * 2;
		else
			window->display_buffer_max = val;
	}

	say("Window scrollback size set to %d", window->display_buffer_max);
	return window;
}


static	Window *window_scroll_backward (Window *window, char **args)
{
	int	val;

	if (args && *args && **args)
	{
		val = get_number("SCROLL_BACKWARD", args);

		if (val == 0)
			window_scrollback_backward(window);
		else
			window_scrollback_backwards_lines(window, val);
	}
	else
		window_scrollback_backward(window);

	return window;
}

static	Window *window_scroll_end (Window *window, char **args)
{
	window_scrollback_end(window);
	return window;
}

static	Window *window_scroll_forward (Window *window, char **args)
{
	int	val;

	if (args && *args && **args)
	{
		val = get_number("SCROLL_FORWARD", args);

		if (val == 0)
			window_scrollback_forward(window);
		else
			window_scrollback_forwards_lines(window, val);
	}
	else
		window_scrollback_forward(window);

	return window;
}

static	Window *window_scroll_start (Window *window, char **args)
{
	window_scrollback_start(window);
	return window;
}

regex_t *last_regex = NULL;

static int	new_search_term (const char *arg)
{
	int	errcode;

	if (last_regex)
		regfree(last_regex);
	else
		last_regex = new_malloc(sizeof(*last_regex));

	errcode = regcomp(last_regex, arg, 
				REG_EXTENDED | REG_ICASE | REG_NOSUB);
	if (errcode != 0)
	{
		char	errstr[1024];

		regerror(errcode, last_regex, errstr, sizeof(errstr));
		say("The regex [%s] isn't acceptable because [%s]", 
				arg, errstr);
		new_free((char **)&last_regex);
		return -1;
	}
	return 0;
}

static Window *window_search_back (Window *window, char **args)
{
	char *arg;

	if ((arg = new_next_arg(*args, args)))
	{
		if (new_search_term(arg))
			return window;
	}

	if (last_regex)
		window_scrollback_to_string(window, last_regex);
	else
		say("Need to know what to search for");

	return window;
}

static Window *window_search_forward (Window *window, char **args)
{
	char *arg;

	if ((arg = new_next_arg(*args, args)))
	{
		if (new_search_term(arg))
			return window;
	}

	if (last_regex)
		window_scrollforward_to_string(window, last_regex);
	else
		say("Need to know what to search for");

	return window;
}

static Window *window_skip (Window *window, char **args)
{
	if (get_boolean("SKIP", args, &window->skip))
		return NULL;

	return window;
}

Window *window_server (Window *window, char **args)
{
	char *	arg;

	if ((arg = next_arg(*args, args)))
	{
		int i;
		int status;

		if ((i = str_to_servref(arg)) == NOSERV)
			i = str_to_newserv(arg);
		from_server = i;

		/*
		 * Lose our channels
		 */
		destroy_window_waiting_channels(window->refnum);
		if (window->server != i)
			reassign_window_channels(window->refnum);

		/*
		 * Associate ourselves with the new server.
		 */
		window_change_server(window, i);
		status = get_server_status(i);

		if (status > SERVER_RECONNECT && status < SERVER_EOF)
		{
		    if (old_server_lastlog_mask) {
			window->window_mask = *old_server_lastlog_mask;
			revamp_window_masks(window);
		    }
		}
		else if (status >= SERVER_CLOSING)
			set_server_status(i, SERVER_RECONNECT);
	}
	else
		display_server_list();

	return window;
}

static Window *window_show (Window *window, char **args)
{
	Window *tmp;

	if ((tmp = get_window("SHOW", args)))
	{
		show_window(tmp);
		window = current_window;
	}
	return window;
}

static Window *window_show_all (Window *window, char **args)
{
	while (invisible_list)
		show_window(invisible_list);
	return window;
}

static Window *window_shrink (Window *window, char **args)
{
	resize_window(RESIZE_REL, window, -get_number("SHRINK", args));
	return window;
}

static Window *window_size (Window *window, char **args)
{
	char *	ptr = *args;
	int	number;

	number = parse_number(args);
	if (ptr == *args) 
		say("Window size is %d", window->display_lines);
	else
		resize_window(RESIZE_ABS, window, number);

	return window;
}

/*
 * This lists the windows that are on the stack, cleaning up any
 * bogus entries on the way.
 */
static Window *window_stack (Window *window, char **args)
{
	WindowStack 	*last, *tmp, *holder;
	Window 		*win = NULL;
	size_t		len = 4;

	while (traverse_all_windows(&win))
	{
		if (win->name && (strlen(win->name) > len))
			len = strlen(win->name);
	}

	say("Window stack:");
	last = NULL;
	tmp = window->screen->window_stack;
	while (tmp)
	{
		if ((win = get_window_by_refnum(tmp->refnum)) != NULL)
		{
			list_a_window(win, len);
			last = tmp;
			tmp = tmp->next;
		}
		else
		{
			holder = tmp->next;
			new_free((char **)&tmp);
			if (last)
				last->next = holder;
			else
				window->screen->window_stack = holder;

			tmp = holder;
		}
	}

	return window;
}

static Window *window_status_format (Window *window, char **args)
{
	char	*arg;

	arg = new_next_arg(*args, args);
	malloc_strcpy(&window->status.line[0].raw, arg);
	window_statusbar_needs_redraw(window);
	rebuild_a_status(window);

	return window;
}

static Window *window_status_format1 (Window *window, char **args)
{
	char	*arg;

	arg = new_next_arg(*args, args);
	malloc_strcpy(&window->status.line[1].raw, arg);
	window_statusbar_needs_redraw(window);
	rebuild_a_status(window);

	return window;
}

static Window *window_status_format2 (Window *window, char **args)
{
	char	*arg;

	arg = new_next_arg(*args, args);
	malloc_strcpy(&window->status.line[2].raw, arg);
	window_statusbar_needs_redraw(window);
	rebuild_a_status(window);

	return window;
}

static Window *window_status_special (Window *window, char **args)
{
	char *arg;

	arg = new_next_arg(*args, args);
	malloc_strcpy(&window->status.special, arg);
	window_statusbar_needs_redraw(window);

	return window;
}

static Window *window_swap (Window *window, char **args)
{
	Window *tmp;

	if ((tmp = get_invisible_window("SWAP", args)))
		swap_window(window, tmp);

	return current_window;
}

static Window *window_swappable (Window *window, char **args)
{
	if (get_boolean("SWAPPABLE", args, &window->swappable))
		return NULL;

	window_statusbar_needs_update(window);
	return window;
}

/*
 * /WINDOW TOPLINE "...."
 * This "saves" the top line of the window from being scrollable.  It sets 
 * the data in this line to whatever the argument is, or if the argument is
 * just a hyphen, it "unsaves" the top line.
 */
static Window *window_topline (Window *window, char **args)
{
	int	line;
	const char *linestr;
	const char *topline;

	if (!(linestr = new_next_arg(*args, args)))
		return window;
	if (!is_number(linestr))
	{
		say("Usage: /WINDOW TOPLINE <number> \"<string>\"");
		return window;
	}
	line = my_atol(linestr);
	if (line <= 0 || line >= 10)
	{
		say("/WINDOW TOPLINE number must be 1 to 9.");
		return window;
	}

	if (!(topline = new_next_arg(*args, args)))
		malloc_strcpy(&window->topline[line - 1], empty_string);
	else if (!strcmp(topline, "-"))
		new_free(&window->topline[line - 1]);
	else
		malloc_strcpy(&window->topline[line - 1], topline);

	window_body_needs_redraw(window);
	return window;
}

static Window *window_toplines (Window *window, char **args)
{
	char *	ptr = *args;
	int	number;
	int	saved = window->toplines_wanted;

	number = parse_number(args);
	if (ptr == *args) 
	{
		say("Window saved lines is %d", window->toplines_wanted);
		return window;
	}
	if (number < 0 || number >= 10)
	{
		say("Window saved lines must be < 10 for now.");
		return window;
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
	return window;
}


typedef Window *(*window_func) (Window *, char **args);

typedef struct window_ops_T {
	const char 	*command;
	window_func 	func;
} window_ops;

static const window_ops options [] = {
	{ "ADD",		window_add 		},
	{ "BACK",		window_back 		},
	{ "BALANCE",		window_balance 		},
	{ "BEEP_ALWAYS",	window_beep_always 	},
	{ "CHANNEL",		window_channel 		},
	{ "CLEAR",		window_clear		},
	{ "CREATE",		window_create 		},
	{ "DELETE",		window_delete 		},
	{ "DESCRIBE",		window_describe		}, /* * */
	{ "DISCON",		window_discon		},
	{ "DOUBLE",		window_double 		},
	{ "ECHO",		window_echo		},
	{ "FIXED",		window_fixed		},
	{ "FLUSH",		window_flush		},
	{ "FLUSH_SCROLLBACK",	window_flush_scrollback	},
	{ "GOTO",		window_goto 		},
	{ "GROW",		window_grow 		},
	{ "HIDE",		window_hide 		},
	{ "HIDE_OTHERS",	window_hide_others 	},
	{ "HOLD_INTERVAL",	window_hold_interval	},
	{ "HOLD_MODE",		window_hold_mode 	},
	{ "HOLD_SLIDER",	window_hold_slider	},
	{ "INDENT",		window_indent 		},
	{ "KILL",		window_kill 		},
	{ "KILL_ALL_HIDDEN",	window_kill_all_hidden	},
	{ "KILL_OTHERS",	window_kill_others 	},
	{ "KILLABLE",		window_killable		},
	{ "KILLSWAP",		window_killswap 	},
	{ "LAST", 		window_last 		},
	{ "LASTLOG",		window_lastlog 		},
	{ "LASTLOG_LEVEL",	window_lastlog_mask 	},
	{ "LEVEL",		window_level		},
	{ "LIST",		window_list 		},
	{ "LOG",		window_log 		},
	{ "LOGFILE",		window_logfile 		},
	{ "MOVE",		window_move 		},
	{ "MOVE_TO",		window_move_to		},
	{ "NAME",		window_name 		},
	{ "NEW",		window_new 		},
	{ "NEW_HIDE",		window_new_hide		}, /* * */
	{ "NEXT",		window_next 		},
	{ "NOSERV",		window_discon		},
	{ "NOTIFIED",		window_notify_list 	},
	{ "NOTIFY",		window_notify 		},
	{ "NOTIFY_LEVEL",	window_notify_mask 	},
	{ "NOTIFY_NAME",	window_notify_name 	},
	{ "NUMBER",		window_number 		},
	{ "POP",		window_pop 		},
	{ "PREVIOUS",		window_previous 	},
	{ "PROMPT",		window_prompt 		},
	{ "PUSH",		window_push 		},
	{ "QUERY",		window_query		},
	{ "REBUILD_SCROLLBACK",	window_rebuild_scrollback },
	{ "REFNUM",		window_refnum 		},
	{ "REFNUM_OR_SWAP",	window_refnum_or_swap	},
	{ "REFRESH",		window_refresh		},
	{ "REJOIN",		window_rejoin		},
	{ "REMOVE",		window_remove 		},
	{ "SCRATCH",		window_scratch		},
	{ "SCROLL",		window_scroll		},
	{ "SCROLLADJ",		window_scrolladj	},
	{ "SCROLLBACK",		window_scrollback	}, /* * */
	{ "SCROLL_BACKWARD",	window_scroll_backward	},
	{ "SCROLL_END",		window_scroll_end	},
	{ "SCROLL_FORWARD",	window_scroll_forward	},
	{ "SCROLL_LINES",	window_scroll_lines	},
	{ "SCROLL_START",	window_scroll_start	},
	{ "SEARCH_BACK",	window_search_back	},
	{ "SEARCH_FORWARD",	window_search_forward	},
	{ "SERVER",		window_server 		},
	{ "SHOW",		window_show 		},
	{ "SHOW_ALL",		window_show_all		}, /* * */
	{ "SHRINK",		window_shrink 		},
	{ "SIZE",		window_size 		},
	{ "SKIP",		window_skip		},
	{ "STACK",		window_stack 		},
	{ "STATUS_FORMAT",	window_status_format	},
	{ "STATUS_FORMAT1",	window_status_format1	},
	{ "STATUS_FORMAT2",	window_status_format2	},
	{ "STATUS_SPECIAL",	window_status_special	},
	{ "SWAP",		window_swap 		},
	{ "SWAPPABLE",		window_swappable	},
	{ "TOPLINE",		window_topline		},
	{ "TOPLINES",		window_toplines		},
	{ NULL,			NULL 			}
};

BUILT_IN_COMMAND(windowcmd)
{
	char 	*arg;
	int 	nargs = 0;
	Window 	*window;
	int	old_status_update, old_from_server;
	unsigned	old_current_window;
	int	winref;
	int	l;

	old_from_server = from_server;
	old_current_window = current_window->refnum;
	old_status_update = permit_status_update(0);
	/* l = message_from(NULL, LEVEL_NONE); */	/* XXX This is bogus */
	window = current_window;

	while ((arg = next_arg(args, &args)))
	{
		int i;
		int len = strlen(arg);

		if (*arg == '-' || *arg == '/')		/* Ignore - or / */
			arg++, len--;

		l = message_setall(window ? (int)window->refnum : -1, 
					who_from, who_level);

		for (i = 0; options[i].func ; i++)
		{
			if (!my_strnicmp(arg, options[i].command, len))
			{
				if (window)
					from_server = window->server;
				winref = window ? (int)window->refnum : -1;
				window = options[i].func(window, &args); 
				nargs++;
				if (!window)
					args = NULL;
				do_hook(WINDOW_COMMAND_LIST, "%d %d %s", 
					winref, 
					window ? (int)window->refnum : -1,
					arg);
				break;
			}
		}

		if (!options[i].func)
		{
			Window *s_window;
			nargs++;

			if ((s_window = get_window_by_desc(arg)))
				window = s_window;
			else
			{
				yell("WINDOW: Invalid window or option: [%s]", arg);
				/* XXX Maybe this should fail? */
				args = NULL;
			}
		}

		pop_message_from(l);
	}

	if (!nargs)
		window_describe(current_window, NULL);

	/*
	 * "from_server" changes only if "current_window" changes.  So if
	 * the latter has not changed, then reset from_server to its orig
	 * value.  Otherwise, set it to the new current window's server!
	 */
	if (current_window && current_window->refnum != old_current_window)
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
void	update_scrollback_indicator (Window *w)
{
}

/*
 * remove_scrollback_indicator removes the indicator from the scrollback, 
 * making it available for placement somewhere else.  This does the whole
 * shebang: finding it, removing it, and cleansing it.
 */
void	remove_scrollback_indicator (Window *w)
{
}

/*
 * window_indicator_is_visible returns 1 if the window's indicator is in
 * the scrollback and 0 if it is not in use.  It's important to call
 * cleanse_indicator() when the indicator is removed from scrollback or 
 * this function will break.
 */
void	window_indicator_is_visible (Window *w)
{
}

/*
 * cleanse_indicator clears the values in the indicator and makes it available
 * for reuse.  If you haven't unlinked it from the scrollback it does that 
 * for you.  It's important to cleanse the indicator because it does things
 * that are used by window_indicator_is_visible().
 */
void	cleanse_indicator (Window *w)
{
}

/*
 * indicator_needs_update tells you when you do a scrollback whether the 
 * indicator needs to be moved further down the scrollback or not.  If the
 * indicator is not being used or if it is above your current view, then it
 * does need to be moved down.  Otherwise it does not need to be moved.
 */ 
void	indicator_needs_update (Window *w)
{
}

/*
 * go_back_to_indicator will return the scrollback view to where the
 * indicator is and then do a full recalculation.
 */
void	go_back_to_indicator (Window *w)
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
		panic(1, "recycle == stuff is bogus");
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
	return stuff;
}

/*
 * This function adds an item to the window's scrollback.  If the item
 * should be displayed on the screen, then 1 is returned.  If the item is
 * not to be displayed, then 0 is returned.  This function handles all
 * the hold_mode stuff.
 */
int 	add_to_scrollback (Window *window, const unsigned char *str, intmax_t refnum)
{
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
	    size_t lines_held;
	    lines_held = window->holding_distance_from_display_ip - window->display_lines;
	    if (lines_held > 0)
		window_statusbar_needs_update(window);
	}

	/*
	 * Handle overflow in the scrollback view -- If the scrollback view
	 * has overflowed, update the status bar in case %K changes.
	 */
	if (window->scrollback_top_of_display)
	{
	    size_t lines_held;
	    lines_held = window->scrollback_distance_from_display_ip - window->display_lines;
	    if (lines_held > 0)
		window_statusbar_needs_update(window);
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

		for (i = 0; i < scroll; i++)
		{
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
int	trim_scrollback (Window *window)
{
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
	window_body_needs_redraw(w);
	window_statusbar_needs_update(w);
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
 * Scroll backwards from the last scrollback point, the last hold point,
 * or the standard place.
 */
static void 	window_scrollback_backwards_lines (Window *window, int my_lines)
{
	Display *new_top;
	int	new_lines;

	if (window->scrollback_top_of_display == window->top_of_scrollback)
	{
		term_beep();
		return;
	}

	if (window->scrollback_top_of_display == NULL)
	{
	    if (window->holding_distance_from_display_ip > window->scrolling_distance_from_display_ip)
		window->scrollback_top_of_display = window->holding_top_of_display;
	    else
		window->scrollback_top_of_display = window->scrolling_top_of_display;
	}

/*
 *	if (!window->scrollback_hint)
 *		insert_scrollback_hint(window);
 */

	new_top = window->scrollback_top_of_display;
	for (new_lines = 0; new_lines < my_lines; new_lines++)
	{
		if (new_top == window->top_of_scrollback)
			break;
		new_top = new_top->prev;
	}

	window->scrollback_top_of_display = new_top;
	recalculate_window_cursor_and_display_ip(window);
	window_body_needs_redraw(window);
	window_statusbar_needs_update(window);
}

/*
 * Scroll the scrollback (or hold mode) forward.
 */
static void 	window_scrollback_forwards_lines (Window *window, int my_lines)
{
	Display *new_top;
	int	unholding;
	int	new_lines = 0;

	if (window->scrollback_top_of_display)
	{
		new_top = window->scrollback_top_of_display;
		unholding = 0;
	}
	else if (window->holding_top_of_display)
	{
		new_top = window->holding_top_of_display;
		unholding = 1;
	}
	else
	{
		term_beep();
		return;
	}

	for (new_lines = 0; new_lines < my_lines; new_lines++)
	{
	    if (new_top == window->display_ip)
		break;
	    new_top = new_top->next;
	}

	if (!unholding)
		window->scrollback_top_of_display = new_top;
	else
		window->holding_top_of_display = new_top;

	recalculate_window_cursor_and_display_ip(window);
	window_body_needs_redraw(window);
	window_statusbar_needs_update(window);
}

static void 	window_scrollback_to_string (Window *window, regex_t *preg)
{
	Display *new_top;

	if (window->scrollback_top_of_display)
		new_top = window->scrollback_top_of_display;
	else if (window->holding_distance_from_display_ip > window->scrolling_distance_from_display_ip)
		new_top = window->holding_top_of_display;
	else
		new_top = window->scrolling_top_of_display;

	while (new_top != window->top_of_scrollback)
	{
		/* Always move up a line before searching */
		new_top = new_top->prev;

		if (regexec(preg, new_top->line, 0, NULL, 0) == 0)
		{
			window->scrollback_top_of_display = new_top;
			recalculate_window_cursor_and_display_ip(window);
			window_body_needs_redraw(window);
			window_statusbar_needs_update(window);
			return;
		}
	}

	term_beep();
}

static void 	window_scrollforward_to_string (Window *window, regex_t *preg)
{
	Display *new_top;

	if (window->scrollback_top_of_display)
		new_top = window->scrollback_top_of_display;
	else if (window->holding_distance_from_display_ip > window->scrolling_distance_from_display_ip)
		new_top = window->holding_top_of_display;
	else
		new_top = window->scrolling_top_of_display;

	while (new_top->next != window->display_ip)
	{
		/* Always move up a line before searching */
		new_top = new_top->next;

		if (regexec(preg, new_top->line, 0, NULL, 0) == 0)
		{
			window->scrollback_top_of_display = new_top;
			recalculate_window_cursor_and_display_ip(window);
			window_body_needs_redraw(window);
			window_statusbar_needs_update(window);
			return;
		}
	}

	term_beep();
}

static void	window_scrollback_start (Window *window)
{
	/* XXX Ok.  So maybe 999999 *is* a magic number. */
	window_scrollback_backwards_lines(window, 999999);
}

/*
 * Cancel out scrollback and holding stuff so you're left pointing at the
 * "standard" place.  Doesn't turn hold mode off, obviously.
 */
static void	window_scrollback_end (Window *window)
{
	window_scrollback_forwards_lines(window, 999999);
}



/* * * * * * * * */
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



/* HOLD MODE STUFF */
BUILT_IN_KEYBINDING(unstop_all_windows)
{
	Window	*tmp = NULL;
	char	my_off[4];
	char *	ptr;

	while (traverse_all_windows(&tmp))
	{
		strcpy(my_off, "OFF");
		ptr = my_off;
		window_hold_mode(tmp, (char **)&ptr);
	}
	update_all_windows();
}

/* toggle_stop_screen: the BIND function TOGGLE_STOP_SCREEN */
BUILT_IN_KEYBINDING(toggle_stop_screen)
{
	char toggle[7], *p = toggle;

	strlcpy(toggle, "TOGGLE", sizeof toggle);
	window_hold_mode(current_window, (char **)&p);
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
void 	recalculate_window_cursor_and_display_ip (Window *window)
{
	Display *tmp;

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
 * Its a bad idea to directly assign screen->current_window!
 */
static void 	set_screens_current_window (Screen *screen, Window *window)
{
	if (!window)
	{
		window = get_window_by_refnum(screen->last_window_refnum);

		/* Cant use a window that is now on a different screen */
		/* Check check a window that doesnt exist, too! */
		if (window && window->screen != screen)
			window = NULL;
	}
	if (!window)
		window = screen->window_list;

	if (window->deceased)
		panic(1, "This window is dead.");
	if (window->screen != screen)
		panic(1, "The window is not on that screen.");
	if (!screen)
		panic(1, "Cannot set the invisible screen's current window.");

	if (screen->current_window != window)
	{
		if (screen->current_window)
		{
			window_statusbar_needs_update(screen->current_window);
			screen->last_window_refnum = screen->current_window->refnum;
		}
		screen->current_window = window;
		window_statusbar_needs_update(screen->current_window);
	}
	if (current_window != window)
		make_window_current(window);

	window->priority = current_window_priority++;
}

void	make_window_current_by_refnum (int refnum)
{
	Window	*new_win;

	if (refnum == -1)
		return;

	if ((new_win = get_window_by_refnum(refnum)))
		make_window_current(new_win);
	else
		say("Window [%d] doesnt exist any more.  Punting.", refnum);
}


/*
 * This is used to make the specified window the current window.  This
 * is preferable to directly doing the assign, because it can deal with
 * finding a current window if the old one has gone away.
 */
void 	make_window_current (Window *window)
{
	Window *old_current_window = current_window;
	int	old_screen, old_window;
	int	new_screen, new_winref;

	if (!window)
		current_window = last_input_screen->current_window;
	else if (current_window != window)
		current_window = window;

	if (current_window == NULL)
		current_window = last_input_screen->window_list;

	if (current_window == NULL)
		current_window = main_screen->window_list;

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

	new_winref = current_window->refnum;
	if (!current_window->screen)
		new_screen = -1;
	else
		new_screen = current_window->screen->screennum;

	do_hook(SWITCH_WINDOWS_LIST, "%d %d %d %d",
		old_screen, old_window,
		new_screen, new_winref);
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

void	check_window_cursor (Window *window)
{
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

	GET_FUNC_ARG(listc, input);
	len = strlen(listc);
	if (!my_strnicmp(listc, "REFNUM", len)) {
	    char *windesc;

	    GET_FUNC_ARG(windesc, input);
	    if (!(w = get_window_by_desc(windesc)))
		RETURN_EMPTY;
	    RETURN_INT(w->refnum);
	} else if (!my_strnicmp(listc, "REFNUMS", len)) {
		w = NULL;
		while (traverse_all_windows(&w))
		    malloc_strcat_wordlist(&ret, space, ltoa(w->refnum));
		RETURN_MSTR(ret);
	} else if (!my_strnicmp(listc, "NEW", len)) {
	    if ((w = new_window(current_window->screen))) {
	        make_window_current(w);
		RETURN_INT(w->refnum);
	    }
	    else
		RETURN_INT(-1);
	} else if (!my_strnicmp(listc, "NEW_HIDE", len)) {
	    if ((w = new_window(NULL)))
		RETURN_INT(w->refnum);
	    else
		RETURN_INT(-1);
	} else if (!my_strnicmp(listc, "GET", len)) {
	    GET_INT_ARG(refnum, input);
	    if (!(w = get_window_by_refnum(refnum)))
		RETURN_EMPTY;

	    GET_FUNC_ARG(listc, input);
	    len = strlen(listc);

	    if (!my_strnicmp(listc, "REFNUM", len)) {
		RETURN_INT(w->refnum);
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
	    } else if (!my_strnicmp(listc, "PROMPT", len)) {
		RETURN_STR(w->prompt);
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
		RETURN_STR(empty_string);
	    } else if (!my_strnicmp(listc, "QUERY_NICK", len)) {
		const char *cc = get_equery_by_refnum(w->refnum);
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
	    } else
		RETURN_EMPTY;
	} else if (!my_strnicmp(listc, "SET", len)) {
	    GET_INT_ARG(refnum, input);
	    if (!(w = get_window_by_refnum(refnum)))
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
		window_body_needs_redraw(w);
		RETURN_INT(1);
	    } else if (!my_strnicmp(listc, "TOPLINES", len)) {
		RETURN_EMPTY;
	    } else if (!my_strnicmp(listc, "ACTIVITY_FORMAT", len)) {
		int line;

		GET_INT_ARG(line, input)
		if (line < 0 || line > 10)
			RETURN_EMPTY;
		malloc_strcpy(&w->activity_format[line], input);
		window_statusbar_needs_update(w);
		RETURN_INT(1);
	    } else if (!my_strnicmp(listc, "ACTIVITY_DATA", len)) {
		int line;

		GET_INT_ARG(line, input)
		if (line < 0 || line > 10)
			RETURN_EMPTY;
		malloc_strcpy(&w->activity_data[line], input);
		window_statusbar_needs_update(w);
		RETURN_INT(1);
	    } else if (!my_strnicmp(listc, "CURRENT_ACTIVITY", len)) {
		int	line;

		GET_INT_ARG(line, input)
		if (line < 0 || line > 10)
			RETURN_EMPTY;
		w->current_activity = line;
		window_statusbar_needs_update(w);
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
	Window *w;

	for (w = s->window_list; w; w = w->next)
		if (w->fixed_size && w->skip)
			count++;

	return count;
}

void	window_change_server (Window * win, int server) 
{
    int oldserver;

    oldserver = win->server;
    win->server = server;
    do_hook(WINDOW_SERVER_LIST, "%u %d %d", win->refnum, oldserver, server);
}


