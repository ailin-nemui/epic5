/* $EPIC: flood.c,v 1.13 2003/07/10 13:08:56 jnelson Exp $ */
/*
 * flood.c: handle channel flooding.
 *
 * Copyright (c) 1990-1992 Tomi Ollila
 * Copyright © 1997 EPIC Software Labs
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
 * This attempts to give you some protection from flooding.  Basically, it keeps
 * track of how far apart (timewise) messages come in from different people.
 * If a single nickname sends more than 3 messages in a row in under a
 * second, this is considered flooding.  It then activates the ON FLOOD with
 * the nickname and type (appropriate for use with IGNORE).
 */

#include "irc.h"
#include "flood.h"
#include "hook.h"
#include "ignore.h"
#include "ircaux.h"
#include "output.h"
#include "server.h"
#include "vars.h"
#include "functions.h"
#include "lastlog.h"
#include "window.h"

static	const char *ignore_types[NUMBER_OF_FLOODS] =
{
	"CRAP",
	"CTCPS",
	"INVITES",
	"JOINS",
	"MSGS",
	"NICKS",
	"NOTES",
	"NOTICES",
	"PUBLIC",
	"TOPICS",
	"WALLOPS",
	"WALLS"
};

typedef struct flood_stru
{
	char		*nuh;
	char		*channel;
	int		server;

	FloodType	type;
	long		cnt;
	Timeval		start;
	int		floods;
}	Flooding;

static	Flooding *flood = (Flooding *) 0;
int	users = 0;


/*
 * If flood_maskuser is 0, proceed normally.  If 2, keep
 * track of the @host only.  If 1, keep track of the U@H
 * only if it begins with an alphanum, and use the @host
 * otherwise.
 */
static const char *	normalize_nuh (const char *nuh)
{
	int maskuser = get_int_var(FLOOD_MASKUSER_VAR);

	if (maskuser == 0) {
	} else if (!nuh) {
	} else if (maskuser == 1 && isalnum(*nuh)) {
	} else {
		char *nnuh = strrchr(nuh, '@');
		nuh = nnuh ? nnuh : nuh;
	}

	return nuh;
}

/*
 * check_flooding: This checks for message flooding of the type specified for
 * the given nickname.  This is described above.  This will return 0 if no
 * flooding took place, or flooding is not being monitored from a certain
 * person.  It will return 1 if flooding is being check for someone and an ON
 * FLOOD is activated.
 */
int	new_check_flooding (const char *nick, const char *nuh, const char *chan, const char *line, FloodType type)
{
static	int	 pos = 0;
	int	 i,
		 numusers,
		 server,
		 retval = 0;
	Timeval	 right_now;
	double	 diff;
	Flooding *tmp;


	/*
	 * Figure out how many people we want to track
	 */
	numusers = get_int_var(FLOOD_USERS_VAR);

	/*
	 * If the number of users has changed, then resize the info array
	 */
	if (users != numusers)
	{
		i = users;
		for (i--; i >= numusers; i--)
		{
			new_free(&(flood[i].nuh));
			new_free(&(flood[i].channel));
		}
		RESIZE(flood, Flooding, numusers);
		for (i++; i < numusers; i++)
		{
			flood[i].nuh = NULL;
			flood[i].channel = NULL;
			flood[i].server = NOSERV;
			flood[i].type = -1;
			flood[i].cnt = 0;
			get_time(&(flood[i].start));
			flood[i].floods = 0;
		}
		users = numusers;
		if (users)
			pos %= users;
	}

	/*
	 * Following 0 people turns off flood checking entirely.
	 */
	if (numusers == 0)
	{
		if (flood)
			new_free((char **)&flood);
		users = 0;
		return 0;
	}

	if (nuh && *nuh)
		nuh = normalize_nuh(nuh);
	else
		return 0;

	/*
	 * What server are we using?
	 */
	server = (from_server == NOSERV) ? primary_server : from_server;

	/*
	 * Look in the flooding array.
	 * Find an entry that matches us:
	 *	It must be the same flooding type and server.
	 *	It must be for the same nickname
	 *	If we're for a channel, it must also be for a channel
	 *		and it must be for our channel
	 *	else if we're not for a channel, it must also not be for
	 *		a channel.
	 */
	for (i = 0; i < users; i++)
	{
		/*
		 * Do some inexpensive tests first
		 */
		if (type != flood[i].type)
			continue;
		if (server != flood[i].server)
			continue;
		if (!flood[i].nuh)
			continue;

		/*
		 * Must be for the person we're looking for
		 */
		if (my_stricmp(nuh, flood[i].nuh))
			continue;

		/*
		 * Must be for a channel if we're for a channel
		 */
		if (!!flood[i].channel != !!chan)
			continue;

		/*
		 * Must be for the channel we're looking for.
		 */
		if (chan && my_stricmp(chan, flood[i].channel))
			continue;

		/*
		 * We have a winner!
		 */
		break;
	}

	get_time(&right_now);

	/*
	 * We didnt find anybody.
	 */
	if (i == users)
	{
		/*
		 * pos points at the next insertion point in the array.
		 */
		int old_pos = pos;
		do {
			pos = (0 < pos ? pos : users) - 1;
		} while (0 < --flood[pos].floods && pos != old_pos);

		tmp = flood + pos;
		malloc_strcpy(&tmp->nuh, nuh);
		if (chan)
			malloc_strcpy(&tmp->channel, chan);
		else
			new_free(&tmp->channel);

		tmp->server = server;
		tmp->type = type;
		tmp->cnt = 0;
		tmp->start = right_now;

		pos = (0 < old_pos ? old_pos : users) - 1;
		return 0;
	}
	else
		tmp = flood + i;

	/*
	 * Has the person flooded too much?
	 */
	if (++tmp->cnt >= get_int_var(FLOOD_AFTER_VAR))
	{
		float rate = get_int_var(FLOOD_RATE_VAR);
		rate /= get_int_var(FLOOD_RATE_PER_VAR);
		diff = time_diff(tmp->start, right_now);

		if ((diff == 0.0 || tmp->cnt / diff >= rate) &&
				(retval = do_hook(FLOOD_LIST, "%s %s %s %s",
				nick, ignore_types[type],
				chan ? chan : "*", line)))
		{
			tmp->floods++;
			message_from(chan, LOG_CRAP);
			if (get_int_var(FLOOD_WARNING_VAR))
				say("FLOOD: %ld %s detected from %s in %f seconds",
					tmp->cnt+1, ignore_types[type], nick, diff);
			message_from(NULL, LOG_CURRENT);
		}
		else
		{
			/*
			 * Not really flooding -- reset back to normal.
			 */
			tmp->cnt = 0;
			tmp->start = right_now;
		}
	}

	if (get_int_var(FLOOD_IGNORE_VAR))
		return retval;
	else
		return 0;
}

int	check_flooding (const char *nick, const char *nuh, FloodType type, const char *line)
{
	return new_check_flooding(nick, nuh, NULL, line, type);
}

/*
 * Note:  This will break whatever uses it when any of the arguments
 *        contain a double quote.
 */
char *	function_floodinfo (char *args)
{
	const char	*arg;
	char *ret = NULL;
	size_t	clue = 0;
	Timeval right_now;
	int	i;

	get_time(&right_now);

	while ((arg = new_next_arg(args, &args))) {
		if (!strspn(arg, "*?"))
			arg = normalize_nuh(arg);
		for (i = 0; i < users; i++) {
			if (flood[i].nuh && wild_match(arg, flood[i].nuh)) {
				malloc_strcat_wordlist_c(&ret, space, "\"", &clue);
				malloc_strcat_wordlist_c(&ret, empty_string, flood[i].nuh, &clue);
				malloc_strcat_wordlist_c(&ret, space, flood[i].channel ? flood[i].channel : star, &clue);
				malloc_strcat_wordlist_c(&ret, space, ignore_types[flood[i].type], &clue);
				malloc_strcat_wordlist_c(&ret, space, ltoa(flood[i].server), &clue);
				malloc_strcat_wordlist_c(&ret, space, ltoa(flood[i].cnt), &clue);
				malloc_strcat_wordlist_c(&ret, space, ftoa(time_diff(flood[i].start, right_now)), &clue);
				malloc_strcat_wordlist_c(&ret, empty_string, "\"", &clue);
			}
		}
	}

	RETURN_MSTR(ret);
}
