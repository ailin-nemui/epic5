/*
 * window.c: Handles the organzation of the logical viewports (``windows'')
 * for irc.  This includes keeping track of what windows are open, where they
 * are, and what is on them.
 *
 * Copyright 1990 Michael Sandrof
 * Copyright 1997 EPIC Software Labs
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 */

#include "irc.h"
#include "screen.h"
#include "window.h"
#include "vars.h"
#include "server.h"
#include "list.h"
#include "term.h"
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

static char *onoff[] = { "OFF", "ON" };

/* Resize relatively or absolutely? */
#define RESIZE_REL 1
#define RESIZE_ABS 2

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
	int	who_level = LOG_CRAP;

/*
 * This is set when we enter the /window command, and cleared when we exit.
 * It is possible for output to occur as a result of a /window command, but
 * the state of the windows may be indeterminite (because they changed size
 * or location perhaps), so when output occurs and this is set, a full update
 * of all the windows occurs, before the output, so that everything goes to
 * wherever it belongs.
 */ 
	int	in_window_command = 0;

/*
 * This is set to 1 if output is to be dispatched normally.  This is set to
 * 0 if all output is to be suppressed (such as when the system wants to add
 * and alias and doesnt want to blab to the user, or when you use ^ to
 * suppress the output of a command.)
 */
	unsigned window_display = 1;

/*
 * This is set to 1 if the status bar may be updated immediately.  This is
 * set to 0 if all status updates should be suppressed.  You need to remember
 * to set it back to 1 before the updates will take effect. ;-)
 */
	int	status_update_flag = 1;

static 	void 	remove_from_invisible_list 	(Window *);
static 	void 	swap_window 			(Window *, Window *);
static	Window	*get_next_window  		(Window *);
static	Window	*get_previous_window 		(Window *);
static 	void 	revamp_window_levels 		(Window *);
static	void 	clear_window 			(Window *);
static	void	resize_window_display 		(Window *);
static 	Window *window_next 			(Window *, char **);
static 	Window *window_previous 		(Window *, char **);
static	void 	set_screens_current_window 	(Screen *, Window *);
static	void 	remove_window_from_screen (Window *window, int hide);
static Window *window_discon (Window *window, char **args);


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
	new_w->name = NULL;

	if (current_window)
		new_w->server = current_window->server;
	else
		new_w->server = -1;
	new_w->last_server = -1;

	new_w->top = 0;			/* Filled in later */
	new_w->bottom = 0;		/* Filled in later */
	new_w->cursor = 0;		/* Filled in later */
	new_w->absolute_size = 0;
	new_w->line_cnt = 0;
	new_w->old_size = 1;		/* Filled in later */
	new_w->visible = 1;
	new_w->update = 0;
	new_w->miscflags = 0;
	new_w->beep_always = 0;
	new_w->scratch_line = -1;
	new_w->noscroll = 0;
	new_w->skip = 0;
	new_w->notify_level = real_notify_level();
	if (!current_window)		/* First window ever */
		new_w->window_level = LOG_ALL;
	else
		new_w->window_level = LOG_NONE;

	new_w->prompt = NULL;		/* Filled in later */
	for (i = 0; i < 3; i++)
	{
		new_w->status.line[i].raw = NULL;
		new_w->status.line[i].format = NULL;
		new_w->status.line[i].count = 0;
		new_w->status.line[i].result = NULL;
	}
	new_w->status.double_status = 0;
	new_w->status.special = NULL;
	rebuild_a_status(new_w);

	new_w->top_of_scrollback = NULL;	/* Filled in later */
	new_w->top_of_display = NULL;		/* Filled in later */
	new_w->ceiling_of_display = NULL;	/* Filled in later */
	new_w->display_ip = NULL;		/* Filled in later */
	new_w->scrollback_point = NULL;
	new_w->display_buffer_size = 0;
	new_w->display_buffer_max = get_int_var(SCROLLBACK_VAR);
	new_w->display_size = 1;		/* Filled in later */
	new_w->lines_scrolled_back = 0;
	new_w->distance_from_display = 0;
	new_w->repaint_start = 0;
	new_w->repaint_end = -1;

	new_w->hold_mode = get_int_var(HOLD_MODE_VAR);
	new_w->holding_something = 0;
	new_w->held_displayed = 0;
	new_w->lines_displayed = 0;
	new_w->lines_held = 0;
	new_w->last_lines_held = 0;
	new_w->hold_interval = get_int_var(HOLD_INTERVAL_VAR);

	new_w->waiting_channel = NULL;
	new_w->current_channel = NULL;
	new_w->bind_channel = NULL;
	new_w->query_nick = NULL;
	new_w->nicks = NULL;

	new_w->lastlog_oldest = NULL;
	new_w->lastlog_newest = NULL;
	new_w->lastlog_level = real_lastlog_level();
	new_w->lastlog_size = 0;
	new_w->lastlog_max = get_int_var(LASTLOG_VAR);

	new_w->log = 0;
	new_w->logfile = NULL;
	new_w->log_fp = NULL;

	new_w->screen = screen;
	new_w->next = new_w->prev = NULL;

	new_w->deceased = 0;

	if (screen)
	{
		/*
		 * Add_to_window_list sets the location and size of the window
		 */
		if (add_to_window_list(screen, new_w))
			set_screens_current_window(screen, new_w);
		else
		{
			new_free((char **)&new_w);
			return NULL;
		}
	}
	else
		add_to_invisible_list(new_w);

	/*
	 * We have to know how big the window is before we can fill in 
	 * the display...
	 */
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
	int	i;
	int	invisible = 0;

	if (!window)
		window = current_window;

	if (!window->screen)
		invisible = 1;

	/*
	 * If this is the last window on a screen, and the client is not
	 * exiting and there are no invisible windows, then /window kill
	 * here is an error.  Tell the user and abort.
	 */
	if ((dead == 0) && window->screen && 
	    (window->screen->visible_windows == 1) && !invisible_list)
	{
		say("You can't kill the last window!");
		return;
	}

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
			panic("My screen says there is only one "
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
	reassign_window_channels(window);

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
	else if (window->screen->visible_windows > 1)
		remove_window_from_screen(window, 0);
	else if (window->screen->visible_windows == 1)
		swap_window(window, invisible_list);
	else
		panic("I don't know how to unlink this window!");

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
			panic("I am not on that screen");
		    else
			make_window_current(last_input_screen->window_list);
		}
		else
			make_window_current(NULL);
	}
	if (window == current_window)
		panic("window == current_window -- this is wrong.");

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
		strmcpy(buffer, window->name, BIG_BUFFER_SIZE);
	else
		strmcpy(buffer, ltoa(window->refnum), BIG_BUFFER_SIZE);


	/*
	 * Clean up after the window's internal data.
	 */
	/* Status bars... */
	for (i = 0; i < 3; i++)
	{
		new_free(&window->status.line[i].raw);
		new_free(&window->status.line[i].format);
		new_free(&window->status.line[i].result);
		window->status.double_status = 0;
		new_free(&window->status.special);
	}

	/* Various things... */
	new_free(&window->query_nick);
	new_free(&window->current_channel);
	new_free(&window->waiting_channel);
	new_free(&window->bind_channel);
	new_free(&window->logfile);
	new_free(&window->name);

	/* The logical display */
	{ 
		Display *next;
		while (window->top_of_scrollback)
		{
			next = window->top_of_scrollback->next;
			new_free(&window->top_of_scrollback->line);
			new_free((char **)&window->top_of_scrollback);
			window->display_buffer_size--;
			window->top_of_scrollback = next;
		}
		window->display_ip = NULL;
		if (window->display_buffer_size != 0)
			panic("display_buffer_size is %d, should be 0", 
				window->display_buffer_size);
	}

	/* The lastlog... */
	while (window->lastlog_size)
		remove_from_lastlog(window);

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
	if (dead == 0)
		window_check_servers();
	do_hook(WINDOW_KILL_LIST, "%s", buffer);
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

/* * * * * * * * * * * * * * * * ITERATE OVER WINDOWS * * * * * * * * * * */
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
		panic("This window is _not_ invisible");

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
	window->visible = 0;
	if (window->screen)
		window->columns = window->screen->co;
	else
		window->columns = current_term->TI_cols;	/* Whatever */
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
		panic("Cannot add window [%d] to NULL screen.", new_w->refnum);

	screen->visible_windows++;
	new_w->screen = screen;
	new_w->visible = 1;
	new_w->miscflags &= ~WINDOW_NOTIFIED;

	/*
	 * If this is the first window to go on the screen
	 */
	if (!screen->current_window)
	{
		screen->window_list_end = screen->window_list = new_w;
		if (dumb_mode)
		{
			new_w->display_size = 24;
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
		if ((screen->current_window->display_size < 4) ||
				get_int_var(ALWAYS_SPLIT_BIGGEST_VAR))
		{
			int	size = 0;

			for (tmp = screen->window_list; tmp; tmp = tmp->next)
			{
				if (tmp->absolute_size)
					continue;
				if (tmp->display_size > size)
				{
					size = tmp->display_size;
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
		biggest->display_size /= 2;
		new_w->display_size = biggest->display_size;
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

	if (!window->visible || !((s = window->screen)))
		panic("This window is not on a screen");

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


/* * * * * * * * * * * * SIZE AND LOCATION PRIMITIVES * * * * * * * */
/*
 * recalculate_window_positions: This runs through the window list and
 * re-adjusts the top and bottom fields of the windows according to their
 * current positions in the window list.  This doesn't change any sizes of
 * the windows 
 */
void	recalculate_window_positions (Screen *screen)
{
	Window	*tmp;
	int	top;

	if (!screen)
		return;		/* Window is hidden.  Dont bother */

	top = 0;
	for (tmp = screen->window_list; tmp; tmp = tmp->next)
	{
		tmp->update |= REDRAW_DISPLAY_FULL | REDRAW_STATUS;
		tmp->top = top;
		tmp->bottom = top + tmp->display_size;
		top += tmp->display_size + 1 + tmp->status.double_status;
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
	/*
	 * v_window -- window to be swapped out
	 * window -- window to be swapped in
	 */

	if (!window)
	{
		say("The window to be swapped in does not exist.");
		return;
	}

	if (window->visible || !v_window->visible)
	{
		say("You can only SWAP a hidden window with a visible window.");
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
	window->top = v_window->top;
	window->display_size = v_window->display_size + 
			       v_window->status.double_status - 
				window->status.double_status;
	window->bottom = window->top + window->display_size;
	window->visible = 1;
	window->screen = v_window->screen;
	if (v_window->screen->current_window == v_window)
		v_window->screen->current_window = window;

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

	/*
	 * Transfer current_window if the current window is being swapped out
	 */
	if (v_window == current_window)
		make_window_current(window);

	/*
	 * And recalculate the window's positions.
	 */
	window->update |= REDRAW_DISPLAY_FULL | REDRAW_STATUS;
	window->miscflags &= ~WINDOW_NOTIFIED;
	update_input(UPDATE_ALL);
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
	return;
#if 0
	Window	*tmp,
		*last;
	int	count;
	int	pos;

	if (offset <= 0)
		return;

	if (!window->screen)
		return;		/* Whatever */

	last = NULL;

	/*
	 * First, remove the window from the screen's list
	 */
	if (window->prev)
		window->prev->next = window->next;
	else
		window->screen->window_list = window->next;

	if (window->next)
		window->next->prev = window->prev;

	/*
	 * Reset the pointers, just to be safe
	 */
	window->next = NULL;
	window->prev = NULL;

	tmp = window->screen->window_list;
	for (count = 1; count < offset; count++)
		tmp = tmp->next;

	/*
	 * Now 'tmp' points at the window we must put AFTER this window.
	 */
	if (tmp->prev)
	{
		window->prev = tmp->prev;
		tmp->prev->next = window;
	}
	else
		window->screen->window_list = window;
	tmp->prev = window;

	/*
	 * Now walk off the positions from the top, starting with 1
	 */
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

	if (offset < 0)
		win_pos = (window->screen->visible_windows + offset + win_pos) %
		    window->screen->visible_windows;
	else
		win_pos = (offset + win_pos) % window->screen->visible_windows;

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
#endif
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

	if (!window->visible)
	{
		say("You cannot change the size of hidden windows!");
		return;
	}

	if (how == RESIZE_ABS)
	{
		offset -= window->display_size;
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

		if (other->absolute_size)
			continue;
	}
	while (other->display_size < offset);

	window_size = window->display_size + offset;
	other_size = other->display_size - offset;

#if 0
	if (how == RESIZE_REL)
	{
		window_size = window->display_size + offset;
		other_size = other->display_size - offset;
	}
	else /* absolute size */
	{
		/* 
		 * How much its growing/shrinking by.  if
		 * offset > display_size, then window_size < 0.
		 * and other window is shrinking.  If offset < display_size,
		 * the window_size > 0, and other_window is growing.
		 */
		window_size = offset;
		offset -= window->display_size;
		other_size = other->display_size - offset;
	}
#endif

	if ((window_size < 0) || (other_size < 0))
	{
		say("Not enough room to resize this window!");
		return;
	}

	window->display_size = window_size;
	other->display_size = other_size;
	recalculate_windows(window->screen);
}

/*
 * resize_display: After determining that the window has changed sizes, this
 * goes through and adjusts the top of the display.  If the window grew, then
 * this will *back up* the top of the display (yes, this is the right thing
 * to do!), and if the window shrank, then it will move forward the top of
 * the display.  We dont worry too much about the economy of redrawing. 
 * If a window is resized, it gets redrawn.
 */
void	resize_window_display (Window *window)
{
	int		cnt = 0, i;
	Display 	*tmp;

	if (dumb_mode)
		return;

	/*
	 * This is called in new_window to initialize the
	 * display the first time
	 */
	if (!window->top_of_scrollback)
	{
		window->top_of_scrollback = new_display_line(NULL);
		window->top_of_scrollback->line = NULL;
		window->top_of_scrollback->next = NULL;
		window->display_buffer_size = 1;
		window->display_ip = window->top_of_scrollback;
		window->top_of_display = window->top_of_scrollback;
		window->ceiling_of_display = window->top_of_display;
		window->old_size = 1;
	}
	else if (window->scrollback_point)
		;
	else
	{
		/*
		 * Find out how much the window has changed by
		 */
		cnt = window->display_size - window->old_size;
		tmp = window->top_of_display;

		/*
		 * If it got bigger, move the top_of_display back.
		 */
		if (cnt > 0)
		{
			for (i = 0; i < cnt; i++)
			{
				if (!tmp || !tmp->prev || 
				    tmp == window->ceiling_of_display)
					break;
				tmp = tmp->prev;
			}
		}

		/*
		 * If it got smaller, then move the top_of_display up
		 */
		else if (cnt < 0)
		{
			/* Use any whitespace we may have lying around */
			cnt += (window->old_size - window->distance_from_display);
			for (i = 0; i > cnt; i--)
			{
				if (tmp == window->display_ip)
					break;
				tmp = tmp->next;
			}
		}
		window->top_of_display = tmp;
		recalculate_window_cursor(window);
	}

	/*
	 * Mark the window for redraw and store the new window size.
	 */
	window->update |= REDRAW_DISPLAY_FULL | REDRAW_STATUS;
	window->old_size = window->display_size;
	return;
}


/* * * * * * * * * * * * WINDOW UPDATING AND RESIZING * * * * * * * * * */
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

	while (traverse_all_windows(&tmp))
		tmp->update |= REDRAW_STATUS | REDRAW_DISPLAY_FAST;
}

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
		if (tmp->absolute_size)
			continue;
		window_resized += tmp->display_size;
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
		if (tmp->absolute_size)
			;
		else
		{
			tmp->display_size = each;
			if (extra)
				tmp->display_size++, extra--;
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
		screen->window_list->display_size = screen->li - 2;
		screen->window_list->bottom = screen->li - 2;
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
		old_li += tmp->display_size + tmp->status.double_status + 1;
		if (tmp->absolute_size && (window_count || tmp->next))
			continue;
		window_resized += tmp->display_size;
		window_count++;
	}

	excess_li = screen->li - old_li;

	for (tmp = screen->window_list; tmp; tmp = tmp->next)
	{
		if (tmp->absolute_size && tmp->next)
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
				offset = (tmp->display_size * excess_li) / 
						window_resized;
			else
				offset = excess_li;

			tmp->display_size += offset;
			if (tmp->display_size < 0)
				tmp->display_size = 1;
			excess_li -= offset;
		}
	}

	recalculate_window_positions(screen);
}

/*
 * update_all_windows: This goes through each visible window and draws the
 * necessary portions according the the update field of the window. 
 */
void 	update_all_windows (void)
{
	Window	*tmp = NULL;

	if (in_window_command)
		return;

	while (traverse_all_windows(&tmp))
	{
		if (tmp->display_size != tmp->old_size)
			resize_window_display(tmp);
		if (tmp->visible && tmp->update)
		{
			int fast_window = tmp->update & REDRAW_DISPLAY_FAST;
			int full_window = tmp->update & REDRAW_DISPLAY_FULL;
			int r_status = tmp->update & REDRAW_STATUS;
			int u_status = tmp->update & UPDATE_STATUS;

			/* Dont do the fast vs full bogus optimization */
			if (full_window || fast_window)
			{
				repaint_window(tmp, tmp->repaint_start,
						    tmp->repaint_end);
			}

			if (r_status)
				update_window_status(tmp, 1);
			else if (u_status)
				update_window_status(tmp, 0);
		}
		tmp->update = 0;
		tmp->repaint_start = 0;
		tmp->repaint_end = -1;
	}
	update_input(UPDATE_JUST_CURSOR);
}


/* * * * * * * * LOCATION AND COMPOSITION OF WINDOWS ON SCREEN * * * * * * */
/*
 * goto_window: This will switch the current window to the N'th window 
 * from the top of the screen.  The "which" has nothing  to do with the 
 * window's refnum, only its location on the screen.
 */
static void 	goto_window (Screen *s, int which)
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
		say("You can't hide an invisible window.");
		return;
	}
	if (window->screen->visible_windows == 1)
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
void 	swap_last_window (char dumb, char *dumber)
{
	if (!invisible_list || !current_window->screen)
		return;

	swap_window(current_window, invisible_list);
	message_from((char *) 0, LOG_CRAP);
	update_all_windows();
	cursor_to_input();
}

/*
 * next_window: This switches the current window to the next visible window 
 * This is a keybinding.
 */
void 	next_window (char dumb, char *dumber)
{
	Window *w;

	if (!last_input_screen)
		return;
	if (last_input_screen->visible_windows == 1)
		return;

	w = get_next_window(last_input_screen->current_window);
	make_window_current(w);
	set_screens_current_window(last_input_screen, w);
	update_all_windows();
}

/*
 * swap_next_window:  This swaps the current window with the next hidden 
 * window.
 * This is a keybinding.
 */
void 	swap_next_window (char dumb, char *dumber)
{
	window_next(current_window, NULL);
	update_all_windows();
}

/*
 * previous_window: This switches the current window to the previous visible
 * window 
 * This is a keybinding
 */
void 	previous_window (char dumb, char *dumber)
{
	Window *w;

	if (!last_input_screen)
		return;
	if (last_input_screen->visible_windows == 1)
		return;

	w = get_previous_window(last_input_screen->current_window);
	make_window_current(w);
	set_screens_current_window(last_input_screen, w);
	update_all_windows();
}

/*
 * swap_previous_window:  This swaps the current window with the next 
 * hidden window.
 * This is a keybinding
 */
void 	swap_previous_window (char dumb, char *dumber)
{
	window_previous(current_window, NULL);
	cursor_to_input();
	update_all_windows();
}

/* show_window: This makes the given window visible.  */
static void 	show_window (Window *window)
{
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
	set_screens_current_window(window->screen, window);
	return;
}




/* * * * * * * * * * * * * GETTING WINDOWS AND WINDOW INFORMATION * * * * */
/*
 * get_window_by_desc: Given either a refnum or a name, find that window
 */
Window *get_window_by_desc (char *stuff)
{
	Window	*w = NULL;	/* bleh */

	while (*stuff == '#')
		stuff++;

	if ((w = get_window_by_name(stuff)))
		return w;

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

/*
 * get_window_by_name: returns a pointer to a window with a matching logical
 * name or null if no window matches 
 */
Window *get_window_by_name (char *name)
{
	Window	*tmp = NULL;

	while (traverse_all_windows(&tmp))
	{
		if (tmp->name && (my_stricmp(tmp->name, name) == 0))
			return (tmp);
	}

	return NULL;
}

/* 
 * DON'T EVEN THINK OF CALLING THIS FUNCTION IF YOU KNOW WHAT IS GOOD
 * FOR YOU!  Well, if you do feel like calling it, you better make sure
 * to make a copy of it immediately, because the ltoa() return value will
 * soon point to someone else's number... ;-)
 */
char *	get_refnum_by_window (const Window *w)
{
	return ltoa(w->refnum);
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
	Window	*tmp;

	if (is_number(arg))
	{
		if ((tmp = get_window_by_refnum(my_atol(arg))) != NULL)
			return tmp->visible;
	}
	if ((tmp = get_window_by_name(arg)) != NULL)
		return tmp->visible;

	return -1;
}

/* 
 * XXXX i have no idea if this belongs here.
 */
char *	get_status_by_refnum(unsigned refnum, int line)
{
	Window *the_window;

	if ((the_window = get_window_by_refnum(refnum)))
	{
		if (line > the_window->status.double_status)
			return NULL;

		return denormalize_string(the_window->status.line[line].result);
	}
	else
		return NULL;
}




/* * * * * * * * * * * * * * * * STATUS LINE * * * * * * * * * * * * * */
/*
 * update_window_status: Update the status line(s) for just one specified
 * window.  If 'refresh' is 1, then we forcibly redraw the status, otherwise
 * we just update it if it has changed.
 */
void 	update_window_status (Window *window, int refresh)
{
	if (!window)
		window = current_window;
	if ((!window->visible) || !status_update_flag || never_connected)
		return;

	if (refresh)
	{
		new_free(&(window->status.line[0].result));
		new_free(&(window->status.line[1].result));
	}
	make_status(window);
}

/*
 * update_all_status: This performs a logical "update_window_status" on
 * every window for the current screen.
 */
void 	update_all_status (void)
{
	Window	*window;

	if (!status_update_flag || never_connected || !foreground)
		return;

	window = NULL;
	while (traverse_all_windows(&window))
	{
		if (!window->visible)		/* on the invisible list */
			break;
		make_status(window);
	}

	update_input(UPDATE_JUST_CURSOR);
}

/*
 * status_update: sets the status_update_flag to whatever flag is.  This also
 * calls update_all_status(), which will update the status line if the flag
 * was true, otherwise it's just ignored 
 */
int	status_update (int flag)
{
	int old_flag = status_update_flag;

	status_update_flag = flag;
	update_all_status();
	cursor_to_input();
	return old_flag;
}

/* * * * * * * * * * * * * INPUT PROMPT * * * * * * * * * * * * * * */
/*
 * set_prompt_by_refnum: changes the prompt for the given window.  A window
 * prompt will be used as the target in place of the query user or current
 * channel if it is set 
 */
void 	set_prompt_by_refnum (unsigned refnum, char *prompt)
{
	Window	*tmp;

	if (!(tmp = get_window_by_refnum(refnum)))
		tmp = current_window;
	malloc_strcpy(&tmp->prompt, prompt);
}




/* get_prompt_by_refnum: returns the prompt for the given window refnum */
char 	*get_prompt_by_refnum (unsigned refnum)
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
char 	*get_target_by_refnum (unsigned refnum)
{
	Window	*tmp;

	if (!(tmp = get_window_by_refnum(refnum)))
		if (!(tmp = last_input_screen->current_window))
			return NULL;

	return tmp->query_nick 	? tmp->query_nick
				: tmp->current_channel 	? tmp->current_channel
							: NULL;
}

/* query_nick: Returns the query nick for the current channel */
const char	*query_nick (void)
{
	return current_window->query_nick;
}


/* * * * * * * * * * * * * * * * * * CHANNELS * * * * * * * * * * * * * */
/*
 * is_current_channel: Returns true is channel is a current channel for any
 * window.  If the delete flag is set, the unset channel as the current
 * channel for any and all windows 
 */
int 	is_current_channel (const char *channel, char remove)
{
	Window	*tmp = NULL;

	if (remove)
		panic("is_current_channel: remove != 0 isn't valid");

	while (traverse_all_windows(&tmp))
	{
		if (tmp->server == from_server && tmp->current_channel &&
		    !my_stricmp(channel, tmp->current_channel))
			return 1;
	}

	return 0;
}

/*
 * set_channel_by_refnum: This sets the current channel for the current
 * window. It returns the current channel as it's value.  If channel is null,
 * the current channel is not changed, but simply reported by the function
 * result.  This treats as a special case setting the current channel to
 * channel "0".  This frees the current_channel for the
 * current_screen->current_window, * setting it to null 
 */
const char *set_channel_by_refnum (unsigned int refnum, const char *channel)
{
	Window	*tmp;
	char	*oldc;

	if ((tmp = get_window_by_refnum(refnum)) == (Window *) 0)
		panic("Invalid window refnum [%d] passed to set_channel_by_refnum", refnum);
	if (!im_on_channel(channel, tmp->server))
		panic("Tried to make [%s:%d] the current channel of window [%d], but I'm not on that channel!", channel, tmp->server, refnum);

	oldc = tmp->current_channel;
	if (!channel || (channel && !strcmp(channel, zero)))
		tmp->current_channel = NULL;
	else
		tmp->current_channel = m_strdup(channel);

	/* Remove "waiting_channel" if we're waiting for this channel. ;-) */
	if (tmp->waiting_channel && channel &&
				!my_stricmp(channel, tmp->waiting_channel))
		new_free(&tmp->waiting_channel);
	tmp->update |= UPDATE_STATUS;
	if (tmp->current_channel)
		set_channel_window(tmp, tmp->current_channel);
	do_hook(SWITCH_CHANNELS_LIST, "%d %s %s", refnum, 
			oldc ? oldc : zero, 
			tmp->current_channel ? tmp->current_channel : zero);
	new_free(&oldc);
	return channel;
}

/* get_channel_by_refnum: returns the current channel for window refnum */
char 	*get_channel_by_refnum (unsigned refnum)
{
	Window	*tmp;

	if ((tmp = get_window_by_refnum(refnum)) == (Window *) 0)
		tmp = current_window;
	return (tmp->current_channel);
}

int	is_bound_to_window (const Window *window, const char *channel)
{
	return (window->bind_channel ? 
		!my_stricmp(window->bind_channel, channel) : 0);
}

Window *get_window_bound_channel (const char *channel)
{
	Window *tmp = NULL;

	while (traverse_all_windows(&tmp))
	    if (tmp->bind_channel && !my_stricmp(tmp->bind_channel, channel))
		return tmp;

	return NULL;
}

int 	is_bound_anywhere (const char *channel)
{
	Window *tmp = NULL;

	while (traverse_all_windows(&tmp))
	    if (tmp->bind_channel && !my_stricmp(tmp->bind_channel, channel))
		return 1;

	return 0;
}

int 	is_bound (const char *channel, int server)
{
	Window *tmp = NULL;

	while (traverse_all_windows(&tmp))
	{
	    if (tmp->server == server && tmp->bind_channel  &&
			!my_stricmp(tmp->bind_channel, channel))
		return 1;
	}

	return 0;
}

void 	unbind_channel (const char *channel, int server)
{
	Window *tmp = NULL;

	while (traverse_all_windows(&tmp))
	{
	    if (tmp->server == server && tmp->bind_channel &&
		!my_stricmp(tmp->bind_channel, channel))
	    {
		new_free(&tmp->bind_channel);
		tmp->bind_channel = NULL;
		return;
	    }
	}
}

char 	*get_bound_channel (Window *window)
{
	return window->bind_channel;
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
 * get_window_oldserver: returns the last server the window was connected to.
 */
int	get_window_oldserver (unsigned refnum)
{
	Window *tmp;
	if (!(tmp = get_window_by_refnum(refnum)))
		tmp = current_window;
	return tmp->last_server;
}

/*
 * Changes any windows that are currently using "old_server" to instead
 * use "new_server".  This is only ever called by connect_to_new_server.
 */
void	change_window_server (int old_server, int new_server)
{
	Window *tmp = NULL;

	/*
	 * Only do this if we're moving servers.
	 */
	if (old_server != new_server)
	{
		/* Move any active windows first... */
		while (traverse_all_windows(&tmp))
		{
			if (tmp->server != old_server)
				continue;

			tmp->server = new_server;

#if 0
			/* 
			 * Unless we are disconnecting, deleting 
			 * current_channel and waiting_channel is not
			 * our responsibility.  But if we are disconnecting
			 * then if we don't do it, nobody can.
			 */
			if (new_server == -1)
				window_discon(tmp, NULL);	/* XXXh */
#endif
		}
	}

	/*
	 * Always try to reclaim any lost windows lying around.
	 */
	tmp = NULL;
	while (traverse_all_windows(&tmp))
	{
		if (tmp->server != -1)
			continue;
		if (tmp->last_server != old_server)
			continue;
		tmp->server = new_server;
		tmp->last_server = -1;
	}
	
	if (old_server == primary_server)
		primary_server = new_server;
	window_check_servers();
}

/*
 * windows_connected_to_server: This returns the number of windows that
 * are actively connected to a server.  This is used by /window server
 */
int	windows_connected_to_server (int server)
{
	Window	*tmp = NULL;
	int	count = 0;

	while (traverse_all_windows(&tmp))
	{
		if (tmp->server == server)
			count++;
	}
	return count;
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
	int	cnt, max, i, connected;
	int	prime = -1;

	connected_to_server = 0;
	max = server_list_size();
	for (i = 0; i < max; i++)
	{
		connected = is_server_open(i);
		cnt = 0;

		tmp = NULL;
		while (traverse_all_windows(&tmp))
		{
			if (tmp->server == i)
			{
				/*
				 * Generally, closed server connections have
				 * their window's moved to new servers
				 * gracefully.  In this case, something
				 * really died.  We just make this window
				 * not connected to any server and save the
				 * last server so connect_to_new_server can
				 * glum this window up next time.
				 */
				if (!connected)
				{
					tmp->last_server = i;
					tmp->server = -1;
				}
				else
				{
					prime = tmp->server;
					cnt++;
				}
			}
		}

		if (cnt)
			connected_to_server++;
		else if (connected)
		{
			dont_save_server_channels(i);
			close_server(i, "No windows for this server");
		}
	}

	if (dead)
		return;

	if (!is_server_open(primary_server))
	{
		tmp = NULL; 
		while (traverse_all_windows(&tmp))
		{
			if (tmp->server == primary_server)
			       tmp->server = prime;
		}
		primary_server = prime;
	}
	update_all_status();
	cursor_to_input();
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
	Window	*tmp;

	/* Do test #1 */
	tmp = NULL;
	while (traverse_all_windows(&tmp))
	{
		if (!tmp->current_channel)
			continue;
		if (!im_on_channel(tmp->current_channel, tmp->server))
			panic("Window [%d]'s current channel [%s] "
			      "does not exist for server [%d]", 
				tmp->refnum, tmp->current_channel,
				tmp->server);
	}

	/* Do test #2 */
	tmp = NULL;
	while (traverse_all_windows(&tmp))
	{
		Window	*w;

		if (!tmp->current_channel)
			continue;
		w = get_channel_window(tmp->current_channel, tmp->server);
		if (w != tmp)
			panic("Window [%d]'s current channel [%s] [%d] "
			      "is connected to a different window [%d]!",
				tmp->refnum, tmp->current_channel, 
				tmp->server, w ? w->refnum : -1);
	}

	/* Tests #3 through #5 are done in names.c */
	channel_check_windows();
}


/* * * * * * * * * * LEVELS * * * * * * * * * */
/*
 * set_level_by_refnum: This sets the window level given a refnum.  It
 * revamps the windows levels as well using revamp_window_levels() 
 */
void 	set_level_by_refnum (unsigned refnum, int level)
{
	Window	*tmp;

	if (!(tmp = get_window_by_refnum(refnum)))
		tmp = current_window;
	tmp->window_level = level;
	revamp_window_levels(tmp);
}

/*
 * revamp_window_levels: Given a level setting for the current window, this
 * makes sure that that level setting is unused by any other window. Thus
 * only one window in the system can be set to a given level.  This only
 * revamps levels for windows with servers matching the given window 
 * it also makes sure that only one window has the level `DCC', as this is
 * not dependant on a server.
 */
static void 	revamp_window_levels (Window *window)
{
	Window	*tmp = NULL;
	int	got_dcc;

	got_dcc = (LOG_DCC & window->window_level) ? 1 : 0;
	while (traverse_all_windows(&tmp))
	{
		if (tmp == window)
			continue;
		if (LOG_DCC & tmp->window_level)
		{
			if (got_dcc)
				tmp->window_level &= ~LOG_DCC;
			got_dcc = 1;
		}
		if (window->server == tmp->server)
			tmp->window_level ^= (tmp->window_level & window->window_level);
	}
}

/*
 * message_to: This allows you to specify a window (by refnum) as a
 * destination for messages.  Used by EXEC routines quite nicely 
 */
void 	message_to (unsigned refnum)
{
	to_window = (refnum) ? get_window_by_refnum(refnum) : NULL;
}

/*
 * save_message_from: this is used to save (for later restoration) the
 * who_from variable.  This is needed when a function (do_hook) is about 
 * to call another function (parse_line) it knows will change who_from.
 * The values are saved on the stack so it will be recursive-safe.
 *
 * NO CHEATING when you call this function to get the value of who_from! ;-)
 */
void 	save_message_from (const char **saved_who_from, int *saved_who_level)
{
	*saved_who_from = who_from;
	*saved_who_level = who_level;
}

/* restore_message_from: restores a previously saved who_from variable */
void 	restore_message_from (const char *saved_who_from, int saved_who_level)
{
	who_from = saved_who_from;
	who_level = saved_who_level;
}

/*
 * message_from: With this you can set the who_from variable and the 
 * who_level variable, used by the display routines to decide which 
 * window messages should go to.  
 */
void 	message_from (const char *who, int level)
{
	static	int	saved_lastlog_level;

#ifdef NO_CHEATING
	malloc_strcpy(&who_from, who);
#else
	who_from = who;
#endif

	/*
	 * Implicitly set the lastlog level, as well.
	 * This uncomplicates a lot of stuff.  Why do i know this
	 * is going to backfire on me?
	 */
	if (level == LOG_CURRENT)
		set_lastlog_msg_level(saved_lastlog_level);
	else
		saved_lastlog_level = set_lastlog_msg_level(level);

	who_level = level;
}

/*
 * message_from_level: Like set_lastlog_msg_level, except for message_from.
 * this is needed by XECHO, because we could want to output things in more
 * than one level.
 */
int 	message_from_level (int level)
{
	int	temp;

	temp = who_level;
	who_level = level;
	return temp;
}

/* * * * * * * * * * * CLEARING WINDOWS * * * * * * * * * * */
static void 	clear_window (Window *window)
{
	if (dumb_mode)
		return;

	/* Scratch windows */
	if (window->scratch_line != -1)
	{
		Display *curr_line;

		/* Just walk every line and nuke whatever is in it */
		curr_line = window->top_of_display;
		while (curr_line && curr_line != window->display_ip)
		{
			malloc_strcpy(&curr_line->line, empty_string);
			curr_line = curr_line->next;
		}
	}

	/* Normal windows */
	else
	{
		window->top_of_display = window->display_ip;
		window->ceiling_of_display = window->top_of_display;
		window->cursor = 0;
		window->lines_scrolled_back = 0;
		window->scrollback_point = NULL;
		window->held_displayed = 0;
		if (window->miscflags & WINDOW_NOTIFIED)
			window->miscflags &= ~WINDOW_NOTIFIED;
	}

	repaint_window(window, 0, -1);
	update_window_status(window, 1);
}

void 	clear_all_windows (int unhold, int visible, int hidden)
{
	Window *tmp = NULL;

	while (traverse_all_windows(&tmp))
	{
		if (visible && !hidden && !tmp->visible)
			continue;
		if (!visible && hidden && tmp->visible)
			continue;

		if (unhold)
			hold_mode(tmp, OFF, 1);
		clear_window(tmp);
	}
}

/*
 * clear_window_by_refnum: just like clear_window(), but it uses a refnum. If
 * the refnum is invalid, the current window is cleared. 
 */
void 	clear_window_by_refnum (unsigned refnum)
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

	if (window->scratch_line != -1)
		return;			/* Cant unclear a scratch window */

	window->top_of_display = window->display_ip;
	for (i = 0; i < window->display_size; i++)
	{
		if (window->top_of_display == window->top_of_scrollback)
			break;
		window->top_of_display = window->top_of_display->prev;
	}
	window->ceiling_of_display = window->top_of_display;
	repaint_window(window, 0, -1);
	update_window_status(window, 0);
}

void	unclear_all_windows (int unhold, int visible, int hidden)
{
	Window *tmp = NULL;

	while (traverse_all_windows(&tmp))
	{
		if (visible && !hidden && !tmp->visible)
			continue;
		if (!visible && hidden && tmp->visible)
			continue;

		if (unhold)
			hold_mode(tmp, OFF, 1);
		unclear_window(tmp);
	}
}

void	unclear_window_by_refnum (unsigned refnum)
{
	Window *tmp;

	if (!(tmp = get_window_by_refnum(refnum)))
		tmp = current_window;
	unclear_window(tmp);
}




/* * * * * * * * * * * * * * * SCROLLING * * * * * * * * * * * * * * */
/*
 * set_scroll_lines: called by /SET SCROLL_LINES to check the scroll lines
 * value 
 */
void 	set_scroll_lines (int size)
{
	if (size == 0)
	{
		say("You cannot turn SCROLL off.  Gripe at me.");
		return;
	}

	else if (size > current_window->display_size)
	{
		say("Maximum lines that may be scrolled is %d", 
			current_window->display_size);
		set_int_var(SCROLL_LINES_VAR, current_window->display_size);
	}
}





/* * * * * * * * * UNSORTED * * * * * * * */

/*
 * set_continued_line: checks the value of CONTINUED_LINE for validity,
 * altering it if its no good 
 */
void 	set_continued_line (char *value)
{
	if (value && (strlen(value) > (current_term->TI_cols / 2)))
		value[current_term->TI_cols / 2] = '\0';
}


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
void    set_scrollback_size (int size)
{
        Window  *window = NULL;

        while (traverse_all_windows(&window))
        {
		if (size < window->display_size * 2)
			window->display_buffer_max = window->display_size * 2;
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

char	*get_nicklist_by_window (Window *win)
{
	WNickList *nick = win->nicks;
	char *stuff = NULL;
	size_t stuffclue = 0;

	for (; nick; nick = nick->next)
		m_sc3cat(&stuff, space, nick->nick, &stuffclue);

	if (!stuff)
		return m_strdup(empty_string);
	else
		return stuff;
}

#define WIN_FORM "%-4s %*.*s %*.*s %*.*s %-9.9s %-10.10s %s%s"
static void 	list_a_window (Window *window, int len)
{
	int	cnw = get_int_var(CHANNEL_NAME_WIDTH_VAR);

	if (cnw == 0)
		cnw = 12;	/* Whatever */

	say(WIN_FORM,           ltoa(window->refnum),
		      12, 12,   get_server_nickname(window->server),
		      len, len, window->name ? window->name : "<None>",
		      cnw, cnw, window->current_channel ? window->current_channel : "<None>",
		                window->query_nick ? window->query_nick : "<None>",
		                get_server_itsname(window->server),
		                bits_to_lastlog_level(window->window_level),
		                window->visible ? empty_string : " Hidden");
}



/* below is stuff used for parsing of WINDOW command */


/*
 * get_window: this parses out any window (visible or not) and returns a
 * pointer to it 
 */
static Window *get_window (char *name, char **args)
{
	char	*arg;
	Window	*tmp;

	if ((arg = next_arg(*args, args)) != NULL)
	{
		if (is_number(arg))
		{
			if ((tmp = get_window_by_refnum(my_atol(arg))) != NULL)
				return (tmp);
		}
		if ((tmp = get_window_by_name(arg)) != NULL)
			return (tmp);
		say("%s: No such window: %s", name, arg);
	}
	else
		say("%s: Please specify a window refnum or name", name);
	return ((Window *) 0);
}

/*
 * get_invisible_window: parses out an invisible window by reference number.
 * Returns the pointer to the window, or null.  The args can also be "LAST"
 * indicating the top of the invisible window list (and thus the last window
 * made invisible) 
 */
static Window *get_invisible_window (char *name, char **args)
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
			if (!tmp->visible)
				return (tmp);
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
static int 	get_number (char *name, char **args)
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
static int 	get_boolean (char *name, char **args, int *var)
{
	char	*arg;
	int	newval;

	newval = *var;
	if (!(arg = next_arg(*args, args)) || do_boolean(arg, &newval))
	{
		say("Value for %s must be ON, OFF, or TOGGLE", name);
		return (-1);
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
		if (!find_in_list((List **)&window->nicks, arg, !USE_WILDCARDS))
		{
			say("Added %s to window name list", arg);
			new_w = (WNickList *)new_malloc(sizeof(WNickList));
			new_w->nick = m_strdup(arg);
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
	if (tmp->visible)
		set_screens_current_window(last_input_screen, tmp);
	else
	{
		swap_window(window, tmp);
		message_from((char *) 0, LOG_CRAP);
	}

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
 * /WINDOW BIND <#channel>
 * Indicates that the window should be "bound" to the specified channel.
 * "binding" a channel to a window means that the channel will always 
 * belong to this window, no matter what.  For example, if a channel is
 * bound to a window, you can do a /join #channel in any window, and it 
 * will always "join" in this window.  This is especially useful when
 * you are disconnected from a server, because when you reconnect, the client
 * often loses track of which channel went to which window.  Binding your
 * channels gives the client a hint where channels belong.
 *
 * You can rebind a channel to a new window, even after it has already
 * been bound elsewhere.
 */
static Window *window_bind (Window *window, char **args)
{
	char *arg;
	Window *w = NULL;

	if ((arg = next_arg(*args, args)))
	{
		if (!is_channel(arg))
		{
			say("BIND: %s is not a valid channel name", arg);
			return NULL;
		}

		/*
		 * If its already bound, no point in continuing.
		 */
		if (window->bind_channel && !my_stricmp(window->bind_channel, arg))
		{
			say("Window is already bound to channel %s", arg);
			return window;
		}

		/*
		 * If the window is bound to something else, let the user
		 * know about that.
		 */
		if (window->bind_channel)
		{
			say("Unbinding channel %s from this window", 
				window->bind_channel);
			window->bind_channel = NULL;
		}

		/*
		 * You must either bind the current channel to a window, or
		 * you must be binding to a window without a current channel
		 */
		if (window->current_channel)
		{
			if (!my_stricmp(window->current_channel, arg))
				malloc_strcpy(&window->bind_channel, arg);
			else
				say("You may only /WINDOW BIND the current channel for this window");
			return window;
		}


		/*
		 * So we know this window doesnt have a current channel.
		 * So we have to find the window where it IS the current
		 * channel (if it is at all)
		 */
		while (traverse_all_windows(&w))
		{
			/*
			 * If we have found a window where this channel
			 * is the current channel, then we make it so that
			 * it is the current channel here.
			 * Also, move_channel_to_window transfer cancels the
			 * channels' bound status on the old window.
			 */
			if (w->current_channel &&
			    !my_stricmp(w->current_channel, arg) &&
			    w->server == window->server)
			{
				move_channel_to_window(arg, w, window);
				w->update |= UPDATE_STATUS;
			}
		}

		/*
		 * Now we mark this channel as being bound here.
		 * and as being our current channel.
		 */
		malloc_strcpy(&window->bind_channel, arg);
		say("Window is bound to channel %s", arg);

		if (im_on_channel(arg, window->server))
		{
			set_channel_by_refnum(window->refnum, arg);
			say("Current channel for window now %s", arg);
		}
	}

	else if ((arg = get_bound_channel(window)))
		say("Window is bound to channel %s", arg);
	else
		say("Window is not bound to any channel");

	return window;
}

/*
 * /WINDOW CHANNEL <#channel>
 * Directs the client to make a specified channel the current channel for
 * the window -- it will JOIN the channel if you are not already on it.
 * If the channel you wish to activate is bound to a different window, you
 * will be notified.  If you are already on the channel in another window,
 * then the channel's window will be switched.  If you do not specify a
 * channel, or if you specify the channel "0", then the window will drop its
 * connection to whatever channel it is in.  You can only join a channel if
 * that channel is not bound to a different window.
 */
Window *window_channel (Window *window, char **args)
{
	char 	*sarg, 
		*arg,
		*arg2 = NULL,
		*arg3 = NULL;
	Window 	*w = NULL;

	/* Fix by Jason Brand, Nov 6, 2000 */
	if (window->server == -1)
	{
		say("This window is not connected to a server.");
		return NULL;
	}

	if ((sarg = new_next_arg(*args, args)))
	{
		if (!(arg = next_arg(sarg, &sarg)))
		{
			say("Huh?");
			return window;
		}

		if (!my_strnicmp(arg, "-i", 2))
		{
			if (invite_channel)
				arg = invite_channel;
			else
			{
				say("You have not been invited to a channel!");
				return window;
			}
		}

                if (!is_channel(arg))
                {
                        say("CHANNEL: %s is not a valid channel name", arg);
                        return NULL;
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
		arg = m_strcat_ues(&arg2, arg, 1);
		sarg = m_strcat_ues(&arg3, sarg, 1);

		while (traverse_all_windows(&w))
		{
			if (w == window || w->server != window->server)
				continue;	/* Not us! */

			if (w->bind_channel &&
				!my_stricmp(arg, w->bind_channel))
			{
			    say("Channel %s is already bound elsewhere", arg);
			    new_free(&arg2);
			    return window;
			}

			if (w->current_channel &&
				!my_stricmp(arg, w->current_channel))
			{
			    move_channel_to_window(arg, w, window);
			    w->update |= UPDATE_STATUS;
			}
		}

		message_from(arg, LOG_CRAP);
		if (im_on_channel(arg, window->server))
		{
			set_channel_by_refnum(window->refnum, arg);
			say("You are now talking to channel %s", 
				check_channel_type(arg));
		}
		else if (arg[0] == '0' && arg[1] == 0)
			set_channel_by_refnum(window->refnum, NULL);
		else
		{
			send_to_aserver(window->server,"JOIN %s %s", arg, sarg);
			malloc_strcpy(&window->waiting_channel, arg);
		}

		new_free(&arg2);
	}
	else
		set_channel_by_refnum(window->refnum, zero);

	message_from(NULL, LOG_CRAP);
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
 *
 * Since the external screen is outside the client's process, it is not really
 * possible for the client to know when the external screen is resized, or
 * what that new size would be.  For this reason, you should not resize any
 * screen when you have external screens open.  If you do, the client will
 * surely become confused and the output will probably be garbled.  You can
 * restore some sanity by making sure that ALL external screens have the same
 * geometry, and then redrawing each screen.
 */
static Window *window_create (Window *window, char **args)
{
#ifdef WINDOW_CREATE
	Window *tmp;
	if ((tmp = (Window *)create_additional_screen()))
		window = tmp;
	else
#endif
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
#ifdef WINDOW_CREATE
	kill_screen(window->screen);
#endif
	return current_window;
}

/*
 * /WINDOW DESCRIBE
 * Directs the client to tell you a bit about the current window.
 * This is the 'default' argument to the /window command.
 */
static Window *window_describe (Window *window, char **args)
{
if (window->name)
	say("Window %s (%u)", 
				window->name, window->refnum);
else
	say("Window %u", window->refnum);

	say("\tServer: %d - %s",
				window->server, 
				window->server > -1 ? 
				get_server_name(window->server) : "<None>");
	say("\tScreen: %p",	window->screen);
	say("\tGeometry Info: [%d %d %d %d %d %d]", 
				window->top, window->bottom, 
				window->held_displayed, window->display_size,
				window->cursor, window->distance_from_display);
	say("\tCO, LI are [%d %d]", current_term->TI_cols, current_term->TI_lines);
	say("\tCurrent channel: %s", 
				window->current_channel ? 
				window->current_channel : "<None>");

if (window->waiting_channel)
	say("\tWaiting channel: %s", 
				window->waiting_channel);

if (window->bind_channel)
	say("\tBound channel: %s", 
				window->bind_channel);
	say("\tQuery User: %s", 
				window->query_nick ? 
				window->query_nick : "<None>");
	say("\tPrompt: %s", 
				window->prompt ? 
				window->prompt : "<None>");
	say("\tSecond status line is %s", 
				onoff[window->status.double_status]);
	say("\tLogging is %s", 
				onoff[window->log]);

if (window->logfile)
	say("\tLogfile is %s", window->logfile);
else
	say("\tNo logfile given");

	say("\tNotification is %s", 
				onoff[window->miscflags & WINDOW_NOTIFY]);
	say("\tHold mode is %s", 
				onoff[window->hold_mode]);
	say("\tWindow level is %s", 
				bits_to_lastlog_level(window->window_level));
	say("\tLastlog level is %s", 
				bits_to_lastlog_level(window->lastlog_level));
	say("\tNotify level is %s", 
				bits_to_lastlog_level(window->notify_level));

	if (window->nicks)
	{
		WNickList *tmp;
		say("\tName list:");
		for (tmp = window->nicks; tmp; tmp = tmp->next)
			say("\t  %s", tmp->nick);
	}

	return window;
}

/*
 * /WINDOW DISCON
 * This disassociates a window with all servers.
 */
static Window *window_discon (Window *window, char **args)
{
	reassign_window_channels(window);
	new_free(&window->current_channel);
	new_free(&window->bind_channel);
	new_free(&window->waiting_channel);
	window->last_server = window->server;
	window->server = -1;		/* XXX This shouldn't be set here. */
	window_check_servers();
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
	int current = window->status.double_status;

	if (get_boolean("DOUBLE", args, &window->status.double_status))
		return NULL;

	window->display_size += current - window->status.double_status;
	recalculate_window_positions(window->screen);
	redraw_all_windows();
	build_status(NULL);
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
extern	void add_to_window (Window *, const unsigned char *); /* XXXXX */

	const char *to_echo;

	if (**args == '"')
		to_echo = new_next_arg(*args, args);
	else
		to_echo = *args, *args = NULL;

	add_to_window(window, (const unsigned char *)to_echo);
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
	if (get_boolean("FIXED", args, &window->absolute_size))
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
	flush_everything_being_held(window);
	return window;
}

/*
 * /WINDOW GOTO refnum
 * This switches the current window selection to the window as specified
 * by the numbered refnum.
 */
static Window *window_goto (Window *window, char **args)
{
	goto_window(window->screen, get_number("GOTO", args));
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
	if (get_boolean("HOLD_MODE", args, &window->hold_mode))
		return NULL;

	set_var_value(HOLD_MODE_VAR, onoff[window->hold_mode]);
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
	delete_window(window);
	return current_window;
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
		if (tmp != window)
			delete_window(tmp);
		tmp = next;
	}

	return window;
}

/*
 * /WINDOW KILLSWAP
 * This arranges for the current window to be replaced by the last window
 * to be hidden, and also destroys the current window.
 */
static Window *window_killswap (Window *window, char **args)
{
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
		int size = my_atol(arg);
		if (window->lastlog_size > size)
		{
			int i, diff;
			diff = window->lastlog_size - size;
			for (i = 0; i < diff; i++)
				remove_from_lastlog(window);
		}
		window->lastlog_max = size;
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
static Window *window_lastlog_level (Window *window, char **args)
{
	char *arg = next_arg(*args, args);;

	if (arg)
		window->lastlog_level = parse_lastlog_level(arg);
	say("Lastlog level is %s", bits_to_lastlog_level(window->lastlog_level));
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
	int	newlevel = 0;

	if ((arg = next_arg(*args, args)))
	{
		if (*arg == '+')
			add = 1, arg++;
		else if (*arg == '-')
			add = -1, arg++;

		newlevel = parse_lastlog_level(arg);
		if (add == 1)
			window->window_level |= newlevel;
		else if (add == 0)
			window->window_level = newlevel;
		else if (add == -1)
			window->window_level &= ~newlevel;

		revamp_window_levels(window);
	}
	say("Window level is %s", bits_to_lastlog_level(window->window_level));
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
		if (tmp->name && (strlen(tmp->name) > len))
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
	char *logfile;
	int add_ext = 1;
	char buffer[BIG_BUFFER_SIZE + 1];

	if (get_boolean("LOG", args, &window->log))
		return NULL;

	if ((logfile = window->logfile))
		add_ext = 0;
	else if (!(logfile = get_string_var(LOGFILE_VAR)))
		logfile = empty_string;

	strmcpy(buffer,  logfile, BIG_BUFFER_SIZE);

	if (add_ext)
	{
		char *title = empty_string;

		strmcat(buffer, ".", BIG_BUFFER_SIZE);
		if ((title = window->current_channel))
			strmcat(buffer, title, BIG_BUFFER_SIZE);
		else if ((title = window->query_nick))
			strmcat(buffer, title, BIG_BUFFER_SIZE);
		else
		{
			strmcat(buffer, "Window_", BIG_BUFFER_SIZE);
			strmcat(buffer, ltoa(window->refnum), BIG_BUFFER_SIZE);
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

	if ((arg = next_arg(*args, args)))
	{
		/* /window name -  unsets the window name */
		if (!strcmp(arg, "-"))
			new_free(&window->name);

		/* /window name to existing name -- ignore this. */
		else if (window->name && (my_stricmp(window->name, arg) == 0))
			return window;

		else if (is_window_name_unique(arg))
		{
			malloc_strcpy(&window->name, arg);
			window->update |= UPDATE_STATUS;
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

	if (!invisible_list)
	{
		say("There are no hidden windows");
		return NULL;
	}

	smallest = window;
	for (tmp = invisible_list; tmp; tmp = tmp->next)
	{
		if (tmp->refnum < smallest->refnum)
			smallest = tmp;
		if ((tmp->refnum > window->refnum)
		    && (!next || (tmp->refnum < next->refnum)))
			next = tmp;
	}

	if (!next)
		next = smallest;

	swap_window(window, next);
	message_from((char *) 0, LOG_CRAP);
	return current_window;
}

static	Window *window_noserv (Window *window, char **args)
{
	return window_discon(window, args);
}

static Window *window_notify (Window *window, char **args)
{
	window->miscflags ^= WINDOW_NOTIFY;
	say("Notification when hidden set to %s",
		window->miscflags & WINDOW_NOTIFY ? on : off);
	return window;
}

static Window *window_notify_level (Window *window, char **args)
{
	char *arg;

	if ((arg = next_arg(*args, args)))
		window->notify_level = parse_lastlog_level(arg);
	say("Window notify level is %s", bits_to_lastlog_level(window->notify_level));
	return window;
}

static Window *window_number (Window *window, char **args)
{
	Window 	*tmp;
	char 	*arg;
	int 	i;

	if ((arg = next_arg(*args, args)))
	{
		if ((i = my_atol(arg)) > 0)
		{
			if ((tmp = get_window_by_refnum(i)))
				tmp->refnum = window->refnum;
			window->refnum = i;
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

		if (win->visible)
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

	if (!invisible_list)
	{
		say("There are no hidden windows");
		return NULL;
	}

	largest = window;
	for (tmp = invisible_list; tmp; tmp = tmp->next)
	{
		if (tmp->refnum > largest->refnum)
			largest = tmp;
		if ((tmp->refnum < window->refnum)
		    && (!previous || tmp->refnum > previous->refnum))
			previous = tmp;
	}

	if (!previous)
		previous = largest;

	swap_window(window, previous);
	message_from((char *) 0, LOG_CRAP);
	return current_window;
}

static Window *window_prompt (Window *window, char **args)
{
	char *arg;

	if ((arg = next_arg(*args, args)))
	{
		malloc_strcpy(&window->prompt, arg);
		window->update |= UPDATE_STATUS;
	}
	else
		say("You must specify a prompt for the window!");

	return window;
}

static Window *window_push (Window *window, char **args)
{
	WindowStack *new_ws;

	new_ws = (WindowStack *) new_malloc(sizeof(WindowStack));
	new_ws->refnum = window->refnum;
	new_ws->next = window->screen->window_stack;
	window->screen->window_stack = new_ws;
	return window;
}

Window *window_query (Window *window, char **args)
{
	WNickList *tmp;
	char	  *nick;
	char	  *a;
	Window	  *sw;

	/*
	 * Nuke the old query list
	 */
	if ((nick = window->query_nick))
	{
		say("Ending conversation with %s", window->query_nick);
		window->update |= UPDATE_STATUS;

		a = nick;
		while (a && *a)
		{
			nick = next_in_comma_list(a, &a);
			if ((tmp = (WNickList *)remove_from_list(
					(List **)&window->nicks, nick)))
			{
				new_free(&tmp->nick);
				new_free((char **)&tmp);
			}
		}
		new_free(&window->query_nick);
	}

	if ((nick = new_next_arg(*args, args)))
	{
		if (!strcmp(nick, "."))
		{
			if (!(nick = sent_nick))
				say("You have not messaged anyone yet");
		}
		else if (!strcmp(nick, ","))
		{
			if (!(nick = recv_nick))
				say("You have not recieved a message yet");
		}
		else if (!strcmp(nick, "*") && 
			!(nick = get_channel_by_refnum(0)))
		{
			say("You are not on a channel");
		}
		else if (*nick == '%')
		{
			if (!is_valid_process(nick))
				nick = NULL;
		}

		if (!nick)
			return window;

		/*
		 * Create the new query list
		 * Ugh.  Make sure this goes to the RIGHT WINDOW!
		 */
		sw = to_window;
		to_window = window;
		say("Starting conversation with %s", nick);
		to_window = sw;

		malloc_strcpy(&window->query_nick, nick);
		window->update |= UPDATE_STATUS;
		a = nick;
		while (a && *a)
		{
			nick = next_in_comma_list(a, &a);
			tmp = (WNickList *)new_malloc(sizeof(WNickList));
			tmp->nick = m_strdup(nick);
			add_to_list((List **)&window->nicks, (List *)tmp);
		}
	}

	update_input(UPDATE_ALL);
	return window;
}

/*
 * /WINDOW REBIND <#channel>
 * The channel will be "bound" to this window unconditionally.  This is
 * the same as /window bind, except that <#channel> does not need to be
 * the current channel of this window.  It may be bound elsewhere, for
 * example.  This function will handle all of that for you.  This function
 * will not fail unless you goof up the argument.
 *
 * Note: This operation is intentionally silent -- Please do not add a 
 * whole bunch of says and yells in here.  The primary purpose of this 
 * function is to be used in things like /on connect where all the output
 * just gets in the way and gives the user no useful information.
 */
static Window *window_rebind (Window *window, char **args)
{
	char *arg;
	Window *w = NULL;

	if (!(arg = next_arg(*args, args)))
	{
		say("REBIND: requires a channel argument");
		return NULL;
	}

	if (!is_channel(arg))
	{
		say("REBIND: %s is not a valid channel name", arg);
		return NULL;
	}

	/*
	 * If its already bound, just accept it and stop here.
	 */
	if (window->bind_channel && !my_stricmp(window->bind_channel, arg))
		return window;

	/*
	 * Unbind whatever is currently bound to the window...
	 */
	if (window->bind_channel)
		new_free(&window->bind_channel);

	/*
	 * Go a-hunting to see if any other window claims this channel.
	 * If they do, forcibly take it from them.
	 */
	while (traverse_all_windows(&w))
	{
		/*
		 * If we have found a window where this channel
		 * is the current channel, then we make it so that
		 * it is the current channel here.  This informs
		 * the channel that it now belongs to us.
		 * This also unbinds it from the old window.
		 */
		if (w->current_channel &&
		    !my_stricmp(w->current_channel, arg) &&
		    w->server == window->server)
		{
			move_channel_to_window(arg, w, window);
			w->update |= UPDATE_STATUS;
		}
	}

	/*
	 * Now lay claim to the channel as belonging to us.
	 * and as being our current channel.  Tell ourselves
	 * (and the user) that we own this channel now.
	 */
	malloc_strcpy(&window->bind_channel, arg);
	if (im_on_channel(arg, window->server))
		set_channel_by_refnum(window->refnum, arg);

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
	char *	chan;
	char *	keys = NULL;
	char *	newchan = NULL;

	/* First off, we have to be connected to join */
	if (from_server == -1 || !is_server_connected(from_server))
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
                        if (invite_channel)
                                chan = invite_channel;
                        else
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

		/* If we're on the channel... */
		if (im_on_channel(chan, from_server))
		{
			/* And i'm on the right server, grab the channel */
			if (window->server == from_server)
			{
				Window *other;
				/* 
				 * XXX - Using get_channel_window() for
				 * this is cheating. 
				 */
				other = get_channel_window(chan, from_server);
				move_channel_to_window(chan, other, window);
				set_channel_by_refnum(window->refnum, chan);
				say("You are now talking to channel %s", 
					check_channel_type(chan));
			}
			/* Otherwise, Do not move the channel */
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
			    if (w->current_channel &&
			        !my_stricmp(w->current_channel, chan))
			    {
				owner = w;
				break;
			    }
			    if (w->bind_channel &&
			        !my_stricmp(w->bind_channel, chan))
			    {
				owner = w;
				break;
			    }
			    if (w->waiting_channel &&
			        !my_stricmp(w->waiting_channel, chan))
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
				panic("There are no windows for this server, "
				      "and there should be.");

			malloc_strcpy(&owner->waiting_channel, chan);
			m_s3cat(&newchan, ",", chan);
		}
	}
	if (newchan)
	{
		if (keys)
			send_to_aserver(from_server, "JOIN %s %s", newchan, keys);
		else
			send_to_aserver(from_server, "JOIN %s", newchan);
		new_free(&newchan);
	}

	message_from(NULL, LOG_CRAP);
	return window;
}

static Window *window_refnum (Window *window, char **args)
{
	Window *tmp;
	if ((tmp = get_window("REFNUM", args)))
	{
		window = tmp;
		make_window_current(tmp);
		if (tmp->visible)
		{
			set_screens_current_window(tmp->screen, tmp);
			window = tmp;
		}
	}
	else
	{
		say("No such window!");
		window = NULL;
	}
	return window;
}

static Window *window_refnum_or_swap (Window *window, char **args)
{
	Window  *tmp;

	if (!(tmp = get_window("REFNUM_OR_SWAP", args)))
	{
		say("No such window!");
		return NULL;
	}

	if (tmp->visible)
	{
		make_window_current(tmp);
		set_screens_current_window(tmp->screen, tmp);
	}
	else
		swap_window(window, tmp);

	return tmp;
}
static Window *window_refresh (Window *window, char **args)
{
	int oiwc = in_window_command;

	in_window_command = 0;
	update_all_windows();
	update_all_status();
	in_window_command = oiwc;
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
	}
	else
		say("REMOVE: Do something!  Geez!");

	return window;
}

static	Window *window_scratch (Window *window, char **args)
{
	int scratch = 0;

	if (get_boolean("SCRATCH", args, &scratch))
		return NULL;

	if (scratch == 1)
		window->scratch_line = 0;
	else
	{
		window->scratch_line = -1;
		window->top_of_display = window->display_ip;
		window->update |= REDRAW_DISPLAY_FULL | REDRAW_STATUS;
	}
	return window;
}

Window *window_scroll (Window *window, char **args)
{
	int scroll = 0;

	if (get_boolean("SCROLL", args, &scroll))
		return NULL;

	if (scroll == 1 && window->scratch_line == -1)
		return window;
	if (scroll == 0 && window->scratch_line == 0)
		return window;

	if (scroll == 1)
	{
		window->scratch_line = -1;
		window->top_of_display = window->display_ip;
		window->update |= REDRAW_DISPLAY_FULL | REDRAW_STATUS;
	}
	else
	{
		window->scratch_line = 0;
		window->noscroll = 1;
	}
	return window;
}

static	Window *window_scrollback (Window *window, char **args)
{
	int val = get_number("SCROLLBACK", args);

	if (!val)
		return NULL;
	if (val < window->display_size * 2)
		window->display_buffer_max = window->display_size * 2;
	else
		window->display_buffer_max = val;

	say("Window scrollback size set to %d", window->display_buffer_max);
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
	int	newconn;

	if ((arg = next_arg(*args, args)))
	{
		int i = find_server_refnum(arg, NULL);

		if (windows_connected_to_server(window->server) > 1)
		{
			clear_reconnect_counts();	/* ?? */
			newconn = 1;
		}
		else
			newconn = 0;

		if (!connect_to_new_server(i, window->server, 1))
		{
			/*
			 * First find a new home for all our channels.
			 * This is a must since we're moving to a 
			 * different server, and we don't want the channels
			 * on one server to point to a window on another
			 * server.
			 */
			reassign_window_channels(window);

			/*
			 * Associate ourselves with the new server.
			 */
			window->server = i;
			window->last_server = -1;

			/*
			 * Set the window's lastlog level that is
			 * in /set new_server_lastlog_level
			 */
			set_level_by_refnum(window->refnum, 
						new_server_lastlog_level);

			/*
			 * And blow away any old channel information 
			 * which we surely cannot use now.
			 */
			new_free(&window->current_channel);
			new_free(&window->bind_channel);
			new_free(&window->waiting_channel);
		}

		/*
		 * Now make sure everything seems coherent.
		 * If we were the last window attached to that server,
		 * then window_check_servers() will close up that server
		 * for us and garbage collect any channels lying around.
		 */
		window_check_servers();
	}
	else
		say("SERVER: You must specify a server");

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
		say("Window size is %d", window->display_size);
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
	WindowStack 	*last, *tmp, *crap;
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
			crap = tmp->next;
			new_free((char **)&tmp);
			if (last)
				last->next = crap;
			else
				window->screen->window_stack = crap;

			tmp = crap;
		}
	}

	return window;
}

static Window *window_status_format (Window *window, char **args)
{
	char	*arg;

	arg = new_next_arg(*args, args);
	malloc_strcpy(&window->status.line[0].raw, arg);
	window->update |= REDRAW_STATUS;
	rebuild_a_status(window);

	return window;
}

static Window *window_status_format1 (Window *window, char **args)
{
	char	*arg;

	arg = new_next_arg(*args, args);
	malloc_strcpy(&window->status.line[1].raw, arg);
	window->update |= REDRAW_STATUS;
	rebuild_a_status(window);

	return window;
}

static Window *window_status_format2 (Window *window, char **args)
{
	char	*arg;

	arg = new_next_arg(*args, args);
	malloc_strcpy(&window->status.line[2].raw, arg);
	window->update |= REDRAW_STATUS;
	rebuild_a_status(window);

	return window;
}

static Window *window_status_special (Window *window, char **args)
{
	char *arg;

	arg = new_next_arg(*args, args);
	malloc_strcpy(&window->status.special, arg);
	window->update |= REDRAW_STATUS;

	return window;
}

static Window *window_swap (Window *window, char **args)
{
	Window *tmp;

	if ((tmp = get_invisible_window("SWAP", args)))
		swap_window(window, tmp);

	return current_window;
}

static Window *window_unbind (Window *window, char **args)
{
	char *arg;

	if ((arg = next_arg(*args, args)))
	{
		if (is_bound(arg, from_server))
		{
			say("Channel %s is no longer bound", arg);
			unbind_channel(arg, from_server);
		}
		else
			say("Channel %s is not bound", arg);
	}
	else
	{
		say("Channel %s is no longer bound", window->bind_channel);
		new_free(&window->bind_channel);
	}
	return window;
}



typedef Window *(*window_func) (Window *, char **args);

typedef struct window_ops_T {
	char 		*command;
	window_func 	func;
} window_ops;

const static window_ops options [] = {
	{ "ADD",		window_add 		},
	{ "BACK",		window_back 		},
	{ "BALANCE",		window_balance 		},
	{ "BEEP_ALWAYS",	window_beep_always 	},
	{ "BIND",		window_bind 		},
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
	{ "GOTO",		window_goto 		},
	{ "GROW",		window_grow 		},
	{ "HIDE",		window_hide 		},
	{ "HIDE_OTHERS",	window_hide_others 	},
	{ "HOLD_INTERVAL",	window_hold_interval	},
	{ "HOLD_MODE",		window_hold_mode 	},
	{ "KILL",		window_kill 		},
	{ "KILL_OTHERS",	window_kill_others 	},
	{ "KILLSWAP",		window_killswap 	},
	{ "LAST", 		window_last 		},
	{ "LASTLOG",		window_lastlog 		},
	{ "LASTLOG_LEVEL",	window_lastlog_level 	},
	{ "LEVEL",		window_level 		},
	{ "LIST",		window_list 		},
	{ "LOG",		window_log 		},
	{ "LOGFILE",		window_logfile 		},
	{ "MOVE",		window_move 		},
	{ "MOVE_TO",		window_move_to		},
	{ "NAME",		window_name 		},
	{ "NEW",		window_new 		},
	{ "NEW_HIDE",		window_new_hide		}, /* * */
	{ "NEXT",		window_next 		},
	{ "NOSERV",		window_noserv		},
	{ "NOTIFY",		window_notify 		},
	{ "NOTIFY_LEVEL",	window_notify_level 	},
	{ "NUMBER",		window_number 		},
	{ "POP",		window_pop 		},
	{ "PREVIOUS",		window_previous 	},
	{ "PROMPT",		window_prompt 		},
	{ "PUSH",		window_push 		},
	{ "QUERY",		window_query		},
	{ "REBIND",		window_rebind		},
	{ "REFNUM",		window_refnum 		},
	{ "REFNUM_OR_SWAP",	window_refnum_or_swap	},
	{ "REFRESH",		window_refresh		},
	{ "REJOIN",		window_rejoin		},
	{ "REMOVE",		window_remove 		},
	{ "SCRATCH",		window_scratch		},
	{ "SCROLL",		window_scroll		},
	{ "SCROLLBACK",		window_scrollback	}, /* * */
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
	{ "UNBIND",		window_unbind 		},
	{ NULL,			NULL 			}
};

BUILT_IN_COMMAND(windowcmd)
{
	char 	*arg;
	int 	nargs = 0;
	Window 	*window;
	int	old_status_update;
	int 	oiwc = in_window_command;

	in_window_command = 1;
	old_status_update = status_update_flag;
	message_from(NULL, LOG_CURRENT);
	window = current_window;

	while ((arg = next_arg(args, &args)))
	{
		int i;
		int len = strlen(arg);

		if (*arg == '-' || *arg == '/')		/* Ignore - or / */
			arg++, len--;

		for (i = 0; options[i].func ; i++)
		{
			if (!my_strnicmp(arg, options[i].command, len))
			{
				window = options[i].func(window, &args); 
				nargs++;
				if (!window)
					args = NULL;
				break;
			}
		}

		if (!options[i].func)
		{
			Window *s_window;
			if ((s_window = get_window_by_desc(arg)))
			{
				nargs++;
				window = s_window;
			}
			else
				yell("WINDOW: Invalid option: [%s]", arg);
		}
	}

	if (!nargs)
		window_describe(current_window, NULL);

	in_window_command = oiwc;
	status_update_flag = old_status_update;
	message_from((char *) 0, LOG_CRAP);
	update_all_windows();
	update_all_status();
	cursor_to_input();
	window_check_channels();
}


/*
 * Everything after this line is in the wrong place.  Consider it 
 * "miscelaneous", and pending reformatting.  Ick.
 */

/* * * * * * * * * * * SCROLLBACK BUFFER * * * * * * * * * * * * * * */
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

void 	delete_display_line (Display *stuff)
{
	if (recycle == stuff)
		panic("recycle == stuff is bogus");
	if (recycle)
		new_free((char **)&recycle);
	recycle = stuff;
	new_free(&stuff->line);
}

Display *new_display_line (Display *prev)
{
	Display *stuff;

	if (recycle)
	{
		stuff = recycle;
		recycle = NULL;
	}
	else
		stuff = (Display *)new_malloc(sizeof(Display));

	stuff->line = NULL;
	stuff->prev = prev;
	stuff->next = NULL;
	return stuff;
}

/* * * * * * * * * * * Scrollback functionality * * * * * * * * * * */
void 	scrollback_backwards_lines (int lines)
{
	Window	*window = current_window;
	Display *new_top = window->top_of_display;
	int	new_lines;

	if (new_top == window->top_of_scrollback)
	{
		term_beep();
		return;
	}

	if (!window->scrollback_point)
		window->scrollback_point = window->top_of_display;

	for (new_lines = 0; new_lines < lines; new_lines++)
	{
		if (new_top == window->top_of_scrollback)
			break;
		new_top = new_top->prev;
	}

	window->top_of_display = new_top;
	window->lines_scrolled_back += new_lines;
	recalculate_window_cursor(window);
	repaint_window(window, 0, -1);
	update_window_status(window, 0);
	cursor_not_in_display(window->screen);
	update_input(UPDATE_JUST_CURSOR);
}

void 	scrollback_forwards_lines (int lines)
{
	Window	*window = current_window;
	Display *new_top = window->top_of_display;
	int	new_lines = 0;

	if (new_top == window->display_ip || !window->scrollback_point)
	{
		term_beep();
		return;
	}

	for (new_lines = 0; new_lines < lines; new_lines++)
	{
		if (new_top == window->scrollback_point)
			break;
		new_top = new_top->next;
	}

	window->top_of_display = new_top;
	window->lines_scrolled_back -= new_lines;
	recalculate_window_cursor(window);
	repaint_window(window, 0, -1);
	update_window_status(window, 0);
	cursor_not_in_display(window->screen);
	update_input(UPDATE_JUST_CURSOR);

	if (window->lines_scrolled_back <= 0)
		scrollback_end (0, NULL);
}

void 	scrollback_forwards (char dumb, char *dumber)
{
	int 	ratio = get_int_var(SCROLLBACK_RATIO_VAR);
	int	lines;

	if (ratio < 1) 
		ratio = 1;
	if (ratio > 100) 
		ratio = 100;

	if ((lines = current_window->display_size * ratio / 100) < 1)
		lines = 1;
	scrollback_forwards_lines(lines);
}

void 	scrollback_backwards (char dumb, char *dumber)
{
	int 	ratio = get_int_var(SCROLLBACK_RATIO_VAR);
	int	lines;

	if (ratio < 1) 
		ratio = 1;
	if (ratio > 100) 
		ratio = 100;

	if ((lines = current_window->display_size * ratio / 100) < 1)
		lines = 1;
	scrollback_backwards_lines(lines);
}


void 	scrollback_end (char dumb, char *dumber)
{
	Window	*window = current_window;

	if (!window->scrollback_point)
	{
		term_beep();
		return;
	}

	/* Adjust the top of window only if we would move forward. */
	if (window->lines_scrolled_back > 0)
		window->top_of_display = window->scrollback_point;

	window->lines_scrolled_back = 0;
	window->scrollback_point = NULL;
	repaint_window(window, 0, -1);
	update_window_status(window, 0);
	cursor_not_in_display(window->screen);
	update_input(UPDATE_JUST_CURSOR);
}

void 	scrollback_start (char dumb, char *dumber)
{
	Window	*window = current_window;

	if (window->display_buffer_size <= window->display_size)
	{
		term_beep();
		return;
	}

	if (!window->scrollback_point)
		window->scrollback_point = window->top_of_display;

	while (window->top_of_display != window->top_of_scrollback)
	{
		window->top_of_display = window->top_of_display->prev;
		window->lines_scrolled_back++;
	}

	repaint_window(window, 0, -1);
	update_window_status(window, 0);
	cursor_not_in_display(window->screen);
	update_input(UPDATE_JUST_CURSOR);
}


/* HOLD MODE STUFF */
/*
 * hold_mode: sets the "hold mode".  Really.  If the update flag is true,
 * this will also update the status line, if needed, to display the hold mode
 * state.  If update is false, only the internal flag is set.  
 * -- Note: update is always 1. --
 */
void	hold_mode (Window *window, int flag, int update)
{
	if (!window)
		window = current_window;

	if (flag != ON && window->scrollback_point)
		update = 0;

	if (flag == TOGGLE)
	{
		if (window->hold_mode == OFF)
			window->hold_mode = ON;
		else
			window->hold_mode = OFF;
	}
	else
		window->hold_mode = flag;

	if (!update)
	{
		window->last_lines_held = -1;
		return;
	}

	if (window->lines_held != window->last_lines_held)
	{
		window->last_lines_held = window->lines_held;
		update_window_status(window, 0);
		if (window->update | UPDATE_STATUS)
			window->update -= UPDATE_STATUS;
		cursor_in_display(window);
		update_input(NO_UPDATE);
	}
}

/*
 * This checks to see if any windows need to be unheld or not
 */
int	unhold_windows(void)
{
	Window 	*w = NULL;
	int	retval = 0;

	while (traverse_all_windows(&w))
	{
		if (!w->hold_mode && w->lines_held)
		{
			if (unhold_a_window(w))
				retval++;
		}
	}

	return retval;
}



void 	unstop_all_windows (char dumb, char *dumber)
{
	Window	*tmp = NULL;

	while (traverse_all_windows(&tmp))
		hold_mode(tmp, OFF, 1);
}

/*
 * reset_line_cnt: called by /SET HOLD_MODE to reset the line counter so we
 * always get a held screen after the proper number of lines 
 */
void 	reset_line_cnt (int value)
{
	current_window->hold_mode = value;
	current_window->held_displayed = 0;
}

/* toggle_stop_screen: the BIND function TOGGLE_STOP_SCREEN */
void 	toggle_stop_screen (char unused, char *not_used)
{
	hold_mode((Window *) 0, TOGGLE, 1);
	update_all_windows();
}

/* 
 * If "scrollback_point" is set, then anything below the bottom of the screen
 * at that point gets nuked.
 * If "scrollback_point" is not set, anything below the current position of
 * the screen gets nuked.
 */
void 	flush_everything_being_held (Window *window)
{
	Display *ptr, *save;
	int 	count;

	if (!window)
		window = current_window;

	count = window->display_size;

	if (window->scrollback_point)
		ptr = window->scrollback_point;
	else
		ptr = window->top_of_display;

	while (--count > 0)
	{
		ptr = ptr->next;
		if (ptr == window->display_ip)
			return;		/* Nothing to flush */
	}

	save = ptr->next;
	ptr->next = window->display_ip;
	window->display_ip->prev = ptr;
	ptr = save;

	while (ptr != window->display_ip)
	{
		Display *next = ptr->next;
		delete_display_line(ptr);
		window->lines_held--;
		window->display_buffer_size--;
		ptr = next;
	}

	recalculate_window_cursor(window);
	if (window->lines_held != 0)
		panic("lines_held == %d after flushing", window->lines_held);

	window->holding_something = 0;
	hold_mode(window, OFF, 1);
}


/*
 * This adjusts the viewport up one full screen.  This calls rite() 
 * indirectly, because repaint_window() uses rite() to do the work.
 * This belongs somewhere else.
 */
int	unhold_a_window (Window *window)
{
	int amount = window->display_size;

	if (window->holding_something &&
		(window->distance_from_display < window->display_size))
	{
		window->holding_something = 0;
		window->lines_held = 0;
	}

	if (!window->lines_held || window->scrollback_point)
		return 0;		/* Right. */

	if (window->lines_held < amount)
		amount = window->lines_held;

	window->lines_held -= amount;
	window->held_displayed = 0;

	if (!window->lines_held)
		window->holding_something = 0;

	if (!amount)
		return 0;		/* Whatever */

	while (amount--)
		window->top_of_display = window->top_of_display->next;

	repaint_window(window, 0, -1);
	update_window_status(window, 0);
	return 1;
}

/*
 * Miscelaneous -- these probably belong somewhere else, but until then,
 * theyre here.
 */


/*
 * This figures out just where the cursor is supposed to be on a window
 * by starting at the top of the window and walking downwards until it
 * finds the insertion point.  It correctly deals with scrollback and
 * hold mode by putting the cursor on the bottom line.
 */
void 	recalculate_window_cursor (Window *window)
{
	Display *tmp;

	window->cursor = window->distance_from_display = 0;
	for (tmp = window->top_of_display; tmp != window->display_ip;
				tmp = tmp->next)
		window->cursor++, window->distance_from_display++;

	if (window->cursor > window->display_size)
		window->cursor = window->display_size;
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
		panic("This window is dead.");
	if (window->screen != screen)
		panic("The window is not on that screen.");
	if (!screen)
		panic("Cannot set the invisible screen's current window.");

	if (screen->current_window != window)
	{
		if (screen->current_window)
		{
			screen->current_window->update |= UPDATE_STATUS;
			screen->last_window_refnum = screen->current_window->refnum;
		}
		screen->current_window = window;
		screen->current_window->update |= UPDATE_STATUS;
	}
	if (current_window != window)
		make_window_current(window);
	update_all_windows();
}

void	make_window_current_by_refnum (int refnum)
{
	Window	*new_window;

	if (refnum == -1)
		return;

	if ((new_window = get_window_by_refnum(refnum)))
		make_window_current(new_window);
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
	int	new_screen, new_window;

	if (!window)
		current_window = last_input_screen->current_window;
	else if (current_window != window)
		current_window = window;

	if (current_window->deceased)
		panic("This window is dead and cannot be made current");

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

	do_hook(SWITCH_WINDOWS_LIST, "%d %d %d %d",
		old_screen, old_window,
		new_screen, new_window);
}
