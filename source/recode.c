/* $EPIC: recode.c,v 1.5 2014/02/18 13:17:12 jnelson Exp $ */
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

#include "irc.h"
#include "ircaux.h"
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
 *	if (word(2 $loadinfo()) != [pf] { load -pf -encoding CP437 $word(1 $loadinfo()); return; }
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
	iconv_t	from_handle;
	iconv_t to_handle;
};

typedef struct RecodeRule RecodeRule;

/* Sigh -- hard limits make me sad */
#define MAX_RECODING_RULES 128
RecodeRule **	recode_rules = NULL;

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
	recode_rules[0]->from_handle = 0;
	recode_rules[0]->to_handle = 0;

	/* Rule 1 is "scripts" */
	recode_rules[1] = (RecodeRule *)new_malloc(sizeof(RecodeRule));
	recode_rules[1]->target = malloc_strdup("scripts");
	recode_rules[1]->encoding = malloc_strdup("CP437");
	recode_rules[1]->from_handle = 0;
	recode_rules[1]->to_handle = 0;

	/* Rule 2 is "irc" */
	recode_rules[2] = (RecodeRule *)new_malloc(sizeof(RecodeRule));
	recode_rules[2]->target = malloc_strdup("irc");
	recode_rules[2]->encoding = malloc_strdup("ISO-8859-1");
	recode_rules[2]->from_handle = 0;
	recode_rules[2]->to_handle = 0;
}

/*
 * find_recoding - Return the encoding we believe 'target' is using.
 *
 * Arguments:
 *	target	- Either the string "console" or "scripts" or "irc"
 *		  In the future, will support servers, channels, and nicks
 *	from	- If not NULL, filled in with an (iconv_t *) you can
 *		  use to translate messages FROM this target.
 *	to	- If not NULL, filled in with an (iconv_t *) you can
 *		  use to translate messages TO this target.
 *
 * Return value:
 *	The encoding we think 'target' is using.  The details of this will
 *	be expanded in the future.
 */
const char *	find_recoding (const char *target, iconv_t *from, iconv_t *to)
{
	int	x;

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

	if (from)
	{
		if (recode_rules[x]->from_handle == 0)
			recode_rules[x]->from_handle = iconv_open("UTF-8", recode_rules[x]->encoding);
		*from = recode_rules[x]->from_handle;
	}

	if (to)
	{
		if (recode_rules[x]->to_handle == 0)
		{
			char *str;
			str = malloc_strdup2(recode_rules[x]->encoding, "//TRANSLIT");
			recode_rules[x]->to_handle = iconv_open(str, "UTF-8");
			new_free(&str);
		}
		*to = recode_rules[x]->to_handle;
	}

	return recode_rules[x]->encoding;
}

/*
 * recode_message - Return the 'which'th element in 'ArgList' in UTF-8
 *		    doing whatever recoding is appropriate
 *
 * Arguments:
 *	from	- The person who sent the message (server or nick)
 *	to	- Who the message was sent to
 *	comm	- The message this originated from
 *		  (If not an irc message, supply NULL)
 *	ArgList	- An array of strings (or just &str)
 *	which	- Which string to recode (0 if you're doing &str)
 */
char *	recode_message (const char *from, const char *to, const char *comm, const char **ArgList, int which)
{
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
}

BUILT_IN_COMMAND(encoding)
{
	const char *target;
	const char *encoding;
	int	x;

	if (!(target = next_arg(args, &args)))
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
	if (!(encoding = next_arg(args, &args)))
	{
		for (x = 0; x < MAX_RECODING_RULES; x++)
		{
			if (!recode_rules[x])
				continue;
			if (!my_stricmp(target, recode_rules[x]->target))
				say("Encoding for %s is %s", 
					recode_rules[x]->target,
					recode_rules[x]->encoding);
		}

		/* Show the encoding for TARGET */
		return;
	}

	for (x = 0; x < MAX_RECODING_RULES; x++)
	{
		if (!recode_rules[x])
			continue;	/* XXX or break; ? */
		if (!my_stricmp(target, recode_rules[x]->target))
			break;
	}

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
		recode_rules[x]->target = malloc_strdup(target);
		recode_rules[x]->from_handle = 0;
		recode_rules[x]->to_handle = 0;

		/* FALLTHROUGH -- set me up below here */
	}


	/* Save the new encoding */
	malloc_strcpy(&recode_rules[x]->encoding, encoding);

	/* Invalidate prior iconv handles */
	if (recode_rules[x]->from_handle != 0)
	{
		iconv_close(recode_rules[x]->from_handle);
		recode_rules[x]->from_handle = 0;
	}
	if (recode_rules[x]->to_handle != 0)
	{
		iconv_close(recode_rules[x]->to_handle);
		recode_rules[x]->to_handle = 0;
	}


	/* Declare what the new encoding is */
	say("Encoding for %s is now %s", recode_rules[x]->target, 
					 recode_rules[x]->encoding);
}




