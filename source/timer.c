/* $EPIC: timer.c,v 1.39 2003/10/28 05:53:57 jnelson Exp $ */
/*
 * timer.c -- handles timers in ircII
 *
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright © 1997, 2003 EPIC Software Labs.
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
/*
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
#include "functions.h"

	int 	timer_exists (const char *ref);
	int 	remove_timer (const char *ref);
static void 	remove_all_timers (void);
static	void	remove_window_timers (int winref);
static	void	list_timers (const char *command);

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
	int	winref;
	size_t	len;

	winref = current_window ? (int)current_window->refnum : -1;

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
				remove_timer(ptr);

			/*
			 * Check to see if it is /timer -delete all
			 *
			 * Ugh.  Yes, this hack really _is_ required.
			 * Please don't change it without talking to
			 * me first to understand why.
			 */
			else if (*ptr && !my_strnicmp(ptr, "ALL", strlen(ptr)))
				remove_all_timers();
			else if (*ptr)
				say("The timer \"%s\" doesn't exist!", ptr);
			else
				panic("how did timercmd get here?");

			return;
		}
		else if (!my_strnicmp(flag + 1, "DELETE_FOR_WINDOW", len))
		{
			if (!(ptr = next_arg(args, &args)) || !is_number(ptr))
			{
			    say("%s: Need a window number for -DELETE_FOR_WINDOW", command);
			    return;
			}

			winref = atol(ptr);
			remove_window_timers(winref);
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
		{
			list_timers(command);
			return;
		}
		else if (!my_strnicmp(flag + 1, "W", 1))	/* WINDOW */
		{
			char 	*na;
			Window *win = NULL;

			if ((na = next_arg(args, &args)))
				win = get_window_by_desc(na);

			if (!win)
			{
			    if (my_stricmp(na, "-1"))
			    {
				say("%s: That window doesnt exist!", command);
				return;
			    }
			    else
				winref = -1;
			}
			else
				winref = win->refnum;
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
			if (waittime)
				update = 0;
			else
			{
				say("%s: To use -UPDATE you must specify a valid refnum", command);
				return;
			}
		}

		if (!waittime)
			interval = -1;
		else
			interval = atof(waittime);

		if (!update && events == -2)
			events = 1;
/*
		else if (events == -2)
			events = -1;
*/

		add_timer(update, want, interval, events, NULL, args, subargs, winref);
	}
	else
		list_timers(command);

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
        Timeval time;
	int	(*callback) (void *);
	void *	callback_data;
        char *	command;
	char	*subargs;
	struct	timerlist_stru *prev;
        struct  timerlist_stru *next;
	long	events;
	Timeval	interval;
	int	server;
	int	window;
}       Timer;

static 	Timer *		PendingTimers;

/*
 * create_timer: 
 */
static Timer * new_timer (void)
{
	Timer *	ntimer;

	ntimer = (Timer *) new_malloc(sizeof(Timer));
	ntimer->ref[0] = 0;
	ntimer->time.tv_sec = 0;
	ntimer->time.tv_usec = 0;
	ntimer->callback = NULL;
	ntimer->callback_data = NULL;
	ntimer->command = NULL;
	ntimer->subargs = NULL;
	ntimer->prev = NULL;
	ntimer->next = NULL;
	ntimer->events = 0;
	ntimer->interval.tv_sec = 0;
	ntimer->interval.tv_usec = 0;
	ntimer->server = FROMSERV;
	ntimer->window = -1;
	return ntimer;
}

/*
 * clone_timer: Create a copy of an existing timer, suitable for rescheduling
 */
static Timer *clone_timer (Timer *otimer)
{
	Timer *ntimer = new_timer();

	strlcpy(ntimer->ref, otimer->ref, sizeof ntimer->ref);
	ntimer->time = otimer->time;
	if ((ntimer->callback = otimer->callback))
		ntimer->callback_data = otimer->callback_data;
	else
		ntimer->command = malloc_strdup(otimer->command);
	ntimer->subargs = malloc_strdup(otimer->subargs);
	ntimer->prev = NULL;
	ntimer->next = NULL;
	ntimer->events = otimer->events;
	ntimer->interval = otimer->interval;
	ntimer->server = otimer->server;
	ntimer->window = otimer->window;
	return ntimer;
}

/*
 * delete_timer:
 */
static void delete_timer (Timer *otimer)
{
	if (!otimer->callback)
	{
		new_free((char **)&otimer->command);
		new_free((char **)&otimer->subargs);
	}
	new_free((char **)&otimer);
}

static int	schedule_timer (Timer *ntimer)
{
	Timer *tmp, *prev;

	/* we've created it, now put it in order */
	for (tmp = PendingTimers; tmp; tmp = tmp->next)
	{
		if (time_diff(tmp->time, ntimer->time) < 0)
			break;
	}

	if (tmp == PendingTimers)
	{
		ntimer->prev = NULL;
		ntimer->next = PendingTimers;
		if (PendingTimers)
			PendingTimers->prev = ntimer;
		PendingTimers = ntimer;
	}
	else if (tmp)
	{
		prev = tmp->prev;
		ntimer->next = tmp;
		ntimer->prev = prev;
		tmp->prev = ntimer;
		prev->next = ntimer;
	}
	else		/* XXX! */
	{
		for (tmp = PendingTimers; tmp->next; tmp = tmp->next)
			;
		tmp->next = ntimer;
		ntimer->prev = tmp;
	}

	return 0;
}

static int	unlink_timer (Timer *timer)
{
	Timer *prev, *next;

	prev = timer->prev;
	next = timer->next;

	if (prev)
		prev->next = next;
	else
		PendingTimers = next;

	if (next)
		next->prev = prev;

	return 0;
}

static	Timer *get_timer (const char *ref)
{
	Timer *tmp;

	for (tmp = PendingTimers; tmp; tmp = tmp->next)
	{
		if (!my_stricmp(tmp->ref, ref))
			return tmp;
	}

	return NULL;
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


/*
 * list_timers:  Display a list of all the TIMER commands that are
 * pending to be executed.
 */
static	void	list_timers (const char *command)
{
	Timer	*tmp;
	Timeval	current;
	double	time_left;
	int	timer_count = 0;

	get_time(&current);
	for (tmp = PendingTimers; tmp; tmp = tmp->next)
	{
		if (tmp->callback)
			continue;

		if (timer_count == 0)
			say("%-10s %-10s %-7s %s", 
				"Timer", "Seconds", "Events", "Command");

		timer_count++;
		time_left = time_diff(current, tmp->time);
		if (time_left < 0)
			time_left = 0;
		say("%-10s %-8.2f %-7ld %s", tmp->ref, time_left, 
					tmp->events, tmp->command);
	}

	if (timer_count == 0)
		say("%s: No commands pending to be executed", command);
}

/*
 * create_timer_ref:  returns the lowest unused reference number for a timer
 * All refnums that are not already in use are valid.
 *
 * The user is allowed to use any string as a refnum, we dont really care.
 * Automatically assigned refnums (when the user doesnt specify one) will
 * always be one more than the highest pending refnum.
 *
 * "refnum_gets" must be REFNUM_MAX + 1 bytes by definition of API.
 */
static	int	create_timer_ref (const char *refnum_wanted, char *refnum_gets)
{
	Timer	*tmp;
	int 	refnum = 0;
	char	*refnum_want;

	/* Max of 10 characters. */
	refnum_want = LOCAL_COPY(refnum_wanted);
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
		strlcpy(refnum_gets, ltoa(refnum+1), REFNUM_MAX + 1);
	}
	else
	{
		/* See if the refnum is available */
		if (get_timer(refnum_want))
			return -1;		/* Already in use */

		strlcpy(refnum_gets, refnum_want, REFNUM_MAX + 1);
	}

	return 0;
}

/*
 * Remove a timer.  This does cleanup only if the timer is a 
 * user-defined timer, otherwise no clean up is done (the caller
 * is responsible to handle it)  This shouldnt output an error,
 * it should be more general and return -1 and let the caller
 * handle it.  Probably will be that way in a future release.
 */
int	remove_timer (const char *ref)
{
	Timer	*tmp;

	if (!(tmp = get_timer(ref)))
	{
		say("TIMER: Can't delete (%s), no such refnum", ref);
		return -1;
	}

	unlink_timer(tmp);
	delete_timer(tmp);
	return 0;
}

static void 	remove_all_timers (void)
{
	Timer *ref, *next;

	for (ref = PendingTimers; ref; ref = next)
	{
		next = ref->next;
		if (ref->callback)
			continue;
		unlink_timer(ref);
		delete_timer(ref);
	}
}

static	void	remove_window_timers (int winref)
{
	Timer *ref, *next;

	for (ref = PendingTimers; ref; ref = next)
	{
		next = ref->next;
		if (ref->callback)
			continue;
		if (ref->window != winref)
			continue;
		unlink_timer(ref);
		delete_timer(ref);
	}
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
char *add_timer (int update, const char *refnum_want, double interval, long events, int (callback) (void *), void *commands, const char *subargs, int winref)
{
	Timer	*ntimer, *otimer = NULL;
	char	refnum_got[REFNUM_MAX + 1];
	Timeval right_now;
	char *	retval;

	/* XXX Eh, maybe it's a hack to check this here. */
	if (interval < 0.01 && events == -1)
	{
		say("You can't infinitely repeat a timer that runs more "
			"than 100 times a second.");
		return NULL;
	}

	right_now = get_time(NULL);

	if (update)
	{
	    if (!(otimer = get_timer(refnum_want)))
		update = 0;		/* Ok so we're not updating! */
	}

	if (update)
	{
		unlink_timer(otimer);
		ntimer = clone_timer(otimer);
		delete_timer(otimer);

		if (interval != -1)
		{
			ntimer->interval = double_to_timeval(interval);
			ntimer->time = time_add(right_now, ntimer->interval);
		}
		if (events != -2)
			ntimer->events = events;

		if (callback)
		{
			/* Delete the previous timer, if necessary */
			if (ntimer->command)
				new_free(&ntimer->command);
			if (ntimer->subargs)
				new_free(&ntimer->subargs);
			ntimer->callback = callback;
			/* Unfortunately, command is "sometimes const". */
			ntimer->callback_data = commands;
			ntimer->subargs = NULL;
		}
		else
		{
			if (ntimer->callback)
				ntimer->callback = NULL;
			malloc_strcpy(&ntimer->command, (const char *)commands);
			malloc_strcpy(&ntimer->subargs, subargs);
		}

		if (winref != -1)
			ntimer->window = winref;

	}
	else
	{
		if (create_timer_ref(refnum_want, refnum_got) == -1)
		{
			say("TIMER: Refnum '%s' already exists", refnum_want);
			return NULL;
		}

		ntimer = new_timer();
		strlcpy(ntimer->ref, refnum_got, sizeof ntimer->ref);
		ntimer->interval = double_to_timeval(interval);
		ntimer->time = time_add(right_now, ntimer->interval);
		ntimer->events = events;
		ntimer->callback = callback;
		/* Unfortunately, command is "sometimes const". */
		if (callback)
			ntimer->callback_data = commands;
		else
			malloc_strcpy(&ntimer->command, (const char *)commands);
		malloc_strcpy(&ntimer->subargs, subargs);
		ntimer->window = winref;
		ntimer->server = from_server;
	}

	schedule_timer(ntimer);
	retval = ntimer->ref;
	return retval;		/* Eliminates a specious warning from gcc */
}



/*
 * TimerTimeout:  Called from irc_io to help create the timeout
 * part of the call to select.
 */
Timeval	TimerTimeout (void)
{
	Timeval	forever = {9999, 0};
	Timeval right_away = {0, 0};
	Timeval	current;
	Timeval	timeout_in;

	/* This, however, should never happen. */
	if (!PendingTimers)
		return forever;

	get_time(&current);
	timeout_in = time_subtract(current, PendingTimers->time);
	if (time_diff(right_away, timeout_in) < 0)
		timeout_in = right_away;
	return timeout_in;
}

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
	Timeval	right_now;
	Timer *	current, *next;
	int	old_from_server = from_server;

	get_time(&right_now);
	while (PendingTimers && time_diff(right_now, PendingTimers->time) < 0)
	{
		int	old_refnum;

		old_refnum = current_window->refnum;
		current = PendingTimers;
		unlink_timer(current);

		/* Reschedule the timer if necessary */
		if (current->events < 0 || (current->events != 1))
		{
			next = clone_timer(current);
			if (next->events != -1)
				next->events--;
			next->time = time_add(next->time, next->interval);
			schedule_timer(next);
		}

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
			from_server = NOSERV;

		/* 
		 * If a callback function was registered, then
		 * we use it.  If no callback function was registered,
		 * then we use ''parse_line''.
		 */
		get_time(&right_now);
		now = right_now;
		if (current->callback)
			(*current->callback)(current->callback_data);
		else
			parse_line("TIMER", current->command, 
						current->subargs ? 
						  current->subargs : 
						  empty_string, 0,0);

		from_server = old_from_server;
		make_window_current_by_refnum(old_refnum);
		delete_timer(current);
	}
}


/* Used by function_timerctl */
/*
 * $timerctl(REFNUM refnum)
 * $timerctl(ADD <refnum> <interval> <events> <commands> <subargs> <window>)
 * $timerctl(DELETE <refnum>)
 * $timerctl(GET <refnum> [LIST])
 * $timerctl(SET <refnum> [ITEM] [VALUE])
 * $timerctl(REFNUMS)
 *
 * [LIST] and [ITEM] are one of the following
 *	TIMEOUT		The precise time the timer will be executed
 *	COMMAND		The commands that will be executed
 *	SUBARGS		The vaule of $* used when this timer is executed
 *	REPEATS		The number of times this timer will be executed
 *	INTERVAL	The interval of time between executions
 *	SERVER		The server this timer bound to
 *	WINDOW		The window this timer bound to
 */
char *	timerctl (char *input)
{
	char *	refstr;
	char *	listc;
	Timer *	t;
	int	len;

	GET_STR_ARG(listc, input);
	len = strlen(listc);
	if (!my_strnicmp(listc, "REFNUM", len)) {
		GET_STR_ARG(refstr, input);
		if (!(t = get_timer(refstr)))
			RETURN_EMPTY;
		RETURN_STR(t->ref);
	} else if (!my_strnicmp(listc, "REFNUMS", len)) {
		char *	retval = NULL;
		size_t	clue = 0;

		for (t = PendingTimers; t; t = t->next)
			malloc_strcat_word_c(&retval, space, t->ref, &clue);
		RETURN_MSTR(retval);
	} else if (!my_strnicmp(listc, "ADD", len)) {
		RETURN_EMPTY;		/* XXX - Not implemented yet. */
	} else if (!my_strnicmp(listc, "DELETE", len)) {
		GET_STR_ARG(refstr, input);
		if (!(t = get_timer(refstr)))
			RETURN_EMPTY;
		if (t->callback)
			RETURN_EMPTY;
		RETURN_INT(remove_timer(refstr));
	} else if (!my_strnicmp(listc, "GET", len)) {
		GET_STR_ARG(refstr, input);
		if (!(t = get_timer(refstr)))
			RETURN_EMPTY;

		GET_STR_ARG(listc, input);
		len = strlen(listc);
		if (!my_strnicmp(listc, "TIMEOUT", len)) {
			return malloc_sprintf(NULL, "%ld %ld", (long) t->time.tv_sec,
						    (long) t->time.tv_usec);
		} else if (!my_strnicmp(listc, "COMMAND", len)) {
			if (t->callback)
				RETURN_EMPTY;
			RETURN_STR(t->command);
		} else if (!my_strnicmp(listc, "SUBARGS", len)) {
			if (t->callback)
				RETURN_EMPTY;
			RETURN_STR(t->subargs);
		} else if (!my_strnicmp(listc, "REPEATS", len)) {
			RETURN_INT(t->events);
		} else if (!my_strnicmp(listc, "INTERVAL", len)) {
			return malloc_sprintf(NULL, "%ld %ld", (long) t->interval.tv_sec,
						    (long) t->interval.tv_usec);
		} else if (!my_strnicmp(listc, "SERVER", len)) {
			RETURN_INT(t->server);
		} else if (!my_strnicmp(listc, "WINDOW", len)) {
			RETURN_INT(t->window);
		}
	} else if (!my_strnicmp(listc, "SET", len)) {
		GET_STR_ARG(refstr, input);
		if (!(t = get_timer(refstr)))
			RETURN_EMPTY;

		/* Changing internal system timers is strictly prohibited */
		if (t->callback)
			RETURN_EMPTY;

		GET_STR_ARG(listc, input);
		len = strlen(listc);
		if (!my_strnicmp(listc, "TIMEOUT", len)) {
			time_t	tv_sec;
			long	tv_usec;

			GET_INT_ARG(tv_sec, input);
			GET_INT_ARG(tv_usec, input);
			t->time.tv_sec = tv_sec;
			t->time.tv_usec = tv_usec;
		} else if (!my_strnicmp(listc, "COMMAND", len)) {
			malloc_strcpy((char **)&t->command, input);
		} else if (!my_strnicmp(listc, "SUBARGS", len)) {
			malloc_strcpy(&t->subargs, input);
		} else if (!my_strnicmp(listc, "REPEATS", len)) {
			long	repeats;

			GET_INT_ARG(repeats, input);
			t->events = repeats;
		} else if (!my_strnicmp(listc, "INTERVAL", len)) {
			time_t	tv_sec;
			long	tv_usec;

			GET_INT_ARG(tv_sec, input);
			GET_INT_ARG(tv_usec, input);
			t->interval.tv_sec = tv_sec;
			t->interval.tv_usec = tv_usec;
		} else if (!my_strnicmp(listc, "SERVER", len)) {
			int	refnum;

			GET_INT_ARG(refnum, input);
			t->server = refnum;
		} else if (!my_strnicmp(listc, "WINDOW", len)) {
			int	winref;

			GET_INT_ARG(winref, input);
			t->window = winref;
		}
	} else
		RETURN_EMPTY;

	RETURN_EMPTY;
}

