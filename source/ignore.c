/*
 * ignore.c: handles the ingore command for irc 
 *
 * Written By Michael Sandrof
 *
 * Copyright(c) 1990 
 *
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 */

#include "irc.h"
#include "ignore.h"
#include "ircaux.h"
#include "list.h"
#include "vars.h"
#include "output.h"
#include "parse.h"

#define NUMBER_OF_IGNORE_LEVELS 9

#define IGNORE_REMOVE 1
#define IGNORE_DONT 2
#define IGNORE_HIGH -1

	char *	highlight_char = NULL;

static	int	remove_ignore (char *);

/*
 * Ignore: the ignore list structure,  consists of the nickname, and the type
 * of ignorance which is to take place 
 */
typedef struct	IgnoreStru
{
	struct	IgnoreStru *next;
	char	*nick;
	int	type;
	int	dont;
	int	high;
}	Ignore;

/* ignored_nicks: pointer to the head of the ignore list */
static	Ignore *ignored_nicks = NULL;

#define DOT (*host ? dot : empty_string)

/*
 * ignore_nickname: adds nick to the ignore list, using type as the type of
 * ignorance to take place.
 */
static void 	ignore_nickname (const char *nicklist, int type, int flag)
{
	Ignore	*new_i;
	char *	nick;
	char *	msg;
	char *	ptr;
	char	new_nick[IRCD_BUFFER_SIZE + 1];
	char	buffer[BIG_BUFFER_SIZE+1];
	char *	p;
	char *	mnick;
	char *	user;
	char *	host;

	for (nick = LOCAL_COPY(nicklist); nick; nick = ptr)
	{
		if ((ptr = strchr(nick, ',')))
			*ptr++ = 0;

		if (!*nick)
			continue;

		if (figure_out_address(nick, &mnick, &user, &host))
			strlcpy(new_nick, nick, IRCD_BUFFER_SIZE);
		else
			snprintf(new_nick, IRCD_BUFFER_SIZE, "%s!%s@%s",
				mnick, user, host);

		if (!(new_i = (Ignore *) list_lookup((List **)&ignored_nicks, 
							new_nick, 
							!USE_WILDCARDS, 
							!REMOVE_FROM_LIST)))
		{
			if (flag == IGNORE_REMOVE)
			{
				say("%s is not on the ignorance list", nick);
				continue;
			}
			else
			{
				if ((new_i = (Ignore *) remove_from_list((List **)&ignored_nicks, new_nick)))
				{
					new_free(&(new_i->nick));
					new_free((char **)&new_i);
				}
				new_i = (Ignore *) new_malloc(sizeof(Ignore));
				new_i->type = 0;
				new_i->dont = 0;
				new_i->high = 0;
				new_i->nick = m_strdup(new_nick);
				upper(new_i->nick);
				add_to_list((List **)&ignored_nicks, 
					    (List *)new_i);
			}
		}

		switch (flag)
		{
			case IGNORE_REMOVE:
				new_i->type &= (~type);
				new_i->high &= (~type);
				new_i->dont &= (~type);
				msg = "Not ignoring";
				break;
			case IGNORE_DONT:
				new_i->dont |= type;
				new_i->type &= (~type);
				new_i->high &= (~type);
				msg = "Never ignoring";
				break;
			case IGNORE_HIGH:
				new_i->high |= type;
				new_i->type &= (~type);
				new_i->dont &= (~type);
				msg = "Highlighting";
				break;
			default:
				new_i->type |= type;
				new_i->high &= (~type);
				new_i->dont &= (~type);
				msg = "Ignoring";
				break;
		}

		if (type == IGNORE_ALL)
		{
			switch (flag)
			{
			    case IGNORE_REMOVE:
				remove_ignore(new_i->nick);
				break;
			    case IGNORE_HIGH:
				say("Highlighting ALL messages from %s", 
						new_i->nick);
				break;
			    case IGNORE_DONT:
				say("Never ignoring messages from %s", 
						new_i->nick);
				break;
			    default:
				say("Ignoring ALL messages from %s", 
						new_i->nick);
				break;
			}
			return;
		}
		else if (type)
		{
			p = stpcpy(buffer, msg);
			if (type & IGNORE_MSGS)
				p = stpcpy(p, " MSGS");
			if (type & IGNORE_PUBLIC)
				p = stpcpy(p, " PUBLIC");
			if (type & IGNORE_WALLS)
				p = stpcpy(p, " WALLS");
			if (type & IGNORE_WALLOPS)
				p = stpcpy(p, " WALLOPS");
			if (type & IGNORE_INVITES)
				p = stpcpy(p, " INVITES");
			if (type & IGNORE_NOTICES)
				p = stpcpy(p, " NOTICES");
			if (type & IGNORE_NOTES)
				p = stpcpy(p, " NOTES");
			if (type & IGNORE_CTCPS)
				p = stpcpy(p, " CTCPS");
			if (type & IGNORE_TOPICS)
				p = stpcpy(p, " TOPICS");
			if (type & IGNORE_NICKS)
				p = stpcpy(p, " NICKS");
			if (type & IGNORE_JOINS)
				p = stpcpy(p, " JOINS");
			if (type & IGNORE_PARTS)
				p = stpcpy(p, " PARTS");
			if (type & IGNORE_CRAP)
				p = stpcpy(p, " CRAP");
			say("%s from %s", buffer, new_i->nick);
		}
	}
}

/*
 * remove_ignore: removes the given nick from the ignore list and returns 0.
 * If the nick wasn't in the ignore list to begin with, 1 is returned. 
 */
static int 	remove_ignore (char *nick)
{
	Ignore	*tmp;
	char	new_nick[IRCD_BUFFER_SIZE + 1];
	int	count = 0;
	char 	*mnick, *user, *host;

	if (figure_out_address(nick, &mnick, &user, &host))
		strlcpy(new_nick, nick, IRCD_BUFFER_SIZE);
	else
		snprintf(new_nick, IRCD_BUFFER_SIZE, "%s!%s@%s",
			mnick, user, host);

	/*
	 * Look for an exact match first.
	 */
	if ((tmp = (Ignore *) list_lookup((List **)&ignored_nicks, new_nick, !USE_WILDCARDS, REMOVE_FROM_LIST)) != NULL)
	{
		say("%s removed from ignorance list", tmp->nick);
		new_free(&(tmp->nick));
		new_free((char **)&tmp);
		count++;
	}

	/*
	 * Otherwise clear everything that matches.
	 */
	else while ((tmp = (Ignore *)list_lookup((List **)&ignored_nicks, new_nick, USE_WILDCARDS, REMOVE_FROM_LIST)) != NULL)
	{
		say("%s removed from ignorance list", tmp->nick);
		new_free(&(tmp->nick));
		new_free((char **)&tmp);
		count++;
	} 

	if (!count)
		say("%s is not in the ignorance list!", new_nick);

	return count;
}


#define BBS BIG_BUFFER_SIZE
#define HANDLE_TYPE(x, y)						\
	     if ((tmp->dont & x) == x)					\
		strmcat(buffer, " DONT-" y, BBS);			\
	else if ((tmp->type & x) == x)					\
		strmcat(buffer, " " y, BBS);				\
	else if ((tmp->high & x) == x)					\
		strmopencat(buffer, BBS, space, high, y, high, NULL);

char	*get_ignore_types (Ignore *tmp)
{
static	char 	buffer[BBS + 1];
	char	*high = highlight_char;

	*buffer = 0;
	HANDLE_TYPE(IGNORE_ALL, "ALL")
	else
	{
		HANDLE_TYPE(IGNORE_MSGS,    "MSGS")
		HANDLE_TYPE(IGNORE_PUBLIC,  "PUBLIC")
		HANDLE_TYPE(IGNORE_WALLS,   "WALLS")
		HANDLE_TYPE(IGNORE_WALLOPS, "WALLOPS")
		HANDLE_TYPE(IGNORE_INVITES, "INVITES")
		HANDLE_TYPE(IGNORE_NOTICES, "NOTICES")
		HANDLE_TYPE(IGNORE_NOTES,   "NOTES")
		HANDLE_TYPE(IGNORE_CTCPS,   "CTCPS")
		HANDLE_TYPE(IGNORE_TOPICS,  "TOPICS")
		HANDLE_TYPE(IGNORE_NICKS,   "NICKS")
		HANDLE_TYPE(IGNORE_JOINS,   "JOINS")
		HANDLE_TYPE(IGNORE_PARTS,   "PARTS")
		HANDLE_TYPE(IGNORE_CRAP,    "CRAP")
	}
	return buffer;
}


/* ignore_list: shows the entired ignorance list */
void	ignore_list (char *nick)
{
	Ignore	*tmp;
	int	len = 0;

	if (!ignored_nicks)
	{
		say("There are no nicknames being ignored");
		return;
	}	

	if (nick)
	{
		len = strlen(nick);
		upper(nick);
	}

	say("Ignorance List:");
	for (tmp = ignored_nicks; tmp; tmp = tmp->next)
	{
		if (nick)
		{
			if (strncmp(nick, tmp->nick, len))
				continue;
		}
		say("\t%s:\t%s", tmp->nick, get_ignore_types(tmp));
	}
}

/*
 * ignore: does the /IGNORE command.  Figures out what type of ignoring the
 * user wants to do and calls the proper ignorance command to do it. 
 */
BUILT_IN_COMMAND(ignore)
{
	char	*nick,
		*type;
	int	len;
	int	flag,
		no_flags;
	char	nick_buffer[127];

	if ((nick = next_arg(args, &args)) != NULL)
	{
		strlcpy(nick_buffer, nick, 127);
		no_flags = 1;

		while ((type = next_arg(args, &args)) != NULL)
		{
			nick = nick_buffer;

			no_flags = 0;
			upper(type);
			switch (*type)
			{
				case '!':
				case '^':
					flag = IGNORE_DONT;
					type++;
					break;
				case '-':
					flag = IGNORE_REMOVE;
					type++;
					break;
				case '+':
					flag = IGNORE_HIGH;
					type++;
					break;
				default:
					flag = 0;
					break;
			}
			if ((len = strlen(type)) == 0)
			{
				say("You must specify one of the following:");
				say("\tALL MSGS PUBLIC WALLS WALLOPS INVITES NOTICES NOTES TOPICS NICKS JOINS PARTS NONE");
				return;
			}

			     if (strncmp(type, "ALL", len) == 0)
				ignore_nickname(nick, IGNORE_ALL, flag);
			else if (strncmp(type, "MSGS", len) == 0)
				ignore_nickname(nick, IGNORE_MSGS, flag);
			else if (strncmp(type, "PUBLIC", len) == 0)
				ignore_nickname(nick, IGNORE_PUBLIC, flag);
			else if (strncmp(type, "WALLS", len) == 0)
				ignore_nickname(nick, IGNORE_WALLS, flag);
			else if (strncmp(type, "WALLOPS", len) == 0)
				ignore_nickname(nick, IGNORE_WALLOPS, flag);
			else if (strncmp(type, "INVITES", len) == 0)
				ignore_nickname(nick, IGNORE_INVITES, flag);
			else if (strncmp(type, "NOTICES", len) == 0)
				ignore_nickname(nick, IGNORE_NOTICES, flag);
			else if (strncmp(type, "NOTES", len) == 0)
				ignore_nickname(nick, IGNORE_NOTES, flag);
			else if (strncmp(type, "CTCPS", len) == 0)
				ignore_nickname(nick, IGNORE_CTCPS, flag);
			else if (strncmp(type, "TOPICS", len) == 0)
				ignore_nickname(nick, IGNORE_TOPICS, flag);
			else if (strncmp(type, "NICKS", len) == 0)
				ignore_nickname(nick, IGNORE_NICKS, flag);
			else if (strncmp(type, "JOINS", len) == 0)
				ignore_nickname(nick, IGNORE_JOINS, flag);
			else if (strncmp(type, "PARTS", len) == 0)
				ignore_nickname(nick, IGNORE_PARTS, flag);
			else if (strncmp(type, "CRAP", len) == 0)
				ignore_nickname(nick, IGNORE_CRAP, flag);
			else if (strncmp(type, "NONE", len) == 0)
			{
				char	*ptr;

				while (nick)
				{
					if ((ptr = strchr(nick, ',')) != NULL)
						*ptr = 0;

					if (*nick)
						remove_ignore(nick);

					if (ptr)
						*ptr++ = ',';
					nick = ptr;
				}
			}
			else
			{
				say("You must specify one of the following:");
				say("\tALL MSGS PUBLIC WALLS WALLOPS INVITES NOTICES NOTES CTCPS TOPICS NICKS JOINS PARTS CRAP NONE");
			}
		}
		if (no_flags)
			ignore_list(nick);
	} else
		ignore_list((char *) 0);
}

/*
 * set_highlight_char: what the name says..  the character to use
 * for highlighting..  either BOLD, INVERSE, or UNDERLINE..
 *
 * This does *not* belong here.
 */
void	set_highlight_char (char *s)
{
	int	len;

	if (!s)
		s = empty_string;
	len = strlen(s);

	if (!my_strnicmp(s, "BOLD", len))
		malloc_strcpy(&highlight_char, BOLD_TOG_STR);
	else if (!my_strnicmp(s, "INVERSE", len))
		malloc_strcpy(&highlight_char, REV_TOG_STR);
	else if (!my_strnicmp(s, "UNDERLINE", len))
		malloc_strcpy(&highlight_char, UND_TOG_STR);
	else
		malloc_strcpy(&highlight_char, s);
}


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
int	check_ignore (const char *nick, const char *userhost, int type)
{
	return check_ignore_channel(nick, userhost, NULL, type);
}

/*
 * "check_ignore_channel" is kind of a bad name for this function, but
 * i was not inspired with a better name.  This function is simply the
 * old 'check_ignore', only it has an additional check for a channel target.
 */
int	check_ignore_channel (const char *nick, const char *userhost, const char *channel, int type)
{
	char 	nuh[IRCD_BUFFER_SIZE];
	Ignore	*tmp;
	int	count = 0;
	int	bestimatch = 0;
	Ignore	*i_match = NULL;
	int	bestcmatch = 0;
	Ignore	*c_match = NULL;

	if (!ignored_nicks)
		return DONT_IGNORE;

	snprintf(nuh, IRCD_BUFFER_SIZE - 1, "%s!%s", nick ? nick : star,
						userhost ? userhost : star);

	for (tmp = ignored_nicks; tmp; tmp = tmp->next)
	{
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
		if (tmp->dont & type)
			return DONT_IGNORE;
		if (tmp->type & type)
			return IGNORED;
		if (tmp->high & type)
			return HIGHLIGHTED;
	}

	/*
	 * If the nickuserhost match did not say anything about the level
	 * that we are interested in, then try a channel ignore next.
	 */
	else if (c_match)
	{
		tmp = c_match;
		if (tmp->dont & type)
			return DONT_IGNORE;
		if (tmp->type & type)
			return IGNORED;
		if (tmp->high & type)
			return HIGHLIGHTED;
	}

	/*
	 * Otherwise i guess we dont ignore them.
	 */
	return DONT_IGNORE;
}


/*
 * get_ignores_by_pattern: Get all the ignores that match the pattern
 * If "covered" is 0, then return all ignores matched by patterns
 * If "covered" is 1, then return all ignores that would activate on patterns
 * MALLOCED
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
				m_sc3cat(&retval, space, tmp->nick, &clue);
		}
	}

	return retval ? retval : m_strdup(empty_string);
}

int	get_type_by_desc (char *type, int *do_mask, int *dont_mask)
{
	char	*l1, *l2;
	int	len;
	int	*mask = NULL;

	*do_mask = *dont_mask = 0;

	while (type && *type)
	{
		l1 = new_next_arg(type, &type);
		while (l1 && *l1)
		{
			l2 = l1;
			if ((l1 = strchr(l1, ',')))
				*l1++ = 0;

			if (*l2 == '!')
			{
				l2++;
				mask = dont_mask;
			}
			else
				mask = do_mask;

			if (!(len = strlen(l2)))
				continue;

			     if (!strncmp(l2, "ALL", len))
				*mask |= IGNORE_ALL;
			else if (!strncmp(l2, "MSGS", len))
				*mask |= IGNORE_MSGS;
			else if (!strncmp(l2, "PUBLIC", len))
				*mask |= IGNORE_PUBLIC;
			else if (!strncmp(l2, "WALLS", len))
				*mask |= IGNORE_WALLS;
			else if (!strncmp(l2, "WALLOPS", len))
				*mask |= IGNORE_WALLOPS;
			else if (!strncmp(l2, "INVITES", len))
				*mask |= IGNORE_INVITES;
			else if (!strncmp(l2, "NOTICES", len))
				*mask |= IGNORE_NOTICES;
			else if (!strncmp(l2, "NOTES", len))
				*mask |= IGNORE_NOTES;
			else if (!strncmp(l2, "CTCPS", len))
				*mask |= IGNORE_CTCPS;
			else if (!strncmp(l2, "TOPICS", len))
				*mask |= IGNORE_TOPICS;
			else if (!strncmp(l2, "NICKS", len))
				*mask |= IGNORE_NICKS;
			else if (!strncmp(l2, "JOINS", len))
				*mask |= IGNORE_JOINS;
			else if (!strncmp(l2, "PARTS", len))
				*mask |= IGNORE_PARTS;
			else if (!strncmp(l2, "CRAP", len))
				*mask |= IGNORE_CRAP;
		}
	}

	return 0;
}

/*
 * This is nasty and should be done in a more generalized way, but until
 * then, this function just does what has to be done.  Please note that 
 * if you go to the pains to re-write the ignore handling, please do fix
 * this to work the right way, please? =)
 */
char	*get_ignore_types_by_pattern (char *pattern)
{
	Ignore	*tmp;

	upper(pattern);
	for (tmp = ignored_nicks; tmp; tmp = tmp->next)
	{
		if (!strcmp(tmp->nick, pattern))
			return get_ignore_types(tmp);
	}

	return empty_string;
}

char	*get_ignore_patterns_by_type (char *ctype)
{
	Ignore	*tmp;
	int	do_mask = 0, dont_mask = 0;
	char	*result = NULL;
	size_t	clue = 0;

	/*
	 * Convert the user's input into something we can use.
	 * If the user doesnt specify anything useful, then we
	 * just punt right here.
	 */
	upper(ctype);
	get_type_by_desc(ctype, &do_mask, &dont_mask);
	if (do_mask == 0 && dont_mask == 0)
		return m_strdup(empty_string);

	for (tmp = ignored_nicks; tmp; tmp = tmp->next)
	{
		/*
		 * Any "negative ignore" bits, if any, must be present.
		 */
		if ((tmp->dont & dont_mask) != dont_mask)
			continue;

		/*
		 * Any "positive ignore" bits, if any, must be present,
		 * but there must not be a corresponding "negative ignore"
		 * bit for the levels as well.  That is to say, the 
		 * negative ignore bits "turn off" any corresponding bits
		 * in the positive ignore set.
		 */
		if (((tmp->type & ~tmp->dont) & do_mask) != do_mask)
			continue;

		/* Add it to the fray */
		m_sc3cat(&result, " ", tmp->nick, &clue);
	}

	return result;
}

