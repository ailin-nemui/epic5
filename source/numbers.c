/*
 * numbers.c: handles all those strange numeric response dished out by that
 * wacky, nutty program we call ircd 
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright 1993, 2003 EPIC Software Labs.
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
#include "input.h"
#include "ircaux.h"
#include "vars.h"
#include "lastlog.h"
#include "hook.h"
#include "server.h"
#include "numbers.h"
#include "window.h"
#include "screen.h"
#include "output.h"
#include "names.h"
#include "parse.h"
#include "commands.h"
#include "notify.h"
#include "vars.h"
#include "who.h"
#include "alias.h"
#include "reg.h"

static void	add_user_who (int refnum, const char *from, const char *comm, const char **ArgList);
static void	add_user_end (int refnum, const char *from, const char *comm, const char **ArgList);
static 	int	number_of_bans = 0;

/*
 * banner: This returns in a static string of either "xxx" where
 * xxx is the current numeric, or "***" if SHOW_NUMBERS is OFF 
 */
const char *	banner (void)
{
	static	char	thing[80];
	const char *str;

	if (current_numeric > 0 && get_int_var(SHOW_NUMERICS_VAR))
		snprintf(thing, sizeof thing, "%3.3d", current_numeric);
	else if ((str = get_string_var(BANNER_VAR)))
	{
		if (get_int_var(BANNER_EXPAND_VAR))
		{
			char *foo = expand_alias(str, empty_string);
			strlcpy(thing, foo, sizeof thing);
			new_free(&foo);
		}
		else
			strlcpy(thing, str, sizeof thing);
	}
	else
		*thing = 0;

	return (thing);
}


/*
 * display_msg: handles the displaying of messages from the variety of
 * possible formats that the irc server spits out.
 *
 * Simplified some time in 1996.
 * -- called by more than one place.
 */
static void 	display_msg (const char *from, const char *comm, const char **ArgList)
{
	char	*ptr = NULL;
	const char	*rest;
	int	drem;

	/* XXX - 
	 * ArgList[0] was passed to who_from() which means we need to 
	 * either reset who_from() with our own copy, or not detokenize
	 * ArgList[0], because this breaks channel targeting!
	 */
	if (!(rest = PasteArgs(ArgList, 0)))
		{ rfc1459_odd(from, comm, ArgList); return; }

	if (from && (my_strnicmp(get_server_itsname(from_server), from,
			strlen(get_server_itsname(from_server))) == 0))
		from = NULL;

	/* This fix by SrfRog, again by |Rain| */
	if (ptr && ptr > rest && ptr[-1] == ' ')	/* per RFC 1459 */
		*ptr++ = 0;
	else
		ptr = NULL;

        drem = (from) && (!get_int_var(SUPPRESS_FROM_REMOTE_SERVER_VAR));

        /*
         * This handles all the different cases of server messages.
         * If you dont believe me, try it out. :P
         *
         * There are 16 distinct possibilities, since there are 4
         * independant variables.  In practice only about 6 to 8 of 
	 * the possibilities are used.
         */
        put_it("%s %s%s%s%s%s%s%s",
                banner(),
                strlen(rest)        ? rest     : empty_string,
                strlen(rest) && ptr ? ":"      : empty_string,
                strlen(rest)        ? space    : empty_string,
                ptr                 ? ptr      : empty_string,
                drem                ? "(from " : empty_string,
                drem                ? from     : empty_string,
                drem                ? ")"      : empty_string
              );
}

/*
 * numbered_command: does (hopefully) the right thing with the numbered
 * responses from the server.  I wasn't real careful to be sure I got them
 * all, but the default case should handle any I missed (sorry) 
 *
 * The format of a numeric looks like so:
 *
 *	:server-name XXX our-nick Arg1 Arg2 Arg3 ... :ArgN
 *
 * The last argument traditionally has a colon before it, but this is not
 * compulsary.  The BreakArgs function has already broken this up into 
 * words for us, so that what we get, looks like this:
 *
 *	server-name	-> 	from parameter
 *	XXX		->	comm parameter
 *	our-nick	->	ArgList[0]
 *	Arg1		->	ArgList[1]
 *	Arg2		->	ArgList[2]
 *	...			...
 *
 * BUT!  There's a wrinkle in the ointment.  The first thing we do is slurp
 * up ArgList[0] (our-nick) and put it in 'user'.  Then we increment the 
 * ArgList array, so what we actually end up with is:
 *
 *	server-name	-> 	from parameter
 *	XXX		->	comm parameter
 *	our-nick	->	user
 *	Arg1		->	ArgList[0]
 *	Arg2		->	ArgList[1]
 *	...			...
 *	ArgN		->	ArgList[N-1]
 *	NULL		->	ArgList[N]
 */
void 	numbered_command (const char *from, const char *comm, char const **ArgList)
{
	const char	*recipient;
	char *	target = NULL;
	char	*copy;
	int	i;
	int	old_current_numeric = current_numeric;
	int	numeric;
	int	l;

	/* All numerics must be in the range (000, 999) */
	if (!comm || !*comm)
		{ rfc1459_odd(from, comm, ArgList); return; }
	numeric = atol(comm);
	if (numeric < 0 || numeric >= FIRST_NAMED_HOOK - 1)
		{ rfc1459_odd(from, comm, ArgList); return; }

	/* All numerics must have a recipient (our nickname) */
	if (!ArgList[0])
		{ rfc1459_odd(from, comm, ArgList); return; }
	recipient = LOCAL_COPY(ArgList[0]);
	ArgList++;

	/* 
	 * Numerics may have a channel target as 1st argument
	 *
	 * We must make a copy of ArgList[0] to pass to message_from
	 * because display_message (above) will call PasteArgs which
	 * will destroy ArgList[0].
	 *
	 * Please note that we don't consume the ArgList[0] argument,
	 * we only peek at it to see if we should target a channel
	 * with message_from().
	 */
	if (ArgList[0])
		target = LOCAL_COPY(ArgList[0]);
	if (target && is_channel(target))
		l = message_from(target, LEVEL_OTHER);
	else
		l = message_from(NULL, LEVEL_OTHER);

	current_numeric = numeric;	/* must be negative of numeric! */


	/*
	 * XXX-
	 * We only have to worry about non-utf8 message anyways.
	 * Those come into two waves:
	 *	1. Specific words that might have come from users
	 *	2. Anything else (that would have come from server)
	 * Remember -- we only care about non-utf8 things.
	 *	311	WHOISUSER	ArgList[0] (the realname)
	 *	314	WHOWASUSER	ArgList[5] (the realname)
	 *	322	LIST		ArgList[2] (the topic)
	 *	332	TOPIC		ArgList[1] (the topic)
	 *	352	WHOREPLY	ArgList[7] (the realname)
	 */

	/*
	 * This first switch statement is only used for those numerics
	 * which either need to perform some action before the numeric
	 * is offered to the user, or by those actions which need to offer
	 * the numeric to the user in some special manner.  
	 *
	 * Those numerics which require only special display if the user
	 * does not hook them, are handled below.
	 *
	 * Those numerics which require special action after the numeric
	 * is offered to the user, are also handled below.
	 *
	 * Each of these numerics must either "break" (go to step 2)
	 * or must "goto END" (goto step 3).
	 */
	switch (numeric)
	{
	/*
	 * I added the "set_server_nickname" here because the client
	 * when auto-fudging your nick will sometimes be confused as
	 * what your nickname really is when you connect.  Since the
	 * server always tells us who the message was set to (ie, us)
	 * we just kind of take it at its word.
	 */
	case 001:	/* #define RPL_WELCOME          001 */
	{
		server_is_registered(from_server, from, recipient);
		break;
	}

	/* 
	 * Now instead of the terribly horrible hack using numeric 002
	 * to get the server name/server version info, we use the 004
	 * numeric which is what is the most logical choice for it.
	 *
	 * If any of the arguments are missing, we don't abort, because
	 * the client needs 004 to sync.  Instead, we just pass in the
	 * NULL values and hope for the best...
	 */
	case 004:	/* #define RPL_MYINFO           004 */
	{
		const char 	*server = NULL, 
				*version = NULL;

		if (!(server = ArgList[0]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		else if (!(version = ArgList[1]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }

		set_server_version_string(from_server, version);
		break;
	}

	case 005:
	{
		int	arg;
		char	*set, *value;

		for (arg = 0; ArgList[arg] && !strchr(ArgList[arg], ' '); arg++) {
			set = LOCAL_COPY(ArgList[arg]);
			value = strchr(set, '=');
			if (value && *value) 
				*value++ = 0;

			if (*set == '+')	/* Parameter append */
			{
				const char *ov = get_server_005(from_server, ++set);
				value = malloc_strdup2(ov, value);
				set_server_005(from_server, set, value);
				new_free(&value);
			}
			if (*set == '-')	/* Parameter removal */
				set_server_005(from_server, ++set, NULL);
			else if (value && *value)
				set_server_005(from_server, set, value);
			else
				set_server_005(from_server, set, space);
		}
		break;
	}

	case 10:		/* EFNext "Use another server"	010 */
	{
		const char *new_server, *new_port_s, *message;
		int	new_port, old_server;
		char *	str = NULL;
		int	new_servref;

		PasteArgs(ArgList, 2);
		if (!(new_server = ArgList[0]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(new_port_s = ArgList[1]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(message = ArgList[2]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		new_port = (int)atol(ArgList[1]);

		/* Must do these things before calling "display_msg" */
		old_server = from_server;
		malloc_sprintf(&str, "%s:%d:::%s:", new_server, new_port, 
					get_server_group(from_server));
		if ((new_servref = str_to_servref(str)) == NOSERV)
			new_servref = str_to_newserv(str);

		say("The server has asked you to switch to %s:%d.  I set up server refnum %d for you.", new_server, new_port, new_servref);
		say("If you want to switch, do /SERVER %d", new_servref);

#if 0
		change_window_server(old_server, new_servref);
#endif
		from_server = old_server;
		break;
	}

	case 14:		/* Erf/TS4 "cookie" numeric	014 */
	{
		const char *	cookie;

		PasteArgs(ArgList, 0);
		if (!(cookie = ArgList[0]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }

		use_server_cookie(from_server);
		set_server_cookie(from_server, cookie);
		goto END;
	}

	case 42:		/* IRCNet's "unique id" numeric 042 */
	{
		const char *unique_id, *message;

		PasteArgs(ArgList, 1);
		if (!(unique_id = ArgList[0]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(message = ArgList[1]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }

		set_server_unique_id(from_server, unique_id);
		break;
	}

        case 301:               /* #define RPL_AWAY             301 */
        {
		const char *nick, *message;

		PasteArgs(ArgList, 1);
		if (!(nick = ArgList[0]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(message = ArgList[1]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }

		/* Ach.  /on 301 doesn't offer 'from' as $0.  Bummer. */
                if (do_hook(current_numeric, "%s %s", nick, message))
		{
			/* XXXX Hack -- figure out another way */
			copy = alloca(IRCD_BUFFER_SIZE + 1);
			*copy = 0;

			for (i = 0; ArgList[i]; i++)
			{
				if (i)
					strlcat(copy, " ", IRCD_BUFFER_SIZE);
				strlcat(copy, ArgList[i], IRCD_BUFFER_SIZE);
			}
			goto DISPLAY;
		}
		goto END;
        }

	/*	311	WHOISUSER	ArgList[0] (the realname) */
	case 311:		/* #define RPL_WHOISUSER        311 */
	{
		const char *nick, *user, *host, *channel, *name;

		PasteArgs(ArgList, 4);
		if (!(nick = ArgList[0]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(user = ArgList[1]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(host = ArgList[2]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(channel = ArgList[3]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(name = ArgList[4]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }

		break;
	}

	/*	314	WHOWASUSER	ArgList[5] (the realname) */
	case 314:		/* #define RPL_WHOWASUSER       314 */
	{
		const char *nick, *user, *host, *unused, *name;

		PasteArgs(ArgList, 4);
		if (!(nick = ArgList[0]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(user = ArgList[1]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(host = ArgList[2]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(unused = ArgList[3]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(name = ArgList[4]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }

		break;
	}


	case 340:		/* #define RPL_USERIP?          340 */
		if (!get_server_005(from_server, "USERIP"))
			break;
		/* FALLTHROUGH */
		FALLTHROUGH
	case 302:		/* #define RPL_USERHOST         302 */
		userhost_returned(from_server, from, comm, ArgList);
		goto END;

	case 303:		/* #define RPL_ISON             303 */
		ison_returned(from_server, from, comm, ArgList);
		goto END;

	case 315:		/* #define RPL_ENDOFWHO         315 */
		who_end(from_server, from, comm, ArgList);
		goto END;

	case 321:		/* #define RPL_LISTSTART        321 */
	{
		const char *channel, *user_cnt, *line;

		channel = ArgList[0] = "Channel";
		user_cnt = ArgList[1] = "Users";
		line = ArgList[2] = "Topic";
		ArgList[3] = NULL;

		/* Then see if they want to hook /ON LIST */
		if (!do_hook(LIST_LIST, "%s %s %s", channel, user_cnt, line))
			goto END;

		/*
		 * Otherwise, this line is ok.
		 */
		break;
	}

	/*	322	LIST		ArgList[2] (the topic)	  */
	case 322:		/* #define RPL_LIST             322 */
	{
		const char *channel, *user_cnt, *line;
		int	cnt;
		int	funny_flags, funny_min, funny_max;
		const char *funny_match;

		PasteArgs(ArgList, 2);
		if (!(channel = ArgList[0]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(user_cnt = ArgList[1]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(line = ArgList[2]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }

		funny_flags = get_server_funny_flags(from_server);
		funny_min = get_server_funny_min(from_server);
		funny_max = get_server_funny_max(from_server);
		funny_match = get_server_funny_match(from_server);

		/* List messages NEVER go to a chanwin */
		pop_message_from(l);
		l = message_from(NULL, LEVEL_OTHER);

		/*
		 * Do not display if the channel has no topic and the user asked
		 * for only channels with topics.
		 */
		if (funny_flags & FUNNY_TOPIC && !(line && *line))
			goto END;

		/*
		 * Do not display if the channel does not have the necessary 
		 * number of users the user asked for
		 */
		cnt = my_atol(user_cnt);
		if (funny_min && (cnt < funny_min))
			goto END;
		if (funny_max && (cnt > funny_max))
			goto END;

		/*
		 * Do not display if the channel is not private or public as the
		 * user requested.
		 */
		if ((funny_flags & FUNNY_PRIVATE) && (*channel != '*'))
			goto END;
		if ((funny_flags & FUNNY_PUBLIC) && (*channel == '*'))
			goto END;

		/*
		 * Do not display if the channel does not match the user's 
		 * supplied wildcard pattern
		 */
		if (funny_match && wild_match(funny_match, channel) == 0)
			goto END;

		/* Then see if they want to hook /ON LIST */
		if (!do_hook(LIST_LIST, "%s %s %s", channel, user_cnt, line))
			goto END;

		/*
		 * Otherwise, this line is ok.
		 */
		break;
	}

	case 324:		/* #define RPL_CHANNELMODEIS    324 */
	{
		const char      *mode, *channel;

		PasteArgs(ArgList, 1);
		if (!(channel = ArgList[0]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(mode = ArgList[1]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }

		/* If we're waiting for MODE reply. */
		if (channel_is_syncing(channel, from_server))
		{
		    int	numonchannel, maxnum;

		    copy = LOCAL_COPY(channel);
		    update_channel_mode(channel, mode);
		    update_all_status();

		    if (is_channel_anonymous(copy, from_server))
			channel_not_waiting(copy, from_server);
		    else
		    {
			maxnum = get_server_max_cached_chan_size(from_server);
			if (maxnum >= 0)
			{
			    numonchannel = number_on_channel(copy, from_server);
			    if (numonchannel <= maxnum)
				whobase(from_server, copy, add_user_who, 
						add_user_end);
			    else
				channel_not_waiting(copy, from_server);
			}
			else
			    whobase(from_server, copy, add_user_who, 
					add_user_end);
		    }
		}

		break;
	}

	/*	332	TOPIC		ArgList[1] (the topic)	  */
	case 332:		/* #define RPL_TOPIC            332 */
	{
		const char *channel, *topic;

		PasteArgs(ArgList, 1);
		if (!(channel = ArgList[0]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(topic = ArgList[1]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		break;
	}


	/*	352	WHOREPLY	ArgList[7] (the realname) */
	case 352:		/* #define RPL_WHOREPLY         352 */
		whoreply(from_server, NULL, comm, ArgList);
		goto END;

	case 353:		/* #define RPL_NAMREPLY         353 */
	{
		const char	*type, *channel, *line;

		PasteArgs(ArgList, 2);
		if (!(type = ArgList[0]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(channel = ArgList[1]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(line = ArgList[2]))
			{ line = empty_string; }

		if (channel_is_syncing(channel, from_server))
		{
		    char *line_copy = LOCAL_COPY(line);
		    char *nick;

		    while ((nick = next_arg(line_copy, &line_copy)) != NULL)
		    {
			/* XXX Hack to work around space at end of 353 */
			forcibly_remove_trailing_spaces(nick, NULL);

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

		    break;
		}
		else
		{
		    int	cnt;
		    const char	*ptr;
		    int	funny_flags, funny_min, funny_max;
		    const char *funny_match;

		    funny_flags = get_server_funny_flags(from_server);
		    funny_min = get_server_funny_min(from_server);
		    funny_max = get_server_funny_max(from_server);
		    funny_match = get_server_funny_match(from_server);

		    ptr = line;
		    for (cnt = -1; ptr; cnt++)
		    {
			if ((ptr = strchr(ptr, ' ')) != NULL)
				ptr++;
		    }

		    if (funny_min && (cnt < funny_min))
			goto END;
		    else if (funny_max && (cnt > funny_max))
			goto END;

		    if ((funny_flags & FUNNY_PRIVATE) && 
					(*type == '='))
			goto END;
		    if ((funny_flags & FUNNY_PUBLIC) && 
					((*type == '*') || (*type == '@')))
			goto END;

		    if (funny_match && wild_match(funny_match, channel) == 0)
			goto END;
		}

		/* Everything is OK. */
		break;
	}

	case 354:		/* #define RPL_XWHOREPLY	354 */
		xwhoreply(from_server, NULL, comm, ArgList);
		goto END;

	/* XXX Yea yea, these are out of order. so shoot me. */
	case 346:               /* #define RPL_INVITELIST (+I for erf) */
	case 348:               /* #define RPL_EXCEPTLIST (+e for erf) */
	case 367:		/* #define RPL_BANLIST */
		number_of_bans++;
		break;

	case 347:               /* #define END_OF_INVITELIST */
	case 349:               /* #define END_OF_EXCEPTLIST */
	case 368:		/* #define END_OF_BANLIST */
	{
		const char	*channel;

		if (!(channel = ArgList[0]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }

#ifdef IRCII_LIKE_BAN_SUMMARY
		if (do_hook(current_numeric, "%s %s %d", 
			from, channel, number_of_bans))
#else
		if (do_hook(current_numeric, "%s %d %s", 
				from, number_of_bans, channel))
#endif
		{
			put_it("%s Total number of %s on %s - %d",
				banner(), 
                                numeric == 347 ? "invites" :
                               (numeric == 349 ? "exceptions" :
                               (numeric == 368 ? "bans" : "wounds")),
                                channel, number_of_bans);
		}
		goto END;
	}

	/* XXX Shouldn't this set "You're operator" flag for hybrid? */
	case 381: 		/* #define RPL_YOUREOPER        381 */
		if (!is_server_registered(from_server))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		break;

        /* ":%s 401 %s %s :No such nick/channel" */
	case 401:		/* #define ERR_NOSUCHNICK       401 */
	{
		const char	*nick;

		if (!(nick = ArgList[0]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }

		if (!is_channel(nick))
		    notify_mark(from_server, nick, 0, 0);

		break;
	}

	/* Bizarre dalnet extended who replies. */
        /* ":%s 402 %s %s :No such server" */
	case 402:
	{
		const char	*server;

		if (!(server = ArgList[0]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }

		fake_who_end(from_server, from, comm, server);
		break;
	}

	/* Yet more bizarre dalnet extended who replies. */
	/* ":%s 522 %s :/WHO Syntax incorrect, use /who ? for help" */
        /* ":%s 523 %s :Error, /who limit of %d exceed." */
	case 522:
	case 523:
	{
		/* 
		 * This dalnet error message doesn't even give us the
		 * courtesy of telling us which who request was in error,
		 * so we have to guess.  Whee.
		 */
		fake_who_end(from_server, from, comm, NULL);
		break;
	}

	case 403:		/* #define ERR_NOSUCHCHANNEL    403 */
	{
		const char *	s;
		const char *	channel;
		const char *	message;

		PasteArgs(ArgList, 1);

		/* Some servers BBC and send back an empty reply. */
		if (!(channel = ArgList[0]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(message = ArgList[1]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }

		/* Do not accept 403's from remote servers. */
		s = get_server_itsname(from_server);
		if (my_strnicmp(s, from, strlen(s)))
			{ rfc1459_odd(from, comm, ArgList); goto END; }

		/* 
		 * Some servers BBC and send this instead of a
		 * 315 numeric when a who request has been completed.
		 */
		if (fake_who_end(from_server, from, comm, channel))
			;

		/*
		 * If you try to JOIN or PART the "*" named channel, as may
		 * happen if epic gets confused, the server may tell us that
		 * channel does not exist.  But it would be death to try to 
		 * destroy that channel as epic will surely do the wrong thing!
		 * Otherwise, we somehow tried to reference a channel that
		 * this server claims does not exist; we blow the channel away
		 * for good measure.
		 */
		else if (strcmp(channel, "*"))
			remove_channel(channel, from_server);

		break;
	}

	case 421:		/* #define ERR_UNKNOWNCOMMAND   421 */
	{
		const char	*token;

		if (!(token = ArgList[0]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }

		if (check_server_redirect(from_server, token))
			goto END;
		if (check_server_wait(from_server, token))
			goto END;

		break;
	}

	case 437:		/* av2.9's "Nick collision" numeric 437 */
				/* Also, undernet/dalnet "You are banned" */
				/* Also, av2.10's "Can't do that" numeric */
				/* Also, cs's "too many nick changes" num */
	{
		/*
		 * Ugh. What a total trainwreck this is.  Sometimes, I
		 * really hate all the ircd's out there in the world that
		 * have to be supported.
		 *
		 * Well, there are at least four different, occasionally
		 * scrutable ways we can get this numeric.
		 *
		 * 1a) On ircnet -- As an unregistered user, the NICK that
		 *	we are trying to register was used in the past 90
		 *	seconds or so.  The server expects us to send
		 *	another NICK request.
		 *		ARGV[0] IS NICK, REGISTERED IS NO
		 * 1b) On ircnet -- As a registered user, the NICK that 
		 *	we are trying to register was used in the past 90
		 *	seconds or so.  The server expects us not to do
		 * 	anything (like a 432 numeric).
		 *		ARGV[0] IS NICK, REGISTERED IS YES
		 * 2) On ircnet -- As a registered user, we are trying to
		 *	join a channel that was netsplit in the past 24 hours
		 *	or so.  The server expects us not to do anything.
		 *		ARGV[0] IS CHANNEL, REGISTERED IS YES
		 * 3) On undernet/dalnet -- As a registered user, who is
		 *	on a channel where we are banned, a NICK request
		 *	was rejected (because we are banned).  The server
		 *	expects us not to do anything.
		 *		ARGV[0] IS CHANNEL, REGISTERED IS YES
		 * 4) On a comstud efnet servers -- As a registered user, 
		 *	we have changed our nicknames too many times in
		 *	too short a time.  The server expects us not to do
		 *	anything.
		 *		ARGV[0] IS ERROR, ARGV[1] IS NULL.
		 *	I understand this numeric will be moving to 439.
		 */

		/*
		 * Weed out the comstud one first, since it's the most bizarre.
		 */
		if (ArgList[0] && ArgList[1] == NULL)
		{
			accept_server_nickname(from_server, recipient);
			break;
		}

		/*
		 * Now if it's a channel, it might be ircnet telling us we
		 * can't join the channel, or undernet telling us that we 
		 * can't change our nickname because we're banned.  The 
		 * easiest way to tell is to see if we are on the channel.
		 */
		if (is_channel(ArgList[0]))
		{
			/* XXX Is this really neccesary? */
			if (!im_on_channel(ArgList[0], from_server))
				remove_channel(ArgList[0], from_server);

			break;
		}

		/* 
		 * Otherwise, it's an ircnet "nick not available" error.
		 * Let the nickname reset numerics handle this mess.
		 */
		/* FALLTHROUGH */
		FALLTHROUGH
	}

	case 432:		/* #define ERR_ERRONEUSNICKNAME 432 */
	case 433:		/* #define ERR_NICKNAMEINUSE    433 */ 
	case 438:		/* Undernet's "Stop changing your nick" */
	case 439:		/* Comstud's "Can't change nickname" */
	case 453:		/* EFnet/TS4 "nickname lost" numeric 453 */
	{
		const char	*nick;

		if (!(nick = ArgList[0]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }

		/* 
		 * This is a hack for BBC from inspircd.
		 * A 439 numeric received before 001 that is sent to the
		 * nickname we requested is NOT a rejection of that nickname,
		 * it is an informational message from inspircd.
		 */
		if (numeric == 439 && 
		    !is_server_registered(from_server) &&
		    !strcmp(recipient, get_pending_nickname(from_server)))
			break;

		nickname_change_rejected(from_server, recipient);

		if (!from)
			from = "-1";

		break;
	}

	case 442:		/* #define ERR_NOTONCHANNEL	442 */
	{
		const char *	s;
		const char *	channel;
		const char *	message;

		PasteArgs(ArgList, 1);

		/* Some servers BBC and send back an empty reply. */
		if (!(channel = ArgList[0]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(message = ArgList[1]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }

		/* Do not accept this numeric from remote servers */
		s = get_server_itsname(from_server);
		if (my_strnicmp(s, from, strlen(s)))
			{ rfc1459_odd(from, comm, ArgList); goto END; }

		/* Do not ever delete the "*" channel */
		if (strcmp(ArgList[0], "*"))
		    remove_channel(ArgList[0], from_server);

		break;
	}

	case 451:		/* #define ERR_NOTREGISTERED    451 */
	/*
	 * Sometimes the server doesn't catch the USER line, so
	 * here we send a simplified version again  -lynx 
	 */
		register_server(from_server, NULL);
		break;

	case 462:		/* #define ERR_ALREADYREGISTRED 462 */
		change_server_nickname(from_server, NULL);
		break;

	case 477:		/* #define ERR_NEEDREGGEDNICK	477 */
		/* IRCnet has a different 477 numeric. */
		if (ArgList[0] && *ArgList[0] == '+')
			break;
		/* FALLTHROUGH */
		FALLTHROUGH
	case 471:		/* #define ERR_CHANNELISFULL    471 */
	case 473:		/* #define ERR_INVITEONLYCHAN   473 */
	case 474:		/* #define ERR_BANNEDFROMCHAN   474 */
	case 475: 		/* #define ERR_BADCHANNELKEY    475 */
	case 476:		/* #define ERR_BADCHANMASK      476 */
	{
		const char	*channel;

		if (!(channel = ArgList[0]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }

		cant_join_channel(ArgList[0], from_server);
		break;
	}
	}

/* DEFAULT OFFER */
	/*
	 * This is the "default hook" case, where we offer to the user all of
	 * the numerics that were not offered above.  We simply catenate
	 * all of the arguments into a string and offer to the user.
	 * If the user bites, then we skip the "default display" section.
	 */
	copy = alloca(IRCD_BUFFER_SIZE + 1);
	*copy = 0;

	for (i = 0; ArgList[i]; i++)
	{
		if (i)
			strlcat(copy, " ", IRCD_BUFFER_SIZE);
		strlcat(copy, ArgList[i], IRCD_BUFFER_SIZE);
	}

	if (!do_hook(current_numeric, "%s %s", from, copy))
		goto END;

DISPLAY:
/* DEFAULT DISPLAY */
	if (!do_hook(NUMERIC_LIST, "%d %s %s", current_numeric, from, copy))
		goto END;

	/*
	 * This is the "default display" case, where if the user does not 
	 * hook the numeric, we output the message in some special way.
	 * If a numeric does not require special outputting, then we will
	 * just display it with ``display_msg''
	 */
	switch (numeric)
	{
	case 221: 		/* #define RPL_UMODEIS          221 */
	{
		const char *umode;

		PasteArgs(ArgList, 0);
		if (!(umode = ArgList[0]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }

		put_it("%s Your user mode is \"%s\"", banner(), umode);
		break;
	}

	case 271:		/* #define SILENCE_LIST		271 */
	{
		const char *perp, *victim;

		PasteArgs(ArgList, 1);
		if (!(perp = ArgList[0]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(victim = ArgList[1]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }

		put_it("%s %s is ignoring %s", banner(), perp, victim);
		break;
	}

	case 301:		/* #define RPL_AWAY             301 */
	{
		const char *nick, *message;

		PasteArgs(ArgList, 1);
		if (!(nick = ArgList[0]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(message = ArgList[1]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }

		put_it("%s %s is away: %s", banner(), nick, message);
		break;
	}

	case 311:		/* #define RPL_WHOISUSER        311 */
	{
		const char *nick, *user, *host, *channel, *name;

		PasteArgs(ArgList, 4);
		if (!(nick = ArgList[0]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(user = ArgList[1]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(host = ArgList[2]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(channel = ArgList[3]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(name = ArgList[4]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }

		put_it("%s %s is %s@%s (%s)", banner(), nick, user, host, name);
		break;
	}

	case 312:		/* #define RPL_WHOISSERVER      312 */
	{
		const char *nick, *server, *pithy;

		PasteArgs(ArgList, 2);
		if (!(nick = ArgList[0]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(server = ArgList[1]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(pithy = ArgList[2]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }

		put_it("%s on irc via server %s (%s)", banner(), server, pithy);
		break;
	}

	case 313:		/* #define RPL_WHOISOPERATOR    313 */
	{
		const char *nick, *message;

		PasteArgs(ArgList, 1);
		if (!(nick = ArgList[0]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(message = ArgList[1]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }

		put_it("%s %s %s", banner(), ArgList[0], ArgList[1]);
		break;
	}

	case 314:		/* #define RPL_WHOWASUSER       314 */
	{
		const char *nick, *user, *host, *unused, *name;

		PasteArgs(ArgList, 4);
		if (!(nick = ArgList[0]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(user = ArgList[1]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(host = ArgList[2]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(unused = ArgList[3]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(name = ArgList[4]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }

		put_it("%s %s was %s@%s (%s)",banner(), nick, user, host, name);
		break;
	}

	case 317:		/* #define RPL_WHOISIDLE        317 */
	{
		const char *nick, *idle_str, *startup_str;
		int		idle;
		const char *	unit;
		char 	startup_ctime[128];

		if (!(nick = ArgList[0]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(idle_str = ArgList[1]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(startup_str = ArgList[2])) { /* No problem */; } 

		*startup_ctime = 0;
		if (startup_str)		/* Undernet/TS4 */
		{
		    time_t	startup;

		    if ((startup = atol(startup_str)) != 0)
			snprintf(startup_ctime, 128, ", signed on at %s", 
							my_ctime(startup));
		}

		if ((idle = atoi(idle_str)) > 59)
		{
			idle /= 60;
			unit = "minute";
		}
		else
			unit = "second";

		put_it ("%s %s has been idle %d %ss%s",
			banner(), nick, idle, unit, startup_ctime);
		break;
	}

	case 318:		/* #define RPL_ENDOFWHOIS       318 */
	{
		PasteArgs(ArgList, 0);
		display_msg(from, comm, ArgList);
		break;
	}

	case 319:		/* #define RPL_WHOISCHANNELS    319 */
	{
		const char *nick, *channels;

		PasteArgs(ArgList, 1);
		if (!(nick = ArgList[0]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(channels = ArgList[1]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }

		put_it("%s on channels: %s", banner(), channels);
		break;
	}

	case 321:		/* #define RPL_LISTSTART	321 */
		/* Our screwy 321 handling demands this. BAH! */
		put_it("%s Channel Users Topic", banner());
		break;

	case 322:		/* #define RPL_LIST             322 */
	{
		static char format[35];
		static int last_width = -1;
		const char *channel, *user_cnt, *line;

		PasteArgs(ArgList, 2);
		if (!(channel = ArgList[0]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(user_cnt = ArgList[1]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(line = ArgList[2]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }

		/* Figure out how to display this to the user. */
		if (last_width != get_int_var(CHANNEL_NAME_WIDTH_VAR))
		{
			if ((last_width = get_int_var(CHANNEL_NAME_WIDTH_VAR)))
				snprintf(format, 35, "%%-%u.%us %%-5s  %%s",
				(unsigned) last_width,
				(unsigned) last_width);
			else
				strlcpy(format, "%s\t%-5s  %s", sizeof format);
		}

		if (*channel == '*')
			say(format, "Prv", user_cnt, line);
		else
			say(format, check_channel_type(channel),
				user_cnt, line);

		break;
	}

	case 324:		/* #define RPL_CHANNELMODEIS    324 */
	{
		const char      *mode, *channel;

		PasteArgs(ArgList, 1);
		if (!(channel = ArgList[0]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(mode = ArgList[1]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }

		put_it("%s Mode for channel %s is \"%s\"",
				banner(), channel, mode);
		break;
	}

	case 329:		/* #define CREATION_TIME	329 */
	{
		const char *channel, *time1_str, *time2_str, *time3_str;
		time_t	time1, time2, time3;

		PasteArgs(ArgList, 2);
		if (!(channel = ArgList[0]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(time1_str = ArgList[1]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(time2_str = ArgList[2])) { /* No problem */; }
		if (!(time3_str = ArgList[3])) { /* No problem */; }


		/* Erf/TS4 support */
		if (time2_str && time3_str)
		{
			time1 = (time_t)my_atol(time1_str);
			time2 = (time_t)my_atol(time2_str);
			time3 = (time_t)my_atol(time3_str);

			put_it("%s Channel %s was created at "INTMAX_FORMAT", "
				  "+c was last set at "INTMAX_FORMAT", "
				  "and has been opless since "INTMAX_FORMAT, 
					banner(), channel, (intmax_t)time1, 
					(intmax_t)time2, (intmax_t)time3);
		}
		else
		{
			time1 = (time_t)my_atol(time1_str);

			put_it("%s Channel %s was created at %s",
					banner(), channel, my_ctime(time1));
		}
		break;
	}

	case 330:		/* #define RPL_WHOISLOGGEDIN	330 */
	{
		const char *nick, *login, *reason;

		PasteArgs(ArgList, 2);
		if (!(nick = ArgList[0]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(login = ArgList[1]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(reason = ArgList[2]))
			{ reason = "is logged in as"; }

		put_it("%s %s %s %s", banner(), nick, reason, login);
		break;
	}

	case 332:		/* #define RPL_TOPIC            332 */
	{
		const char *channel, *topic;

		PasteArgs(ArgList, 1);
		if (!(channel = ArgList[0]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(topic = ArgList[1]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }

		put_it("%s Topic for %s: %s", banner(), channel, topic);
		break;
	}

	case 333:		/* #define RPL_TOPICWHOTIME	333 */
	{
		const char *channel, *nick, *when_str;
		time_t	howlong;

		if (!(channel = ArgList[0]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(nick = ArgList[1]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(when_str = ArgList[2]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }

		howlong = time(NULL) - my_atol(when_str);
		put_it("%s The topic was set by %s "INTMAX_FORMAT" sec ago",
				banner(), nick, (intmax_t)howlong);
		break;
	}

	case 338:		/* #define RPL_WHOISACTUALLY	338 */
	{
		const char *who, *host;

		if (!(who = ArgList[0]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(host = ArgList[1]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }

		put_it("%s %s is actually using %s", banner(), who, host);
		break;
	}

	case 341:		/* #define RPL_INVITING         341 */
	{
		const char *nick, *channel;

		if (!(nick = ArgList[0]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(channel = ArgList[1]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }

		pop_message_from(l);
		l = message_from(channel, LEVEL_OTHER);
		put_it("%s Inviting %s to channel %s", banner(), nick, channel);
		break;
	}

	case 351:		/* #define RPL_VERSION          351 */
	{
		const char *version, *itsname, *stuff;

		PasteArgs(ArgList, 2);
		if (!(version = ArgList[0]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(itsname = ArgList[1])) {
			/* As a favor to larne, for inspired */
			put_it("%s Server %s", banner(), version);
			break;
		}
		if (!(stuff = ArgList[2]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }

		put_it("%s Server %s: %s %s",banner(), itsname, version, stuff);
		break;
	}

	case 353:		/* #define RPL_NAMREPLY         353 */
	{
		static int last_width;
		char format[41];
		const char	*type, *channel, *line;

		PasteArgs(ArgList, 2);
		if (!(type = ArgList[0]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(channel = ArgList[1]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(line = ArgList[2]))
			{ line = empty_string; }

		/* This is only for when the user joined the channel */
		if (channel_is_syncing(channel, from_server))
		{
			/* If the user bites on /ON NAMES, then skip the rest */
			pop_message_from(l);
			l = message_from(channel, LEVEL_OTHER);
			if (do_hook(NAMES_LIST, "%s %s", channel, line))
			    if (get_int_var(SHOW_CHANNEL_NAMES_VAR))
				say("Users on %s: %s",
					check_channel_type(channel), line);
			break;
		}

		/* If the user grabs /ON NAMES then just stop right here */
		if (!do_hook(NAMES_LIST, "%s %s", channel, line))
			break;

		/* This all is for when the user has not just joined channel */
		if (last_width != get_int_var(CHANNEL_NAME_WIDTH_VAR))
		{
			if ((last_width = get_int_var(CHANNEL_NAME_WIDTH_VAR)))
				snprintf(format, 40, "%%s: %%-%u.%us %%s",
					(unsigned char) last_width,
					(unsigned char) last_width);
			else
				strlcpy(format, "%s: %s\t%s", sizeof format);
		}
		else
			strlcpy(format, "%s: %s\t%s", sizeof format);

		pop_message_from(l);
		l = message_from(channel, LEVEL_OTHER);
		if (*type == '=') 
		{
		    if (last_width && ((int)strlen(channel) > last_width))
		    {
			char *channel_copy = LOCAL_COPY(channel);
			channel_copy[last_width-1] = '>';
			channel_copy[last_width] = 0;
			channel = channel_copy;
		    }
		    put_it(format, "Pub", check_channel_type(channel), line);
		}
		else if (*type == '*') 
		    put_it(format, "Prv", check_channel_type(channel), line);
		else if (*type == '@')
		    put_it(format, "Sec", check_channel_type(channel), line);

		break;
	}

	case 364:		/* #define RPL_LINKS            364 */
	{
		const char	*itsname, *uplink, *stuff;

		PasteArgs(ArgList, 2);
		if (!(itsname = ArgList[0]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(uplink = ArgList[1]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(stuff = ArgList[2])) { stuff = empty_string; }

		if (stuff)
			put_it("%s %-20s %-20s %s", banner(),
					itsname, uplink, stuff);
		else
			put_it("%s %-20s %s", banner(), itsname, uplink);

		break;
	}

	case 366:		/* #define RPL_ENDOFNAMES       366 */
	{
		const char	*channel;

		if (!(channel = ArgList[0]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }

		if (!channel_is_syncing(channel, from_server))
			display_msg(from, comm, ArgList);

		break;
	}

	case 346:               /* #define RPL_INVITELIST (+I for erf) */
	case 348:               /* #define RPL_EXCEPTLIST (+e for erf) */
	case 367:
	{
		const char	*channel, *ban, *perp, *when_str;
		time_t	howlong;

		if (!(channel = ArgList[0]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(ban = ArgList[1]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(perp = ArgList[2])) { /* No problem. */ }
		if (!(when_str = ArgList[3])) { /* No problem. */ }

		if (perp && when_str) 
		{
			howlong = time(NULL) - my_atol(when_str);
			put_it("%s %s %-25s set by %-10s "INTMAX_FORMAT" sec ago", 
				banner(), channel, ban, perp, (intmax_t)howlong);
		}
		else
			put_it("%s %s %s", banner(), channel, ban);

		break;
	}

	case 401:		/* #define ERR_NOSUCHNICK       401 */
	{
		const char	*nick, *stuff;

		PasteArgs(ArgList, 1);
		if (!(nick = ArgList[0]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }
		if (!(stuff = ArgList[1]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }

		put_it("%s %s: %s", banner(), nick, stuff);
		break;
	}

	case 219:		/* #define RPL_ENDOFSTATS       219 */
	case 232:		/* #define RPL_ENDOFSERVICES    232 */
	case 365:		/* #define RPL_ENDOFLINKS       365 */
	case 369:		/* #define RPL_ENDOFWHOWAS      369 */
	case 374:		/* #define RPL_ENDOFINFO        374 */
	case 394:		/* #define RPL_ENDOFUSERS       394 */
	{
		PasteArgs(ArgList, 0);
		display_msg(from, comm, ArgList);
		break;
	}

	case 471:		/* #define ERR_CHANNELISFULL    471 */
	{
		const char *message;

		PasteArgs(ArgList, 0);
		if (!(message = ArgList[0]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }

		put_it("%s %s (Channel is full)", banner(), message);
		break;
	}
	case 473:		/* #define ERR_INVITEONLYCHAN   473 */
	{
		const char *message;

		PasteArgs(ArgList, 0);
		if (!(message = ArgList[0]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }

		put_it("%s %s (You must be invited)", banner(), message);
		break;
	}
	case 474:		/* #define ERR_BANNEDFROMCHAN   474 */
	{
		const char *message;

		PasteArgs(ArgList, 0);
		if (!(message = ArgList[0]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }

		put_it("%s %s (You are banned)", banner(), message);
		break;
	}
	case 475: 		/* #define ERR_BADCHANNELKEY    475 */
	{
		const char *message;

		PasteArgs(ArgList, 0);
		if (!(message = ArgList[0]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }

		put_it("%s %s (You must give the correct key)", 
						banner(), message);
		break;
	}
	case 476:		/* #define ERR_BADCHANMASK      476 */
	{
		const char *message;

		PasteArgs(ArgList, 0);
		if (!(message = ArgList[0]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }

		put_it("%s %s (Bad channel mask)", banner(), message);
		break;
	}
	case 477:		/* #define ERR_NEEDREGGEDNICK	477 */
	{
		const char *message;

		PasteArgs(ArgList, 0);
		if (!(message = ArgList[0]))
			{ rfc1459_odd(from, comm, ArgList); goto END; }

		/* IRCnet has a different 477 numeric. */
		if (message && *message == '+')
		{
			display_msg(from, comm, ArgList);
			break;
		}

		PasteArgs(ArgList, 0);
		put_it("%s %s (You must use a registered nickname)", 
					banner(), message);
		break;
	}

	default:
		display_msg(from, comm, ArgList);
	}

END:
	/*
	 * This is where we clean up after our numeric.  Numeric-specific
	 * cleanups can occur here, and then below we reset the display
	 * settings.
	 */
	switch (numeric)
	{
	case 347:               /* #define END_OF_INVITELIST */
	case 349:               /* #define END_OF_EXCEPTLIST */
	case 368:
		number_of_bans = 0;
		break;
	case 464:		/* #define ERR_PASSWDMISMATCH   464 */
	{
		if (oper_command)
			oper_command = 0;
		else if (!is_server_registered(from_server))
		{
			say("Password required for connection to server %s",
				get_server_name(from_server));
			if (!dumb_mode)
			{
				add_wait_prompt("Server Password:", 
					password_sendline, NUMSTR(from_server),
					WAIT_PROMPT_LINE, 0);
			}
		}
	}
	}

	current_numeric = old_current_numeric;
	pop_message_from(l);
}


static void	add_user_who (int refnum, const char *from, const char *comm, const char **ArgList)
{
	const char 	*channel, *user, *host, *server, *nick;
	size_t	size;
	char 	*uh;

	if (!(channel = ArgList[0]))
		{ rfc1459_odd(from, "*", ArgList); return; }
	if (!(user = ArgList[1]))
		{ rfc1459_odd(from, "*", ArgList); return; }
	if (!(host = ArgList[2]))
		{ rfc1459_odd(from, "*", ArgList); return; }
	if (!(server = ArgList[3]))
		{ rfc1459_odd(from, "*", ArgList); return; }
	if (!(nick = ArgList[4]))
		{ rfc1459_odd(from, "*", ArgList); return; }

	size = strlen(user) + strlen(host) + 2;
	uh = alloca(size);
	snprintf(uh, size, "%s@%s", user, host);
	add_userhost_to_channel(channel, nick, refnum, uh);
}

static void	add_user_end (int refnum, const char *from, const char *comm, const char **ArgList)
{
	char *	copy;
	char *	channel;

	if (!ArgList[0])
		{ rfc1459_odd(from, "*", ArgList); return; }

	copy = LOCAL_COPY(ArgList[0]);
	channel = next_arg(copy, &copy);
	channel_not_waiting(channel, refnum);
}

