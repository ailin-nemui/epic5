/*
 * flood.c: handle channel flooding. 
 *
 * This attempts to give you some protection from flooding.  Basically, it keeps
 * track of how far apart (timewise) messages come in from different people.
 * If a single nickname sends more than 3 messages in a row in under a
 * second, this is considered flooding.  It then activates the ON FLOOD with
 * the nickname and type (appropriate for use with IGNORE). 
 *
 * Thanks to Tomi Ollila <f36664r@puukko.hut.fi> for this one. 
 * Modified Nov-Dec 1997 by SrfRoG (srfrog@nema.com), for the EPIC project.
 */

#include "irc.h"
#include "flood.h"
#include "hook.h"
#include "ignore.h"
#include "ircaux.h"
#include "output.h"
#include "server.h"
#include "vars.h"

static	char	*ignore_types[NUMBER_OF_FLOODS] =
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
	struct timeval	start;
	int		floods;
}	Flooding;

static	Flooding *flood = (Flooding *) 0;


/*
 * check_flooding: This checks for message flooding of the type specified for
 * the given nickname.  This is described above.  This will return 0 if no
 * flooding took place, or flooding is not being monitored from a certain
 * person.  It will return 1 if flooding is being check for someone and an ON
 * FLOOD is activated. 
 */
int	new_check_flooding (char *nick, char *nuh, char *chan, char *line, FloodType type)
{
static	int	 users = 0,
		 pos = 0;
	int	 i, 
		 numusers,
		 server,
		 retval = 1;
	struct timeval	 right_now;
	double	 diff;
	Flooding *tmp;


	/*
	 * Figure out how many people we want to track
	 */
	numusers = get_int_var(FLOOD_USERS_VAR);

	/*
	 * Following 0 people turns off flood checking entirely.
	 */
	if (numusers == 0)
	{
		if (flood)
			new_free((char **)&flood);
		users = 0;
		return 1;
	}

	/*
	 * If the number of users has changed, then resize the info array
	 */
	if (users != numusers)
	{
		RESIZE(flood, Flooding, numusers);
		i = users;
		for (; i > numusers; i--)
		{
			new_free(&flood[i].nuh);
			new_free(&flood[i].channel);
		}
		for (; i < numusers; i++)
		{
			flood[i].nuh = NULL;
			flood[i].channel = NULL;
			flood[i].server = -1;
			flood[i].type = -1;
			flood[i].cnt = 0;
			get_time(&(flood[i].start));
			flood[i].floods = 0;
		}
		users = numusers;
	}

	/*
	 * What server are we using?
	 */
	server = (from_server == -1) ? primary_server : from_server;

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
		do
			pos = (pos + 1) % users;
		while (flood[pos].floods && pos != old_pos);

		tmp = flood + pos;
		malloc_strcpy(&tmp->nuh, nuh);
		if (chan)
			malloc_strcpy(&tmp->channel, chan);
		else
			new_free(&tmp->channel);

		tmp->server = server;
		tmp->type = type;
		tmp->cnt = 1;
		tmp->start = right_now;

		return 1;
	}
	else
		tmp = flood + i;

	/*
	 * Has the person flooded too much?
	 */
	if (++tmp->cnt >= get_int_var(FLOOD_AFTER_VAR))
	{
		diff = time_diff(tmp->start, right_now);

		if (diff == 0.0 || tmp->cnt / diff >= get_int_var(FLOOD_RATE_VAR))
		{
			if (get_int_var(FLOOD_WARNING_VAR))
				say("%s flooding detected from %s", 
						ignore_types[type], nick);

			retval = do_hook(FLOOD_LIST, "%s %s %s %s",
				nick, ignore_types[type], 
				chan ? chan : "*", line);

			tmp->floods++;
		}
		else
		{
			/*
			 * Not really flooding -- reset back to normal.
			 */
			tmp->floods = 0;
		}

		/*
		 * Not really flooding -- reset back to normal.
		 */
		tmp->cnt = 1;
		tmp->start = right_now;
	}

	return retval;
}

int	check_flooding (char *nick, char *nuh, FloodType type, char *line)
{
	return new_check_flooding(nick, nuh, NULL, line, type);
}

