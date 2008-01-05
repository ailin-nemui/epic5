/* $EPIC: levels.c,v 1.12 2008/01/05 19:00:26 jnelson Exp $ */
/*
 * levels.c - Sorting things by category -- Window/Lastlog, Ignore, and Floods
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright © 1993, 2005 EPIC Software Labs.
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
#include "levels.h"
#include "ircaux.h"
#include "functions.h"
#include "output.h"

int	LEVEL_NONE,	LEVEL_CURRENT,	LEVEL_OTHER;
int	LEVEL_PUBLIC,	LEVEL_MSG,	LEVEL_NOTICE,	LEVEL_WALL;
int	LEVEL_WALLOP,	LEVEL_OPNOTE,	LEVEL_SNOTE,	LEVEL_ACTION;
int	LEVEL_DCC,	LEVEL_CTCP,	LEVEL_INVITE,	LEVEL_JOIN;
int	LEVEL_NICK,	LEVEL_TOPIC,	LEVEL_PART,	LEVEL_QUIT;
int	LEVEL_KICK,	LEVEL_MODE,	LEVEL_OPERWALL,	LEVEL_SYSERR;
int	LEVEL_USER1,	LEVEL_USER2;
int	LEVEL_USER3,	LEVEL_USER4,	LEVEL_USER5,	LEVEL_USER6;
int	LEVEL_USER7,	LEVEL_USER8,	LEVEL_USER9,	LEVEL_USER10;
int	LEVEL_ALL;

static	Bucket *level_bucket = NULL;
static	int	next_level = 1;

void	init_levels (void)
{
	level_bucket = new_bucket();
	LEVEL_NONE      = 0;
	LEVEL_OTHER     = add_new_level("OTHER");
	add_new_level_alias(LEVEL_OTHER, "CRAP");
	LEVEL_PUBLIC    = add_new_level("PUBLICS");
	LEVEL_MSG       = add_new_level("MSGS");
	LEVEL_NOTICE    = add_new_level("NOTICES");
	LEVEL_WALL      = add_new_level("WALLS");
	LEVEL_WALLOP    = add_new_level("WALLOPS");
	LEVEL_OPNOTE    = add_new_level("OPNOTES");
	LEVEL_SNOTE     = add_new_level("SNOTES");
	LEVEL_ACTION    = add_new_level("ACTIONS");
	LEVEL_DCC       = add_new_level("DCCS");
	LEVEL_CTCP      = add_new_level("CTCPS");
	LEVEL_INVITE    = add_new_level("INVITES");
	LEVEL_JOIN      = add_new_level("JOINS");
	LEVEL_NICK      = add_new_level("NICKS");
	LEVEL_TOPIC     = add_new_level("TOPICS");
	LEVEL_PART      = add_new_level("PARTS");
	LEVEL_QUIT      = add_new_level("QUITS");
	LEVEL_KICK      = add_new_level("KICKS");
	LEVEL_MODE      = add_new_level("MODES");
	LEVEL_OPERWALL	= add_new_level("OPERWALL");
	LEVEL_SYSERR	= add_new_level("SYSERR");
	LEVEL_USER1     = add_new_level("USER1");
	LEVEL_USER2     = add_new_level("USER2");
	LEVEL_USER3     = add_new_level("USER3");
	LEVEL_USER4     = add_new_level("USER4");
	LEVEL_USER5     = add_new_level("USER5");
	LEVEL_USER6     = add_new_level("USER6");
	LEVEL_USER7     = add_new_level("USER7");
	LEVEL_USER8     = add_new_level("USER8");
	LEVEL_USER9     = add_new_level("USER9");
	LEVEL_USER10    = add_new_level("USER10");
	LEVEL_ALL       = 0x7FFFFFFF;
}

#define LEVELNAME(i) level_bucket->list[i].name
#define LEVELNUM(i) (*(int *)(level_bucket->list[i].stuff))

int	add_new_level (const char *name)
{
	const char *name_copy;
	int *	levelnum;
	int	i;

	if ((i = str_to_level(name)) != -1)
		return i;

	/* Don't allow overflow */
	if (next_level >= BIT_MAXBIT)
		return -1;

	name_copy = malloc_strdup(name);
	levelnum = new_malloc(sizeof(int));
	*levelnum = next_level++;
	add_to_bucket(level_bucket, name_copy, levelnum);
	return *levelnum;
}

int	add_new_level_alias (int level, const char *name)
{
	const char *name_copy;
	int *	levelnum;
	int	i;

	if ((i = str_to_level(name)) != -1)
		return i;

	name_copy = malloc_strdup(name);
	levelnum = new_malloc(sizeof(int));
	*levelnum = level;
	add_to_bucket(level_bucket, name_copy, levelnum);
	return *levelnum;
}

char *	get_all_levels (void)
{
	char *buffer = NULL;
	size_t clue = 0;
	int	i;
	int	next = 1;

	for (i = 0; i < level_bucket->numitems; i++)
	{
	    /* This is done to skip aliases... */
	    if (LEVELNUM(i) == next)
	    {
	        malloc_strcat_word_c(&buffer, space, LEVELNAME(i), DWORD_NO, &clue);
		next++;
	    }
	}

	return buffer;
}

static const char *	mask_to_positive_str (const Mask *mask)
{
	static char	buffer[512];
	int	i;
	int	next = 1;

	*buffer = 0;
	for (i = 0; i < level_bucket->numitems; i++)
	{
	    /* This is done to skip aliases... */
	    if (LEVELNUM(i) == next)
	    {
		if (mask_isset(mask, next))
		{
		    if (*buffer)
			strlcat(buffer, " ", sizeof buffer);
		    strlcat(buffer, LEVELNAME(i), sizeof buffer);
		}
		next++;
	    }
	}
	return buffer;
}

static const char *	mask_to_negative_str (const Mask *mask)
{
	static char	buffer[512];
	int	i;
	int	next = 1;

	*buffer = 0;
	strlcpy(buffer, "ALL", sizeof buffer);

	for (i = 0; i < level_bucket->numitems; i++)
	{
	    /* This is done to skip aliases... */
	    if (LEVELNUM(i) == next)
	    {
		if (!mask_isset(mask, next))
		{
		    if (*buffer)
			strlcat(buffer, " -", sizeof buffer);
		    strlcat(buffer, LEVELNAME(i), sizeof buffer);
		}
		next++;
	    }
	}
	return buffer;
}

const char *	mask_to_str (const Mask *mask)
{
	static	char	buffer[512]; /* this *should* be enough for this */
	const char 	*str1, *str2;

	if (mask_isall(mask))
	{
		strlcpy(buffer, "ALL", sizeof buffer);
		return buffer;
	}
	if (mask_isnone(mask))
	{
		strlcpy(buffer, "NONE", sizeof buffer);
		return buffer;
	}

	str1 = mask_to_positive_str(mask);
	str2 = mask_to_negative_str(mask);
	if (strlen(str1) <= strlen(str2))
		return str1;
	else
		return str2;
}

int	str_to_mask (Mask *mask, const char *orig, char **rejects)
{
	char	*ptr,
		*rest;
	int	len,
		i,
		neg;
	int	warn = 0;
	char *	str;
	size_t	cluep = 0;

	mask_unsetall(mask);

	if (!orig)
		return 0;		/* Whatever */

	if (rejects == NULL || *rejects != NULL)
		panic(1, "str_to_mask: rejects must be a pointer to null");

	str = LOCAL_COPY(orig);
	while ((str = next_arg(str, &rest)) != NULL)
	{
	    while (str)
	    {
		if ((ptr = strchr(str, ',')) != NULL)
			*ptr++ = 0;
		if ((len = strlen(str)) != 0)
		{
			if (my_strnicmp(str, "ALL", len) == 0)
				mask_setall(mask);
			else if (my_strnicmp(str, "NONE", len) == 0)
				mask_unsetall(mask);
			else
			{
			    if (*str == '-')
			    {
				str++, len--;
				neg = 1;
			    }
			    else
				neg = 0;

			    for (i = 0; i < level_bucket->numitems; i++)
			    {
				if (!my_strnicmp(str, LEVELNAME(i), len))
				{
					if (neg)
					    mask_unset(mask, LEVELNUM(i));
					else
					    mask_set(mask, LEVELNUM(i));
					break;
				}
			    }

			    if (i == level_bucket->numitems)
				malloc_strcat_word_c(rejects, space, str, 
							DWORD_NO, &cluep);
			}
		}
		str = ptr;
	    }
	    str = rest;
	}

	if (rejects && *rejects)
		return -1;

	return 0;
}

int	standard_level_warning (const char *who, char **rejects)
{
	if (rejects && *rejects)
	{
		char *s;

		say("%s ignored the these unsupported levels: %s", 
				who, *rejects);
		s = get_all_levels();
		say("The valid levels are: %s", s);
		new_free(&s);
		new_free(rejects);
	}
}


int	str_to_level (const char *orig)
{
	int	i, len;

	len = strlen(orig);
	for (i = 0; i < level_bucket->numitems; i++)
	    if (!my_strnicmp(orig, LEVELNAME(i), len))
		return LEVELNUM(i);

	return -1;
}

const char *	level_to_str (int l)
{
	int	i;

	if (l == LEVEL_NONE)
		return "NONE";
	else if (l == LEVEL_ALL)
		return "ALL";
	else
	{
	    for (i = 0; i < level_bucket->numitems; i++)
		if (l == LEVELNUM(i))
		    return LEVELNAME(i);
	}

	return empty_string;
}

/*
 * $levelctl(LEVELS)
 * $levelctl(ADD name)
 * $levelctl(ALIAS old-name new-name)
 * $levelctl(LOOKUP name-or-number)
 * $levelctl(NORMALIZE string)
 */
char *levelctl	(char *input)
{
	char	*listc, *retval;
	const char *newlevel, *oldlevel;
	int	oldnum, newnum;

	GET_FUNC_ARG(listc, input);
        if (!my_strnicmp(listc, "LEVELS", 2)) {
		retval = get_all_levels();
		RETURN_MSTR(retval);
        } else if (!my_strnicmp(listc, "ADD", 2)) {
		GET_FUNC_ARG(newlevel, input);
		newnum = add_new_level(newlevel);
		RETURN_INT(newnum);
        } else if (!my_strnicmp(listc, "ALIAS", 2)) {
		GET_FUNC_ARG(oldlevel, input);
		GET_FUNC_ARG(newlevel, input);
		oldnum = str_to_level(oldlevel);
		newnum = add_new_level_alias(oldnum, newlevel);
		RETURN_INT(newnum);
        } else if (!my_strnicmp(listc, "LOOKUP", 2)) {
		GET_FUNC_ARG(newlevel, input);
		if (is_number(newlevel)) {
			oldnum = STR2INT(newlevel);
			oldlevel = level_to_str(oldnum);
			RETURN_STR(oldlevel);
		} else {
			oldnum = str_to_level(newlevel);
			RETURN_INT(oldnum);
		}
        } else if (!my_strnicmp(listc, "NORMALIZE", 1)) {
		Mask m;
		const char *r;
		char *error = NULL;

		mask_unsetall(&m);
		str_to_mask(&m, input, &error);	/* Errors are ignored */
		r = mask_to_str(&m);
		RETURN_STR(r);
	}

        RETURN_EMPTY;
}

