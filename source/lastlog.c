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
static	char	rcsid[] = "@(#)$Id: lastlog.c,v 1.1 2000/12/05 00:11:57 jnelson Exp $";
#endif

#include "irc.h"
#include "lastlog.h"
#include "window.h"
#include "screen.h"
#include "vars.h"
#include "ircaux.h"
#include "output.h"

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
	Lastlog *tmp;
	Lastlog *end_holder;

	if (window->lastlog_tail)
	{
		end_holder = window->lastlog_tail;
		tmp = window->lastlog_tail->prev;
		window->lastlog_tail = tmp;
		if (tmp)
			tmp->next = (Lastlog *) 0;
		else
			window->lastlog_head = window->lastlog_tail;
		window->lastlog_size--;
		new_free((char **)&(end_holder->msg));
		new_free((char **)&end_holder);
	}
	else
		window->lastlog_size = 0;
}

/*
 * set_lastlog_size: sets up a lastlog buffer of size given.  If the lastlog
 * has gotten larger than it was before, all previous lastlog entry remain.
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
 * lastlog: the /LASTLOG command.  Displays the lastlog to the screen. If
 * args contains a valid integer, only that many lastlog entries are shown
 * (if the value is less than lastlog_size), otherwise the entire lastlog is
 * displayed 
 */
BUILT_IN_COMMAND(lastlog)
{
	int	cnt,
		from = 0,
		p,
		i,
		level = 0,
		msg_level,
		len,
		mask = 0,
		header = 1,
		lines = 0,
		reverse = 0;
	int	remove = 0;
	Lastlog *start_pos;
	char	*match = NULL,
		*arg;
	char	*blah = NULL;

	message_from((char *) 0, LOG_CURRENT);
	cnt = current_window->lastlog_size;

	while ((arg = new_next_arg(args, &args)) != NULL)
	{
		if (*arg == '-')
		{
			arg++;
			if (!(len = strlen(arg)))
				header = 0;
			else if (!my_strnicmp(arg, "LITERAL", len))
			{
				if (match)
				{
					say("Second -LITERAL argument ignored");
					new_next_arg(args, &args);
					continue;
				}
				if ((match = new_next_arg(args, &args)) != NULL)
					continue;
				say("Need pattern for -LITERAL");
				return;
			}
			else if (!my_strnicmp(arg, "MAX", len))
			{
				char *ptr;
				if ((ptr = new_next_arg(args, &args)))
					lines = my_atol(ptr);
				if (lines < 0)
					lines = 0;
			}
			else if (!my_strnicmp(arg, "REVERSE", len))
				reverse = 1;
			else
			{
				/*
				 * A single hyphen is "show this level only"
				 * A double hyphen is "dont show this level"
				 */
				if (*arg == '-')
					remove = 1, arg++;
				else
					remove = 0;

				/*
				 * Which can be combined with -ALL, which 
				 * turns on all levels.  Use --MSGS or
				 * whatever to turn off ones you dont want.
				 */
				if (!my_strnicmp(arg, "ALL", len))
				{
					if (remove)
						mask = 0;
					else
						mask = LOG_ALL;
					continue;	/* Go to next arg */
				}

				/*
				 * Find the lastlog level in our list.
				 */
				for (i = 0, p = 1; 
				     i < NUMBER_OF_LEVELS; 
				     i++, p *= 2)
				{
					if (!my_strnicmp(levels[i], arg, len))
					{
						if (remove)
							mask &= ~p;
						else
							mask |= p;
						break;
					}
				}

				if (i == NUMBER_OF_LEVELS)
				{
					say("Unknown flag: %s", arg);
					message_from((char *) 0, LOG_CRAP);
					return;
				}
			}
		}
		else
		{
			if (level == 0)
			{
				if (match || isdigit(*arg))
				{
					cnt = atoi(arg);
					level++;
				}
				else
					match = arg;
			}
			else if (level == 1)
			{
				from = atoi(arg);
				level++;
			}
		}
	}

	start_pos = current_window->lastlog_head;
	level = current_window->lastlog_level;
	msg_level = set_lastlog_msg_level(0);

	if (!reverse)
	{
		for (i = 0; (i < from) && start_pos; start_pos = start_pos->next)
			if (!mask || (mask & start_pos->level))
				i++;

		for (i = 0; (i < cnt) && start_pos; start_pos = start_pos->next)
			if (!mask || (mask & start_pos->level))
				i++;
		start_pos = start_pos ? start_pos->prev : current_window->lastlog_tail;
	}
	else
		start_pos = current_window->lastlog_head;

	/* Let's not get confused here, display a seperator.. -lynx */
	if (header)
		say("Lastlog:");

	if (match)
	{
		blah = (char *)alloca(strlen(match) + 3);
		sprintf(blah, "*%s*", match);
	}

	for (i = 0; 
	     (i < cnt) && start_pos; 
	     start_pos = (reverse ? start_pos->next : start_pos->prev))
	{
		if (!mask || (mask & start_pos->level))
		{
			i++;
			if (!match || wild_match(blah, start_pos->msg))
			{
				put_it("%s", start_pos->msg);
				if (!--lines)
					break;
			}
		}
	}
	if (header)
		say("End of Lastlog");
	current_window->lastlog_level = level;
	set_lastlog_msg_level(msg_level);
	message_from((char *) 0, LOG_CRAP);
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
/* This is probably bogus. */
#if 0
		/* no nulls or empty lines (they contain "> ") */
		if (line && (strlen(line) > 2))
#endif
		{
			new_l = (Lastlog *)new_malloc(sizeof(Lastlog));
			new_l->next = window->lastlog_head;
			new_l->prev = NULL;
			new_l->level = msg_level;
			new_l->msg = m_strdup(line);

			if (window->lastlog_head)
				window->lastlog_head->prev = new_l;
			window->lastlog_head = new_l;

			if (!window->lastlog_tail)
				window->lastlog_tail = window->lastlog_head;

			if (window->lastlog_size++ >= window->lastlog_max)
				remove_from_lastlog(window);
		}
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
	for (start_pos = win->lastlog_head; line; start_pos = start_pos->next)
		line--;

	if (!start_pos)
		start_pos = win->lastlog_tail;
	else
		start_pos = start_pos->prev;

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

	GET_STR_ARG(windesc, word);
	GET_STR_ARG(pattern, word);
	levels = parse_lastlog_level(word);

	/* Get the current window, default to current window */
	if (!(win = get_window_by_desc(windesc)))
		RETURN_EMPTY;

	for (iter = win->lastlog_head; iter; iter = iter->next, line++)
	{
		if (iter->level & levels)
			if (wild_match(pattern, iter->msg))
				m_s3cat(&retval, space, ltoa(line));
	}

	if (retval)
		return retval;

	RETURN_EMPTY;
}


