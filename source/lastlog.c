/* $EPIC: lastlog.c,v 1.84 2012/06/24 23:07:54 jnelson Exp $ */
/*
 * lastlog.c: handles the lastlog features of irc. 
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright © 1993, 2003 EPIC Software Labs.
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
#include "levels.h"
#include "lastlog.h"
#include "window.h"
#include "screen.h"
#include "vars.h"
#include "ircaux.h"
#include "output.h"
#include "numbers.h"
#include "functions.h"
#include "reg.h"
#include "alias.h"

typedef struct	lastlog_stru
{
	int	level;
	char	*target;
	char	*msg;
	struct	lastlog_stru	*older;
	struct	lastlog_stru	*newer;
	time_t	when;
	int	visible;
	intmax_t refnum;
	unsigned winref;
	int	dead;
}	Lastlog;

static	intmax_t global_lastlog_refnum = 0;

static int	show_lastlog (Lastlog **l, int *skip, int *number, Mask *level_mask, char *match, regex_t *rex, char *nomatch, int *max, const char *target, int mangler, unsigned winref, char **);
static int	oldest_lastlog_for_window (Lastlog **item, unsigned winref);
static int	newer_lastlog_entry (Lastlog **item, unsigned winref);
static int	older_lastlog_entry (Lastlog **item, unsigned winref);
static int	newest_lastlog_for_window (Lastlog **item, unsigned winref);
static void	remove_lastlog_item (Lastlog *item);
static void	switch_lastlog_window (Lastlog *item, unsigned newref);
static void	move_lastlog_item (Lastlog *item, unsigned newref);

Lastlog *	lastlog_oldest = NULL;
Lastlog *	lastlog_newest = NULL;

/**********************************************************************/
/*
 * lastlog_level: current bitmap setting of which things should be stored in
 * the lastlog.  The LOG_MSG, LOG_NOTICE, etc., defines tell more about this 
 */
static	Mask	lastlog_mask;
static	Mask	notify_mask;
	Mask *	new_server_lastlog_mask = NULL;
	Mask *	old_server_lastlog_mask = NULL;
	Mask 	current_window_mask;

/*
 * set_lastlog_mask: called whenever a "SET LASTLOG_LEVEL" is done.  It
 * parses the settings and sets the lastlog_mask variable appropriately.  It
 * also rewrites the LASTLOG_LEVEL variable to make it look nice 
 */
void	set_lastlog_mask (void *stuff)
{
	VARIABLE *v;
	const char *str;
	char *rejects = NULL;

	v = (VARIABLE *)stuff;
	str = v->string;

	if (str_to_mask(&lastlog_mask, str, &rejects))
		standard_level_warning("/SET LASTLOG_LEVEL", &rejects);
	malloc_strcpy(&v->string, mask_to_str(&lastlog_mask));

	current_window->lastlog_mask = lastlog_mask;
}

void	set_new_server_lastlog_mask (void *stuff)
{
	VARIABLE *v;
	const char *str;
	char *rejects = NULL;

	v = (VARIABLE *)stuff;
	str = v->string;

	if (str)
	{
	    if (!new_server_lastlog_mask)
	    {
		new_server_lastlog_mask = (Mask *)new_malloc(sizeof(Mask));
		mask_unsetall(new_server_lastlog_mask);
	    }
	    if (str_to_mask(new_server_lastlog_mask, str, &rejects))
		standard_level_warning("/SET NEW_SERVER_LASTLOG_LEVEL", &rejects);
	    malloc_strcpy(&v->string, mask_to_str(new_server_lastlog_mask));
	}
	else
	    new_free(&new_server_lastlog_mask);
}

void	set_old_server_lastlog_mask (void *stuff)
{
	VARIABLE *v;
	const char *str;
	char *rejects = NULL;

	v = (VARIABLE *)stuff;
	str = v->string;

	if (str)
	{
	    if (!old_server_lastlog_mask)
	    {
		old_server_lastlog_mask = (Mask *)new_malloc(sizeof(Mask));
		mask_unsetall(old_server_lastlog_mask);
	    }
	    if (str_to_mask(old_server_lastlog_mask, str, &rejects))
		standard_level_warning("/SET OLD_SERVER_LASTLOG_LEVEL", &rejects);
	    malloc_strcpy(&v->string, mask_to_str(old_server_lastlog_mask));
	}
	else
	    new_free(&old_server_lastlog_mask);
}

Mask	real_notify_mask (void)
{
	return (notify_mask);
}

Mask	real_lastlog_mask (void)
{
	return (lastlog_mask);
}

void	set_notify_mask (void *stuff)
{
	VARIABLE *v;
	const char *str;
	char *rejects = NULL;

	v = (VARIABLE *)stuff;
	str = v->string;

	if (str_to_mask(&notify_mask, str, &rejects))
		standard_level_warning("/SET NOTIFY_LEVEL", &rejects);
	malloc_strcpy(&v->string, mask_to_str(&notify_mask));

	current_window->notify_mask = notify_mask;
}

void	set_current_window_mask (void *stuff)
{
	VARIABLE *v;
	const char *str;
	char *rejects = NULL;

	v = (VARIABLE *)stuff;
	str = v->string;

	if (str_to_mask(&current_window_mask, str, &rejects))
		standard_level_warning("/SET CURRENT_WINDOW_LEVEL", &rejects);
	malloc_strcpy(&v->string, mask_to_str(&current_window_mask));
}


/**********************************************************************/
/*
 * add_to_lastlog: adds the line to the lastlog.  If the LASTLOG_CONVERSATION
 * variable is on, then only those lines that are user messages (private
 * messages, channel messages, wall's, and any outgoing messages) are
 * recorded, otherwise, everything is recorded 
 */
intmax_t	add_to_lastlog (Window *window, const char *line)
{
	Lastlog *new_l;

	if (!window)
		window = current_window;

	new_l = (Lastlog *)new_malloc(sizeof(Lastlog));
	new_l->dead = 0;
	new_l->refnum = global_lastlog_refnum++;
	new_l->older = lastlog_newest;
	new_l->newer = NULL;
	new_l->level = who_level;
	new_l->msg = malloc_strdup(line);
	new_l->winref = window->refnum;
	if (who_from)
		new_l->target = malloc_strdup(who_from);
	else
		new_l->target = NULL;
	time(&new_l->when);

	/* * * */
	if (lastlog_newest && lastlog_newest->dead)
		panic(1, "Lastlog_newest is dead");

	if (lastlog_newest)
		lastlog_newest->newer = new_l;
	lastlog_newest = new_l;

	if (!lastlog_oldest)
		lastlog_oldest = lastlog_newest;

	if (mask_isset(&window->lastlog_mask, who_level))
	{
		new_l->visible = 1;
		window->lastlog_size++;
		trim_lastlog(window);
	}
	else
		new_l->visible = 0;

	/* * * */
	return new_l->refnum;
}


void 	trim_lastlog (Window *window)
{
	Lastlog *item = NULL;

	while (window->lastlog_size > window->lastlog_max)
	{
		item = NULL;
		if (oldest_lastlog_for_window(&item, window->refnum))
		{
			if (item->visible)
				window->lastlog_size--;
			remove_lastlog_item(item);
		}
	}
}

/*
 * set_lastlog_size: sets up a lastlog buffer of size given.  If the lastlog
 * has gotten larger than it was before, all newer lastlog entries remain.
 * If it get smaller, some are deleted from the end. 
 */
void	set_lastlog_size (void *stuff)
{
	VARIABLE *v;
	int	size;
	Window	*window = NULL;

	v = (VARIABLE *)stuff;
	size = v->integer;

	while (traverse_all_windows(&window))
	{
		window->lastlog_max = size;
		trim_lastlog(window);
	}
}

/*
 * The /LASTLOG command:
 * Syntax:
 *	/LASTLOG [options] [<string>] [<number1> [<number2>]]
 * Options:
 *	-			Do not show header and footer
 *	-reverse		Show matches newest-to-oldest (reverse order)
 *			        instead of oldest-to-newest (normal order)
 * 	-literal <pattern>	line must wildcard match <pattern>
 *	-regex <regex>		line must match <regex>.
 *	-ignore <pattern>	line must NOT wldcard match <pattern>
 *	-max <number>		Only show first <number> matches
 *	-skip <number>		Skip this many leading lastlog entries
 *	-number <number>	Only search this many lastlog entries
 *	-rewrite "<expando>"	Rewrite each line with the expando.
 *	-<LEVEL>		Add <LEVEL> to "level mask"
 *	--<LEVEL>		Remove <LEVEL> from "level mask"
 *	-ALL			Add all levels to "level mask"
 *	--ALL			Reset/clear the "level mask" <default>
 *	<pattern>		<pattern> is search target
 *	<number1>		Start from the <number1>th most recent record.
 *	<number2>		Continue <number2> more records after that.
 *
 * The /LASTLOG command shows all of the lines that have appeared to your
 * window's lastlog; the oldest one first, and the newest one last, except
 * that the following seven restrictions apply IN THIS ORDER:
 *	1 If the -reverse option is specified, lines will be shown in
 *	  "reverse order", that is the newest one first, and the oldest
 *	  one last.
 *	2 If any of the "LEVEL" options (including ALL) are specified,
 *	  then the line must have one of the lastlog levels specified
 *	  by the "level mask".
 *	3 If the -number option is specified only the most recent <number> 
 *	  lines will be looked at.
 *	4 If the -skip option is specified, the first <number> lines after
 *        the start (either the oldest entry, or whatever you specified
 *        with the -number option) will be skipped (will not be looked at).
 *	5 If the -literal option is specified, some portion of the line
 *	  must be matched by the pattern <pattern>
 *	6 If the -regex option is specified, some portion of the line
 *	  must be matched by the regex <regex>
 *	7 If the -max option is specified, only the first <number> matching
 *	  lines will be shown; others will be suppressed.
 * Furthermore:
 *      * The "level mask" is turned off by default which means that all
 *	  rule #2 doesn't apply.  --ALL forcibly turns off the level mask.
 *	* "LEVEL" options are cumulative and sequential.  That means that
 *	  if you turn off a level and then later turn it back on, it will
 *	  be on.  If you turn off all options with --ALL and then turn 
 *	  some back on, those after --ALL will be on.
 *	* Up to three naked options may be specified for backwards 
 *	  compatability with ircII.  The very first naked parameter that 
 *	  is not a number is considered to be the argument to the -LITERAL 
 *	  option.  The second naked parameter had better be a number and
 *	  it is taken as the argument to the -NUMBER option.  The third naked
 *	  parameter also needs to be a number and is taken as the argument
 *	  to the -SKIP option.  This can get confusing; always use the
 *	  options in scripts -- only use backwards compatability options
 *	  at the input prompt. ;-)
 */
BUILT_IN_COMMAND(lastlog)
{
	int		reverse = 0;
	Mask		level_mask;
	int		skip = -1;
	int		number = INT_MAX;
	int		max = -1;
	char *		match = NULL;
	char *		nomatch = NULL;
	char *		target = NULL;
	char *		regex = NULL;
	Lastlog *	start;
	Lastlog *	end;
	Lastlog *	l;
	Lastlog *	lastshown;
	regex_t 	realreg;
	regex_t *	rex = NULL;
	int		cnt;
	char *		arg;
	int		header = 1;
	Mask		save_mask;
	int		before = -1;
	int		after = 0;
	int		counter = 0;
	int		show_separator = 0;
	const char *	separator = "----";
	char *		outfile = NULL;
	FILE *		outfp = NULL;
	int		mangler = 0;
	int		lc;
	char *		rewrite = NULL;
	unsigned	winref = current_window->refnum;

	lc = message_setall(0, NULL, LEVEL_OTHER);
	cnt = current_window->lastlog_size;
	save_mask = current_window->lastlog_mask;
	mask_unsetall(&current_window->lastlog_mask);
	mask_unsetall(&level_mask);
	rewrite = get_string_var(LASTLOG_REWRITE_VAR);

	while ((arg = new_next_arg(args, &args)) != NULL)
	{
	    size_t	len = strlen(arg);

	    if (!strcmp(arg, "-"))
		header = 0;
	    else if (!my_strnicmp(arg, "-LITERAL", len))
	    {
		if (!(match = new_next_arg(args, &args)))
		{
			yell("LASTLOG -LITERAL requires an argument.");
			goto bail;
		}
	    }
	    else if (!my_strnicmp(arg, "-REGEX", len))
	    {
		if (!(regex = new_next_arg(args, &args)))
		{
			yell("LASTLOG -REGEX requires an argument.");
			goto bail;
		}
	    }
	    else if (!my_strnicmp(arg, "-IGNORE", len))
	    {
		if (!(nomatch = new_next_arg(args, &args)))
		{
			yell("LASTLOG -IGNORE requires an argument.");
			goto bail;
		}
	    }
	    else if (!my_strnicmp(arg, "-TARGET", len))
	    {
		if (!(target = new_next_arg(args, &args)))
		{
			yell("LASTLOG -TARGET requires an argument.");
			goto bail;
		}
	    }
	    else if (!my_strnicmp(arg, "-MAXIMUM", len))
	    {
		char *x = new_next_arg(args, &args);
		if (!is_number(x))
		{
			yell("LASTLOG -MAXIMUM requires a numerical argument.");
			goto bail;
		}
		if ((max = my_atol(x)) < 0)
		{
			yell("LASTLOG -MAXIMUM argument must be "
					"a positive number.");
			goto bail;
		}
	    }
	    else if (!my_strnicmp(arg, "-MANGLE", len))
	    {
		char *	x = new_next_arg(args, &args);

		mangler = parse_mangle(x, mangler, NULL);
	    }
	    else if (!my_strnicmp(arg, "-SKIP", len))
	    {
		char *x = new_next_arg(args, &args);
		if (!is_number(x))
		{
			yell("LASTLOG -SKIP requires a numerical argument.");
			goto bail;
		}
		if ((skip = my_atol(x)) < 0)
		{
			yell("LASTLOG -SKIP argument must be "
					"a positive number.");
			goto bail;
		}
	    }
	    else if (!my_strnicmp(arg, "-NUMBER", len))
	    {
		char *x = new_next_arg(args, &args);
		if (!is_number(x))
		{
			yell("LASTLOG -NUMBER requires a numerical argument.");
			goto bail;
		}
		if ((number = my_atol(x)) < 0)
		{
			yell("LASTLOG -NUMBER argument must be "
					"a positive number.");
			goto bail;
		}
	    }
	    else if (!my_strnicmp(arg, "-CONTEXT", len))
	    {
		char *x, *before_str, *after_str;

		x = new_next_arg(args, &args);
		before_str = x;
		if (x && (after_str = strchr(x, ',')))
			*after_str++ = 0;

		if (!x || !is_number(before_str))
		{
			yell("LASTLOG -CONTEXT requires a numeric argument.");
			goto bail;
		}
		if ((before = my_atol(x)) < 0)
			before = 0;

		if (!after_str)
			after = before;
		else
			after = my_atol(after_str);
		if (after < 0)
			after = 0;
	    }
	    else if (!my_strnicmp(arg, "-FILE", len))
	    {
		outfile = new_next_arg(args, &args);
	    }
	    else if (!my_strnicmp(arg, "-SEPARATOR", len))
	    {
		char *x;
		if ((x = new_next_arg(args, &args)))
			separator = x;
		else
			separator = NULL;
	    }
	    else if (!my_strnicmp(arg, "-REVERSE", len))
		reverse = 1;
	    else if (!my_strnicmp(arg, "-ALL", len))
		mask_setall(&level_mask);
	    else if (!my_strnicmp(arg, "--ALL", len))
		mask_unsetall(&level_mask);
	    else if (!my_strnicmp(arg, "--", 2))
	    {
		int	i = str_to_level(arg+2);
		if (i != -1)
			mask_unset(&level_mask, i);
		else
		{
			say("Unknown flag: %s", arg);
			goto bail;
		}
	    }
	    else if (!my_strnicmp(arg, "-REWRITE", len))
	    {
		rewrite = new_next_arg(args, &args);
	    }
	    else if (!my_strnicmp(arg, "-WINDOW", len))
	    {
		Window *w;
		char *x;

		x = new_next_arg(args, &args);
		if (!(w = get_window_by_desc(x)))
		{
			yell("LASTLOG -WINDOW %s is not a valid window "
				"name or number", x);
			goto bail;
		}
		winref = w->refnum;
	    }
	    else if (!my_strnicmp(arg, "-", 1))
	    {
		int	i = str_to_level(arg+1);
		if (i != -1)
			mask_set(&level_mask, i);
		else
		{
			say("Unknown flag: %s", arg);
			goto bail;
		}
	    }
	    else if (is_number(arg) && number == INT_MAX)
	    {
		if ((number = my_atol(arg)) < 0)
		{
			yell("LASTLOG requires a positive number argument.");
			goto bail;
		}
	    }
	    else if (is_number(arg) && number != INT_MAX)
	    {
		if ((skip = my_atol(arg)) < 0)
		{
			yell("LASTLOG requires a positive number argument.");
			goto bail;
		}
	    }
	    else
		match = arg;
	}

	/* Normalize arguments here */
	if (skip > cnt)
	{
		if (x_debug & DEBUG_LASTLOG)
			yell("l: skip [%d] > cnt [%d]", skip, cnt);
		goto bail;	/* Skipping the entire thing is a no-op */
	}
	if (skip >= 0 && number <= 0)
	{
		if (x_debug & DEBUG_LASTLOG)
			yell("l: number [%d] <= 0", number);
		goto bail;		/* Iterating 0 records is a no-op */
	}
	if (max == 0)
	{
		if (x_debug & DEBUG_LASTLOG)
			yell("l: max == 0");
		goto bail;		/* Displaying 0 records is a no-op */
	}
	if (match)
	{
		size_t	size;
		char *	blah;

		size = strlen(match) + 3;
		blah = alloca(size);
		snprintf(blah, size, "*%s*", match);
		match = blah;
	}
	if (nomatch)
	{
		size_t	size;
		char *	blah;

		size = strlen(nomatch) + 3;
		blah = alloca(size);
		snprintf(blah, size, "*%s*", nomatch);
		nomatch = blah;
	}
	if (regex)
	{
		int	options = REG_EXTENDED | REG_ICASE | REG_NOSUB;
		int	errcode;

		if ((errcode = regcomp(&realreg, regex, options)))
		{
			char errmsg[1024];
			regerror(errcode, &realreg, errmsg, 1024);
			yell("%s", errmsg);
			goto bail;
		}
		rex = &realreg;
	}

	if (x_debug & DEBUG_LASTLOG)
	{
		yell("Lastlog summary status:");
		yell("Pattern: [%s]", match ? match : "<none>");
		yell("Regex: [%s]", regex ? regex : "<none>");
		yell("Ignore: [%s]", nomatch ? nomatch : "<none>");
		yell("Target: [%s]", target ? target : "<none>");
		yell("Header: %d", header);
		yell("Reverse: %d", reverse);
		yell("Skip: %d", skip);
		yell("Number: %d", number);
		yell("Max: %d", max);
		yell("Mask: %s", mask_to_str(&level_mask));
	}

	if (outfile)
	{
		if ((outfp = fopen(outfile, "a")) == NULL)
		{
			say("Couldn't open output file");
			goto bail;
		}
	}

	/* Iterate over the lastlog here */
	if (header)
		file_put_it(outfp, "%s Lastlog:", banner());

	/*
	 * Ugh.  This is way too complicated for its own good.  Let's
	 * take this one step at a time.  We can either go forwards or
	 * in reverse.  Let's consider this generically:
	 *
	 * ALWAYS BEFORE WE "GO TO THE NEXT ITEM" if "counter" 
	 * is greater than one we print the current item.
	 *
	 *	If 'counter' is greater than 0, decrease it by one.
	 *	If we haven't skipped 'skip' items, go to the next item.
	 *	If we have seen 'number' items already, then stop.
	 *	If this item isn't of a level in 'level_mask' go to next item.
	 *	If this item isn't matched by 'match' go to next item.
	 *	If this item isn't regexed by 'reg' go to next item.
	 *	-- At this point, the item "matches" and should be displayed.
	 *	If "counter" is 0, then this is a new match on its own.
	 *	   Go back "context" items, and set "counter" to "distance".
	 *	Otherwise, if "counter" is not 0, then we are in the middle
	 *	   of a previous match.  Do not go back, but set "counter"
	 *	   to "distance" (to make sure we keep outputting).
	 */
	if (reverse == 0)
	{
	    int i = 0;

	    for (start = end = lastlog_newest; start != lastlog_oldest; )
	    {
		if (start->visible && start->winref == winref)
		{
		    if (mask_isnone(&level_mask) || 
				(mask_isset(&level_mask, start->level)))
			i++;
		    if (i == number)
			break;
		}
		start = start->older;
	    }

	    lastshown = NULL;
	    for (l = start; l; (void)(l && (l = l->newer)))
	    {
		char *result = NULL;

		if (show_lastlog(&l, &skip, &number, &level_mask, 
				match, rex, nomatch, &max, target, 
				mangler, winref, &result))
		{
		    if (counter == 0 && before > 0)
		    {
			counter = 1;
			for (i = 0; i < before; i++)
			{
			    if (l && l == lastshown)
			    {
				if (l->newer)
				    l = l->newer;
				show_separator = 0;
				break;
			    }

			    if (l && l->older)
			    {
				l = l->older;
				counter++;
			    }
			}
		    }
		    else if (after != -1)
			counter = after + 1;
		    else
			counter = 1;

		    if (show_separator)
		    {
			file_put_it(outfp, "%s", separator);
			show_separator = 0;
		    }

		    if (counter)
		    {
			if (rewrite)
			{
				unsigned char *n, vitals[10240];

				snprintf(vitals, sizeof(vitals),
					"%ld %ld %ld %ld . . . %s %s",
						(long)l->refnum,
						(long)l->when,
						(long)l->winref,
						(long)l->level,
						l->target?l->target:".",
						result?result:".");

				n = expand_alias(rewrite, vitals);
				file_put_it(outfp, "%s", n);
				new_free(&n);
			}
			else
				file_put_it(outfp, "%s", result);

			lastshown = l;
			counter--;
			if (counter == 0 && before != -1 && separator)
				show_separator = 1;
		    }
		}

		new_free(&result);
		if (l == end)
			break;
	    }
	}
	else
	{
	    int i = 0;

	    for (start = end = lastlog_newest; end != lastlog_oldest; )
	    {
		if (end->visible && end->winref == winref)
		{
		    if (mask_isnone(&level_mask) || 
				(mask_isset(&level_mask, end->level)))
			i++;
		    if (i == number)
			break;
		}
		end = end->older;
	    }

	    lastshown = NULL;
	    for (l = start; l; (void)(l && (l = l->older)))
	    {
		char *result = NULL;

		if (show_lastlog(&l, &skip, &number, &level_mask, 
				match, rex, nomatch, &max, target, 
				mangler, winref, &result))
		{
		    if (counter == 0 && before > 0)
		    {
			counter = 1;
			for (i = 0; i < before; i++)
			{
			    if (l && l == lastshown)
			    {
				if (l->older)
				    l = l->older;
				show_separator = 0;
				break;
			    }

			    if (l && l->newer)
			    {
				l = l->newer;
				counter++;
			    }
			}
		    }
		    else if (after != -1)
			counter = after + 1;
		    else
			counter = 1;

		    if (show_separator)
		    {
			file_put_it(outfp, "%s", separator);
			show_separator = 0;
		    }

		    if (counter)
		    {
			if (rewrite)
			{
				unsigned char *n, vitals[10240];

				snprintf(vitals, sizeof(vitals),
					"%ld %ld %ld %ld . . . %s %s",
						(long)l->refnum,
						(long)l->when,
						(long)l->winref,
						(long)l->level,
						l->target?l->target:".",
						result?result:".");

				n = expand_alias(rewrite, vitals);
				file_put_it(outfp, "%s", n);
				new_free(&n);
			}
			else
				file_put_it(outfp, "%s", result);

			lastshown = l;
			counter--;
			if (counter == 0 && before != -1 && separator)
				show_separator = 1;
		    }
		}
		new_free(&result);
		if (l == end)
			break;
	    }
	}
	if (header)
		file_put_it(outfp, "%s End of Lastlog", banner());
bail:
	if (outfp)
		fclose(outfp);
	if (rex)
		regfree(rex);
	current_window->lastlog_mask = save_mask;
	pop_message_from(lc);
	return;
}

/*
 * This returns 1 if the current item pointed to by 'l' is something that
 * should be displayed based on the criteron provided.
 */
static int	show_lastlog (Lastlog **l, int *skip, int *number, Mask *level_mask, char *match, regex_t *rex, char *nomatch, int *max, const char *target, int mangler, unsigned winref, char **result)
{
	const char *str = NULL;
	*result = NULL;

	if ((*l)->winref != winref)
	{
		if (x_debug & DEBUG_LASTLOG)
			yell("Lastlog item belongs to another window");
		return 0;
	}


	if ((*l)->visible == 0)
	{
		if (x_debug & DEBUG_LASTLOG)
			yell("Lastlog item is not visible");
		return 0;
	}

	if (*skip > 0)
	{
		if (x_debug & DEBUG_LASTLOG)
			yell("Skip > 0 -- [%d]", *skip);
		(*skip)--;	/* Have not skipped enough leading records */
		return 0;
	}

	if (!mask_isnone(level_mask) && !mask_isset(level_mask, (*l)->level))
	{
		if (x_debug & DEBUG_LASTLOG)
			yell("Level_mask != level ([%s] [%s])",
				mask_to_str(level_mask), level_to_str((*l)->level));
		return 0;			/* Not of proper level */
	}

	if (mangler)
	{
		char *	output, *rresult;

		rresult = new_normalize_string((*l)->msg, 1, mangler);
		output = LOCAL_COPY(rresult);
		new_free(&rresult);
		str = output;
	}
	else
		str = (*l)->msg;

	if (match && !wild_match(match, str))
	{
		if (x_debug & DEBUG_LASTLOG)
			yell("Line [%s] not matched [%s]", str, match);
		return 0;			/* Pattern match failed */
	}
	if (nomatch && wild_match(nomatch, str))
	{
		if (x_debug & DEBUG_LASTLOG)
			yell("Line [%s] is matched [%s]", str, nomatch);
		return 0;			/* Pattern match failed */
	}

	if (rex && regexec(rex, str, 0, NULL, 0))
	{
		if (x_debug & DEBUG_LASTLOG)
			yell("Line [%s] not regexed", str);
		return 0;			/* Regex match failed */
	}
	if (target && (!(*l)->target || !wild_match(target, (*l)->target)))
	{
		if (x_debug & DEBUG_LASTLOG)
			yell("Target [%s] not matched [%s]", 
					(*l)->target, target);
		return 0;			/* Target match failed */
	}
	if (*max == 0)
	{
		if (x_debug & DEBUG_LASTLOG)
			yell("max == 0");
		*l = NULL;	/* Have shown maximum number of matches */
		return 0;
	}
	if (*max > 0)
		(*max)--;	/* Have not yet shown max number of matches */

	(*result) = malloc_strdup(str);
	return 1;		/* Show it! */
}

/*
 * reconstitute_scrollback: walk through the lastlog, and put_it everything,
 * making sure to reset the level and all that jazz.  This will cause the 
 * scrollback to be rebroken, etc.
 */
void	reconstitute_scrollback (Window *window)
{
	Lastlog *li;

	for (li = lastlog_oldest; li; li = li->newer)
	{
	    if (li->winref == window->refnum)
		add_to_window_scrollback(window, li->msg, li->refnum);
	}
}
	
/*
 * $line(<line number> [window number])
 * Returns the text of logical line <line number> from the lastlog of 
 * window <window number>.  If no window number is supplied, the current
 * window will be used.  If the window number is invalid, the function
 * will return the false value.
 *
 * Lines are numbered from 1, starting at the most recent line in the buffer.
 * Contributed by Crackbaby (Matt Carothers) on March 19, 1998.
 */
BUILT_IN_FUNCTION(function_line, word)
{
	int	line = 0;
	const char *	windesc = zero;
	Lastlog	*start_pos;
	Window	*win;
	char	*extra;
	int	do_level = 0;
	int	do_timestamp = 0;
	char *	retval = NULL;
	size_t	clue = 0;

	GET_INT_ARG(line, word);

	while (word && *word)
	{
		GET_FUNC_ARG(extra, word);

		if (!my_stricmp(extra, "-LEVEL"))
			do_level = 1;
		else if (!my_stricmp(extra, "-TIME"))
			do_timestamp = 1;
		else
			windesc = extra;
	}

	/* Get the current window, default to current window */
	if (!(win = get_window_by_desc(windesc)))
		RETURN_EMPTY;

	/* Make sure that the line request is within reason */
	if (line < 1 || line > win->lastlog_size)
		RETURN_EMPTY;

	/* Get the line from the lastlog */
	for (start_pos = lastlog_newest; line; start_pos = start_pos->older)
	{
		if (start_pos->winref != win->refnum)
			continue;
		if (start_pos->visible)
			line--;
	}

	if (!start_pos)
		start_pos = lastlog_oldest;
	else
		start_pos = start_pos->newer;

	while ((start_pos->visible == 0 || start_pos->winref != win->refnum) && 
				start_pos->newer)
		start_pos = start_pos->newer;

	/* If there are no visible lastlog items, punt */
	if (!start_pos)
		RETURN_EMPTY;

	malloc_strcat_c(&retval, start_pos->msg, &clue);

	if (do_level)
		malloc_strcat_wordlist_c(&retval, space, 
					level_to_str(start_pos->level), &clue);
	if (do_timestamp)
		malloc_strcat_wordlist_c(&retval, space, 
					NUMSTR(start_pos->when), &clue);

	RETURN_MSTR(retval);
}

/*
 * $lastlog(<window description> <lastlog levels>)
 * Returns all of the lastlog lines (suitable for use with $line()) on the
 * indicated window (0 for the current window) that have any of the lastlog 
 * levels as represented by the lastlog levels. If the window number is 
 * invalid, the function will return the false value.
 */
BUILT_IN_FUNCTION(function_lastlog, word)
{
	const char *	windesc = zero;
	char *	pattern = NULL;
	char *	retval = NULL;
	Lastlog	*iter;
	Window *win;
	Mask	lastlog_levels;
	int	line = 1;
	size_t	rvclue = 0;
	char *	rejects = NULL;

	GET_FUNC_ARG(windesc, word);
	GET_DWORD_ARG(pattern, word);
	if (str_to_mask(&lastlog_levels, word, &rejects))
		standard_level_warning("$lastlog()", &rejects);

	/* Get the current window, default to current window */
	if (!(win = get_window_by_desc(windesc)))
		RETURN_EMPTY;

	for (iter = lastlog_newest; iter; iter = iter->older)
	{
		if (iter->winref != win->refnum)
			continue;
		if (iter->visible == 0)
			continue;

		if (mask_isset(&lastlog_levels, iter->level))
		    if (wild_match(pattern, iter->msg))
			malloc_strcat_word_c(&retval, space, 
					ltoa(line), DWORD_NO, &rvclue);
		line++;
	}

	if (retval)
		return retval;

	RETURN_EMPTY;
}


#if 0
/*
 * Here's the plan:
 * 	$lastlogctl(REFNUMS <windesc> <levels>)
 *	  Like $lastlog(), returns all lastlog refnums for <windesc> that
 *	  belong to one of the level(s).  The refnums returned do not work
 *	  with $line() (but work with $lastlogctl())
 *
 *	$lastlogctl(GET <refnum> LEVEL)
 *	  The lastlog item's level, eg, MSGS, PUBLICS, OTHER.
 *	$lastlogctl(GET <refnum> TARGET)
 *	  The item's target: 
 *		For messages sent to you, the sender.
 *	  	For messages you sent, the receiver.  
 *		For messages sent to a channel, the channel.
 *		For everything else, the empty string.
 *	$lastlogctl(GET <refnum> TEXT)
 *	  The logical line of output (not broken into lines)
 *	$lastlogctl(GET <refnum> TIMESTAMP)
 *	  The time when the logical line of output was first displayed
 *	$lastlogctl(GET <refnum> VISIBLE)
 *	  Whether or not this logical line is visible
 *	$lastlogctl(GET <refnum> WINDOW)
 *	  WHich window this item belongs to
 *	$lastlogctl(GET <refnum> FILTER_LEVEL)
 *	  If not visible, which filter level it is hidden by
 *	  View filter -1 means it's hidden from /lastlog but shows on screen
 *	  View filter 0 means it's been "deleted" from the screen and lastlog
 *		(Normally you'd never unapply filter level 0)
 *	  View filter 1 or above means it's temporarily hidden on the /lastlog 
 *		and the screen.
 *
 *	$lastlogctl(SET <refnum> LEVEL <level>)
 *	  You can change the level (this should cascade to display_level)
 *	$lastlogctl(SET <refnum> TARGET <target>)
 *	  You can change the target (this should cascade to display_level)
 *	$lastlogctl(SET <refnum> TEXT <text>)
 *	  You can change the text itself (maybe to add a timestamp)
 *	$lastlogctl(SET <refnum> TIMESTAMP <timestamp>)
 *	  You can change the timestamp (but i don't recommend it)
 *	$lastlogctl(SET <refnum> WINDOW)
 *	  You can change what window this output belongs to.
 *	$lastlogctl(SET <refnum> FILTER_LEVEL <number>)
 *	  Hide the item from the window and /lastlog
 *	  If <number> is -1, it's not hidden from the window
 *	  If <number> is 0, it's p
 *
 *	$lastlogctl(ADDFILTER <window>)
 *	  Returns a new filter level at which you can apply filters
 *	  All filters applied at one level will "undo" together.
 *	  Normally this means you normally only apply one filter per level
 *	$lastlogctl(APPLYFILTER <window> <level> HIDELEVEL ...)
 *	$lastlogctl(APPLYFILTER <window> <level> ONLYLEVEL ...)
 *	$lastlogctl(APPLYFILTER <window> <level> HIDETARGET ...)
 *	$lastlogctl(APPLYFILTER <window> <level> ONLYTARGET ...)
 *	$lastlogctl(APPLYFILTER <window> <level> HIDETEXT ...)
 *	$lastlogctl(APPLYFILTER <window> <level> ONLYTEXT ...)
 *	$lastlogctl(APPLYFILTER <window> <level> HIDETIME starttime endtime)
 *	$lastlogctl(APPLYFILTER <window> <level> ONLYTIME starttime endtime)
 *
 *	$lastlogctl(FILTERLEVEL)
 *		Returns the current viewfilter display level.
 *		Every time you add a view filter, this goes up
 *		Every time you remove a view filter this goes down.
 *		The default value is 1 , because "filter 0" is the level filter
 */
BUILT_IN_FUNCTION(function_lastlogctl, input)
{

	RETURN_STR(empty_string);
}
#endif

/************************************************************************/

static int	oldest_lastlog_for_window (Lastlog **item, unsigned winref)
{
	*item = NULL;
	return newer_lastlog_entry(item, winref);
}

static int	newer_lastlog_entry (Lastlog **item, unsigned winref)
{
	Lastlog *i;

	if (*item)
		i = (*item)->newer;
	else
		i = lastlog_oldest;

	while (i && i->winref != winref)
		i = i->newer;
	*item = i;

	return i ? 1 : 0;
}

static int	older_lastlog_entry (Lastlog **item, unsigned winref)
{
	Lastlog *i;

	if (*item)
		i = (*item)->older;
	else
		i = lastlog_newest;

	while (i && i->winref != winref)
		i = i->older;
	*item = i;

	return i ? 1 : 0;
}

static int	newest_lastlog_for_window (Lastlog **item, unsigned winref)
{
	*item = NULL;
	return older_lastlog_entry(item, winref);
}

/*
 * Because this function does not have access to the window that this
 * lastlog item belongs to *the caller* is responsible for adjusting
 * window->lastlog_size!  I could change that in the future I guess
 */
static void	remove_lastlog_item (Lastlog *item)
{
	if (item->dead)
		panic(1, "Lastlog item is already dead.");

	if (item == lastlog_oldest)
	{
		if (item->older != NULL)
			panic(1, "Oldest lastlog item %jd has older item %jd",
				item->refnum, item->older->refnum);

		if (item->newer)
			item->newer->older = NULL;
		lastlog_oldest = item->newer;
		item->newer = NULL;
	}

	if (item == lastlog_newest)
	{
		if (item->newer != NULL)
			panic(1, "Newest lastlog item %jd has newer item %jd",
				item->refnum, item->newer->refnum);

		if (item->older)
			item->older->newer = NULL;
		lastlog_newest = item->older;
		item->older = NULL;
	}

	if (item->older)
		item->older->newer = item->newer;
	if (item->newer)
		item->newer->older = item->older;
	item->newer = item->older = NULL;

	item->dead = 1;
	new_free((char **)&item->msg);
	new_free((char **)&item->target);
	new_free((char **)&item);
}

static void	switch_lastlog_window (Lastlog *item, unsigned newref)
{
	/* Mark the old window's scrollback for reconstitution */
	window_scrollback_needs_rebuild(item->winref);
	item->winref = newref;
	/* Mark the new window's scrollback for reconstitution */
	window_scrollback_needs_rebuild(item->winref);
}

/***************************************************************************/
static void	move_lastlog_item (Lastlog *item, unsigned newref)
{
	unsigned	oldref = item->winref;

	item->winref = newref;
	window_scrollback_needs_rebuild(oldref);
	window_scrollback_needs_rebuild(newref);
}

void	move_all_lastlog (unsigned oldref, unsigned newref)
{
	Lastlog *l;

	for (l = lastlog_oldest; l; l = l->newer)
	{
		if (l->winref != oldref)
			continue;
		move_lastlog_item(l, newref);
	}
}

void	move_lastlog_item_by_string (unsigned oldref, unsigned newref, const char *str)
{
	Lastlog *l;

	for (l = lastlog_oldest; l; l = l->newer)
	{
		if (l->winref != oldref)
			continue;
		if (!stristr(l->msg, str))
			continue;
		move_lastlog_item(l, newref);
	}
}

void	move_lastlog_item_by_target (unsigned oldref, unsigned newref, const char *str)
{
	Lastlog *l;

	for (l = lastlog_oldest; l; l = l->newer)
	{
		if (l->winref != oldref)
			continue;
		if (!my_stricmp(l->target, str))
			continue;
		move_lastlog_item(l, newref);
	}
}

void	move_lastlog_item_by_level (unsigned oldref, unsigned newref, Mask *levels)
{
	Lastlog *l;

	for (l = lastlog_oldest; l; l = l->newer)
	{
		if (l->winref != oldref)
			continue;
		if (!mask_isset(levels, l->level))
			continue;
		move_lastlog_item(l, newref);
	}
}

void	move_lastlog_item_by_regex (unsigned oldref, unsigned newref, const char *str)
{
	Lastlog *l;
	regex_t preg;
	int	errcode;

	errcode = regcomp(&preg, str, REG_EXTENDED | REG_ICASE | REG_NOSUB);
	if (errcode != 0)
	{
		char	errstr[256];
		regerror(errcode, &preg, errstr, sizeof(errstr));
		say("Regular expression [%s] does not compile: %s", 
			str, errstr);
		return;
	}

	for (l = lastlog_oldest; l; l = l->newer)
	{
		if (l->winref != oldref)
			continue;
		if (regexec(&preg, l->msg, 0, NULL, 0))
			continue;
		move_lastlog_item(l, newref);
	}

	regfree(&preg);
}

void	lastlog_swap_winrefs (unsigned oldref, unsigned newref)
{
	Lastlog *l;

	for (l = lastlog_oldest; l; l = l->newer)
	{
		if (l->winref == oldref)
			l->winref = newref;
		else if (l->winref == newref)
			l->winref = oldref;
	}
}

