/* $EPIC: parse.c,v 1.55 2003/12/17 09:25:30 jnelson Exp $ */
/*
 * parse.c: handles messages from the server.   Believe it or not.  I
 * certainly wouldn't if I were you. 
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright © 1997, 2003 EPIC Software Labs.
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
#include "server.h"
#include "names.h"
#include "vars.h"
#include "ctcp.h"
#include "hook.h"
#include "commands.h"
#include "ignore.h"
#include "lastlog.h"
#include "ircaux.h"
#include "sedcrypt.h"
#include "term.h"
#include "flood.h"
#include "window.h"
#include "screen.h"
#include "output.h"
#include "numbers.h"
#include "parse.h"
#include "notify.h"
#include "notice.h"
#include "timer.h"

#define STRING_CHANNEL 	'+'
#define MULTI_CHANNEL 	'#'
#define LOCAL_CHANNEL 	'&'
#define ID_CHANNEL	'!'

#define space 		' '	/* Taken from rfc 1459 */
#define	MAXPARA		15	/* Taken from rfc 1459 */

static	void	strip_modes (const char *, const char *, const char *);

/* User and host information from server 2.7 */
const char	*FromUserHost = empty_string;

/*
 * is_channel: determines if the argument is a channel.  If it's a number,
 * begins with MULTI_CHANNEL and has no '*', or STRING_CHANNEL, then its a
 * channel 
 */
int 	is_channel (const char *to)
{
	const	char	*chantypes;

	if (!to || !*to)
		return 0;

	chantypes = get_server_005(from_server, "CHANTYPES");
	if (chantypes && *chantypes)
		return (!!strchr(chantypes, *to));

	return ((*to == MULTI_CHANNEL) || (*to == STRING_CHANNEL) ||
		(*to == LOCAL_CHANNEL) || (*to == ID_CHANNEL));
}


const char *	PasteArgs (const char **arg_list, int paste_point)
{
	int	i;
	char	*ptr;

	/*
	 * Make sure there are enough args to parse...
	 */
	for (i = 0; i < paste_point; i++)
		if (!arg_list[i] || !*arg_list[i])
			return NULL;		/* Not enough args */

	for (i = paste_point; arg_list[i] && arg_list[i + 1]; i++)
	{
		ptr = (char *)
#ifdef HAVE_INTPTR_T
				(intptr_t)
#endif
					   arg_list[i];
		ptr[strlen(ptr)] = ' ';
	}
	arg_list[paste_point + 1] = NULL;
	return arg_list[paste_point];
}

/*
 * BreakArgs: breaks up the line from the server, in to where its from,
 * setting FromUserHost if it should be, and returns all the arguements
 * that are there.   Re-written by phone, dec 1992.
 *		     Re-written again by esl, april 1996.
 *
 * This doesnt strip out extraneous spaces any more.
 */
static void 	BreakArgs (char *Input, const char **Sender, const char **OutPut)
{
	int	ArgCount;

	/*
	 * Paranoia.  Clean out any bogus ptrs still left on OutPut...
	 */
	for (ArgCount = 0; ArgCount <= MAXPARA + 1; ArgCount++)
		OutPut[ArgCount] = NULL;
	ArgCount = 0;

	/*
	 * The RFC describes it fully, but in a short form, a line looks like:
	 * [:sender[!user@host]] COMMAND ARGUMENT [[:]ARGUMENT]{0..14}
	 */

	/*
	 * Look to see if the optional :sender is present.
	 */
	if (*Input == ':')
	{
		char	*fuh;

		fuh = ++Input;
		while (*Input && *Input != space)
			Input++;
		if (*Input == space)
			*Input++ = 0;

		/*
		 * Look to see if the optional !user@host is present.
		 */
		*Sender = fuh;
		while (*fuh && *fuh != '!')
			fuh++;
		if (*fuh == '!')
			*fuh++ = 0;
		FromUserHost = fuh;
	}
	/*
	 * No sender present.
	 */
	else
		*Sender = FromUserHost = empty_string;

	/*
	 * Now we go through the argument list...
	 */
	for (;;)
	{
		while (*Input && *Input == space)
			Input++;

		if (!*Input)
			break;

		if (*Input == ':')
		{
			OutPut[ArgCount++] = ++Input;
			break;
		}

		OutPut[ArgCount++] = Input;
		if (ArgCount > MAXPARA)
			break;

		while (*Input && *Input != space)
			Input++;
		if (*Input == space)
			*Input++ = 0;
	}
	OutPut[ArgCount] = NULL;
}

/* in response to a TOPIC message from the server */
static void	p_topic (const char *from, const char *comm, const char **ArgList)
{
	const char 	*high, *channel, *new_topic;
	int	l;

	if (!(channel = ArgList[0])) 
		{ rfc1459_odd(from, comm, ArgList); return; }
	if (!(new_topic = ArgList[1])) 
		{ rfc1459_odd(from, comm, ArgList); return; }

	switch (check_ignore_channel(from, FromUserHost, 
					channel, LEVEL_TOPIC))
	{
		case IGNORED:
			return;
		case HIGHLIGHTED:
			high = highlight_char;
			break;
		default:
			high = empty_string;
			break;
	}

	if (new_check_flooding(from, FromUserHost, channel, new_topic, LEVEL_TOPIC))
		return;

	l = message_from(channel, LEVEL_TOPIC);
	if (do_hook(TOPIC_LIST, "%s %s %s", from, channel, new_topic))
		say("%s has changed the topic on channel %s to %s",
			from, check_channel_type(channel), new_topic);
	pop_message_from(l);
}

static void	p_wallops (const char *from, const char *comm, const char **ArgList)
{
	const char 	*message;
	int 	server_wallop;
	const char	*high;
	int	l;

	if (!(message = ArgList[0]))
		{ rfc1459_odd(from, comm, ArgList); return; }

	server_wallop = strchr(from, '.') ? 1 : 0;

	/* Check for ignores... */
	switch (check_ignore(from, FromUserHost, LEVEL_WALLOP))
	{
		case (IGNORED):
			return;
		case (HIGHLIGHTED):
			high = highlight_char;
			break;
		default:
			high = empty_string;
			break;
	}

	/* Check for floods... servers are exempted from flood checks */
	if (!server_wallop && check_flooding(from, FromUserHost, 
						LEVEL_WALLOP, message))
		return;


	l = message_from(from, LEVEL_WALLOP);
	if (do_hook(WALLOP_LIST, "%s %c %s", 
				from, 
				server_wallop ? 'S' : '*', 
				message))
		put_it("%s!%s%s!%s %s", 
				high, from, 
				server_wallop ? empty_string : star, 
				high, message);
	pop_message_from(l);
}

static void	p_privmsg (const char *from, const char *comm, const char **ArgList)
{
	const char	*target, *message;
	int		hook_type,
			level;
	const char	*hook_format;
	const char	*flood_channel = NULL;
	const char	*high;
	int	l;

	PasteArgs(ArgList, 1);
	if (!(target = ArgList[0]))
		{ rfc1459_odd(from, comm, ArgList); return; }
	if (!(message = ArgList[1]))
		{ rfc1459_odd(from, comm, ArgList); return; }

	set_server_doing_privmsg(from_server, 1);
	sed = 0;

	/*
	 * Do ctcp's first, and if there's nothing left, then dont
	 * go to all the work below.  Plus, we dont set message_from
	 * until we know there's other stuff besides the ctcp in the
	 * message, which keeps things going to the wrong window.
	 */
	message = do_ctcp(from, target, (char *)
#ifdef HAVE_INTPTR_T
					(intptr_t)
#endif
						message);
	if (!*message) {
		set_server_doing_privmsg(from_server, 0);
		return;
	}
	/* ooops. cant just do is_channel(to) because of # walls... */
	if (is_channel(target) && im_on_channel(target, from_server))
	{
		level = LEVEL_PUBLIC;
		flood_channel = target;

		if (!is_channel_nomsgs(target, from_server) && 
				!is_on_channel(target, from)) {
			hook_type = PUBLIC_MSG_LIST;
			hook_format = "%s(%s/%s)%s %s";
		} else if (is_current_channel(target, from_server)) {
			hook_type = PUBLIC_LIST;
			hook_format = "%s<%s%.0s>%s %s";
		} else {
			hook_type = PUBLIC_OTHER_LIST;
			hook_format = "%s<%s:%s>%s %s";
		}
	}
	else if (!is_me(from_server, target))
	{
		level = LEVEL_WALL;
		flood_channel = NULL;

		hook_type = MSG_GROUP_LIST;
		hook_format = "%s<-%s:%s->%s %s";
	}
	else
	{
		level = LEVEL_MSG;
		flood_channel = NULL;

		hook_type = MSG_LIST;
		hook_format = NULL;	/* See below */
		target = NULL;		/* Target is the sender */
	}

	if (!target || !*target)
		target = from;		/* Target is actually sender here */

	switch (check_ignore_channel(from, FromUserHost, target, level))
	{
		case IGNORED:
			set_server_doing_privmsg(from_server, 0);
			return;
		case HIGHLIGHTED:
			high = highlight_char;
			break;
		default:
			high = empty_string;
			break;
	}

	/* Encrypted privmsgs are specifically exempted from flood control */
	if (sed)
	{
		int	do_return = 1;

		sed = 0;
		l = message_from(target, level);
		if (do_hook(ENCRYPTED_PRIVMSG_LIST, "%s %s %s", from, target,
				message))
			do_return = 0;
		pop_message_from(l);

		if (do_return) {
			set_server_doing_privmsg(from_server, 0);
			return;
		}
	}

	if (new_check_flooding(from, FromUserHost, flood_channel, 
				message, level)) {
		set_server_doing_privmsg(from_server, 0);
		return;
	}

	/* Control the "last public/private nickname" variables */
	if (hook_type == PUBLIC_LIST || 
	    hook_type == PUBLIC_MSG_LIST || 
	    hook_type == PUBLIC_OTHER_LIST)
		set_server_public_nick(from_server, from);
	else if (hook_type == MSG_LIST)
		set_server_recv_nick(from_server, from);

	/* Go ahead and throw it to the user */
	l = message_from(target, level);

	if (do_hook(GENERAL_PRIVMSG_LIST, "%s %s %s", from, target, message))
	{
	    if (hook_type == MSG_LIST)
	    {
		const char *away = get_server_away(NOSERV);

		if (do_hook(hook_type, "%s %s", from, message))
		{
		    if (away)
		    {
			time_t blah = time(NULL);
			put_it("%s*%s*%s %s <%.16s>", 
				high, from, high, message, 
				ctime(&blah));
		    }
		    else
			put_it("%s*%s*%s %s", high, from, high,message);
		}
	    }

	    else if (do_hook(hook_type, "%s %s %s", from, target, message))
		put_it(hook_format, high, from, check_channel_type(target), 
				high, message);
	}

	/* Clean up and go home. */
	pop_message_from(l);
	set_server_doing_privmsg(from_server, 0);

	/* Alas, this is not protected by protocol enforcement. :( */
	notify_mark(from_server, from, 1, 0);
}

static void	p_quit (const char *from, const char *comm, const char **ArgList)
{
	const char *	quit_message;
	int		one_prints = 1;
	const char *	chan;
	int		l;
	const char *	high;

	if (!(quit_message = ArgList[0]))
		{ rfc1459_odd(from, comm, ArgList); return; }

	/*
	 * Normally, we do not throw the user a hook until after we
	 * have taken care of administrative details.  But in this case,
	 * someone has QUIT but the user may want their user@host info
	 * so we cannot remove them from the channel until after we have
	 * thrown the hook.  That is the only reason this is out of order.
	 */
	switch (check_ignore(from, FromUserHost, LEVEL_QUIT))
	{
		case IGNORED:
			goto remove_quitter;
		case HIGHLIGHTED:
			high = highlight_char;
			break;
		default:
			high = empty_string;
			break;
	}

	if (check_flooding(from, FromUserHost, LEVEL_QUIT, quit_message))
		goto remove_quitter;

	for (chan = walk_channels(1, from); chan; chan = walk_channels(0, from))
	{
	    switch (check_ignore_channel(from, FromUserHost, chan, LEVEL_QUIT))
	    {
		case IGNORED:
			one_prints = 0;
			continue;
		case HIGHLIGHTED:
			high = highlight_char;
			break;
		default:
			high = empty_string;
			break;
	    }

	    l = message_from(chan, LEVEL_QUIT);
	    if (!do_hook(CHANNEL_SIGNOFF_LIST, "%s %s %s", chan, from, 
							quit_message))
		one_prints = 0;
	    pop_message_from(l);
	}

	if (one_prints)
	{
		l = message_from(what_channel(from), LEVEL_QUIT);
		if (do_hook(SIGNOFF_LIST, "%s %s", from, quit_message))
			say("Signoff: %s%s%s (%s)", 
				high, from, high, quit_message);
		pop_message_from(l);
	}

	/*
	 * This is purely ergonomic.  If the user is ignoring this person
	 * then if we tell the user that this person is offline as soon as
	 * we get the QUIT, this will leak to the user that the person was
	 * on the channel, thus defeating the ignore.  Best to just wait 
	 * until the top of the next minute.
	 */
	notify_mark(from_server, from, 0, 0);

remove_quitter:
	/* Send all data about this unperson to the memory hole. */
	remove_from_channel(NULL, from, from_server);
}

static void	p_pong (const char *from, const char *comm, const char **ArgList)
{
	const char *	pong_server, *pong_message;
	int	server_pong = 0;

	PasteArgs(ArgList, 1);
	if (!(pong_server = ArgList[0]))
		{ rfc1459_odd(from, comm, ArgList); return; }
	if (!(pong_message = ArgList[1]))
		{ rfc1459_odd(from, comm, ArgList); return; }

	if (strchr(pong_server, '.'))
		server_pong = 1;

	/*
	 * In theory, we could try to use PING/PONG messages to 
	 * do /redirect and /wait, but we don't.  When the day comes
	 * that we do, this code will do that for us.
	 */
	if (!my_stricmp(from, get_server_itsname(from_server)))
	{
		if (check_server_redirect(from_server, pong_message))
			return;
		if (check_server_wait(from_server, pong_message))
			return;
	}

	if (check_ignore(from, FromUserHost, LEVEL_CRAP) == IGNORED)
		return;

	if (do_hook(PONG_LIST, "%s %s %s", from, pong_server, pong_message))
	    if (server_pong)
		say("%s: PONG received from %s (%s)", pong_server, 
							from, pong_message);
}

static void	p_error (const char *from, const char *comm, const char **ArgList)
{
	const char *	the_error;

	PasteArgs(ArgList, 0);
	if (!(the_error = ArgList[0]))
		{ rfc1459_odd(from, comm, ArgList); return; }

	if (!from || !isgraph(*from))
		from = star;

	if (do_hook(ERROR_LIST, "%s %s", from, the_error))
		say("%s %s", from, the_error);
}

#if 0
static void	add_user_who (int refnum, const char *from, const char *comm, const char **ArgList)
{
	const char 	*channel, *user, *host, *server, *nick;
	size_t	size;
	char 	*userhost;

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

	/* Obviously this is safe. */
	size = strlen(user) + strlen(host) + 2;
	userhost = alloca(size);
	snprintf(userhost, size, "%s@%s", user, host);
	add_userhost_to_channel(channel, nick, refnum, userhost);
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
#endif

static void	p_channel (const char *from, const char *comm, const char **ArgList)
{
	const char	*channel;
	char 	*c;
	int 	op = 0, vo = 0, ha = 0;
	char 	extra[20];
	const char	*high;
	int	l;

	/* We cannot join channel 0 */
	if (!(channel = ArgList[0]))
		{ rfc1459_odd(from, comm, ArgList); return; }
	if (!strcmp(channel, zero))
		{ rfc1459_odd(from, comm, ArgList); return; }

	/*
	 * Workaround for extremely gratuitous protocol change in av2.9
	 */
	if ((c = strchr(channel, '\007')))
	{
		for (*c++ = 0; *c; c++)
		{
			     if (*c == 'o') op = 1;
			else if (*c == 'v') vo = 1;
		}
	}

	if (is_me(from_server, from))
	{
#if 0
		char *copy = LOCAL_COPY(channel);
#endif

		add_channel(channel, from_server);
		send_to_server("MODE %s", channel);
#if 0
		whobase(from_server, copy, add_user_who, add_user_end);
#endif
	}
	else
	{
		add_to_channel(channel, from, from_server, 0, op, vo, ha);
		add_userhost_to_channel(channel, from, from_server, FromUserHost);
	}

	switch (check_ignore_channel(from, FromUserHost, channel, LEVEL_JOIN))
	{
		case IGNORED:
			return;
		case HIGHLIGHTED:
			high = highlight_char;
			break;
		default:
			high = empty_string;
			break;
	}

	if (new_check_flooding(from, FromUserHost, channel, star, LEVEL_JOIN))
		return;

	set_server_joined_nick(from_server, from);

	*extra = 0;
	if (op)
		strlcat(extra, " (+o)", sizeof extra);
	if (vo)
		strlcat(extra, " (+v)", sizeof extra);

	l = message_from(channel, LEVEL_JOIN);
	if (do_hook(JOIN_LIST, "%s %s %s %s", 
			from, channel, FromUserHost, extra))
		say("%s%s%s (%s) has joined channel %s%s%s%s", 
			high, from, high, FromUserHost, 
			high, check_channel_type(channel), high, extra);
	pop_message_from(l);

	/*
	 * The placement of this is purely ergonomic.  The user might
	 * be alarmed if epic thrown an /on notify_signon before it
	 * throws the /on join that triggers it.  Plus, if the user is
	 * ignoring this person (nothing says you can't ignore someone
	 * who is on your notify list), then it would also not be the
	 * best idea to throw /on notify_signon as a result of an
	 * /on join since that would leak to the user that the person
	 * has joined the channel -- best to just leave the notify stuff
	 * alone until the top of the next minute.
	 */
	notify_mark(from_server, from, 1, 0);
}

static void 	p_invite (const char *from, const char *comm, const char **ArgList)
{
	const char	*invitee, *invited_to;
	const char	*high;
	int	l;

	if (!(invitee = ArgList[0]))
		{ rfc1459_odd(from, comm, ArgList); return; }
	if (!(invited_to = ArgList[1]))
		{ rfc1459_odd(from, comm, ArgList); return; }

	switch (check_ignore_channel(from, FromUserHost, 
					invited_to, LEVEL_INVITE))
	{
		case IGNORED:
			return;
		case HIGHLIGHTED:
			high = highlight_char;
			break;
		default:
			high = empty_string;
			break;
	}

	if (check_flooding(from, FromUserHost, LEVEL_INVITE, invited_to))
		return;

	set_server_invite_channel(from_server, invited_to);
	set_server_recv_nick(from_server, from);

	l = message_from(from, LEVEL_INVITE);
	if (do_hook(INVITE_LIST, "%s %s %s", from, invited_to, FromUserHost))
		say("%s%s (%s)%s invites you to channel %s", high,
			from, FromUserHost, high, invited_to);
	pop_message_from(l);
}

/* 
 * Reconnect has been put back in the "jon2 comprimise" 
 *
 * Unlimited autoreconnect for server kills
 * Limited autoreconnect for forground procis on oper kills
 *	(/set auto_reconnect ON)
 * No autoreconnect on oper kills for background procis
 *
 * /on disconnect is still always hooked no matter what.
 *
 * You may configure this any way you please.. I really dont give
 * a darn what autoconnect you use... but this is a fair default.
 *
 * NOTE: If you want to change it, you *will* have to do it yourself.
 * Im quite serious.  Im not going to change this.
 */
static void	p_kill (const char *from, const char *comm, const char **ArgList)
{
	const char 	*victim, *reason;
	int 	hooked;

	if (!(victim = ArgList[0]))
		{ rfc1459_odd(from, comm, ArgList); return; }
	if (!(reason = ArgList[1])) { }

	/* 
	 * Bogorific Microsoft Exchange ``IRC'' server sends out a KILL
	 * protocol message instead of a QUIT protocol message when
	 * someone is killed on your server.  Do the obviously appropriate
	 * thing and reroute this misdirected protocol message to 
	 * p_quit, where it should have been sent in the first place.
	 * Die Microsoft, Die.
	 */
	if (!is_me(from_server, victim))
	{
		/* I don't care if this doesn't work.  */
		p_quit(from, comm, ArgList);	/* Die Microsoft, Die */
		return;
	}


	/*
	 * If we've been killed by our server, we need take no action
	 * because when we are dropped by the server, we will take this as
	 * any other case where we recieve abnormal termination from the
	 * server and we will reconnect and rejoin.
	 */
	if (strchr(from, '.'))
        {
		if (!reason) { reason = "Probably due to a nick collision"; }

		say("Server [%s] has rejected you. (%s)", from, reason);
		return;
	}


	/*
	 * We've been killed.  Save our channels, close up shop on 
	 * this server.  We need to call window_check_servers before
	 * we do any output, in case we want the output to go anywhere
	 * meaningful.
	 */
	if (!reason) { reason = "No Reason Given"; }

	if ((hooked = do_hook(DISCONNECT_LIST, "Killed by %s (%s)",
						from, reason)))
	{
		say("You have been killed by that fascist [%s] %s", 
						from, reason);
	}

	/* 
	 * If we are a bot, and /on disconnect didnt hook, 
	 * then we arent going anywhere.  We might as well quit.
	 * Also quit if #define QUIT_ON_OPERATOR_KILL
	 */
#ifndef QUIT_ON_OPERATOR_KILL
	if (background && !hooked)
#endif
	{
		say("Too bad, you lose.");
		irc_exit(1, NULL);
	}

	if (!background && get_int_var(AUTO_RECONNECT_VAR))
	{
		char *	sc = new_malloc(16);

		snprintf(sc, 15, "%d", from_server);
		add_timer(0, empty_string, 
			  get_int_var(AUTO_RECONNECT_DELAY_VAR), 1,
			  auto_reconnect_callback,
			  sc, NULL, current_window->refnum);
	}

	/* Suppress auto-reconnect (to wait for above timer) */
	server_reconnects_to(from_server, NOSERV);
}

static void	p_ping (const char *from, const char *comm, const char **ArgList)
{
	const char *	message;

        PasteArgs(ArgList, 0);
	if (!(message = ArgList[0]))
		{ rfc1459_odd(from, comm, ArgList); return; }

	send_to_server("PONG %s", message);
}

static void	p_silence (const char *from, const char *comm, const char **ArgList)
{
	const char *target;
	char mag;

	if (!(target = ArgList[0]))
		{ rfc1459_odd(from, comm, ArgList); return; }

	mag = *target++;
	if (do_hook(SILENCE_LIST, "%c %s", mag, target))
	{
		if (mag == '+')
			say ("You will no longer receive msgs from %s", target);
		else if (mag == '-')
			say ("You will now recieve msgs from %s", target);
		else
			say ("Unrecognized silence argument: %s", target);
	}
}

static void	p_nick (const char *from, const char *comm, const char **ArgList)
{
	const char	*new_nick;
	int		been_hooked = 0,
			its_me = 0;
	const char	*chan;
	const char	*high;
	int		ignored = 0;
	int		l;

	if (!(new_nick = ArgList[0]))
		{ rfc1459_odd(from, comm, ArgList); return; }

	/*
	 * Is this me changing nick?
	 */
	if (is_me(from_server, from))
	{
		its_me = 1;
		accept_server_nickname(from_server, new_nick);
		set_server_nickname_pending(from_server, 0);
	}

	switch (check_ignore(from, FromUserHost, LEVEL_NICK))
	{
		case IGNORED:
			goto do_rename;
		case HIGHLIGHTED:
			high = highlight_char;
			break;
		default:
			high = empty_string;
			break;
	}

	if (check_flooding(from, FromUserHost, LEVEL_NICK, new_nick))
		goto do_rename;

	for (chan = walk_channels(1, from); chan; chan = walk_channels(0, from))
	{
		if (check_ignore_channel(from, FromUserHost, chan, 
						LEVEL_NICK) == IGNORED)
		{
			ignored = 1;
			continue;
		}

		l = message_from(chan, LEVEL_NICK);
		if (!do_hook(CHANNEL_NICK_LIST, "%s %s %s", chan, from, new_nick))
			been_hooked = 1;
		pop_message_from(l);
	}

	if (!been_hooked && !ignored)
	{
		if (its_me)
			l = message_from(NULL, LEVEL_NICK);
		else
			l = message_from(what_channel(from), LEVEL_NICK);

		if (do_hook(NICKNAME_LIST, "%s %s", from, new_nick))
			say("%s%s%s is now known as %s%s%s", 
				high, from, high, 
				high, new_nick, high);

		pop_message_from(l);
	}

do_rename:
	notify_mark(from_server, from, 0, 0);
	rename_nick(from, new_nick, from_server);
	notify_mark(from_server, new_nick, 1, 0);
}

static void	p_mode (const char *from, const char *comm, const char **ArgList)
{
	const char	*target, *changes;
	const char	*m_target;
	const char	*type;
	const char	*high;
	int		l;

	PasteArgs(ArgList, 1);
	if (!(target = ArgList[0]))
		{ rfc1459_odd(from, comm, ArgList); return; }
	if (!(changes = ArgList[1])) { return; } /* Ignore UnrealIRCD */
	if (!*changes) { return; }		 /* Ignore UnrealIRCD */

	if (get_int_var(MODE_STRIPPER_VAR))
		strip_modes(from, target, changes);

	if (is_channel(target))
	{
		m_target = target;
		target = check_channel_type(target);
		type = "on channel";
	}
	else
	{
		m_target = NULL;
		type = "for user";
	}

	switch (check_ignore_channel(from, FromUserHost, target, LEVEL_MODE))
	{
		case IGNORED:
			goto do_update_mode;
		case HIGHLIGHTED:
			high = highlight_char;
			break;
		default:
			high = empty_string;
			break;
	}

	if (check_flooding(from, FromUserHost, LEVEL_MODE, changes))
		goto do_update_mode;

	l = message_from(m_target, LEVEL_MODE);
	if (do_hook(MODE_LIST, "%s %s %s", from, target, changes))
	    say("Mode change \"%s\" %s %s%s%s by %s%s%s",
					changes, type, 
					high, target, high,
					high, from, high);
	pop_message_from(l);

do_update_mode:
	if (is_channel(target))
		update_channel_mode(target, changes);
	else
		update_user_mode(changes);
}

static void strip_modes (const char *from, const char *channel, const char *line)
{
	char	*mode;
	char 	*pointer;
	char	mag = '+'; /* XXXX Bogus */
        char    *copy = (char *) 0;
	char	*free_copy;
	int	l;

	free_copy = LOCAL_COPY(line);
	copy = free_copy;
	mode = next_arg(copy, &copy);

	if (is_channel(channel))
	{
	    l = message_from(channel, LEVEL_MODE);

	    for (pointer = mode; *pointer; pointer++)
	    {
		char	c = *pointer;
		char	*arg = NULL;

		switch (chanmodetype(c))
		{
			case 1:
				mag = c;
				continue;
			case 6:
				break;
			case 5:
				if (mag == '-')
					break;
			case 4: case 3: case 2:
				if ((arg = safe_new_next_arg(copy, &copy)))
					break;
			default:
				/* We already get a yell from decifer_mode() */
				break;
		}
		if (arg)
			do_hook(MODE_STRIPPED_LIST,
				"%s %s %c%c %s",
				from, channel, mag,
				c,arg);
		else
			do_hook(MODE_STRIPPED_LIST,
				"%s %s %c%c",
				from,channel,mag,c);
	    }

	    pop_message_from(l);
	}

	else /* User mode */
	{
	    l = message_from(NULL, LEVEL_MODE);

	    for (pointer = mode; *pointer; pointer++)
	    {
		char	c = *pointer;

		switch (c) 
		{
			case '+' :
			case '-' : mag = c; break;
#if 0	/* Not implemented yet */
			case 's' :
			{
				if (umode_s_takes_arg(from_server))
				{
					do_hook(MODE_STRIPPED_LIST, 
						"%s %s %c%c %s", 
						from, channel, mag, c, 
						next_arg(copy, &copy));
					break;
				}
				/* ELSE FALLTHROUGH */
			}
#endif
			default  : 
				do_hook(MODE_STRIPPED_LIST, 
					"%s %s %c%c", 
					from, channel, mag, c);
				break;
		}
	    }

	    pop_message_from(l);
	}

}

static void	p_kick (const char *from, const char *comm, const char **ArgList)
{
	const char	*channel, *victim, *comment;
	const char *	high;
	int	l;

	if (!(channel = ArgList[0]))
		{ rfc1459_odd(from, comm, ArgList); return; }
	if (!(victim = ArgList[1]))
		{ rfc1459_odd(from, comm, ArgList); return; }
	if (!(comment = ArgList[2])) { comment = "(no comment)"; }


	/*
	 * All this to handle being kicked...
	 */
	if (is_me(-1, victim))
	{
		Window *win, *old_tw, *old_cw;

		/*
		 * Uh-oh.  If win is null we have a problem.
		 */
		if (!(win = get_window_by_refnum(
				get_channel_winref(channel, from_server))))
		{
		    /*
		     * Check to see if we got a KICK for a 
		     * channel we dont think we're on.
		     */
		    if (im_on_channel(channel, from_server))
			panic("Window is NULL for channel [%s]", channel);

		    yell("You were KICKed by [%s] on channel [%s] "
			 "(reason [%s]), which you are not on!  "
			 "Will not try to auto-rejoin", 
				from, channel, comment);

		    return;
		}

		if (get_int_var(AUTO_REJOIN_VAR))
		{
			const char *key = get_channel_key(channel, from_server);
			if (!key)
				key = empty_string;

			add_timer(0, empty_string, 
				  get_int_var(AUTO_REJOIN_DELAY_VAR), 1,
				  auto_rejoin_callback, 
				  malloc_sprintf(NULL, "%s %d %d %s", channel, 
						from_server, 
						win->refnum,
						key),
				  NULL, win->refnum);
		}
		remove_channel(channel, from_server);
		update_all_status();

		old_tw = to_window;
		old_cw = current_window;
		current_window = win;
		to_window = win;
		l = message_from(channel, LEVEL_KICK);

		if (do_hook(KICK_LIST, "%s %s %s %s", victim, from, 
					check_channel_type(channel), comment))
			say("You have been kicked off channel %s by %s (%s)", 
					check_channel_type(channel), from, 
					comment);

		pop_message_from(l);
		to_window = old_tw;
		current_window = old_cw;
		return;
	}

	switch (check_ignore_channel(from, FromUserHost, channel, LEVEL_KICK))
	{
		case IGNORED:
			goto do_remove_nick;
		case HIGHLIGHTED:
			high = highlight_char;
			break;
		default:
			high = empty_string;
			break;
	}

	switch (check_ignore_channel(victim, fetch_userhost(from_server, victim), channel, LEVEL_KICK))
	{
		case IGNORED:
			goto do_remove_nick;
		case HIGHLIGHTED:
			high = highlight_char;
			break;
		default:
			high = empty_string;
			break;
	}


	if (check_flooding(from, FromUserHost, LEVEL_KICK, victim))
		goto do_remove_nick;

	l = message_from(channel, LEVEL_KICK);
	if (do_hook(KICK_LIST, "%s %s %s %s", 
			victim, from, channel, comment))
		say("%s%s%s has been kicked off channel %s by %s (%s)", 
			high, victim, high,
			check_channel_type(channel), 
			high, from, high, comment);
	pop_message_from(l);

do_remove_nick:
	/*
	 * The placement of this is purely ergonomic.  When someone is
	 * kicked, the user may want to know what their userhost was so 
	 * they can take whatever appropriate action is called for.  This
	 * requires that the user still be considered "on channel" in the
	 * /on kick, even though the user has departed.
	 *
	 * Send all data for this unperson to the memory hole.
	 */
	remove_from_channel(channel, victim, from_server);
}

static void	p_part (const char *from, const char *comm, const char **ArgList)
{
	const char	*channel, *reason;
	int	l;

	PasteArgs(ArgList, 1);
	if (!(channel = ArgList[0]))
		{ rfc1459_odd(from, comm, ArgList); return; }
	if (!(reason = ArgList[1])) { }

	if ((check_ignore_channel(from, FromUserHost, 
					channel, LEVEL_PART) != IGNORED))
	{
		l = message_from(channel, LEVEL_PART);
		if (reason)		/* Dalnet part messages */
		{
			if (do_hook(LEAVE_LIST, "%s %s %s %s", 
				from, channel, FromUserHost, reason))
			    say("%s has left channel %s because (%s)", 
				from, check_channel_type(channel), reason);
		}
		else
		{
			if (do_hook(LEAVE_LIST, "%s %s %s", 
				from, channel, FromUserHost))
			    say("%s has left channel %s", 
				from, check_channel_type(channel));
		}
		pop_message_from(l);
	}

	if (is_me(from_server, from))
		remove_channel(channel, from_server);
	else
		remove_from_channel(channel, from, from_server);
}

/*
 * Egads. i hope this is right.
 */
static void	p_rpong (const char *from, const char *comm, const char **ArgList)
{
	const char *	nick, *target_server, *millisecs, *orig_time;
	time_t delay;

	if (!(nick = ArgList[0]))
		{ rfc1459_odd(from, comm, ArgList); return; }
	if (!(target_server = ArgList[1]))
		{ rfc1459_odd(from, comm, ArgList); return; }
	if (!(millisecs = ArgList[2]))
		{ rfc1459_odd(from, comm, ArgList); return; }
	if (!(orig_time = ArgList[3]))
		{ rfc1459_odd(from, comm, ArgList); return; }

	/*
	 * :server RPONG yournick remoteserv ms :yourargs
	 *
	 * ArgList[0] -- our nickname (presumably)
	 * ArgList[1] -- The server we RPING'd
	 * ArgList[2] -- The number of ms it took to return
	 * ArgList[3] -- The arguments we passed (presumably)
	 */

	delay = time(NULL) - atol(orig_time);
	say("Pingtime %s - %s : %s ms (total delay: %ld s)",
		from, target_server, millisecs, delay);
}


void	rfc1459_odd (const char *from, const char *comm, const char **ArgList)
{
	const char *	stuff;

	PasteArgs(ArgList, 0);
	if (!(stuff = ArgList[0]))
		stuff = empty_string;

	if (!from || !*from)
		from = "*";
	if (!comm || !*comm)
		comm = "*";

	if (do_hook(ODD_SERVER_STUFF_LIST, "%s %s %s", from, comm, stuff))
		say("Odd server stuff: \"%s %s\" (%s)", comm, stuff, from);
}

protocol_command rfc1459[] = {
{	"ADMIN",	NULL,		NULL,		0		},
{	"AWAY",		NULL,		NULL,		0		},
{ 	"CONNECT",	NULL,		NULL,		0		},
{	"ERROR",	p_error,	NULL,		0		},
{	"ERROR:",	p_error,	NULL,		0		},
{	"INVITE",	p_invite,	NULL,		0		},
{	"INFO",		NULL,		NULL,		0		},
{	"ISON",		NULL,		NULL,		PROTO_NOQUOTE	},
{	"JOIN",		p_channel,	NULL,		0		},
{	"KICK",		p_kick,		NULL,		0		},
{	"KILL",		p_kill,		NULL,		0		},
{	"LINKS",	NULL,		NULL,		0		},
{	"LIST",		NULL,		NULL,		0		},
{	"MODE",		p_mode,		NULL,		0		},
{	"NAMES",	NULL,		NULL,		0		},
{	"NICK",		p_nick,		NULL,		PROTO_NOQUOTE	},
{	"NOTICE",	p_notice,	NULL,		0		},
{	"OPER",		NULL,		NULL,		0		},
{	"PART",		p_part,		NULL,		0		},
{	"PASS",		NULL,		NULL,		0 		},
{	"PING",		p_ping,		NULL,		0		},
{	"PONG",		p_pong,		NULL,		0		},
{	"PRIVMSG",	p_privmsg,	NULL,		0		},
{	"QUIT",		p_quit,		NULL,		PROTO_DEPREC	},
{	"REHASH",	NULL,		NULL,		0		},
{	"RESTART",	NULL,		NULL,		0		},
{	"RPONG",	p_rpong,	NULL,		0		},
{	"SERVER",	NULL,		NULL,		PROTO_NOQUOTE	},
{	"SILENCE",	p_silence,	NULL,		0		},
{	"SQUIT",	NULL,		NULL,		0		},
{	"STATS",	NULL,		NULL,		0		},
{	"SUMMON",	NULL,		NULL,		0		},
{	"TIME",		NULL,		NULL,		0		},
{	"TRACE",	NULL,		NULL,		0		},
{	"TOPIC",	p_topic,	NULL,		0		},
{	"USER",		NULL,		NULL,		0		},
{	"USERHOST",	NULL,		NULL,		PROTO_NOQUOTE	},
{	"USERS",	NULL,		NULL,		0		},
{	"VERSION",	NULL,		NULL,		0		},
{	"WALLOPS",	p_wallops,	NULL,		0		},
{	"WHO",		NULL,		NULL,		PROTO_DEPREC	},
{	"WHOIS",	NULL,		NULL,		0		},
{	"WHOWAS",	NULL,		NULL,		0		},
{	NULL,		NULL,		NULL,		0		}
};
#define NUMBER_OF_COMMANDS (sizeof(rfc1459) / sizeof(protocol_command)) - 2;
int 	num_protocol_cmds = -1;

/*
 * parse_server: parses messages from the server, doing what should be done
 * with them 
 */
void 	parse_server (const char *orig_line, size_t orig_line_size)
{
	const char	*from;
	const char	*comm;
	const char	**ArgList;
	const char	*TrueArgs[MAXPARA + 2];	/* Include space for command */
	protocol_command *retval;
	int	loc;
	int	cnt;
	char	*line;
	size_t	size;

	if (num_protocol_cmds == -1)
		num_protocol_cmds = NUMBER_OF_COMMANDS;

	if (!orig_line || !*orig_line)
		return;		/* empty line from server -- bye bye */

	if (*orig_line == ':')
	{
		if (!do_hook(RAW_IRC_LIST, "%s", orig_line + 1))
			return;
	}
	else if (!do_hook(RAW_IRC_LIST, "* %s", orig_line))
		return;

	size = (orig_line_size + 1) * 11;
	line = alloca(size + 1);
	strlcpy(line, orig_line, orig_line_size);
	if (inbound_line_mangler)
	{
	    if (mangle_line(line, inbound_line_mangler, orig_line_size) > orig_line_size)
		yell("mangle_line truncated its result.  Ack.");
	}

	ArgList = TrueArgs;
	BreakArgs(line, &from, ArgList);

	if ((!(comm = *ArgList++)) || !from || !*ArgList)
	{ 
		rfc1459_odd(from, comm, ArgList);
		return;		/* Serious protocol violation -- ByeBye */
	}

	/* 
	 * I reformatted these in may '96 by using the output of /stats m
	 * from a few busy servers.  They are arranged so that the most 
	 * common types are high on the list (to save the average number
	 * of compares.)  I will be doing more testing in the future on
	 * a live client to see if this is a reasonable order.
	 */
	if (is_number(comm))
		numbered_command(from, comm, ArgList);
	else
	{
		retval = (protocol_command *)find_fixed_array_item(
			(void *)rfc1459, sizeof(protocol_command), 
			num_protocol_cmds + 1, comm, &cnt, &loc);

		if (cnt < 0 && rfc1459[loc].inbound_handler)
			rfc1459[loc].inbound_handler(from, comm, ArgList);
		else
			rfc1459_odd(from, comm, ArgList);
	}

	FromUserHost = empty_string;
	from_server = -1;
}
