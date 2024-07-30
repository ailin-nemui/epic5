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
#include "list.h"
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
#include "words.h"
#include "functions.h"

#include <pwd.h>
#ifdef HAVE_UNAME
# include <sys/utsname.h>
#endif

/* CTCP BITFLAGS */
#define CTCP_SPECIAL	1	/* Special handlers handle everything and don't return anything */
#define CTCP_ORDINARY   2	/* Ordinary handlers either return a inline value or you should tell the user */
#define CTCP_REPLACE_ARGS   4	/* A "replace args" CTCP rewrites the args, but still needs to be "handled" normally */
#define CTCP_ACTIVE	8	/* Whether to handle requests (or ignore them) */
#define CTCP_RAW	32	/* Requires the original payload, not a recoded message */
#define CTCP_RESTARTABLE 64	/* Requires the CTCP processing to be restarted after handling */

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

static int	in_ctcp = 0;

/*
 * lookup_ctcp - Convert a CTCP name into a CTCP index.
 *
 * Arguments:
 *	name - the name of a CTCP to be searched for
 *
 * Return value:
 *	-1	- 'name' does not map to an internal CTCP
 *	>= 0	- 'name' refers to an internal CTCP, an integer 'r' 
 *		  such that CTCP(r) and CTCP_NAME(r) refer to that CTCP
 */
static int	lookup_ctcp (const char *name)
{
	int	i;

	for (i = 0; i < ctcp_bucket->numitems; i++)
		if (my_stricmp(name, CTCP_NAME(i)) == 0)
			return i;

	return -1;
}


/*
 * To make it easier on myself, I use a macro to ensure ctcp handler C functions
 * are always prototyped correctly.
 */
#define CTCP_HANDLER(x) \
static char * x (const char *from, const char *to, const char *cmd, char *args)

static	void	add_ctcp (const char *name, int flag, const char *desc, CTCP_Handler func, CTCP_Handler repl, const char *user_func, const char *user_repl)
{
	CtcpEntry *ctcp;
	char *	name_copy;

	ctcp = (CtcpEntry *)new_malloc(sizeof(CtcpEntry));
	ctcp->flag = flag;
	ctcp->flag |= CTCP_ACTIVE;		/* By default all CTCPs start as active */
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

	/* The 'name' belongs to the bucket, so it must be malloc()ed */
	name_copy = malloc_strdup(name);
	add_to_bucket(ctcp_bucket, name_copy, ctcp);
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
	List	*key = NULL;
	const char	*crypt_who;
	char 	*tofrom = NULL;
	char	*ret = NULL;
	char 	*extra = NULL;

	if (*from == '=')		/* DCC CHAT message */
		crypt_who = from;
	else if (is_me(from_server, to))
		crypt_who = from;
	else
		crypt_who = to;

	malloc_sprintf(&tofrom, "%s,%s!%s", nonull(to), nonull(from), nonull(FromUserHost));

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
	if (get_server_doing_privmsg(from_server) > 0)
		extra = malloc_strdup(do_ctcp(1, from, to, ret));
	else if (get_server_doing_notice(from_server) > 0)
		extra = malloc_strdup(do_ctcp(0, from, to, ret));
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
 * CTCP ACTION - Creates a special "ACTION" level message
 * 		Does not reply.
 *		The original CTCP ACTION done by lynX
 */
CTCP_HANDLER(do_atmosphere)
{
	int	l;
	int	ignore;

	if (!args || !*args)
		return NULL;

	/* Xavier mentioned that we should allow /ignore #chan action */
	ignore = check_ignore_channel(from, FromUserHost, to, LEVEL_ACTION);

	if (ignore == IGNORED)
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


/************************************************************************/
/*
 * split_CTCP - Extract a CTCP out of a message body
 *
 * Arguments:
 *	raw_message -- A message, either a PRIVMSG, NOTICE, or DCC CHAT.
 *			- If the message contains a CTCP, then the string
 *			  will be truncated to the part before the CTCP.
 *			- If the message does not contain a CTCP, it is
 *			  unchanged.
 *	ctcp_dest   -- A buffer (of size IRCD_BUFFER_SIZE)
 *			- If the message contains a CTCP, then the CTCP
 *			  itself (without the CTCP_DELIMs) will be put
 *			  in here.
 *			- If the message does not contain a CTCP, it is
 *			  unchanged
 *	after_ctcp  -- A buffer (of size IRCD_BUFFER_SIZE)
 *			- If the message contains a CTCP, then the part
 *			  of the message after the CTCP will be put in here
 *			- If the message does not contain a CTCP, it is
 *			  unchanged
 *
 * Return value:
 *	-1	- No CTCP was found.  All parameters are unchanged
 *	 0	- A CTCP was found.  All three parameters were changed
 */
static int split_CTCP (char *raw_message, char *ctcp_dest, char *after_ctcp)
{
	char 	*ctcp_start, 
		*ctcp_end;

	*ctcp_dest = *after_ctcp = 0;

	if (!(ctcp_start = strchr(raw_message, CTCP_DELIM_CHAR)))
		return -1;		/* No CTCPs present. */

	if (!(ctcp_end = strchr(ctcp_start + 1, CTCP_DELIM_CHAR)))
		return -1;		 /* No CTCPs present after all */

	*ctcp_start++ = 0;
	*ctcp_end++ = 0;

	strlcpy(ctcp_dest, ctcp_start, IRCD_BUFFER_SIZE - 1);
	strlcpy(after_ctcp, ctcp_end, IRCD_BUFFER_SIZE - 1);
	return 0;		/* All done! */
}


/*
 * do_ctcp - Remove and process all CTCPs within a message
 *
 * Arguments:
 *	request - Am i processing a request or a response?
 *		   1 = This is a PRIVMSG or DCC CHAT (a request)
 *		   0 = This is a NOTICE (a response)
 *	from	- Who sent the CTCP
 *	to	- Who received the CTCP (nick, channel, wall)
 *	str	- The message we received. (may be modified)
 *		  This must be at least BIG_BUFFER_SIZE+1 or bigger.
 *
 * Return value:
 *	'str' is returned.  
 *	'str' may be modified.
 *	It is guaranteed that 'str' shall contain no CTCPs upon return.
 */
char *	do_ctcp (int request, const char *from, const char *to, char *str)
{
	int 	flag;
	char 	local_ctcp_buffer [BIG_BUFFER_SIZE + 1],
		the_ctcp          [IRCD_BUFFER_SIZE + 1],
		after             [IRCD_BUFFER_SIZE + 1];
	char	*ctcp_command,
		*ctcp_argument;
	char 	*original_ctcp_argument;
	int	i;
	char	*ptr = NULL;
	int	dont_process_more = 0;
	int	l;
	char *	extra = NULL;
	int 	delim_char;

	/*
	 * Messages with less than 2 CTCP delims don't have a CTCP in them.
	 * Messages with > 8 delims are probably rogue/attack messages.
	 * We can save a lot of cycles by heading those off at the pass.
	 */
	delim_char = charcount(str, CTCP_DELIM_CHAR);
	if (delim_char < 2)
		return str;		/* No CTCPs. */
	if (delim_char > 8)
		dont_process_more = 1;	/* Historical limit of 4 CTCPs */


	/*
	 * Ignored CTCP messages, or requests during a flood, are 
	 * removed, but not processed.
	 * Although all CTCPs are subject to IGNORE, and requests are subject
	 * to flood control; we must apply these restrictions on the inside
	 * of the loop, for each CTCP we see.
	 */
	flag = check_ignore_channel(from, FromUserHost, to, LEVEL_CTCP);

	/* /IGNOREd messages are removed but not processed */
	if (flag == IGNORED)
		dont_process_more = 1;

	/* Messages sent to global targets are removed but not processed */
	if (*to == '$' || (*to == '#' && !im_on_channel(to, from_server)))
		dont_process_more = 1;



	/* Set up the window level/logging */
	if (im_on_channel(to, from_server))
		l = message_from(to, LEVEL_CTCP);
	else
		l = message_from(from, LEVEL_CTCP);


	/* For each CTCP we extract from 'local_ctcp_buffer'.... */
	strlcpy(local_ctcp_buffer, str, sizeof(local_ctcp_buffer) - 2);
	for (;;new_free(&extra), strlcat(local_ctcp_buffer, after, sizeof(local_ctcp_buffer) - 2))
	{
		/* Extract next CTCP. If none found, we're done! */
		if (split_CTCP(local_ctcp_buffer, the_ctcp, after))
			break;		/* All done! */

		/* If the CTCP is empty (ie, ^A^A), ignore it.  */
		if (!*the_ctcp)
			continue;

		/* If we're removing-but-not-processing CTCPs, ignore it */
		if (dont_process_more)
			continue;


		/* * * */
		/* Seperate the "command" from the "argument" */
		ctcp_command = the_ctcp;
		if ((ctcp_argument = strchr(the_ctcp, ' ')))
			*ctcp_argument++ = 0;
		else
			ctcp_argument = endstr(the_ctcp);

		/*
		 * rfc1459_any_to_utf8 specifically ignores CTCPs, because
		 * recoding binary data (such as an encrypted message) would
		 * corrupt the message.  
		 *
		 * So some CTCPs are "recodable" and some are not.
		 *
		 * The CTCP_RAW is set for any CTCPs which are NOT
		 * to be recoded prior to handling.  These are the encryption
		 * CTCPS.
		 *
		 * For the NORECORD ctcps, we save "original_ctcp_argument"
		 * For everybody else, 'ctcp_argument' is recoded.
		 */
		original_ctcp_argument = ctcp_argument;
		inbound_recode(from, from_server, to, ctcp_argument, &extra);
		if (extra)
			ctcp_argument = extra;

		/* 
		 * Offer it to the user FIRST.
		 * CTCPs handled via /on CTCP_REQUEST are treated as 
		 * ordinary "i sent a reply" CTCPs 
		 */
		if (request)
		{
			in_ctcp++;

			/* If the user "handles" it, then we're done with it! */
			if (!do_hook(CTCP_REQUEST_LIST, "%s %s %s %s",
					from, to, ctcp_command, ctcp_argument))
			{
				in_ctcp--;
				dont_process_more = 1;
				continue;
			}

			in_ctcp--;
			/* 
			 * User did not "handle" it.  with /on ctcp_request.
			 * Let's continue on! 
			 */
		}

		/*
		 * Next, look for a built-in CTCP handler
		 */
		/* Does this CTCP have a built-in handler? */
		for (i = 0; i < ctcp_bucket->numitems; i++)
		{
			if (!strcmp(ctcp_command, CTCP_NAME(i)))
			{
				/* This counts only if there is a function to call! */
				if (request && (CTCP(i)->func || CTCP(i)->user_func))
					break;
				else if (!request && (CTCP(i)->repl || CTCP(i)->user_repl))
					break;
			}
		}

		/* There is a function to call. */
		if (i < ctcp_bucket->numitems)
		{
			if ((CTCP(i)->flag & CTCP_RAW))
				ctcp_argument = original_ctcp_argument;

			in_ctcp++;

			/* Call the appropriate callback (four-ways!) */
			if (request)
			{
			    /* Inactive CTCP requests are silently dropped */
			    if ((CTCP(i)->flag & CTCP_ACTIVE))
			    {
				if (CTCP(i)->user_func)
				{
					char *args = NULL;
					malloc_sprintf(&args, "%s %s %s %s", from, to, ctcp_command, ctcp_argument);
					ptr = call_lambda_function("CTCP", CTCP(i)->user_func, args);
					new_free(&args);
				}
				else if (CTCP(i)->func)
					ptr = CTCP(i)->func(from, to, ctcp_command, ctcp_argument);
			    }
			}
			else
			{
				if (CTCP(i)->user_repl)
				{
					char *args = NULL;
					malloc_sprintf(&args, "%s %s %s %s", from, to, ctcp_command, ctcp_argument);
					ptr = call_lambda_function("CTCP", CTCP(i)->user_repl, args);
					/* An empty string is the same as NULL here */
					if (!ptr || !*ptr)
						new_free(&ptr);
					new_free(&args);
				}
				else if (CTCP(i)->repl)
					ptr = CTCP(i)->repl(from, to, ctcp_command, ctcp_argument);
			}
			in_ctcp--;

			/***** Was the CTCP "handled"? *****/

			/*
			 * A CTCP that returns a value is either a 
			 *  - Argument replacer (CTCP_REPLACE_ARGS)  [CTCP PING]
			 *  - Whole-string replacer (default) [CTCP AES256-CBC]
			 *
			 * Whole-string replacers paste themselves back inline and
			 * then go around for another pass.
			 *
			 * Argument Replacers are still "handled" in the ordinary way.
			 */
			if (ptr)
			{
				if (CTCP(i)->flag & CTCP_REPLACE_ARGS)
				{
					/* 
					 * "extra" is where we stuck the original ctcp arguments.
					 * When a CTCP is handled ordinarily, it will retrieve
					 * the CTCP arguments from 'extra'.  So if we are replacing
					 * the arguments, it is only logical to put them in 'extra'.
					 *
					 * This works even if extra == NULL here (because there was
					 * no recoding), because we unconditionally reset
					 * ctcp_argument to extra below.
					 */
					malloc_strcpy(&extra, ptr);
					new_free(&ptr);
				}
				else
				{
					strlcat(local_ctcp_buffer, ptr, sizeof local_ctcp_buffer);
					new_free(&ptr);
					if (CTCP(i)->flag & CTCP_RESTARTABLE)
						continue; 
				}
			}

			/*
			 * A CTCP that does not return a value but is "special" (/me, /dcc)
			 * is considered "handled"
			 */
			if (CTCP(i)->flag & CTCP_SPECIAL)
				continue;

			/* Otherwise, let's continue on! */
		}

		/* Default handling -- tell the user about it */
		/* !!!! Don't remove this, without reading the comments above !!! */
		if (extra)
			ctcp_argument = extra;
		in_ctcp++;
		if (request)
		{
			if (do_hook(CTCP_LIST, "%s %s %s %s", from, to, 
						ctcp_command, ctcp_argument))
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
		else
		{
			if (do_hook(CTCP_REPLY_LIST, "%s %s %s %s", 
					from, to, ctcp_command, ctcp_argument))
				say("CTCP %s reply from %s: %s", 
						ctcp_command, from, ctcp_argument);

		}
		in_ctcp--;

		dont_process_more = 1;
	}

	/*
	 * When we are all done, 'local_ctcp_buffer' contains a message without
	 * any CTCPs in it!
	 *
	 * 'str' is required to be BIG_BUFFER_SIZE + 1 or bigger per the API.
	 */
	pop_message_from(l);
	strlcpy(str, local_ctcp_buffer, BIG_BUFFER_SIZE);
	return str;
}


/*
 * send_ctcp - Format and send a properly encoded CTCP message
 *
 * Arguments:
 *	request  - 1 - This is a CTCP request originating with the user
 *		   0 - This is a CTCP reply in response to a CTCP request
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
void	send_ctcp (int request, const char *to, const char *type, const char *format, ...)
{
	char *	putbuf2;
	int	len;
	int	l;
	const char *protocol;
static	time_t	last_ctcp_reply = 0;

	/* Make sure that the final \001 doesnt get truncated */
	if ((len = IRCD_BUFFER_SIZE - (12 + strlen(to))) <= 0)
		return;				/* Whatever. */
	putbuf2 = alloca(len);

	if (request)
		protocol = "PRIVMSG";
	else
		protocol = "NOTICE";

	/* 
	 * Enforce _outbound_ CTCP Response Flood Protection.
	 * To keep botnets from flooding us off by sending
	 * us a flurry of CTCP requests from different nicks,
	 * we refuse to send a CTCP response until 2 seconds
	 * of quiet has happened.
	 * 
	 * This only affects responses.  We never throttle
	 * requests.
	 */
	if (!request && get_int_var(NO_CTCP_FLOOD_VAR))
	{
		if (time(NULL) - last_ctcp_reply < 2)
		{
                        /*
                         * This extends the flood protection until
                         * we dont get a CTCP for 2 seconds.
                         */
                        last_ctcp_reply = time(NULL);
                        if (x_debug & DEBUG_CTCPS)
                                say("CTCP flood reply to [%s] dropped", to);
			return;
		}
	}

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


/*
 * In a slight departure from tradition, this ctl function is not object-oriented (restful)
 *
 *   $ctcpctl(SET <ctcp-name> REQUEST {code})
 *	Register {code} to be run when client gets a CTCP <ctcp-name> request.
 *	- If {code} returns a string, that string replaces the CTCP request in 
 *	  the message.  The user is not otherwise notified.
 *	- If {code} does not return a string, the CTCP is removed normally,
 *	  and the user is notified of the CTCP request normally
 *	This creates the new CTCP if necessary.
 *	Setting a user callback will supercede any internal callback.
 *
 *   $ctcpctl(SET <ctcp-name> RESPONSE {code})
 *	Register {code} to be run when client gets a CTCP <ctcp-name> reply.
 *	- If {code} returns a string, that string replaces the CTCP request in
 *	  the message.  The user is not otherwise notified.
 *	- If {code} does not return a string, the CTCP is removed normally,
 *	  and the user is notified of the CTCP response normally.
 *	Setting a user callback will supercede any internal callback.
 *
 *   $ctcpctl(SET <ctcp-name> DESCRIPTION <alias_name>)
 *	Each CTCP has a "description string", which is used by CTCP CLIENTINFO.
 * 
 *   $ctcpctl(SET <ctcp-name> SPECIAL 1|0)
 *	A CTCP can be "special", meaning it is entirely self-contained and acts 
 *	as a sink of data.  The two special CTCPs are CTCP ACTION (/me) and 
 *	CTCP DCC (/dcc).  You can make your own special DCCs, but be careful.
 *
 *   $ctcpctl(SET <ctcp-name> REPLACE_ARGS 1|0)
 *      A CTCP handler that returns a value is either a "replacer" or a "rewriter".
 *      A "Replacer" replaces the arguments to the CTCP (this is what CTCP PING does)
 *      A "Rewriter" replaces the entire CTCP with a different string (this is 
 *                  what crypto CTCPs do)
 *      When this is 1, it is a "replacer".  When this is 0, it is a "rewriter".
 *
 *   $ctcpctl(SET <ctcp-name> RAW 1|0)
 *	Ordinarily, everything in the client is a string and has to be subject
 *	to /encode recoding to make it utf-8 before your ircII code gets to 
 *	interact with it.  Some CTCPs, like the encryption ctcps, work on 
 *	raw/binary data that must not get recoded because it's not a string.  
 *	If RAW is turned on, you will get the raw CTCP-encoded binary 
 *	data, which you _must not_ pass	to /echo unless you like things to 
 *	break.
 *
 *   $ctcpctl(GET <ctcp-name> REQUEST)
 *   $ctcpctl(GET <ctcp-name> RESPONSE)
 *   $ctcpctl(GET <ctcp-name> DESCRIPTION)
 *   $ctcpctl(GET <ctcp-name> SPECIAL)
 *   $ctcpctl(GET <ctcp-name> RAW)
 *	Fetch information about a built-in CTCP.  If <ctcp-name> does not 
 *	represent an internal CTCP, or the value requested does not apply,
 * 	(such as a CTCP that does not have a user-defined REQUEST or 
 *	RESPONSE), the empty string is returned.
 *
 * Additionally:
 *
 *   $ctcpctl(ALL)
 *	Returns the names of all built-in/registered CTCPs
 */
BUILT_IN_FUNCTION(function_ctcpctl, input)
{
	char *	op;
	size_t	op_len;
	char *	ctcp_name;
	char *	field;
	int	i;

	GET_FUNC_ARG(op, input);
	op_len = strlen(op);

	if (!my_strnicmp(op, "ALL", op_len)) {
                char buffer[BIG_BUFFER_SIZE + 1];
                *buffer = '\0';

                for (i = 0; i < ctcp_bucket->numitems; i++)
                {
                        const char *name = CTCP_NAME(i);
                        strlcat(buffer, name, sizeof buffer);
                        strlcat(buffer, " ", sizeof buffer);
                }
		RETURN_FSTR(buffer);
	}
	else if (!my_strnicmp(op, "ACTIVE", op_len)) {
                char buffer[BIG_BUFFER_SIZE + 1];
                *buffer = '\0';

                for (i = 0; i < ctcp_bucket->numitems; i++)
                {
			/* Inactive CTCPs are excluded here */
			if ((CTCP(i)->flag) & CTCP_ACTIVE)
			{
				const char *name = CTCP_NAME(i);
				strlcat(buffer, name, sizeof buffer);
				strlcat(buffer, " ", sizeof buffer);
			}
                }
		RETURN_FSTR(buffer);
	}
	else if (!my_strnicmp(op, "INACTIVE", op_len)) {
                char buffer[BIG_BUFFER_SIZE + 1];
                *buffer = '\0';

                for (i = 0; i < ctcp_bucket->numitems; i++)
                {
			/* Only Inactive CTCPs are included here */
			if (!((CTCP(i)->flag) & CTCP_ACTIVE))
			{
				const char *name = CTCP_NAME(i);
				strlcat(buffer, name, sizeof buffer);
				strlcat(buffer, " ", sizeof buffer);
			}
                }
		RETURN_FSTR(buffer);
	}

	GET_FUNC_ARG(ctcp_name, input);
	GET_FUNC_ARG(field, input);

	upper(ctcp_name);
	i = lookup_ctcp(ctcp_name);

	if (!my_strnicmp(op, "SET", op_len)) {
		/* Boostrap a new built-in CTCP if necessary */
		if (i == -1)
		{
			add_ctcp(ctcp_name, CTCP_ORDINARY, ctcp_name, NULL, NULL, NULL, NULL);
			i = lookup_ctcp(ctcp_name);
		}

		if (!my_stricmp(field, "REQUEST")) {
			malloc_strcpy(&(CTCP(i)->user_func), input);
		} else if (!my_stricmp(field, "RESPONSE")) {
			malloc_strcpy(&(CTCP(i)->user_repl), input);
		} else if (!my_stricmp(field, "DESCRIPTION")) {
			malloc_strcpy(&(CTCP(i)->desc), input);
		} else if (!my_stricmp(field, "SPECIAL")) {
			if (!strcmp(input, one))
				CTCP(i)->flag = (CTCP(i)->flag | CTCP_SPECIAL);
			else if (!strcmp(input, zero))
				CTCP(i)->flag = (CTCP(i)->flag & ~CTCP_SPECIAL);
			else
				RETURN_EMPTY;
		} else if (!my_stricmp(field, "REPLACE_ARGS")) {
			if (!strcmp(input, one))
				CTCP(i)->flag = (CTCP(i)->flag | CTCP_REPLACE_ARGS);
			else if (!strcmp(input, zero))
				CTCP(i)->flag = (CTCP(i)->flag & ~CTCP_REPLACE_ARGS);
			else
				RETURN_EMPTY;
		} else if (!my_stricmp(field, "RAW")) {
			if (!strcmp(input, one))
				CTCP(i)->flag = (CTCP(i)->flag | CTCP_RAW);
			else if (!strcmp(input, zero))
				CTCP(i)->flag = (CTCP(i)->flag & ~CTCP_RAW);
			else
				RETURN_EMPTY;
		} else if (!my_stricmp(field, "ACTIVE")) {
			if (!strcmp(input, one))
				CTCP(i)->flag = (CTCP(i)->flag | CTCP_ACTIVE);
			else if (!strcmp(input, zero))
				CTCP(i)->flag = (CTCP(i)->flag & ~CTCP_ACTIVE);
			else
				RETURN_EMPTY;
		} else {
			RETURN_EMPTY;
		}

		RETURN_INT(1);
	} else if (!my_strnicmp(op, "GET", op_len)) {
		if (!my_stricmp(field, "REQUEST")) {
			RETURN_STR(CTCP(i)->user_func);
		} else if (!my_stricmp(field, "RESPONSE")) {
			RETURN_STR(CTCP(i)->user_repl);
		} else if (!my_stricmp(field, "DESCRIPTION")) {
			RETURN_STR(CTCP(i)->desc);
		} else if (!my_stricmp(field, "SPECIAL")) {
			RETURN_INT((CTCP(i)->flag & CTCP_SPECIAL) != 0);
		} else if (!my_stricmp(field, "RAW")) {
			RETURN_INT((CTCP(i)->flag & CTCP_RAW) != 0);
		} else if (!my_stricmp(field, "ACTIVE")) {
			RETURN_INT((CTCP(i)->flag & CTCP_ACTIVE) != 0);
		} else {
			RETURN_EMPTY;
		}
	} else {
		RETURN_EMPTY;
	}
}

int	init_ctcp (void)
{
	ctcp_bucket = new_bucket();

	/* Special/Internal CTCPs */
	add_ctcp("ACTION", 		CTCP_SPECIAL, 
				"contains action descriptions for atmosphere", 
				do_atmosphere, 	do_atmosphere, NULL, NULL);
	add_ctcp("DCC", 		CTCP_SPECIAL, 
				"requests a direct_client_connection", 
				do_dcc, 	do_dcc_reply, NULL, NULL);

	/* Strong Crypto CTCPs */
	add_ctcp("AESSHA256-CBC", 	CTCP_ORDINARY | CTCP_RAW | CTCP_RESTARTABLE,
				"transmit aes256-cbc ciphertext using a sha256 key",
				do_crypto, 	do_crypto, NULL, NULL );
	add_ctcp("AES256-CBC", 		CTCP_ORDINARY | CTCP_RAW | CTCP_RESTARTABLE,
				"transmit aes256-cbc ciphertext",
				do_crypto, 	do_crypto, NULL, NULL );
	add_ctcp("CAST128ED-CBC", 	CTCP_ORDINARY | CTCP_RAW | CTCP_RESTARTABLE,
				"transmit cast5-cbc ciphertext",
				do_crypto, 	do_crypto, NULL, NULL );
	add_ctcp("BLOWFISH-CBC", 	CTCP_ORDINARY | CTCP_RAW | CTCP_RESTARTABLE,
				"transmit blowfish-cbc ciphertext",
				do_crypto, 	do_crypto, NULL, NULL );
	add_ctcp("FISH", 		CTCP_ORDINARY | CTCP_RAW | CTCP_RESTARTABLE,
				"transmit FiSH (blowfish-ecb with sha256'd key) ciphertext",
				do_crypto, 	do_crypto, NULL, NULL );
	add_ctcp("SED", 		CTCP_ORDINARY | CTCP_RAW | CTCP_RESTARTABLE,
				"transmit simple_encrypted_data ciphertext",
				do_crypto, 	do_crypto, NULL, NULL );
	add_ctcp("SEDSHA", 		CTCP_ORDINARY | CTCP_RAW | CTCP_RESTARTABLE,
				"transmit simple_encrypted_data ciphertext using a sha256 key",
				do_crypto, 	do_crypto, NULL, NULL );

	return 0;
}

#if 0
void    help_topics_ctcp (FILE *f)
{
        int     x;                                                              

        for (x = 0; ctcp_cmd[x].name; x++)                            
                fprintf(f, "ctcp %s\n", ctcp_cmd[x].name);
}
#endif


