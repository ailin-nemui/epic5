/* $EPIC: names.c,v 1.41 2003/04/24 21:49:25 jnelson Exp $ */
/*
 * names.c: This here is used to maintain a list of all the people currently
 * on your channel.  Seems to work 
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright © 1993, 2003 EPIC Software Labs.
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
#include "alist.h"
#include "names.h"
#include "output.h"
#include "screen.h"
#include "window.h"
#include "vars.h"
#include "server.h"
#include "list.h"
#include "hook.h"

typedef struct nick_stru
{
	char 	*nick;		/* nickname of person on channel */
	u_32int_t hash;		/* Hash of the nickname */
	char	*userhost;	/* Their userhost, if we know it */
	short	suspicious;	/* True if the nick might be truncated */
	short	chanop;		/* True if they are a channel operator */
	short	voice;		/* 1 if they are, 0 if theyre not, -1 if uk */
	short	half_assed;	/* 1 if they are, 0 if theyre not, -1 if uk */
}	Nick;

typedef	struct	nick_list_stru
{
	Nick	**list;
	int	max;
	int	max_alloc;
	alist_func func;
	hash_type hash;
}	NickList;

static	int	current_channel_counter = 0;

/* ChannelList: structure for the list of channels you are current on */
typedef	struct	channel_stru
{
struct	channel_stru *	next;		/* pointer to next channel */
struct	channel_stru *	prev;		/* pointer to previous channel */
	char *		channel;	/* channel name */
	int		server;		/* The server the channel is "on" */
	int		winref;		/* The window the channel is "on" */
	int		curr_count;	/* Current channel precedence */
	int		waiting;	/* just acting as a placeholder... */
	int		inactive;	/* waiting on a reconnect... */
	int		bound;		/* Bound to this window? */
	int		current;	/* Current to this window? */
	NickList	nicks;		/* alist of nicks on channel */

	unsigned long	mode;		/* Current mode settings for channel */
	unsigned long	i_mode;		/* channel mode for cached string */
	char *		s_mode;		/* cached string version of modes */
	int		limit;		/* max users for the channel */
	char *		key;		/* key for this channel */
	char		chop;		/* true if you are chop */
	char		voice;		/* true if you are voiced */
	char		half_assed;	/* true if you are a helper */
	Timeval		join_time;	/* When we joined the channel */
}	Channel;


/*
 * The variable "mode_str" must correspond in order to the modes defined
 * here, or all heck will break loose.  You have been warned.
 */
static	char	mode_str[] = "aciklmnprstzDMOR";

const int	MODE_ANONYMOUS	= 1 << 0;	/* av2.9 */
const int	MODE_C		= 1 << 1;	/* erf/TS4 */
const int 	MODE_INVITE 	= 1 << 2;	/* RFC */
const int 	MODE_KEY    	= 1 << 3;	/* RFC */
const int	MODE_LIMIT	= 1 << 4;	/* RFC */
const int 	MODE_MODERATED	= 1 << 5;	/* RFC */
const int	MODE_MSGS	= 1 << 6;	/* RFC */
const int	MODE_PRIVATE	= 1 << 7;	/* RFC */
const int	MODE_REGISTERED = 1 << 8;	/* Duhnet */
const int	MODE_SECRET	= 1 << 9;	/* RFC */
const int	MODE_TOPIC	= 1 << 10;	/* RFC */
const int	MODE_Z		= 1 << 11;	/* erf/TS4 */
const int	MODE_D		= 1 << 12;	/* special request */
const int	MODE_M          = 1 << 13;	/* Duhnet */
const int	MODE_OPER_ONLY	= 1 << 14;	/* Duhnet */
const int	MODE_RESTRICTED = 1 << 15;	/* Duhnet */
const int	MODE_C2		= 1 << 16;	/* Quakenet */


/* channel_list: list of all the channels you are currently on */
static	Channel *	channel_list = NULL;

#if 0
/* For new eu2.9 !channels */
static	char		new_channel_format[BIG_BUFFER_SIZE];
#else
static	int	match_chan_with_id (const char *chan, const char *match);
#endif
	void	channel_hold_election (int winref);


/*
 * This isnt strictly neccesary, its more of a cosmetic function.
 */
static int	traverse_all_channels (Channel **ptr, int server, int only_this_server)
{
	if (!*ptr)
		*ptr = channel_list;
	else
		*ptr = (*ptr)->next;

	if (only_this_server)
		while (*ptr && (*ptr)->server != server)
			*ptr = (*ptr)->next;
	else
		while (*ptr && (*ptr)->server == server)
			*ptr = (*ptr)->next;


	if (!*ptr)
		return 0;

	/* The tests that used to be done here moved elsewhere */
	return 1;
}


/*
 *
 * Channel maintainance
 *
 */

static Channel *find_channel (const char *channel, int server)
{
	Channel *ch = NULL;

	if (server == NOSERV)
		server = primary_server;

	/* Automatically grok the ``*'' channel. */
	if (!channel || !*channel || !strcmp(channel, "*"))
		if (!(channel = get_echannel_by_refnum(0)))
			return NULL;		/* sb colten */

	while (traverse_all_channels(&ch, server, 1))
		if (!my_stricmp(ch->channel, channel))
			return ch;

	return NULL;
}

/* Channel constructor */
static Channel *create_channel (const char *name, int server)
{
	Channel *new_c = (Channel *)new_malloc(sizeof(Channel));

	new_c->prev = new_c->next = NULL;
	new_c->channel = m_strdup(name);
	new_c->server = server;
	new_c->waiting = 0;
	new_c->inactive = 0;
	new_c->winref = -1;
	new_c->bound = 0;
	new_c->current = 0;
	new_c->nicks.max_alloc = new_c->nicks.max = 0;
	new_c->nicks.list = NULL;
	new_c->nicks.func = (alist_func) my_strnicmp;
	new_c->nicks.hash = HASH_INSENSITIVE;

	new_c->mode = 0;
	new_c->i_mode = 0;
	new_c->s_mode = NULL;
	new_c->limit = 0;
	new_c->key = NULL;
	new_c->chop = 0;
	new_c->voice = 0;
	new_c->half_assed = 0;

	new_c->next = channel_list;
	if (channel_list)
		channel_list->prev = new_c;
	channel_list = new_c;
	return new_c;
}

/* Nicklist destructor */
static void 	clear_channel (Channel *chan)
{
	NickList *list = &chan->nicks;
	int	i;

	for (i = 0; i < list->max; i++)
	{
		new_free(&list->list[i]->nick);
		new_free(&list->list[i]->userhost);
		new_free(&list->list[i]);
	}
	new_free((void **)&list->list);
	list->max = list->max_alloc = 0;
}

/* Channel destructor -- caller must free "chan". */
static void 	destroy_channel (Channel *chan)
{
	int	is_current_now;
	Char *	new_current_channel;

	is_current_now = is_current_channel(chan->channel, chan->server);

	if (chan != channel_list)
	{
		if (!chan->prev)
			panic("chan != channel_list, but chan->prev is NULL");
		chan->prev->next = chan->next;
	}
	else
	{
		if (chan->prev)
			panic("channel_list->prev is not NULL");
		channel_list = chan->next;
	}

	if (chan->next)
		chan->next->prev = chan->prev;

	/*
	 * If we are a current window, then we will no longer be so;
	 * We must hold an election for a new current window and throw
	 * the switch channels thing.
	 */
	if (is_current_now)
	{
	    channel_hold_election(chan->winref);
	    if (!(new_current_channel = get_echannel_by_refnum(chan->winref)))
		new_current_channel = zero;

	    do_hook(SWITCH_CHANNELS_LIST, "%d %s %s",
			chan->winref, chan->channel, new_current_channel);
	}

	new_free(&chan->channel);
	chan->server = NOSERV;
	chan->winref = -1;

	if (chan->nicks.max_alloc)
		clear_channel(chan);

	chan->mode = 0;
	chan->i_mode = 0;
	new_free(&chan->s_mode);
	chan->limit = 0;
	new_free(&chan->key); 
	chan->chop = 0;
	chan->voice = 0;
}

/*
 * add_channel: adds the named channel to the channel list.
 * The added channel becomes the current channel as well.
 */
void 	add_channel (const char *name, int server)
{
	Channel *new_c;
	int	was_window = -1;
	Window	*tmp = NULL;

	if ((new_c = find_channel(name, server)))
	{
		was_window = new_c->winref;
		destroy_channel(new_c);
		malloc_strcpy(&(new_c->channel), name);
		new_c->server = server;
	}
	else
		new_c = create_channel(name, server);

	new_c->inactive = 0;		/* Channel no longer is a placeholder */
	new_c->waiting = 1;		/* This channel is "syncing" */
	get_time(&new_c->join_time);

	if (was_window == -1)
	{
	    /* Try to find "our" window for this channel. */
	    while (traverse_all_windows(&tmp))
	    {
		if (tmp->server != from_server)
			continue;
		if (!tmp->waiting_channel && !tmp->bind_channel)
			 continue;

		if (tmp->bind_channel && 
			!match_chan_with_id(name, tmp->bind_channel))
		{
			was_window = tmp->refnum;
			break;
		}

		if (tmp->waiting_channel &&
			!match_chan_with_id(name, tmp->waiting_channel))
		{
			new_free(&tmp->waiting_channel);
			was_window = tmp->refnum;
			break;
		}
	    }
	}

	if (was_window == -1)
		was_window = get_winref_by_servref(from_server);

	set_channel_window(name, from_server, was_window, 1);
	update_all_windows();
	return;
}

/*
 * remove_channel: removes the named channel from the channel list.
 * If the channel is not in the channel list, nothing happens.
 * If the channel is the current channel, the next current channel will
 *	be whatever is at channel_list when we're done.  If channel_list
 * 	is empty when we're done, you go into limbo (channel 0).
 * If you already have a pointer to a channel to be deleted, DO NOT CALL
 * 	this function.  Instead, call "destroy_channel" directly.  Do not pass
 * 	the "name" field from a valid channel through the "channel" arg!
 */
void 	remove_channel (const char *channel, int server)
{
	Channel *tmp;
	int	old_from_server = from_server;

	/* Nuke just one channel */
	if (channel)
	{
		if ((tmp = find_channel(channel, server)))
		{
			destroy_channel(tmp);
			new_free((char **)&tmp);
		}
	}

	/* Nuke all the channels */
	else
	{
		while ((tmp = channel_list))
		{
			destroy_channel(tmp);
			new_free((char **)&tmp);
		}
	}

	from_server = old_from_server;
	update_all_windows();
}


/*
 *
 * Nickname maintainance
 *
 */
static Nick *	find_nick_on_channel (Channel *ch, const char *nick)
{
	int cnt, loc;
	Nick *new_n = (Nick *)find_array_item((array *)&ch->nicks,
						nick, &cnt, &loc);

	if (cnt >= 0 || !new_n)
		return NULL;

	return new_n;
}

static Nick *	find_nick (int server, const char *channel, const char *nick)
{
	Channel *ch;
	if ((ch = find_channel(channel, server)))
		return find_nick_on_channel(ch, nick);

	return NULL;
}

/*
 * XXX - This entire function is a hack and should be totaly unnecesary, 
 * but unfortunately due to a historical server bug we have to engage in 
 * this kind of chicanery.  Bleh.  (I hope this works right, i wrote it 
 * rather quickly.)  Obviously, this function can't be called a lot,
 * only in exceptional circumstances.
 */
static Nick *	find_suspicious_on_channel (Channel *ch, const char *nick)
{
	int	pos;

	/*
	 * Efficiency here isn't terribly important, but correctness IS.
	 */
	for (pos = 0; pos < ch->nicks.max; pos++)
	{
		Nick *	n = ch->nicks.list[pos];
		char *	s = n->nick;
		size_t	siz = strlen(s);

		/* 
		 * Is the nick in the list (s) a subset of 'nick'? 
		 * If not, keep going.
		 */
		if (ch->nicks.func(s, nick, siz))
			continue;

		/*
		 * Is this a suspicious nickname?
		 * If not, keep going.
		 *
		 * --- Note that we take no corrective action here.
		 * That is on purpose.  Please don't change that.
		 */
		if (n->suspicious == 1)
			return n;
	}

	return NULL;
}



/*
 * add_to_channel: adds the given nickname to the given channel.  If the
 * nickname is already on the channel, nothing happens.  If the channel is
 * not on the channel list, nothing happens (although perhaps the channel
 * should be addded to the list?  but this should never happen) 
 */
void 	add_to_channel (const char *channel, const char *nick, int server, int suspicious, int oper, int voice, int ha)
{
	Nick 	*new_n, *old;
	Channel *chan;
	int	ischop = oper;
	int	isvoice = voice;
	int	half_assed = ha;
const	char	*prefix;

	if (!(chan = find_channel(channel, server)))
		return;

	prefix = get_server_005(from_server, "PREFIX");
	if (prefix && *prefix == '(' && (prefix = strchr(prefix, ')')))
		prefix++;
	if (!prefix || !*prefix)
		prefix = "@%+";

	/* 
	 * This is defensive just in case someone in the future
	 * decides to do the right thing...
	 */
	for (;;)
	{
		if (!strchr(prefix, *nick))
		{
			break;
		}
		else if (*nick == '+')
		{
			nick++;
			if (is_me(server, nick))
				chan->voice = 1;
			isvoice = 1;
			break;
		}
		else if (*nick == '@')
		{
			nick++;
			if (is_me(server, nick))
				chan->chop = 1;
			else 
			{
				if (isvoice == 0)
					isvoice = -1;
				if (half_assed == 0)
					half_assed = -1;
			}
			ischop = 1;
			break;
		}
		else if (*nick == '%')
		{
			nick++;
			if (is_me(server, nick))
				chan->half_assed = 1;
			else
			{
				if (isvoice == 0)
					isvoice = -1;
			}
			half_assed = 1;
			break;
		}
		else
		{
			nick++;
			break;
		}
	}

	new_n = (Nick *)new_malloc(sizeof(Nick));
	new_n->nick = m_strdup(nick);
	new_n->userhost = NULL;
	new_n->suspicious = suspicious;
	new_n->chanop = ischop;
	new_n->voice = isvoice;
	new_n->half_assed = half_assed;

	if ((old = (Nick *)add_to_array((array *)&chan->nicks, (array_item *)new_n)))
	{
		new_free(&old->nick);
		new_free(&old->userhost);
	}
}

void 	add_userhost_to_channel (const char *channel, const char *nick, int server, const char *uh)
{
	Channel *chan;
	Nick *new_n = NULL;

	/*
	 * This call implicitly occurs during a race condition.  It is 
	 * completely possible that this function will be called and
	 * 'channel' will not exist (because the user PARTed before the WHO
	 * request is finished.)  In this case, we should just silently
	 * ignore the request.  That means we have to actually check to see
	 * if the channel exists before we whine about the user not existing.
	 */
	if (!(chan = find_channel(channel, server)))
		return;		/* Oh well.  Time to punt. */

	while (!(new_n = find_nick_on_channel(chan, nick)))
	{
		if (!(new_n = find_suspicious_on_channel(chan, nick)))
		{
			yell("User [%s!%s] was not on the names list for "
				"channel [%s] on server [%d] -- adding them",
				nick, uh, channel, server);
			add_to_channel(channel, nick, server, 0, 0, 0, 0);
		}

		/*
		 * XXX Ideally, this should be done some other way, but
		 * what the heck.
		 */
		else
		{
		    remove_from_array((array *)&chan->nicks, new_n->nick);
		    malloc_strcpy(&new_n->nick, nick);
		    add_to_array((array *)&chan->nicks, (array_item *)new_n);
		    if (x_debug & DEBUG_CHANNELS)
		    {
			yell("Detected and corrected a nickname mangled by "
				"server-side truncation bug");
			yell("Server [%d] Channel [%s] Nickname [%s]",
				server, channel, nick);
		    }
		    /*
		     * Yes, yes i know i could 'break' here, but i
		     * intentionally do not do so becuase i want to make
		     * sure that this correction code is, well, correct,
		     * and produces reasonable results.
		     */
		}
	}

	malloc_strcpy(&new_n->userhost, uh);
}


/*
 * remove_from_channel: removes the given nickname from the given channel. If
 * the nickname is not on the channel or the channel doesn't exist, nothing
 * happens. 
 */
void 	remove_from_channel (const char *channel, const char *nick, int server)
{
	Channel *chan = NULL;
	Nick	*tmp;

	if (server == NOSERV) return;

	while (traverse_all_channels(&chan, server, 1))
	{
		/* This is correct, dont change it! */
		if (channel && my_stricmp(channel, chan->channel))
			continue;

		if ((tmp = (Nick *)remove_from_array((array *)&chan->nicks, nick)))
		{
			new_free(&tmp->nick);
			new_free(&tmp->userhost); /* Da5id reported mf here */
			new_free((char **)&tmp);
		}
	}
}

/*
 * rename_nick: in response to a changed nickname, this looks up the given
 * nickname on all you channels and changes it the new_nick.  Now it also
 * restores the userhost (was lost before [oops!])
 */
void 	rename_nick (const char *old_nick, const char *new_nick, int server)
{
	Channel *chan = NULL;
	Nick	*tmp;

	if (server == NOSERV) return;		/* Sanity check */

	while (traverse_all_channels(&chan, server, 1))
	{
		if ((tmp = (Nick *)remove_from_array((array *)&chan->nicks, old_nick)))
		{
			malloc_strcpy(&tmp->nick, new_nick);
			add_to_array((array *)&chan->nicks, (array_item *)tmp);
		}
	}
}


/*
 * check_channel_type: checks if the given channel is a normal #channel
 * or a new !channel from irc2.10.  If the latter, then it reformats it
 * a bit into a more user-friendly form.
 */
const char *	check_channel_type (const char *channel)
{
	/* Grumblesmurf */
	return channel;
#if 0
	if (*channel != '!' || strlen(channel) < 6)
		return channel;

	snprintf(new_channel_format, sizeof new_channel_format,
			"[%.6s] %s", channel, channel + 6);
	return new_channel_format;
#endif
}

int 	im_on_channel (const char *channel, int refnum)
{
	return (find_channel(channel, refnum) ? 1 : 0);
}

int 	is_on_channel (const char *channel, const char *nick)
{
	if (find_nick(from_server, channel, nick))
		return 1;
	else
		return 0;
}

int 	is_chanop (const char *channel, const char *nick)
{
	Nick *n;

	if ((n = find_nick(from_server, channel, nick)))
		return n->chanop;
	else
		return 0;
}

int	is_chanvoice (const char *channel, const char *nick)
{
	Nick *n;

	if ((n = find_nick(from_server, channel, nick)))
		return n->voice;
	else
		return 0;
}

int	is_halfop (const char *channel, const char *nick)
{
	Nick *n;

	if ((n = find_nick(from_server, channel, nick)))
		return n->half_assed;
	else
		return 0;
}

int	number_on_channel (const char *name, int server)
{
	Channel *channel = find_channel(name, server);

	if (channel)
		return channel->nicks.max;
	else
		return 0;
}

char	*create_nick_list (const char *name, int server)
{
	Channel *channel = find_channel(name, server);
	char 	*str = NULL;
	int 	i;
	size_t	clue = 0;

	if (!channel)
		return NULL;

	for (i = 0; i < channel->nicks.max; i++)
		m_sc3cat(&str, space, channel->nicks.list[i]->nick, &clue);

	return str;
}

char	*create_chops_list (const char *name, int server)
{
	Channel *channel = find_channel(name, server);
	char 	*str = NULL;
	int 	i;
	size_t	clue = 0;

	if (!channel)
		return m_strdup(empty_string);

	for (i = 0; i < channel->nicks.max; i++)
	    if (channel->nicks.list[i]->chanop)
		m_sc3cat(&str, space, channel->nicks.list[i]->nick, &clue);

	if (!str)
		return m_strdup(empty_string);
	return str;
}

char	*create_nochops_list (const char *name, int server)
{
	Channel *channel = find_channel(name, server);
	char 	*str = NULL;
	int 	i;
	size_t	clue = 0;

	if (!channel)
		return m_strdup(empty_string);

	for (i = 0; i < channel->nicks.max; i++)
	    if (!channel->nicks.list[i]->chanop)
		m_sc3cat(&str, space, channel->nicks.list[i]->nick, &clue);

	if (!str)
		return m_strdup(empty_string);
	return str;
}

/*
 *
 * Channel mode maintainance
 *
 */

/*
 * get_cmode: Get the current mode string for the specified server.
 * We only do a refresh if the mode has changed since we generated it.
 */
static char *	get_cmode (Channel *chan)
{
	int	mode_pos = 0,
		str_pos = 0;
	char	local_buffer[BIG_BUFFER_SIZE];

	/* Used the cache value if its still good */
	if ((chan->mode == chan->i_mode) && chan->s_mode)
		return chan->s_mode;

	chan->i_mode = chan->mode;

	local_buffer[0] = 0;
	while (chan->mode >= (1 << mode_pos))
	{
		if (chan->mode & (1 << mode_pos))
			local_buffer[str_pos++] = mode_str[mode_pos];
		mode_pos++;
	}
	local_buffer[str_pos] = 0;

	if (chan->key)
	{
		strlcat(local_buffer, " ", sizeof local_buffer);
		strlcat(local_buffer, chan->key, sizeof local_buffer);
	}
	if (chan->limit)
	{
		strlcat(local_buffer, " ", sizeof local_buffer);
		strlcat(local_buffer, ltoa(chan->limit), sizeof local_buffer);
	}

	malloc_strcpy(&chan->s_mode, local_buffer);
	return (chan->s_mode);
}

/*
 * Figure out type mode type for the current server.
 *
 * Type 1 is a flag meaning altering char. (+ or -);
 * Type 2 is present in a PREFIX.
 * Types 3-6 correspond to fields 1-4 of CHANMODES.
 * Type 0 is an invalid mode.
 *
 * If these variables aren't present, we chose hopefuly sane defaults.
 * Be careful not to put too many extras in those defaults, and be careful
 * not to put duplicate characters, because one will overwrite the other,
 * and of course there is bound to be at least one server that will give you
 * either PREFIX or CHANMODES but not both.  The way we escape such damage
 * now is to assume that no servers are changing the types of the rfc modes,
 * because that would be really dumb.
 *
 * There is lots of room for improvement here.  Ideally it should deal with
 * UTF8 characters and the scripters need to $serverctl(set x 005 CHANMODE)
 * gracefully.
 */
int	chanmodetype (char mode)
{
const	char	*chanmodes, *prefix;
	char	modetype = 3;

	if (strchr("+-", mode))
		return 1;

	prefix = get_server_005(from_server, "PREFIX");
	if (!prefix || *prefix++ != '(')
		prefix = "ohv";
	for (; *prefix && *prefix != ')'; prefix++)
		if (*prefix == mode)
			return 2;

	chanmodes = get_server_005(from_server, "CHANMODES");
	if (!chanmodes)
		chanmodes = "b,k,l,imnpst";
	for (; *chanmodes; chanmodes++)
		if (*chanmodes == ',')
			modetype++;
		else if (*chanmodes == mode)
			return modetype;

	return 0;
}

/*
 * decifer_mode: This will figure out the mode string as returned by mode
 * commands and convert that mode string into a one byte bit map of modes 
 */
static void	decifer_mode (const char *modes, Channel *chan)
{
	int	add = 0;
	char *	rest;
	Nick *	nick;
	int	value = 0;
	char *	mode_str;

	/* Make a copy of it.*/
	mode_str = LOCAL_COPY(modes);

	/* Punt if its not all there */
	if (!(mode_str = next_arg(mode_str, &rest)))
		return;

	for (; *mode_str; mode_str++)
	{
	    const char *arg = NULL;

	    switch (chanmodetype(*mode_str))
	    {
		case 6: case 1:
			break;
		case 5:
			if (!add) break;
		case 4: case 3: case 2:
			if ((arg = next_arg(rest, &rest)))
				break;
			yell("WARNING:  Mode parser or server is BROKE.  Mode=%c%c args: %s",
					add ? '+' : '-', *mode_str, rest);
		default:
			yell("Defaulting %c%c to CHANMODE type D", add?'+':'-', *mode_str);
	    }

	    switch (*mode_str)
	    {
		case '+':
			add = 1;
			continue;
		case '-':
			add = 0;
			continue;

		case 'a':
			value = MODE_ANONYMOUS;
			break;
		case 'c':
			value = MODE_C;
			break;
		case 'C':
			value = MODE_C2;
			break;
		case 'D':
			value = MODE_D;
			break;
		case 'i':
			value = MODE_INVITE;
			break;
		case 'm':
			value = MODE_MODERATED;
			break;
		case 'n':
			value = MODE_MSGS;
			break;
		case 'p':
			value = MODE_PRIVATE;
			break;
		case 'r':
			value = MODE_REGISTERED;
			break;
		case 'R':
			value = MODE_RESTRICTED;
			break;
		case 's':
			value = MODE_SECRET;
			break;
		case 't':
			value = MODE_TOPIC;
			break;
		case 'z':		/* Erf/TS4 "zapped" */
			value = MODE_Z;
			break;
		case 'M':		/* Duhnet's mute-mode */
			value = MODE_M;
			break;
		case 'O':		/* Duhhnet's oper-only */
			value = MODE_OPER_ONLY;
			break;
		case 'k':
		{
			if (!arg)
			{
			    yell("Channel %s is +k, but has no key.  "
				 "This server broke backwards compatability",
				 chan->channel);
			    continue;
			}

			value = MODE_KEY;

			if (add)
				malloc_strcpy(&chan->key, arg);
			else
				new_free(&chan->key);

			chan->i_mode = -1;	/* Revoke old cache */
			break;	
		}
		case 'l':
		{
			value = MODE_LIMIT;
			if (!add)
				arg = zero;

			chan->limit = my_atol(arg);
			chan->i_mode = -1;	/* Revoke old cache */
			continue;
		}

		case 'o':
		{
			/* 
			 * Borked av2.9 sends a +o to the channel
			 * when you create it, but doesnt bother to
			 * send your nickname, too. blah.
			 */
			if (!arg)
			    arg = get_server_nickname(from_server);

			if (is_me(from_server, arg))
				chan->chop = add;
			if ((nick = find_nick_on_channel(chan, arg)))
				nick->chanop = add;
			continue;
		}
		case 'v':
		{
			if (!arg)
			{
				yell("Channel %s got a mode +v "
				     "without an argument.  "
				     "This server broke backwards compatability",
					chan->channel);
				continue;
			}

			if (is_me(from_server, arg))
				chan->voice = add;
			if ((nick = find_nick_on_channel(chan, arg)))
				nick->voice = add;
			continue;
		}
		case 'h': /* erfnet's borked 'half-assed oper' mode */
		{
			if (!arg)
			{
				yell("Channel %s got a mode +h "
				     "without an argument.  "
				     "This server broke backwards compatability",
					chan->channel);
				continue;
			}

			if (is_me(from_server, arg))
				chan->half_assed = add;
			if ((nick = find_nick_on_channel(chan, arg)))
				nick->half_assed = add;
			continue;
		}
		case 'b':
		case 'e':	/* borked erfnet ban exceptions */
		case 'I':	/* borked ircnet invite exceptions */
		{
			continue;
		}
	    }

	    if (add)
		chan->mode |= value;
	    else
		chan->mode &= ~value;
	}
	if (rest && *rest)
		yell("WARNING:  Mode parser or server is BROKE.  Remaining args: %s", rest);

	if (!chan->limit)
		chan->mode &= ~MODE_LIMIT;
	else
		chan->mode |= MODE_LIMIT;
}

/* XXX Probably doesnt belong here. im tired, though */
int	channel_is_syncing (const char *channel, int server)
{
	Channel *tmp = find_channel(channel, server);

	if (tmp && tmp->waiting)
		return 1;
	else
		return 0;
}

void	channel_not_waiting (const char *channel, int server)
{
	Channel *tmp = find_channel(channel, server);

	if (tmp)
	{
		tmp->waiting = 0;
		message_from(channel, LOG_CRAP);
		do_hook(CHANNEL_SYNC_LIST, "%s %f %d",
			tmp->channel, 
			time_diff(tmp->join_time, get_time(NULL)),
			tmp->server);
		message_from(NULL, LOG_CRAP);
	}
	else
		yell("Channel_sync -- didn't find [%s:%d]",
			channel, server);
}

void 	update_channel_mode (const char *channel, const char *mode)
{
	Channel *tmp = find_channel(channel, from_server);

	if (tmp)
		decifer_mode(mode, tmp);
	update_all_status();
}

const char 	*get_channel_key (const char *channel, int server)
{
	Channel *tmp = find_channel(channel, server);

	if (tmp && tmp->key)
		return tmp->key;
	else
		return empty_string;
}


char	*get_channel_mode (const char *channel, int server)
{
	Channel *tmp = find_channel(channel, server);

	if (tmp)
		return get_cmode(tmp);
	else
		return (empty_string);
}


/*
 * is_channel_mode: returns the logical AND of the given mode with the
 * channels mode.  Useful for testing a channels mode 
 */
int 	is_channel_private (const char *channel, int server_index)
{
	Channel *tmp = find_channel(channel, server_index);

	if (tmp)
		return (tmp->mode & (MODE_PRIVATE | MODE_SECRET));
	else
		return 0;
}

int	is_channel_nomsgs (const char *channel, int server_index)
{
	Channel *tmp = find_channel(channel, server_index);

	if (tmp)
		return (tmp->mode & (MODE_MSGS));
	else
		return 0;
}



/*
 * 
 * ----- misc stuff -----
 *
 */
static void 	show_channel (Channel *chan)
{
	NickList 	*tmp = &chan->nicks;
	char		local_buf[BIG_BUFFER_SIZE * 10 + 1];
	char		*ptr;
	int		nick_len;
	int		len;
	int		i;

	ptr = local_buf;
	*ptr = 0;
	nick_len = BIG_BUFFER_SIZE * 10;

	for (i = 0; i < tmp->max; i++)
	{
		strlcpy(ptr, tmp->list[i]->nick, nick_len);
		if (tmp->list[i]->userhost)
		{
			strlcat(ptr, "!", nick_len);
			strlcat(ptr, tmp->list[i]->userhost, nick_len);
		}
		strlcat(ptr, space, nick_len);

		len = strlen(ptr);
		nick_len -= len;
		ptr += len;

		if (nick_len <= 0)
			break;		/* No more space. */
	}

	say("\t%s +%s (%s) (Win: %d): %s", 
		chan->channel, 
		get_cmode(chan),
		get_server_name(chan->server), 
		chan->winref > 0 ? chan->winref : -1,
		local_buf);
}

char	*scan_channel (char *cname)
{
	Channel 	*wc = find_channel(cname, from_server);
	NickList 	*nicks;
	char		buffer[NICKNAME_LEN + 5];
	char		*retval = NULL;
	int		i;
	size_t	clue = 0;

	if (!wc)
		return m_strdup(empty_string);

	nicks = &wc->nicks;
	for (i = 0; i < nicks->max; i++)
	{
		if (nicks->list[i]->chanop)
			buffer[0] = '@';
		else if (nicks->list[i]->half_assed == 1)
			buffer[0] = '%';
		else
			buffer[0] = '.';

		if (nicks->list[i]->voice == 1)
			buffer[1] = '+';
		else if (nicks->list[i]->voice == -1)
			buffer[1] = '?';
		else
			buffer[1] = '.';

		strlcpy(buffer + 2, nicks->list[i]->nick, sizeof(buffer) - 2);
		m_sc3cat(&retval, space, buffer, &clue);
	}

	if (retval == NULL)
		return m_strdup(empty_string);		/* Don't return NULL */

	return retval;
}


/* list_channels: displays your current channel and your channel list */
void 	list_channels (void)
{
	Channel *tmp = NULL;
	const char *channame;

	if (!channel_list)
	{
		say("You are not on any channels");
		return;
	}

	if ((channame = get_echannel_by_refnum(0)))
		say("Current channel %s", channame);
	else
		say("No current channel for this window");

	say("You are on the following channels:");
	while (traverse_all_channels(&tmp, from_server, 1))
		show_channel(tmp);

	if (connected_to_server != 1)
	{
		say("Other servers:");
		tmp = NULL;
		while (traverse_all_channels(&tmp, from_server, 0))
			show_channel(tmp);
	}
}


/* This is a keybinding */
void 	switch_channels (char dumb, char *dumber)
{
	int	lowcount = current_channel_counter;
	Char *	winner = NULL;
	int	highcount = -1;
	int	current;
	int	server;
	Channel *chan;
	int	xswitch;

	current = get_window_by_refnum(0)->refnum;
	server = get_window_server(0);
	xswitch = get_int_var(SWITCH_CHANNELS_BETWEEN_WINDOWS_VAR);

	chan = NULL;
	while (traverse_all_channels(&chan, server, 1))
	{
		/* Don't switch to another window's chan's */
		if (xswitch == 0 && chan->winref != current)
			continue;

		/* Don't switch to another window's current channel */
		if (chan->winref != current &&
		    is_current_channel(chan->channel, chan->server))
			continue;

		if (chan->curr_count > highcount)
		    highcount = chan->curr_count;
		if (chan->curr_count < lowcount)
		{
		    lowcount = chan->curr_count;
		    winner = chan->channel;
		}
	}

	/*
	 * If there are no channels on this window, punt.
	 * If there is only one channel on this window, punt.
	 */
	if (winner == NULL || highcount == -1 || highcount == lowcount)
		return;

	/*
	 * Reset the oldest channel as current.
	 */
	set_channel_window(winner, server, current, 1);
}

/* 
 * This is the guts for "window_current_channel", where current-channel
 * elections occur.
 */
static Channel *window_current_channel_internal (int window, int server)
{
        Channel *       tmp = NULL;
        int             maxcount = -1;
        Channel *       winner = NULL;

        if (server == -1)
                return NULL;            /* No channel. */
        /* This can cause crashes */

        while (traverse_all_channels(&tmp, server, 1))
        {
                if (tmp->winref != window)
                        continue;
                if (tmp->inactive)
                        continue;
                if (tmp->curr_count > maxcount)
                {
                        maxcount = tmp->curr_count;
                        winner = tmp;
                }
        }

        if (winner)
                return winner;
        return NULL;
}

/* 
 * This is the guts for "get_echannel_by_refnum", 
 * the current-channel feature.
 */
const char *  window_current_channel (int window, int server)
{
        Channel *       tmp;

        if (!(tmp = window_current_channel_internal(window, server)))
                return NULL;
        return tmp->channel;
}

void 	change_server_channels (int old_s, int new_s)
{
	Channel *tmp = NULL;

	if (old_s < 0) return;		/* Sanity check */

	while (traverse_all_channels(&tmp, old_s, 1))
	{
		tmp->server = new_s;
		tmp->inactive = 1;
		/* Is there any case where this is bad? */
		clear_channel(tmp);	
	}
}

int  channel_window (const char * channel, int server)
{
	Channel *ch;

	if ((ch = find_channel(channel, server)))
                return ch->winref;
        return 0;
}

int     is_current_channel (const char *channel, int server)
{
        int  window;
        const char *  name;
 
        if ((window = channel_window(channel, server)) > 0)
                if ((name = window_current_channel(window, server)))
                        if (!my_stricmp(name, channel)) 
                                return 1;
        return 0; 
}

/*
 * This is called by connect_to_new_server(), if the new server we are going
 * to attach to is already an established connection; by close_server(), if
 * it was asked to have the server's channels thrown away; by 
 * reconnect_all_channels() after reconnect and the JOINs have been sent off.
 */
void 	destroy_server_channels (int server)
{
	Channel	*tmp = NULL;
	int	reset = 0;

	if (server == NOSERV)
		return;		/* Sanity check */

	/*
	 * Regretably, the referntial integrity of the channels is broken
	 * at this point -- meaning that we cannot use traverse_all_channels
	 * which attempts to restore referntial integrity -- but we do not
	 * want that!  So we have to slog through this on our own and kill
	 * off the channels without the assistance of traverse_all_channels.
	 */
	for (tmp = channel_list; tmp; 
		reset ? (tmp = channel_list) : (tmp = tmp->next))
	{
		reset = 0;
		if (tmp->server != server)
			continue;
		destroy_channel(tmp);
		new_free((char **)&tmp);
		reset = 1;
	}
	window_check_channels();
}


/*
 * reconnect_all_channels:  the main auto-rejoin-on-reconnect function.
 * Called after you connect to a server as part of the "continuing action"
 * of a disconnect handling event.  It gloms up the channel names on the
 * list for the current server (which at this point are just acting as 
 * placeholders), and makes sure that all the windows are all happy with 
 * their current channels, and then spits out a JOIN for all of the 
 * channels, and then trashcans the channels.
 *
 * This probably never works quite like people expect.
 */
void 	reconnect_all_channels (void)
{
	Channel *tmp = NULL;
	char	*channels = NULL;
	char	*keyed_channels = NULL;
	char	*keys = NULL;
	size_t	chan_clue = 0, kc_clue = 0, key_clue = 0;

	/* Oh, what the heck. */
	if (!get_int_var(AUTO_REJOIN_CONNECT_VAR)) {
		destroy_server_channels(from_server);
		return;
	}

	while (traverse_all_channels(&tmp, from_server, 1))
	{
		if (!tmp->inactive)
			yell("Ack.  Reconnecting channel [%s] on server [%d] "
			     "but it isn't inactive!", 
				tmp->channel, tmp->server);

		if (tmp->key)
		{
			m_sc3cat(&keyed_channels, ",", tmp->channel, &kc_clue);
			m_sc3cat(&keys, ",", tmp->key, &key_clue);
		}
		else
			m_sc3cat(&channels, ",", tmp->channel, &chan_clue);

		clear_channel(tmp);
	}

	/* This is probably useless, but its harmless. */
	save_channels(from_server);

	/*
	 * Interestingly enough, black magic on the server's part makes
	 * this work.  I sure hope they dont "break" this in the future...
	 */
	if (keyed_channels)
		send_to_server("JOIN %s %s", keyed_channels, keys);
	if (channels)
		send_to_server("JOIN %s", channels);

	new_free(&channels);
	new_free(&keyed_channels);
	new_free(&keys);

	/*
	 * I wish i didnt have to do this... :/
	 */
	destroy_server_channels(from_server);
}


const char *	what_channel (const char *nick)
{
	Channel *tmp = NULL;

	while (traverse_all_channels(&tmp, from_server, 1))
	{
		if (find_nick_on_channel(tmp, nick))
			return tmp->channel;
	}

	return NULL;
}

const char *	walk_channels (int init, const char *nick)
{
	static	Channel *tmp = (Channel *) 0;

	if (init)
		tmp = NULL;

	while (traverse_all_channels(&tmp, from_server, 1))
	{
		if (find_nick_on_channel(tmp, nick))
			return (tmp->channel);
	}

	return NULL;
}

const char *	fetch_userhost (int server, const char *nick)
{
	Channel *tmp = NULL;
	Nick *user = NULL;

	if (server == NOSERV) return NULL;		/* Sanity check */

	while (traverse_all_channels(&tmp, server, 1))
	{
		if ((user = find_nick_on_channel(tmp, nick)) && 
				user->userhost)
			return user->userhost;
	}

	return NULL;
}

int 	get_channel_oper (const char *channel, int server)
{
	Channel *chan;

	if ((chan = find_channel(channel, server)))
		return chan->chop;
	else
		return 0;
}

int	get_channel_halfop (const char *channel, int server)
{
	Channel *chan;

	if ((chan = find_channel(channel, server)))
		return chan->half_assed;
	else
		return 0;
}

int 	get_channel_voice (const char *channel, int server)
{
	Channel *chan;

	if ((chan = find_channel(channel, server)))
		return chan->voice;
	else
		return 0;
}

int	get_channel_winref (const char *channel, int server)
{
	Channel *tmp = find_channel(channel, server);

	if (tmp)
		return tmp->winref;

	return -1;
}

void 	set_channel_window (const char *channel, int server, int winref, int as_current)
{
	Channel *tmp;
	int	is_current_now;
	int	old_window;
	Char *	old_window_new_curchan;
	Char *	new_window_old_curchan;

	if (channel == NULL)
		panic("channel == NULL in set_channel_window!");

	if ((tmp = find_channel(channel, get_window_server(winref))))
	{
		/* We need to know if we are a current channel. */
		is_current_now = is_current_channel(channel, server);

		/* 
		 * Sanity check -- if we are already the current channel
		 * of the target window, then don't do anything more.
		 */
		if (is_current_now && tmp->winref == winref && as_current)
			return;

		/*
		 * We need to know the present current channel of the new
		 * target window.
		 */
		if (!(new_window_old_curchan = get_echannel_by_refnum(winref)))
			new_window_old_curchan = zero;

		/* move the channel to the new window */
		old_window = tmp->winref;
		tmp->winref = winref;
		if (as_current)
			tmp->curr_count = current_channel_counter++;
		else
			tmp->curr_count = 0;

		/* 
		 * If we moved to a new window, and we were the current
		 * channel of the old window, then we need to hold an election
		 * for a new current channel on the old window.  We also
		 * throw the switch channels thing.
	 	 */
		if (old_window != winref && old_window > 0 && is_current_now)
		{
		    channel_hold_election(old_window);
		    if (!(old_window_new_curchan = get_echannel_by_refnum(old_window)))
			old_window_new_curchan = zero;
		    do_hook(SWITCH_CHANNELS_LIST, "%d %s %s",
				old_window, channel, old_window_new_curchan);
		}

		/* 
		 * But in every case we are made a current channel,
		 * we need to hold an election for a new current channel
		 * (which we will win) and throw the switch channels thing.
	 	 */
		if (as_current)
		{
			channel_hold_election(winref);
			do_hook(SWITCH_CHANNELS_LIST, "%d %s %s",
				winref, new_window_old_curchan, channel);
		}
	}
	else
		yell("WARNING! set_channel_window is acting on a channel "
			"that doesn't exist even though the window is "
			"expecting it.  This is probably a bug. "
			"[%s] [%d]", channel, get_window_server(winref));
}


/*
 * This moves "chan" from "old_w" to "new_w", safely -- this replaces the old
 * way of "unset_window_current_channel" and "reset_window_current_channel".
 * This was written by Robohak in January 2001.
 */
void   move_channel_to_window (const char *chan, int server, int old_w, int new_w)
{
	Channel *tmp = NULL;
	const char *x;

	if (chan == NULL || strcmp(chan, zero) == 0)
		return;

	if (old_w <= 0)
		old_w = get_channel_winref(chan, server);

        if (old_w <= 0 || new_w <= 0 || get_window_server(old_w) != server)
		return;

	if (!(tmp = find_channel(chan, server)))
		return;
	if (tmp->winref != old_w)
		panic("Channel [%s:%d] is on window [%d] not on window [%d] (moving to [%d])",
			chan, server, tmp->winref, old_w, new_w);

	/* 
	 * Unbind this channel unconditionally unless the new window has
	 * already bound it.
	 */
	x = get_bound_channel_by_refnum(new_w);
	if (x && my_stricmp(chan, x))
		unbind_channel(chan, tmp->server);

	/*
	 * If the old window is waiting for this channel, make the new
	 * window wait for this channel.
	 */
	if (is_window_waiting_for_channel(old_w, chan))
		move_waiting_channel(old_w, new_w);

	/* set_channel_window calls new elections */
	set_channel_window(chan, server, new_w, 1);
}


void	channel_hold_election (int winref)
{
	if (winref <= 0)
		return;			/* Whatever.  Probably should panic */

	window_statusbar_needs_update(get_window_by_refnum(winref));
	update_all_windows();
}


/*
 * For any given window, re-assign all of the channels that are connected
 * to that window.
 */
void	reassign_window_channels (int window)
{
	Channel *tmp = NULL;
	Window *w = NULL;
	int	caution = 0;
	int	winserv;
	int	winoldserv;

	winserv = get_window_server(window);
	winoldserv = get_window_oldserver(window);

	if (winserv == NOSERV && winoldserv == NOSERV)
		caution = 1;

	for (tmp = channel_list; tmp; tmp = tmp->next)
	{
		if (tmp->winref != window)
			continue;

		if (caution)
		{
			yell("Deleting channels for disconnected window [%d] "
				"-- caution!", window);
			caution = 0;
		}

		if (tmp->server != winserv && tmp->server != winoldserv)
		{
			yell("Channel [%s:%d] is connected to window [%d] "
				"which is apparantly on another server", 
				tmp->channel, tmp->server, window);
		}

		/*
		 * Find a new home for this channel...
		 * If no new windows are found, 'winref' remains -1 and
		 * I'm not exactly quite sure what will happen there...
		 */
		tmp->winref = -1;
		while (traverse_all_windows(&w))
		{
			if (w->server == tmp->server && w->refnum != window)
			{
				tmp->winref = w->refnum;
				break;
			}
		}
	}
}

char *	create_channel_list (int server)
{
	Channel	*tmp = NULL;
	char	*retval = NULL;
	size_t	clue = 0;

	if (server >= 0)
	{
		while (traverse_all_channels(&tmp, server, 1))
			m_sc3cat(&retval, space, tmp->channel, &clue);
	}

	return retval ? retval : m_strdup(empty_string);
}

/*
 * This is (supposed to be) only ever called by close_server, and is called
 * whenever it was requested that the channels on a closing server not be
 * thrown away, but instead converted into placeholders.  So that's what we
 * do here.
 */
void 	save_channels (int servref)
{
	Window	*tmp = NULL;
	Channel *tmpc = NULL;
	const char *chan;

	/*
	 * Go through all of the windows for this server
	 * and mark their current channels as WAITING.
	 */
	while (traverse_all_windows(&tmp))
	{
		if (tmp->server != servref)
			continue;

		if (tmp->waiting_channel)
			continue;		/* Yea yea yea */

		if ((chan = get_echannel_by_refnum(tmp->refnum)))
			tmp->waiting_channel = m_strdup(chan);
	}

	/*
	 * Go through all the channels for this server
	 * and mark them as WAITING.
	 */
	while (traverse_all_channels(&tmpc, servref, 1))
		tmpc->inactive = 1;
}

/* I don't know if this belongs here. */
void	cant_join_channel (const char *channel, int server)
{
	Window *w = NULL;
	while (traverse_all_windows(&w))
	{
		if (w->server != server)
			continue;
		if (!w->waiting_channel)
			continue;
		if (!my_stricmp(w->waiting_channel, channel))
			new_free(&w->waiting_channel);
	}
	update_all_windows();
}

/**************************************************************************/
/* 
 * Im sure this doesnt belong here, but im not sure where it does belong.
 */
int 	auto_rejoin_callback (void *d)
{
	char *	data    = (char *) d;
	char *	channel	= next_arg(data, &data);
	int 	server	= parse_server_index(next_arg(data, &data), 0);
	Window *window 	= get_window_by_refnum(my_atol(next_arg(data, &data)));
	char *	key    	= next_arg(data, &data);

	if (key && *key)
		send_to_aserver(server, "JOIN %s %s", channel, key);
	else
		send_to_aserver(server, "JOIN %s", channel);

	if (window)
		malloc_strcpy(&window->waiting_channel, channel);
	new_free((char **)&d);

	return 0;
}

/*
 * match_chan_with_id: check if the channel matches.  It checks also takes
 * the ID into account for !channels
 * match can be NULL, chan should never be NULL.
 * it returns 0 when they amtch, something else when not.
 */
static int	match_chan_with_id (const char *chan, const char *match)
{
	int	i = 1;

	if (!match)
		return 1;

	if (*chan == '!' && *match == '!')
	{
		/*
		 * Check that it's a channel being created.  If so, skip
		 * that char.
		 */
		if (match[1] == '!')
			i++;

		/*
		 * !IDchan should also match !chan, which is how we usually
		 * see it. (the ID is 5 chars long)
		 */
		return (my_stricmp(chan + 1, match + i) &&
			my_stricmp(chan + 6, match + i));
	}

	return my_stricmp(chan, match);
}


/*
 *
 */
void	channel_check_windows (void)
{
	Channel	*tmp = NULL;
	Window *w = NULL;
	int	reset = 0;

	/* Do test #3 -- check for windowless channels */
	for (tmp = channel_list; tmp; 
		reset ? (tmp = channel_list) : (tmp = tmp->next))
	{
		reset = 0;

		if (tmp->winref > 0)
			continue;

		w = NULL;
		while (traverse_all_windows(&w))
		{
		    if (w->server == tmp->server)
		    {
			yell("Repaired referential integrity failure: "
			     "channel [%s] on server [%d] was not "
			     "connected to any window; moved to "
			     "window refnum [%d]",
				tmp->channel, tmp->server, 
				w->refnum);
			tmp->winref = w->refnum;
			reset = 1;
			break;
		    }
		}

		if (tmp->winref <= 0)
		{
			yell("Repaired referential integrity failure: "
			     "server [%d] has channels, but no "
			     "windows -- throwing away all of these "
			     "server's channels", tmp->server);
			destroy_server_channels(tmp->server);
			update_all_windows();
			reset = 1;
		}
	}

	/* Do test #4 -- check for windows from other servers */
	for (tmp = channel_list; tmp; 
		reset ? (tmp = channel_list) : (tmp = tmp->next))
	{
		if (tmp->winref <= 0)
			panic("I thought we just checked for this! [1]");

		if (get_window_server(tmp->winref) == NOSERV && 
			     tmp->server == get_window_oldserver(tmp->winref))
			continue;			/* This is OK. */

		if (tmp->server != get_window_server(tmp->winref))
			panic("Referential integrity failure: "
			      "Channel [%s] on server [%d] is connected "
			      "to window [%d] on server [%d]",
				tmp->channel, tmp->server, 
				tmp->winref, get_window_server(tmp->winref));
	}

	/* Do test #5 -- check for bogus windows */
	for (tmp = channel_list; tmp; 
		reset ? (tmp = channel_list) : (tmp = tmp->next))
	{
		if (tmp->winref <= 0)
			panic("I thought we just checked for this! [2]");

		if (!get_window_by_refnum(tmp->winref))
			panic("Referential integrity failure: "
			      "Channel [%s] on server [%d] is connected "
			      "to window [%d] "
			      "that doesn't exist any more!",
				tmp->channel, tmp->server, 
				tmp->winref);
	}

	/* Do test #7 -- check for abandoned channels */
	for (tmp = channel_list; tmp;
		reset ? (tmp = channel_list) : (tmp = tmp->next))
	{
		if (tmp->winref <= 0)
			panic("I thought we just checked for this! [4]");

		if (did_server_rejoin_channels(tmp->server) && tmp->inactive)
			panic("Channel [%s] on server [%d] is inactive "
				"even though this server is connected!",
				tmp->channel, tmp->server);

		if (is_server_registered(tmp->server) &&
				!did_server_rejoin_channels(tmp->server) && 
				!tmp->inactive)
			panic("Channel [%s] on server [%d] is NOT inactive "
				"even though this server is NOT connected!",
				tmp->channel, tmp->server);
	}

	return;
}
