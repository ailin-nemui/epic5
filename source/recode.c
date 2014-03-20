/* $EPIC: recode.c,v 1.13 2014/03/20 15:25:54 jnelson Exp $ */
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
 * (non-EFNet 'hop's would use the fallback 'irc' rule unless another rule 
 *  said different)
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

	char *	target_copy;
	char *	server_part;
	char *	target_part;
	ServerInfo si;

	iconv_t	inbound_handle;
	iconv_t outbound_handle;
	int	magic;		/* 0 - can be deleted; 1 - cannot be deleted */
};
typedef struct RecodeRule RecodeRule;

/*
 * XXX TODO -- There shouldn't be a hard limit on number of rules 
 */
#define MAX_RECODING_RULES 128
RecodeRule **	recode_rules = NULL;
static int	update_recoding_encoding (RecodeRule *r, const char *encoding);

/*
 * create_recoding_rule - Create a new Recoding Rule from scratch
 *
 * Arguments:
 *	target	 - The target this rule will apply to.  The syntax of this
 *		   argument is discussed extensively in this file.
 *	encoding - The encoding that this rule will use for this target.
 *		   MAY BE NULL -- See below
 *	magic	 - 0 if this is a user-created rule; 1 if this is a system rule
 *
 * Return Value:
 *	A new RecodeRule that you should assign to recode_rules[x], where x
 *	is any open slot in recode_rules.
 *
 * Notes:
 *	If "encoding" is NULL, then the new rule IS NOT COMPLETE and MUST
 *	NOT BE USED until you call update_recoding_encoding() FIRST!
 *	After you complete the new rule, you can assign to recode_rules.
 *	Assigning an incomplete rule to recode_rules will lead to a crash.
 */
static RecodeRule *	create_recoding_rule (const char *target, const char *encoding, int magic)
{
	RecodeRule *	r;
	char *	target_copy;

	r = (RecodeRule *)new_malloc(sizeof(RecodeRule));
	r->target = malloc_strdup(target);
	if (encoding)
		r->encoding = malloc_strdup(encoding);
	else
		r->encoding = NULL;
	r->server_part = NULL;
	r->target_part = NULL;
	clear_serverinfo(&r->si);
	r->inbound_handle = 0;
	r->outbound_handle = 0;


	/* 
	 * Turn "target" into "server_part" and "target_part"
	 * and "si"
	 * Magic rules do not get this treatment.
	 */
	if (magic == 0)
	{
		target_copy = r->target_copy = malloc_strdup(r->target);

		/* A channel is a channel! */
		if (is_channel(target_copy))
		{
			r->server_part = NULL;
			r->target_part = target_copy;
		}

		/* A number is a server! */
		if (is_number(target_copy))
		{
			r->server_part = target_copy;
			r->target_part = NULL;
		}

		/* 
		 * Anything that isn't a channel that contains a 
		 * slash is a [server]/[target]
		 */
		else if (strchr(target_copy, '/'))
		{
			r->server_part = target_copy;
			r->target_part = strchr(target_copy, '/');
			*r->target_part++ = 0;
		}

		/*
		 * Anything that isn't a channel or has a slash but
		 * has a dot in it, is a server
		 */
		else if (strchr(target_copy, '.'))
		{
			r->server_part = target_copy;
			r->target_part = NULL;
		}

		/* Everything else is a nickname. */
		else
		{
			r->server_part = NULL;
			r->target_part = target_copy;
		}

		if (r->server_part && !*r->server_part)
			r->server_part = NULL;
		if (r->target_part && !*r->target_part)
			r->target_part = NULL;

		if (x_debug & DEBUG_RECODE)
			yell("Server part [%s], target part [%s]", 
				r->server_part, r->target_part);

		/**********************************************************/
		/* If the rule doesn't limit the server, then it's ok */
		if (r->server_part != NULL)
		{
			clear_serverinfo(&r->si);
			str_to_serverinfo(r->server_part, &r->si);
		}
	}

	return r;
}

/*
 * update_recoding_encoding - Change the encoding of a new or existing rule
 *
 * Arguments:
 *	r	 - A RecodeRule that is either new or existing
 *	encoding - The encoding this rule should use.
 *
 * Return Value:
 *	returns 0;
 *
 * Note: If you call create_recoding_rule() with "encoding" == NULL, then 
 * 	you __MUST__ call this function immediately to set the encoding.  
 *	Failure to do so will probably call a NULL deref when the rule 
 *	actually gets evaluated.
 */
static int	update_recoding_encoding (RecodeRule *r, const char *encoding)
{
	/* Save the new encoding */
	malloc_strcpy(&r->encoding, encoding);

	/* Invalidate prior iconv handles */
	if (r->inbound_handle != 0)
	{
		iconv_close(r->inbound_handle);
		r->inbound_handle = 0;
	}
	if (r->outbound_handle != 0)
	{
		iconv_close(r->outbound_handle);
		r->outbound_handle = 0;
	}

	return 0;
}


/*
 * check_recoding_iconv - Return (iconv_t) handles for transcoding
 *
 * Arguments:
 *	r	 - A RecodeRule that you want to use
 *	inbound	 - A place to put an (iconv_t) if you're using this rule
 *		   to process an INCOMING message.  May be NULL.
 *	outbound - A place to put an (iconv_t) if you're using this rule
 *		   to process an OUTBOUND message.  May be NULL.
 *
 * Return Value:
 *	Returns the encoding as a string (r->encoding);
 */
static const char *	check_recoding_iconv (RecodeRule *r, iconv_t *inbound, iconv_t *outbound)
{
	/* If requested, provide an (iconv_t) used for messages FROM person */
	if (inbound)
	{
		if (r->inbound_handle == 0)
			r->inbound_handle = iconv_open("UTF-8", r->encoding);
		*inbound = r->inbound_handle;
	}

	/* If requested, provide an (iconv_t) used for messages TO person */
	if (outbound)
	{
		if (r->outbound_handle == 0)
		{
			char *str;
			str = malloc_strdup2(r->encoding, "//TRANSLIT");
			r->outbound_handle = iconv_open(str, "UTF-8");
			new_free(&str);
		}
		*outbound = r->outbound_handle;
	}

	return r->encoding;
}


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

	/*
	 * XXX TODO -- Pull out into a function to grow the rule set
	 */
	recode_rules = (RecodeRule **)new_malloc(sizeof(RecodeRule *) * MAX_RECODING_RULES);
	for (x = 0; x < MAX_RECODING_RULES; x++)
		recode_rules[x] = NULL;

	/* Rule 0 is "console" */
	recode_rules[0] = create_recoding_rule("console", console_encoding, 1);

	/* Rule 1 is "scripts" */
	recode_rules[1] = create_recoding_rule("scripts", "CP437", 1);

	/* Rule 2 is "irc" */
	recode_rules[2] = create_recoding_rule("irc", "ISO-8859-1", 1);
}


/*
 * find_recoding - Return the encoding for 'target'.
 *		  NOTE - "target" is an EXACT match.  So it's only suitable
 *		  for finding the magic targets (console/irc/scripts).
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

	/* Return the encoding name itself */
	return check_recoding_iconv(recode_rules[x], inbound, outbound);
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
 *	Returns the "encoding" of most appropriate rule.
 *	Stores into *code an (iconv_t) for the translation you want to do.
 *
 * Notes:
 *	Recode rules are evaluated for the "best match", given this priority.
 *	     6. /ENCODING server/nickname
 *	     5. /ENCODING /nickname
 *	     4. /ENCODING server/channel
 *	     3. /ENCODING /channel
 *	     2. /ENCODING server/
 *	     1. /ENCODING irc
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
static const char *	decide_encoding (const unsigned char *from, const unsigned char *target, int server, iconv_t *code)
{
	int	i = 0;
	int	winner = -1;
	int	winning_score = -1;
	ServerInfo si;

	/*
	 * Evaluate each rule.
	 *	1. Does it apply to this message?
	 *	2. What is its score (priority)?
	 *	3. Is it the best match so far?
	 */
	for (i = 0; i < MAX_RECODING_RULES; i++)
	{
		RecodeRule *r;
		int	this_score;

		/* 
		 * XXX TODO - When you delete a rule, it leaves a gap.
		 * The gaps should probably be auto-closed so we know
		 * when we've checked the rules, instead of iterating over
		 * all 128 of them every time.
		 */
		if (!(r = recode_rules[i]))
			continue;	/* XXX or break;? */

		if (x_debug & DEBUG_RECODE)
			yell("Evaluating rule %d: %s", i, r->target);

		/* Skip rules without targets */
		/* BTW, this is "impossible", so a panic may be better */
		if (r->target == NULL)
		{
			if (x_debug & DEBUG_RECODE)
				yell("No target.");
			continue;
		}

		/* Special case the magic fallback rule. */
		/*
		 * XXX TODO -- Not all messages that will go through this
		 * function are from irc.  There should be a way to say 
		 * which magic rule we want to use.
		 */
		if (from && !my_stricmp(r->target, "irc"))
		{
			if (x_debug & DEBUG_RECODE)
				yell("irc magic rule ok for inbound msg");
			goto target_ok;
		}

		/**********************************************************/
		if (x_debug & DEBUG_RECODE)
			yell("Server part [%s], target part [%s]", 
				r->server_part, r->target_part);

		/**********************************************************/
		/*
		 *
		 * 1a. Is the Server acceptable?
		 *
		 */
		/* If the rule doesn't limit the server, then it's ok */
		/* If there is a server part, it must match our refnum */
		if (r->server_part != NULL)
		{
			if (!serverinfo_matches_servref(&r->si, server))
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
		 * 1b. Is the Target acceptable?
		 *
		 */

		/* If the rule doesn't limit the target, then it's ok */
		if (r->target_part == NULL)
		{
			if (x_debug & DEBUG_RECODE)
				yell("No target part -- ok");
			goto target_ok;
		}

		/* 
		 * If I'm sending the message and this rule isn't to 
		 * whomever i'm sending it to, it's not valid.
		 */
		if (from == NULL)
		{
			if (!my_stricmp(r->target_part, target))
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
			if (!my_stricmp(r->target_part, target))
			{
				if (x_debug & DEBUG_RECODE)
					yell("Inbound message - Target matches recipient -- ok");
				goto target_ok;
			}

			if (!my_stricmp(r->target_part, from))
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
		 * 2. Decide what the score for this rule should be
		 *     60. /ENCODING server/nickname
		 *     50. /ENCODING /nickname
		 *     40. /ENCODING server/channel
		 *     30. /ENCODING /channel
		 *     20. /ENCODING server/
 		 *     10. /ENCODING irc		(magic rule)
		 *
		 */
		if (r->server_part != NULL && r->target_part != NULL &&
					    !is_channel(r->target_part))
			this_score = 60;
		else if (r->server_part == NULL && r->target_part != NULL &&
					    !is_channel(r->target_part))
			this_score = 50;
		else if (r->server_part != NULL && r->target_part != NULL &&
					    is_channel(r->target_part))
			this_score = 40;
		else if (r->server_part == NULL && r->target_part != NULL && 
					    is_channel(r->target_part))
			this_score = 30;
		else if (r->server_part != NULL && r->target_part == NULL)
			this_score = 20;
		else if (from && !my_stricmp(r->target, "irc"))
			this_score = 10;
		/* This is in case someone tries to be too clever */
		else
			this_score = -100;

		/**********************************************************/
		if (x_debug & DEBUG_RECODE)
			yell("rule %d has score %d", i, this_score);

		/*
		 * 
		 * 3. Decide if this is the best rule (so far)
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
	 * If there is no winner (which should only happen if we're
	 * sending an outbound message), then UTF-8 it is!
	 */
	if (winner == -1 && from == NULL)
		return NULL;
	if (winner == -1)
		panic(1, "Did not find a recode rule for %d/%s/%s", 
					server, from, target);


	/* If from == NULL, we are sending the message outbound */
	if (from == NULL)
	    return check_recoding_iconv(recode_rules[winner], NULL, code);
	else
	    return check_recoding_iconv(recode_rules[winner], code, NULL);
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
	const char *	encoding;
	char *	new_buffer;
	size_t	new_buffer_len;
	
	if (invalid_utf8str(message))
		yell("WARNING - recoding outbound message, but it is not UTF8.  This will surely do the wrong thing.");

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
	const char *	encoding;
	char *	new_buffer;
	size_t	new_buffer_len;

	/* Nothing in, nothing out.  Should this be handled differently? */
	if (!message)
		return NULL;

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
	/*
	 * XXX TODO -- This should be impossible.  A panic is probably better.
	 */
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

/*
 * The /ENCODING command:
 *
 *	/ENCODING			Output all recode rules
 *	/ENCODING <rule>		Output one recode rule
 *	/ENCODING <rule> NONE		Remove a recoding rule
 *	/ENCODING <rule> <encoding>	Create/modify a recoding rule
 *
 * Recoding rules look like:
 *		server/nickname		(Containing a slash)
 *		nickname		(Not a number or containing a dot)
 *		server/channel		(Containing a slash)
 *		channel			(Starting with #, &, etc)
 *		server			(A number, or containing a dot)
 *	the literal string "irc" (cannot delete)
 *
 * The "server" part can be:
 *		A server refnum (number)
 *		A server "ourname" (with a dot)
 *		A server "itsname" (with a dot)
 *		A server group 
 *		Any server altname 
 * If the "server" part is not a number, or does not contain a dot, make
 * sure to follow it with a slash so it's not mistaken for a nick!
 *
 * If the "nick" part is any of "irc", "console", or "scripts", make sure
 * to precede it with a slash so it's not mistaken for a magic rule!
 *
 * The "encoding" can be anything that iconv_open(3) will accept.
 * The "encoding" should not be anything that would yield a non-c-string.
 */
BUILT_IN_COMMAND(encoding)
{
	char *		arg;
	const char *	encoding;
	int		x;
	const char *	server = NULL;
	const char *	target = NULL;

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
		if (!recode_rules[x])
			say("There is no encoding for %s", arg);

		/* You can't delete the system rules */
		else if (recode_rules[x]->magic == 1)
		{
			say("You cannot remove a fallback rule");
			return;
		}

		/* XXX TODO - Removing a rule should be in its own func */
		else
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

		return;
	}

	/* If there is not already a rule, create a new (blank) one. */
	if (x == MAX_RECODING_RULES || recode_rules[x] == NULL)
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

		/* We don't set up the encoding here -- fallthrough below */
		recode_rules[x] = create_recoding_rule(arg, NULL, 0);

		/* FALLTHROUGH -- set me up below here */
	}

	/* 
	 * Modify the existing (or newly created) rule 
	 */
	update_recoding_encoding(recode_rules[x], encoding);

	/* Declare what the new encoding is */
	say("Encoding for %s is now %s", recode_rules[x]->target, 
					 recode_rules[x]->encoding);
}

