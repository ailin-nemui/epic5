/*
 * lastlog.c: handles the lastlog features of irc. 
 *
 * Written By Michael Sandrof
 *
 * Copyright(c) 1990 
 *
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 */

#if 0
static	char	rcsid[] = "@(#)$Id: lastlog.c,v 1.10 2001/11/15 22:27:54 jnelson Exp $";
#endif

#include "irc.h"
#include "lastlog.h"
#include "window.h"
#include "screen.h"
#include "vars.h"
#include "ircaux.h"
#include "output.h"
#include <regex.h>

static int	show_lastlog (Lastlog **l, int *skip, int *number, int, char *match, regex_t *reg, int *max);

/*
 * lastlog_level: current bitmap setting of which things should be stored in
 * the lastlog.  The LOG_MSG, LOG_NOTICE, etc., defines tell more about this 
 */
static	int	lastlog_level;
static	int	notify_level;
	int	new_server_lastlog_level;
	int	beep_on_level;
	int 	current_window_level = 0;

/*
 * msg_level: the mask for the current message level.  What?  Did he really
 * say that?  This is set in the set_lastlog_msg_level() routine as it
 * compared to the lastlog_level variable to see if what ever is being added
 * should actually be added 
 */
static	int	msg_level = LOG_CRAP;

#define NUMBER_OF_LEVELS 16
static	char	*levels[] =
{
	"CRAP",		"PUBLIC",	"MSGS",		"NOTICES",
	"WALLS",	"WALLOPS",	"NOTES",	"OPNOTES",
	"SNOTES",	"ACTIONS",	"DCC",		"CTCP",
	"USERLOG1",	"USERLOG2",	"USERLOG3",	"USERLOG4"
};

/*
 * warn_lastlog_level: tells the user what levels are available.
 */
static void	warn_lastlog_levels (void)
{
	char buffer[BIG_BUFFER_SIZE + 1];
	int i;

	strmcpy(buffer, "Valid levels: ", BIG_BUFFER_SIZE);
	for (i = 0; i < NUMBER_OF_LEVELS; i++)
	{
		strmcat(buffer, levels[i], BIG_BUFFER_SIZE);
		strmcat(buffer, " ", BIG_BUFFER_SIZE);
	}
	say("%s", buffer);
}

/*
 * bits_to_lastlog_level: converts the bitmap of lastlog levels into a nice
 * string format.  Note that this uses the global buffer, so watch out 
 */
char	*bits_to_lastlog_level (int level)
{
	static	char	buffer[256]; /* this *should* be enough for this */
	int	i,
		p;

	if (level == LOG_ALL)
		strcpy(buffer, "ALL");
	else if (level == 0)
		strcpy(buffer, "NONE");
	else
	{
		*buffer = '\0';
		for (i = 0, p = 1; i < NUMBER_OF_LEVELS; i++, p *= 2)
		{
			if (level & p)
			{
				if (*buffer)
					strmcat(buffer, " ", 255);
				strmcat(buffer, levels[i], 255);
			}
		}
	}
	return (buffer);
}

int	parse_lastlog_level (char *str)
{
	char	*ptr,
		*rest;
	int	len,
		i,
		p,
		level,
		neg;
	int	warn = 0;

	level = 0;
	while ((str = next_arg(str, &rest)) != NULL)
	{
	    while (str)
	    {
		if ((ptr = strchr(str, ',')) != NULL)
			*ptr++ = '\0';
		if ((len = strlen(str)) != 0)
		{
			if (my_strnicmp(str, "ALL", len) == 0)
				level = LOG_ALL;
			else if (my_strnicmp(str, "NONE", len) == 0)
				level = 0;
			else
			{
				if (*str == '-')
				{
					str++, len--;
					neg = 1;
				}
				else
					neg = 0;

				for (i = 0, p = 1; i < NUMBER_OF_LEVELS;
						i++, p *= 2)
				{
					if (!my_strnicmp(str, levels[i], len))
					{
						if (neg)
							level &= (LOG_ALL ^ p);
						else
							level |= p;
						break;
					}
				}
				if (i == NUMBER_OF_LEVELS)
				{
					say("Unknown level: %s", str);
					if (!warn)
					{
						warn_lastlog_levels();
						warn = 1;
					}
				}
			}
		}
		str = ptr;
	    }
	    str = rest;
	}
	return (level);
}

/*
 * set_lastlog_level: called whenever a "SET LASTLOG_LEVEL" is done.  It
 * parses the settings and sets the lastlog_level variable appropriately.  It
 * also rewrites the LASTLOG_LEVEL variable to make it look nice 
 */
void	set_lastlog_level (char *str)
{
	lastlog_level = parse_lastlog_level(str);
	set_string_var(LASTLOG_LEVEL_VAR, bits_to_lastlog_level(lastlog_level));
	current_window->lastlog_level = lastlog_level;
}

void	set_new_server_lastlog_level (char *str)
{
	new_server_lastlog_level = parse_lastlog_level(str);
	set_string_var(NEW_SERVER_LASTLOG_LEVEL_VAR, 
			bits_to_lastlog_level(new_server_lastlog_level));
}


void 	remove_from_lastlog (Window *window)
{
	Lastlog *new_oldest;
	Lastlog *being_removed;

	if (window->lastlog_oldest)
	{
		being_removed = window->lastlog_oldest;
		new_oldest = being_removed->newer;
		window->lastlog_oldest = new_oldest;
		if (new_oldest)
			new_oldest->older = NULL;
		else
			window->lastlog_newest = NULL;
		window->lastlog_size--;
		new_free((char **)&(being_removed->msg));
		new_free((char **)&being_removed);
	}
	else
		window->lastlog_size = 0;
}

/*
 * set_lastlog_size: sets up a lastlog buffer of size given.  If the lastlog
 * has gotten larger than it was before, all newer lastlog entries remain.
 * If it get smaller, some are deleted from the end. 
 */
void	set_lastlog_size (int size)
{
	int	i,
		diff;
	Window	*window = NULL;

	while (traverse_all_windows(&window))
	{
		if (window->lastlog_size > size)
		{
			diff = window->lastlog_size - size;
			for (i = 0; i < diff; i++)
				remove_from_lastlog(window);
		}
		window->lastlog_max = size;
	}
}

/* set_lastlog_msg_level: sets the message level for recording in the lastlog */
int 	set_lastlog_msg_level (int level)
{
	int	old;

	old = msg_level;
	msg_level = level;
	return (old);
}


/*
 * The /LASTLOG command:
 * Syntax:
 *	/LASTLOG [options] [<string>] [<number1> [<number2>]]
 * Options:
 *	-			Do not show header and footer
 *	-reverse		Show matches newest-to-oldest (reverse order)
 *			        instead of oldest-to-newest (normal order)
 * 	-literal <pattern>	<string> is search target, not option.
 *	-regex <regex>		line must match <regex>.
 *	-max <number>		Only show first <number> matches
 *	-skip <number>		Skip this many leading lastlog entries
 *	-number <number>	Only search this many lastlog entries
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
	int		level_mask = 0;
	int		skip = -1;
	int		number = INT_MAX;
	int		max = -1;
	char *		match = NULL;
	char *		regex = NULL;
	Lastlog *	start;
	Lastlog *	end;
	Lastlog *	l;
	regex_t 	realreg;
	regex_t *	reg = NULL;
	int		cnt;
	char *		arg;
	int		header = 1;
	int		save_level;
	int		before = -1;
	int		after = 0;
	int		counter = 0;
	int		show_separator = 0;
	char *		separator = "----";

	message_from(NULL, LOG_CURRENT);
	cnt = current_window->lastlog_size;
	save_level = current_window->lastlog_level;
	current_window->lastlog_level = 0;

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
		if ((after_str = strchr(x, ',')))
			*after_str++ = 0;

		if (!is_number(before_str))
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
		level_mask = LOG_ALL;
	    else if (!my_strnicmp(arg, "--ALL", len))
		level_mask = 0;
	    else if (!my_strnicmp(arg, "--", 2))
	    {
		int	i;
		for (i = 0; i < NUMBER_OF_LEVELS; i++)
		{
		    if (!my_strnicmp(levels[i], arg+2, len-2))
		    {
			level_mask &= ~(1 << i);
			break;
		    }
		}
		if (i == NUMBER_OF_LEVELS)
		{
			say("Unknown flag: %s", arg);
			goto bail;
		}
	    }
	    else if (!my_strnicmp(arg, "-", 1))
	    {
		int	i;
		for (i = 0; i < NUMBER_OF_LEVELS; i++)
		{
		    if (!my_strnicmp(levels[i], arg+1, len-1))
		    {
			level_mask |= (1 << i);
			break;
		    }
		}
		if (i == NUMBER_OF_LEVELS)
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
		char *	blah = alloca(strlen(match) + 3);
		sprintf(blah, "*%s*", match);
		match = blah;
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
		reg = &realreg;
	}

	if (x_debug & DEBUG_LASTLOG)
	{
		yell("Lastlog summary status:");
		yell("Pattern: [%s]", match);
		yell("Regex: [%s]", regex);
		yell("Header: %d", header);
		yell("Reverse: %d", reverse);
		yell("Skip: %d", skip);
		yell("Number: %d", number);
		yell("Max: %d", max);
		yell("Mask: %d", level_mask);
	}

	/* Iterate over the lastlog here */
	if (header)
		say("Lastlog:");

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
	    int i;

	    end = current_window->lastlog_newest;
	    start = end;
	    for (i = 1; i < number; i++)
	    {
		if (start == current_window->lastlog_oldest)
			break;
		start = start->older;
	    }

	    for (l = start; l; (void)(l && (l = l->newer)))
	    {
		if (show_lastlog(&l, &skip, &number, level_mask, 
				match, reg, &max))
		{
		    if (show_separator)
		    {
			put_it("%s", separator);
			show_separator = 0;
		    }

		    if (counter == 0 && before > 0)
		    {
			int i;

			for (i = 0; i < before; i++)
			     if (l && l->older)
				l = l->older;
			counter = before + 1;

		    }
		    else if (after != -1)
			counter = after + 1;
		    else
			counter = 1;
		}
		if (counter)
		{
			put_it("%s", l->msg);
			counter--;
			if (counter == 0 && before != -1 && separator)
				show_separator = 1;
		}

		if (l == end)
			break;
	    }
	}
	else
	{
	    int i;

	    start = current_window->lastlog_newest;
	    end = start;
	    for (i = 1; i < number; i++)
	    {
		if (end == current_window->lastlog_oldest)
			break;
		end = end->older;
	    }

	    for (l = start; l; (void)(l && (l = l->older)))
	    {
		if (show_lastlog(&l, &skip, &number, level_mask, 
				match, reg, &max))
		{
		    if (show_separator)
		    {
			put_it("%s", separator);
			show_separator = 0;
		    }

		    if (counter == 0 && before > 0)
		    {
			int i;
			for (i = 0; i < before; i++)
			     if (l && l->newer)
				l = l->newer;
			counter = before + 1;
		    }
		    else if (after != -1)
			counter = after + 1;
		    else
			counter = 1;
		}
		if (counter)
		{
			put_it("%s", l->msg);
			counter--;
			if (counter == 0 && before != -1 && separator)
				show_separator = 1;
		}
		if (l == end)
			break;
	    }
	}
	if (header)
		say("End of Lastlog");
bail:
	if (reg)
		regfree(reg);
	current_window->lastlog_level = save_level;
	set_lastlog_msg_level(msg_level);
	message_from(NULL, LOG_CRAP);
	return;
}

/*
 * This returns 1 if the current item pointed to by 'l' is something that
 * should be displayed based on the criteron provided.
 */
static int	show_lastlog (Lastlog **l, int *skip, int *number, int level_mask, char *match, regex_t *reg, int *max)
{
	if (*skip > 0)
	{
		if (x_debug & DEBUG_LASTLOG)
			yell("Skip > 0 -- [%d]", *skip);
		(*skip)--;	/* Have not skipped enough leading records */
		return 0;
	}
#if 0
	if (*number == 0)
	{
		if (x_debug & DEBUG_LASTLOG)
			yell("Number == 0");
		*l = NULL;	/* Have iterated over the max num. records */
		return 0;
	}
	if (*number > 0)
		(*number)--;	/* Have not yet iterated over max records */
#endif

	if (level_mask && (((*l)->level & level_mask) == 0))
	{
		if (x_debug & DEBUG_LASTLOG)
			yell("Level_mask != level ([%d] [%d])",
				level_mask, (*l)->level);
		return 0;			/* Not of proper level */
	}
	if (match && !wild_match(match, (*l)->msg))
	{
		if (x_debug & DEBUG_LASTLOG)
			yell("Line [%s] not matched [%s]", (*l)->msg, match);
		return 0;			/* Pattern match failed */
	}
	if (reg && regexec(reg, (*l)->msg, 0, NULL, 0))
	{
		if (x_debug & DEBUG_LASTLOG)
			yell("Line [%s] not regexed", (*l)->msg);
		return 0;			/* Regex match failed */
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

	return 1;		/* Show it! */
}

/*
 * add_to_lastlog: adds the line to the lastlog.  If the LASTLOG_CONVERSATION
 * variable is on, then only those lines that are user messages (private
 * messages, channel messages, wall's, and any outgoing messages) are
 * recorded, otherwise, everything is recorded 
 */
void 	add_to_lastlog (Window *window, const char *line)
{
	Lastlog *new_l;

	if (!window)
		window = current_window;

	if (window->lastlog_level & msg_level)
	{
		new_l = (Lastlog *)new_malloc(sizeof(Lastlog));
		new_l->older = window->lastlog_newest;
		new_l->newer = NULL;
		new_l->level = msg_level;
		new_l->msg = m_strdup(line);

		if (window->lastlog_newest)
			window->lastlog_newest->newer = new_l;
		window->lastlog_newest = new_l;

		if (!window->lastlog_oldest)
			window->lastlog_oldest = window->lastlog_newest;

		if (window->lastlog_size++ >= window->lastlog_max)
			remove_from_lastlog(window);
	}
}

int	real_notify_level (void)
{
	return (notify_level);
}

int	real_lastlog_level (void)
{
	return (lastlog_level);
}

void	set_notify_level (char *str)
{
	notify_level = parse_lastlog_level(str);
	set_string_var(NOTIFY_LEVEL_VAR, bits_to_lastlog_level(notify_level));
	current_window->notify_level = notify_level;
}

void 	set_beep_on_msg (char *str)
{
	beep_on_level = parse_lastlog_level(str);
	set_string_var(BEEP_ON_MSG_VAR, bits_to_lastlog_level(beep_on_level));
}

void	set_current_window_level (char *str)
{
	current_window_level = parse_lastlog_level(str);
	set_string_var(CURRENT_WINDOW_LEVEL_VAR, 
			bits_to_lastlog_level(current_window_level));
}

#define EMPTY empty_string
#define RETURN_EMPTY return m_strdup(EMPTY)
#define RETURN_IF_EMPTY(x) if (empty( x )) RETURN_EMPTY
#define GET_INT_ARG(x, y) {RETURN_IF_EMPTY(y); x = my_atol(safe_new_next_arg(y, &y));}
#define GET_STR_ARG(x, y) {RETURN_IF_EMPTY((y)); x = new_next_arg((y), &(y));RETURN_IF_EMPTY((x));}
#define RETURN_STR(x) return m_strdup(x ? x : EMPTY);

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
char 	*function_line (char *word)
{
	int	line = 0;
	char *	windesc = zero;
	Lastlog	*start_pos;
	Window	*win;
	char	*extra;
	int	do_level = 0;

	GET_INT_ARG(line, word);

	while (word && *word)
	{
		GET_STR_ARG(extra, word);

		if (!my_stricmp(extra, "-LEVEL"))
			do_level = 1;
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
	for (start_pos = win->lastlog_newest; line; start_pos = start_pos->older)
		line--;

	if (!start_pos)
		start_pos = win->lastlog_oldest;
	else
		start_pos = start_pos->newer;

	if (do_level)
		return m_sprintf("%s %s", start_pos->msg, 
					levels[start_pos->level]);
	else
		RETURN_STR(start_pos->msg);
}

/*
 * $lastlog(<window description> <lastlog levels>)
 * Returns all of the lastlog lines (suitable for use with $line()) on the
 * indicated window (0 for the current window) that have any of the lastlog 
 * levels as represented by the lastlog levels. If the window number is 
 * invalid, the function will return the false value.
 */
char *function_lastlog (char *word)
{
	char *	windesc = zero;
	char *	pattern = NULL;
	char *	retval = NULL;
	Lastlog	*iter;
	Window *win;
	int	levels;
	int	line = 1;
	size_t	rvclue = 0;

	GET_STR_ARG(windesc, word);
	GET_STR_ARG(pattern, word);
	levels = parse_lastlog_level(word);

	/* Get the current window, default to current window */
	if (!(win = get_window_by_desc(windesc)))
		RETURN_EMPTY;

	for (iter = win->lastlog_newest; iter; iter = iter->older, line++)
	{
		if (iter->level & levels)
			if (wild_match(pattern, iter->msg))
				m_sc3cat(&retval, space, ltoa(line), &rvclue);
	}

	if (retval)
		return retval;

	RETURN_EMPTY;
}


