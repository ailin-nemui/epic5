/* $EPIC: ignore.c,v 1.24 2004/07/29 16:59:03 jnelson Exp $ */
/*
 * ignore.c: handles the ingore command for irc 
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright © 1995, 2003 EPIC Software Labs.
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
 * Here's the plan...
 *
 * IGNORE THEORY: There are times when we want to single out a particular
 * person, server, or channel for special handling.  The process of
 * identifying another target on irc for special handling (see below),
 * and the descriptions and dispositions of what you want epic to do when
 * that target sends you a message, is known as "IGNORING" them.  Any
 * particular message may be subject to one or more "IGNORE" rules.
 *
 * Each Ignore rule may either "pass" (do nothing) on a message or terminate
 * the disposition of a message.  There are three dispositions available:
 *
 *   1) Suppressive ignore (IGNORE_SUPPRESS) - A message from this person 
 *      shall be discarded and shall not generate any direct output.
 *   2) Exceptive ignore (IGNORE_DONT) - A message from this person shall be 
 *      considered important enough to be exempt from all other ignore rules.
 *      Ignores of this type are "exceptions" to ignores of other types.
 *   3) Highlight ignore (IGNORE_HIGH) - A message from this person shall not
 *      only be exempt from all other ignore rules, but EPIC's default output
 *      shall be embellished to indicate to the user that a person of import
 *      has sent a message.
 *
 * Each Ignore type has (currently) many "levels" through which all messages
 * are sorted:
 *	MSGS		PRIVMSGs sent to you
 *	PUBLIC		PRIVMSGs sent to a channel you are on
 *	WALLS		PRIVMSGs not covered by MSGS or PUBLIC
 *	WALLOPS		WALLOPs messages sent to anybody.
 *	INVITES		INVITEs sent to you
 *	NOTICES		NOTICEs sent to anybody
 *	CTCPS		CTCP requests or CTCP replies sent to anybody
 *	TOPICS		TOPIC changes by anybody
 *	NICKS		NICK changes by anybody
 *	JOINS		JOINs to any channel by anybody
 *	PARTS		PARTs from any channel by anybody
 *	CRAP		QUITs, MODEs, KICKs and PONGs.
 *
 * In addition, the user can use these shorthand level descriptions:
 *	ALL		All of the above.
 *
 * Further in addition, the user can set ancilliary data which have the
 * following descriptions:
 *	REASON "arg"	Set the ignore's reason to the double-quoted
 *			word argument after REASON
 *	TIMEOUT number	Automatically cancel the ignore after "number"
 *			seconds from now.
 *
 * Each message that can be ignored fits into one of the above categories.
 * For each Ignore item, for each level, the three dispotions are mutually
 * exclusive of each other.  If you add MSGS to your suppressive ignores, 
 * it is implied that MSGS will be removed from Exceptive and Highlight
 * ignores.
 * 
 * It is not necessary for each ignore to have a disposition for each level.
 * If an ignore does not declare a disposition for a level, it "passes" on 
 * that message and further ignore rules will be checked.
 */
#define WANT_LEVEL_NAMES

#include "irc.h"
#include "ignore.h"
#include "ircaux.h"
#include "list.h"
#include "vars.h"
#include "output.h"
#include "parse.h"
#include "timer.h"
#include "functions.h"
#include "window.h"
#include "reg.h"

#define IGNORE_REMOVE 	-1
#define IGNORE_SUPPRESS  0
#define IGNORE_DONT 	 1
#define IGNORE_HIGH 	 2


/*
 * Ignore: the ignore list structure,  consists of the nickname, and the type
 * of ignorance which is to take place 
 */
typedef struct	IgnoreStru
{
	struct	IgnoreStru *next;
	char	*nick;			/* What is being ignored */
	int	refnum;			/* The refnum for internal use */
	Mask	type;			/* Suppressive ignores */
	Mask	dont;			/* Exceptional ignores */
	Mask	high;			/* Highlight ignores */
	int	counter;		/* How many times it has triggered */
	Timeval	creation;		/* When it was created */
	Timeval	last_used;		/* When it was last ``triggered'' */
	Timeval	expiration;		/* When this ignore expires */
	char	*reason;
	int	enabled;
}	Ignore;

/* ignored_nicks: pointer to the head of the ignore list */
static	Ignore *ignored_nicks = NULL;
static	int	global_ignore_refnum = 0;

static void	expire_ignores			(void);
static const char *	get_ignore_types 		(Ignore *item, int);
static int	change_ignore_mask_by_desc (const char *type, Mask *do_mask, Mask *dont_mask, Mask *high_mask, char **reason, Timeval *expire);
static int	ignore_change			(Ignore *, int, void *);
static int	ignore_list			(Ignore *, int, void *);
static int	foreach_ignore			(const char *, int, 
						 int (*)(Ignore *, int, void *),
						 int, void *);
static int	remove_ignore			(const char *);

/*****************************************************************************/
static Ignore *new_ignore (const char *new_nick)
{
	Ignore *item;

	item = (Ignore *) new_malloc(sizeof(Ignore));
	item->nick = malloc_strdup(new_nick);
	upper(item->nick);
	item->reason = NULL;
	item->refnum = ++global_ignore_refnum;
	mask_unsetall(&item->type);
	mask_unsetall(&item->dont);
	mask_unsetall(&item->high);
	item->counter = 0;
	get_time(&item->creation);
	item->last_used.tv_sec = 0;
	item->last_used.tv_usec = 0;
	item->expiration.tv_sec = 0;
	item->expiration.tv_usec = 0;
	item->enabled = 1;
	add_to_list((List **)&ignored_nicks, (List *)item);
	return item;
}

/*
 * get_ignore_by_refnum: When all you have is a refnum, all the world's 
 *			 a linked list...
 *
 * Arguments: 
 *  'refnum' -- The refnum of an ignore allegedly in use
 *
 * Return Value:
 *  If 'refnum' is actually in use, that Ignore item is returned, 
 *  otherwise NULL is returned.
 */
static Ignore *get_ignore_by_refnum (int refnum)
{
	Ignore *item;

	for (item = ignored_nicks; item; item = item->next)
		if (item->refnum == refnum)
			return item;

	return NULL;
}

/****************************************************************************/
/*
 * do_expire_ignores: TIMER callback for when ignores should be reaped
 *
 * Arguments:
 *  'ignored' -- Not used.
 *
 * Return Value:
 *  No return value
 */
int	do_expire_ignores (void *ignored)
{
	expire_ignores();
	return 0;
}

/*
 * expire_ignores: find and destroy any timers which have reached the end
 * 	of their useful lifetime.
 *
 * Arguments:
 *  No arguments
 *
 * Return Value:
 *  No return value
 */
static void	expire_ignores (void)
{
	Ignore *item, *next;
	Timeval	right_now;

	if (!ignored_nicks)
		return;

	get_time(&right_now);
	for (item = ignored_nicks; item; item = next)
	{
		next = item->next;
		if (item->expiration.tv_sec != 0 &&
				time_diff(right_now, item->expiration) < 0)
			remove_ignore(item->nick);
	}
}

/*
 * remove_ignore: Delete one or more Ignore items by description.
 *
 * Arguments:
 *  'nick' - A string containing either the exact value of an Ignore item
 *		(ie, a nick!user@host, server name, or channel name), or 
 *		a wildcard pattern that matches one or more values of Ignore
 *		items.  If it is an exact value, that ignore item is deleted.
 *		If it is a wildcard pattern, then all ignore items that are
 *		matched by the pattern are deleted.
 *
 * Return value:
 *  This function returns the number of ignore items deleted.
 *
 * Notes:
 *  This function may delete more than one item!
 *
 *  This function tells the user what the changes are.  If you do not want
 *  the output to occur, set window_display to 0 before calling.
 */
static int 	remove_ignore (const char *nick)
{
	Ignore	*item;
	char	new_nick[IRCD_BUFFER_SIZE + 1];
	int	count = 0;
	char 	*mnick, *user, *host;

	if (is_number(nick))
	{
	    Ignore *last;
	    int	refnum = my_atol(nick);

	    for (last = NULL, item = ignored_nicks; item; item = item->next)
	    {
		if (item->refnum == refnum)
		{
		    if (last)
			last->next = item->next;
		    else
			ignored_nicks = item->next;

		    say("%s removed from ignorance list (ignore refnum %d)", 
				item->nick, item->refnum);
		    new_free(&(item->nick));
		    new_free(&(item->reason));
		    new_free((char **)&item);
		    return 1;
		}
		last = item;
	    }

	    say("Ignore refnum [%d] is not in use!", refnum);
	    return 0;
	}

	if (figure_out_address(nick, &mnick, &user, &host))
		strlcpy(new_nick, nick, IRCD_BUFFER_SIZE);
	else
		snprintf(new_nick, IRCD_BUFFER_SIZE, "%s!%s@%s",
				*mnick ? mnick : star,
				*user ? user : star,
				*host ? host : star);

	/*
	 * Look for an exact match first.
	 */
	if ((item = (Ignore *) list_lookup((List **)&ignored_nicks, new_nick, 
					!USE_WILDCARDS, REMOVE_FROM_LIST)))
	{
		say("%s removed from ignorance list (ignore refnum %d)", 
				item->nick, item->refnum);
		new_free(&(item->nick));
		new_free(&(item->reason));
		new_free((char **)&item);
		count++;
	}

	/*
	 * Otherwise clear everything that matches.
	 */
	else while ((item = (Ignore *)list_lookup((List **)&ignored_nicks, 
						nick, USE_WILDCARDS, 
						REMOVE_FROM_LIST)))
	{
		say("%s removed from ignorance list (ignore refnum %d)", 
				item->nick, item->refnum);
		new_free(&(item->nick));
		new_free(&(item->reason));
		new_free((char **)&item);
		count++;
	} 

	if (!count)
		say("%s is not in the ignorance list!", new_nick);

	return count;
}

/***************************************************************************/
/*
 * get_ignore_types:  Summarize the effects of an Ignore rule.
 *
 * Arguments:
 *  'item' - The Ignore item whose dispositions shall be summarized
 *
 * Return value:
 *  This function returns a static, temporary string that contains a summary
 *  of the effects of the Ignore rule 'item'.  This string will be destroyed
 *  the next time someone calls this function so if you want to keep it, you
 *  must make a copy of it.  You must not try to write to the string.
 */
#define HANDLE_TYPE(x,y)						\
	if (mask_isset(&item->dont, LEVEL_ ## x))			\
	{								\
	    if ((y) == 1)						\
		strlcat_c(buffer, " DONT-" #x, sizeof buffer, &clue);	\
	    else if ((y) == 2)						\
		strlcat_c(buffer, " ^" #x, sizeof buffer, &clue);	\
	}								\
	else if (mask_isset(&item->type, LEVEL_ ## x))			\
	{								\
	    if ((y) == 1)						\
		strlcat_c(buffer, " " #x, sizeof buffer, &clue);	\
	    else if ((y) == 2)						\
		strlcat_c(buffer, " /" #x, sizeof buffer, &clue);	\
	}								\
	else if (mask_isset(&item->high, LEVEL_ ## x))			\
	{								\
	    if ((y) == 1)						\
		strlopencat_c(buffer, sizeof buffer, &clue, space, 	\
				high, #x, high, NULL);			\
	    else if ((y) == 2)						\
		strlcat_c(buffer, " +" #x, sizeof buffer, &clue);	\
	}

static const char *	get_ignore_types (Ignore *item, int output_type)
{
static	char 	buffer[BIG_BUFFER_SIZE + 1];
	char	*high = highlight_char;
	size_t	clue;
	char	*retval;

	clue = 0;
	*buffer = 0;
	HANDLE_TYPE(ALL, output_type)
	else
	{
		HANDLE_TYPE(PUBLIC, output_type)
		HANDLE_TYPE(MSG, output_type)
		HANDLE_TYPE(NOTICE, output_type)
		HANDLE_TYPE(WALL, output_type)
		HANDLE_TYPE(WALLOP, output_type)
		HANDLE_TYPE(OPNOTE, output_type)
		HANDLE_TYPE(SNOTE, output_type)
		HANDLE_TYPE(ACTION, output_type)
		HANDLE_TYPE(DCC, output_type)
		HANDLE_TYPE(CTCP, output_type)
		HANDLE_TYPE(INVITE, output_type)
		HANDLE_TYPE(JOIN, output_type)
		HANDLE_TYPE(NICK, output_type)
		HANDLE_TYPE(TOPIC, output_type)
		HANDLE_TYPE(PART, output_type)
		HANDLE_TYPE(QUIT, output_type)
		HANDLE_TYPE(KICK, output_type)
		HANDLE_TYPE(MODE, output_type)
		HANDLE_TYPE(CRAP, output_type)
	}

	retval = buffer;
	while (isspace(*retval))
		retval++;
	return retval;		/* Eh! */
}

/*
 * change_ignore_mask_by_desc:  Change ignore mask and ancilliary data in
 *	the manner provided by a user description ignore-dispensation list.
 *
 * Arguments:
 *  'type' 	- A comma-and-space separated list of ignore levels along with
 *		  dispensations for the list.  For each level and dispensation
 *		  included, the following variables will be changed:
 *  'do_mask' 	- Suppressive ignores
 *  'dont_mask' - Exceptional ignores
 *  'high_mask' - Highlight ignores
 *  'reason' 	- The reason the ignore was created.  May be NULL.
 *		  Expects an argument.  If argument not provided or argument
 *		  is a hyphen ("-"), the reason is unset.
 *  'expire' 	- The time the ignore shall expire.  May be NULL.
 *		  Expects an argument.  If argument not provided or argument
 *		  is a hyphen ("-"), the timeout is unset.
 *
 * Return value:
 *  The value 0 is returned.
 *
 * Notes:
 *  The following dispositions are supported:
 *	-<TYPE>		Remove <TYPE> from all disposition types
 *	!<TYPE>		Change <TYPE> to an exceptional ignore
 *	^<TYPE>		Same as !<TYPE>
 *	+<TYPE>		Change <TYPE> to a highlight ignore
 *	/<TYPE>		Change <TYPE> to a suppressive ignore
 *	<TYPE>		Same as /<TYPE>
 *
 *  The supported <TYPE>s are defined at the top of this file in the 
 *	"IGNORE THEORY" documentation.
 */
static int	change_ignore_mask_by_desc (const char *type, Mask *do_mask, Mask *dont_mask, Mask *high_mask, char **reason, Timeval *expire)
{
	char	*l1, *l2;
	int	len;
	Mask	*mask = NULL, *del1, *del2, *del3;
	char *	copy;
	int	bit = LEVEL_NONE;
	int	i;

	copy = LOCAL_COPY(type);
        while ((l1 = new_next_arg(copy, &copy)))
	{
	    while (*l1 && (l2 = next_in_comma_list(l1, &l1)))
	    {
		switch (*l2)
		{
			case '-':
				l2++;
				mask = NULL;
				del1 = do_mask;
				del2 = dont_mask;
				del3 = high_mask;
				break;
			case '!':
			case '^':
				l2++;
				mask = dont_mask;
				del1 = do_mask;
				del2 = high_mask;
				del3 = NULL;
				break;
			case '+':
				l2++;
				mask = high_mask;
				del1 = do_mask;
				del2 = dont_mask;
				del3 = NULL;
				break;
			case '/':
			default:
				mask = do_mask;
				del1 = dont_mask;
				del2 = high_mask;
				del3 = NULL;
				break;
		}

		if (!(len = strlen(l2)))
			continue;

		if (!my_strnicmp(l2, "REASON", len))
		{
			char *the_reason;

			the_reason = new_next_arg(copy, &copy);
			if (reason)
			{
			    if (!mask || !the_reason || !*the_reason ||
						!strcmp(the_reason, "-"))
				new_free(reason);
			    else
				malloc_strcpy(reason, the_reason);
			}

			continue;
		}
		else if (!my_strnicmp(l2, "TIMEOUT", len))
		{
			char *the_timeout;

			the_timeout = new_next_arg(copy, &copy);
			if (expire)
			{
			    if (!mask || !the_timeout || !*the_timeout ||
					!strcmp(the_timeout, "-"))
			    {
				expire->tv_sec = 0;
				expire->tv_usec = 0;
			    }
			    else if (is_real_number(the_timeout))
			    {
				double seconds;
				Timeval right_now, interval;

				seconds = atof(the_timeout);
				interval = double_to_timeval(seconds);
				get_time(&right_now);
				*expire = time_add(right_now, interval);

				add_timer(0, empty_string, seconds, 1, 
					  do_expire_ignores, NULL, NULL, -1);
			    }
			}

			continue;
		}

		/* It's a level of some sort */
		if (!my_strnicmp(l2, "NONE", len))
		{
			mask = NULL;
			del1 = do_mask;
			del2 = dont_mask;
			del3 = high_mask;
			bit = LEVEL_ALL;
		}
		else if (!my_strnicmp(l2, "ALL", len))
			bit = LEVEL_ALL;
		else
		{
		    for (i = 1; i < NUMBER_OF_LEVELS; i++)
		    {
			if (!my_strnicmp(l2, level_types[i], len))
			{
			    bit = i;
			    break;
			}
		    }

		    if (i == NUMBER_OF_LEVELS)
		    {
			say("You must specify one of the following:");
			say("\tALL CRAP PUBLIC MSG NOTICE WALL WALLOP "
				"OPNOTE SNOTE ACTION DCC CTCP INVITE JOIN "
				"NICK TOPIC PART QUIT KICK MODE NONE "
				"REASON \"<reason>\" TIMEOUT <seconds>");
			continue;
		    }
		}

		if (mask)
			mask_set(mask, bit);
		if (del1)
			mask_unset(del1, bit);
		if (del2)
			mask_unset(del2, bit);
		if (del3)
			mask_unset(del3, bit);
	    }
	}

	return 0;
}

/*****************************************************************************/
/*
 * ignore_change: Change the types of things being ignored for an
 *			Ignore item.
 *
 * Arguments:
 *  'item'     - A particular Ignore item to be operated upon.
 *  'type'     - Ignored -- should be 1
 *  'data'     - A pointer to a string containing ignore type changes.
 *
 * Return value:
 *  This function returns 0.
 *
 * Notes:
 *  This function tells the user what the changes are.  If you do not want
 *  the output to occur, set window_display to 0 before calling.
 */
static int	ignore_change (Ignore *item, int type, void *data)
{
	char *changes;

	changes = (char *)data;
	change_ignore_mask_by_desc(changes, &item->type, &item->dont, 
					&item->high, &item->reason, 
					&item->expiration);

	/*
	 * Tell the user the new state of the ignore.
	 * Garbage collect this ignore if it is clear.
	 * remove_ignore() does the output for us here.
	 */
	if (mask_isnone(&item->type) && mask_isnone(&item->high) &&
	    mask_isnone(&item->dont))
	{
		remove_ignore(item->nick);
		return 0;
	}

	if (item->reason)
		say("Now ignoring %s from %s (refnum %d) because %s", 
			get_ignore_types(item, 1), item->nick, item->refnum, 
			item->reason);
	else
		say("Now ignoring %s from %s (refnum %d)",
			get_ignore_types(item, 1), item->nick, item->refnum);

	return 0;
}

/*
 * ignore_list: Tell the user about a particular ignore item.
 *
 * Arguments:
 *  'item'     - A particular Ignore item to be described.
 *  'type'     - This value is ignored.  Pass the value 1.
 *  'data'     - This value is ignored.  Pass a pointer to an integer.
 *
 * Return value:
 *  This function returns 0.
 */
static int	ignore_list (Ignore *item, int type, void *data)
{
	int	expiring = 0;
	double	time_to_expire = 0;
	Timeval	right_now;

	if (item->expiration.tv_sec != 0)
	{
		get_time(&right_now);
		time_to_expire = time_diff(right_now, item->expiration);
		expiring = 1;
	}

	if (item->reason)
	{
	    if (!expiring)
	    {
		say("[%d]\t%s:\t%s (%s)", item->refnum, item->nick, 
				get_ignore_types(item, 1), item->reason);
	    }
	    else
	    {
		say("[%d]\t%s:\t%s (%s) (%f seconds left)", 
				item->refnum, item->nick, 
				get_ignore_types(item, 1), item->reason,
				time_to_expire);
	    }
	}
	else
	{
	    if (!expiring)
	    {
		say("[%d]\t%s:\t%s", item->refnum, item->nick, 
				get_ignore_types(item, 1));
	    }
	    else
	    {
		say("[%d]\t%s:\t%s (%f seconds left)", item->refnum, 
				item->nick, get_ignore_types(item, 1),
				time_to_expire);
	    }
	}
	return 0;
}


/***************************************************************************/
/*
 * foreach_ignore: call a function to manipulate one or more ignore items.
 *
 * Arguments:
 *  'nicklist' - A comma-and-space separated list of nicknames, channels,
 *		 and targets, each of which will be operated on.
 *  'create'   - If 1, items in 'nicklist' which do not exist will be created
 *		 on demand.  Otherwise they will be skipped with an error.
 *  'callback' - For each (Ignore) item represented by targets in 'nicklist',
 *		 this function will be called passing in the (Ignore) item,
 *		 and the two following data items:
 *  'data1'    - An integer value to be passed to the callback function.
 *		 The usual mean is 1 to set a value and 0 to unset a value
 *  'data2'    - A pointer to payload data which shall be passed to the
 *		 callback value.  This is usually a pointer to an integer
 *		 or a character string.
 *
 * Return value:
 *  This function returns 0.
 *
 * Notes:
 *  This function allows you to easily perform the same operation on one or
 *    more ignore items as requested by the user.
 *  If a target in "nicklist" is not currently being ignored, and 'create' is
 *    true, a new entry for that target will be created, and the new item will
 *    be passed to the callback.  This is a preferred way to create new ignore
 *    items.
 */
static int	foreach_ignore (const char *nicklist, int create, int (*callback) (Ignore *, int, void *), int data1, void *data2)
{
	char *copy, *arg, *nick;
	Ignore *item;
	char *	mnick;
	char *	user;
	char *	host;
	char	new_nick[IRCD_BUFFER_SIZE + 1];

	if (nicklist == NULL)
	{
		for (item = ignored_nicks; item; item = item->next)
			callback(item, data1, data2);
		return 0;
	}

	/*
	 * Walk over 'nicklist', which separates each target with spaces
	 * or with commas, ie:
	 *	/ignore "hop hop2" ALL
	 *	/ignore hop,hop2 ALL
	 */
	copy = LOCAL_COPY(nicklist);
        while ((arg = new_next_arg(copy, &copy)))
	{
	    while (*arg && (nick = next_in_comma_list(arg, &arg)))
	    {
		if (is_number(nick))
		{
		    int	refnum = my_atol(nick);

		    for (item = ignored_nicks; item; item = item->next)
			if (item->refnum == refnum)
				break;

		    if (!item)
		    {
			say("Ignore refnum [%d] is not in use!", refnum);
			continue;
		    }
		}
		else
		{
		    /*
		     * If possible, fill out the address.  If it cannot
		     * be figured out, just use what we were given.
		     */
		    if (figure_out_address(nick, &mnick, &user, &host))
			strlcpy(new_nick, nick, sizeof new_nick);
		    else
			snprintf(new_nick, sizeof new_nick, "%s!%s@%s",
					*mnick ? mnick : star,
					*user ? user : star,
					*host ? host : star);

		    /*
		     * Create a new ignore item if this one does not exist.
		     */
		    if ((item = (Ignore *)find_in_list((List **)&ignored_nicks, 
							new_nick, 0)) == NULL)
		    {
			if (create == 0)
			{
				say("%s is not being ignored!", new_nick);
				continue;
			}

			item = new_ignore(new_nick);
		    }
		}

		callback(item, data1, data2);
	    }
	}

	return 0;
}


/**************************************************************************/
/*
 * ignore: The /IGNORE command
 *
 * Arguments:
 *  'args' - The command line arguments to /IGNORE.
 *
 * Return value:
 *  None
 *
 * Notes:
 *  /IGNORE				
 *	List all ignore items
 *  /IGNORE <target-or-refnum-list>
 *	List one or more particular ignore item by giving the target or
 *	refnum; separate multiple items by commas or spaces.  Surround 
 *	the list with double quotes if it contains spaces.
 *  /IGNORE <target-or-refnum-list> <level-dispositions>
 *	Change one or more particular ignore items, assigning the given
 *	levels to the given dispositions.  The target list may be separated
 *	with spaces or commas.  Surround the target list with double quotes
 *	if it contains spaces.  The level dispositions may be separated with
 *	spaces or commas.  It is not necessary to surround the level
 *	dispositions with double quotes.
 *
 * Where:
 *  <target-or-refnum-list> := ["] <target>|<refnum> 
 *				[<delim> <target-or-refnum-list>]* ["]
 *  <target> := [<nick>][!<user>@<host>]
 *  <refnum> := <number>*
 *  <delim>  := <space>|<comma>
 *  <level-dispositions> := <disposition> <level>
 *  <disposition> :=   [!|^] 			Set exclusionary ignore 
 *                   | -			Remove level from all types
 *		     | +			Set highlight ignore
 *		     | [/|<empty string>]	Set suppressive ignore
 *  <level> := MSGS | PUBLIC | WALLS | WALLOPS | INVITES |
 *	       NOTICES | CTCPS | TOPICS | NICKS | 
 *	       JOINS | PARTS | CRAP | NONE | ALL | 
 *	       REASON "<reason>" | TIMEOUT "<number>"
 *
 * Examples:
 *	/IGNORE #EPIC +ALL		(Highlight all messages to epic)
 *	/IGNORE #EPIC -ALL		(Delete ignore item for epic)
 *	/IGNORE #EPIC NONE		(Same thing as -ALL)
 *	/IGNORE 			(List all ignores)
 *	/IGNORE 1 NONE			(Remove ignore refnum 1)
 *	/IGNORE 2,3 NONE		(Remove ignore refnums 2 and 3)
 *	/IGNORE "2 3" NONE		(Same thing)
 */
BUILT_IN_COMMAND(ignore)
{
	char	*nick;

	nick = new_next_arg(args, &args);
	if (nick && !is_string_empty(args))
		foreach_ignore(nick, 1, ignore_change, 1, args);
	else
	{
		say("Ignorance List:");
		foreach_ignore(nick, 0, ignore_list, 0, NULL);
	}
}

/**************************** INTERNAL API ********************************/
/*
 * get_ignores_by_pattern: Forward or reverse matching of ignore patterns
 *			   with a list of patterns.  The built in $igmask()
 *			   and $rigmask() functions.
 *
 * Arguments:
 * 'patterns' - A space separated list of patterns to use in forward
 *	        or reverse matching the Ignore items.
 * 'covered'  - If 0, do forward matching ($igmask()), 
 *		if 1, do reverse matching ($rigmask()).
 *
 * Return value:
 *  The return value is a MALLOCED word list containing all of the ignore
 *	wildcard masks covering or covered by the 'patterns'
 *
 * Notes:
 *  "Forward" matching means treating each ignore pattern as a literal
 *	string and the user's input as a wildcard pattern.  All ignore 
 *	patterns which are matched by the input are returned.
 *  "Reverse" matching means treating each ignore pattern as wildcard
 *	patterns and the user's input as a literal string.  All ignore
 *	patterns which match the input are returned.
 *  Forward matching asks the question -- "Which ignore patterns contain
 *	this text in them?"
 *  Reverse matching asks the question -- "If this was the nick!user@host,
 *	which ignore patterns apply to that target?"
 */
char 	*get_ignores_by_pattern (char *patterns, int covered)
{
	Ignore	*tmp;
	char 	*pattern;
	char 	*retval = NULL;
	size_t	clue = 0;

	while ((pattern = new_next_arg(patterns, &patterns)))
	{
		for (tmp = ignored_nicks; tmp; tmp = tmp->next)
		{
			if (covered ? wild_match(tmp->nick, pattern)
				    : wild_match(pattern, tmp->nick))
				malloc_strcat_word_c(&retval, space, tmp->nick, &clue);
		}
	}

	return retval ? retval : malloc_strdup(empty_string);
}


/*
 * get_ignore_types_by_pattern:  The $igtype() built in function
 *
 * Arguments:
 *  'pattern' - A single ignore pattern or refnum.
 *
 * Return value:
 *  The targets being ignored by the ignore item, suitable for displaying
 *  to the user.
 */
const char	*get_ignore_types_by_pattern (char *pattern)
{
	Ignore	*tmp;
	int	number = -1;

	if (is_number(pattern))
		if ((number = my_atol(pattern)) < 0)
			number = -1;

	for (tmp = ignored_nicks; tmp; tmp = tmp->next)
	{
		if (!my_stricmp(tmp->nick, pattern))
			return get_ignore_types(tmp, 1);
		if (number > -1 && tmp->refnum == number)
			return get_ignore_types(tmp, 1);
	}

	return empty_string;
}

/*
 * get_ignore_patterns_by_type:  The $rigtype() built in function
 *
 * Arguments:
 *  'ctype' - A comma-and-space separated list of ignore level descriptions
 *
 * Return value:
 *  A MALLOCED string containing a word list of all of the targets 
 *	(nick!user@host or #channels) ignoring at least the levels 
 *	described by the argument.
 */
char	*get_ignore_patterns_by_type (char *ctype)
{
	Ignore	*tmp;
	Mask	do_mask, dont_mask, high_mask;
	char	*result = NULL;
	size_t	clue = 0;

	mask_unsetall(&do_mask);
	mask_unsetall(&dont_mask);
	mask_unsetall(&high_mask);

	/*
	 * Convert the user's input into something we can use.
	 * If the user doesnt specify anything useful, then we
	 * just punt right here.
	 */
	change_ignore_mask_by_desc(ctype, &do_mask, &dont_mask, &high_mask, NULL, NULL);
	if (mask_isnone(&do_mask) && mask_isnone(&do_mask) && mask_isnone(&high_mask))
		return malloc_strdup(empty_string);

	for (tmp = ignored_nicks; tmp; tmp = tmp->next)
	{
	    int	i;

	    /*
	     * change_ignore_mask_by_desc is supposed to ensure that
	     * each bit is set only once in 'do_mask', 'dont_mask', 
	     * and 'high_mask', and we already know this is the case
	     * for the Ignores.  So we check each of the three ignore
	     * types, and if this ignore has all of the levels set in
	     * all of the right places, it's ok.  It could have more 
	     * levels than what the user asked for, but it can't have 
	     * levels with different dispositions.
	     */
	    for (i = 1; i < NUMBER_OF_LEVELS; i++)
	    {
		if (mask_isset(&dont_mask, i) && !mask_isset(&tmp->dont, i))
			goto bail;
		if (mask_isset(&do_mask, i) && !mask_isset(&tmp->type, i))
			goto bail;
		if (mask_isset(&high_mask, i) && !mask_isset(&tmp->high, i))
			goto bail;
	    }

	    /* Add it to the fray */
	    malloc_strcat_word_c(&result, space, tmp->nick, &clue);
bail:
	    continue;
	}

	return result;
}

/*
 * Here's the plan:
 *
 * $ignorectl(REFNUMS)
 * $ignorectl(REFNUM <ignore-pattern>)
 * $ignorectl(ADD <ignore-pattern> [level-desc])
 * $ignorectl(CHANGE <refnum> [level-desc])
 * $ignorectl(DELETE <refnum>)
 * $ignorectl(PATTERN <pattern>)
 * $ignorectl(RPATTERN <pattern>)
 * $ignorectl(WITH_TYPES <pattern>)
 * $ignorectl(GET <refnum> [LIST])
 * $ignorectl(SET <refnum> [ITEM] [VALUE])
 *
 * [LIST] and [ITEM] are one of the following
 *	NICK		The pattern being ignored.  Changing this is dangerous.
 *	LEVELS		A parsable summary of what is being ignored
 *	TYPE		An integer representing the suppressive ignores
 *	DONT		An integer representing the exceptional ignores
 *	HIGH		An integer representing the highlight ignores
 *	EXPIRATION	The time the ignore will expire
 *	REASON		The reason why we're ignoring this pattern.
 */ 
char *	ignorectl (char *input)
{
	char *	refstr;
	char *	listc;
	Ignore *i;
	int	len;
	int	owd;

	GET_STR_ARG(listc, input);
	len = strlen(listc);
	if (!my_strnicmp(listc, "REFNUM", len)) {
		if ((i = (Ignore *)find_in_list((List **)&ignored_nicks,
					input, 0)) == NULL)
			RETURN_EMPTY;

		RETURN_INT(i->refnum);
	} else if (!my_strnicmp(listc, "REFNUMS", len)) {
		char *	retval = NULL;
		size_t	clue = 0;

		for (i = ignored_nicks; i; i = i->next)
			malloc_strcat_word_c(&retval, space, 
						ltoa(i->refnum), &clue);
		RETURN_MSTR(retval);
	} else if (!my_strnicmp(listc, "ADD", len)) {
		char *	pattern;

		GET_STR_ARG(pattern, input);

		owd = window_display;
		window_display = 0;
		i = new_ignore(pattern);
		ignore_change(i, 1, input);
		window_display = owd;
		RETURN_INT(i->refnum);
	} else if (!my_strnicmp(listc, "CHANGE", len)) {
		int	refnum;

		GET_STR_ARG(refstr, input);
		if (!is_number(refstr))
			RETURN_EMPTY;
		refnum = my_atol(refstr);
		if (!(i = get_ignore_by_refnum(refnum)))
			RETURN_EMPTY;

		owd = window_display;
		window_display = 0;
		ignore_change(i, 1, input);
		window_display = owd;
		RETURN_INT(i->refnum);
	} else if (!my_strnicmp(listc, "DELETE", len)) {
		RETURN_INT(remove_ignore(input));
	} else if (!my_strnicmp(listc, "PATTERN", len)) {
		RETURN_MSTR(get_ignores_by_pattern(input, 0));
	} else if (!my_strnicmp(listc, "RPATTERN", len)) {
		RETURN_MSTR(get_ignores_by_pattern(input, 1));
	} else if (!my_strnicmp(listc, "WITH_TYPES", len)) {
		RETURN_MSTR(get_ignore_patterns_by_type(input));
	} else if (!my_strnicmp(listc, "GET", len)) {
		int	refnum;

		GET_STR_ARG(refstr, input);
		if (!is_number(refstr))
			RETURN_EMPTY;
		refnum = my_atol(refstr);
		if (!(i = get_ignore_by_refnum(refnum)))
			RETURN_EMPTY;

		GET_STR_ARG(listc, input);
		len = strlen(listc);
		if (!my_strnicmp(listc, "NICK", len)) {
			RETURN_STR(i->nick);
		} else if (!my_strnicmp(listc, "LEVELS", len)) {
			RETURN_STR(get_ignore_types(i, 2));
		} else if (!my_strnicmp(listc, "SUPPRESS", len)) {
			RETURN_INT(i->type.__bits[0]);
		} else if (!my_strnicmp(listc, "EXCEPT", len)) {
			RETURN_INT(i->dont.__bits[0]);
		} else if (!my_strnicmp(listc, "HIGHLIGHT", len)) {
			RETURN_INT(i->high.__bits[0]);
		} else if (!my_strnicmp(listc, "EXPIRATION", len)) {
			char *ptr = NULL;
			return malloc_sprintf(&ptr, "%ld %ld", 
				(long) i->expiration.tv_sec,
				(long) i->expiration.tv_usec);
		} else if (!my_strnicmp(listc, "REASON", len)) {
			RETURN_STR(i->reason);
		} else if (!my_strnicmp(listc, "COUNTER", len)) {
			RETURN_INT(i->counter);
		} else if (!my_strnicmp(listc, "CREATION", len)) {
			char *ptr = NULL;
			return malloc_sprintf(&ptr, "%ld %ld", 
				(long) i->creation.tv_sec,
				(long) i->creation.tv_usec);
		} else if (!my_strnicmp(listc, "LAST_USED", len)) {
			char *ptr = NULL;
			return malloc_sprintf(&ptr, "%ld %ld", 
				(long) i->last_used.tv_sec,
				(long) i->last_used.tv_usec);
		} else if (!my_strnicmp(listc, "ENABLED", len)) {
			RETURN_INT(i->enabled);
		}
	} else if (!my_strnicmp(listc, "SET", len)) {
		int	refnum;

		GET_STR_ARG(refstr, input);
		if (!is_number(refstr))
			RETURN_EMPTY;
		refnum = my_atol(refstr);
		if (!(i = get_ignore_by_refnum(refnum)))
			RETURN_EMPTY;

		GET_STR_ARG(listc, input);
		len = strlen(listc);
		if (!my_strnicmp(listc, "NICK", len)) {
			malloc_strcpy(&i->nick, input);
			RETURN_INT(i->refnum);
		} else if (!my_strnicmp(listc, "LEVELS", len)) {
			mask_unsetall(&i->type);
			mask_unsetall(&i->dont);
			mask_unsetall(&i->high);
			ignore_change(i, 1, input);
			RETURN_INT(i->refnum);
		} else if (!my_strnicmp(listc, "SUPPRESS", len)) {
			GET_INT_ARG(i->type.__bits[0], input);
			RETURN_INT(i->refnum);
		} else if (!my_strnicmp(listc, "EXCEPT", len)) {
			GET_INT_ARG(i->dont.__bits[0], input);
			RETURN_INT(i->refnum);
		} else if (!my_strnicmp(listc, "HIGHLIGHT", len)) {
			GET_INT_ARG(i->high.__bits[0], input);
			RETURN_INT(i->refnum);
		} else if (!my_strnicmp(listc, "EXPIRATION", len)) {
			Timeval to;

			GET_INT_ARG(to.tv_sec, input);
			GET_INT_ARG(to.tv_usec, input);
			i->expiration = to;
			RETURN_INT(i->refnum);
		} else if (!my_strnicmp(listc, "REASON", len)) {
			if (is_string_empty(input))
				new_free(&i->reason);
			else
				malloc_strcpy(&i->reason, input);
			RETURN_INT(i->refnum);
		} else if (!my_strnicmp(listc, "CREATION", len)) {
			Timeval to;

			GET_INT_ARG(to.tv_sec, input);
			GET_INT_ARG(to.tv_usec, input);
			i->creation = to;
			RETURN_INT(i->refnum);
		} else if (!my_strnicmp(listc, "LAST_USED", len)) {
			Timeval to;

			GET_INT_ARG(to.tv_sec, input);
			GET_INT_ARG(to.tv_usec, input);
			i->last_used = to;
			RETURN_INT(i->refnum);
		} else if (!my_strnicmp(listc, "COUNTER", len)) {
			GET_INT_ARG(i->counter, input);
			RETURN_INT(i->refnum);
		} else if (!my_strnicmp(listc, "ENABLED", len)) {
			GET_INT_ARG(i->enabled, input);
			RETURN_INT(i->refnum);
		}
	}

	/* We get here if something is not implemented. */
	RETURN_EMPTY;
}


/***************************** BACK END *************************************/
/* 
 * check_ignore -- replaces the old double_ignore
 *   Why did i change the name?
 *      * double_ignore isnt really applicable any more becuase it doesnt
 *        do two ignore lookups, it only does one.
 *      * This function doesnt look anything like the old double_ignore
 *      * This function works for the new *!*@* patterns stored by
 *        ignore instead of the old nick and userhost patterns.
 * (jfn may 1995)
 */
int	check_ignore (const char *nick, const char *uh, int mask)
{
	return check_ignore_channel(nick, uh, NULL, mask);
}

/*
 * "check_ignore_channel" is kind of a bad name for this function, but
 * i was not inspired with a better name.  This function is simply the
 * old 'check_ignore', only it has an additional check for a channel target.
 */
int	check_ignore_channel (const char *nick, const char *uh, const char *channel, int level)
{
	char 	nuh[IRCD_BUFFER_SIZE];
	Ignore	*tmp;
	int	count = 0;
	int	bestimatch = 0;
	Ignore	*i_match = NULL;
	int	bestcmatch = 0;
	Ignore	*c_match = NULL;

	if (!ignored_nicks)
		return NOT_IGNORED;

	snprintf(nuh, IRCD_BUFFER_SIZE - 1, "%s!%s", nick ? nick : star,
						uh ? uh : star);

	for (tmp = ignored_nicks; tmp; tmp = tmp->next)
	{
		if (!tmp->enabled)
			continue;

		/*
		 * Always check for exact matches first...
		 */
		if (!strcmp(tmp->nick, nuh))
		{
			i_match = tmp;
			break;
		}

		/*
		 * Then check for wildcard matches...
		 */
		count = wild_match(tmp->nick, nuh);
		if (count > bestimatch)
		{
			bestimatch = count;
			i_match = tmp;
		}

		/*
		 * Then check for channels...
		 */
		if (channel)
		{
			count = wild_match(tmp->nick, channel);
			if (count > bestcmatch)
			{
				bestcmatch = count;
				c_match = tmp;
			}
		}
	}

	/*
	 * We've found something... Always prefer a nickuserhost match
	 * over a channel match, and always prefer an exact match over
	 * a wildcard match...
	 */
	if (i_match)
	{
		tmp = i_match;
		if (mask_isset(&tmp->dont, level))
		{
			tmp->counter++;
			get_time(&tmp->last_used);
			return NOT_IGNORED;
		}
		if (mask_isset(&tmp->type, level))
		{
			tmp->counter++;
			get_time(&tmp->last_used);
			return IGNORED;
		}
		if (mask_isset(&tmp->high, level))
		{
			tmp->counter++;
			get_time(&tmp->last_used);
			return HIGHLIGHTED;
		}
	}

	/*
	 * If the nickuserhost match did not say anything about the level
	 * that we are interested in, then try a channel ignore next.
	 */
	else if (c_match)
	{
		tmp = c_match;
		if (mask_isset(&tmp->dont, level))
		{
			tmp->counter++;
			get_time(&tmp->last_used);
			return NOT_IGNORED;
		}
		if (mask_isset(&tmp->type, level))
		{
			tmp->counter++;
			get_time(&tmp->last_used);
			return IGNORED;
		}
		if (mask_isset(&tmp->high, level))
		{
			tmp->counter++;
			get_time(&tmp->last_used);
			return HIGHLIGHTED;
		}
	}

	/*
	 * Otherwise i guess we dont ignore them.
	 */
	return NOT_IGNORED;
}

