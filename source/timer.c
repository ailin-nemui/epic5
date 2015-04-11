/* $EPIC: timer.c,v 1.58 2015/04/11 04:16:35 jnelson Exp $ */
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
static	void 	remove_all_timers (void);
static	void	remove_timers_by_domref (int domain, int domref);
static	void	list_timers (const char *command);

/*
 * timercmd - The /TIMER command
 *
 * Usage:
 *  Listing Timers:
 *	/TIMER			
 *		Show all pending user-created TIMERs
 * 
 *  Deleting Timers:
 *	/TIMER -DELETE <refnum>
 *		Delete timer with name <refnum>
 *	/TIMER -DELETE ALL
 *		Delete all timers
 *	/TIMER -DELETE_FOR_WINDOW <winref>
 *		Delete all WINDOW timers that are bound to <winref>.
 *		<Winref> must be a number.  Use $winnum() to get that.
 *
 *  Updating Timers:
 *	/TIMER -UPDATE -REFNUM <timeref> [other arguments below]
 *		(a) To update a Timer, you must use the -REFNUM argument
 *		    to specify the timer to update.  
 *		(b) Updating a <timeref> that does not exist is not an error
 *		    if you specify a timeout; it will be created for you.
 *		
 *  Creating a Timer:
 *	/TIMER [options below] <seconds> <commands>
 *		Run <commands> after <seconds> seconds.
 *		(a) If you run a /TIMER whenever the client is handling
 *		    server data, the /TIMER will attach to the current server
 *		    and whatever its current window is at that time. ("Server")
 *		(b) If you run a /TIMER whenever the client is handling
 *		    user input, the /TIMER will attach to the current window
 *		    and whatever its current server is at that time. ("Window")
 *		(c) Otherwise, the /TIMER will run in whatever the current
 *		    context is at the time it goes off. ("General")
 *
 *	/TIMER -REFNUM
 *		The TIMER should have a specific name (default: auto-generated)
 *	/TIMER -REPEAT
 *		The TIMER should run this many times (default: 1 time)
 *		The magic value -1 means "run forever"
 *	/TIMER -SNAP
 *		The TIMER should run "at the top of" the interval.
 *		EG, /TIMER -REPEAT -1 -SNAP 60 {echo It's a new minute!} 
 *		will run at the top of every minute.  The first execution
 *		will happen "early" to make it work out.
 *
 *	/TIMER -WINDOW
 *		The TIMER should be a WINDOW Timer (default: auto-detected)
 *	/TIMER -SERVER
 *		The TIMER should be a SERVER Timer (default: auto-detected)
 *	/TIMER -GENERAL
 *		The TIMER should be a GENERAL Timer (default: auto-detected)
 *	/TIMER -CANCELABLE
 *		If the TIMER cannot restore its concept (because it is a 
 *		WINDOW Timer and the window has gone away; or because it is
 *		a SERVER Timer and the server has been deleted), the Timer
 *		will not execute when it goes off. (default: Window Timers
 *		and Server Timers become General Timers if they can't restore
 *		their context)
 */
BUILT_IN_COMMAND(timercmd)
{
	char *		waittime;
	char *		flag;
	const char *	want = empty_string;
	char *		ptr;
	double		interval;
	long		events = -2;
	int		update = 0;
	size_t		len;
	TimerDomain	domain;
	int		domref;
	int		cancelable = 0;
	int		snap = 0;

	if (parsing_server_index != NOSERV)
	{
		domain = SERVER_TIMER;
		domref = parsing_server_index;
	}
	else
	{
		domain = WINDOW_TIMER;
		domref = current_window ? (int)current_window->refnum : -1;
	}

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
		else if (timer_exists(ptr) == 1)
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
			say("/TIMER -DELETE got confused and chose not to do anything.");	/* This used to be a panic */

		return;
	    }
	    else if (!my_strnicmp(flag + 1, "DELETE_FOR_WINDOW", len))
	    {
		if (!(ptr = next_arg(args, &args)) || !is_number(ptr))
		{
		    say("%s: Need a window number for -DELETE_FOR_WINDOW", 
				command);
		    return;
		}

		domref = atol(ptr);
		remove_timers_by_domref(WINDOW_TIMER, domref);
		return;
	    }
	    else if (!my_strnicmp(flag+1, "REFNUM", len))	/* REFNUM */
	    {
		want = next_arg(args, &args);
		if (!want || !*want)
		{
			say("%s: Missing argument to -REFNUM", command);
			return;
		}

		continue;
	    }
	    else if (!my_strnicmp(flag+1, "REPEAT", len))	/* REPEAT */
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
	    else if (!my_strnicmp(flag + 1, "UPDATE", len))	/* UPDATE */
		update = 1;

	    else if (!my_strnicmp(flag + 1, "LIST", len))	/* LIST */
	    {
		list_timers(command);
		return;
	    }
	    else if (!my_strnicmp(flag + 1, "WINDOW", len))	/* WINDOW */
	    {
		char 	*na;
		Window *win = NULL;

		domain = WINDOW_TIMER;
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
			domref = -1;
		}
		else
			domref = win->refnum;
	    }
	    else if (!my_strnicmp(flag + 1, "SERVER", len))	/* SERVER */
	    {
		char 	*na;

		domain = SERVER_TIMER;
		domref = from_server;

		if ((na = next_arg(args, &args)))
		{
		    if ((domref = str_to_servref(na)) == NOSERV)
		    {
			/* -1 defaults to from_server anyways */
			if (!my_stricmp(na, "-1"))
			    domref = from_server;
			else
			{
			    say("%s: Server %s doesnt exist!", command, na);
			    return;
			}
		    }
		}
	    }
	    else if (!my_strnicmp(flag + 1, "SNAP", len))	/* SNAP */
	    {
		snap = 1;
	    }
	    else if (!my_strnicmp(flag + 1, "GENERAL", len))	/* GENERAL */
	    {
		domain = GENERAL_TIMER;
		domref = -1;
	    }
	    else if (!my_strnicmp(flag + 1, "CANCELABLE", len))	/* CANCELABLE */
	    {
		cancelable = 1;
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
		int	i;

		if (update && ((i = timer_exists(want)) <= 0))
		{
			/* If the timer doesn't exist, ignore -update */
			if (i == 0 && waittime)
				update = 0;

			/* 
			 * Otherwise, either you didn't use -refnum, or
			 * you tried to update a timer that doesn't exist,
			 * but didn't provide a timeout, so we can't 
			 * create a timer for you.
			 */
			else
			{
				say("%s: To use -UPDATE you must specify a "
						"valid refnum", command);
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

		add_timer(update, want, interval, events, NULL, args, subargs, 
				domain, domref, cancelable, snap);
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
	char	*ref;
        Timeval time;
	int	(*callback) (void *);
	void *	callback_data;
        char *	command;
	char	*subargs;
	struct	timerlist_stru *prev;
        struct  timerlist_stru *next;
	long	events;
	Timeval	interval;
	int	domain;
	int	domref;
	int	cancelable;
	long	fires;
}       Timer;

static 	Timer *		PendingTimers;

static Timer *	new_timer (void);
static Timer *	clone_timer (Timer *otimer);
static void	delete_timer (Timer *otimer);
static int	schedule_timer (Timer *ntimer);
static int	unlink_timer (Timer *timer);
static Timer *	get_timer (const char *ref);

/*
 * new_timer - Create a blank Timer that can be filled in.
 *
 * Arguments:
 *	none
 *
 * Return Value:
 *	A new (Timer *) that can be filled in and passed to schedule_timer().
 * 	After a Timer is executed or cancelled, it should be passed to 
 *	  delete_timer().
 */
static Timer *	new_timer (void)
{
	Timer *	ntimer;

	ntimer = (Timer *) new_malloc(sizeof(Timer));
	ntimer->ref = NULL;
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
	ntimer->domain = GENERAL_TIMER;
	ntimer->domain = -1;
	ntimer->cancelable = 0;
	ntimer->fires = 0;
	return ntimer;
}

/*
 * clone_timer -  Create a new Timer that is an exact copy of an existing
 *		  Timer, suitable for re-scheduling.
 * 
 * Argument:
 *	otimer - A Timer that should be cloned
 *
 * Return Value:
 *	A new (Timer *) that is identical to 'otimer', that you can pass
 *	   to schedule_timer()
 *
 * Notes:
 *	Cloning a timer is necessary to allow you to Delete (/timer -delete)
 *	a Repeating Timer (/timer -repeat).  If you deleted a repeating timer,
 *	that would cause problems in ExecuteTimers().  So what we do is treat
 *	every Timer as a single-fire:
 *	  1. Schedule a Timer
 *	  2. Timer Goes Off
 *	  3. Timer is removed from Schedule List
 *	  4. If Timer is a Repeating Timer, it is Cloned, and the Clone is
 *	     scheduled for its next execution
 *	  5. The Timer is executed
 * 	In this way, when you do /timer -delete within a repeating timer,
 *	it deletes the Clone, without clobbering the active timer.
 *
 *	In the same way, updating a Timer doesn't actually change the timer,
 *	but changes a clone of that Timer, causing the new (cloned) Timer to
 *	be created and the old Timer to be deleted.  That makes it easier
 *	to ensure the updated timer gets to the right spot on the Timer list.
 */
static Timer *	clone_timer (Timer *otimer)
{
	Timer *ntimer = new_timer();

	malloc_strcpy(&ntimer->ref, otimer->ref);
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
	ntimer->domain = otimer->domain;
	ntimer->domref = otimer->domref;
	ntimer->cancelable = otimer->cancelable;
	ntimer->fires = otimer->fires;
	return ntimer;
}

/*
 * delete_timer: clean up after a Timer that is no longer needed.
 *		 You must _not_ submit a Timer that is still on the schedule.
 *
 * Arguments:
 *	ntimer - An unneeded Timer that is not on the Timer list.
 *		 Deleting a still-scheduled Timer is an error.
 */
static void	delete_timer (Timer *otimer)
{
	Timer *tmp;

	/* 
	 * First we make sure 'otimer' is not still scheduled
	 * before we go free()ing it.
	 * (This is a violation of the API, but we're forgiving)
	 * (The other option is to make this return failure, but 
	 *  since this is a void function, we'll just DTRT)
	 */
	for (tmp = PendingTimers; tmp; tmp = tmp->next)
	{
		if (tmp == otimer)
		{
			yell("delete_timer: Warning: Deleting a timer that "
				"is still scheduled.  Unscheduling it.");
			unlink_timer(otimer);
			break;
		}
	}

	if (!otimer->callback)
	{
		new_free((char **)&otimer->command);
		new_free((char **)&otimer->subargs);
	}
	new_free(&otimer->ref);
	new_free((char **)&otimer);
}

/*
 * schedule_timer: Submit a completed Timer to be executed later.
 *		   You must not change the Timer after it is submitted.
 *
 * Arguments:
 *	ntimer - A completely filled-in timer that needs to be executed.
 *	  	(a) ntimer->time must point to when the timer is to go off.
 *		(b) ntimer->prev and ->next will be overwritten.
 *		(c) You must not change 'ntimer' after this returns.
 *		(d) ntimer must not already be scheduled.
 *
 * Return Value:
 *	0 - The timer has been scheduled (always succeeds)
 */
static int	schedule_timer (Timer *ntimer)
{
	Timer *tmp, *prev;

	ntimer->fires = 0;

	/*
	 * If 'ntimer' is already scheduled, we will desschedule it,
	 * so that it may be re-inserted in the correct place.
	 */
	for (tmp = PendingTimers; tmp; tmp = tmp->next)
	{
		if (tmp == ntimer)
		{
			yell("schedule_timer: Warning: Scheduling a timer "
				"that is already scheduled.  Fixing that.");
			unlink_timer(ntimer);
			break;
		}
	}

	/* Figure out the correct place */
	for (tmp = PendingTimers; tmp; tmp = tmp->next)
	{
		if (time_diff(tmp->time, ntimer->time) < 0)
			break;
	}

	/* 'ntimer' is earlier than all the timers in PendingTimers */
	if (tmp == PendingTimers)
	{
		ntimer->prev = NULL;
		ntimer->next = PendingTimers;
		if (PendingTimers)
			PendingTimers->prev = ntimer;
		PendingTimers = ntimer;
	}
	/* 'ntimer' is earlier than 'tmp' but later than 'tmp->prev' */
	else if (tmp)
	{
		prev = tmp->prev;
		ntimer->next = tmp;
		ntimer->prev = prev;
		tmp->prev = ntimer;
		prev->next = ntimer;
	}
	/* 'ntimer' is later than every timer in PendingTimers */
	else
	{
		for (tmp = PendingTimers; tmp->next; tmp = tmp->next)
			;
		tmp->next = ntimer;
		ntimer->prev = tmp;
	}

	return 0;
}

/*
 * unlink_timer - Remove a Timer from the TimerList ("unschedule it")
 * 
 * Arguments:
 *	timer	- A Timer on the TimerList.
 *		  (a) It is an error to unlink a timer not on the timer list.
 *		      Doing so may result in a crash.
 *
 * Return Value:
 *	-1	- The timer was not scheduled (no change to 'timer')
 *	 0	- The timer is de-scheduled 
 */
static int	unlink_timer (Timer *timer)
{
	Timer *tmp, *prev, *next;

	/*
	 * We only modify 'timer' if it is actually scheduled.
	 * unlinking an unscheduled timer is a safe no-op.
	 */
	for (tmp = PendingTimers; tmp; tmp = tmp->next)
	{
		if (tmp == timer)
		{
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
	}

	/* I didn't find that timer on the schedule list */
	return -1;
}

/*
 * get_timer - Return a schedule Timer using its refname.
 *
 * Arguments:
 *	ref	- A Timer refname (timer->ref, from /TIMER -REF)
 *		 (a) ref must not be NULL or an empty string.
 *
 * Return Value:
 *	NULL	 - No scheduled timer exists by that name
 *	non-NULL - A Scheduled Timer by the given name.
 *		(a) You must _NOT_ change a Scheduled Timer unless
 *		    you unlink_timer() it first.
 *		(b) The best practice for changing a Scheduled Timer:
 *		    1) x = get_timer();
 *		    2) y = clone_timer(x);
 *		    3) unlink_timer(x);
 *		    4) <make changes to y>
 *		    5) schedule_timer(y);
 *		    6) delete_timer(x);
 */
static	Timer *get_timer (const char *ref)
{
	Timer *tmp;

	/* 'ref' must be a non-empty string */
	if (!ref || !*ref)
		return NULL;

	for (tmp = PendingTimers; tmp; tmp = tmp->next)
	{
		if (!my_stricmp(tmp->ref, ref))
			return tmp;
	}

	return NULL;
}

/*
 * timer_exists - Verify if a refnum is in use by a scheduled Timer.
 *
 * Arguments:
 *	ref	- A Timer Refname
 *
 * Return Value:
 *	-1	- 'ref' is an invalid refnum
 *	 0	- 'ref' is available for use (no scheduled timer exists)
 *	 1	- 'ref' is not available for use (already is in use)
 */
int 	timer_exists (const char *ref)
{
	if (!ref || !*ref)
		return -1;

	if (get_timer(ref))
		return 1;
	else
		return 0;
}

/*
 * dump_timers - show all timers in case of emergency
 */
void    dump_timers (void)
{
        Timer   *tmp;
        Timeval current;
        double  time_left;

        yell("*X*X*X*X*X*X*X*X*X* WARNING *X*X*X*X*X*X*X*X*X*X");
        yell("POLLING LOOP DETECTED -- IMPORTANT DEBUGGING INFO");
        yell("MAKE SURE TO SAVE THIS VERY IMPORTANT INFORMATION!");
        yell("%s", empty_string);
        say("Timer     Seconds   Events Command");

        get_time(&current);
        for (tmp = PendingTimers; tmp; tmp = tmp->next)
        {
                time_left = time_diff(current, tmp->time);
                if (time_left <= 0)
                    yell("--> %-10s %-10.2f %-7ld %ld %s", 
                                tmp->ref, time_left, tmp->events, 
				tmp->fires,
                                tmp->callback ? "SYSTEM" : tmp->command);
                else
                    yell("    %-10s %-10.2f %-7ld %ld %s", 
                                tmp->ref, time_left, tmp->events,
				tmp->fires,
                                tmp->callback ? "SYSTEM" : tmp->command);
        }
        yell("Make sure to give this list to hop on #epic on efnet!");
        yell("*X*X*X*X*X*X*X*X*X* WARNING *X*X*X*X*X*X*X*X*X*X");
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
		say("%-10s %-10.2f %-7ld %s", tmp->ref, time_left, 
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
static	int	create_timer_ref (const char *refnum_wanted, char **refnum_gets)
{
	Timer	*tmp;
	int 	refnum = 0;
	char	*refnum_want;
	int	i, pts;

	refnum_want = LOCAL_COPY(refnum_wanted);

	/* If the user doesnt care */
	if (*refnum_want == 0)
	{
		/* So ... we count the number of times that exist. */
		for (pts = 0, tmp = PendingTimers; tmp; tmp = tmp->next)
			pts++;

		/* 
		 * Now, for all the numbers (0 .. [timer count + 1]), 
		 * at least one of those numbers *has* to be available,
		 */ 
		for (i = 0; i <= pts + 1; i++)
		{
			/* Are any timers named 'i'? */
			for (tmp = PendingTimers; tmp; tmp = tmp->next)
			{
				if (!is_number(tmp->ref))
					continue;
				if (i == my_atol(tmp->ref))
					break;
			}

			/* 
			 * If 'tmp' is null, then we didn't find a refnum 'i'.
			 * So 'i' is our winner!
			 */
			if (tmp == NULL)
			{
				malloc_sprintf(refnum_gets, "%d", i);
				break;
			}
		}
	}
	else
	{
		/* See if the refnum is available */
		if (get_timer(refnum_want))
			return -1;		/* Already in use */

		malloc_strcpy(refnum_gets, refnum_want);
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

static	void	remove_timers_by_domref (int domain, int domref)
{
	Timer *ref, *next;

	for (ref = PendingTimers; ref; ref = next)
	{
		next = ref->next;
		if (ref->callback)
			continue;
		if (ref->domain != domain)
			continue;
		if (ref->domref != domref)
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
 *  refnum_want: The refnum requested.  
 *		 (1) User-supplied for /TIMER timers
 *		 (2) the empty string for system timers ("dont care")
 *  interval:	 How long until the timer should fire; 
 *		 (1) for repeating timers (events != 1), the timer will 
 *		     fire with this interval.
 *		 (2) for "snap" timers, the first fire will be the next
 *		     time time() % interval == 0.
 *  events:	 The number of times this event should fire.  
 *		 (1) The value -1 means "repeat forever"
 *		 (2) Timers automatically delete after they fire the
 *		     requested number of times.
 *
 * Scenario 1: You want to run ircII commands (/TIMER timers)
 * | callback:	 NULL
 * | commands:	 some ircII commands to run when the timer goes off
 * | subargs:	 what to use to expand $0's, etc in the 'what' variable.
 *
 * Scenario 2: You want to call an internal function (system timers)
 * | callback:	 function to call when timer goes off
 * | commands:	 argument to pass to "callback" function.  Should be some
 * |		 non-auto storage, perhaps a struct or a malloced char *
 * |		 array.  The caller is responsible for disposing of this
 * |		 area when it is called, since the timer mechanism does not
 * |		 know anything of the nature of the argument.
 * | subargs:	 should be NULL, its ignored anyhow.
 *
 *  domain:	What the TIMER should bind to:
 *		(a) SERVER_TIMER  - 'domref' refers to a server refnum.
 *			Each time this timer runs, set from_server to 'domref'
 *			and set the current window to whatever 'domref's 
 *			current window is at that time.
 *		(b) WINDOW_TIME - 'domref' refers to a window refnum.
 *			Each time this timer runs, set current_window to
 *			'domref' and set the current server to whatever 
 *			'domref's server is at that time.
 *		(c) GENERAL_TIMER - 'domref' is ignored.  
 *			Do not save or restore from_server or current_window: 
 *			run in whatever the context is when it goes off.
 *  domref:	Either a server refnum, window refnum, or -1 (see 'domain')
 *  cancelable:	A "Cancelable" timer will not fire if its context cannot be
 *		restored (ie, the server it is bound to is disconnected or
 *		deleted; or the window it is bound to is deleted).  A normal
 *		(non-cancelable) timer will turn into a GENERAL_TIMER if its
 *		context cannot be restored.
 *  snap:	A "snap" timer runs every time (time() % interval == 0).
 *		This is useful for things that (eg) run at the top of every 
 *		minute (60), hour (3600), or day (86400)
 */
char *add_timer (int update, const char *refnum_want, double interval, long events, int (callback) (void *), void *commands, const char *subargs, TimerDomain domain, int domref, int cancelable, int snap)
{
	Timer	*ntimer, *otimer = NULL;
	char *	refnum_got = NULL;
	Timeval right_now;
	char *	retval;

	right_now = get_time(NULL);

	/*
	 * We do this first, because if 'interval' is invalid, we don't
	 * want to do the expensive clone/create/delete operation.
 	 * It is ineligant to check for this error here.
	 */
	if (update == 1 && interval == -1)	/* Not changing the interval */
		(void) 0;	/* XXX sigh */
	else if (interval < 0.01 && events == -1)
	{
		say("You can't infinitely repeat a timer that runs more "
			"than 100 times a second.");
		return NULL;
	}

	/* 
	 * If we say we're updating; but the timer does not exist, 
	 * then we're not updating. ;-)
	 */
	if (update)
	{
	    if (!(otimer = get_timer(refnum_want)))
		update = 0;		/* Ok so we're not updating! */
	}

	/*
	 * Arrange for an appropriate Timer to be in 'ntimer'.
	 */
	if (update)
	{
		unlink_timer(otimer);
		ntimer = clone_timer(otimer);
		delete_timer(otimer);
	}
	else
	{
		if (create_timer_ref(refnum_want, &refnum_got) == -1)
		{
			say("TIMER: Refnum '%s' already exists", refnum_want);
			return NULL;
		}

		ntimer = new_timer();
		ntimer->ref = refnum_got;
	}

	/* Update the interval */
	if (update == 1 && interval == -1)
		(void) 0;	/* XXX sigh - not updating interval */
	else
	{
		ntimer->interval = double_to_timeval(interval);
		if (snap)
		{
			double x = time_to_next_interval(interval);
			ntimer->time = time_add(right_now, double_to_timeval(x));
		}
		else
			ntimer->time = time_add(right_now, ntimer->interval);
	}

	/* Update the repeat events */
	if (update == 1 && events == -2)
		(void) 0;	/* XXX sigh - not updating events */
	else
		ntimer->events = events;


	/* Update the callback */
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
		ntimer->callback = NULL;
		malloc_strcpy(&ntimer->command, (const char *)commands);
		malloc_strcpy(&ntimer->subargs, subargs);
	}

	/* Update the domain refnum */
	ntimer->domain = domain;
	if (update == 1 && domref == -1)
		(void) 0;	/* XXX sigh - not updating domref */
	else
		ntimer->domref = domref;

	/* Update the cancelable */
	if (update == 1 && cancelable == -1)
		(void) 0;	/* XXX sigh - not updating cancelable */
	else
		ntimer->cancelable = cancelable;


	/* Schedule up the new/updated timer! */
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
	PendingTimers->fires++;
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

		if (current->domain == SERVER_TIMER)
		{
		    if (!is_server_valid(current->domref))
		    {
			if (current->cancelable)
			    goto advance;
			/* Otherwise, pretend you were a  "GENERAL" type */
		    }
		    else
		    {
			from_server = current->domref;
			make_window_current_by_refnum(
					get_winref_by_servref(from_server));
		    }
		}
		else if (current->domain == WINDOW_TIMER)
		{
		    if (!get_window_by_refnum(current->domref))
		    {
			if (current->cancelable)
			    goto advance;
			/* Otherwise, pretend you were a "GENERAL" type */
		    }
		    else
		    {
			make_window_current_by_refnum(current->domref);
			from_server = current_window->server;
		    }
		}
		else
		{
		    /* General timers focus on the current window. */
		    if (current_window)
		    {
			if (current_window->server != from_server)
			    from_server = current_window->server;
		    }
		    else
		    {
			if (from_server != NOSERV)
			    make_window_current_by_refnum(
				get_winref_by_servref(from_server));
		    }
		}

		/* 
		 * If a callback function was registered, then
		 * we use it.  If no callback function was registered,
		 * then we call the lambda function.
		 */
		get_time(&right_now);
		now = right_now;
		if (current->callback)
			(*current->callback)(current->callback_data);
		else
			call_lambda_command("TIMER", current->command,
							current->subargs);

		from_server = old_from_server;
		make_window_current_by_refnum(old_refnum);
advance:
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

	GET_FUNC_ARG(listc, input);
	len = strlen(listc);
	if (!my_strnicmp(listc, "REFNUM", len)) {
		GET_FUNC_ARG(refstr, input);
		if (!(t = get_timer(refstr)))
			RETURN_EMPTY;
		RETURN_STR(t->ref);
	} else if (!my_strnicmp(listc, "REFNUMS", len)) {
		char *	retval = NULL;
		size_t	clue = 0;

		for (t = PendingTimers; t; t = t->next)
			malloc_strcat_word_c(&retval, space, t->ref, DWORD_DWORDS, &clue);
		RETURN_MSTR(retval);
	} else if (!my_strnicmp(listc, "ADD", len)) {
		RETURN_EMPTY;		/* XXX - Not implemented yet. */
	} else if (!my_strnicmp(listc, "DELETE", len)) {
		GET_FUNC_ARG(refstr, input);
		if (!(t = get_timer(refstr)))
			RETURN_EMPTY;
		if (t->callback)
			RETURN_EMPTY;
		RETURN_INT(remove_timer(refstr));
	} else if (!my_strnicmp(listc, "GET", len)) {
		GET_FUNC_ARG(refstr, input);
		if (!(t = get_timer(refstr)))
			RETURN_EMPTY;

		GET_FUNC_ARG(listc, input);
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
			if (t->domain != SERVER_TIMER)
				RETURN_INT(-1);
			RETURN_INT(t->domref);
		} else if (!my_strnicmp(listc, "WINDOW", len)) {
			if (t->domain != WINDOW_TIMER)
				RETURN_INT(-1);
			RETURN_INT(t->domref);
		}
	} else if (!my_strnicmp(listc, "SET", len)) {
		GET_FUNC_ARG(refstr, input);
		if (!(t = get_timer(refstr)))
			RETURN_EMPTY;

		/* Changing internal system timers is strictly prohibited */
		if (t->callback)
			RETURN_EMPTY;

		GET_FUNC_ARG(listc, input);
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
			t->domain = SERVER_TIMER;
			t->domref = refnum;
		} else if (!my_strnicmp(listc, "WINDOW", len)) {
			int	refnum;

			GET_INT_ARG(refnum, input);
			t->domain = WINDOW_TIMER;
			t->domref = refnum;
		}
	} else
		RETURN_EMPTY;

	RETURN_EMPTY;
}

/*
 * The /WINDOW NUMBER command actually swaps the refnums of two windows:
 * It's possible that 'newref' isn't in use, so that's ok.
 */
void    timers_swap_winrefs (unsigned oldref, unsigned newref)
{
	Timer *ref;

	for (ref = PendingTimers; ref; ref = ref->next)
        {
                if (ref->domain != WINDOW_TIMER)
                        continue;

		/* Window refnums are "unsigned",
		 *  but timer domain refnums aren't */
		if (ref->domref == (int)newref)
			ref->domref = oldref;
		else if (ref->domref == (int)oldref)
			ref->domref = newref;
        }
}

void    timers_merge_winrefs (unsigned oldref, unsigned newref)
{
	Timer *ref;

	for (ref = PendingTimers; ref; ref = ref->next)
        {
                if (ref->domain != WINDOW_TIMER)
                        continue;

		if (ref->domref == (int)oldref)
			ref->domref = newref;
        }
}


