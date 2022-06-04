/*
 * clock.c -- The status bar clock ($Z), cpu saver, and system timers
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright 1997, 2003 EPIC Software Labs
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

#include "irc.h"
#include "ircaux.h"
#include "clock.h"
#include "hook.h"
#include "vars.h"
#include "server.h"
#include "status.h"
#include "screen.h"
#include "termx.h"
#include "timer.h"
#include "input.h"
#include "mail.h"
#include "notify.h"
#include "output.h"

/************************************************************************/
/*			SYSTEM CLOCK					*/
/************************************************************************/
/* 
 * XXX This is bogus -- it gets set when you do /set clock_format.
 * The reason we use a global variable is to cut down on the number of
 * calls to get_string_var, which is truly bogus, but neccesary for any
 * semblance of efficiency.
 */
	char		*time_format = NULL;	/* XXX Bogus XXX */
static	const char	*strftime_24hour = "%R";
static	const char	*strftime_12hour = "%I:%M%p";
static	char		current_clock[256];
	char		clock_timeref[] = "CLKTIM";

static void	reset_broken_clock (void)
{
	snprintf(current_clock, sizeof current_clock, 
		"%c12%c:%c00AM%c",
		BLINK_TOG, BLINK_TOG, BLINK_TOG, BLINK_TOG);
}

static void	reset_standard_clock (void)
{
static		int	min = -1;
static		int	hour = -1;
		Timeval	tv;
	struct 	tm	time_val;
		time_t	hideous;

	/* 
	 * Every time /set clock_interval goes off this function calls
	 * localtime() to update $Z.  Localtime() is very expensive, so
	 * if you /set clock_interval too low you will use a lot of cpu.
	 */
	get_time(&tv);
	hideous = tv.tv_sec;
	time_val = *localtime(&hideous);

	if (time_format)	/* XXX Bogus XXX */
		strftime(current_clock, sizeof current_clock, 
				time_format, &time_val);
	else if (get_int_var(CLOCK_24HOUR_VAR))
		strftime(current_clock, sizeof current_clock,
				strftime_24hour, &time_val);
	else
		strftime(current_clock, sizeof current_clock,
				strftime_12hour, &time_val);

	/*
	 * Either way we only update /on timer and /on idle every minute.
	 */
	if ((time_val.tm_min != min) || (time_val.tm_hour != hour))
	{
		int	old_server = from_server;

		hour = time_val.tm_hour;
		min = time_val.tm_min;

		from_server = primary_server;
		do_hook(TIMER_LIST, "%02d:%02d", hour, min);
		do_hook(IDLE_LIST, INTMAX_FORMAT, (intmax_t)(tv.tv_sec - idle_time.tv_sec) / 60);
		from_server = old_server;
	}
}

static void	reset_metric_clock (void)
{
static	long	last_milliday;
	double	metric_time;
	long	current_milliday;

	get_metric_time(&metric_time);
	current_milliday = (long)metric_time;

	snprintf(current_clock, sizeof current_clock, 
			"%ld", (long)metric_time);

	if (current_milliday != last_milliday)
	{
		int	old_server = from_server;
		int	idle_milliday;

		idle_milliday = (int)(timeval_to_metric(&idle_time).mt_mdays);
		from_server = primary_server;
		do_hook(TIMER_LIST, "%03ld", current_milliday);
		do_hook(IDLE_LIST, "%ld", 
			(long)(idle_milliday - current_milliday));
		from_server = old_server;
		last_milliday = current_milliday;
	}
}

void	reset_clock (void *stuff)
{
	const char *	sclock;

	if (x_debug & DEBUG_BROKEN_CLOCK)
		reset_broken_clock();
	else if (get_int_var(METRIC_TIME_VAR))
		reset_metric_clock();
	else
		reset_standard_clock();

	sclock = get_string_var(STATUS_CLOCK_VAR);
	if (sclock && *sclock)
		update_all_status();
}

/* update_clock: figures out the current time and returns it in a nice format */
const char *	get_clock (void)
{
	return current_clock;
}

void	clock_systimer (void)
{
	/* reset_clock does `update_all_status' for us */
	reset_clock(NULL);
}

void    set_clock_interval (void *stuff)
{
	update_system_timer(clock_timeref);	/* XXX Oh heck, why not? */
}

void     set_clock_format (void *stuff)
{
	const VARIABLE *v;
	const char *value;

	v = stuff;
	value = v->string;
        malloc_strcpy(&time_format, value);
        reset_clock(NULL);
}

void	my_set_clock (void *stuff)
{
	update_system_timer(clock_timeref);
	update_all_status();
}

/************************************************************************/
/*			CPU SAVER WATCHDOG				*/
/************************************************************************/
	int	cpu_saver = 0;

BUILT_IN_KEYBINDING(cpu_saver_on)
{
        cpu_saver = 1;
        update_all_status(); 
}

static const char cpu_saver_timeref[] = "CPUTIM";

/*
 * This is a watchdog timer that checks to see when we're idle enough to 
 * turn on CPU SAVER mode.  The above timers may honor what we do here but
 * they're not required to.
 */
int	cpu_saver_timer (void *schedule_only)
{
	double	interval;
	double	been_idlin;

	if (cpu_saver)
		return 0;

	get_time(&now);
	been_idlin = time_diff(idle_time, now);
	interval = get_int_var(CPU_SAVER_AFTER_VAR) * 60;

	if (interval < 1)
		return 0;

	if (been_idlin > interval)
		cpu_saver_on(0, NULL);
	else
		add_timer(1, cpu_saver_timeref, interval - been_idlin, 
				1, cpu_saver_timer, NULL, NULL, 
				GENERAL_TIMER, -1, 0, 0);
	return 0;
}

void    set_cpu_saver_after (void *stuff)
{
	const VARIABLE *v;
	int	value;

	v = stuff;
	value = v->integer;

        if (value == 0)
	{
		/* Remove the watchdog timer only if it is running. */
		if (timer_exists(cpu_saver_timeref))
			remove_timer(cpu_saver_timeref);
	}
	else
                cpu_saver_timer(NULL);
}

void	set_cpu_saver_every (void *stuff)
{
	VARIABLE *v;
	int	value;

	v = stuff;
	value = v->integer;

	if (value < 60)
	{
		say("/SET CPU_SAVER_EVERY must be set to at least 60");
		v->integer = 60;
	}
}

/************************************************************************/
/*			SYSTEM TIMERS					*/
/************************************************************************/
struct system_timer {
	char *	name;
	int	honors_cpu_saver;
	int *	interval_variable;
	int *	toggle_variable;
	Timeval	last_event;
	void	(*callback) (void);
};

struct system_timer system_timers[] = {
	{ clock_timeref, 	1, 
	  &CLOCK_INTERVAL_VAR, 	&CLOCK_VAR, 	{ 0, 0 },
	  clock_systimer 	},
	{ notify_timeref, 	1, 
	  &NOTIFY_INTERVAL_VAR,  &NOTIFY_VAR, 	{ 0, 0 },
	  notify_systimer	},
	{ mail_timeref, 	1, 
	  &MAIL_INTERVAL_VAR, 	&MAIL_VAR, 	{ 0, 0 },
	  mail_systimer 	},
	{ NULL,			0, 
	  0,			0,		{ 0, 0 },
	  NULL 			}
};

/*
 * This is a unified callback function that handles the processing for all
 * of the system timers.  
 * It calculates how long until the callback should be called again:
 *	If CPU SAVER mode is on, then /set CPU_SAVE_EVERY seconds
 *	If it is off, then the next round /SET *_INTERVAL seconds
 *	  where * is "CLOCK", "NOTIFY" or "MAIL".
 * Then it calls the processing callback ("callback") to do the work.
 */
static int	system_timer (void *entry)
{
	double	timeout = 0;
	int	nominal_timeout;
	struct system_timer *item = NULL;

	item = entry;

	if (item->honors_cpu_saver && cpu_saver)
	{
	    nominal_timeout = get_int_var(CPU_SAVER_EVERY_VAR);
	    timeout = nominal_timeout;
	}
	else
	{
	    nominal_timeout = get_int_var(*item->interval_variable);
	    timeout = time_to_next_interval(nominal_timeout);
	}

	add_timer(1, item->name, timeout, 1, system_timer, entry, NULL, 
				GENERAL_TIMER, -1, 0, 0);

	item->callback();
	item->last_event = now;
	return 0;
}

int	update_system_timer (const char *entry)
{
	int	i;
	int	all = 0;

	if (entry == NULL)
		all = 1;

	for (i = 0; system_timers[i].name; i++)
	{
	    if (all == 1 || !strcmp(system_timers[i].name, entry))
	    {
		/* This needs to be set before calling 'system_timer' */
		get_time(&now);

		if (get_int_var(*system_timers[i].toggle_variable) &&
		    get_int_var(*system_timers[i].interval_variable))
			system_timer(&system_timers[i]);
		else
		{
		    if (timer_exists(system_timers[i].name))
			remove_timer(system_timers[i].name);
		}

		if (all == 0)
			return 0;
	    }
	}

	if (all == 1)
		return 0;

	return -1;
}

/**************************************************************************/
/*
 * When cpu saver mode is turned off (by the user pressing a key), then
 * we immediately do all of the system timers and then they will reset
 * themselves to go off as regular.
 */
void	reset_system_timers (void)
{
	cpu_saver = 0;
	update_system_timer(NULL);
	cpu_saver_timer(NULL);
}

