/* $EPIC: recode.c,v 1.7 2014/03/12 02:38:19 jnelson Exp $ */
/*
 * recode.c - Transcoding between string encodings
 * 
 * Copyright 2012, 2014 EPIC Software Labs.
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

#define NEED_SERVER_LIST
#include "irc.h"
#include "ircaux.h"
#include "output.h"
#include "parse.h"
#include "server.h"
#include <langinfo.h>
#include <locale.h>

/*
 * Here's the plan...
 *
 * * * * * PREFACE 
 *
 * There are three primary inbound sources of information:
 *	1. Stuff you type
 *	2. Scripts you /load
 *	3. Stuff you get from IRC
 * (there are other sources, like DCC CHAT, but those aren't handled yet)
 *
 * In an ideal world, everything would be in UTF-8, but we have to handle
 * data that is not in UTF-8 and we don't know what the encoding is. 
 * There is some research into auto-detection of encoding, but nothing that
 * was mature enough to use.
 *
 * So it falls on you, the user, to use the /ENCODING command to tell the
 * client what to do with things that are not in UTF-8.
 *
 * * * * * HANDLING THE CONSOLE (stuff you type, stuff you see)
 *
 * I've observed there are two types of people:
 *	1. People who don't use 8 bit characters and know nothing of locales
 *	   (lets call those people "English speakers")
 *	2. People who undestand exactly what locale they're using.
 *	   (lets call those people "The World")
 *
 * If you have a locale set up, the client will use that for the encoding
 * used by your terminal emulator.  For most of you, you're using UTF-8.
 * Everything should Just Work for UTF-8 users with properly set locales.
 *
 * For NON-UTF8 users, or those who don't have any idea what a locale is, 
 * the client has to do some fallback.  It's been my observation that 
 * English Speaking UTF-8 users have some idea what locales are, and are 
 * inclined to set up their locale to make it work right.  
 *
 * So my attention falls to people without locales.  I've decided the best
 * choice is to just assume they're using ISO-8859-1.
 *
 * If for any reason, the client doesn't guess right, you can overrule it
 * by using /ENCODING
 * 
 * Maybe you really are using UTF-8 but you don't have your locale set:
 *	/ENCODING console UTF-8
 *
 * Or maybe you're using something else, like KOI8-R
 *	/ENCODING console KOI8-R
 *
 * The client will expect you to type in the console encoding, and will
 * translate all output to the encoding before display.
 *
 * * * * * * HANDLING NON-UTF8, NON-DECLARED SCRIPTS
 *
 * However.... Scripts are not usually encoded in UTF8 because of ascii 
 * art.  Most EPIC5 scripts appear to be encoded in CP437.
 *
 * For scripts that are not encoded in UTF-8, epic needs to convert them.
 * Scripts can make this easy on themselves by using /load -encoding:
 *	if (word(2 $loadinfo()) != [pf]) { load -pf -encoding CP437 $word(1 $loadinfo()); return; }
 *
 * For scripts not in utf8 that don't declare an encoding, the client has to
 * assume *something*, and that is CP437.  This can be overruled with /ENCODING
 *	/ENCODING scripts CP437
 *
 * The benefit of all this is CP437 ascii art will now work correctly for all!
 *
 * * * * * * HANDLING NON-UTF8 STUFF ROM IRC
 * 
 * We receive messages from irc from two different types of sources:
 *	1. Servers (which have a dot in their name)
 *	2. Nicks (which do not have a dot in their name)
 *
 * By default, all outbound messages you send will be in UTF-8, and any 
 * incoming messages in UTF-8 will be handled automatically.
 *
 * There are times when someone on irc will send you a non-utf8 message,
 * or there are people you want to send non-utf8 messages to.
 *
 * Any non-utf8 message you receive from irc will be assumed to be ISO-8859-1
 * and can be overruled by :
 *	/ENCODING irc ISO-8859-1
 *
 * You can overrule this on a server/channel/nick basis:
 *
 * We know "irc.foo.com" uses ISO-8859-15
 *	/ENCODING irc.foo.com KOI8-R
 * or
 *	/ENCODING irc.foo.com/ KOI8-R
 *
 * Across all servers, every 'hop' we know uses ISO-8859-15
 *	/ENCODING hop ISO-8859-15
 * or
 *	/ENCODING /hop ISO-8859-15
 *
 * We only want to set it for 'hop' on EFNet:
 * (non-EFNet 'hop's would still be UTF-8 unless another rule said different)
 *	/ENCODING EFNet/hop ISO-8859-15
 *
 * We know that on #happyfuntime on EFNet, people use ISO-8859-8.
 *	/ENCODING EFNet/#happyfuntime ISO-8859-8
 *
 * The server part can be:
 *	1. A server refnum
 *	2. An "ourname" or "itsname" of the server
 *	3. The server's group
 *	4. Any of the server's altnames
 *	5. The server's 005 NETWORK value
 *
 * Both the server and slash are optional.  Leaving the server blank means
 * "all servers" and a leading slash is handy for nicknames that collide
 * with the builtin names
 * 
 * We know that the user with nick 'console' uses ISO-8859-15
 *	/ENCODING /console ISO-8859-15
 */

struct RecodeRule {
	char *	target;
	char *	encoding;
	iconv_t	inbound_handle;
	iconv_t outbound_handle;
};

typedef struct RecodeRule RecodeRule;

/* Sigh -- hard limits make me sad */
#define MAX_RECODING_RULES 128
RecodeRule **	recode_rules = NULL;

/*
 * init_recodings - Set up the three "magic" /RECODE rules at start-up
 *
 * Arguments:
 *	None
 * Return Value:
 *	None
 *
 * This function look at your local env vars to decide what codeset you
 * are using.  If you are using "US-ASCII" (7 bits only) then it defaults
 * to ISO-8859-1 because that is as reasonable a guess as anything.  If you
 * don't like that, set your locale. ;-)
 *
 * It sets up three magic encoding rules:
 * 1. "console" (input stuff -- whatever your locale says)
 * 2. "scripts" (non-utf8 scripts that don't declare encoding -- CP437)
 * 3. "irc" (non-utf8 stuff from irc you don't have a rule for -- ISO-8859-1)
 *
 * After this function returns, you can use the /RECODE command to set up 
 * extra rules that will handle stuff from irc.
 */ 
void	init_recodings (void)
{
	int	x;
	const char *	console_encoding;

	/* 
	 * By default use whatever the LC_ALL variable says.
	 * But if there is no LC_ALL variable, then the encoding
	 * will be "US-ASCII" at which point we will default to ISO-8859-1
	 * since that has the widest capability, plus, we can auto-detect
	 * if it is wrong at runtime.
	 */
	console_encoding = nl_langinfo(CODESET);
	if (!my_stricmp(console_encoding, "US-ASCII"))
		console_encoding = "ISO-8859-1";

	recode_rules = (RecodeRule **)new_malloc(sizeof(RecodeRule *) * MAX_RECODING_RULES);
	for (x = 0; x < MAX_RECODING_RULES; x++)
		recode_rules[x] = NULL;

	/* Rule 0 is "console" */
	recode_rules[0] = (RecodeRule *)new_malloc(sizeof(RecodeRule));
	recode_rules[0]->target = malloc_strdup("console");
	recode_rules[0]->encoding = malloc_strdup(console_encoding);
	recode_rules[0]->inbound_handle = 0;
	recode_rules[0]->outbound_handle = 0;

	/* Rule 1 is "scripts" */
	recode_rules[1] = (RecodeRule *)new_malloc(sizeof(RecodeRule));
	recode_rules[1]->target = malloc_strdup("scripts");
	recode_rules[1]->encoding = malloc_strdup("CP437");
	recode_rules[1]->inbound_handle = 0;
	recode_rules[1]->outbound_handle = 0;

	/* Rule 2 is "irc" */
	recode_rules[2] = (RecodeRule *)new_malloc(sizeof(RecodeRule));
	recode_rules[2]->target = malloc_strdup("irc");
	recode_rules[2]->encoding = malloc_strdup("ISO-8859-1");
	recode_rules[2]->inbound_handle = 0;
	recode_rules[2]->outbound_handle = 0;
}

/*
 * find_recoding - Return the encoding for 'target'.
 *		  NOTE - "target" is an EXACT match.  So it's only suitable
 *		  for finding magic targets (console/irc/scripts)
 *		  For non-exact matches, use decide_encoding().
 *
 * Arguments:
 *	target	- Either the string "console" or "scripts" or "irc"
 *		  In the future, will support servers, channels, and nicks
 *	inbound	- If not NULL, filled in with an (iconv_t *) you can
 *		  use to translate messages FROM this target.
 *	outbound - If not NULL, filled in with an (iconv_t *) you can
 *		  use to translate messages TO this target.
 *
 * Return value:
 *	The encoding we think 'target' is using.  The details of this will
 *	be expanded in the future.
 */
const char *	find_recoding (const char *target, iconv_t *inbound, iconv_t *outbound)
{
	int	x;

	/* Find the recode rule for "target" */
	for (x = 0; x < MAX_RECODING_RULES; x++)
	{
		if (!recode_rules[x])
			continue;	/* XXX or break; ? */
		if (!my_stricmp(target, recode_rules[x]->target))
			break;
	}

	/* Not found? */
	if (!recode_rules[x])
		return NULL;

	/* If requested, provide an (iconv_t) used for messages FROM person */
	if (inbound)
	{
		if (recode_rules[x]->inbound_handle == 0)
			recode_rules[x]->inbound_handle = iconv_open("UTF-8", recode_rules[x]->encoding);
		*inbound = recode_rules[x]->inbound_handle;
	}

	/* If requested, provide an (iconv_t) used for messages TO person */
	if (outbound)
	{
		if (recode_rules[x]->outbound_handle == 0)
		{
			char *str;
			str = malloc_strdup2(recode_rules[x]->encoding, "//TRANSLIT");
			recode_rules[x]->outbound_handle = iconv_open(str, "UTF-8");
			new_free(&str);
		}
		*outbound = recode_rules[x]->outbound_handle;
	}

	/* Return the encoding name itself */
	return recode_rules[x]->encoding;
}

/*
 * decide_encoding - Check recode rules and return the most appropriate one
 *
 * Arguments:
 *	from	 - Who sent the message.  If we're sending it, should be NULL.
 *	target	 - Who will receive message.  our nick/another nick/channel.
 *	server	 - What server sent to/received from
 *	code	 - A pointer where we can stash the (iconv_t) to use.
 *
 * Return Value:
 *	If "orig_msg" is a UTF-8 string, then NULL is returned, indicating
 *	that no recoding is required.
 *
 *	Otherwise, the following rules are applied:
 *	     1. /ENCODING server/nickname
 *	     2. /ENCODING /nickname
 *	     3. /ENCODING server/channel
 *	     4. /ENCODING /channel
 *	     5. /ENCODING server/
 *	     6. /ENCODING irc
 *
 * 	For now, I'm not going to permit wildcards, because I think they
 *	just make things too confusing.  "empty" parts indicate a wildcard.
 *	ie, "/ENCODING server/" is meant to be "/ENCODING server/<star>".
 *	Perhaps I will add syntactic sugar to recognize and ignore *'s.
 *
 *	"SERVER" is itself a tricky thing, because there are many ways to
 *	refer to a server.  We have a helper function that tells us whether
 *	the encoding rule *could* match the servref:
 *		1. A server refnum
 *		2. A server itsname
 *		3. A server ourname
 *		4. A server group
 *		5. A server altname
 *
 *	/ENCODING rules can be ambiguous -- there might be multiple rules
 *	that could apply.  Don't do this.  I can't guarantee which one will
 *	be used, and I don't want to make it complicated to figure that out.
 *
 */
static char *	decide_encoding (const unsigned char *from, const unsigned char *target, int server, iconv_t *code)
{
	int	i = 0;
	int	winner = -1;
	int	winning_score = -1;
	ServerInfo si;

	/*
	 * XXX Originally, I did 6 passes over the rules, doing lots
	 * of crazy complicated stuff, but that got too complicated.
	 * Now what I do is evaluate each rule ONCE and give it a score.
	 * Highest scoring rule is tracked, and final winner is used.
	 */
	for (i = 0; i < MAX_RECODING_RULES; i++)
	{
		char *	target_copy;
		char *	server_part = NULL;
		char *	target_part = NULL;
		int	this_score;

		if (!recode_rules[i])
			continue;	/* XXX or break;? */

		if (x_debug & DEBUG_RECODE)
			yell("Evaluating rule %d: %s", i, recode_rules[i]->target);

		/* Skip rules without targets */
		if (recode_rules[i]->target == NULL)
		{
			if (x_debug & DEBUG_RECODE)
				yell("No target.");
			continue;
		}

		if (!my_stricmp(recode_rules[i]->target, "irc"))
		{
			if (x_debug & DEBUG_RECODE)
				yell("irc magic rule ok");
			goto target_ok;
		}

		/**********************************************************/
		/* 
		 *
		 * 1. Split the "target" into a server part and
		 *     a target part.
		 *
		 */

		target_copy = LOCAL_COPY(recode_rules[i]->target);

		/* A channel is a channel! */
		if (is_channel(target_copy))
		{
			server_part = NULL;
			target_part = target_copy;
		}

		/* A number is a server! */
		if (is_number(target_copy))
		{
			server_part = target_copy;
			target_part = NULL;
		}

		/* 
		 * Anything that isn't a channel that contains a 
		 * slash is a [server]/[target]
		 */
		else if (strchr(target_copy, '/'))
		{
			server_part = target_copy;
			target_part = strchr(target_copy, '/');
			*target_part++ = 0;
		}

		/*
		 * Anything that isn't a channel or has a slash but
		 * has a dot in it, is a server
		 */
		else if (strchr(target_copy, '.'))
		{
			server_part = target_copy;
			target_part = NULL;
		}

		/* Everything else is a nickname. */
		else
		{
			server_part = NULL;
			target_part = target_copy;
		}

		if (server_part && !*server_part)
			server_part = NULL;
		if (target_part && !*target_part)
			target_part = NULL;

		if (x_debug & DEBUG_RECODE)
			yell("Server part [%s], target part [%s]", server_part, target_part);

		/**********************************************************/
		/*
		 *
		 * 2a. Is the Server acceptable?
		 *
		 */
		/* If the rule doesn't limit the server, then it's ok */
		/* If there is a server part, it must match our refnum */
		/* XXX We should be caching the ServerInfo here */
		if (server_part != NULL)
		{
			clear_serverinfo(&si);
			str_to_serverinfo(server_part, &si);
			if (!serverinfo_matches_servref(&si, server))
			{
				if (x_debug & DEBUG_RECODE)
					yell("Server part does not match expectations for %d"), server;
				continue;
			}
		}
		if (x_debug & DEBUG_RECODE)
			yell("Server part is ok");

		/*
		 *
		 * 2b. Is the Target acceptable?
		 *
		 */

		/* If the rule doesn't limit the target, then it's ok */
		if (target_part == NULL)
		{
			if (x_debug & DEBUG_RECODE)
				yell("No target part -- ok");
			goto target_ok;
		}

		/* 
		 * If I'm sending the message and this rule isn't to 
		 * whomever i'm sending it to, it's not valid.
		 */
		if (!from)
		{
			if (!my_stricmp(target_part, target))
			{
				if (x_debug & DEBUG_RECODE)
					yell("Outbound message - Target matches sender -- ok");
				goto target_ok;
			}

			/* Not acceptable */
			if (x_debug & DEBUG_RECODE)
				yell("Outbound message - Target does not match sender");
			continue;
		}

		/* 
		 * If I'm receiving the message, then the rule is ok if and
		 * only if the rule applies to the sender (nick) or target 
		 * (channel)
		 */
		else
		{
			if (!my_stricmp(target_part, target))
			{
				if (x_debug & DEBUG_RECODE)
					yell("Inbound message - Target matches recipient -- ok");
				goto target_ok;
			}

			if (!my_stricmp(target_part, from))
			{
				if (x_debug & DEBUG_RECODE)
					yell("inbound message - target matches sender -- ok");
				goto target_ok;
			}

			if (x_debug & DEBUG_RECODE)
				yell("Inbound message - not applicable");
			/* Not acceptable */
			continue;
		}

		if (x_debug & DEBUG_RECODE)
			yell("Other - Not acceptable");

		/* Not acceptable */
		continue;

target_ok:
		/**********************************************************/
		/*
		 *
		 * 3. Decide what the score for this rule should be
		 *     60. /ENCODING server/nickname
		 *     50. /ENCODING /nickname
		 *     40. /ENCODING server/channel
		 *     30. /ENCODING /channel
		 *     20. /ENCODING server/
 		 *     10. /ENCODING irc		(magic rule)
		 *
		 */
		if (server_part != NULL && target_part != NULL &&
					!is_channel(target_part))
			this_score = 60;
		else if (server_part == NULL && target_part != NULL &&
						!is_channel(target_part))
			this_score = 50;
		else if (server_part != NULL && target_part != NULL &&
						is_channel(target_part))
			this_score = 40;
		else if (server_part == NULL && target_part != NULL && 
						is_channel(target_part))
			this_score = 30;
		else if (server_part != NULL && target_part == NULL)
			this_score = 20;
		else if (!my_stricmp(recode_rules[i]->target, "irc"))
			this_score = 10;
		/* This is in case someone tries to be too clever */
		else
			this_score = -100;

		/**********************************************************/
		if (x_debug & DEBUG_RECODE)
			yell("rule %d has score %d", i, this_score);

		/*
		 * 
		 * 4. Decide if this is the best rule (so far)
		 *
		 */
		if (this_score > winning_score)
		{
			winner = i;
			winning_score = this_score;
			if (x_debug & DEBUG_RECODE)
				yell("Best rule so far");
		}
	}


	/*
	 * We must have picked *some* rule (even if it was just "irc")
	 */
	if (winner == -1)
		panic(1, "Did not find a recode rule for %d/%s/%s", 
				server, from, target);

	i = winner;

	/* If from == NULL, we are sending the message outbound */
	if (from == NULL)
	{
	    if (recode_rules[i]->outbound_handle == 0)
	    {
		char *str;
		str = malloc_strdup2(recode_rules[i]->encoding, "//TRANSLIT");
		recode_rules[i]->outbound_handle = iconv_open(str, "UTF-8");
		new_free(&str);
	    }

	    *code = recode_rules[i]->outbound_handle;
	}

	else
	{
	    if (recode_rules[i]->inbound_handle == 0)
		recode_rules[i]->inbound_handle = iconv_open("UTF-8", 
						recode_rules[i]->encoding);
	    *code = recode_rules[i]->inbound_handle;
	}

	return recode_rules[i]->encoding;
}

/*
 * outbound_recode - Prepare a UTF-8 message for sending to IRC, recoding
 *		     it to another encoding if necessary.
 *
 * Arguments:
 *	to	- Who the message will be sent to (channel or nick)
 *	server	- The server it will be sent through
 *	message - The message to be sent (must be in UTF-8)
 *	extra	- A pointer to NULL.  If the message is recoded, it will
 *		  be set to a pointer that must be new_free()d later.
 *
 * Return Value:
 *	The string "message" is returned, possibly in another encoding if
 *	  "server" and "to" are associated with a recoding rule.  If no
 *	  recoding is required, then 'message' itself is returned, and 
 *	  'extra' is unmodified.
 *	If "message" is recoded, the return value will be a malloc()ed pointer.
 *	  For your convenience, the value will be stored in 'extra' so you can
 *	  new_free() it later.  You must new_free() it later.
 */
const char *	outbound_recode (const char *to, int server, const char *message, char **extra)
{
	iconv_t	i;
	char *	encoding;
	char *	new_buffer;
	size_t	new_buffer_len;
	
	/* If there is no place to put the retval, don't do anything */
	if (!extra)
		return message;

	/* If no recoding is necessary, then we're done. */
	if (!(encoding = decide_encoding(NULL, to, server, &i)))
		return message;

	new_buffer = malloc_strdup(message);
	new_buffer_len = strlen(message) + 1;

	recode_with_iconv_t(i, &new_buffer, &new_buffer_len);

	*extra = new_buffer;
	return *extra;
}


/*
 * inbound_recode - Ensure a message received from irc is in UTF-8.
 *		    Non-UTF8 messages will be converted using an appropriate
 *		    /ENCODING rule (if available) or defaults (if necessary)
 *
 * Arguments:
 *	from	- Who sent the message
 *	server	- The server it was sent through
 *	to	- Who the message was sent to (channel or (our) nick)
 *	message - The message we received (of unknown encoding)
 *	extra	- A pointer to NULL.  If the message is recoded, it will
 *		  be set to a pointer that must be new_free()d later.
 *
 * Return Value:
 *	If "message" is a valid UTF-8 string, "message" is returned.  "extra"
 *	will not be modified.
 *
 *	If "message" is not a valid UTF-8 string, a new string containing
 *	"message" will be returned in UTF-8.  
 *	/ENCODING rules will be used to decide what encoding "message" is.
 *	For your convenience, the new pointer will be stored in 'extra' so you
 *	can new_free() it later.  You must new_free() it later.
 */
const char *	inbound_recode (const char *from, int server, const char *to, const char *message, char **extra)
{
	iconv_t	i;
	char *	msg;
	char *	encoding;
	char *	new_buffer;
	size_t	new_buffer_len;

	/* 
	 * XXX Some day, it'd be cool to do statistical analysis of the
	 * message to decide what encoding it is
	 */

	/* The easiest thing is to accept it if it's valid UTF-8 */
	msg = LOCAL_COPY(message);
	if (!invalid_utf8str(msg))
		return message;
	
	/* If there is no place to put the retval, don't do anything */
	if (!extra)
		return message;

	/* If no recoding is necessary, then we're done. */
	if (!(encoding = decide_encoding(from, to, server, &i)))
		return message;

	new_buffer = malloc_strdup(message);
	new_buffer_len = strlen(message) + 1;

	recode_with_iconv_t(i, &new_buffer, &new_buffer_len);

	*extra = new_buffer;
	return *extra;
}


/*
 * ucs_to_console - Return the unicode key point in whatever format is 
 * 		    suitable for the console's encoding
 *		    It works like ucs_to_utf8.
 *
 * Arguments:
 *	key	- A unicode code point
 *	utf8str	- Where to put the code point in the user's encoding
 *	utf8strsiz - How big utf8str is.
 */
int     ucs_to_console (u_32int_t codepoint, unsigned char *deststr, size_t deststrsiz)
{
	char	utf8str[16];
	size_t	utf8strsiz;
	iconv_t	xlat;
	int	n;
	char *	x;
	const char *	s;
	size_t	slen, xlen;

	utf8strsiz = ucs_to_utf8(codepoint, utf8str, 16) + 1;
	find_recoding("console", NULL, &xlat);

	s = utf8str;
	x = deststr;

	if ((n = iconv(xlat, &s, &utf8strsiz, &x, &deststrsiz)) != 0)
	{
		if (errno == EINVAL || errno == EILSEQ)
		{
			/* What to do? */
			return -1;
		}
	}
	return 0;
}

BUILT_IN_COMMAND(encoding)
{
	char *arg;
	const char *encoding;
	int	x;
	const char *server = NULL;
	const char *target = NULL;

	/* /ENCODING    	-> Output all rules */
	if (!(arg = next_arg(args, &args)))
	{
		for (x = 0; x < MAX_RECODING_RULES; x++)
		{
			if (!recode_rules[x])
				continue;
			say("Encoding for %s is %s", 
				recode_rules[x]->target,
				recode_rules[x]->encoding);
		}

		return;
	}

	/* /ENCODING rule	-> Output a single rule */
	if (!(encoding = next_arg(args, &args)))
	{
		for (x = 0; x < MAX_RECODING_RULES; x++)
		{
			if (!recode_rules[x])
				continue;
			if (!my_stricmp(arg, recode_rules[x]->target))
				say("Encoding for %s is %s", 
					recode_rules[x]->target,
					recode_rules[x]->encoding);
		}

		/* Show the encoding for TARGET */
		return;
	}

	/* Check to see if there is already a rule for this target */
	for (x = 0; x < MAX_RECODING_RULES; x++)
	{
		if (!recode_rules[x])
			continue;	/* XXX or break; ? */
		if (!my_stricmp(arg, recode_rules[x]->target))
			break;
	}

	if (!my_stricmp(encoding, "none"))
	{
		if (recode_rules[x])
		{
			iconv_close(recode_rules[x]->inbound_handle);
			recode_rules[x]->inbound_handle = 0;
			iconv_close(recode_rules[x]->outbound_handle);
			recode_rules[x]->outbound_handle = 0;
			new_free(&recode_rules[x]->encoding);
			new_free(&recode_rules[x]->target);
			new_free((char **)&recode_rules[x]);
			say("Removed encoding for %s", arg);
		}
		else
			say("There is no encoding for %s", arg);

		return;
	}

	/* If there is not already a rule, create a new (blank) one. */
	if (!recode_rules[x])
	{
		for (x = 0; x < MAX_RECODING_RULES; x++)
		{
			if (!recode_rules[x])
				break;
		}
		if (x == MAX_RECODING_RULES)
		{
			say("Sorry, no more room for recoding rules!");
			return;
		}
		recode_rules[x] = (RecodeRule *)new_malloc(sizeof(RecodeRule));
		recode_rules[x]->target = malloc_strdup(arg);
		recode_rules[x]->inbound_handle = 0;
		recode_rules[x]->outbound_handle = 0;
		recode_rules[x]->encoding = NULL;

		/* FALLTHROUGH -- set me up below here */
	}

	/* 
	 * Modify the existing (or newly created) rule 
	 */

	/* Save the new encoding */
	malloc_strcpy(&recode_rules[x]->encoding, encoding);

	/* Invalidate prior iconv handles */
	if (recode_rules[x]->inbound_handle != 0)
	{
		iconv_close(recode_rules[x]->inbound_handle);
		recode_rules[x]->inbound_handle = 0;
	}
	if (recode_rules[x]->outbound_handle != 0)
	{
		iconv_close(recode_rules[x]->outbound_handle);
		recode_rules[x]->outbound_handle = 0;
	}


	/* Declare what the new encoding is */
	say("Encoding for %s is now %s", recode_rules[x]->target, 
					 recode_rules[x]->encoding);
}


