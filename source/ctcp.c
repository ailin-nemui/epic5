/* $EPIC: ctcp.c,v 1.62 2014/03/21 20:54:58 jnelson Exp $ */
/*
 * ctcp.c:handles the client-to-client protocol(ctcp). 
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright © 1993, 2003 EPIC Software Labs
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
/*
 * Serious cleanup by jfn (August 1996)
 */

#include "irc.h"
#include "sedcrypt.h"
#include "ctcp.h"
#include "dcc.h"
#include "commands.h"
#include "hook.h"
#include "ignore.h"
#include "ircaux.h"
#include "lastlog.h"
#include "names.h"
#include "output.h"
#include "parse.h"
#include "server.h"
#include "status.h"
#include "vars.h"
#include "window.h"
#include "ifcmd.h"
#include "flood.h"
#include "words.h"

#include <pwd.h>
#ifdef HAVE_UNAME
# include <sys/utsname.h>
#endif

#define CTCP_SPECIAL	0	/* Limited, quiet, noreply, not expanded */
#define CTCP_REPLY	1	/* Sends a reply (not significant) */
#define CTCP_INLINE	2	/* Expands to an inline value */
#define CTCP_NOLIMIT	4	/* Limit of one per privmsg. */
#define CTCP_TELLUSER	8	/* Tell the user about it. */

static	int 	split_CTCP (char *, char *, char *);

/*
 * ctcp_entry: the format for each ctcp function.   note that the function
 * described takes 4 parameters, a pointer to the ctcp entry, who the message
 * was from, who the message was to (nickname, channel, etc), and the rest of
 * the ctcp message.  it can return null, or it can return a malloced string
 * that will be inserted into the oringal message at the point of the ctcp.
 * if null is returned, nothing is added to the original message
 */

struct _CtcpEntry;
typedef char *(*CTCP_Handler) (struct _CtcpEntry *, const char *, const char *, char *);
typedef	struct _CtcpEntry
{
	const char	*name;  /* name of ctcp datatag */
	int		id;	/* index of this ctcp command */
	int		flag;	/* Action modifiers */
	const char	*desc;  /* description returned by ctcp clientinfo */
	CTCP_Handler 	func;	/* function that does the dirty deed */
	CTCP_Handler 	repl;	/* Function that is called for reply */
}	CtcpEntry;

#define CTCP_HANDLER(x) \
static char * x (CtcpEntry *ctcp, Char *from, Char *to, char *cmd)

/* forward declarations for the built in CTCP functions */
CTCP_HANDLER(do_crypto);
CTCP_HANDLER(do_version);
CTCP_HANDLER(do_clientinfo);
CTCP_HANDLER(do_ping);
CTCP_HANDLER(do_echo);
CTCP_HANDLER(do_userinfo);
CTCP_HANDLER(do_finger);
CTCP_HANDLER(do_time);
CTCP_HANDLER(do_atmosphere);
CTCP_HANDLER(do_dcc);
CTCP_HANDLER(do_utc);
CTCP_HANDLER(do_dcc_reply);
CTCP_HANDLER(do_ping_reply);

static CtcpEntry ctcp_cmd[] =
{
	/* The most common ones */
	{ "ACTION",	CTCP_ACTION, 	CTCP_SPECIAL | CTCP_NOLIMIT,
		"contains action descriptions for atmosphere",
		do_atmosphere, 	do_atmosphere },
	{ "DCC",	CTCP_DCC, 	CTCP_SPECIAL | CTCP_NOLIMIT,
		"requests a direct_client_connection",
		do_dcc, 	do_dcc_reply },
	{ "VERSION",	CTCP_VERSION,	CTCP_REPLY | CTCP_TELLUSER,
		"shows client type, version and environment",
		do_version, 	NULL },

	/* Common ones to people using strong crypto */
	{ "AESSHA256-CBC",	CTCP_AESSHA256,	CTCP_INLINE | CTCP_NOLIMIT,
		"transmit aes256-cbc ciphertext using a sha256 key",
		do_crypto, 	do_crypto },
	{ "AES256-CBC",		CTCP_AES256,	CTCP_INLINE | CTCP_NOLIMIT,
		"transmit aes256-cbc ciphertext",
		do_crypto, 	do_crypto },
	{ "CAST128ED-CBC",	CTCP_CAST5, 	CTCP_INLINE | CTCP_NOLIMIT,
		"transmit cast5-cbc ciphertext",
		do_crypto, 	do_crypto},
	{ "BLOWFISH-CBC",	CTCP_BLOWFISH,	CTCP_INLINE | CTCP_NOLIMIT,
		"transmit blowfish-cbc ciphertext",
		do_crypto, 	do_crypto },
	{ "FISH",		CTCP_FISH,	CTCP_INLINE | CTCP_NOLIMIT,
		"transmit FiSH (blowfish-ecb with sha256'd key) ciphertext",
		do_crypto, 	do_crypto },
	{ "SED",		CTCP_SED, 	CTCP_INLINE | CTCP_NOLIMIT,
		"transmit simple_encrypted_data ciphertext",
		do_crypto, 	do_crypto },
	{ "SEDSHA",		CTCP_SEDSHA, 	CTCP_INLINE | CTCP_NOLIMIT,
		"transmit simple_encrypted_data ciphertext using a sha256 key",
		do_crypto, 	do_crypto },

	/* The uncommon ones */
	{ "PING", 	CTCP_PING, 	CTCP_REPLY | CTCP_TELLUSER,
		"returns the arguments it receives",
		do_ping, 	do_ping_reply },
	{ "ECHO", 	CTCP_ECHO, 	CTCP_REPLY | CTCP_TELLUSER,
		"returns the arguments it receives",
		do_echo, 	NULL },
	{ "UTC",	CTCP_UTC, 	CTCP_INLINE | CTCP_NOLIMIT,
		"substitutes the local timezone",
		do_utc, 	do_utc },
	{ "CLIENTINFO",	CTCP_CLIENTINFO,CTCP_REPLY | CTCP_TELLUSER,
		"gives information about available CTCP commands",
		do_clientinfo, 	NULL },
	{ "USERINFO",	CTCP_USERINFO, 	CTCP_REPLY | CTCP_TELLUSER,
		"returns user settable information",
		do_userinfo, 	NULL },
	{ "ERRMSG",	CTCP_ERRMSG, 	CTCP_REPLY | CTCP_TELLUSER,
		"returns error messages",
		do_echo, 	NULL },
	{ "FINGER",	CTCP_FINGER, 	CTCP_REPLY | CTCP_TELLUSER,
		"shows real name, login name and idle time of user", 
		do_finger, 	NULL },
	{ "TIME",	CTCP_TIME, 	CTCP_REPLY | CTCP_TELLUSER,
		"tells you the time on the user's host",
		do_time, 	NULL },
	{ NULL,		CTCP_CUSTOM,	CTCP_REPLY | CTCP_TELLUSER,
		NULL,
		NULL, NULL }
};

static const char	*ctcp_type[] =
{
	"PRIVMSG",
	"NOTICE"
};

/* This is set to one if we parsed an SED */
int     sed = 0;

/*
 * in_ctcp_flag is set to true when IRCII is handling a CTCP request.  This
 * is used by the ctcp() sending function to force NOTICEs to be used in any
 * CTCP REPLY 
 */
int	in_ctcp_flag = 0;


/**************************** CTCP PARSERS ****************************/

/********** INLINE EXPANSION CTCPS ***************/
/*
 * do_crypto: Performs strong decryption for ctcp.  Returns in a malloc 
 * string the decryped message (if a key is set for that user) or the text 
 * "[ENCRYPTED MESSAGE]" 
 */
CTCP_HANDLER(do_crypto)
{
	Crypt	*key = NULL;
	const char	*crypt_who;
	char 	*tofrom;
	char	*ret = NULL, *ret2 = NULL;

	if (*from == '=')		/* DCC CHAT message */
		crypt_who = from;
	else if (is_me(from_server, to))
		crypt_who = from;
	else
		crypt_who = to;

	tofrom = malloc_strdup3(to, ",", from);
	malloc_strcat2_c(&tofrom, "!", FromUserHost, NULL);

	if ((key = is_crypted(tofrom, from_server, ctcp->id)) ||
	    (key = is_crypted(crypt_who, from_server, ctcp->id)))
		ret = decrypt_msg(cmd, key);

	new_free(&tofrom);

	if (!key || !ret) {
		sed = 2;
		malloc_strcpy(&ret2, "[ENCRYPTED MESSAGE]");
	} else {
		char *extra = NULL;

		/* 
		 * There might be a CTCP message in there,
		 * so we see if we can find it.
		 */
		if (get_server_doing_privmsg(from_server))
			ret2 = malloc_strdup(do_ctcp(from, to, ret));
		else if (get_server_doing_notice(from_server))
			ret2 = malloc_strdup(do_notice_ctcp(from, to, ret));
		else
		{
			ret2 = ret;
			ret = NULL;
		}

		/*
		 * We must recode to UTF8
		 */
                inbound_recode(from, from_server, to, ret2, &extra);
                if (extra)
		{
			new_free(&ret2);
			ret2 = extra;
		}

		sed = 1;
	}

	new_free(&ret);
	return ret2;
}

CTCP_HANDLER(do_utc)
{
	if (!cmd || !*cmd)
		return malloc_strdup(empty_string);

	return malloc_strdup(my_ctime(my_atol(cmd)));
}


/*
 * do_atmosphere: does the CTCP ACTION command --- done by lynX
 * Changed this to make the default look less offensive to users
 * who don't like it and added a /on ACTION. This is more in keeping
 * with the design philosophy behind IRCII
 */
CTCP_HANDLER(do_atmosphere)
{
	int	l;
	int	flag, fflag;

	if (!cmd || !*cmd)
		return NULL;

	/* Xavier mentioned that we should allow /ignore #chan action */
	flag = check_ignore_channel(from, FromUserHost, to, LEVEL_ACTION);
	fflag = new_check_flooding(from, FromUserHost, 
					is_channel(to) ? to : NULL,
					cmd, LEVEL_ACTION);

	if (flag == IGNORED || fflag == 1)
		return NULL;

	if (is_channel(to))
	{
		l = message_from(to, LEVEL_ACTION);
		if (do_hook(ACTION_LIST, "%s %s %s", from, to, cmd))
		{
			if (is_current_channel(to, from_server))
				put_it("* %s %s", from, cmd);
			else
				put_it("* %s:%s %s", from, to, cmd);
		}
	}
	else
	{
		l = message_from(from, LEVEL_ACTION);
		if (do_hook(ACTION_LIST, "%s %s %s", from, to, cmd))
			put_it("*> %s %s", from, cmd);
	}

	pop_message_from(l);
	return NULL;
}

/*
 * do_dcc: Records data on an incoming DCC offer. Makes sure it's a
 *	user->user CTCP, as channel DCCs don't make any sense whatsoever
 */
CTCP_HANDLER(do_dcc)
{
	char	*type;
	char	*description;
	char	*inetaddr;
	char	*port;
	char	*size;
	char	*extra_flags;

	if (!is_me(from_server, to) && *from != '=')
		return NULL;

	if     (!(type = next_arg(cmd, &cmd)) ||
		!(description = (get_int_var(DCC_DEQUOTE_FILENAMES_VAR)
				? new_next_arg(cmd, &cmd)
				: next_arg(cmd, &cmd))) ||
		!(inetaddr = next_arg(cmd, &cmd)) ||
		!(port = next_arg(cmd, &cmd)))
			return NULL;

	size = next_arg(cmd, &cmd);
	extra_flags = next_arg(cmd, &cmd);

	register_dcc_offer(from, type, description, inetaddr, port, size, extra_flags, cmd);
	return NULL;
}



/*************** REPLY-GENERATING CTCPS *****************/

/*
 * do_clientinfo: performs the CLIENTINFO CTCP.  If cmd is empty, returns the
 * list of all CTCPs currently recognized by IRCII.  If an arg is supplied,
 * it returns specific information on that CTCP.  If a matching CTCP is not
 * found, an ERRMSG ctcp is returned 
 */
CTCP_HANDLER(do_clientinfo)
{
	int	i;

	if (cmd && *cmd)
	{
		for (i = 0; i < NUMBER_OF_CTCPS; i++)
		{
			if (my_stricmp(cmd, ctcp_cmd[i].name) == 0)
			{
				send_ctcp(CTCP_NOTICE, from, CTCP_CLIENTINFO, 
					"%s %s", 
					ctcp_cmd[i].name, ctcp_cmd[i].desc);
				return NULL;
			}
		}
		send_ctcp(CTCP_NOTICE, from, CTCP_ERRMSG,
				"%s: %s is not a valid function",
				ctcp_cmd[CTCP_CLIENTINFO].name, cmd);
	}
	else
	{
		char buffer[BIG_BUFFER_SIZE + 1];
		*buffer = '\0';

		for (i = 0; i < NUMBER_OF_CTCPS; i++)
		{
			strlcat(buffer, ctcp_cmd[i].name, sizeof buffer);
			strlcat(buffer, " ", sizeof buffer);
		}
		send_ctcp(CTCP_NOTICE, from, CTCP_CLIENTINFO,
			"%s :Use CLIENTINFO <COMMAND> to get more specific information", 
			buffer);
	}
	return NULL;
}

/* do_version: does the CTCP VERSION command */
CTCP_HANDLER(do_version)
{
	char	*tmp;

	/*
	 * The old way seemed lame to me... let's show system name and
	 * release information as well.  This will surely help out
	 * experts/gurus answer newbie questions.  -- Jake [WinterHawk] Khuon
	 *
	 * For the paranoid, UNAME_HACK hides the gory details of your OS.
	 */
#if defined(HAVE_UNAME) && !defined(UNAME_HACK)
	struct utsname un;
	const char	*the_unix,
			*the_version;

	if (uname(&un) < 0)
	{
		the_version = empty_string;
		the_unix = "unknown";
	}
	else
	{
		the_version = un.release;
		the_unix = un.sysname;
	}

	send_ctcp(CTCP_NOTICE, from, CTCP_VERSION, 
			"ircII %s %s %s - %s", 
			irc_version, the_unix, the_version,
			(tmp = get_string_var(CLIENT_INFORMATION_VAR)) ? 
				tmp : IRCII_COMMENT);
#else
	send_ctcp(CTCP_NOTICE, from, CTCP_VERSION, 
			"ircII %s *IX - %s", 
			irc_version,
			(tmp = get_string_var(CLIENT_INFORMATION_VAR)) ? 
				tmp : IRCII_COMMENT);
#endif
	return NULL;
}

/* do_time: does the CTCP TIME command --- done by Veggen */
CTCP_HANDLER(do_time)
{
	send_ctcp(CTCP_NOTICE, from, CTCP_TIME, 
			"%s", my_ctime(time(NULL)));
	return NULL;
}

/* do_userinfo: does the CTCP USERINFO command */
CTCP_HANDLER(do_userinfo)
{
	char *tmp;

	send_ctcp(CTCP_NOTICE, from, CTCP_USERINFO, "%s", 
		(tmp = get_string_var(USER_INFORMATION_VAR)) ? tmp : "<No User Information>");
	return NULL;
}

/*
 * do_echo: does the CTCP ECHO command. Does not send an error if the
 * CTCP was sent to a channel.
 */
CTCP_HANDLER(do_echo)
{
	if (!is_channel(to))
		send_ctcp(CTCP_NOTICE, from, ctcp->id, "%s", cmd);
	return NULL;
}

CTCP_HANDLER(do_ping)
{
	send_ctcp(CTCP_NOTICE, from, CTCP_PING, "%s", cmd ? cmd : empty_string);
	return NULL;
}


/* 
 * Does the CTCP FINGER reply 
 */
CTCP_HANDLER(do_finger)
{
	struct	passwd	*pwd;
	time_t	diff;
	char	*tmp;
	char	*ctcpuser,
		*ctcpfinger;
	const char	*my_host;
	char	userbuff[NAME_LEN + 1];
	char	gecosbuff[NAME_LEN + 1];

	if ((my_host = get_server_userhost(from_server)) &&
			strchr(my_host, '@'))
		my_host = strchr(my_host, '@') + 1;
	else
		my_host = hostname;

	diff = time(NULL) - idle_time.tv_sec;

	if (!(pwd = getpwuid(getuid())))
		return NULL;

#ifndef GECOS_DELIMITER
#define GECOS_DELIMITER ','
#endif

#if defined(ALLOW_USER_SPECIFIED_LOGIN)
	if ((ctcpuser = getenv("IRCUSER"))) 
		strlcpy(userbuff, ctcpuser, sizeof userbuff);
	else
#endif
	{
		if (pwd->pw_name)
			strlcpy(userbuff, pwd->pw_name, sizeof userbuff);
		else
			strlcpy(userbuff, "epic-user", sizeof userbuff);
	}

#if defined(ALLOW_USER_SPECIFIED_LOGIN)
	if ((ctcpfinger = getenv("IRCFINGER"))) 
		strlcpy(gecosbuff, ctcpfinger, sizeof gecosbuff);
	else
#endif
	      if (pwd->pw_gecos)
		strlcpy(gecosbuff, pwd->pw_gecos, sizeof gecosbuff);
	else
		strlcpy(gecosbuff, "Esteemed EPIC User", sizeof gecosbuff);
	if ((tmp = strchr(gecosbuff, GECOS_DELIMITER)) != NULL)
		*tmp = 0;

	send_ctcp(CTCP_NOTICE, from, CTCP_FINGER, 
		"%s (%s@%s) Idle %ld second%s", 
		gecosbuff, userbuff, my_host, diff, plural(diff));

	return NULL;
}


/* 
 * If we recieve a CTCP DCC REJECT in a notice, then we want to remove
 * the offending DCC request
 */
CTCP_HANDLER(do_dcc_reply)
{
	char *subcmd = NULL;
	char *type = NULL;

	if (is_channel(to))
		return NULL;

	if (cmd && *cmd)
		subcmd = next_arg(cmd, &cmd);
	if (cmd && *cmd)
		type = next_arg(cmd, &cmd);

	if (subcmd && type && !strcmp(subcmd, "REJECT"))
		dcc_reject(from, type, cmd);

	return NULL;
}


/*
 * Handles CTCP PING replies.
 */
CTCP_HANDLER(do_ping_reply)
{
	Timeval t;
	time_t 	tsec = 0, 
		tusec = 0, 
		orig;
	char *	ptr;

	if (!cmd || !*cmd)
		return NULL;		/* This is a fake -- cant happen. */

	orig = my_atol(cmd);
	get_time(&t);

	/* Reply must be between time we started and right now */
	if (orig < start_time.tv_sec || orig > t.tv_sec)
	{
		say("Invalid CTCP PING reply [%s] dropped.", cmd);
		return NULL;
	}

	tsec = t.tv_sec - orig;

	if ((ptr = sindex(cmd, " .")))
	{
		*ptr++ = 0;
		tusec = t.tv_usec - my_atol(ptr);
	}

	/*
	 * 'cmd' is a pointer to the inside of do_ctcp's 'the_ctcp' buffer
	 * which is IRCD_BUFFER_SIZE bytes big; cmd points to (allegedly)
	 * the sixth position.  But just be paranoid and assume half that, 
	 * so we will always be safe.
	 */
	snprintf(cmd, IRCD_BUFFER_SIZE / 2, "%f seconds", 
			(float)(tsec + (tusec / 1000000.0)));
	return NULL;
}


/************************************************************************/
/*
 * do_ctcp: a re-entrant form of a CTCP parser.  The old one was lame,
 * so i took a hatchet to it so it didnt suck.
 *
 * XXXX - important!  The third argument -- 'str', is expected to be
 * 'BIG_BUFFER_SIZE + 1' or larger.  If it isnt, chaos will probably 
 * ensue if you get spammed with lots of CTCP UTC requests.
 *
 * UTC requests can be at minimum 5 bytes, and the expansion is always 24.
 * That means you can cram (510 - strlen("PRIVMSG x :") / 5) UTCs (100)
 * into a privmsg.  That means itll expand to 2400 characters.  We silently
 * limit the number of valid CTCPs to 4.  Anything more than that we dont
 * even bother with. (4 * 24 + 11 -> 106), which is less than
 * IRCD_BUFFER_SIZE, which gives us plenty of safety.
 *
 * XXXX - The normal way of implementation required two copies -- once into a
 * temporary buffer, once back into the original buffer -- for the best case
 * scenario.  This is horrendously inefficient, since most privmsgs dont
 * contain any CTCPs.  So we check to see if there are any CTCPs in the
 * message before we bother doing anything.  THIS IS AN INELEGANT HACK!
 * But the call to charcount() is less expensive than even one copy to 
 * strlcpy() since they both evaluate *each* character, and charcount()
 * doesnt have to do a write unless the character is present.  So it is 
 * definitely worth the cost to save CPU time for 99% of the PRIVMSGs.
 */
char *	do_ctcp (const char *from, const char *to, char *str)
{
	int 	flag;
	int	fflag;
	char 	local_ctcp_buffer [BIG_BUFFER_SIZE + 1],
		the_ctcp          [IRCD_BUFFER_SIZE + 1],
		last              [IRCD_BUFFER_SIZE + 1];
	char	*ctcp_command,
		*ctcp_argument;
	int	i;
	char	*ptr = NULL;
	int	allow_ctcp_reply = 1;
static	time_t	last_ctcp_parsed = 0;
	int	l;

	int delim_char = charcount(str, CTCP_DELIM_CHAR);

	if (delim_char < 2)
		return str;		/* No CTCPs. */
	if (delim_char > 8)
		allow_ctcp_reply = 0;	/* Historical limit of 4 CTCPs */

	flag = check_ignore_channel(from, FromUserHost, to, LEVEL_CTCP);
	fflag = new_check_flooding(from, FromUserHost, is_channel(to) ? to : NULL,
						str, LEVEL_CTCP);

	in_ctcp_flag++;
	strlcpy(local_ctcp_buffer, str, sizeof(local_ctcp_buffer) - 2);

	for (;;strlcat(local_ctcp_buffer, last, sizeof(local_ctcp_buffer) - 2))
	{
		if (split_CTCP(local_ctcp_buffer, the_ctcp, last))
			break;		/* All done! */

		if (!*the_ctcp)
			continue;	/* Empty requests are ignored */

		/*
		 * Apply some integrety rules:
		 * -- If we've already replied to a CTCP, ignore it.
		 * -- If user is ignoring sender, ignore it.
		 * -- If we're being flooded, ignore it.
		 * -- If CTCP was a global msg, ignore it.
		 */

		/*
		 * Yes, this intentionally ignores "unlimited" CTCPs like
		 * UTC and SED.  Ultimately, we have to make sure that
		 * CTCP expansions dont overrun any buffers that might
		 * contain this string down the road.  So by allowing up to
		 * 4 CTCPs, we know we cant overflow -- but if we have more
		 * than 40, it might overflow, and its probably a spam, so
		 * no need to shed tears over ignoring them.  Also makes
		 * the sanity checking much simpler.
		 */
		if (!allow_ctcp_reply)
			continue;

		/*
		 * Check to see if the user is ignoring person.
		 * Or if we're suppressing a flood.
		 */
		if (flag == IGNORED || fflag == 1)
		{
			if (x_debug & DEBUG_CTCPS)
				yell("CTCP from [%s] ignored", from);
			allow_ctcp_reply = 0;
			continue;
		}

		/*
		 * Check for CTCP flooding
		 */
		if (get_int_var(NO_CTCP_FLOOD_VAR))
		{
		    if (time(NULL) - last_ctcp_parsed < 2)
		    {
			/*
			 * This extends the flood protection until
			 * we dont get a CTCP for 2 seconds.
			 */
			last_ctcp_parsed = time(NULL);
			allow_ctcp_reply = 0;
			if (x_debug & DEBUG_CTCPS)
				say("CTCP flood from [%s] ignored", from);
			continue;
		    }
		}

		/*
		 * Check for global message
		 */
		if (*to == '$' || (*to == '#' && !im_on_channel(to, from_server)))
		{
			allow_ctcp_reply = 0;
			continue;
		}


		/*
		 * Now its ok to parse the CTCP.
		 * First we remove the argument.
		 * XXX - CTCP spec says word delim MUST be space.
		 */
		ctcp_command = the_ctcp;
		ctcp_argument = strchr(the_ctcp, ' ');
		if (ctcp_argument)
			*ctcp_argument++ = 0;
		else
			ctcp_argument = endstr(the_ctcp);

		/* Set up the window level/logging */
		if (im_on_channel(to, from_server))
			l = message_from(to, LEVEL_CTCP);
		else
			l = message_from(from, LEVEL_CTCP);

		/*
		 * Then we look for the correct CTCP.
		 */
		for (i = 0; i < NUMBER_OF_CTCPS; i++)
			if (!strcmp(ctcp_command, ctcp_cmd[i].name))
				break;

		/*
		 * We didnt find it?
		 */
		if (i == NUMBER_OF_CTCPS)
		{
			/*
			 * Offer it to the user.
			 * Maybe they know what to do with it.
			 */
			if (do_hook(CTCP_REQUEST_LIST, "%s %s %s %s",
				from, to, ctcp_command, ctcp_argument))
			    if (do_hook(CTCP_LIST, "%s %s %s %s", from, to, 
						ctcp_command, ctcp_argument))
			    {
				    say("Unknown CTCP %s from %s to %s: %s%s",
					ctcp_command, from, to, 
					*ctcp_argument ? ": " : empty_string, 
					ctcp_argument);
			    }
			time(&last_ctcp_parsed);
			allow_ctcp_reply = 0;
			pop_message_from(l);
			continue;
		}

		/* 
		 * We did find it.  Acknowledge it.
		 */
		ptr = NULL;
		if (do_hook(CTCP_REQUEST_LIST, "%s %s %s %s",
				from, to, ctcp_command, ctcp_argument))
		{
			ptr = ctcp_cmd[i].func(ctcp_cmd + i, from, 
						to, ctcp_argument);
		}

		/*
		 * If this isnt an 'unlimited' CTCP, set up flood protection.
		 *
		 * No, this wont allow users to flood any more than they
		 * would normally.  The UTC/SED gets converted into a 
		 * regular privmsg body, which is flagged via FLOOD_PUBLIC.
		 */
		if (!(ctcp_cmd[i].flag & CTCP_NOLIMIT))
		{
			time(&last_ctcp_parsed);
			allow_ctcp_reply = 0;
		}


		/*
		 * We've only gotten to this point if its a valid CTCP
		 * query and we decided to parse it.
		 */

		/*
		 * If its an ``INLINE'' CTCP, we paste it back in.
		 */
		if (ctcp_cmd[i].flag & CTCP_INLINE)
			strlcat(local_ctcp_buffer, ptr ? ptr : empty_string, sizeof local_ctcp_buffer);

		/* 
		 * If its ``INTERESTING'', tell the user.
		 * Note that this isnt mutex with ``INLINE'' in theory,
		 * even though it is in practice.  Dont use 'else' here.
		 */
		if (ctcp_cmd[i].flag & CTCP_TELLUSER)
		{
		    if (do_hook(CTCP_LIST, "%s %s %s %s", 
				from, to, ctcp_command, ctcp_argument))
		    {
			    if (is_me(from_server, to))
				say("CTCP %s from %s%s%s", 
					ctcp_command, from, 
					*ctcp_argument ? ": " : empty_string, 
					ctcp_argument);
			    else
				say("CTCP %s from %s to %s%s%s",
					ctcp_command, from, to, 
					*ctcp_argument ? ": " : empty_string, 
					ctcp_argument);
		    }
		}
		new_free(&ptr);
		pop_message_from(l);
	}

	in_ctcp_flag--;

	/* 
	 * 'str' is required to be BIG_BUFFER_SIZE + 1 or bigger per the API.
	 */
	strlcpy(str, local_ctcp_buffer, BIG_BUFFER_SIZE);
	return str;
}



/*
 * do_notice_ctcp: a re-entrant form of a CTCP reply parser.
 * See the implementation notes in do_ctcp().
 */
char *	do_notice_ctcp (const char *from, const char *to, char *str)
{
	int 	flag;
	char 	local_ctcp_buffer [BIG_BUFFER_SIZE + 1],
		the_ctcp          [IRCD_BUFFER_SIZE + 1],
		last              [IRCD_BUFFER_SIZE + 1];
	char	*ctcp_command,
		*ctcp_argument;
	int	i;
	char	*ptr;
	int	allow_ctcp_reply = 1;
	int	l;

	int delim_char = charcount(str, CTCP_DELIM_CHAR);

	if (delim_char < 2)
		return str;		/* No CTCPs. */
	if (delim_char > 8)
		allow_ctcp_reply = 0;	/* Ignore all the CTCPs. */

	/* We handle ignore, but not flooding (obviously) */
	flag = check_ignore_channel(from, FromUserHost, to, LEVEL_CTCP);
	in_ctcp_flag++;
	strlcpy(local_ctcp_buffer, str, sizeof(local_ctcp_buffer) - 2);

	for (;;strlcat(local_ctcp_buffer, last, sizeof(local_ctcp_buffer) - 2))
	{
		if (split_CTCP(local_ctcp_buffer, the_ctcp, last))
			break;		/* All done! */

		if (!*the_ctcp)
			continue;	/* Empty requests are ignored */

		/*
		 * The logic of all this is essentially the same as 
		 * do_ctcp
		 */

		if (!allow_ctcp_reply)
			continue;

		if (flag == IGNORED)
		{
			if (x_debug & DEBUG_CTCPS)
				yell("CTCP REPLY from [%s] ignored", from);
			allow_ctcp_reply = 0;
			continue;
		}

		/* But we don't check ctcp flooding (obviously) */

		/* Global messages -- just drop the CTCP */
		if (*to == '$' || (is_channel(to) && 
					!im_on_channel(to, from_server)))
		{
			allow_ctcp_reply = 0;
			continue;
		}


		/*
		 * Parse CTCP message
		 * CTCP spec says word delim MUST be space
		 */
		ctcp_command = the_ctcp;
		ctcp_argument = strchr(the_ctcp, ' ');
		if (ctcp_argument)
			*ctcp_argument++ = 0;
		else
			ctcp_argument = endstr(the_ctcp);

		/* Set up the window level/logging */
		if (is_channel(to))
			l = message_from(to, LEVEL_CTCP);
		else
			l = message_from(from, LEVEL_CTCP);

		/* 
		 * Find the correct CTCP and run it.
		 */
		for (i = 0; i < NUMBER_OF_CTCPS; i++)
			if (!strcmp(ctcp_command, ctcp_cmd[i].name))
				break;

		/* 
		 * If its a built in CTCP command, check to see if its
		 * got a reply handler, call if appropriate.
		 */
		if (i < NUMBER_OF_CTCPS && ctcp_cmd[i].repl)
		{
		    if ((ptr = ctcp_cmd[i].repl(ctcp_cmd + i, from, to, 
						ctcp_argument)))
		    {
			strlcat(local_ctcp_buffer, ptr, 
					sizeof local_ctcp_buffer);
			new_free(&ptr);
			pop_message_from(l);
			continue;
		    }
		}

		/* Toss it at the user.  */
		if (ctcp_cmd[i].flag & CTCP_TELLUSER)
		{
		    if (do_hook(CTCP_REPLY_LIST, "%s %s %s %s", 
					from, to, ctcp_command, ctcp_argument))
			say("CTCP %s reply from %s: %s", 
					ctcp_command, from, ctcp_argument);
		}
		if (!(ctcp_cmd[i].flag & CTCP_NOLIMIT))
			allow_ctcp_reply = 0;

		pop_message_from(l);
	}

	in_ctcp_flag--;

	/* 
	 * local_ctcp_buffer is derived from 'str', so its always
	 * smaller or equal in size to 'str', so this copy is safe.
	 */
	strlcpy(str, local_ctcp_buffer, BIG_BUFFER_SIZE);
	return str;
}



/* in_ctcp: simply returns the value of the ctcp flag */
int	in_ctcp (void) { return (in_ctcp_flag); }



/*
 * This is no longer directly sends information to its target.
 * As part of a larger attempt to consolidate all data transmission
 * into send_text, this function was modified so as to use send_text().
 * This function can send both direct CTCP requests, as well as the
 * appropriate CTCP replies.  By its use of send_text(), it can send
 * CTCPs to DCC CHAT and irc nickname peers, and handles encryption
 * transparantly.  This greatly reduces the logic, complexity, and
 * possibility for error in this function.
 */
void	send_ctcp (int type, const char *to, int datatag, const char *format, ...)
{
	char 	putbuf [BIG_BUFFER_SIZE + 1],
		*putbuf2;
	int	len;
	int	l;
	const char *pb;
	char	*extra = NULL;

	/* Make sure that the final \001 doesnt get truncated */
	if ((len = IRCD_BUFFER_SIZE - (12 + strlen(to))) <= 0)
		return;				/* Whatever. */
	putbuf2 = alloca(len);

	l = message_from(to, LEVEL_CTCP);
	if (format)
	{
		va_list args;
		va_start(args, format);
		vsnprintf(putbuf, BIG_BUFFER_SIZE, format, args);
		va_end(args);

		pb = outbound_recode(to, from_server, putbuf, &extra);

		do_hook(SEND_CTCP_LIST, "%s %s %s %s", 
				ctcp_type[type], to, 
				ctcp_cmd[datatag].name, pb);
		snprintf(putbuf2, len, "%c%s %s%c", 
				CTCP_DELIM_CHAR, 
				ctcp_cmd[datatag].name, pb, 
				CTCP_DELIM_CHAR);

		new_free(&extra);
	}
	else
	{
		do_hook(SEND_CTCP_LIST, "%s %s %s", 
				ctcp_type[type], to, 
				ctcp_cmd[datatag].name);
		snprintf(putbuf2, len, "%c%s%c", 
				CTCP_DELIM_CHAR, 
				ctcp_cmd[datatag].name, 
				CTCP_DELIM_CHAR);
	}

	/* XXX - Ugh.  What a hack. */
	putbuf2[len - 2] = CTCP_DELIM_CHAR;
	putbuf2[len - 1] = 0;

	send_text(from_server, to, putbuf2, ctcp_type[type], 0);
	pop_message_from(l);
}


int 	get_ctcp_val (char *str)
{
	int i;

	for (i = 0; i < NUMBER_OF_CTCPS; i++)
		if (!strcmp(str, ctcp_cmd[i].name))
			return i;

	/*
	 * This is *dangerous*, but it works.  The only place that
	 * calls this function is edit.c:ctcp(), and it immediately
	 * calls send_ctcp().  So the pointer that is being passed
	 * to us is globally allocated at a level higher then ctcp().
	 * so it wont be bogus until some time after ctcp() returns,
	 * but at that point, we dont care any more.
	 */
	ctcp_cmd[CTCP_CUSTOM].name = str;
	return CTCP_CUSTOM;
}



/*
 * XXXX -- some may call this a hack, but if youve got a better
 * way to handle this job, id love to use it.
 */
static int split_CTCP (char *raw_message, char *ctcp_dest, char *after_ctcp)
{
	char 	*ctcp_start, 
		*ctcp_end;

	*ctcp_dest = *after_ctcp = 0;

	if (!(ctcp_start = strchr(raw_message, CTCP_DELIM_CHAR)))
		return -1;		/* No CTCPs present. */
	*ctcp_start++ = 0;

	if (!(ctcp_end = strchr(ctcp_start, CTCP_DELIM_CHAR)))
	{
		*--ctcp_start = CTCP_DELIM_CHAR; /* Revert change */
		return -1;		 /* No CTCPs present after all */
	}
	*ctcp_end++ = 0;

	strlcpy(ctcp_dest, ctcp_start, IRCD_BUFFER_SIZE - 1);
	strlcpy(after_ctcp, ctcp_end, IRCD_BUFFER_SIZE - 1);
	return 0;		/* All done! */
}

