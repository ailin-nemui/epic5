/* $EPIC: notice.c,v 1.22 2003/05/09 04:29:52 jnelson Exp $ */
/*
 * notice.c: special stuff for parsing NOTICEs
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright © 1993, 2003 EPIC Software Labs.
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
#include "ctcp.h"
#include "window.h"
#include "lastlog.h"
#include "flood.h"
#include "vars.h"
#include "ircaux.h"
#include "hook.h"
#include "ignore.h"
#include "server.h"
#include "output.h"
#include "names.h"
#include "parse.h"
#include "notify.h"
#include "notice.h"
#include "commands.h"

static	time_t 	convert_note_time_to_real_time (char *stuff);
static	int 	kill_message (const char *from, const char *line);
static	int	never_connected = 1;


/*
 * The client apparantly never was adapted to handle the new NOTE syntax.
 * So i had to kludge this up to work with it.  Currently, NOTEs are sent
 * something like this:
 *
 *	NOTICE yournick :Note from nick!user@host /xxxd:xxh:xxm/ [N] message
 *
 * and parse() calls parse_notice(), who notices that there is no pefix
 * and passes it off to parse_local_server_notice(), who checks to see 
 * if it is a note (it is), blows away the "Note from" part, and re-sets 
 * the "from" and "FromUserHost" parts with the nick!user@host part and 
 * passes us the buck with 'line' pointing at the time description 
 * (the /.../ part)
 */
static void 	parse_note (char *from, char *line)
{
	char	*date,
		*flags,
		*high;
	time_t	when;
	int 	level;

	switch (check_ignore(from, FromUserHost, IGNORE_NOTES))
	{
		case IGNORED:
			return;
		case HIGHLIGHTED:
			high = highlight_char;
			break;
		default:
			high = empty_string;
	}

	if (check_flooding(from, FromUserHost, NOTE_FLOOD, line))
		return;

/* 
	at this point, line looks like:
	"/xxxd:xxh:xxm/ [FLAGS] message goes here"
 */
	date = next_arg(line, &line);
	flags = next_arg(line, &line);

	when = convert_note_time_to_real_time(date);
	level = set_lastlog_msg_level(LOG_NOTES);

	if (do_hook(NOTE_LIST, "%s %lu %s", from, when, line))
	{
		if (time(NULL) - when > 60)	/* not just sent */
			put_it("%s[%s]%s %s (%s)", high, from, high, 
						   line, my_ctime(when));
		else
			put_it("%s[%s]%s %s", high, from, high, line);
	}

	if (beep_on_level & LOG_NOTES)
		beep_em(1);
	set_lastlog_msg_level(level);
}

static time_t	convert_note_time_to_real_time(char *stuff)
{
	time_t days = 0, hours = 0, minutes = 0;

	stuff++;			      /* first character is a '/' */
	days = strtoul(stuff, &stuff, 10);    /* get the number of days */
	stuff++;			      /* skip over the 'd' */
	stuff++;			      /* skip over the ':' */
	hours = strtoul(stuff, &stuff, 10);   /* get the number of hours */
	stuff++;			      /* skip over the 'h' */
	stuff++;			      /* skip over the ':' */
	minutes = strtoul(stuff, &stuff, 10); /* get the number of minutes */
	stuff++;			      /* skip over the 'm' */
	stuff++;			      /* skip over the '/' */
	if (*stuff)
		yell("cntto: bad format");

	hours += days * 24;
	minutes += hours * 60;
	return (time(NULL) - minutes * 60);
}

/*
 * This parses NOTICEs that are sent from that wacky ircd we are connected
 * to, and 'to' is guaranteed not to be a channel.
 */
static 	void 	parse_local_server_notice (const char *from, const char *to, const char *line)
{
	int	lastlog_level;
	const char *	f;

	f = from;
	if (!f || !*f)
		if (!(f = get_server_itsname(from_server)))
			f = get_server_name(from_server);

	/* OPERator Notices */
	if (!strncmp(line, "*** Notice -- ", 13))
	{
		if (!strncmp(line + 14, "Received KILL message for ", 26))
			if (kill_message(f, line + 40))
				return;

		message_from(to, LOG_OPNOTE);
		lastlog_level = set_lastlog_msg_level(LOG_OPNOTE);
		if (!do_hook(OPER_NOTICE_LIST, "%s %s", f, line + 14))
			return;
	}

	/* NOTEs */
	else if (!strncmp(line, "Note", 4))
	{
		char *note_from = NULL;
		char *point = NULL;

		if (strlen(line) > 10)
		{
			/* Skip the "Note From" part */
			note_from = LOCAL_COPY(line + 10); 

			if ((point = strchr(note_from, '!')))
			{
				*point++ = 0;
				FromUserHost = line;
				if ((point = strchr(FromUserHost, ' ')))
				{
					*point++ = 0;
					parse_note(note_from, point);
				}
				FromUserHost = empty_string;
			}
		}
		return;
	}

	message_from(from, LOG_SNOTE);
	lastlog_level = set_lastlog_msg_level(LOG_SNOTE);

	/* Check to see if the notice already has its own header... */
	if (do_hook(GENERAL_NOTICE_LIST, "%s %s %s", f, to, line))
	{
	    if (*line == '*' || *line == '#')
	    {
		if (do_hook(SERVER_NOTICE_LIST, "%s %s", f, line))
			put_it("%s", line);
	    }
	    else
		if (do_hook(SERVER_NOTICE_LIST, "%s *** %s", f, line))
			say("%s", line);
	}

	if (lastlog_level)
	{
		set_lastlog_msg_level(lastlog_level);
		message_from(NULL, lastlog_level);
	}
}

/*
 * The main handler for those wacky NOTICE commands...
 * This is as much like p_privmsg as i can get away with.
 */
void 	p_notice (const char *from, const char *comm, const char **ArgList)
{
	const char 	*target, *message;
	int		level,
			hook_type;
	const char *	flood_channel = NULL;
	char *		high;

	PasteArgs(ArgList, 1);
	if (!(target = ArgList[0]))
		{ rfc1459_odd(from, comm, ArgList); return; }
	if (!(message = ArgList[1]))
		{ rfc1459_odd(from, comm, ArgList); return; }

	set_server_doing_notice(from_server, 1);
	sed = 0;

	/* Do normal /CTCP reply handling */
	/* XXX -- Casting "message" to (char *) is cheating. */
	message = do_notice_ctcp(from, target, (char *)message);
	if (!*message) {
		set_server_doing_notice(from_server, 0);
		return;
	}

	/* Check to see if it is a "Server Notice" */
	if ((!from || !*from) || !strcmp(get_server_itsname(from_server), from))
	{
		parse_local_server_notice(from, target, message);
		set_server_doing_notice(from_server, 0);
		return;
	}
	/* For pesky prefix-less NOTICEs substitute the server's name */
	if (!from || !*from)
		from = get_server_name(from_server);

	/*
	 * Note that NOTICEs from servers are not "server notices" unless
	 * the target is not a channel (ie, it is sent to us).  Any notice
	 * that is sent to a channel is a normal NOTICE, notwithstanding
	 * _who_ sent it.
	 */
	if (is_channel(target) && im_on_channel(target, from_server))
	{
		flood_channel = target;
		hook_type = PUBLIC_NOTICE_LIST;
	}
	else if (!is_me(from_server, target))
	{
		flood_channel = NULL;
		hook_type = NOTICE_LIST;
	}
	else
	{
		flood_channel = NULL;
		hook_type = NOTICE_LIST;
		target = from;
	}

	/* Check for /ignore's */
	switch (check_ignore_channel(from, FromUserHost, target, IGNORE_NOTICES))
	{
		case IGNORED:
			set_server_doing_notice(from_server, 0);
			return;
		case HIGHLIGHTED:
			high = highlight_char;
			break; /* oops! */
		default:
			high = empty_string;
	}

	/* Let the user know if it is an encrypted notice */
	/* Note that this is always hooked, even during a flood */
	if (sed)
	{
		int	do_return = 1;

		sed = 0;
		level = set_lastlog_msg_level(LOG_NOTICE);
		message_from(target, LOG_NOTICE);

		if (do_hook(ENCRYPTED_NOTICE_LIST, "%s %s %s", 
				from, target, sed == 1 ? message : empty_string))
			do_return = 0;

		set_lastlog_msg_level(level);
		message_from(NULL, LOG_CURRENT);

		if (do_return) {
			set_server_doing_notice(from_server, 0);
			return;
		}
	}

	if (new_check_flooding(from, FromUserHost, flood_channel, 
					message, NOTICE_FLOOD)) {
		set_server_doing_notice(from_server, 0);
		return;
	}


	/* Beep the user if they asked us to */
	if (beep_on_level & LOG_NOTICE)
		beep_em(1);

	/* Go ahead and throw it to the user */
	level = set_lastlog_msg_level(LOG_NOTICE);
	message_from(target, LOG_NOTICE);

	if (do_hook(GENERAL_NOTICE_LIST, "%s %s %s", from, target, message))
	{
	    if (hook_type == NOTICE_LIST)
	    {
		if (do_hook(hook_type, "%s %s", from, message))
			put_it("%s-%s-%s %s", high, from, high, message);
	    }
	    else
	    {
		if (do_hook(hook_type, "%s %s %s", from, target, message))
			put_it("%s-%s:%s-%s %s", high, from, target, high, 
							message);
	    }
	}

	/* Clean up and go home. */
	set_lastlog_msg_level(level);
	message_from(NULL, LOG_CURRENT);
	set_server_doing_notice(from_server, 0);

	/* Alas, this is not protected by protocol enforcement. :( */
	notify_mark(from_server, from, 1, 0);
}

/*
 * got_initial_version_28: this is called when ircii gets the serial
 * number 004 reply.  We do this becuase the 004 numeric gives us the
 * server name and version in an easy to use fashion, and doesnt
 * rely on the syntax or construction of the 002 numeric.
 *
 * Hacked as neccesary by jfn, May 1995
 */
void 	got_initial_version_28 (const char *server, const char *version, const char *umodes)
{
	/* Worthless 004 reply.  Hope for the best! */
	if (!server || !version || !umodes)
	{
		set_server_version(from_server, Server2_8);
		set_server_version_string(from_server, "<none provided>");
		set_server_itsname(from_server, get_server_name(from_server));
	}
	else
	{
		if (!strncmp(version, "2.8", 3))
		{
			if (strstr(version, "mu") || strstr(version, "me"))
				set_server_version(from_server, Server_u2_8);
			else
				set_server_version(from_server, Server2_8);
		}
		else if (!strncmp(version, "2.9", 3))
			set_server_version(from_server, Server2_9);
		else if (!strncmp(version, "2.10", 4))
			set_server_version(from_server, Server2_10);
		else if (!strncmp(version, "u2.9", 4))
			set_server_version(from_server, Server_u2_9);
		else if (!strncmp(version, "u2.10", 4))
			set_server_version(from_server, Server_u2_10);
		else if (!strncmp(version, "u3.0", 4))
			set_server_version(from_server, Server_u3_0);
		else
			set_server_version(from_server, Server2_8);

		set_server_version_string(from_server, version);
		set_server_itsname(from_server, server);
		/* set_possible_umodes(from_server, umodes); */
	}

	reconnect_all_channels();
	server_did_rejoin_channels(from_server);
	message_from(NULL, LOG_CRAP);
	reinstate_user_modes();

	if (never_connected)
	{
		never_connected = 0;
		permit_status_update(1);

		if (!ircrc_loaded)
			load_ircrc();

		if (default_channel)
		{
			e_channel("JOIN", default_channel, empty_string);
			new_free(&default_channel);
		}
	}
	else if (get_server_away(from_server))
		set_server_away(from_server, get_server_away(from_server));

	update_all_status();
	do_hook(CONNECT_LIST, "%s %d %s", get_server_name(from_server),
		get_server_port(from_server), get_server_itsname(from_server));
	window_check_channels();
}

int 	kill_message (const char *from, const char *cline)
{
	char *poor_sap;
	char *bastard;
	const char *path_to_bastard;
	char *reason;
	char *line;

	line = LOCAL_COPY(cline);
	poor_sap = next_arg(line, &line);

	/* Dalnet kill BBC and doesnt append the period */
	if (!end_strcmp(poor_sap, ".", 1))
		chop(poor_sap, 1);

	/* dalnet kill BBC and doesnt use "From", but "from" */
	if (my_strnicmp(line, "From ", 5))
	{
		yell("Attempted to parse an ill-formed KILL request [%s %s]",
			poor_sap, line);
		return 0;
	}
	line += 5;
	bastard = next_arg(line, &line);

	/* Hybrid BBC and doesn't include the kill-path. */
	/* Fend off future BBC kills */
	if (my_strnicmp(line, "Path: ", 6))
	{
		path_to_bastard = "*";
		reason = line;		/* Hope for the best */
	}
	else
	{
		line += 6;
		path_to_bastard = next_arg(line, &line);
		reason = line;
	}

	return !do_hook(KILL_LIST, "%s %s %s %s %s", from, poor_sap, bastard,
					path_to_bastard, reason);
}

/*
 * XXX - I suppose this doesn't belong here, but where does it belong?
 */
void    load_ircrc (void)
{
        char buffer[7];
        strlcpy(buffer, "global", sizeof buffer);

        loading_global = 1;
        load("LOAD", buffer, empty_string);
        loading_global = 0;

        /* read the startup file */
        if (access(epicrc_file, R_OK) == 0 && !quick_startup)
        {
                load("LOAD", epicrc_file, empty_string);
                startup_file = epicrc_file;
        }
        else if (access(ircrc_file, R_OK) == 0 && !quick_startup)
        {
                load("LOAD", ircrc_file, empty_string);
                startup_file = ircrc_file;
        }

        ircrc_loaded = 1;
}

