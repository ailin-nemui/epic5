/* $EPIC: history.c,v 1.3 2001/02/19 20:37:03 jnelson Exp $ */
/*
 * history.c: stuff to handle command line history 
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright © 1993-1999 Jeremy Nelson and others ("EPIC Software Labs").
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
#include "vars.h"
#include "history.h"
#include "output.h"
#include "input.h"
#include "screen.h"

typedef struct	HistoryStru
{
	int	number;
	char	*stuff;
	struct	HistoryStru *next;		/* Older */
	struct	HistoryStru *prev;		/* Newer */
}	History;

/* command_history: pointer to head of command_history list */
static	History *command_history_head = (History *)NULL; 	/* Newest */
static	History *command_history_tail = (History *)NULL;	/* Oldest */
static	History *command_history_pos = (History *)NULL;		/* Current */

/* hist_size: the current size of the command_history array */
static	int	hist_size = 0;

/* hist_count: the absolute counter for the history list */
static	int	hist_count = 0;

/*
 * last_dir: the direction (next or previous) of the last get_from_history()
 * call.... reset by add to history 
 */
static	int	last_dir = -1;

/*
 * set_history_size: adjusts the size of the command_history to be size. If
 * the array is not yet allocated, it is set to size and all the entries
 * nulled.  If it exists, it is resized to the new size with a realloc.  Any
 * new entries are nulled. 
 */
void	set_history_size (int size)
{
	int	i,
		cnt;
	History *ptr;

	if (size < hist_size)
	{
		cnt = hist_size - size;
		for (i = 0; i < cnt; i++)
		{
			ptr = command_history_tail;
			command_history_tail = ptr->prev;
			new_free(&(ptr->stuff));
			new_free((char **)&ptr);
		}
		if (command_history_tail == (History *)NULL)
			command_history_head = (History *)NULL;
		else
			command_history_tail->next = (History *)NULL;
		hist_size = size;
	}
}

/*
 * shove_to_history: a key binding that saves the current line into
 * the history and then deletes the whole line.  Useful for when you
 * are in the middle of a big line and need to "get out" to do something
 * else quick for just a second, and you dont want to have to retype
 * everything all over again
 */
void	shove_to_history (char unused, char *not_used)
{
	add_to_history(get_input());
	input_clear_line(unused, not_used);
}

/*
 * add_to_history: adds the given line to the history array.  The history
 * array is a circular buffer, and add_to_history handles all that stuff. It
 * automagically allocted and deallocated memory as needed 
 */
void	add_to_history (char *line)
{
	History *new_h;

	if (get_int_var(HISTORY_VAR) == 0 || !line || !*line)
		return;

	last_dir = OLDER;
	command_history_pos = NULL;

	if (command_history_head && !strcmp(command_history_head->stuff, line))
		return;

	if ((hist_size == get_int_var(HISTORY_VAR)) && 
		command_history_tail)
	{
		if (hist_size == 1)
		{
			malloc_strcpy(&command_history_tail->stuff, line);
			return;
		}

		new_h = command_history_tail;
		command_history_tail = command_history_tail->prev;
		command_history_tail->next = NULL;
		new_free(&new_h->stuff);
		new_free((char **)&new_h);
		if (command_history_tail == NULL)
			command_history_head = NULL;
	}
	else
		hist_size++;

	new_h = (History *) new_malloc(sizeof(History));
	new_h->stuff = m_strdup(line);
	new_h->number = hist_count;
	new_h->next = command_history_head;
	new_h->prev = NULL;

	if (command_history_head)
		command_history_head->prev = new_h;
	command_history_head = new_h;
	if (command_history_tail == NULL)
		command_history_tail = new_h;

	hist_count++;
}

/* history: the /HISTORY command, shows the command history buffer. */
BUILT_IN_COMMAND(history)
{
	int	cnt,
		max;
	char	*value;
	History *tmp;

	say("Command History:");
	if (get_int_var(HISTORY_VAR))
	{
		if ((value = next_arg(args, &args)) != NULL)
		{
			if ((max = my_atol(value)) > get_int_var(HISTORY_VAR))
				max = get_int_var(HISTORY_VAR);
		}
		else
			max = get_int_var(HISTORY_VAR);

		for (tmp = command_history_tail, cnt = 0; tmp && (cnt < max);
				tmp = tmp->prev, cnt++)
			put_it("%d: %s", tmp->number, tmp->stuff);
	}
}

/*
 * do_history: This finds the given history entry in either the history file,
 * or the in memory history buffer (if no history file is given). It then
 * returns the found entry as its function value, or null if the entry is not
 * found for some reason.  Note that this routine mallocs the string returned  
 */
char *	do_history (char *com, char *rest)
{
	int	hist_num;
	char	*ptr,
		*ret = NULL;
static	char	*last_com = NULL;
	History	*tmp = NULL;

	if (!com || !*com)
	{
		if (last_com)
			com = last_com;
		else
			com = empty_string;
	}
	else
		malloc_strcpy(&last_com, com);

	if (!is_number(com))
	{
		char	*match_str = (char *)NULL;
		char	*cmdc = get_string_var(CMDCHARS_VAR);

		if (!end_strcmp(com, "*", 1))
			match_str = m_strdup(com);
		else
			match_str = m_2dup(com, "*");

		if (get_int_var(HISTORY_VAR))
		{
			if ((last_dir == -1) || (tmp == NULL))
				tmp = command_history_head;
			else
				tmp = tmp->next;

			for (; tmp; tmp = tmp->next)
			{
				ptr = tmp->stuff;
				while (ptr && strchr(cmdc, *ptr))
					ptr++;

				if (wild_match(match_str, ptr))
				{
					last_dir = OLDER;
					new_free(&match_str);
					ret = m_2dup("/", ptr);
					return m_s3cat_s(&ret, " ", rest);
				}
			}
		}

		last_dir = -1;
		new_free(&match_str);
		say("No match");
	}
	else
	{
		hist_num = my_atol(com);
		if (hist_num > 0)
		{
			for (tmp = command_history_head; tmp; tmp = tmp->next)
			{
				if (tmp->number == hist_num)
				{
					ret = m_2dup("/", tmp->stuff);
					m_s3cat_s(&ret, " ", rest);
					return (ret);
				}
			}
		}
		else
		{
			hist_num++;
			for (tmp = command_history_head; tmp && hist_num < 0; )
				tmp = tmp->next, hist_num++;

			if (tmp)
			{
				ret = m_2dup("/", tmp->stuff);
				m_s3cat_s(&ret, " ", rest);
				return (ret);
			}
		}

		say("No such history entry: %d", hist_num);
		return NULL;
	}

	return NULL;
}

/*
 * get_history: gets the next history entry, either the OLDER entry or the
 * NEWER entry, and sets it to the current input string 
 */
void	get_history (int which)
{
	int	circleq = get_int_var(HISTORY_CIRCLEQ_VAR);

	if ((get_int_var(HISTORY_VAR) == 0) || (hist_size == 0))
		return;

	/* 
	 * Getting NEWER entries, ie, moving FORWARD, ie N -> N + 1,
	 *					      ie MAX -> 1.
	 * (Cursor down)
	 */
	if (which == NEWER)		/* NEXT */
	{
		/* If we have used history since last input */
		if (command_history_pos)
		{
			/* If there is a newer entry, use it */
			if (command_history_pos->prev)
				command_history_pos = command_history_pos->prev;

			/* If the history is a circleq, go to oldest entry */
			else if (circleq)
				command_history_pos = command_history_tail;

			/* Otherwise, exit history browsing. */
			else
				command_history_pos = NULL;
		}

		/* We are using history for the first time here */
		else
		{
		    if (circleq)
		    {
			/* Add what we have to the input */
			add_to_history(get_input());

			/* Set the position to the oldest entry */
			command_history_pos = command_history_tail;
		    }
		}
	}

	/*
	 * Getting OLDER entries, ie, moving BACKWARD, ie N -> N - 1,
	 *					       ie 1 -> MAX.
	 * (Cursor up)
	 */
	else	/* OLDER */			/* PREV */
	{
		/* If we have used history since last input */
		if (command_history_pos)
		{
			/* If there is an older entry, use it */
			if (command_history_pos->next)
				command_history_pos = command_history_pos->next;

			/* If circleq, get the newest entry */
			else if (circleq)
				command_history_pos = command_history_head;

			/* Otherwise, we don't go anywhere */
		}

		/* We are using history for the first time here */
		else
		{
			char *	i = get_input();

			/* 
			 * Add what we have to the input,
			 * And do add_to_history a little favor.
			 */
			if (!i || !*i)
			    command_history_pos = command_history_head;
			else
			{
			    add_to_history(i);
			    command_history_pos = command_history_head;
			    if (command_history_pos->next)
				command_history_pos = command_history_pos->next;
			}
		}
	}

	if (command_history_pos)
		set_input(command_history_pos->stuff);
	else
		set_input(empty_string);

	update_input(UPDATE_ALL);
}

void	abort_history_browsing (int fullblown)
{
	command_history_pos = NULL;
	if (fullblown)
	{
		set_input(empty_string);
		update_input(UPDATE_ALL);
	}
}

