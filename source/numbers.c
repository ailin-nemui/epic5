/*
 * numbers.c: handles all those strange numeric response dished out by that
 * wacky, nutty program we call ircd 
 *
 * written by michael sandrof
 *
 * copyright(c) 1990 
 *
 * see the copyright file, or do a help ircii copyright 
 */

#if 0
static	char	rcsid[] = "$Id: numbers.c,v 1.19 2002/02/04 05:37:44 jnelson Exp $";
#endif

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
void 	display_msg (char *from, char **ArgList)
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
void 	numbered_command (char *from, int comm, char **ArgList)
{
	char	*user;
	char	none_of_these = 0;
	int	flag,
		lastlog_level;
	int	old_current_numeric = current_numeric;

	/* There are no valid numerics that do not have arguments */
	if (!ArgList[0] || !ArgList[1])
		return;

	user = (*ArgList[0]) ? ArgList[0] : NULL;
	ArgList++;

	lastlog_level = set_lastlog_msg_level(LOG_CRAP);
	message_from(NULL, LOG_CRAP);

	current_numeric = -comm;	/* must be negative of numeric! */

	/*
	 * This first switch statement is only for those numerics which
	 * require special handling by the client.  Numerics which require
	 * no more than displaying a message are handled below and not here.
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
		accept_server_nickname(from_server, user);
		set_server_motd(from_server, 1);
		server_is_connected(from_server, 1);

		userhostbase(NULL, got_my_userhost, 1);
		PasteArgs(ArgList, 0);
		if (do_hook(current_numeric, "%s %s", from, *ArgList)) 
			display_msg(from, ArgList);
		break;
	}

	/* 
	 * Now instead of the terribly horrible hack using numeric 002
	 * to get the server name/server version info, we use the 004
	 * numeric which is what is the most logical choice for it.
	 */
	case 004:	/* #define RPL_MYINFO           004 */
	{
		got_initial_version_28(ArgList);
		PasteArgs(ArgList, 0);
		if (do_hook(current_numeric, "%s %s", from, *ArgList))
			display_msg(from, ArgList);
		break;
	}

	case 10:		/* EFNext "Use another server"	010 */
	{
		char *	new_server;
		int	new_port;
		int	old_server = from_server;

		if (!ArgList[0] || !ArgList[1] || !ArgList[2])
			break;		/* Not what i'm expecting */

		new_server = ArgList[0];
		new_port = atoi(ArgList[1]);
		PasteArgs(ArgList, 2);

		/* Must do these things before calling "display_msg" */
		add_to_server_list(new_server, new_port, NULL, NULL, NULL, 0);
		server_reconnects_to(old_server, from_server);
		from_server = old_server;

		if (do_hook(current_numeric, "%s %s %d %s", from, 
				new_server, new_port, ArgList[2]))
			display_msg(from, ArgList);
		break;
	}

	case 14:		/* Erf/TS4 "cookie" numeric	014 */
		set_server_cookie(from_server, ArgList[0]);
		break;

	/* XXX Doesn't belong here */
	case 301:		/* #define RPL_AWAY             301 */
        {
                PasteArgs(ArgList, 1);
                if (do_hook(current_numeric, "%s %s", ArgList[0], ArgList[1]))
                        put_it("%s %s is away: %s", numeric_banner(),
                                ArgList[0], ArgList[1]);
                break;
	}

	case 302:		/* #define RPL_USERHOST         302 */
		userhost_returned(from, ArgList);
		break;

	case 303:		/* #define RPL_ISON             303 */
		ison_returned(from, ArgList);
		break;

	/* XXX Doesn't belong here */
	case 311:		/* #define RPL_WHOISUSER        311 */
        {
                PasteArgs(ArgList, 4);
                message_from(NULL, LOG_CRAP);
                if (do_hook(current_numeric, "%s %s %s %s %s %s",
                                from, ArgList[0], ArgList[1], ArgList[2],
                                ArgList[3], ArgList[4]))
                        put_it("%s %s is %s@%s (%s)", numeric_banner(),
                                ArgList[0], ArgList[1], ArgList[2], ArgList[4]);
                break;
        }

	/* XXX Doesn't belong here */
	case 312:		/* #define RPL_WHOISSERVER      312 */
	{
		if (do_hook(current_numeric, "%s %s %s %s", from, ArgList[0], ArgList[1], ArgList[2]))
			put_it("%s on irc via server %s (%s)", numeric_banner(),
				ArgList[1], ArgList[2]);
		break;
	}

	/* XXX Doesn't belong here */
	case 313:		/* #define RPL_WHOISOPERATOR    313 */
	{
		PasteArgs(ArgList, 1);
		if (do_hook(current_numeric, "%s %s %s", from, ArgList[0], ArgList[1]))
			put_it("%s %s %s", numeric_banner(), ArgList[0], ArgList[1]);
		break;
	}

	/* XXX Doesn't belong here */
	case 314:		/* #define RPL_WHOWASUSER       314 */
	{
		PasteArgs(ArgList, 4);
		message_from(NULL, LOG_CRAP);
		if (do_hook(current_numeric, "%s %s %s %s %s %s",
				from, ArgList[0], ArgList[1], ArgList[2],
				ArgList[3], ArgList[4]))
			put_it("%s %s was %s@%s (%s)", numeric_banner(),
				ArgList[0], ArgList[1], ArgList[2], ArgList[4]);
		break;
	}

	case 315:		/* #define RPL_ENDOFWHO         315 */
	{
		PasteArgs(ArgList, 0);
		who_end(from, ArgList);
		break;
	}

#if 0
	/* 
	 * At the specific request of Kev, don't just eat this, but
	 * treat it as Just Another Numeric.
	 */
	case 316:		/* supported, but deprecated */
		break;
#endif

	/* XXX Doesn't belong here */
	case 317:		/* #define RPL_WHOISIDLE        317 */
	{
		char	flag, *nick, *idle_str, *startup_str;
		int	idle;
		time_t	startup;

		nick = ArgList[0];
		idle_str = ArgList[1];
		startup_str = ArgList[2];

		if (ArgList[3])	/* undernet */
		{
			PasteArgs(ArgList, 3);
			if (nick && idle_str && do_hook(current_numeric, "%s %s %s %s %s",
						from, nick, idle_str, startup_str, ArgList[3]))
			{
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
		}
		else	/* efnet */
		{
			PasteArgs(ArgList, 1);
			if (nick && idle_str && 
			    do_hook(current_numeric, "%s %s %s", from, nick, idle_str))
			{
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
		}

		break;
	}

	/* XXX Doesn't belong here */
	case 318:		/* #define RPL_ENDOFWHOIS       318 */
        {
                PasteArgs(ArgList, 0);
                if (do_hook(current_numeric, "%s %s", from, ArgList[0]))
                        if (get_int_var(SHOW_END_OF_MSGS_VAR))
                                display_msg(from, ArgList);
                break;
        }

	/* XXX Doesn't belong here */
	case 319:		/* #define RPL_WHOISCHANNELS    319 */
	{
		PasteArgs(ArgList, 1);
		if (do_hook(current_numeric, "%s %s %s", from, ArgList[0], ArgList[1]))
			put_it("%s on channels: %s", numeric_banner(), ArgList[1]);
		break;
	}

	case 321:		/* #define RPL_LISTSTART        321 */
	{
		ArgList[0] = "Channel\0Users\0Topic";
		ArgList[1] = ArgList[0] + 8;
		ArgList[2] = ArgList[1] + 6;
		ArgList[3] = (char *) 0;
		funny_list(from, ArgList);
		break;
	}

	case 322:		/* #define RPL_LIST             322 */
		funny_list(from, ArgList);
		break;

	case 324:		/* #define RPL_CHANNELMODEIS    324 */
		funny_mode(from, ArgList);
		break;

#if 0
	/* 
	 * At the specific request of Kev, I am suspending support 
	 * for this aircd feature, as it has been re-used by one of the
	 * big four.  XXX - It didn't belong here anyways.
	 */
	case 340:		/* #define RPL_INVITING_OTHER	340 */
	{
		if (ArgList[2])
		{
			message_from(ArgList[0], LOG_CRAP);
			if (do_hook(current_numeric, "%s %s %s %s", from, ArgList[0], ArgList[1], ArgList[2]))
				put_it("%s %s has invited %s to channel %s", numeric_banner(), ArgList[0], ArgList[1], ArgList[2]);
		}
		break;
	}
#endif

	/* XXX Doesn't belong here */
	case 341:		/* #define RPL_INVITING         341 */
	{
		if (ArgList[1])
		{
			message_from(ArgList[1], LOG_CRAP);
			if (do_hook(current_numeric, "%s %s %s", from, ArgList[0], ArgList[1]))
				put_it("%s Inviting %s to channel %s", numeric_banner(), ArgList[0], ArgList[1]);
		}
		break;
	}


	case 352:		/* #define RPL_WHOREPLY         352 */
		whoreply((char *) 0, ArgList);
		break;

	case 353:		/* #define RPL_NAMREPLY         353 */
		funny_namreply(from, ArgList);
		break;

	case 354:		/* #define RPL_XWHOREPLY	354 */
		xwhoreply(NULL, ArgList);
		break;

	/* XXX Doesn't belong here */
	case 366:		/* #define RPL_ENDOFNAMES       366 */
	{
		int	flag = 1;

		PasteArgs(ArgList, 1);
		message_from(ArgList[0], LOG_CRAP);
		if (get_int_var(SHOW_END_OF_MSGS_VAR))
			flag = do_hook(current_numeric, "%s %s %s", from, ArgList[0], ArgList[1]);
		message_from(NULL, LOG_CURRENT);
	
		if (!channel_is_syncing(ArgList[0], from_server))
		{
			PasteArgs(ArgList, 0);
			if (get_int_var(SHOW_END_OF_MSGS_VAR) && flag)
				display_msg(from, ArgList);
		}

		break;
	}

	case 367:		/* #define RPL_BANLIST */
	{
		number_of_bans++;
		if (ArgList[2])
		{
			time_t tme = (time_t) strtoul(ArgList[3], NULL, 10);
			if (do_hook(current_numeric, "%s %s %s %s %s", 
				from, ArgList[0], ArgList[1], ArgList[2], ArgList[3]))
			put_it("%s %s %-25s set by %-10s %lu sec ago", 
				numeric_banner(), ArgList[0],
				ArgList[1], ArgList[2], 
				(unsigned long)(time(NULL) - tme));
		}
		else
			if (do_hook(current_numeric, "%s %s %s", from, ArgList[0], ArgList[1]))
				put_it("%s %s %s",numeric_banner(), ArgList[0], ArgList[1]);
		break;
	}
	case 368:		/* #define END_OF_BANLIST */
	{
		if (get_int_var(SHOW_END_OF_MSGS_VAR))
		{
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
		}
		number_of_bans = 0;
		break;
	}

	/* 
	 * I put these here so that if you have SUPPRESS_SERVER_MOTD set
	 * to on, then you cant hook 372, 375, 376, as advertised.
	 */
	case 372:		/* #define RPL_MOTD             372 */
	case 377:		/* #define FORCE_RPL_MOTD	377 */
	{
		if (!get_int_var(SUPPRESS_SERVER_MOTD_VAR) ||
		    !get_server_motd(from_server))
		{
			PasteArgs(ArgList, 0);
			if (do_hook(current_numeric, "%s %s", from, ArgList[0]))
				put_it("%s %s", numeric_banner(), ArgList[0]);
		}
		break;
	}
	case 375:		/* #define RPL_MOTDSTART        375 */
	{
		if (!get_int_var(SUPPRESS_SERVER_MOTD_VAR) ||
		    !get_server_motd(from_server))
		{
			PasteArgs(ArgList, 0);
			if (do_hook(current_numeric, "%s %s", from, ArgList[0]))
				put_it("%s %s", numeric_banner(), ArgList[0]);
		}
		break;
	}
	case 376:		/* #define RPL_ENDOFMOTD        376 */
	{
		if (get_int_var(SHOW_END_OF_MSGS_VAR) &&
		    (!get_int_var(SUPPRESS_SERVER_MOTD_VAR) ||
		    !get_server_motd(from_server)))
		{
			PasteArgs(ArgList, 0);
			if (do_hook(current_numeric, "%s %s", from, ArgList[0]))
				put_it("%s %s", numeric_banner(), ArgList[0]);
		}
		set_server_motd(from_server, 0);
		break;
	}

	/* XXX Shouldn't this set "You're operator" flag for hybrid? */
	case 381: 		/* #define RPL_YOUREOPER        381 */
		PasteArgs(ArgList, 0);
		if (!is_server_connected(from_server))
			say("Odd server stuff from %s: %s", from, ArgList[0]);
		else if (do_hook(current_numeric, "%s %s", from, ArgList[0]))
			display_msg(from, ArgList);
		break;

        /* ":%s 401 %s %s :No such nick/channel" */
	case 401:		/* #define ERR_NOSUCHNICK       401 */
	{
		PasteArgs(ArgList, 1);
		if (do_hook(current_numeric, "%s %s %s", from, 
						ArgList[0], ArgList[1]))
			put_it("%s %s: %s", numeric_banner(), 
						ArgList[0], ArgList[1]);
		notify_mark(ArgList[0], 0, 0);

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
	{
		PasteArgs(ArgList, 1);
		/* 
		 * Some servers BBC by sending this instead of
		 * a 315 numeric when a who request has been completed.
		 */
		fake_who_end(from, ArgList[0]);

		if (do_hook(current_numeric, "%s %s %s", from, 
					ArgList[0], ArgList[1]))
			display_msg(from, ArgList);
		break;
	}

	/* Yet more bizarre dalnet extended who replies. */
	/* ":%s 522 %s :/WHO Syntax incorrect, use /who ? for help" */
        /* ":%s 523 %s :Error, /who limit of %d exceed." */
	case 522:
	case 523:
	{
		PasteArgs(ArgList, 0);
		/* 
		 * This dalnet error message doesn't even give us the
		 * courtesy of telling us which who request was in error,
		 * so we have to guess.  Whee.
		 */
		fake_who_end(from, NULL);

		if (do_hook(current_numeric, "%s %s", from, ArgList[0]))
			display_msg(from, ArgList);
		break;
	}

	case 403:		/* #define ERR_NOSUCHCHANNEL    403 */
	{
		const char	*s;

		PasteArgs(ArgList, 1);

		/* Do not accept 403's from remote servers. */
		s = get_server_itsname(from_server);
		if (my_strnicmp(s, from, strlen(s)))
			break;

		/* Some servers BBC and send back an empty reply. */
		if (!ArgList[0])
		{
			if (do_hook(current_numeric, "%s *", from))
				put_it("%s You did not specify a channel", 
					numeric_banner());
			break;
		}

		/* 
		 * Some servers BBC and send this instead of a
		 * 315 numeric when a who request has been completed.
		 */
		if (fake_who_end(from, ArgList[0]))
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
		else if (strcmp(ArgList[0], "*"))
			remove_channel(ArgList[0], from_server);

		if (do_hook(current_numeric, "%s %s %s", from, 
					ArgList[0], ArgList[1]))
			display_msg(from, ArgList);

		break;
	}

	case 421:		/* #define ERR_UNKNOWNCOMMAND   421 */
	{
		if (check_server_redirect(ArgList[0]))
			break;
		if (check_wait_command(ArgList[0]))
			break;

		PasteArgs(ArgList, 0);
		if (do_hook(current_numeric, "%s %s", from, ArgList[0]))
			display_msg(from, ArgList);
		break;
	}
	case 432:		/* #define ERR_ERRONEUSNICKNAME 432 */
	{
		if (!my_stricmp(user, ArgList[0]))
			yell("WARNING:  Strange invalid nick message received.  You are probably lagged.");
		else if (get_int_var(AUTO_NEW_NICK_VAR))
			fudge_nickname(from_server);
		else
			reset_nickname(from_server);

		PasteArgs(ArgList, 0);
		if (do_hook(current_numeric, "%s %s", from, *ArgList))
			display_msg(from, ArgList);

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
			accept_server_nickname(from_server, user);
			if (do_hook(current_numeric, "%s %s", from, ArgList[0]))
				display_msg(from, ArgList);
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

		    if (do_hook(current_numeric, "%s %s", from, ArgList[0]))
			display_msg(from, ArgList);
		    break;
		}

		/*
		 * Otherwise, a nick command failed.  Oh boy.
		 * If we are registered, abort the nick change and
		 * hope for the best.
		 */
		if (is_server_connected(from_server))
		{
			accept_server_nickname(from_server, user);
			if (do_hook(current_numeric, "%s %s", from, ArgList[0]))
				display_msg(from, ArgList);
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
		if (!my_stricmp(user, ArgList[0]))
			/* This should stop the "rolling nicks" in their tracks. */
			yell("WARNING:  Strange invalid nick message received.  You are probably lagged.");
		else if (get_int_var(AUTO_NEW_NICK_VAR))
			fudge_nickname(from_server);
		else
			reset_nickname(from_server);

		PasteArgs(ArgList, 0);
		if (never_connected &&
			do_hook(current_numeric, "-1 %s", *ArgList))
				display_msg(from, ArgList);
		else if (!never_connected && 
			do_hook(current_numeric, "%s %s", from, *ArgList))
				display_msg(from, ArgList);

		break;
	}

	case 439:		/* Comstud's "Can't change nickname" */
	{
		accept_server_nickname(from_server, user);
		if (do_hook(current_numeric, "%s %s", from, ArgList[0]))
			display_msg(from, ArgList);
		break;
	}

	case 442:		/* #define ERR_NOTONCHANNEL	442 */
	{
		const char *	s;
		if (!ArgList[0])
			break;

		PasteArgs(ArgList, 0);
		s = get_server_itsname(from_server);
		if (!my_strnicmp(s, from, strlen(s)))
		{
			if (strcmp(ArgList[0], "*"))
			    remove_channel(ArgList[0], from_server);
		}

		/* Why wasn't this offered before? */
		if (do_hook(current_numeric, "%s %s", from, *ArgList))
			display_msg(from, ArgList);
		break;
	}

	case 451:		/* #define ERR_NOTREGISTERED    451 */
	/*
	 * Sometimes the server doesn't catch the USER line, so
	 * here we send a simplified version again  -lynx 
	 */
		register_server(from_server, NULL);

		PasteArgs(ArgList, 0);
		if (do_hook(current_numeric, "%s %s", from, *ArgList))
			display_msg(from, ArgList);
		break;


	case 462:		/* #define ERR_ALREADYREGISTRED 462 */
	{
		change_server_nickname(from_server, NULL);

		PasteArgs(ArgList, 0);
		if (do_hook(current_numeric, "%s %s", from, *ArgList))
			display_msg(from, ArgList);
		break;
	}

	case 464:		/* #define ERR_PASSWDMISMATCH   464 */
	{
		PasteArgs(ArgList, 0);
		flag = do_hook(current_numeric, "%s %s", from, ArgList[0]);

		if (oper_command)
		{
			if (flag)
				display_msg(from, ArgList);
			oper_command = 0;
		}
		else
		{
			char	server_num[8];

			server_reconnects_to(from_server, -1);
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
		break;
	}

	case 465:		/* #define ERR_YOUREBANNEDCREEP 465 */
	{
		PasteArgs(ArgList, 0);
		/* 
		 * There used to be a say() here, but if we arent 
		 * connected to a server, then doing say() is not
		 * a good idea.  So now it just doesnt do anything.
		 */
		server_reconnects_to(from_server, -1);
		if (do_hook(current_numeric, "%s %s", from, ArgList[0]))
			display_msg(from, ArgList);
		break;
	}

	/* XXX These do not belong here */
	case 219:		/* #define RPL_ENDOFSTATS       219 */
	case 232:		/* #define RPL_ENDOFSERVICES    232 */
	case 365:		/* #define RPL_ENDOFLINKS       365 */
	case 369:		/* #define RPL_ENDOFWHOWAS      369 */
	case 374:		/* #define RPL_ENDOFINFO        374 */
	case 394:		/* #define RPL_ENDOFUSERS       394 */
	{
		int hook;

		PasteArgs(ArgList, 0);
		hook = do_hook(current_numeric, "%s %s", from, *ArgList);
		if (hook && get_int_var(SHOW_END_OF_MSGS_VAR))
			display_msg(from, ArgList);
		break;
	}

	case 471:		/* #define ERR_CHANNELISFULL    471 */
	case 473:		/* #define ERR_INVITEONLYCHAN   473 */
	case 474:		/* #define ERR_BANNEDFROMCHAN   474 */
	case 475: 		/* #define ERR_BADCHANNELKEY    475 */
	case 476:		/* #define ERR_BADCHANMASK      476 */
	case 477:		/* #define ERR_NEEDREGGEDNICK	477 */
	{
		char *reason;

		/* ircnet has a different 477 numeric. */
		if (comm == 477 && ArgList[0] && *ArgList[0] == '+')
		{
			PasteArgs(ArgList, 0);
			if (do_hook(current_numeric, "%s %s", from, ArgList[0]))
				display_msg(from, ArgList);
			break;
		}

		if (ArgList[0])
			cant_join_channel(ArgList[0], from_server);

		PasteArgs(ArgList, 0);
		switch(comm)
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

		if (do_hook(current_numeric, "%s %s", from, ArgList[0]))
			put_it("%s %s (%s)", numeric_banner(), ArgList[0], reason);
		break;
	}


	/*
	 * The following accumulates the remaining arguments
	 * in ArgSpace for hook detection. We can't use
	 * PasteArgs here because we still need the arguments
	 * separated for use elsewhere.
	 */
	default:
	{
		char	*ArgSpace = (char *) 0;
		int	i, len, do_message_from = 0;

		for (i = len = 0; ArgList[i]; len += strlen(ArgList[i++]))
			;
		len += (i - 1);
		ArgSpace = new_malloc(len + 1);
		ArgSpace[0] = '\0';
		/* this is cheating */
		if (ArgList[0] && is_channel(ArgList[0]))
		       do_message_from = 1;
		for (i = 0; ArgList[i]; i++)
		{
			if (i)
				strcat(ArgSpace, " ");
			strcat(ArgSpace, ArgList[i]);
		}
		if (do_message_from)
			message_from (ArgList[0], LOG_CRAP);
		if (!do_hook(current_numeric, "%s %s", from, ArgSpace))
		{
			new_free(&ArgSpace);
			if (do_message_from)
				set_lastlog_msg_level(lastlog_level);
			break;
		}
		if (do_message_from)
			message_from((char *) 0, lastlog_level);
		new_free(&ArgSpace);
		none_of_these = 1;
	} /* end of default case */
	} /* end of switch */

	/* the following do not hurt the ircII if intercepted by a hook */
	if (none_of_these)
	{
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

		case 329:		/* #define CREATION_TIME	329 */
		{
			/* Erf/TS4 support */
			if (ArgList[3])
			{
				time_t tme1 = (time_t)my_atol(ArgList[1]);
				time_t tme2 = (time_t)my_atol(ArgList[2]);
				time_t tme3 = (time_t)my_atol(ArgList[3]);

				message_from(ArgList[0], LOG_CRAP);
				put_it("%s Channel %s was created at %ld, +c was last set at %ld, and has been opless since %ld", numeric_banner(), ArgList[0], tme1, tme2, tme3);
				message_from((char *) 0, LOG_CURRENT);
			}
			if (ArgList[1])
			{
				time_t tme = (time_t)my_atol(ArgList[1]);

				message_from(ArgList[0], LOG_CRAP);
				put_it("%s Channel %s was created at %s",
						numeric_banner(),
					ArgList[0], my_ctime(tme));
				message_from((char *) 0, LOG_CURRENT);
			}
			break;
		}
		case 332:		/* #define RPL_TOPIC            332 */
		{
			message_from(ArgList[0], LOG_CRAP);
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

		case 384:		/* #define RPL_MYPORTIS         384 */
		{
			PasteArgs(ArgList, 0);
			put_it("%s %s %s", numeric_banner(), ArgList[0], user);
			break;
		}

#define RPL_CLOSEEND         363
		case 323:               /* #define RPL_LISTEND          323 */
			funny_print_widelist();
			/* FALLTHROUGH */

		default:
			display_msg(from, ArgList);
		}
	}

	current_numeric = old_current_numeric;
	set_lastlog_msg_level(lastlog_level);
}


