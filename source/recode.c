/* $EPIC: recode.c,v 1.1 2014/02/10 17:40:37 jnelson Exp $ */
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

/*
 * Here's the plan...
 *
 * EPIC wants to handle everything internally as a UTF-8 string, but 
 * sometimes we will receive a non-UTF8 string and have to decide how
 * to convert it into UTF8.
 *
 * There is some research in the topic of automatic encoding detection,
 * and certainly my conversations with epic users tells me that most
 * non-english speakers want to use UTF-8, and english speakers are using
 * UTF-8 implicitly anyways.  So I don't expect this to be too critical
 * to how people use irc.
 *
 * Nevertheless, it is not my desire to cut off people who do not use 
 * UTF-8, either the epic user, or others on irc -- so we need to be 
 * able to tell each other about what encoding we're using.
 *
 * Declare the encoding your terminal emulator uses (if it's not utf-8)
 *	/ENCODING console ISO-8859-1
 *
 * Declare what encoding non-utf8 scripts should be assumed to be
 * (assuming they don't declare their encoding, WHICH THEY SHOULD)
 *	/ENCODING scripts CP437
 *
 * Declare what encoding irc.foo.com uses
 *	/ENCODING irc.foo.com ISO-8859-15
 *
 * Declare what encoding #epic uses on EFNet
 *	/ENCODING EFNet/#epic ISO-8859-1
 *
 * Declare that hop uses ISO-85519-1
 *	/ENCODING hop ISO-8859-1
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *    IMPORTANT! IMPORTANT! IMPORTANT! IMPORTANT! IMPORTANT!   *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * It is critical to remember the purpose of this is to handle
 * NON-UTF8 strings.  So you may be tempted to say, "oh, I should
 * declare that my channel uses UTF-8", but EPIC already knows
 * how to handle that.  What you want to do is tell EPIC what to 
 * do with NON-UTF8 stuff it comes across.
 *
 * So the encoding of a target must not be UTF-8, and EPIC will 
 * remind you of this if you try.
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

	recode_rules = (RecodeRule **)new_malloc(sizeof(RecodeRule *) * MAX_RECODING_RULES);
	for (x = 0; x < MAX_RECODING_RULES; x++)
		recode_rules[x] = NULL;

	/* Rule 0 is "console" */
	recode_rules[0] = (RecodeRule *)new_malloc(sizeof(RecodeRule));
	recode_rules[0]->target = malloc_strdup("console");
	recode_rules[0]->encoding = malloc_strdup("UTF-8");
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
	malloc_strcpy(&recode_rules[0]->encoding, encoding);

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
	say("Encoding for %s is now %s", target, encoding);
}




