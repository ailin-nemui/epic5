/* $EPIC: funny.c,v 1.9 2003/01/26 03:25:38 jnelson Exp $ */
/*
 * funny.c: Handles the /LIST and /NAMES replies.  Also deals with the
 * /NAMES and /MODE replies that we have automatically sent to us when
 * we join a channel.
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright © 1995, 2003 EPIC Software Labs
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
#include "ircaux.h"
#include "names.h"
#include "numbers.h"
#include "output.h"
#include "parse.h"
#include "server.h"
#include "term.h"
#include "vars.h"

static	char	*match = (char *) 0;
static	int	funny_min;
static	int	funny_max;
static	int	funny_flags;


void	set_funny_flags (int min, int max, int flags, const char *stuff)
{
	funny_min = min;
	funny_max = max;
	funny_flags = flags;
	malloc_strcpy(&match, stuff);
}

void	list_reply (const char *from, const char *comm, char **ArgList)
{
	char	*channel,
		*user_cnt,
		*line;
	int	cnt;
static	char	format[25];
static	int	last_width = -1;

	PasteArgs(ArgList, 2);
	if (!(channel = ArgList[0]))
		{ rfc1459_odd(from, comm, ArgList); return; }
	if (!(user_cnt = ArgList[1]))
		{ rfc1459_odd(from, comm, ArgList); return; }
	if (!(line = ArgList[2]))
		{ rfc1459_odd(from, comm, ArgList); return; }

	if (last_width != get_int_var(CHANNEL_NAME_WIDTH_VAR))
	{
		if ((last_width = get_int_var(CHANNEL_NAME_WIDTH_VAR)) != 0)
			snprintf(format, 25, "%%-%u.%us %%-5s  %%s",
						(unsigned) last_width,
						(unsigned) last_width);
		else
			strcpy(format, "%s\t%-5s  %s");
	}

	/* 
	 * Do not display if the channel has no topic and the user asked
	 * for only channels with topics.
	 */
	if (funny_flags & FUNNY_TOPIC && !(line && *line))
		return;

	/*
	 * Do not display if the channel does not have the necessary number
	 * of users the user asked for
	 */
	cnt = my_atol(user_cnt);
	if (funny_min && (cnt < funny_min))
		return;
	if (funny_max && (cnt > funny_max))
		return;

	/*
	 * Do not display if the channel is not private or public as the
	 * user requested.
	 */
	if ((funny_flags & FUNNY_PRIVATE) && (*channel != '*'))
		return;
	if ((funny_flags & FUNNY_PUBLIC) && (*channel == '*'))
		return;

	/*
	 * Do not display if the channel does not match the user's supplied
	 * wildcard pattern
	 */
	if (match)
	{
		if (wild_match(match, channel) == 0)
			return;
	}

	/*
	 * Otherwise, just throw it at the user.
	 */
	if (do_hook(current_numeric, "%s %s %s %s", from,  channel, user_cnt, line) 
	    && do_hook(LIST_LIST, "%s %s %s", channel, user_cnt, line))
	{
		if (*channel == '*')
			say(format, "Prv", user_cnt, line);
		else
			say(format, check_channel_type(channel), 
					user_cnt, line);
	}
}

void	names_reply (const char *from, const char *comm, char **ArgList)
{
	char	*type,
		*nick,
		*channel;
static	char	format[40];
static	int	last_width = -1;
	int	cnt;
	char	*ptr;
	char	*line;

	PasteArgs(ArgList, 2);
	if (!(type = ArgList[0]))
		{ rfc1459_odd(from, comm, ArgList); return; }
	if (!(channel = ArgList[1]))
		{ rfc1459_odd(from, comm, ArgList); return; }
	if (!(line = ArgList[2]))
		{ rfc1459_odd(from, comm, ArgList); return; }

	if (channel_is_syncing(channel, from_server))
	{
	    message_from(channel, LOG_CRAP);
	    if (do_hook(current_numeric, "%s %s %s %s", 
					from, type, channel, line) && 
			do_hook(NAMES_LIST, "%s %s", channel, line) &&
			get_int_var(SHOW_CHANNEL_NAMES_VAR))
		say("Users on %s: %s", check_channel_type(channel), line);
	    message_from(NULL, LOG_CURRENT);

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
			add_to_channel(channel, nick, from_server, 1, 0, 0, 0);
		else
			add_to_channel(channel, nick, from_server, 0, 0, 0, 0);
	    }

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

	if (match)
	{
		if (wild_match(match, channel) == 0)
			return;
	}

	message_from(channel, LOG_CRAP);
	if (do_hook(current_numeric, "%s %s %s %s", from, type, channel, line) 
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

void	mode_reply (const char *from, const char *comm, char **ArgList)
{
	char	*mode, *channel;

	PasteArgs(ArgList, 1);
	if (!(channel = ArgList[0]))
		{ rfc1459_odd(from, comm, ArgList); return; }
	if (!(mode = ArgList[1]))
		{ rfc1459_odd(from, comm, ArgList); return; }

	/* If we're waiting for MODE reply. */
	if (channel_is_syncing(channel, from_server))
	{
		update_channel_mode(channel, mode);
		update_all_status();
		return;
	}

	message_from(channel, LOG_CRAP);
	if (do_hook(current_numeric, "%s %s %s", from, channel, mode))
		put_it("%s Mode for channel %s is \"%s\"",
			numeric_banner(), channel, mode);
	return;
}
