/*
 * ctcp.c:handles the client-to-client protocol(ctcp). 
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright 1993, 2018 EPIC Software Labs
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
/* Major revamps in 1996 and 2018 */

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

/* CTCP BITFLAGS */
/*
 * TBD - Convert this to a lookup table for $ctcpctl()
 *
 * Each supported CTCP has these bitflags describing its behavior
 */
#define CTCP_SPECIAL	0	/* Special/Internal - handles everything itself */
#define CTCP_REPLY	1	/* Will send a reply to the requester */
#define CTCP_INLINE	2	/* Returns a value to replace the CTCP inline */
#define CTCP_NOLIMIT	4	/* Is NOT subject to ctcp flood control - ctcp handler should never suppress */
#define CTCP_TELLUSER	8	/* Does not tell the user - ctcp handler should do that */
#define CTCP_NORECODE	16	/* Recodes the message itself; ctcp handler should NOT do that */

/* CTCP ENTRIES */
/*
 * A CTCP Entry lists the built in CTCPs
 * Out of the box, the client comes with some CTCPs implemented as C functions.
 * You can add your own CTCP handlers with ircII aliases.
 *
 * "Why should I register a CTCP handler rather than using /on ctcp_request?"
 * you might ask.  There needs to be a way to script a CTCP handler that can
 * expand inline (such as CTCP UTC), and there's no good way to do that with
 * an /ON.
 *
 * CTCP Handlers (whether in C or ircII take 4 arguments:
 *	$0 - The sender of the CTCP
 *	$1 - The receiver of the CTCP (ie, you, or a channel)
 *	$2 - The kind of CTCP (ie, ACTION or VERSION or DCC)
 *	$3 - Arguments to the CTCP (not all CTCPs have arguments - can be NULL)
 */
typedef char *(*CTCP_Handler) (const char *, const char *, const char *, char *);
typedef	struct _CtcpEntry
{
	int		flag;		/* Action modifiers */
	char *		desc;  		/* description returned by ctcp clientinfo */
	CTCP_Handler 	func;		/* C function to handle requests */
	CTCP_Handler 	repl;		/* C function to handle replies */
	char *		user_func;	/* Block of code to handle requests */
	char *		user_repl;	/* Block of code to handle replies */
}	CtcpEntry;

/*
 * Let's review buckets real quick...
 * Buckets are an (insert-)ordered array of key-value pairs
 *
 * The bucket itself contains
 *	numitems	-> The number of items in the bucket
 *	list		-> An array of (BucketItem *)s, from (0 to numitems - 1)
 *
 * Each BucketItem is just a key-value pair:
 *	name -> (char *)
 *	stuff -> (void *)
 *
 * Thus, bucket->list[i]       is the i'th bucket item.
 *       bucket->list[i].name  is the key (name) of the i'th bucket item
 *       bucket->list[i].stuff is the value of the i'th bucket item.
 */

static	Bucket	*ctcp_bucket = NULL;

/* The name of a CTCP is now the Key of the BucketItem. it used to be in the value */
#define CTCP_NAME(i) ctcp_bucket->list[i].name

/* The value of a CTCP is the Value of the BucketItem. */
#define CTCP(i) ((CtcpEntry *)ctcp_bucket->list[i].stuff)


/*
 * To make it easier on myself, I use a macro to ensure ctcp handler C functions
 * are always prototyped correctly.
 */
#define CTCP_HANDLER(x) \
static char * x (const char *from, const char *to, const char *cmd, char *args)

#if 0
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
#endif

static	void	add_ctcp (const char *name, int flag, const char *desc, CTCP_Handler func, CTCP_Handler repl, const char *user_func, const char *user_repl)
{
	CtcpEntry *ctcp;
	int	numval;
	const char *strval;

	ctcp = (CtcpEntry *)new_malloc(sizeof(CtcpEntry));
	ctcp->flag = flag;
	ctcp->desc = malloc_strdup(desc);

	ctcp->func = func;
	ctcp->repl = repl;

	if (user_func)
		ctcp->user_func = malloc_strdup(user_func);
	else
		ctcp->user_func = NULL;
	if (user_repl)
		ctcp->user_repl = malloc_strdup(user_repl);
	else
		ctcp->user_repl = NULL;

	add_to_bucket(ctcp_bucket, name, ctcp);
}

/*
 * XXX This global variable is sadly used to tell other systems
 * about whether a CTCP resulted in an encrypted message.
 * (SED stands for "Simple Encrypted Data", which used to be the
 * only form of encryption).  There has not yet been designed
 * an easier way to pass this kind of info back to the handler
 * that has to decide whether to throw /on encrypted_privmsg or not.
 * Oh well.
 */
int     sed = 0;


/**************************** CTCP PARSERS ****************************/

/********** INLINE EXPANSION CTCPS ***************/
/*
 * do_crypt: Generalized decryption for /CRYPT targets
 *
 * Notes:
 *	This supports encryption over DCC CHAT (`from' will start with "=")
 *      If the CTCP was sent to a channel, then the peer is the "target".
 *      If the CTCP was not sent to a channel, then the peer is the sender.
 *
 * It will look up to see if you have a /crypt for the peer for the kind of
 * encryption.  If you do have a /crypt, it will decrypt the message.
 * If you do not have a /crypt, it will return "[ENCRYPTED MESSAGE]".
 */
CTCP_HANDLER(do_crypto)
{
	Crypt	*key = NULL;
	const char	*crypt_who;
	char 	*tofrom;
	char	*ret = NULL;
	char 	*extra = NULL;

	if (*from == '=')		/* DCC CHAT message */
		crypt_who = from;
	else if (is_me(from_server, to))
		crypt_who = from;
	else
		crypt_who = to;

	tofrom = malloc_strdup3(to, ",", from);
	malloc_strcat2_c(&tofrom, "!", FromUserHost, NULL);

	if ((key = is_crypted(tofrom, from_server, cmd)) ||
	    (key = is_crypted(crypt_who, from_server, cmd)))
		ret = decrypt_msg(args, key);

	new_free(&tofrom);

	/*
	 * Key would be NULL if someone sent us a rogue encrypted
	 * message (ie, we don't have a password).  Ret should never
	 * be NULL (but we can be defensive against the future).
	 * In either case, something went seriously wrong.
	 */
	if (!key || !ret) 
	{
		if (ret)
			new_free(&ret);

		sed = 2;
		malloc_strcpy(&ret, "[ENCRYPTED MESSAGE]");
		return ret;
	} 


	/*
	 * NOW WE HANDLE THE DECRYPTED MESSAGE....
	 */

	/*
	 * CTCP messages can be recursive (ie, a decrypted msg
	 * might yield another CTCP message), and so we must not
	 * recode until we have removed any sub-ctcps!
	 */
	if (get_server_doing_privmsg(from_server))
		extra = malloc_strdup(do_ctcp(from, to, ret));
	else if (get_server_doing_notice(from_server))
		extra = malloc_strdup(do_notice_ctcp(from, to, ret));
	else
	{
		extra = ret;
		ret = NULL;
	}

	new_free(&ret);
	ret = extra;
	extra = NULL;

	/*
	 * What we're left with is just the plain part of the CTCP.
	 * In rfc1459_any_to_utf8(), CTCP messages are specifically
	 * detected and ignored [because recoding binary data will
	 * corrupt the data].  But that does not mean the message
	 * doesn't need decoding -- it just needs to be done after
	 * the message is decrypted.
	 */
	inbound_recode(from, from_server, to, ret, &extra);

	/*
	 * If a recoding actually occurred, free the source string
	 * and then use the decoded string going forward.
	 */
	if (extra)
	{
		new_free(&ret);
		ret = extra;
	}

	sed = 1;
	return ret;
}

/*
 * CTCP UTC - Expands inline to your current time.
 *		Does not reply.
 */
CTCP_HANDLER(do_utc)
{
	if (!args || !*args)
		return malloc_strdup(empty_string);

	return malloc_strdup(my_ctime(my_atol(args)));
}


/*
 * CTCP ACTION - Creates a special "ACTION" level message
 * 		Does not reply.
 *		The original CTCP ACTION done by lynX
 */
CTCP_HANDLER(do_atmosphere)
{
	int	l;
	int	ignore, flood;

	if (!args || !*args)
		return NULL;

	/* Xavier mentioned that we should allow /ignore #chan action */
	ignore = check_ignore_channel(from, FromUserHost, to, LEVEL_ACTION);
	flood = new_check_flooding(from, FromUserHost, 
					is_channel(to) ? to : NULL,
					args, LEVEL_ACTION);

	if (ignore == IGNORED || flood)
		return NULL;

	if (is_channel(to))
	{
		l = message_from(to, LEVEL_ACTION);
		if (do_hook(ACTION_LIST, "%s %s %s", from, to, args))
		{
			if (is_current_channel(to, from_server))
				put_it("* %s %s", from, args);
			else
				put_it("* %s:%s %s", from, to, args);
		}
	}
	else
	{
		l = message_from(from, LEVEL_ACTION);
		if (do_hook(ACTION_LIST, "%s %s %s", from, to, args))
			put_it("*> %s %s", from, args);
	}

	pop_message_from(l);
	return NULL;
}

/*
 * CTCP DCC - Direct Client Connections (file transfers and private chats)
 *		Does not reply.
 *		Only user->user CTCP DCCs are acceptable.
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

	if     (!(type = next_arg(args, &args)) ||
		!(description = (get_int_var(DCC_DEQUOTE_FILENAMES_VAR)
				? new_next_arg(args, &args)
				: next_arg(args, &args))) ||
		!(inetaddr = next_arg(args, &args)) ||
		!(port = next_arg(args, &args)))
			return NULL;

	size = next_arg(args, &args);
	extra_flags = next_arg(args, &args);

	register_dcc_offer(from, type, description, inetaddr, port, size, extra_flags, args);
	return NULL;
}



/*************** REPLY-GENERATING CTCPS *****************/

/*
 * do_clientinfo: performs the CLIENTINFO CTCP.  If args is empty, returns the
 * list of all CTCPs currently recognized by IRCII.  If an arg is supplied,
 * it returns specific information on that CTCP.  If a matching CTCP is not
 * found, an ERRMSG ctcp is returned 
 */
CTCP_HANDLER(do_clientinfo)
{
	int	i;

	if (args && *args)
	{
		for (i = 0; i < ctcp_bucket->numitems; i++)
		{
			if (my_stricmp(args, CTCP_NAME(i)) == 0)
			{
				send_ctcp("NOTICE", from, "CLIENTINFO", "%s %s", CTCP_NAME(i), CTCP(i)->desc);
				return NULL;
			}
		}
		send_ctcp("NOTICE", from, "ERRMSG", "%s: %s is not a valid function", "CLIENTINFO", args);
	}
	else
	{
		char buffer[BIG_BUFFER_SIZE + 1];
		*buffer = '\0';

		for (i = 0; i < ctcp_bucket->numitems; i++)
		{
			const char *name = CTCP_NAME(i);
			strlcat(buffer, name, sizeof buffer);
			strlcat(buffer, " ", sizeof buffer);
		}
		send_ctcp("NOTICE", from, cmd, "%s :Use %s <COMMAND> to get more specific information", buffer, cmd);
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
	const char	*the_unix;

	if (uname(&un) < 0)
		the_unix = "unknown";
	else
		the_unix = un.sysname;

	/* We no longer show the detailed version of your OS. */
	send_ctcp("NOTICE", from, cmd, "ircII %s %s - %s", irc_version, the_unix, 
			(tmp = get_string_var(CLIENT_INFORMATION_VAR)) ? 
				tmp : IRCII_COMMENT);
#else
	send_ctcp("NOTICE", from, cmd, "ircII %s *IX - %s", irc_version,
			(tmp = get_string_var(CLIENT_INFORMATION_VAR)) ? 
				tmp : IRCII_COMMENT);
#endif
	return NULL;
}

/* do_time: does the CTCP TIME command --- done by Veggen */
CTCP_HANDLER(do_time)
{
	send_ctcp("NOTICE", from, cmd, "%s", my_ctime(time(NULL)));
	return NULL;
}

/* do_userinfo: does the CTCP USERINFO command */
CTCP_HANDLER(do_userinfo)
{
	char *tmp;

	send_ctcp("NOTICE", from, cmd, "%s", (tmp = get_string_var(USER_INFORMATION_VAR)) ? tmp : "<No User Information>");
	return NULL;
}

/*
 * do_echo: does the CTCP ECHO command. Does not send an error if the
 * CTCP was sent to a channel.
 */
CTCP_HANDLER(do_echo)
{
	if (!is_channel(to))
		send_ctcp("NOTICE", from, cmd, "%s", args);
	return NULL;
}

CTCP_HANDLER(do_ping)
{
	send_ctcp("NOTICE", from, cmd, "%s", args ? args : empty_string);
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

	send_ctcp("NOTICE", from, cmd, "%s (%s@%s) Idle %ld second%s", 
		gecosbuff, userbuff, my_host, diff, plural(diff));

	return NULL;
}


/* 
 * If we recieve a CTCP DCC REJECT in a notice, then we want to remove
 * the offending DCC request
 */
CTCP_HANDLER(do_dcc_reply)
{
	char *subargs = NULL;
	char *type = NULL;

	if (is_channel(to))
		return NULL;

	if (args && *args)
		subargs = next_arg(args, &args);
	if (args && *args)
		type = next_arg(args, &args);

	if (subargs && type && !strcmp(subargs, "REJECT"))
		dcc_reject(from, type, args);

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

	if (!args || !*args)
		return NULL;		/* This is a fake -- cant happen. */

	orig = my_atol(args);
	get_time(&t);

	/* Reply must be between time we started and right now */
	if (orig < start_time.tv_sec || orig > t.tv_sec)
	{
		say("Invalid CTCP PING reply [%s] dropped.", args);
		return NULL;
	}

	tsec = t.tv_sec - orig;

	if ((ptr = strchr(args, ' ')) || (ptr = strchr(args, '.')))
	{
		*ptr++ = 0;
		tusec = t.tv_usec - my_atol(ptr);
	}

	/*
	 * 'args' is a pointer to the inside of do_ctcp's 'the_ctcp' buffer
	 * which is IRCD_BUFFER_SIZE bytes big; args points to (allegedly)
	 * the sixth position.  But just be paranoid and assume half that, 
	 * so we will always be safe.
	 */
	snprintf(args, IRCD_BUFFER_SIZE / 2, "%f seconds", 
			(float)(tsec + (tusec / 1000000.0)));
	return NULL;
}


/************************************************************************/
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
	char *	extra = NULL;

	int delim_char = charcount(str, CTCP_DELIM_CHAR);

	if (delim_char < 2)
		return str;		/* No CTCPs. */
	if (delim_char > 8)
		allow_ctcp_reply = 0;	/* Historical limit of 4 CTCPs */

	flag = check_ignore_channel(from, FromUserHost, to, LEVEL_CTCP);
	fflag = new_check_flooding(from, FromUserHost, is_channel(to) ? to : NULL,
						str, LEVEL_CTCP);

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
		for (i = 0; i < ctcp_bucket->numitems; i++)
			if (!strcmp(ctcp_command, CTCP_NAME(i)))
				break;

		/*
		 * We didnt find it?
		 */
		if (i == ctcp_bucket->numitems)
		{
			/*
			 * Offer it to the user.
			 * Maybe they know what to do with it.
			 */
			if (do_hook(CTCP_REQUEST_LIST, "%s %s %s %s",
				from, to, ctcp_command, ctcp_argument))
			{
			    if (do_hook(CTCP_LIST, "%s %s %s %s", from, to, 
						ctcp_command, ctcp_argument))
			    {
				    say("Unknown CTCP %s from %s to %s: %s%s",
					ctcp_command, from, to, 
					*ctcp_argument ? ": " : empty_string, 
					ctcp_argument);
			    }
			}
			time(&last_ctcp_parsed);
			allow_ctcp_reply = 0;
			pop_message_from(l);
			continue;
		}

		/*
		 * rfc1459_any_to_utf8 specifically ignores CTCPs, because
		 * recoding binary data (such as an encrypted message) would
		 * corrupt the message.  
		 *
		 * So some CTCPs are "recodable" and some are not.
		 *
		 * The CTCP_NORECODE is set for any CTCPs which are NOT
		 * to be recoded prior to handling.  These are the encryption
		 * CTCPS.
		 *
		 * All other CTCPs have not been recoded by the time they
		 * reach here, so we must do it here!
		 */
		if (!(CTCP(i)->flag & CTCP_NORECODE))
		{
		   /*
		    * We must recode to UTF8
		    */
		   inbound_recode(from, from_server, to, ctcp_argument, &extra);
		   if (extra)
			ctcp_argument = extra;
		}

		/* 
		 * We did find it.  Acknowledge it.
		 */
		ptr = NULL;
		if (do_hook(CTCP_REQUEST_LIST, "%s %s %s %s",
				from, to, ctcp_command, ctcp_argument))
		{
			ptr = CTCP(i)->func(from, to, ctcp_command, ctcp_argument);
		}

		/*
		 * If this isnt an 'unlimited' CTCP, set up flood protection.
		 *
		 * No, this wont allow users to flood any more than they
		 * would normally.  The UTC/SED gets converted into a 
		 * regular privmsg body, which is flagged via FLOOD_PUBLIC.
		 */
		if (!(CTCP(i)->flag & CTCP_NOLIMIT))
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
		if (CTCP(i)->flag & CTCP_INLINE)
			strlcat(local_ctcp_buffer, ptr ? ptr : empty_string, sizeof local_ctcp_buffer);

		/* 
		 * If its ``INTERESTING'', tell the user.
		 * Note that this isnt mutex with ``INLINE'' in theory,
		 * even though it is in practice.  Dont use 'else' here.
		 */
		if (CTCP(i)->flag & CTCP_TELLUSER)
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
		new_free(&extra);
		new_free(&ptr);
		pop_message_from(l);
	}

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
		for (i = 0; i < ctcp_bucket->numitems; i++)
			if (!strcmp(ctcp_command, CTCP_NAME(i)))
				break;

		/* 
		 * If its a built in CTCP command, check to see if its
		 * got a reply handler, call if appropriate.
		 */
		if (i < ctcp_bucket->numitems && CTCP(i)->repl)
		{
		    if ((ptr = CTCP(i)->repl(from, to, ctcp_command, ctcp_argument)))
		    {
			strlcat(local_ctcp_buffer, ptr, 
					sizeof local_ctcp_buffer);
			new_free(&ptr);
			pop_message_from(l);
			continue;
		    }
		}

		/* Toss it at the user.  */
		if (CTCP(i)->flag & CTCP_TELLUSER)
		{
		    if (do_hook(CTCP_REPLY_LIST, "%s %s %s %s", 
					from, to, ctcp_command, ctcp_argument))
			say("CTCP %s reply from %s: %s", 
					ctcp_command, from, ctcp_argument);
		}
		if (!(CTCP(i)->flag & CTCP_NOLIMIT))
			allow_ctcp_reply = 0;

		pop_message_from(l);
	}

	/* 
	 * local_ctcp_buffer is derived from 'str', so its always
	 * smaller or equal in size to 'str', so this copy is safe.
	 */
	strlcpy(str, local_ctcp_buffer, BIG_BUFFER_SIZE);
	return str;
}



/*
 * send_ctcp - Format and send a properly encoded CTCP message
 *
 * Arguments:
 *	protocol - Either "PRIVMSG" for a request or "NOTICE" for a reply.
 *		   Other values will have undefined behavior.
 *	to	- The target to send the message to.
 *	type	- A string describing the CTCP being sent or replied to.
 *		  Previously this used to be an int into an array of strings,
 *		  but this is all free-form now.
 *	format  - NULL -- If the CTCP does not provide any arguments
 *		  A printf() format -- If the CTCP does provide any arguments
 *
 * Notes:
 *	Because we use send_text(), the following things happen automatically:
 *	  - We can CTCP any target, including DCC CHATs
 *	  - All encryption is honored
 *	We also honor all appropriate /encode-ings
 *
 * Example:
 *	To send a /me to a channel:
 *		send_ctcp("PRIVMSG", channel, "ACTION", "%s", message);
 */
void	send_ctcp (const char *protocol, const char *to, const char *type, const char *format, ...)
{
	char *	putbuf2;
	int	len;
	int	l;

	/* Make sure that the final \001 doesnt get truncated */
	if ((len = IRCD_BUFFER_SIZE - (12 + strlen(to))) <= 0)
		return;				/* Whatever. */
	putbuf2 = alloca(len);

	l = message_from(to, LEVEL_CTCP);
	if (format)
	{
		const char *pb;
		char *	extra = NULL;
		char 	putbuf [BIG_BUFFER_SIZE + 1];
		va_list args;

		va_start(args, format);
		vsnprintf(putbuf, BIG_BUFFER_SIZE, format, args);
		va_end(args);

		/*
		 * We only recode the ARGUMENTS because the base
		 * part of the CTCP is expected to be 7-bit ascii.
		 * This isn't strictly enforced, so if you send a
		 * CTCP message with a fancy type name, the behavior
		 * is unspecified.
		 */
		pb = outbound_recode(to, from_server, putbuf, &extra);

		do_hook(SEND_CTCP_LIST, "%s %s %s %s", 
				protocol, to, type, pb);
		snprintf(putbuf2, len, "%c%s %s%c", 
				CTCP_DELIM_CHAR, type, pb, CTCP_DELIM_CHAR);

		new_free(&extra);
	}
	else
	{
		do_hook(SEND_CTCP_LIST, "%s %s %s", 
				protocol, to, type);
		snprintf(putbuf2, len, "%c%s%c", 
				CTCP_DELIM_CHAR, type, CTCP_DELIM_CHAR);
	}

	/* XXX - Ugh.  What a hack. */
	putbuf2[len - 2] = CTCP_DELIM_CHAR;
	putbuf2[len - 1] = 0;

	send_text(from_server, to, putbuf2, protocol, 0, 1);
	pop_message_from(l);
}


#if 0
/*
 * In a slight departure from tradition, this ctl function is not object-oriented (restful)
 *
 *   $ctcpctl(SET <ctcp-name> NORMAL_REQUEST <alias_name>)
 *	A "Normal Request" means a CTCP in a PRIVMSG that (usually) elicits a reply.
 *	Examples of "normal requests" are DCC, VERSION, FINGER, PING, USERINFO
 *
 *   $ctcpctl(SET <ctcp-name> INLINE_REQUEST <alias_name>)
 *	A "Inline Request" means a CTCP in a PRIVMSG that does not expect a reply, but
 *	instead carries information that needs to be transformed for the user
 *	Examples of "inline requests" are ACTION (/me), UTC, and all the Crypto stuff
 *
 *   $ctcpctl(SET <ctcp-name> NORMAL_REPLY <alias_name>)
 *	A "Normal Reply" means a CTCP in a NOTICE that (usually) is a response to a
 *	CTCP "normal request" you made to someone else.  Since the default behavior 
 *	of CTCP Reply handling is to display it to the user, YOU USUALLY DO NOT NEED
 *	TO SPECIFY A REPLY HANDLER, unless you're doing something unusual.  The IRC
 *	protocol forbids a reply to a NOTICE, so you can't respond to a reply without
 *	doing something you shouldn't do.
 *	The only example of this is DCC REJECTs
 *
 *   $ctcpctl(SET <ctcp-name> INLINE_REPLY <alias_name>)
 *	A "Inline Reply" means a CTCP in a NOTICE that carries information that needs
 *	to be transformed for the user
 *	Examples of "inline replies" are ACTION (/me) and crypto messages in NOTICEs
 */
char *	ctcpctl	(char *input)
{
	char *	op;
	size_t	op_len;
	char *	ctcp_name;
	size_t	ctcp_name_len;
	char *	handle_type;
	size_t	handle_type_len;
	char *	alias_name;
	size_t	alias_name_len;

	GET_FUNC_ARG(op, input);
	op_len = strlen(op);

	if (!my_strnicmp(op, "SET", op_len)) {
	} else if (!my_strnicmp(op, "GET", op_len)) {

	if (!my_strnicmp(op, "NORMAL_REQUEST", len)) {
	} else if (!my_strnicmp(op, "INLINE_REQUEST", len)) {
	} else if (!my_strnicmp(op, "NORMAL_REPLY", len)) {
	} else if (!my_strnicmp(op, "INLINE_REPLY", len)) {
	}
}
#endif


#if 0
void    help_topics_ctcp (FILE *f)
{
        int     x;                                                              

        for (x = 0; ctcp_cmd[x].name; x++)                            
                fprintf(f, "ctcp %s\n", ctcp_cmd[x].name);
}
#endif

int	init_ctcp (void)
{
	ctcp_bucket = new_bucket();

	/* Special/Internal CTCPs */
	add_ctcp("ACTION", 		CTCP_SPECIAL | CTCP_NOLIMIT, 
				"contains action descriptions for atmosphere", 
				do_atmosphere, 	do_atmosphere, NULL, NULL);
	add_ctcp("DCC", 		CTCP_SPECIAL | CTCP_NOLIMIT, 
				"requests a direct_client_connection", 
				do_dcc, 	do_dcc_reply, NULL, NULL);

	/* Strong Crypto CTCPs */
	add_ctcp("AESSHA256-CBC", 	CTCP_INLINE | CTCP_NOLIMIT | CTCP_NORECODE,
				"transmit aes256-cbc ciphertext using a sha256 key",
				do_crypto, 	do_crypto, NULL, NULL );
	add_ctcp("AES256-CBC", 		CTCP_INLINE | CTCP_NOLIMIT | CTCP_NORECODE,
				"transmit aes256-cbc ciphertext",
				do_crypto, 	do_crypto, NULL, NULL );
	add_ctcp("CAST128ED-CBC", 	CTCP_INLINE | CTCP_NOLIMIT | CTCP_NORECODE,
				"transmit cast5-cbc ciphertext",
				do_crypto, 	do_crypto, NULL, NULL );
	add_ctcp("BLOWFISH-CBC", 	CTCP_INLINE | CTCP_NOLIMIT | CTCP_NORECODE,
				"transmit blowfish-cbc ciphertext",
				do_crypto, 	do_crypto, NULL, NULL );
	add_ctcp("FISH", 		CTCP_INLINE | CTCP_NOLIMIT | CTCP_NORECODE,
				"transmit FiSH (blowfish-ecb with sha256'd key) ciphertext",
				do_crypto, 	do_crypto, NULL, NULL );
	add_ctcp("SED", 		CTCP_INLINE | CTCP_NOLIMIT | CTCP_NORECODE,
				"transmit simple_encrypted_data ciphertext",
				do_crypto, 	do_crypto, NULL, NULL );
	add_ctcp("SEDSHA", 		CTCP_INLINE | CTCP_NOLIMIT | CTCP_NORECODE,
				"transmit simple_encrypted_data ciphertext using a sha256 key",
				do_crypto, 	do_crypto, NULL, NULL );

	/* Inline expando CTCPs */
	add_ctcp("UTC", 		CTCP_INLINE | CTCP_NOLIMIT,
				"substitutes the local timezone",
				do_utc, 	do_utc , NULL, NULL);

	/* Classic response-generating CTCPs */
	add_ctcp("VERSION", 		CTCP_REPLY | CTCP_TELLUSER,
				"shows client type, version and environment",
				do_version, 	NULL, NULL, NULL );
	add_ctcp("PING", 		CTCP_REPLY | CTCP_TELLUSER,
				"returns the arguments it receives",
				do_ping, 	do_ping_reply, NULL, NULL );
	add_ctcp("ECHO", 		CTCP_REPLY | CTCP_TELLUSER,
				"returns the arguments it receives",
				do_echo, 	NULL, NULL, NULL );
	add_ctcp("CLIENTINFO", 		CTCP_REPLY | CTCP_TELLUSER,
				"gives information about available CTCP commands",
				do_clientinfo, 	NULL, NULL, NULL );
	add_ctcp("USERINFO",		CTCP_REPLY | CTCP_TELLUSER,
				"returns user settable information",
				do_userinfo, 	NULL, NULL, NULL );
	add_ctcp("ERRMSG", 		CTCP_REPLY | CTCP_TELLUSER,
				"returns error messages",
				do_echo, 	NULL, NULL, NULL);
	add_ctcp("FINGER", 		CTCP_REPLY | CTCP_TELLUSER,
				"shows real name, login name and idle time of user", 
				do_finger, 	NULL, NULL, NULL );
	add_ctcp("TIME", 		CTCP_REPLY | CTCP_TELLUSER,
				"tells you the time on the user's host",
				do_time, 	NULL, NULL, NULL );
}


