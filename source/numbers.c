/* $EPIC: numbers.c,v 1.34 2003/01/26 03:25:38 jnelson Exp $ */
/*
 * numbers.c: handles all those strange numeric response dished out by that
 * wacky, nutty program we call ircd 
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright © 1993, 2002 EPIC Software Labs.
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
#include "funny.h"
#include "parse.h"
#include "commands.h"
#include "notice.h"
#include "notify.h"
#include "vars.h"
#include "who.h"
#include "alias.h"

static 	int	number_of_bans = 0;

/*
 * numeric_banner: This returns in a static string of either "xxx" where
 * xxx is the current numeric, or "***" if SHOW_NUMBERS is OFF 
 */
char	*numeric_banner (void)
{
	static	char	thing[80];
	char *str;

	if (current_numeric < 0 && get_int_var(SHOW_NUMERICS_VAR))
		sprintf(thing, "%3.3u", -current_numeric);
	else if ((str = get_string_var(BANNER_VAR)))
	{
		if (get_int_var(BANNER_EXPAND_VAR))
		{
			int af;
			char *foo = expand_alias(str, empty_string, &af, NULL);
			strmcpy(thing, foo, 79);
			new_free(&foo);
		}
		else
			strlcpy(thing, str, 80);
	}
	else
		*thing = 0;

	return (thing);
}


/*
 * display_msg: handles the displaying of messages from the variety of
 * possible formats that the irc server spits out.
 *
 * Simplified by Jeremy Nelson (esl) some time in 1996.
 * -- called by more than one place.
 */
void 	display_msg (const char *from, char **ArgList)
{
	char	*ptr = NULL;
	char	*rest;
	int	drem;

	rest = PasteArgs(ArgList, 0);
	if (from && (my_strnicmp(get_server_itsname(from_server), from,
			strlen(get_server_itsname(from_server))) == 0))
		from = NULL;

	/* This fix by SrfRog, again by |Rain| */
	ptr = strchr(rest, ':');
	if (ptr && ptr > rest && ptr[-1] == ' ')	/* per RFC 1459 */
		*ptr++ = 0;
	else
		ptr = NULL;

        drem = (from) && (!get_int_var(SUPPRESS_FROM_REMOTE_SERVER));

        /*
         * This handles all the different cases of server messages.
         * If you dont believe me, try it out. :P
         *
         * There are 16 distinct possibilities, since there are 4
         * independant variables.  In practice only about 6 to 8 of 
	 * the possibilities are used.
         */
        put_it("%s %s%s%s%s%s%s%s",
                numeric_banner(),
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
void 	numbered_command (const char *from, const char *orig_comm, char **ArgList)
{
	const char	*target;
	char	*copy;
	int	i;
	int	lastlog_level;
	int	old_current_numeric = current_numeric;
	int	comm;

	/* All numerics must have a target (our nickname) */
	if (!orig_comm || !*orig_comm)
		{ rfc1459_odd(from, orig_comm, ArgList); return; }
	if (!(target = ArgList[0]))
		{ rfc1459_odd(from, orig_comm, ArgList); return; }
	ArgList++;

	lastlog_level = set_lastlog_msg_level(LOG_CRAP);
	if (ArgList[0] && is_channel(ArgList[0]))
		message_from(ArgList[0], LOG_CRAP);
	else
		message_from(NULL, LOG_CRAP);

	comm = atol(orig_comm);
	current_numeric = -comm;	/* must be negative of numeric! */

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
	switch (comm)
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
		accept_server_nickname(from_server, target);
		server_is_registered(from_server, 1);
		userhostbase(from_server, NULL, got_my_userhost, 1);
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
				*version = NULL, 
				*umodes = NULL;

		PasteArgs(ArgList, 3);
		if (!(server = ArgList[0]))
			{ rfc1459_odd(from, orig_comm, ArgList); }
		else if (!(version = ArgList[1]))
			{ rfc1459_odd(from, orig_comm, ArgList); }
		else if (!(umodes = ArgList[2]))
			{ rfc1459_odd(from, orig_comm, ArgList); }

		got_initial_version_28(server, version, umodes);
		break;
	}

	case 005:
	{
		int	arg;
		char	*set, *value;
		for (arg = 0; ArgList[arg] && !strchr(ArgList[arg], ' '); arg++) {
			set = m_strdup(ArgList[arg]);
			value = strchr(set, '=');
			if (value && *value) 
				*(value++) = '\0';
			set_server_005(from_server, set, value?value:space);
			new_free(&set);
		}
		break;
	}

	case 10:		/* EFNext "Use another server"	010 */
	{
		const char *	new_server;
		const char *	new_port_s;
		const char *	message;
		int		new_port;
		int		old_server;

		PasteArgs(ArgList, 2);
		if (!(new_server = ArgList[0]))
			{ rfc1459_odd(from, orig_comm, ArgList); goto END; }
		if (!(new_port_s = ArgList[1]))
			{ rfc1459_odd(from, orig_comm, ArgList); goto END; }
		if (!(message = ArgList[2]))
			{ rfc1459_odd(from, orig_comm, ArgList); goto END; }
		new_port = atol(ArgList[1]);

		/* Must do these things before calling "display_msg" */
		old_server = from_server;
		add_to_server_list(new_server, new_port, NULL, NULL,
				get_server_group(from_server), NULL, 0);
		server_reconnects_to(old_server, from_server);
		from_server = old_server;

		break;
	}

	case 14:		/* Erf/TS4 "cookie" numeric	014 */
		use_server_cookie(from_server);
		set_server_cookie(from_server, ArgList[0]);
		goto END;

        case 301:               /* #define RPL_AWAY             301 */
        {
                PasteArgs(ArgList, 1);
                if (do_hook(current_numeric, "%s %s", ArgList[0], ArgList[1]))
			break;
		goto END;
        }

	case 307:		/* #define RPL_USERIP           307 */
		if (!get_server_005(from_server, "USERIP"))
			break;
		/* FALLTHROUGH */
	case 302:		/* #define RPL_USERHOST         302 */
		userhost_returned(from_server, from, orig_comm, ArgList);
		goto END;

	case 303:		/* #define RPL_ISON             303 */
		ison_returned(from_server, from, orig_comm, ArgList);
		goto END;

	case 315:		/* #define RPL_ENDOFWHO         315 */
		who_end(from_server, from, orig_comm, ArgList);
		goto END;

	case 321:		/* #define RPL_LISTSTART        321 */
	{
		ArgList[0] = "Channel";
		ArgList[1] = "Users";
		ArgList[2] = "Topic";
		ArgList[3] = NULL;
		list_reply(from, orig_comm, ArgList);
		goto END;
	}

	case 322:		/* #define RPL_LIST             322 */
		list_reply(from, orig_comm, ArgList);
		goto END;

	case 324:		/* #define RPL_CHANNELMODEIS    324 */
		mode_reply(from, orig_comm, ArgList);
		goto END;

	case 352:		/* #define RPL_WHOREPLY         352 */
		whoreply(from_server, NULL, orig_comm, ArgList);
		goto END;

	case 353:		/* #define RPL_NAMREPLY         353 */
		names_reply(from, orig_comm, ArgList);
		goto END;

	case 354:		/* #define RPL_XWHOREPLY	354 */
		xwhoreply(from_server, NULL, orig_comm, ArgList);
		goto END;

	case 367:		/* #define RPL_BANLIST */
		number_of_bans++;
		break;

	case 368:		/* #define END_OF_BANLIST */
	{
		if (get_int_var(SHOW_END_OF_MSGS_VAR))
			goto END;

#ifdef IRCII_LIKE_BAN_SUMMARY
		if (do_hook(current_numeric, "%s %s %d", 
			from, *ArgList, number_of_bans))
#else
		if (do_hook(current_numeric, "%s %d %s", 
			from, number_of_bans, *ArgList))
#endif
		{
			put_it("%s Total number of bans on %s - %d",
				numeric_banner(), ArgList[0], 
				number_of_bans);
		}

		goto END;
	}

	/* XXX Shouldn't this set "You're operator" flag for hybrid? */
	case 381: 		/* #define RPL_YOUREOPER        381 */
		if (!is_server_registered(from_server))
			{ rfc1459_odd(from, orig_comm, ArgList); goto END; }
		break;

        /* ":%s 401 %s %s :No such nick/channel" */
	case 401:		/* #define ERR_NOSUCHNICK       401 */
	{
		notify_mark(from_server, ArgList[0], 0, 0);
		if (get_int_var(AUTO_WHOWAS_VAR))
		{
			int foo = get_int_var(NUM_OF_WHOWAS_VAR);

			if (foo > -1)
				send_to_server("WHOWAS %s %d", ArgList[0], foo);
			else
				send_to_server("WHOWAS %s", ArgList[0]);
		}

		break;
	}

	/* Bizarre dalnet extended who replies. */
        /* ":%s 402 %s %s :No such server" */
	case 402:
		fake_who_end(from_server, from, orig_comm, ArgList[0]);
		break;

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
		fake_who_end(from_server, from, orig_comm, NULL);
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
			{ rfc1459_odd(from, orig_comm, ArgList); goto END; }
		if (!(message = ArgList[1]))
			{ rfc1459_odd(from, orig_comm, ArgList); goto END; }

		/* Do not accept 403's from remote servers. */
		s = get_server_itsname(from_server);
		if (my_strnicmp(s, from, strlen(s)))
			{ rfc1459_odd(from, orig_comm, ArgList); goto END; }

		/* 
		 * Some servers BBC and send this instead of a
		 * 315 numeric when a who request has been completed.
		 */
		if (fake_who_end(from_server, from, orig_comm, channel))
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
		if (check_server_redirect(from_server, ArgList[0]))
			goto END;
		if (check_server_wait(from_server, ArgList[0]))
			goto END;

		break;
	}
	case 432:		/* #define ERR_ERRONEUSNICKNAME 432 */
	{
		if (!my_stricmp(target, ArgList[0]))
			yell("WARNING:  Strange invalid nick message received.  You are probably lagged.");
		else if (get_int_var(AUTO_NEW_NICK_VAR))
			fudge_nickname(from_server);
		else
			reset_nickname(from_server);

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
			accept_server_nickname(from_server, target);
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
		 * Otherwise, a nick command failed.  Oh boy.
		 * If we are registered, abort the nick change and
		 * hope for the best.
		 */
		if (is_server_registered(from_server))
		{
			accept_server_nickname(from_server, target);
			break;
		}

		/* 
		 * Otherwise, it's an ircnet "nick not available" error.
		 * Let the nickname reset numerics handle this mess.
		 */
		/* FALLTHROUGH */
	}

	case 433:		/* #define ERR_NICKNAMEINUSE    433 */ 
	case 438:		/* EFnet/TS4 "nick collision" numeric 438 */
	case 453:		/* EFnet/TS4 "nickname lost" numeric 453 */
	{
		if (!my_stricmp(target, ArgList[0]))
			/* This should stop the "rolling nicks" in their tracks. */
			yell("WARNING:  Strange invalid nick message received.  You are probably lagged.");
		else if (get_int_var(AUTO_NEW_NICK_VAR))
			fudge_nickname(from_server);
		else
			reset_nickname(from_server);

		if (!from)
			from = "-1";

		break;
	}

	case 439:		/* Comstud's "Can't change nickname" */
	{
		accept_server_nickname(from_server, target);
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
			{ rfc1459_odd(from, orig_comm, ArgList); goto END; }
		if (!(message = ArgList[1]))
			{ rfc1459_odd(from, orig_comm, ArgList); goto END; }

		/* Do not accept this numeric from remote servers */
		s = get_server_itsname(from_server);
		if (my_strnicmp(s, from, strlen(s)))
			{ rfc1459_odd(from, orig_comm, ArgList); goto END; }

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

	case 465:		/* #define ERR_YOUREBANNEDCREEP 465 */
	{
		/* 
		 * There used to be a say() here, but if we arent 
		 * connected to a server, then doing say() is not
		 * a good idea.  So now it just doesnt do anything.
		 */
		server_reconnects_to(from_server, NOSERV);
		break;
	}

	case 477:		/* #define ERR_NEEDREGGEDNICK	477 */
		/* IRCnet has a different 477 numeric. */
		if (ArgList[0] && *ArgList[0] == '+')
			break;
		/* FALLTHROUGH */
	case 471:		/* #define ERR_CHANNELISFULL    471 */
	case 473:		/* #define ERR_INVITEONLYCHAN   473 */
	case 474:		/* #define ERR_BANNEDFROMCHAN   474 */
	case 475: 		/* #define ERR_BADCHANNELKEY    475 */
	case 476:		/* #define ERR_BADCHANMASK      476 */
	{
		if (ArgList[0])
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

/* DEFAULT DISPLAY */
	/*
	 * This is the "default display" case, where if the user does not 
	 * hook the numeric, we output the message in some special way.
	 * If a numeric does not require special outputting, then we will
	 * just display it with ``display_msg''
	 */
	switch (comm)
	{
	case 221: 		/* #define RPL_UMODEIS          221 */
	{
		put_it("%s Your user mode is \"%s\"", numeric_banner(),
			ArgList[0]);
		break;
	}

	case 271:		/* #define SILENCE_LIST		271 */
	{
		put_it ("%s %s is ignoring %s", numeric_banner(), ArgList[0], ArgList[1]);
		break;
	}

	case 301:		/* #define RPL_AWAY             301 */
	{
		if (!ArgList[0] || !ArgList[1])
			break;		/* Not what i'm expecting */

		PasteArgs(ArgList, 1);
		put_it("%s %s is away: %s", numeric_banner(),
			ArgList[0], ArgList[1]);
		break;
	}

	case 311:		/* #define RPL_WHOISUSER        311 */
	{
		if (!ArgList[0] || !ArgList[1] || !ArgList[2] || !ArgList[3] || !ArgList[4])
			return;		/* Larneproofing */

		PasteArgs(ArgList, 4);
		put_it("%s %s is %s@%s (%s)", numeric_banner(),
			ArgList[0], ArgList[1], ArgList[2], ArgList[4]);
		break;
	}

	case 312:		/* #define RPL_WHOISSERVER      312 */
	{
		if (!ArgList[0] || !ArgList[1] || !ArgList[2])
			return;		/* Larneproofing */

		put_it("%s on irc via server %s (%s)", numeric_banner(),
			ArgList[1], ArgList[2]);
		break;
	}

	case 313:		/* #define RPL_WHOISOPERATOR    313 */
	{
		if (!ArgList[0] || !ArgList[1])
			break;		/* Larne-proof epic */

		PasteArgs(ArgList, 1);
		put_it("%s %s %s", numeric_banner(), ArgList[0], ArgList[1]);
		break;
	}

	case 314:		/* #define RPL_WHOWASUSER       314 */
	{
		if (!ArgList[0] || !ArgList[1] || !ArgList[2] || !ArgList[3] || !ArgList[4])
			return;		/* Larneproofing */

		PasteArgs(ArgList, 4);
		put_it("%s %s was %s@%s (%s)", numeric_banner(),
			ArgList[0], ArgList[1], ArgList[2], ArgList[4]);
		break;
	}

	case 317:		/* #define RPL_WHOISIDLE        317 */
	{
		char	flag, *nick, *idle_str, *startup_str;
		int	idle;
		time_t	startup;

		if (!ArgList[0] || !ArgList[1] || !ArgList[2])
			return;		/* Larneproofing */

		nick = ArgList[0];
		idle_str = ArgList[1];
		startup_str = ArgList[2];

		if (ArgList[3])	/* undernet */
		{
			PasteArgs(ArgList, 3);
			if ((idle = atoi(idle_str)) > 59)
			{
				idle /= 60;
				flag = 1;
			}
			else
				flag = 0;

			if ((startup = atol(startup_str)) != 0)
				put_it ("%s %s has been idle %d %ss, signed on at %s",
					numeric_banner(), nick, idle, 
					flag?"minute":"second",my_ctime(startup));
			else
				put_it("%s %s has been idle %d %ss", 
					numeric_banner(), nick, idle, 
					flag? "minute": "second");
		}
		else	/* efnet */
		{
			PasteArgs(ArgList, 1);
			if ((idle = atoi(idle_str)) > 59)
			{
				idle /= 60;
				flag = 1;
			}
			else
				flag = 0;

			put_it ("%s %s has been idle %d %ss",
				numeric_banner(), nick, idle, 
				flag?"minute":"second");
		}

		break;
	}

	case 318:		/* #define RPL_ENDOFWHOIS       318 */
	{
		PasteArgs(ArgList, 0);
		if (get_int_var(SHOW_END_OF_MSGS_VAR))
			display_msg(from, ArgList);
		break;
	}

	case 319:		/* #define RPL_WHOISCHANNELS    319 */
	{
		if (!ArgList[0] || !ArgList[1])
			return;		/* Larneproofing */

		PasteArgs(ArgList, 1);
		put_it("%s on channels: %s", numeric_banner(), ArgList[1]);
		break;
	}


	case 329:		/* #define CREATION_TIME	329 */
	{
		/* Erf/TS4 support */
		if (ArgList[1] && ArgList[2] && ArgList[3])
		{
			time_t tme1 = (time_t)my_atol(ArgList[1]);
			time_t tme2 = (time_t)my_atol(ArgList[2]);
			time_t tme3 = (time_t)my_atol(ArgList[3]);

			put_it("%s Channel %s was created at %ld, +c was last set at %ld, and has been opless since %ld", numeric_banner(), ArgList[0], tme1, tme2, tme3);
		}
		else if (ArgList[1])
		{
			time_t tme = (time_t)my_atol(ArgList[1]);

			put_it("%s Channel %s was created at %s",
					numeric_banner(),
				ArgList[0], my_ctime(tme));
		}
		break;
	}
	case 332:		/* #define RPL_TOPIC            332 */
	{
		put_it("%s Topic for %s: %s", numeric_banner(), ArgList[0], ArgList[1]);
		break;
	}

	case 333:		/* #define RPL_TOPICWHOTIME	333 */
	{
		/* Bug in aircd makes this check neccesary.  */
		if (ArgList[2])
		{
			time_t tme = (unsigned long)my_atol(ArgList[2]);
			put_it("%s The topic was set by %s %lu sec ago",numeric_banner(), 
				ArgList[1], (unsigned long)(time(NULL)-tme));
		}
		break;
	}

	case 341:		/* #define RPL_INVITING         341 */
	{
		if (!ArgList[0] || !ArgList[1])
			return;		/* Larneproofing */

		message_from(ArgList[1], LOG_CRAP);
		put_it("%s Inviting %s to channel %s", numeric_banner(), ArgList[0], ArgList[1]);
		break;
	}

	case 351:		/* #define RPL_VERSION          351 */
	{
		PasteArgs(ArgList, 2);
		put_it("%s Server %s: %s %s", numeric_banner(), ArgList[1],
			ArgList[0], ArgList[2]);
		break;
	}

	case 364:		/* #define RPL_LINKS            364 */
	{
		if (ArgList[2])
		{
			PasteArgs(ArgList, 2);
			put_it("%s %-20s %-20s %s", numeric_banner(),
				ArgList[0], ArgList[1], ArgList[2]);
		}
		else
		{
			PasteArgs(ArgList, 1);
			put_it("%s %-20s %s", numeric_banner(),
				ArgList[0], ArgList[1]);
		}
		break;
	}

	case 366:		/* #define RPL_ENDOFNAMES       366 */
	{
		if (get_int_var(SHOW_END_OF_MSGS_VAR))
		{
			if (!channel_is_syncing(ArgList[0], from_server))
				display_msg(from, ArgList);
		}
		break;
	}

	case 367:
	{
		if (!ArgList[0] || !ArgList[1])
			return;		/* Larneproofing */

		if (ArgList[2] && ArgList[3])
		{
			time_t tme = (time_t) strtoul(ArgList[3], NULL, 10);
			put_it("%s %s %-25s set by %-10s %lu sec ago", 
				numeric_banner(), ArgList[0],
				ArgList[1], ArgList[2], 
				(unsigned long)(time(NULL) - tme));
		}
		else
			put_it("%s %s %s",numeric_banner(), ArgList[0], ArgList[1]);
		break;
	}

	case 401:		/* #define ERR_NOSUCHNICK       401 */
	{
		if (!ArgList[0] || !ArgList[1])
			return;		/* Larneproofing */

		PasteArgs(ArgList, 1);
		put_it("%s %s: %s", numeric_banner(), ArgList[0], ArgList[1]);

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
		if (get_int_var(SHOW_END_OF_MSGS_VAR))
			display_msg(from, ArgList);
		break;
	}

	case 477:		/* #define ERR_NEEDREGGEDNICK	477 */
		/* IRCnet has a different 477 numeric. */
		if (ArgList[0] && *ArgList[0] == '+')
		{
			display_msg(from, ArgList);
			break;
		}
		/* FALLTHROUGH */
	case 471:		/* #define ERR_CHANNELISFULL    471 */
	case 473:		/* #define ERR_INVITEONLYCHAN   473 */
	case 474:		/* #define ERR_BANNEDFROMCHAN   474 */
	case 475: 		/* #define ERR_BADCHANNELKEY    475 */
	case 476:		/* #define ERR_BADCHANMASK      476 */
	{
		char *reason;

		PasteArgs(ArgList, 0);
		switch (comm)
		{
		    case 471:
			reason = "Channel is full";
			break;
		    case 473:
			reason = "You must be invited";
			break;
		    case 474:
			reason = "You are banned";
			break;
		    case 475:
			reason = "You must give the correct key";
			break;
		    case 476:
			reason = "Bad channel mask";
			break;
		    case 477:
			reason = "You must use a registered nickname";
			break;
		    default:
			reason = "Because the server said so";
			break;
		}	

		put_it("%s %s (%s)", numeric_banner(), ArgList[0], reason);
		break;
	}

	default:
		display_msg(from, ArgList);
	}

END:
	/*
	 * This is where we clean up after our numeric.  Numeric-specific
	 * cleanups can occur here, and then below we reset the display
	 * settings.
	 */
	switch (comm)
	{
	case 368:
		number_of_bans = 0;
		break;
	case 464:		/* #define ERR_PASSWDMISMATCH   464 */
	{
		char	server_num[8];

		if (oper_command)
			oper_command = 0;
		else
		{
			server_reconnects_to(from_server, NOSERV);
			say("Password required for connection to server %s",
				get_server_name(from_server));
			if (!dumb_mode)
			{
				strlcpy(server_num, ltoa(from_server), 8);
				add_wait_prompt("Server Password:", 
					password_sendline,
				       server_num, WAIT_PROMPT_LINE, 0);
			}
		}
	}
	}

	current_numeric = old_current_numeric;
	set_lastlog_msg_level(lastlog_level);
	message_from(NULL, LOG_CURRENT);
}


