/* $EPIC: debug.c,v 1.25 2007/04/12 02:37:24 jnelson Exp $ */
/*
 * debug.c -- controll the values of x_debug.
 *
 * Copyright © 1997, 2002 EPIC Software Labs
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
#include "output.h"
#include "reg.h"
#include "commands.h"

#if 0
unsigned long x_debug = DEBUG_SERVER_CONNECT;
#else
unsigned long x_debug = 0;
#endif

struct debug_opts
{
	const char *	command;
	int		flag;
};

static struct debug_opts opts[] = 
{
	{ "LOCAL_VARS",		DEBUG_LOCAL_VARS },
	{ "ALIAS",		0 },
	{ "CHANNELS",		DEBUG_CHANNELS },
	{ "CTCPS",		DEBUG_CTCPS },
	{ "DCC_SEARCH",		DEBUG_DCC_SEARCH },
	{ "OUTBOUND",		DEBUG_OUTBOUND },
	{ "INBOUND",		DEBUG_INBOUND },
	{ "DCC_XMIT",		DEBUG_DCC_XMIT },
	{ "WAITS",		DEBUG_WAITS },
	{ "MEMORY",		0 },
	{ "SERVER_CONNECT",	DEBUG_SERVER_CONNECT },
	{ "CRASH",		DEBUG_CRASH },
	{ "COLOR",		0 },
	{ "NOTIFY",		DEBUG_NOTIFY },
	{ "REGEX",		DEBUG_REGEX },
	{ "REGEX_DEBUG",	DEBUG_REGEX_DEBUG },
	{ "BROKEN_CLOCK",	DEBUG_BROKEN_CLOCK },
	{ "UNKNOWN",		DEBUG_UNKNOWN },
	{ "BOLD_HELPER",	0 },
	{ "NEW_MATH",		0 },
	{ "NEW_MATH_DEBUG",	DEBUG_NEW_MATH_DEBUG },
	{ "AUTOKEY",		0 },
	{ "EXTRACTW",		DEBUG_EXTRACTW },
	{ "SLASH_HACK",		DEBUG_SLASH_HACK },
	{ "LASTLOG",		DEBUG_LASTLOG },
	{ "SSL",		DEBUG_SSL },
	{ "EXTRACTW_DEBUG",	DEBUG_EXTRACTW_DEBUG },
	{ "MESSAGE_FROM",	DEBUG_MESSAGE_FROM },
	{ "WHO_QUEUE",		DEBUG_WHO_QUEUE },
	{ "OLD_MATH",		0 },
	{ "DWORD",        	DEBUG_DWORD },
	{ "ALL",		~0},
	{ NULL,			0 },
};



BUILT_IN_COMMAND(xdebugcmd)
{
	int 	cnt;
	int 	rem = 0;
	char *	this_arg;
	int	original_xdebug = x_debug;

	if (!args || !*args)
	{
		char buffer[512];
		int i = 0;

		buffer[0] = 0;
		for (i = 0; opts[i].command; i++)
		{
			if (buffer[0])
				strlcat(buffer, ", ", sizeof buffer);
			strlcat(buffer, opts[i].command, sizeof buffer);
		}

		say("Usage: XDEBUG [-][+]%s", buffer);
		return;
	}

	while (args && *args && *args != '{')
	{
		this_arg = upper(next_arg(args, &args));
		if (*this_arg == '-')
			rem = 1, this_arg++;
		else if (*this_arg == '+')
			rem = 0, this_arg++;

		for (cnt = 0; opts[cnt].command; cnt++)
		{
			if (!strncmp(this_arg, opts[cnt].command, 
						strlen(this_arg)))
			{
				if (rem)
					x_debug &= ~opts[cnt].flag;
				else
					x_debug |= opts[cnt].flag;
				break;
			}
		}
		if (!opts[cnt].command)
			say("Unrecognized XDEBUG option '%s'", this_arg);
	}

	if (args && *args && *args == '{')
	{
		runcmds(args, subargs);
		x_debug = original_xdebug;
	}
}

char *	function_xdebug (char *word)
{
	char	*ret = NULL;
	const char	*mask = NULL;
	int	cnt;
	size_t	clue = 0;

	mask = next_arg(word, &word);
	mask = mask && *mask ? mask : star;

	for (cnt = 0; opts[cnt].command; cnt++)
	{
		if (!~opts[cnt].flag) {
			continue;
		} else if (!wild_match(mask,opts[cnt].command)) {
			continue;
		} else if (x_debug & opts[cnt].flag) {
			malloc_strcat_wordlist_c(&ret, space, "+", &clue);
		} else {
			malloc_strcat_wordlist_c(&ret, space, "-", &clue);
		}
		malloc_strcat_c(&ret, opts[cnt].command, &clue);
	}

	malloc_strcat_c(&ret, "", &clue);
	return ret;
}
