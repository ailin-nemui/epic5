/* $EPIC: notice.c,v 1.11 2002/07/30 16:12:59 crazyed Exp $ */
/*
 * notice.c: special stuff for parsing NOTICEs
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
#include "ctcp.h"
#include "window.h"
#include "lastlog.h"
#include "flood.h"
#include "vars.h"
#include "ircaux.h"
#include "hook.h"
#include "ignore.h"
#include "server.h"
#include "funny.h"
#include "output.h"
#include "names.h"
#include "parse.h"
#include "notify.h"
#include "notice.h"
#include "commands.h"

static	time_t 	convert_note_time_to_real_time (char *stuff);
static	int 	kill_message (const char *from, char *line);
	int	doing_notice = 0;


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

	if (!check_flooding(from, FromUserHost, NOTE_FLOOD, line))
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
static 	void 	parse_local_server_notice (char *from, char *to, char *line)
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
		if (strlen(line) > 10)
		{
			line += 10; /* skip the "Note from" part */
			note_from = line;
			if ((line = strchr(note_from, '!')))
			{
				*line++ = 0;
				FromUserHost = line;
				if ((line = strchr(FromUserHost, ' ')))
				{
					*line++ = 0;
					parse_note(note_from, line);
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
 */
void 	parse_notice (char *from, char **Args)
{
	int	level,
		type;
	char	*to;
	int	no_flooding;
	char	*high;
	char	*line;

	PasteArgs(Args, 1);
	to   = Args[0];
	line = Args[1];
	if (!to || !line)
		return;

	/* 
	 * If 'to' is empty (present, but zero length), this is a
	 * seriously mal-formed NOTICE.  I think this test is here 
	 * only to save a coredump -- this shouldn't happen with any
	 * real irc server.
	 */
	if (!*to)
	{
		if (line && *line)
			put_it("[obsolete server notice] %s", line + 1);
		return;
	}

	/*
	 * Suppress the sending of PRIVMSGs or NOTICEs until this
	 * global variable is reset.
	 */
	doing_notice = 1;
	sed = 0;

	/*
	 * Note that NOTICEs from servers are not "server notices" unless
	 * the target is not a channel (ie, it is sent to us).  Any notice
	 * that is sent to a channel is a normal NOTICE, notwithstanding
	 * _who_ sent it.
	 */
	if (is_channel(to))
	{
		message_from(to, LOG_NOTICE);
		type = PUBLIC_NOTICE_LIST;
	}

	/* Check to see if it is a "Server Notice" */
	else if (!from || !*from || 
		!strcmp(get_server_itsname(from_server), from))
	{
		parse_local_server_notice(from, to, line);
		doing_notice = 0;
		return;
	}

	/* It is a notice from someone else, possibly remote server */
	else
	{
		message_from(from, LOG_NOTICE);
		type = NOTICE_LIST;
	}


	/* Set the default output target level */
	level = set_lastlog_msg_level(LOG_NOTICE);

	/* Check for /ignore's */
	switch (check_ignore_channel(from, FromUserHost, to, IGNORE_NOTICES))
	{
		case IGNORED:
			goto the_end;
		case HIGHLIGHTED:
			high = highlight_char;
			break; /* oops! */
		default:
			high = empty_string;
	}

	/* Check for /notify's */
	notify_mark(from, 1, 0);

	/* Do normal /CTCP reply handling */
	line = do_notice_ctcp(from, to, line);
	if (!*line)
		goto the_end;

	/* Check for flooding */
	no_flooding = check_flooding(from, FromUserHost, NOTICE_FLOOD, line);

	/* Let the user know if it is an encrypted notice */
	/* Note that this is always hooked, even during a flood */
	if (sed != 0 && !do_hook(ENCRYPTED_NOTICE_LIST, "%s %s %s", 
			from, to, sed == 1 ? line : empty_string))
	{
		sed = 0;
		goto the_end;
	}

	/* Do not parse the notice if we are being flooded */
	if (!no_flooding)
		goto the_end;

	/* Offer the notice to the user and do output */
	if (do_hook(GENERAL_NOTICE_LIST, "%s %s %s", from, to, line))
	{
	    if (type == NOTICE_LIST)
	    {
		if (do_hook(type, "%s %s", from, line))
			put_it("%s-%s-%s %s", high, from, high, line);
	    }
	    else
	    {
		if (do_hook(type, "%s %s %s", from, to, line))
			put_it("%s-%s:%s-%s %s", high, from, to, high, line);
	    }
	}
	if (beep_on_level & LOG_NOTICE)
		beep_em(1);

the_end:
	/* Clean up and go home. */
	set_lastlog_msg_level(level);
	message_from(NULL, level);
	doing_notice = 0;
}

/*
 * got_initial_version_28: this is called when ircii gets the serial
 * number 004 reply.  We do this becuase the 004 numeric gives us the
 * server name and version in an easy to use fashion, and doesnt
 * rely on the syntax or construction of the 002 numeric.
 *
 * Hacked as neccesary by jfn, May 1995
 */
void 	got_initial_version_28 (char **ArgList)
{
	char *server, *version, *umodes;

	server = ArgList[0];
	version = ArgList[1];
	umodes = ArgList[2];

	if (!server || !version || !umodes)
	{
		yell("Bummer.  This server returned a worthless 004 numeric.");
		yell("I'll have to guess at all the values");

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
		set_possible_umodes(from_server, umodes);
	}

	reconnect_all_channels();
	server_did_rejoin_channels(from_server);
	message_from(NULL, LOG_CRAP);
	reinstate_user_modes();

	if (never_connected)
	{
		never_connected = 0;

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

int 	kill_message (const char *from, char *cline)
{
	char *poor_sap;
	char *bastard;
	char *path_to_bastard;
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
        strcpy(buffer, "global");

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

