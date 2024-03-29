/*
 * alist.c -- resizeable arrays (dicts)
 *
 * Copyright 1997, 2015 EPIC Software Labs
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. All redistributions, whether of source code or in binary form must
 *    retain the the above copyright notice, the above paragraph (the one
 *    permitting redistribution), this list of conditions, and the following
 *    disclaimer.
 * 2. The names of the author(s) may not be used to endorse or promote 
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
#define __need_cs_alist_hash__
#define __need_ci_alist_hash__
#include "irc.h"
#include "alist.h"
#include "ircaux.h"
#include "output.h"

#define ALIST_ITEM(alist, loc) ((alist_item_ *) ((alist) -> list [ (loc) ]))
#define LALIST_ITEM(alist, loc) (((alist) -> list [ (loc) ]))

/* Function decls */
static	void	check_alist_size (alist *list);
	void 	move_alist_items (alist *list, int start, int end, int dir);

alist **all_alists = NULL;
int	all_alists_size = 0;

/*
 * Returns an entry that has been displaced, if any.
 * XXX 'item' shall be replaced with 'char *name' and 'void *data'
 */
void *	add_to_alist (alist *a, const char *name, void *item)
{
	int 		count;
	int 		location = 0;
	void *		ret = NULL;
	uint32_t	mask; 	/* Dummy var */
	alist_item_ *	item_;

	/* Initialize our internal item */
	item_ = (alist_item_ *)new_malloc(sizeof(alist_item_));
	item_->name = NULL;
	malloc_strcpy(&item_->name, name);	
	if (a->hash == HASH_INSENSITIVE)
		item_->hash = ci_alist_hash(item_->name, &mask);
	else
		item_->hash = cs_alist_hash(item_->name, &mask);
	item_->data = item;

	check_alist_size(a);
	if (a->max)
	{
		find_alist_item(a, name, &count, &location);
		if (count < 0)
		{
			ret = ALIST_ITEM(a, location)->data;
			a->max--;
		}
		else
			move_alist_items(a, location, a->max, 1);
	}

	a->list[location] = item_;
	a->max++;
	return ret;
}

/*
 * Returns the entry that has been removed, if any.
 */
void *	remove_from_alist (alist *a, const char *name)
{
	int 	count, 
		location = 0;

	if (a->max)
	{
		find_alist_item(a, name, &count, &location);
		if (count >= 0)
			return NULL;

		return alist_pop(a, location);
	}
	return NULL;	/* Cant delete whats not there */
}

/* Remove the 'which'th item from the given alist */
void *	alist_pop (alist *a, int which)
{
	alist_item_ *item_ = NULL;
	void *ret = NULL;

	if (which < 0 || which >= a->max)
		return NULL;

	item_ = ALIST_ITEM(a, which);
	ret = item_->data;

	move_alist_items(a, which + 1, a->max, -1);
	a->max--;
	check_alist_size(a);

	new_free(&item_->name);
	new_free((char **)&item_);
	return ret;
}

void *	alist_lookup (alist *a, const char *name, int wild, int rem)
{
	int 	count, 
		location;

	if (rem)
		return remove_from_alist(a, name);
	else
		return find_alist_item(a, name, &count, &location);
}

static void	check_alist_size (alist *a)
{
	if (a->total_max && (a->total_max < a->max))
		panic(1, "alist->max < alist->total_max");

	if (a->total_max == 0)
	{
		new_free(&a->list);		/* Immortality isn't necessarily a good thing. */
		a->total_max = 6;		/* Good size to start with */
	}
	else if (a->max == a->total_max - 1) /* Colten suggested this */
		a->total_max *= 2;
	else if ((a->total_max > 6) && (a->max * 3 < a->total_max))
		a->total_max /= 2;
	else
		return;

	RESIZE(a->list, alist_item_ *, a->total_max);
}

/*
 * Move ``start'' through ``end'' alist elements ``dir'' places up
 * in the alist.  If ``dir'' is negative, move them down in the alist.
 * Fill in the vacated spots with NULLs.
 */
void	move_alist_items (alist *a, int start, int end, int dir)
{
	int 	i;

	if (dir > 0)
	{
		for (i = end; i >= start; i--)
			LALIST_ITEM(a, i + dir) = ALIST_ITEM(a, i);
		for (i = dir; i > 0; i--)
			LALIST_ITEM(a, start + i - 1) = NULL;
	}
	else if (dir < 0)
	{
		for (i = start; i <= end; i++)
			LALIST_ITEM(a, i + dir) = ALIST_ITEM(a, i);
		for (i = end - dir + 1; i <= end; i++)
			LALIST_ITEM(a, i) = NULL;
	}
}


/*
 * This is just a generalization of the old function  ``find_command''
 *
 * You give it an alist_item and a name, and it gives you the number of
 * items that are completed by that name, and where you could find/put that
 * item in the list.  It returns the alist_item most appropriate.
 *
 * If ``cnt'' is less than -1, then there was one exact match and one or
 *	more ambiguous matches in addition.  The exact match's location 
 *	is put into ``loc'' and its entry is returned.  The ambigous matches
 *	are (of course) immediately subsequent to it.
 *
 * If ``cnt'' is -1, then there was one exact match.  Its location is
 *	put into ``loc'' and its entry is returned.
 *
 * If ``cnt'' is zero, then there were no matches for ``name'', but ``loc''
 * 	is set to the location in the alist in which you could place the
 * 	specified name in the sorted list.
 *
 * If ``cnt'' is one, then there was one command that non-ambiguously 
 * 	completes ``name''
 *
 * If ``cnt'' is greater than one, then there was exactly ``cnt'' number
 *	of entries that completed ``name'', but they are all ambiguous.
 *	The entry that is lowest alphabetically is returned, and its
 *	location is put into ``loc''.
 */
void *	find_alist_item (alist *set, const char *name, int *cnt, int *loc)
{
	size_t		len = strlen(name);
	intmax_t	c = 0;
	int		pos = 0, 
			tospot, /* :-) */
			min, 
			max;
	uint32_t	mask;
	uint32_t	hash;

	if (set->hash == HASH_INSENSITIVE)
		hash = ci_alist_hash(name, &mask);
	else
		hash = cs_alist_hash(name, &mask);

	*cnt = 0;
	if (!set->list || !set->max)
	{
		*loc = 0;
		return NULL;
	}

	/*
	 * The first search is a cheap refinement.  Using the hash
	 * value, find a range of symbols that have the same first
	 * four letters as 'name'.  Since we're doing all integer
	 * comparisons, its cheaper than the full blown string compares.
	 */
	/*
	 * OK, well all of that has been rolled into these two loops,
	 * designed to find the start and end of the range directly.
	 */

	tospot = max = set->max - 1;
	min = 0;

	while (max >= min)
	{
		pos = (max - min) / 2 + min;
		c = (intmax_t)(hash & mask) - (intmax_t)(ALIST_ITEM(set, pos)->hash & mask);
		if (c == 0) {
			c = set->func(name, ALIST_ITEM(set, pos)->name, len);
		}
		if (c == 0) {
			if (max == pos)
				break;
			max = pos;
		}
		else if (c < 0)
			tospot = max = pos - 1;
		else
			min = pos + 1;
	}

	/*
	 * If we can't find a symbol that qualifies, then we can just drop
	 * out here.  This is good because a "pass" (lookup for a symbol that
	 * does not exist) requires only cheap integer comparisons.
	 */
	if (c != 0)
	{
		if (c > 0)
			*loc = pos + 1;
		else
			*loc = pos;
		return NULL;
	}

	/*
	 * At this point, min is set to the first matching name in
	 * the range and tospot is set to a higher position than
	 * the last.  These are used to refine the next search.
	 */

	max = tospot;
	tospot = pos;

	while (max >= min)
	{
		pos = (min - max) / 2 + max;  /* Don't ask */
		c = (intmax_t)(hash & mask) - (intmax_t)(ALIST_ITEM(set, pos)->hash & mask);
		if (c == 0) {
			c = set->func(name, ALIST_ITEM(set, pos)->name, len);
		}
		if (c == 0) {
			if (min == pos)
				break;
			min = pos;
		}
		else if (c < 0)
			max = pos - 1;
		else
			min = pos + 1;
	}

	min = tospot;

	/*
	 * If we've gotten this far, then we've found at least
	 * one appropriate entry.  So we set *cnt to one
	 */
	*cnt = 1 + pos - min;

	/*
	 * Alphabetically, the string that would be identical to
	 * 'name' would be the first item in a string of items that
	 * all began with 'name'.  That is to say, if there is an
	 * exact match, its sitting under item 'min'.  So we check
	 * for that and whack the count appropriately.
	 */
	if (0 == (ALIST_ITEM(set, min)->name)[len])
		*cnt *= -1;

	/*
	 * Then we tell the caller where the lowest match is,
	 * in case they want to insert here.
	 */
	if (loc)
		*loc = min;

	/*
	 * Then we return the first item that matches.
	 */
	return ALIST_ITEM(set, min)->data;
}

void *	get_alist_item (alist *set, int location)
{
	if (location < 0 || location > set->max)
		return NULL;

	return ALIST_ITEM(set, location)->data;
}

