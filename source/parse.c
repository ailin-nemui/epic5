/*
 * parse.c: handles messages from the server.   Believe it or not.  I
 * certainly wouldn't if I were you. 
 *
 * Copyright 1990 Michael Sandrof
 * Copyright 1993 Matthew Green
 * Copyright 1997 EPIC Software Labs
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
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
#include "funny.h"
#include "crypt.h"
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

static  void    strip_modes (char *, char *, char *);

/*
 * joined_nick: the nickname of the last person who joined the current
 * channel 
 */
	char	*joined_nick = (char *) 0;

/* public_nick: nick of the last person to send a message to your channel */
	char	*public_nick = (char *) 0;

/* User and host information from server 2.7 */
	char	*FromUserHost = empty_string;

/* doing a PRIVMSG */
	int	doing_privmsg = 0;

/* fake: alerts the user. */
void 	fake (void)
{
	yell("-- Fake Message receieved -- ");
	return;
}

/*
 * is_channel: determines if the argument is a channel.  If it's a number,
 * begins with MULTI_CHANNEL and has no '*', or STRING_CHANNEL, then its a
 * channel 
 */
int 	is_channel(const char *to)
{
	return ( (to) && (     (*to == MULTI_CHANNEL) 
			    || (*to == STRING_CHANNEL)
		            || (*to == LOCAL_CHANNEL) 
			    || (*to == ID_CHANNEL)));
}


char *	PasteArgs (char **arg_list, int paste_point)
{
	int	i;

	/*
	 * Make sure there are enough args to parse...
	 */
	for (i = 0; i < paste_point; i++)
		if (!arg_list[i])
			return NULL;		/* Not enough args */

	for (i = paste_point; arg_list[i] && arg_list[i + 1]; i++)
		arg_list[i][strlen(arg_list[i])] = ' ';
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
void 	BreakArgs (char *Input, char **Sender, char **OutPut)
{
	int	ArgCount;
	char	*fuh;

	/*
	 * Paranoia.  Clean out any bogus ptrs still left on OutPut...
	 */
	for (ArgCount = 0; ArgCount < MAXPARA; ArgCount++)
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
		*Sender = ++Input;
		while (*Input && *Input != space)
			Input++;
		if (*Input == space)
			*Input++ = 0;

		/*
		 * Look to see if the optional !user@host is present.
		 */
		fuh = *Sender;
		while (*fuh && *fuh != '!')
			fuh++;
		if (*fuh == '!')
			*fuh++ = 0;
	}
	/*
	 * No sender present.
	 */
	else
		*Sender = fuh = empty_string;

	FromUserHost = fuh;

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
		if (ArgCount >= MAXPARA)
			break;

		while (*Input && *Input != space)
			Input++;
		if (*Input == space)
			*Input++ = 0;
	}
	OutPut[ArgCount] = NULL;
}

/* in response to a TOPIC message from the server */
static void p_topic (char *from, char **ArgList)
{
	char	*high;

	if (!ArgList[1]) 
	{
		fake();
		return;
	}

	switch (check_ignore_channel(from, FromUserHost, 
					ArgList[0], IGNORE_TOPICS))
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

	if (!new_check_flooding(from, ArgList[0], ArgList[1], TOPIC_FLOOD))
		return;

	message_from(ArgList[0], LOG_CRAP);
	if (do_hook(TOPIC_LIST, "%s %s %s", from, ArgList[0], ArgList[1]))
		say("%s has changed the topic on channel %s to %s",
			from, check_channel_type(ArgList[0]), ArgList[1]);
	message_from((char *) 0, LOG_CURRENT);
}

static void p_wallops (char *from, char **ArgList)
{
	int 	server_wallop = strchr(from, '.') ? 1 : 0;

	if (!ArgList[0])
	{
		fake();
		return;
	}

	/* wallops from a server */
	if (server_wallop || check_flooding(from, WALLOP_FLOOD, ArgList[0]))
	{
		int	level;
		char	*high;

		switch (check_ignore(from, FromUserHost, IGNORE_WALLOPS))
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
		message_from(from, LOG_WALLOP);
		level = set_lastlog_msg_level(LOG_WALLOP);
		if (do_hook(WALLOP_LIST, "%s %c %s", 
					from, 
					server_wallop ? 'S' : '*', 
					ArgList[0]))
			put_it("%s!%s%s!%s %s", 
					high, from, 
					server_wallop ? empty_string : star, 
					high, ArgList[0]);

		if (beep_on_level & LOG_WALLOP)
			beep_em(1);
		set_lastlog_msg_level(level);
		message_from((char *) 0, LOG_CRAP);
	}
}

static void p_privmsg (char *from, char **Args)
{
	int		level,
			list_type,
			flood_type,
			log_type;
	char		*flood_channel = NULL;
	unsigned char	ignore_type;
	char		*ptr,
			*to;
	char		*high;
	int		flood;
	int		hook_normal = 1;

	PasteArgs(Args, 1);
	to = Args[0];
	ptr = Args[1];
	if (!to || !ptr)
	{
		fake();
		return;
	}

	doing_privmsg = 1;

	/*
	 * Do ctcp's first, and if there's nothing left, then dont
	 * go to all the work below.  Plus, we dont set message_from
	 * until we know there's other stuff besides the ctcp in the
	 * message, which keeps things going to the wrong window.
	 */
	ptr = do_ctcp(from, to, ptr);
	if (!*ptr)
	{
		doing_privmsg = 0;
		return;
	}

	/* ooops. cant just do is_channel(to) because of # walls... */
	if (is_channel(to) && im_on_channel(to, from_server))
	{
		message_from(to, LOG_PUBLIC);	/* Duh! */
		malloc_strcpy(&public_nick, from);
		flood_channel = to;
		flood_type = PUBLIC_FLOOD;
		log_type = LOG_PUBLIC;
		ignore_type = IGNORE_PUBLIC;

		if (!is_channel_nomsgs(to, from_server) && 
		    !is_on_channel(to, from))
			list_type = PUBLIC_MSG_LIST;
		else
		{
			if (is_current_channel(to, 0))
				list_type = PUBLIC_LIST;
			else
				list_type = PUBLIC_OTHER_LIST;
		}
	}
	else
	{
		message_from(from, LOG_MSG);
		if (!is_me(-1, to))
		{
			log_type = LOG_WALL;
			ignore_type = IGNORE_WALLS;
			list_type = MSG_GROUP_LIST;
			flood_type = WALL_FLOOD;
		}
		else
		{
			log_type = LOG_MSG;
			ignore_type = IGNORE_MSGS;
			list_type = MSG_LIST;
			flood_type = MSG_FLOOD;
		}
	}

	switch (check_ignore_channel(from, FromUserHost, to, ignore_type))
	{
		case IGNORED:
		{
			doing_privmsg = 0;
			return;
		}
		case HIGHLIGHTED:
			high = highlight_char;
			break;
		default:
			high = empty_string;
			break;
	}
	flood = new_check_flooding(from, flood_channel, ptr, flood_type);

	/* Encrypted privmsgs are specifically exempted from flood control */
	level = set_lastlog_msg_level(log_type);
	if (sed != 0)
	{
		sed = 0;
		if (!do_hook(ENCRYPTED_PRIVMSG_LIST, "%s %s %s", from, to, ptr))
			hook_normal = 0;
	}

	if (flood && hook_normal)
	{ 
	  if (do_hook(GENERAL_PRIVMSG_LIST, "%s %s %s", from, to, ptr))
	  {
	    switch (list_type)
	    {
		case PUBLIC_MSG_LIST:
		{
			if (do_hook(list_type, "%s %s %s", from, to, ptr))
			    put_it("%s(%s/%s)%s %s", 
				high, from, check_channel_type(to), 
				high, ptr);
			break;
		}
		case MSG_GROUP_LIST:
		{
			if (do_hook(list_type, "%s %s %s", from, to, ptr))
			    put_it("%s<-%s:%s->%s %s", 
				high, from, check_channel_type(to), 
				high, ptr);
			break;
		}
		case MSG_LIST:
		{
			malloc_strcpy(&recv_nick, from);
			if (get_server_away(-2))
				beep_em(get_int_var(BEEP_WHEN_AWAY_VAR));

			if (do_hook(list_type, "%s %s", from, ptr))
			{
			    if (get_server_away(-2))
			    {
				time_t blah = time(NULL);
				put_it("%s*%s*%s %s <%.16s>", 
					high, from, high, ptr, ctime(&blah));
			    }
			    else
				put_it("%s*%s*%s %s", high, from, high, ptr);
			}
			break;
		}
		case PUBLIC_LIST:
		{
			if (do_hook(list_type, "%s %s %s", from, to, ptr))
				put_it("%s<%s>%s %s", high, from, high, ptr);
			break;
		}
		case PUBLIC_OTHER_LIST:
		{
			if (do_hook(list_type, "%s %s %s", from, to, ptr))
				put_it("%s<%s:%s>%s %s", 
					high, from, check_channel_type(to), 
					high, ptr);
			break;
		}
	    }
	  }
	  if (beep_on_level & log_type)
	    beep_em(1);
	}

	sed = 0;
	set_lastlog_msg_level(level);
	message_from(NULL, LOG_CURRENT);
	doing_privmsg = 0;
}

static void p_quit (char *from, char **ArgList)
{
	int		one_prints = 0;
	const char *	chan;
	char *		Reason;

	if (check_ignore(from, FromUserHost, IGNORE_CRAP) != IGNORED)
	{
		PasteArgs(ArgList, 0);
		Reason = ArgList[0] ? ArgList[0] : "?";
		for (chan = walk_channels(1, from); chan; chan = walk_channels(0, from))
		{
			message_from(chan, LOG_CRAP);
			if (do_hook(CHANNEL_SIGNOFF_LIST, "%s %s %s", chan, from, Reason))
				one_prints = 1;
			message_from((char *) 0, LOG_CURRENT);
		}
		if (one_prints)
		{
			message_from(what_channel(from), LOG_CRAP);
			if (do_hook(SIGNOFF_LIST, "%s %s", from, Reason))
				say("Signoff: %s (%s)", from, Reason);
			message_from((char *) 0, LOG_CURRENT);
		}
	}
	notify_mark(from, 0, 0);
	remove_from_channel((char *) 0, from, from_server);
	message_from((char *) 0, LOG_CURRENT);


	/*
	 * If we ever see our own quit, something is amiss.
	 * Apparantly, this can happen when you change servers,
	 * and you change nicks because your old nick is lagged
	 * behind.  If the client somehow doesnt get wind of your
	 * new nick change before the old nick gets knocked off,
	 * the client could possibly see your old nick quit before
	 * it knows about your new nick.  (I personally dont believe
	 * this, but thats the story ive been getting.)
	 */
	if (is_me(-1, from))
	{
		yell("Internal inconsistency: Quit message for myself:");
		yell("Pertinent information: (%s[%s]) (%s!%s (%s))",get_server_nickname(from_server), get_server_name(from_server), from, FromUserHost, ArgList[0]);
	}
}

static void p_pong (char *from, char **ArgList)
{
	PasteArgs(ArgList, 1);

	if (!my_stricmp(from, get_server_itsname(from_server)))
	{
		if (check_server_redirect(ArgList[1]))
			return;
		if (check_wait_command(ArgList[1]))
			return;
	}

	if (check_ignore(from, FromUserHost, IGNORE_CRAP) != IGNORED)
	{
		if (do_hook(PONG_LIST, "%s %s %s", from, ArgList[0], ArgList[1])
			&& ArgList[0] && strchr(ArgList[0], '.'))
		    say("%s: PONG received from %s (%s)", 
				ArgList[0], from, ArgList[1]);
	}
}

static void p_error (char *from, char **ArgList)
{
	PasteArgs(ArgList, 0);
	if (!ArgList[0])
		{fake();return;}
	if (do_hook(ERROR_LIST, "%s %s", from, ArgList[0]))
		say("%s %s", from, ArgList[0]);
}

void	add_user_who (char *from, char **ArgList)
{
	char *userhost;

	/* Obviously this is safe. */
	userhost = alloca(strlen(ArgList[1]) + strlen(ArgList[2]) + 2);
	sprintf(userhost, "%s@%s", ArgList[1], ArgList[2]);
	add_userhost_to_channel(ArgList[0], ArgList[4], from_server, userhost);
}

void	add_user_end (char *from, char **ArgList)
{
	char *		copy;
	const char *	channel;

	copy = LOCAL_COPY(ArgList[0]);
	channel = next_arg(copy, &copy);
	channel_not_waiting(channel, from_server);
}

static void	p_channel (char *from, char **ArgList)
{
	char	*c, *channel = NULL;
	int 	op = 0, vo = 0, ha = 0;
	char 	extra[20];
	char	*high;

	if (!strcmp(ArgList[0], zero))
		{fake();return;}

	channel = ArgList[0];
	malloc_strcpy(&joined_nick, from);

	/*
	 * Workaround for extremely gratuitous protocol change in ef2.9
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
		add_channel(channel, from_server);
		send_to_server("MODE %s", channel);
		whobase(channel, add_user_who, add_user_end);
	}
	else
	{
		add_to_channel(channel, from, from_server, 0, op, vo, ha);
		add_userhost_to_channel(channel, from, from_server, FromUserHost);
	}

	switch (check_ignore_channel(from, FromUserHost, channel, IGNORE_JOINS))
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

	if (!new_check_flooding(from, channel, star, JOIN_FLOOD))
		return;

	*extra = 0;
	if (op)
		strcat(extra, " (+o)");
	if (vo)
		strcat(extra, " (+v)");

	message_from(channel, LOG_CRAP);
	if (do_hook(JOIN_LIST, "%s %s %s %s", 
			from, channel, FromUserHost, extra))
		say("%s%s%s (%s) has joined channel %s%s%s%s", 
			high, from, high, FromUserHost, 
			high, check_channel_type(channel), high, extra);
	message_from((char *) 0, LOG_CURRENT);

	/*
	 * This should be done last to ensure that the userhost has been
	 * properly handled...
	 */
	notify_mark(from, 1, 0);
}

static void 	p_invite (char *from, char **ArgList)
{
	char	*high;

	switch (check_ignore_channel(from, FromUserHost, ArgList[1], IGNORE_INVITES))
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

	if (!check_flooding(from, INVITE_FLOOD, ArgList[1]))
		return;

	if (ArgList[0] && ArgList[1])
	{
		message_from(from, LOG_CRAP);
		if (do_hook(INVITE_LIST, "%s %s %s", from, ArgList[1],FromUserHost))
			say("%s%s (%s)%s invites you to channel %s", high,
				from, FromUserHost, high, ArgList[1]);
		malloc_strcpy(&invite_channel, ArgList[1]);
		malloc_strcpy(&recv_nick, from);
	}
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
static void	p_kill (char *from, char **ArgList)
{
	/* 
	 * Bogorific Microsoft Exchange ``IRC'' server sends out a KILL
	 * protocol message instead of a QUIT protocol message when
	 * someone is killed on your server.  Do the obviously appropriate
	 * thing and reroute this misdirected protocol message to 
	 * p_quit, where it should have been sent in the first place.
	 * Die Microsoft, Die.
	 */
	if (!is_me(-1, ArgList[0]))
	{
		/* I don't care if this doesn't work.  */
		p_quit(from, ArgList);	/* Die Microsoft, Die */
		return;
	}


	/*
	 * We've been killed.  Save our channels, close up shop on 
	 * this server.  We need to call window_check_servers before
	 * we do any output, in case we want the output to go anywhere
	 * meaningful.
	 */
	if (strchr(from, '.'))
        {
		say("Server [%s] has rejected you. (%s)",
			from, ArgList[1] ? ArgList[1] : 
			"probably due to a nick collision");
	}
	else
	{
		int 	hooked;

		if ((hooked = do_hook(DISCONNECT_LIST,"Killed by %s (%s)",
			from, ArgList[1] ? ArgList[1] : "(No Reason Given)")))
		{
		   say("You have been killed by that fascist [%s] %s", 
			from, ArgList[1] ? ArgList[1] : "(No Reason Given)");
		}

/* If we are a background, and /on disconnect didnt hook, then we arent
   going anywhere.  We might as well quit. */ 
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
				  sc, NULL, current_window);
		}

		server_reconnects_to(from_server, -1);
	}
}

static void p_ping (char *from, char **ArgList)
{
        PasteArgs(ArgList, 0);
	send_to_server("PONG %s", ArgList[0]);
}

static void p_silence (char *from, char **ArgList)
{
	char *target = ArgList[0];
	char mag = *target++;

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


static void p_nick (char *from, char **ArgList)
{
	int		been_hooked = 0,
			its_me = 0;
	const char	*chan;
	char		*line;
	char		*high;
	int		ignored = 0;

	line = ArgList[0];

	/*
	 * Is this me changing nick?
	 */
	if (is_me(-1, from))
	{
		accept_server_nickname(from_server, line);
		its_me = 1;
		nick_command_is_pending(from_server, 0);
	}

	switch (check_ignore(from, FromUserHost, IGNORE_NICKS))
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

	if (!check_flooding(from, NICK_FLOOD, line))
		goto do_rename;

	for (chan = walk_channels(1, from); chan; chan = walk_channels(0, from))
	{
		if (check_ignore_channel(from, FromUserHost, chan, 
						IGNORE_NICKS) == IGNORED)
		{
			ignored = 1;
			continue;
		}

		message_from(chan, LOG_CRAP);
		if (!do_hook(CHANNEL_NICK_LIST, "%s %s %s", chan, from, line))
			been_hooked = 1;
	}

	if (!been_hooked && !ignored)
	{
		if (its_me)
			message_from((char *) 0, LOG_CRAP);
		else
			message_from(what_channel(from), LOG_CRAP);

		if (do_hook(NICKNAME_LIST, "%s %s", from, line))
			say("%s is now known as %s", from, line);
	}

do_rename:
	notify_mark(from, 0, 0);
	rename_nick(from, line, from_server);
	notify_mark(line, 1, 0);
}

static void p_mode (char *from, char **ArgList)
{
	char	*channel;
	char	*line;
	int	flag;

	PasteArgs(ArgList, 1);
	channel = ArgList[0];
	line = ArgList[1];

	/* 
	 * Stupid lame broken pathetic UnrealIRCD server sends a bloody
	 * VOID MODE CHANGE when you connect.  How totaly useless and
	 * pointless a change.  What possesses people to make these kinds
	 * of blithering idiot modifications in the first place?
	 */
	if (!line || !*line)
	{
		say("Server sent an empty MODE change; ignoring it.");
		return;
	}

	flag = check_ignore_channel(from, FromUserHost, channel, IGNORE_CRAP);
	message_from(channel, LOG_CRAP);
	if (channel && line)
	{
                if (get_int_var(MODE_STRIPPER_VAR))
                        strip_modes(from, channel, line);
		if (is_channel(channel))
		{
			if (flag != IGNORED && do_hook(MODE_LIST, "%s %s %s",
				     from, check_channel_type(channel), line))
				say("Mode change \"%s\" on channel %s by %s",
				     line, check_channel_type(channel), from);
			update_channel_mode(channel, line);
		}
		else
		{
			if (flag != IGNORED && do_hook(MODE_LIST, "%s %s %s",
				     from, channel, line))
				say("Mode change \"%s\" for user %s by %s",
						line, channel, from);
			update_user_mode(line);
		}
		update_all_status();
	}
	message_from(NULL, LOG_CURRENT);
}

static void strip_modes (char *from, char *channel, char *line)
{
	char	*mode;
	char 	*pointer;
	char	mag = '+'; /* XXXX Bogus */
        char    *copy = (char *) 0;
	char	*free_copy;

	free_copy = LOCAL_COPY(line);

	copy = free_copy;
	mode = next_arg(copy, &copy);

	if (is_channel(channel))
	{
	    for (pointer = mode; *pointer; pointer++)
	    {
		char	c = *pointer;

		/* 
		 * Conversion from "next_arg" to "safe_new_next_arg"
		 * done on Aug 30, 2001 because of lame-o servers that
		 * don't send arguments with +l, +b, +k, +o, +v, +e, +I
		 * and the like.  You all know who you are.  Don't do that.
		 */
		switch (c) 
		{
			case '+' :
			case '-' : 
				mag = c; 
				break;

			case 'l' : 
			{
				if (mag == '+')
					do_hook(MODE_STRIPPED_LIST,
						"%s %s %c%c %s",
						from, channel, mag,
						c,safe_new_next_arg(copy,&copy));
				else
					do_hook(MODE_STRIPPED_LIST,
						"%s %s %c%c",
						from,channel,mag,c);
				break;
			}

			case 'a' : case 'i' : case 'm' : case 'n' :
			case 'p' : case 's' : case 't' : case 'z' : 
			case 'c' : case 'r' : case 'R' : case 'O' :
			case 'M' :
			{
				do_hook(MODE_STRIPPED_LIST,
					"%s %s %c%c",
					from, channel, mag, c);
				break;
			}

			case 'b' : case 'k' : case 'o' : case 'v' : 
			case 'e' : case 'I' :
			{
				do_hook(MODE_STRIPPED_LIST,
					"%s %s %c%c %s", 
					from, channel, mag, c,
					safe_new_next_arg(copy,&copy));
				break;
			}
		}
	    }
	}
	else /* User mode */
	{
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
	}
}

static void p_kick (char *from, char **ArgList)
{
	char	*channel,
		*who,
		*comment;

	channel = ArgList[0];
	who = ArgList[1];
	comment = ArgList[2] ? ArgList[2] : "(no comment)" ;

	message_from(channel, LOG_CRAP);
	if (channel && who)
	{
		if (is_me(-1, who))
		{
			const char *key = get_channel_key(channel, from_server);
			Window *win = get_channel_window(channel, from_server);
			Window *old_tw = to_window;
			Window *old_cw = current_window;

			if (!key)
				key = empty_string;

			/*
			 * Uh-oh.  If win is null we have a problem.
			 */
			if (!win)
			{
				/*
				 * Check to see if we got a KICK for a 
				 * channel we dont think we're on.
				 */
				if (!im_on_channel(channel, from_server))
					yell("You were KICKed by [%s] on channel [%s] (reason [%s]), which you are not on!  Will not try to auto-rejoin", who, channel, comment);
				else
					panic("Window is NULL for channel [%s]", channel);

				return;
			}

			if (get_int_var(AUTO_REJOIN_VAR))
			{
				add_timer(0, empty_string, 
					  get_int_var(AUTO_REJOIN_DELAY_VAR), 1,
					  auto_rejoin_callback, 
					  m_sprintf("%s %d %d %s", channel, 
							from_server, 
							win->refnum,
							key),
					  NULL, win);
			}
			remove_channel(channel, from_server);
			update_all_status();

			current_window = win;
			to_window = win;
			if (do_hook(KICK_LIST, "%s %s %s %s", who, from, check_channel_type(channel), comment))
				say("You have been kicked off channel %s by %s (%s)", check_channel_type(channel), from, comment);
			to_window = old_tw;
			current_window = old_cw;
		}
		else
		{
			if ((check_ignore_channel(from, FromUserHost, channel, IGNORE_CRAP) != IGNORED) && 
			     do_hook(KICK_LIST, "%s %s %s %s", who, from, channel, comment))
				say("%s has been kicked off channel %s by %s (%s)", who, check_channel_type(channel), from, comment);
			remove_from_channel(channel, who, from_server);
		}
	}
	message_from(NULL, LOG_CURRENT);
}

static void p_part (char *from, char **ArgList)
{
	char	*channel;

	channel = ArgList[0];

	if ((check_ignore_channel(from, FromUserHost, channel, IGNORE_PARTS) != IGNORED))
	{
		message_from(channel, LOG_CRAP);
		if (ArgList[1])		/* Dalnet part messages */
		{
			PasteArgs(ArgList, 1);
			if (do_hook(LEAVE_LIST, "%s %s %s %s", 
				from, channel, FromUserHost, ArgList[1]))
			    say("%s has left channel %s because (%s)", 
				from, check_channel_type(channel), 
				ArgList[1]);
		}
		else
		{
			if (do_hook(LEAVE_LIST, "%s %s %s", 
				from, channel, FromUserHost))
			    say("%s has left channel %s", 
				from, check_channel_type(channel));
		}
		message_from(NULL, LOG_CURRENT);
	}

	if (is_me(-1, from))
		remove_channel(channel, from_server);
	else
		remove_from_channel(channel, from, from_server);
}

/*
 * Egads. i hope this is right.
 */
static void p_rpong (char *from, char **ArgList)
{
	if (!ArgList[3])
	{
		/* 
		 * We should always get an ArgList[3].  Punt if we dont.
		 */
		PasteArgs(ArgList, 0);
		say("RPONG %s (from %s)", ArgList[0], from);
	}
	else
	{
		/*
		 * :server RPONG yournick remoteserv ms :yourargs
		 *
		 * ArgList[0] -- our nickname (presumably)
		 * ArgList[1] -- The server we RPING'd
		 * ArgList[2] -- The number of ms it took to return
		 * ArgList[3] -- The arguments we passed (presumably)
		 */
		time_t delay = time(NULL) - atol(ArgList[3]);

		say("Pingtime %s - %s : %s ms (total delay: %ld s)",
			from, ArgList[1], ArgList[2], delay);
	}
}


static void rfc1459_odd (char *from, char *comm, char **ArgList)
{
	PasteArgs(ArgList, 0);
	if (do_hook(ODD_SERVER_STUFF_LIST, "%s %s %s", 
		from ? from : "*", comm, ArgList[0]))
	{
		if (from)
			say("Odd server stuff: \"%s %s\" (%s)", comm, ArgList[0], from);
		else
			say("Odd server stuff: \"%s %s\"", comm, ArgList[0]);
	}
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
{	"NOTICE",	parse_notice,	NULL,		0		},
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
void 	parse_server (char *orig_line)
{
	char	*from,
		*comm;
	int	numeric;
	char	**ArgList;
	char	*TrueArgs[MAXPARA + 1];
	protocol_command *retval;
	int	loc;
	int	cnt;
	char	*line = NULL;
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

	if (inbound_line_mangler)
	{
		size = strlen(orig_line) * 3;
		line = alloca(size + 1);
		strcpy(line, orig_line);
		if (mangle_line(line, inbound_line_mangler, size) > size)
			yell("mangle_line truncated its result.  Ack.");
	}
	else
		line = orig_line;

	ArgList = TrueArgs;
	BreakArgs(line, &from, ArgList);

	/* XXXX - i dont think 'from' can be null here.  */
	if ((!(comm = *ArgList++)) || !from || !*ArgList)
		return;		/* Serious protocol violation -- ByeBye */

	/* 
	 * I reformatted these in may '96 by using the output of /stats m
	 * from a few busy servers.  They are arranged so that the most 
	 * common types are high on the list (to save the average number
	 * of compares.)  I will be doing more testing in the future on
	 * a live client to see if this is a reasonable order.
	 */
	if ((numeric = atoi(comm)))
		numbered_command(from, numeric, ArgList);
	else
	{
		retval = (protocol_command *)find_fixed_array_item(
			(void *)rfc1459, sizeof(protocol_command), 
			num_protocol_cmds + 1, comm, &cnt, &loc);

		if (cnt < 0 && rfc1459[loc].inbound_handler)
			rfc1459[loc].inbound_handler(from, ArgList);
		else
			rfc1459_odd(from, comm, ArgList);
	}

	FromUserHost = empty_string;
	from_server = -1;
}
