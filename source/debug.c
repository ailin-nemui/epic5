/*
 * debug.c -- controll the values of x_debug.
 *
 * Written by Jeremy Nelson
 * Copyright 1997 EPIC Software Labs
 * See the COPYRIGHT file for more information
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

