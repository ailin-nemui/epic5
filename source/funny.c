/* $EPIC: funny.c,v 1.8 2002/12/11 19:20:23 crazyed Exp $ */
/*
 * funny.c: Handles the /LIST and /NAMES replies.  Also deals with the
 * /NAMES and /MODE replies that we have automatically sent to us when
 * we join a channel.  Also handles user modes.
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright © 1995, 2002 EPIC Software Labs
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
#include "funny.h"
#include "hook.h"
#include "ignore.h"
#include "ircaux.h"
#include "lastlog.h"
#include "names.h"
#include "numbers.h"
#include "output.h"
#include "parse.h"
#include "server.h"
#include "term.h"
#include "vars.h"

struct	WideListInfoStru
{
	char	*channel;
	int	users;
};

typedef	struct WideListInfoStru WideList;

static	WideList **wide_list = (WideList **) 0;
static	int	wl_size = 0;
static	int	wl_elements = 0;

static	char	*match = (char *) 0;
extern	char	*send_umode;

static	int	funny_min;
static	int	funny_max;
static	int	funny_flags;

void	funny_match (char *stuff)
{
	malloc_strcpy(&match, stuff);
}

void	set_funny_flags (int min, int max, int flags)
{
	funny_min = min;
	funny_max = max;
	funny_flags = flags;
}

static int	funny_widelist_users (WideList **left, WideList **right)
{
	if ((**left).users > (**right).users)
		return -1;
	else if ((**right).users > (**left).users)
		return 1;
	else
		return my_stricmp((**left).channel, (**right).channel);
}

static int	funny_widelist_names (WideList **left, WideList **right)
{
	int	comp;

	if ((comp = my_stricmp((**left).channel, (**right).channel)) != 0)
		return comp;
	else if ((**left).users > (**right).users)
		return -1;
	else if ((**right).users > (**left).users)
		return 1;
	else
		return 0;
}

typedef int qsort_f (const void *, const void *);

void	funny_print_widelist (void)
{
	int	i;
	char	buffer1[BIG_BUFFER_SIZE + 1];
	char	buffer2[BIG_BUFFER_SIZE + 1];

	if (!wide_list)
		return;

	if (funny_flags & FUNNY_NAME)
		qsort(wide_list, wl_elements, sizeof(WideList *), 
			(qsort_f *)funny_widelist_names);
	else if (funny_flags & FUNNY_USERS)
		qsort(wide_list, wl_elements, sizeof(WideList *),
			(qsort_f *)funny_widelist_users);

	*buffer1 = 0;
	for (i = 1; i < wl_elements; i++)
	{
		snprintf(buffer2, BIG_BUFFER_SIZE, "%s(%d) ", 
			wide_list[i]->channel, wide_list[i]->users);

		/* This is safe as long as BIG_BUFFER_SIZE > TI_cols */
		/* Obviously, BIG_BUFFER_SIZE > TI_cols, right? */
		if (strlen(buffer1) + strlen(buffer2) > 
				current_term->TI_cols - 5)
		{
			if (do_hook(WIDELIST_LIST, "%s", buffer1))
				say("%s", buffer1);
			strcpy(buffer1, buffer2);
		}
		else
			strcat(buffer1, buffer2);

		new_free(&wide_list[i]->channel);
		new_free((char **)&wide_list[i]);
	}
	if (*buffer1 && do_hook(WIDELIST_LIST, "%s", buffer1))
		say("%s" , buffer1);

	new_free(&wide_list[0]->channel);
	new_free((char **)&wide_list[0]);
	new_free((char **)&wide_list);
	wl_elements = wl_size = 0;
}

void	funny_list (char *from, char **ArgList)
{
	char	*channel,
		*user_cnt,
		*line;
	WideList **new_list;
	int	cnt;
static	char	format[25];
static	int	last_width = -1;

	if (!ArgList[0] || !ArgList[1] || !ArgList[2])
		return;		/* Larne-proof */

	if (last_width != get_int_var(CHANNEL_NAME_WIDTH_VAR))
	{
		if ((last_width = get_int_var(CHANNEL_NAME_WIDTH_VAR)) != 0)
			snprintf(format, 25, "%%-%u.%us %%-5s  %%s",
				(unsigned char) last_width,
				(unsigned char) last_width);
		else
			strcpy(format, "%s\t%-5s  %s");
	}

	channel = ArgList[0];
	user_cnt = ArgList[1];
	line = PasteArgs(ArgList, 2);

	if (funny_flags & FUNNY_TOPIC && !(line && *line))
		return;

	cnt = my_atol(user_cnt);
	if (funny_min && (cnt < funny_min))
		return;
	if (funny_max && (cnt > funny_max))
		return;
	if ((funny_flags & FUNNY_PRIVATE) && (*channel != '*'))
		return;
	if ((funny_flags & FUNNY_PUBLIC) && (*channel == '*'))
		return;

	if (match)
	{
		if (wild_match(match, channel) == 0)
			return;
	}
	if (funny_flags & FUNNY_WIDE)
	{
		if (wl_elements >= wl_size)
		{
			new_list = (WideList **) new_malloc(sizeof(WideList *) *
			    (wl_size + 50));
			memset(new_list, 0, sizeof(WideList *) * (wl_size + 50));
			if (wl_size)
				memmove(new_list, wide_list, sizeof(WideList *) * wl_size);
			wl_size += 50;
			new_free((char **)&wide_list);
			wide_list = new_list;
		}
		wide_list[wl_elements] = (WideList *)
			new_malloc(sizeof(WideList));
		wide_list[wl_elements]->channel = (char *) 0;
		wide_list[wl_elements]->users = cnt;
		malloc_strcpy(&wide_list[wl_elements]->channel,
				(*channel != '*') ? channel : "Prv");
		wl_elements++;
		return;
	}

	if (do_hook(current_numeric, "%s %s %s %s", from,  channel, user_cnt, line) 
	    && do_hook(LIST_LIST, "%s %s %s", channel, user_cnt, line))
	{
		if (channel && user_cnt)
		{
			if (*channel == '*')
				say(format, "Prv", user_cnt, line);
			else
				say(format, check_channel_type(channel), 
						user_cnt, line);
		}
	}
}

void	funny_namreply (char *from, char **Args)
{
	char	*type,
		*nick,
		*channel;
static	char	format[40];
static	int	last_width = -1;
	int	cnt;
	char	*ptr;
	char	*line;

	if (!Args[0] || !Args[1] || !Args[2])
		return;		/* Larne-proof */

	PasteArgs(Args, 2);
	type = Args[0];
	channel = Args[1];
	line = Args[2];

	if (channel_is_syncing(channel, from_server))
	{
		message_from(channel, LOG_CRAP);
		if (do_hook(current_numeric, "%s %s %s %s", 
						from, type, channel, line) && 
		    do_hook(NAMES_LIST, "%s %s", channel, line) &&
		    get_int_var(SHOW_CHANNEL_NAMES_VAR))
			say("Users on %s: %s", 
				check_channel_type(channel), line);

		while ((nick = next_arg(line, &line)) != NULL)
		{
			/*
			 * 1999 Oct 29 -- This is a hack to compensate for
			 * a bug in older ircd implementations that can result
			 * in a truncated nickname at the end of a names reply.
			 * The last nickname in a names list is then always
			 * treated with suspicion until the WHO reply is 
			 * completed and we know that its not truncated. --esl
			 */
			if (!line || !*line)
				add_to_channel(channel, nick, from_server, 
						1, 0, 0, 0);
			else
				add_to_channel(channel, nick, from_server, 
						0, 0, 0, 0);
		}

		message_from(NULL, LOG_CURRENT);
		return;
	}

	if (last_width != get_int_var(CHANNEL_NAME_WIDTH_VAR))
	{
		if ((last_width = get_int_var(CHANNEL_NAME_WIDTH_VAR)) != 0)
			snprintf(format, 40, "%%s: %%-%u.%us %%s",
				(unsigned char) last_width,
				(unsigned char) last_width);
		else
			strcpy(format, "%s: %s\t%s");
	}
	ptr = line;
	for (cnt = -1; ptr; cnt++)
	{
		if ((ptr = strchr(ptr, ' ')) != NULL)
			ptr++;
	}
	if (funny_min && (cnt < funny_min))
		return;
	else if (funny_max && (cnt > funny_max))
		return;
	if ((funny_flags & FUNNY_PRIVATE) && (*type == '='))
		return;
	if ((funny_flags & FUNNY_PUBLIC) && ((*type == '*') || (*type == '@')))
		return;

	if (type && channel)
	{
		if (match)
		{
			if (wild_match(match, channel) == 0)
				return;
		}

		message_from(channel, LOG_CRAP);
		if (do_hook(current_numeric, "%s %s %s %s", 
					from, type, channel, line) 
		 && do_hook(NAMES_LIST, "%s %s", channel, line))
		{
			switch (*type)
			{
				case '=':
					if (last_width && (strlen(channel) > last_width))
					{
						channel[last_width-1] = '>';
						channel[last_width] = (char) 0;
					}
					put_it(format, "Pub", check_channel_type(channel), line);
					break;
				case '*':
					put_it(format, "Prv", check_channel_type(channel), line);
					break;
				case '@':
					put_it(format, "Sec", check_channel_type(channel), line);
					break;
			}
		}
		message_from(NULL, LOG_CURRENT);
	}
}

void	funny_mode (char *from, char **ArgList)
{
	char	*mode, *channel;

	if (!ArgList[0] || !ArgList[1])
		return;

	channel = ArgList[0];
	mode = ArgList[1];
	PasteArgs(ArgList, 1);
	if (channel_is_syncing(channel, from_server))
	{
		update_channel_mode(channel, mode);
		update_all_status();
#if 0					/* Now done at the end of WHO reply */
		channel_not_waiting(channel, from_server);
#endif
	}
	else
	{
		if (channel)
		{
			message_from(channel, LOG_CRAP);
			if (do_hook(current_numeric, "%s %s %s", from, channel, mode))
				put_it("%s Mode for channel %s is \"%s\"",
					numeric_banner(), channel, mode);
		}
		else
		{
			if (do_hook(current_numeric, "%s %s", from, mode))
				put_it("%s Channel mode is \"%s\"",
					numeric_banner(), mode);
		}
	}
}

void	update_user_mode (char *modes)
{
	int	onoff = 1;
	const char	*p_umodes = get_possible_umodes(from_server);

	if (x_debug & DEBUG_SERVER_CONNECT)
		yell("Possible user modes for server [%d]: [%s]", from_server, p_umodes);

	for (; *modes; modes++)
	{
		if (*modes == '-')
			onoff = 0;
		else if (*modes == '+')
			onoff = 1;

		else if   ((*modes >= 'a' && *modes <= 'z')
			|| (*modes >= 'A' && *modes <= 'Z'))
		{
			size_t 	idx;
			int 	c = *modes;

			idx = ccspan(p_umodes, c);
			if (p_umodes && p_umodes[idx] == 0)
				yell("WARNING: Invalid user mode %c referenced on server %d",
						*modes, last_server);
			else
				set_server_flag(from_server, idx, onoff);

			if (c == 'O' || c == 'o')
				set_server_operator(from_server, onoff);
		}
	}
}

void	reinstate_user_modes (void)
{
	const char *modes = get_umode(from_server);

	if (!modes && !*modes)
		modes = send_umode;

	if (modes && *modes)
	{
		if (x_debug & DEBUG_OUTBOUND)
			yell("Reinstating your user modes on server [%d] to [%s]", from_server, modes);
		send_to_server("MODE %s +%s", get_server_nickname(from_server), modes);
		clear_user_modes(from_server);
	}
}

