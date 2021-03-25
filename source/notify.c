/*
 * notify.c: a few handy routines to notify you when people enter and leave irc 
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright 1997, 2003 EPIC Software Labs.
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
 * Revamped by lynX - Dec '91
 * Expanded to allow notify on every server in Feb 1996.
 * Mostly rewritten in Dec 1997.
 */

#define NEED_SERVER_LIST
#include "irc.h"
#include "alist.h"
#include "notify.h"
#include "ircaux.h"
#include "hook.h"
#include "server.h"
#include "output.h"
#include "parse.h"
#include "vars.h"
#include "who.h"
#include "clock.h"
#include "timer.h"
#include "window.h"
#include "input.h"

void 	batch_notify_userhost		(const char *nick);
void 	dispatch_notify_userhosts	(int);
void 	notify_userhost_dispatch	(int, UserhostItem *f, const char *, const char *);
void 	notify_userhost_reply		(int, const char *nick, const char *uh);

/* NotifyList: the structure for the notify stuff */
typedef	struct	notify_stru
{
	char	*nick;			/* Who are we watching? */
	u_32int_t hash;			/* Hash of the nick */
	int	flag;			/* Is the person on irc? */
} NotifyItem;

#if 0		/* Declared in notify.h */
typedef struct	notify_alist
{
	NotifyItem **	list;
	int		max;
	int		max_alloc;
	alist_func	func;
	hash_type	hash;
	char *		ison;
} NotifyList;
#endif

static 	void	ison_notify (int refnum, char *AskedFor, char *AreOn);
static 	void	rebuild_notify_ison 	(int server);


#define NOTIFY_LIST(s)		(&(s->notify_list))
#define NOTIFY_MAX(s)  		(s->notify_list.max)
#define NOTIFY_ITEM(s, i) 	(s->notify_list.list[i])

static void	show_notify_list (int all)
{
	Server *s;
	int	i;
	char	*list = (char *) 0;
	size_t	clue;

	if (!(s = get_server(from_server)))
		return;

	for (i = 0, clue = 0; i < NOTIFY_MAX(s); i++)
	{
		if (NOTIFY_ITEM(s, i)->flag)
		    malloc_strcat_wordlist_c(&list, space, NOTIFY_ITEM(s, i)->nick, &clue);
	}

	if (list)
		say("Currently present: %s", list);

	if (all)
	{
		new_free(&list);
		for (i = 0, clue = 0; i < NOTIFY_MAX(s); i++)
		{
			if (!NOTIFY_ITEM(s, i)->flag)
			    malloc_strcat_wordlist_c(&list, space, NOTIFY_ITEM(s, i)->nick, &clue);
		}
		if (list) 
			say("Currently absent: %s", list);
	}
	new_free(&list);
}

static	void	rebuild_all_ison (void)
{
	int i;

	for (i = 0; i < number_of_servers; i++)
		rebuild_notify_ison(i);
}

static void	rebuild_notify_ison (int refnum)
{
	Server *s;
	int i;
	size_t clue = 0;

	if (!(s = get_server(refnum)))
		return;		/* No server, no go */

	if (NOTIFY_LIST(s)->ison)
		NOTIFY_LIST(s)->ison[0] = 0;

	for (i = 0; i < NOTIFY_MAX(s); i++)
	{
		malloc_strcat_wordlist_c(&(NOTIFY_LIST(s)->ison),
			space, NOTIFY_ITEM(s, i)->nick, &clue);
	}
}


/* notify: the NOTIFY command.  Does the whole ball-o-wax */
BUILT_IN_COMMAND(notify)
{
	Server		*s;
	char		*nick,
			*list = (char *) 0,
			*ptr;
	int		no_nicks = 1;
	int		do_ison = 0;
	int		shown = 0;
	NotifyItem	*new_n;
	int		refnum, first = 0, last = number_of_servers;
	int		added = 0;
	size_t		clue = 0;

	malloc_strcpy(&list, empty_string);
	while ((nick = next_arg(args, &args)))
	{
	    for (no_nicks = 0; nick; nick = ptr)
	    {
		shown = 0;
		if ((ptr = strchr(nick, ',')))
		    *ptr++ = '\0';

		if (0 <= from_server && !my_stricmp(":", nick))
			first = last = from_server, last++;
		else if (*nick == '-')
		{
		    nick++;
		    if (*nick)
		    {
			for (refnum = first; refnum < last; refnum++)
			{
			    if (!(s = get_server(refnum)))
				continue;

			    if ((new_n = (NotifyItem *)remove_from_array(
					(array *)NOTIFY_LIST(s), nick)))
			    {
				new_free(&(new_n->nick));
				new_free((char **)&new_n);

				if (!shown)
				{
				    say("%s removed from notify list", nick);
				    shown = 1;
				}
			    }
			    else
			    {
				if (!shown)
				{
				    say("%s is not on the notify list", nick);
				    shown = 1;
				}
			    }
			}
		    }
		    else
		    {
			for (refnum = first; refnum < last; refnum++)
			{
			    if (!(s = get_server(refnum)))
				continue;

			    while ((new_n = (NotifyItem *)array_pop(
						(array *)NOTIFY_LIST(s), 0)))
			    {
				new_free(&new_n->nick);
				new_free((char **)&new_n);
			    }
			}
			say("Notify list cleared");
		    }
		}
		else
		{
		    /* compatibility */
		    if (*nick == '+')
			nick++;

		    if (!*nick)
		    {
			show_notify_list(0);
			continue;
		    }

		    if (strchr(nick, '*'))
		    {
			say("Wildcards not allowed in NOTIFY nicknames!");
			continue;
		    }

		    for (refnum = first; refnum < last; refnum++)
		    {
			if (!(s = get_server(refnum)))
			    continue;

			if ((new_n = (NotifyItem *)array_lookup(
					(array *)NOTIFY_LIST(s), nick, 0, 0)))
			{
			    continue;	/* Already there! */
			}

			new_n = (NotifyItem *)new_malloc(sizeof(NotifyItem));
			new_n->nick = malloc_strdup(nick);
			new_n->flag = 0;
			add_to_array((array *)NOTIFY_LIST(s), 
					(array_item *)new_n);
			added = 1;
		     }

		    if (added)
		    {
			malloc_strcat_wordlist_c(&list, space, nick, &clue);
			do_ison = 1;
		    }

		    say("%s added to the notification list", nick);
		}
	    } /* End of for */
	} /* End of while */

	if (do_ison && get_int_var(DO_NOTIFY_IMMEDIATELY_VAR))
	{
	    for (refnum = first; refnum < last; refnum++)
	    {
		if (!(s = get_server(refnum)))
		    continue;

		if (is_server_registered(refnum) && list && *list)
			isonbase(refnum, list, ison_notify);
	    }
	}

	new_free(&list);
	rebuild_all_ison();

	if (no_nicks)
	    show_notify_list(1);
}

static void	ison_notify (int refnum, char *AskedFor, char *AreOn)
{
	char	*NextAsked;
	char	*NextGot;

	if (x_debug & DEBUG_NOTIFY)
		yell("Checking notify: we asked for [%s] and we got back [%s]", AskedFor, AreOn);

	NextGot = next_arg(AreOn, &AreOn);
	if (x_debug & DEBUG_NOTIFY)
		yell("Looking for [%s]", NextGot ? NextGot : "<Nobody>");
	while ((NextAsked = next_arg(AskedFor, &AskedFor)) != NULL)
	{
		if (NextGot && !my_stricmp(NextAsked, NextGot))
		{
			if (x_debug & DEBUG_NOTIFY)
				yell("OK.  Found [%s].", NextAsked);
			notify_mark(refnum, NextAsked, 1, 1);
			NextGot = next_arg(AreOn, &AreOn);
			if (x_debug & DEBUG_NOTIFY)
				yell("Looking for [%s]", NextGot ? NextGot : "<Nobody>");
		}
		else
		{
			if (x_debug & DEBUG_NOTIFY)
				yell("Well, [%s] doesnt look to be on.", NextAsked);
			notify_mark(refnum, NextAsked, 0, 1);
		}
	}

	if (x_debug & DEBUG_NOTIFY)
		if (AreOn && *AreOn)
			yell("Hrm.  There's stuff left in AreOn, and there shouldn't be. [%s]", AreOn);

	dispatch_notify_userhosts(refnum);
}

/*
 * do_notify:  This goes through the master notify list and collects it
 * into groups of 500 character sets.  Then it sends this out to each 
 * connected server.  It repeats this until the whole list has been parsed.
 */
void 	do_notify (void)
{
	Server 		*s;
	int 		old_from_server = from_server;
	int		servnum;

	if (!number_of_servers)
		return;

	if (x_debug & DEBUG_NOTIFY)
		yell("do_notify() was called...");

	for (servnum = 0; servnum < number_of_servers; servnum++)
	{
		if (!(s = get_server(servnum)))
			continue;

		if (!is_server_registered(servnum))
		{
		    if (x_debug & DEBUG_NOTIFY)
			yell("Server [%d] is not connected so we "
			     "will not issue an ISON for it.", servnum);
		    continue;
		}

		from_server = servnum;
		if (NOTIFY_LIST(s)->ison && *NOTIFY_LIST(s)->ison
				&& !s->ison_wait)
		{
			isonbase(servnum, NOTIFY_LIST(s)->ison, ison_notify);
			if (x_debug & DEBUG_NOTIFY)
			    yell("Notify ISON issued for server [%d] with [%s]"
				, servnum, NOTIFY_LIST(s)->ison);
		}
		else if (NOTIFY_MAX(s))
		{
			if (x_debug & DEBUG_NOTIFY)
				yell("Server [%d]'s notify list is"
					"empty and it shouldn't be.",
						servnum);
			rebuild_notify_ison(servnum);
		}
		else if (x_debug & DEBUG_NOTIFY)
			yell("Server [%d]'s notify list is empty.",
				servnum);
	}

	if (x_debug & DEBUG_NOTIFY)
		yell("do_notify() is all done...");
	from_server = old_from_server;
	return;
}

/*
 * notify_mark: This marks a given person on the notify list as either on irc
 * (if flag is 1), or not on irc (if flag is 0).  If the person's status has
 * changed since the last check, a message is displayed to that effect.  If
 * the person is not on the notify list, this call is ignored 
 * doit if passed as 0 means it comes from a join, or a msg, etc, not from
 * an ison reply.  1 is the other..
 *
 * We implicitly assume that if this is called with 'doit' set to 0 that
 * the caller has already "registered" the user.  What that means is that
 * when the caller does a notify_mark for a single user, a USERHOST request
 * for that user should yeild cached userhost information.  This is a terrible
 * thing to assume, but it wont break if cached userhosts go away, we'd just
 * be doing an extra server request -- which we're already doing anyhow! --
 * so in the worst case this cant be any worse than it is already.
 */
void 	notify_mark (int refnum, const char *nick, int flag, int doit)
{
	Server		*s;
	NotifyItem 	*tmp;
	int		count;
	int		loc;

	if (!(s = get_server(refnum)))
		return;

	if ((tmp = (NotifyItem *)find_array_item(
				(array *)NOTIFY_LIST(s), nick, 
				&count, &loc)) && count < 0)
	{
		if (flag)
		{
			if (tmp->flag != 1)
			{
			    if (get_int_var(NOTIFY_USERHOST_AUTOMATIC_VAR))
			        batch_notify_userhost(nick);
			    else
				notify_userhost_reply(refnum, nick, NULL);
			}
			tmp->flag = 1;

			if (!doit)
				dispatch_notify_userhosts(refnum);
		}
		else
		{
			if (tmp->flag == 1)
			{
			    if (do_hook(NOTIFY_SIGNOFF_LIST, "%s", nick))
				say("Signoff by %s detected", nick);
			}
			tmp->flag = 0;
		}
	}
	else
	{
		if (x_debug & DEBUG_NOTIFY)
			yell("Notify_mark called for [%s], they dont seem to exist!", nick);
	}
}


static char *	batched_notify_userhosts = NULL;
static int 	batched_notifies = 0;

void 	batch_notify_userhost (const char *nick)
{
	malloc_strcat_wordlist(&batched_notify_userhosts, space, nick);
	batched_notifies++;
}

void 	dispatch_notify_userhosts (int refnum)
{
	if (batched_notify_userhosts)
	{
		if (x_debug & DEBUG_NOTIFY)
			yell("Dispatching notifies to server [%d], [%s]", from_server, batched_notify_userhosts);
		userhostbase(refnum, batched_notify_userhosts, NULL, notify_userhost_dispatch, 1);
		new_free(&batched_notify_userhosts);
		batched_notifies = 0;
	}
}

void 	notify_userhost_dispatch (int refnum, UserhostItem *stuff, const char *nick, const char *text)
{
	char uh[BIG_BUFFER_SIZE + 1];

	snprintf(uh, sizeof uh, "%s@%s", stuff->user, stuff->host);
	notify_userhost_reply(refnum, stuff->nick, uh);
}

void 	notify_userhost_reply (int refnum, const char *nick, const char *uh)
{
	Server *s;
	NotifyItem *tmp;

	if (!uh)
		uh = empty_string;

	if (!(s = get_server(refnum)))
		return;

	if ((tmp = (NotifyItem *)array_lookup((array *)NOTIFY_LIST(s), nick, 0, 0)))
	{
		if (do_hook(NOTIFY_SIGNON_LIST, "%s %s", nick, uh))
		{
			if (*uh)
				say("Signon by %s!%s detected", nick, uh);
			else
				say("Signon by %s detected", nick);
		}

		/*
		 * copy the correct case of the nick
		 * into our array  ;)
		 */
		malloc_strcpy(&tmp->nick, nick);
		malloc_strcpy(&last_notify_nick, nick);
	}
}


void 	make_notify_list (int refnum)
{
	Server *s, *sp = NULL;
	NotifyItem *tmp;
	char *list = NULL;
	int i;
	size_t clue = 0;

	if (!(s = get_server(refnum)))
		return;

	s->notify_list.list = NULL;
	s->notify_list.max = 0;
	s->notify_list.max_alloc = 0;
	/* XXX - Which of these two is correct?  Neither? */
	/* s->notify_list.func = (alist_func)my_strnicmp; */
	s->notify_list.func = (alist_func)my_stricmp; 
	s->notify_list.hash = HASH_INSENSITIVE;
	s->notify_list.ison = NULL;

	for (i = 0; i < number_of_servers; i++)
		if ((sp = get_server(i)) && NOTIFY_MAX(sp))
			break;
	if (!sp)
		return;			/* No notify list to copy. */

	for (i = 0; i < NOTIFY_MAX(sp); i++)
	{
		tmp = (NotifyItem *)new_malloc(sizeof(NotifyItem));
		tmp->nick = malloc_strdup(NOTIFY_ITEM(sp, i)->nick);
		tmp->flag = 0;

		add_to_array ((array *)NOTIFY_LIST(s), (array_item *)tmp);
		malloc_strcat_wordlist_c(&list, space, tmp->nick, &clue);
	}

	if (list && !s->ison_wait)
	{
		isonbase(refnum, list, ison_notify);
		new_free(&list);
	}
}

void	destroy_notify_list (int refnum)
{
	Server *s;
	NotifyItem *	item;

	if (!(s = get_server(refnum)))
		return;

	while (NOTIFY_MAX(s))
	{
		item = (NotifyItem *) array_pop((array *)NOTIFY_LIST(s), 0);
		if (item) 
			new_free(&item->nick);
		new_free(&item);
	}
	new_free(&(NOTIFY_LIST(s)->ison));
	new_free(NOTIFY_LIST(s));
}

char *	get_notify_nicks (int refnum, int showon)
{
	Server *s;
	char *list = NULL;
	int i;
	size_t rvclue=0;

	if (!(s = get_server(refnum)))
		return malloc_strdup(empty_string);

	for (i = 0; i < NOTIFY_MAX(s); i++)
	{
		if (showon == -1 || showon == NOTIFY_ITEM(s, i)->flag)
			malloc_strcat_wordlist_c(&list, space, NOTIFY_ITEM(s, i)->nick, &rvclue);
	}

	return (list ? list : malloc_strdup(empty_string));
}

/***************************************************************************/
char 	notify_timeref[] = "NOTTIM";

void	notify_systimer (void)
{
	do_notify();
}

void    set_notify_interval (void *stuff)
{
	update_system_timer(notify_timeref);
}

void    set_notify (void *stuff)
{
	update_system_timer(notify_timeref);
}

