/* $EPIC: debug.c,v 1.4 2002/07/06 03:50:11 jnelson Exp $ */
/*
 * debug.c -- controll the values of x_debug.
 *
 * Copyright © 1997, 2002 Jeremy Nelson and others ("EPIC Software Labs").
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

unsigned long x_debug = 0;

struct debug_opts
{
	char 	*command;
	int	flag;
};

static struct debug_opts opts[] = 
{
	{ "LOCAL_VARS",		DEBUG_LOCAL_VARS },
	{ "ALIAS",		DEBUG_ALIAS },
	{ "CHANNELS",		DEBUG_CHANNELS },
	{ "CTCPS",		DEBUG_CTCPS },
	{ "DCC_SEARCH",		DEBUG_DCC_SEARCH },
	{ "OUTBOUND",		DEBUG_OUTBOUND },
	{ "INBOUND",		DEBUG_INBOUND },
	{ "DCC_XMIT",		DEBUG_DCC_XMIT },
	{ "WAITS",		DEBUG_WAITS },
	{ "MEMORY",		DEBUG_MEMORY },
	{ "SERVER_CONNECT",	DEBUG_SERVER_CONNECT },
	{ "CRASH",		DEBUG_CRASH },
	{ "COLOR",		DEBUG_COLOR },
	{ "NOTIFY",		DEBUG_NOTIFY },
	{ "REGEX",		DEBUG_REGEX },
	{ "REGEX_DEBUG",	DEBUG_REGEX_DEBUG },
	{ "BROKEN_CLOCK",	DEBUG_BROKEN_CLOCK },
	{ "UNKNOWN",		DEBUG_UNKNOWN },
	{ "BOLD_HELPER",	DEBUG_BOLD_HELPER },
	{ "NEW_MATH",		DEBUG_NEW_MATH },
	{ "NEW_MATH_DEBUG",	DEBUG_NEW_MATH_DEBUG },
	{ "AUTOKEY",		DEBUG_AUTOKEY },
	{ "EXTRACTW",		DEBUG_EXTRACTW },
	{ "SLASH_HACK",		DEBUG_SLASH_HACK },
	{ "LASTLOG",		DEBUG_LASTLOG },
	{ "SSL",		DEBUG_SSL },
	{ "ALL",		0xFFFFFFFF },
	{ NULL,			0 },
};



BUILT_IN_COMMAND(xdebugcmd)
{
	int cnt;
	int remove = 0;
	char *this_arg;

	if (!args || !*args)
	{
		char buffer[512];
		int i = 0;

		buffer[0] = 0;
		for (i = 0; opts[i].command; i++)
		{
			if (buffer[0])
				strmcat(buffer, ", ", 511);
			strmcat(buffer, opts[i].command, 511);
		}

		say("Usage: XDEBUG [-][+]%s", buffer);
		return;
	}

	while (args && *args)
	{
		this_arg = upper(next_arg(args, &args));
		if (*this_arg == '-')
			remove = 1, this_arg++;
		else if (*this_arg == '+')
			this_arg++;

		for (cnt = 0; opts[cnt].command; cnt++)
		{
			if (!strncmp(this_arg, opts[cnt].command, strlen(this_arg)))
			{
				if (remove)
					x_debug &= ~opts[cnt].flag;
				else
					x_debug |= opts[cnt].flag;
				break;
			}
		}
		if (!opts[cnt].command)
			say("Unrecognized XDEBUG option '%s'", this_arg);
	}
}

