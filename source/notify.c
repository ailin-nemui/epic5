/*
 * notify.c: a few handy routines to notify you when people enter and leave irc 
 *
 * Written By Michael Sandrof
 * Copyright 1990, 1995 Michael Sandroff and others
 * Copyright 1997 EPIC Software Labs
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 *
 * Revamped by lynX - Dec '91
 * Expanded to allow notify on every server by hop, Feb 1996.
 * Mostly rewritten by jfn (esl), Dec 1997.
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

void 	batch_notify_userhost		(char *nick);
void 	dispatch_notify_userhosts	(void);
void 	notify_userhost_dispatch	(UserhostItem *f, char *, char *);
void 	notify_userhost_reply		(char *nick, char *userhost);

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

	void	ison_notify 		(char *, char *);
static 	void	rebuild_notify_ison 	(int server);


#define NOTIFY_LIST(s)		(&(server_list[s].notify_list))
#define NOTIFY_MAX(s)  		(NOTIFY_LIST(s)->max)
#define NOTIFY_ITEM(s, i) 	(NOTIFY_LIST(s)->list[i])

void	show_notify_list (int all)
{
	int	i;
	char	*list = (char *) 0;

	if (from_server == -1)
		return;

	for (i = 0; i < NOTIFY_MAX(from_server); i++)
	{
		if (NOTIFY_ITEM(from_server, i)->flag)
		    m_s3cat(&list, space, NOTIFY_ITEM(from_server, i)->nick);
	}

	if (list)
		say("Currently present: %s", list);

	if (all)
	{
		new_free(&list);
		for (i = 0; i < NOTIFY_MAX(from_server); i++)
		{
			if (!NOTIFY_ITEM(from_server, i)->flag)
			    m_s3cat(&list, space, NOTIFY_ITEM(from_server, i)->nick);
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

static void	rebuild_notify_ison (int server)
{
	char *stuff;
	int i;

	if (from_server == -1)
		return;		/* No server, no go */

	stuff = NOTIFY_LIST(from_server)->ison;

	if (NOTIFY_LIST(from_server)->ison)
		NOTIFY_LIST(from_server)->ison[0] = 0;

	for (i = 0; i < NOTIFY_MAX(from_server); i++)
	{
		m_s3cat(&(NOTIFY_LIST(from_server)->ison),
			space, NOTIFY_ITEM(from_server, i)->nick);
	}
}


/* notify: the NOTIFY command.  Does the whole ball-o-wax */
BUILT_IN_COMMAND(notify)
{
	char		*nick,
			*list = (char *) 0,
			*ptr;
	int		no_nicks = 1;
	int		do_ison = 0;
	int		shown = 0;
	NotifyItem	*new_n;
	int		servnum;
	int		added = 0;

	malloc_strcpy(&list, empty_string);
	while ((nick = next_arg(args, &args)))
	{
		for (no_nicks = 0; nick; nick = ptr)
		{
shown = 0;
if ((ptr = strchr(nick, ',')))
	*ptr++ = '\0';

if (*nick == '-')
{
	nick++;
	if (*nick)
	{
		for (servnum = 0; servnum < number_of_servers; servnum++)
		{
			if ((new_n = (NotifyItem *)remove_from_array(
				(array *)NOTIFY_LIST(servnum), nick)))
			{
				new_free(&(new_n->nick));
				new_free((char **)&new_n);

				if (!shown)
				{
					say("%s removed from notify list",
						nick);
					shown = 1;
				}
			}
			else
			{
				if (!shown)
				{
					say("%s is not on the notify list", 
						nick);
					shown = 1;
				}
			}
		}
	}
	else
	{
		for (servnum = 0; servnum < number_of_servers; servnum++)
		{
			while ((new_n = (NotifyItem *)array_pop(
				(array *)NOTIFY_LIST(servnum), 0)))
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

	for (servnum = 0; servnum < number_of_servers; servnum++)
	{
		if ((new_n = (NotifyItem *)array_lookup(
				(array *)NOTIFY_LIST(servnum), nick, 0, 0)))
		{
			continue;	/* Already there! */
		}

		new_n = (NotifyItem *)new_malloc(sizeof(NotifyItem));
		new_n->nick = m_strdup(nick);
		new_n->flag = 0;
		add_to_array((array *)NOTIFY_LIST(servnum), 
			     (array_item *)new_n);
		added = 1;
	}

	if (added)
	{
		m_s3cat(&list, space, new_n->nick);
		do_ison = 1;
	}

	say("%s added to the notification list", nick);
}

		} /* End of for */
	} /* End of while */

	if (do_ison && get_int_var(DO_NOTIFY_IMMEDIATELY_VAR))
	{
		int ofs = from_server;

		for (servnum = 0; servnum < number_of_servers; servnum++)
		{
			from_server = servnum;
			if (is_server_connected(from_server))
				if (list && *list)
					isonbase(list, ison_notify);
		}
		from_server = ofs;
	}

	new_free(&list);
	rebuild_all_ison();

	if (no_nicks)
		show_notify_list(1);
}

void	ison_notify (char *AskedFor, char *AreOn)
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
			notify_mark(NextAsked, 1, 1);
			NextGot = next_arg(AreOn, &AreOn);
			if (x_debug & DEBUG_NOTIFY)
				yell("Looking for [%s]", NextGot ? NextGot : "<Nobody>");
		}
		else
		{
			if (x_debug & DEBUG_NOTIFY)
				yell("Well, [%s] doesnt look to be on.", NextAsked);
			notify_mark(NextAsked, 0, 1);
		}
	}

	if (x_debug & DEBUG_NOTIFY)
		if (AreOn && *AreOn)
			yell("Hrm.  There's stuff left in AreOn, and there shouldn't be. [%s]", AreOn);

	dispatch_notify_userhosts();
}

/*
 * do_notify:  This goes through the master notify list and collects it
 * into groups of 500 character sets.  Then it sends this out to each 
 * connected server.  It repeats this until the whole list has been parsed.
 */
void 	do_notify (void)
{
	int 		old_from_server = from_server;
	int		servnum;
static	time_t		next_notify = 0;
	int		interval = get_int_var(NOTIFY_INTERVAL_VAR);

	if (!number_of_servers)
		return;

	if (x_debug & DEBUG_NOTIFY)
		yell("do_notify() was called...");

	if (!next_notify)
		next_notify = time(NULL);
	else if (time(NULL) < next_notify)
	{
		if (x_debug & DEBUG_NOTIFY)
			yell("Not time for notify yet [%ld] [%ld]",
				time(NULL), next_notify);

		return;		/* Not yet */
	}

	/* We want to do it *on average* every 'interval' seconds */
	next_notify += interval;
	for (servnum = 0; servnum < number_of_servers; servnum++)
	{
		if (is_server_connected(servnum))
		{
			from_server = servnum;
			if (NOTIFY_LIST(servnum)->ison && 
			    *NOTIFY_LIST(servnum)->ison)
			{
				isonbase(NOTIFY_LIST(servnum)->ison, ison_notify);
				if (x_debug & DEBUG_NOTIFY)
				    yell("Notify ISON issued for server [%d]", 
						servnum);
			}
			else if (NOTIFY_MAX(servnum))
			{
				if (x_debug & DEBUG_NOTIFY)
					yell("Server [%d]'s notify list is"
						"empty and it shouldn't be.",
							servnum);
				rebuild_all_ison();
			}
			else if (x_debug & DEBUG_NOTIFY)
				yell("Server [%d]'s notify list is empty.",
					servnum);
		}
		else if (x_debug & DEBUG_NOTIFY)
			yell("Server [%d] is not connected so we "
			     "will not issue an ISON for it.", servnum);
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
void 	notify_mark (char *nick, int flag, int doit)
{
	NotifyItem 	*tmp;
	int		count;
	int		loc;

	if ((tmp = (NotifyItem *)find_array_item(
				(array *)NOTIFY_LIST(from_server), nick, 
				&count, &loc)) && count < 0)
	{
		if (flag)
		{
			if (tmp->flag != 1)
			{
			    if (get_int_var(NOTIFY_USERHOST_AUTOMATIC_VAR))
			        batch_notify_userhost(nick);
			    else
				notify_userhost_reply(nick, NULL);
			}
			tmp->flag = 1;

			if (!doit)
				dispatch_notify_userhosts();
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

void 	batch_notify_userhost (char *nick)
{
	m_s3cat(&batched_notify_userhosts, space, nick);
	batched_notifies++;
}

void 	dispatch_notify_userhosts (void)
{
	if (batched_notify_userhosts)
	{
		if (x_debug & DEBUG_NOTIFY)
			yell("Dispatching notifies to server [%d], [%s]", from_server, batched_notify_userhosts);
		userhostbase(batched_notify_userhosts, notify_userhost_dispatch, 1);
		new_free(&batched_notify_userhosts);
		batched_notifies = 0;
	}
}

void 	notify_userhost_dispatch (UserhostItem *stuff, char *nick, char *text)
{
	char userhost[BIG_BUFFER_SIZE + 1];

	snprintf(userhost, BIG_BUFFER_SIZE, "%s@%s", stuff->user, stuff->host);
	notify_userhost_reply(stuff->nick, userhost);
}

void 	notify_userhost_reply (char *nick, char *userhost)
{
	NotifyItem *tmp;

	if (!userhost)
		userhost = empty_string;

	if ((tmp = (NotifyItem *)array_lookup(
				(array *)NOTIFY_LIST(from_server), nick, 0, 0)))
	{
		if (do_hook(NOTIFY_SIGNON_LIST, "%s %s", nick, userhost))
		{
			if (!*userhost)
				say("Signon by %s!%s detected", nick, userhost);
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


void 	save_notify (FILE *fp)
{
	int i;

	if (number_of_servers && NOTIFY_MAX(0))
	{
		fprintf(fp, "NOTIFY");
		for (i = 0; i < NOTIFY_MAX(0); i++)
			fprintf(fp, " %s", NOTIFY_ITEM(0, i)->nick);
		fprintf(fp, "\n");
	}
}

void 	make_notify_list (int servnum)
{
	NotifyItem *tmp;
	char *list = NULL;
	int i;

	server_list[servnum].notify_list.list = NULL;
	server_list[servnum].notify_list.max = 0;
	server_list[servnum].notify_list.max_alloc = 0;
	server_list[servnum].notify_list.func = (alist_func)my_stricmp;
	server_list[servnum].notify_list.hash = HASH_INSENSITIVE;
	server_list[servnum].notify_list.ison = NULL;

	for (i = 0; i < NOTIFY_MAX(0); i++)
	{
		tmp = (NotifyItem *)new_malloc(sizeof(NotifyItem));
		tmp->nick = m_strdup(NOTIFY_ITEM(0, i)->nick);
		tmp->flag = 0;

		add_to_array ((array *)NOTIFY_LIST(servnum),
			      (array_item *)tmp);
		m_s3cat(&list, space, tmp->nick);
	}

	if (list)
	{
		isonbase(list, ison_notify);
		new_free(&list);
	}
}


char *	get_notify_nicks (int showserver, int showon)
{
	char *list = NULL;
	int i;
	size_t rvclue=0;

	if (showserver < 0 || showserver >= number_of_servers)
		return m_strdup(empty_string);

	for (i = 0; i < NOTIFY_MAX(showserver); i++)
	{
		if (showon == -1 || showon == NOTIFY_ITEM(showserver, i)->flag)
			m_sc3cat(&list, space, NOTIFY_ITEM(showserver, i)->nick, &rvclue);
	}

	return (list ? list : m_strdup(empty_string));
}
