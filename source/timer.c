/*
 * timer.c -- handles timers in ircII
 * Copyright 1993, 1996 Matthew Green
 * This file organized/adapted by Jeremy Nelson
 *
 * This used to be in edit.c, and it used to only allow you to
 * register ircII commands to be executed later.  I needed more
 * generality then that, specifically the ability to register
 * any function to be called, so i pulled it all together into
 * this file and called it timer.c
 */

#include "irc.h"
#include "ircaux.h"
#include "window.h"
#include "timer.h"
#include "hook.h"
#include "output.h"
#include "commands.h"
#include "server.h"
#include "screen.h"

static	void	show_timer 		(const char *command);
static 	void	delete_all_timers 	(void);

/*
 * timercmd: the bit that handles the TIMER command.  If there are no
 * arguements, then just list the currently pending timers, if we are
 * give a -DELETE flag, attempt to delete the timer from the list.  Else
 * consider it to be a timer to add, and add it.
 */
BUILT_IN_COMMAND(timercmd)
{
	char	*waittime,
		*flag;
	char	*want = empty_string;
	char	*ptr;
	double	interval;
	long	events = -2;
	int	update = 0;
	Window	*win = current_window;
	size_t	len;

	while (*args == '-' || *args == '/')
	{
		flag = next_arg(args, &args);
		len = strlen(flag + 1);

		if (!my_strnicmp(flag + 1, "DELETE", len))
		{
			if (!(ptr = next_arg(args, &args)))
			    say("%s: Need a timer reference number for -DELETE",
					command);

			/* 
			 * Try to delete what was given.  If that
			 * doesn't work, see if it is "delete all".
			 * Delete_timer returns -1 on error.
			 */
			else if (timer_exists(ptr))
				delete_timer(ptr);

			/*
			 * Check to see if it is /timer -delete all
			 *
			 * Ugh.  Yes, this hack really _is_ required.
			 * Please don't change it without talking to
			 * me first to understand why.
			 */
			else if (*ptr && !my_strnicmp(ptr, "ALL", strlen(ptr)))
				delete_all_timers();
			else if (*ptr)
				say("The timer \"%s\" doesn't exist!", ptr);
			else
				panic("how did timercmd get here?");

			return;
		}
		else if (!my_strnicmp(flag+1, "REF", 3))	/* REFNUM */
		{
			want = next_arg(args, &args);
			if (!want || !*want)
			{
				say("%s: Missing argument to -REFNUM", command);
				return;
			}

			continue;
		}
		else if (!my_strnicmp(flag+1, "REP", 3))	/* REPEAT */
		{
			char *na = next_arg(args, &args);
			if (!na || !*na)
			{
				say("%s: Missing argument to -REPEAT", command);
				return;
			}
			if (!strcmp(na, "*") || !strcmp(na, "-1"))
				events = -1;
			else if ((events = my_atol(na)) == 0)
				return;	 /* Repeating 0 times is a noop */

			continue;
		}
		else if (!my_strnicmp(flag + 1, "U", 1))	/* UPDATE */
			update = 1;

		else if (!my_strnicmp(flag + 1, "L", 1))	/* LIST */
			show_timer(command);
		else if (!my_strnicmp(flag + 1, "W", 1))	/* WINDOW */
		{
			char 	*na;

			if ((na = next_arg(args, &args)))
				win = get_window_by_desc(na);

			if (!win)
			{
			    if (my_stricmp(na, "-1"))
			    {
				say("%s: That window doesnt exist!", command);
				return;
			    }
			}
		}

		else
		{
			say("%s: %s no such flag", command, flag);
			return;
		}
	}

	/* else check to see if we have no args -> list */
	waittime = next_arg(args, &args);
	if (update || waittime)
	{
		if (update && !timer_exists(want))
		{
			say("%s: To use -UPDATE you must specify a valid refnum", command);
			return;
		}

		if (!waittime)
			interval = -1;
		else
			interval = atof(waittime);

		if (!update && events == -2)
			events = 1;
		else if (events == -2)
			events = -1;

		add_timer(update, want, interval, events, NULL, args, subargs, win);
	}
	else
		show_timer(command);

	return;
}

/*
 * This is put here on purpose -- we dont want any of the above functions
 * to have any knowledge of this struct.
 */
#define REFNUM_MAX 10
typedef struct  timerlist_stru
{
	char	ref[REFNUM_MAX + 1];
        struct timeval time;
	int	(*callback) (void *);
        void    *command;
	char	*subargs;
        struct  timerlist_stru *next;
	long	events;
	struct timeval	interval;
	int	server;
	int	window;
}       TimerList;

static 	char *		schedule_timer (TimerList *ntimer);
static	TimerList *	get_timer (const char *ref);

static 	TimerList *	PendingTimers;
static 	char *		current_exec_timer = empty_string;
static	int 		parsingtimer = 0;

/*
 * ExecuteTimers:  checks to see if any currently pending timers have
 * gone off, and if so, execute them, delete them, etc, setting the
 * current_exec_timer, so that we can't remove the timer while its
 * still executing.
 *
 * changed the behavior: timers will not hook while we are waiting.
 */
void 	ExecuteTimers (void)
{
	struct timeval	now;
	TimerList *	current;
	int		old_from_server = from_server;

	/*
	 * We DONT want to parse timers if we're waiting,
	 * if we're already parsing another time, or
	 * if there are no timers, as it gets icky...
	 */
        if (waiting_out > waiting_in || parsingtimer || !PendingTimers)
                return;

        parsingtimer = 1;
	get_time(&now);

/*
yell("PendingTimers->time is [%ld/%ld]", PendingTimers->time.tv_sec, PendingTimers->time.tv_usec);
yell("now is [%ld/%ld]", now.tv_sec, now.tv_usec);
yell("Difference is [%f]", time_diff(PendingTimers->time, now));
*/

	while (PendingTimers && time_diff(now, PendingTimers->time) < 0)
	{
		int	old_refnum = current_window->refnum;

		current = PendingTimers;

		/*
		 * Restore from_server and current_window from when the
		 * timer was registered
		 */
		make_window_current_by_refnum(current->window);

		if (is_server_open(current->server))
			from_server = current->server;
		else if (is_server_open(current_window->server))
			from_server = current_window->server;
		else
			from_server = -1;

		/* 
		 * If a callback function was registered, then
		 * we use it.  If no callback function was registered,
		 * then we use ''parse_line''.
		 */
		current_exec_timer = current->ref;
		if (current->callback)
			(*current->callback)(current->command);
		else
			parse_line("TIMER", (char *)current->command, 
						current->subargs, 0,0);

		current_exec_timer = empty_string;
		from_server = old_from_server;
		make_window_current_by_refnum(old_refnum);

		/*
		 * Remove timer from PendingTimers list
		 * now that it has been executed
		 */
		PendingTimers = PendingTimers->next;

		/*
		 * Clean up or reschedule the timer
		 */
		if (current->events < 0 || (current->events != 1))
		{
			if (current->events != -1)
				current->events--;
/*
yell("Rescheduling...");
yell("current->time is [%ld/%ld]", current->time.tv_sec, current->time.tv_usec);
yell("interval is [%ld/%ld]", current->interval.tv_sec, current->interval.tv_usec);
*/
			current->time = time_add(current->time, current->interval);
/*
yell("new time is [%ld/%ld]", current->time.tv_sec, current->time.tv_usec);
*/

			schedule_timer(current);
		}
		else
		{
			if (!current->callback)
			{
				new_free((char **)&current->command);
				new_free((char **)&current->subargs);
			}
			new_free((char **)&current);
		}
	}
        parsingtimer = 0;
}

/*
 * show_timer:  Display a list of all the TIMER commands that are
 * pending to be executed.
 */
static	void	show_timer (const char *command)
{
	TimerList	*tmp;
	struct timeval	current;
	double		time_left;

	if (!PendingTimers)
	{
		say("%s: No commands pending to be executed", command);
		return;
	}

	get_time(&current);

	say("%-10s %-10s %-7s %s", "Timer", "Seconds", "Events", "Command");
	for (tmp = PendingTimers; tmp && (tmp->callback == NULL); tmp = tmp->next)
	{
		time_left = time_diff(tmp->time, current);
		if (time_left < 0)
			time_left = 0;
		say("%-10s %-8.2f %-7ld %s", tmp->ref, time_left, tmp->events, (char *)tmp->command);
	}
}

/*
 * create_timer_ref:  returns the lowest unused reference number for a timer
 * All refnums that are not already in use are valid.
 *
 * The user is allowed to use any string as a refnum, we dont really care.
 * Automatically assigned refnums (when the user doesnt specify one) will
 * always be one more than the highest pending refnum.
 */
static	int	create_timer_ref (char *refnum_want, char *refnum_gets)
{
	TimerList	*tmp;
	int 		refnum = 0;

	/* Max of 10 characters. */
	if (strlen(refnum_want) > REFNUM_MAX)
		refnum_want[REFNUM_MAX] = 0;

	/* If the user doesnt care */
	if (*refnum_want == 0)
	{
		/* Find the lowest refnum available */
		for (tmp = PendingTimers; tmp; tmp = tmp->next)
		{
			if (refnum < my_atol(tmp->ref))
				refnum = my_atol(tmp->ref);
		}
		strmcpy(refnum_gets, ltoa(refnum+1), REFNUM_MAX);
	}
	else
	{
		/* 
		 * If the refnum is being used by the current executing timer
		 * it should be safe to use it as long as it will die after
		 * this execution
		 */
		if (current_exec_timer != empty_string &&
		    !my_stricmp(refnum_want, current_exec_timer))
		{
			if ((tmp = get_timer(current_exec_timer)) && tmp->events != 1)
				return -1;
		}
		else
		{
			/* See if the refnum is available */
			for (tmp = PendingTimers; tmp; tmp = tmp->next)
			{
				if (!my_stricmp(tmp->ref, refnum_want))
					return -1;
			}
		}
		strmcpy(refnum_gets, refnum_want, REFNUM_MAX);
	}

	return 0;
}

/*
 * Deletes a refnum.  This does cleanup only if the timer is a 
 * user-defined timer, otherwise no clean up is done (the caller
 * is responsible to handle it)  This shouldnt output an error,
 * it should be more general and return -1 and let the caller
 * handle it.  Probably will be that way in a future release.
 */
int	delete_timer (char *ref)
{
	TimerList	*tmp,
			*prev;

	if (current_exec_timer != empty_string)
	{
		say("You may not remove a TIMER within another TIMER");
		return -1;
	}

	for (prev = tmp = PendingTimers; tmp; prev = tmp, tmp = tmp->next)
	{
		/* can only delete user created timers */
		if (!my_stricmp(tmp->ref, ref))
		{
			if (tmp == prev)
				PendingTimers = PendingTimers->next;
			else
				prev->next = tmp->next;

			if (!tmp->callback)
			{
				new_free((char **)&tmp->command);
				new_free((char **)&tmp->subargs);
			}
			new_free((char **)&tmp);
			return 0;
		}
	}
	say("TIMER: Can't delete (%s), no such refnum", ref);
	return -1;
}

static void 	delete_all_timers (void)
{
	while (PendingTimers)
	{
	    if (delete_timer(PendingTimers->ref))
	    {
		say("TIMER: Deleting all timers aborted due to error");
		return;
	    }
	}
}

int 	timer_exists (const char *ref)
{
	if (!ref || !*ref)
		return 0;

	if (get_timer(ref))
		return 1;
	else
		return 0;
}

static	TimerList *get_timer (const char *ref)
{
	TimerList *tmp;

	for (tmp = PendingTimers; tmp; tmp = tmp->next)
	{
		if (!my_stricmp(tmp->ref, ref))
			return tmp;
	}

	return NULL;
}


/*
 * You call this to register a timer callback.
 *
 * The arguments:
 *  update:      This should be 1 if we're updating the specified refnum
 *  refnum_want: The refnum requested.  This should only be sepcified
 *		 by the user, functions wanting callbacks should specify
 *		 the empty string, which means "dont care".
 * The rest of the arguments are dependant upon the value of "callback"
 *	-- if "callback" is NULL then:
 *  callback:	 NULL
 *  what:	 some ircII commands to run when the timer goes off
 *  subargs:	 what to use to expand $0's, etc in the 'what' variable.
 *
 *	-- if "callback" is non-NULL then:
 *  callback:	 function to call when timer goes off
 *  what:	 argument to pass to "callback" function.  Should be some
 *		 non-auto storage, perhaps a struct or a malloced char *
 *		 array.  The caller is responsible for disposing of this
 *		 area when it is called, since the timer mechanism does not
 *		 know anything of the nature of the argument.
 * subargs:	 should be NULL, its ignored anyhow.
 */
char *add_timer (int update, char *refnum_want, double when, long events, int (callback) (void *), char *what, const char *subargs, Window *w)
{
	TimerList	*ntimer, *otimer = NULL;
	char		refnum_got[REFNUM_MAX + 1];

	ntimer = (TimerList *) new_malloc(sizeof(TimerList));
	ntimer->interval = double_to_timeval(when);
	ntimer->time = time_add(get_time(NULL), ntimer->interval);
	ntimer->events = events;
	ntimer->server = from_server;
	ntimer->window = w ? w->refnum : -1;

	if (update)
		otimer = get_timer(refnum_want);

	if (otimer)
	{
		if (when == -1)
		{
			ntimer->time = otimer->time;
			ntimer->interval = otimer->interval;
		}

		if (events == -1)
			ntimer->events = otimer->events;

		ntimer->callback = NULL;
		if (what && *what)
		{
			ntimer->command = m_strdup(what);
			ntimer->subargs = m_strdup(subargs);
		}
		else
		{
			ntimer->command = m_strdup(otimer->command);
			ntimer->subargs = m_strdup(otimer->subargs);
		}

		delete_timer(refnum_want);
	}
	else
	{
		if ((ntimer->callback = callback))
		{
			ntimer->command = (void *)what;
			ntimer->subargs = (void *)NULL;
		}
		else
		{
			ntimer->command = m_strdup(what);
			ntimer->subargs = m_strdup(subargs);
		}
	}

	if (create_timer_ref(refnum_want, refnum_got) == -1)
	{
		say("TIMER: Refnum '%s' already exists", refnum_want);
		new_free((char **)&ntimer);
		return NULL;
	}
	/* This is safe */
	strcpy(ntimer->ref, refnum_got);

	return schedule_timer(ntimer);
}

static char *schedule_timer (TimerList *ntimer)
{
	TimerList **slot;

	/* we've created it, now put it in order */
	for (slot = &PendingTimers; *slot; slot = &(*slot)->next)
	{
		if (time_diff((*slot)->time, ntimer->time) < 0)
			break;
	}
	ntimer->next = *slot;
	*slot = ntimer;
	return ntimer->ref;
}

/*
 * TimerTimeout:  Called from irc_io to help create the timeout
 * part of the call to select.
 */
static struct timeval forever = { 100000, 0 };
static struct timeval none = { 0, 0 };
struct timeval TimerTimeout (void)
{
	struct timeval	current;
	struct timeval	timeout_in;

	/* 
	 * If executing ExecuteTimers here would be invalid, then
	 * do not bother telling the caller we are ready.
	 */
        if (waiting_out > waiting_in || parsingtimer || !PendingTimers)
                return forever;
	if (!PendingTimers)
		return forever;	/* Absurdly large. */

	get_time(&current);
	timeout_in = time_subtract(current, PendingTimers->time);
	return (timeout_in.tv_sec < 0) ? none : timeout_in;
}

