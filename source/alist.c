/* $EPIC: alist.c,v 1.14 2007/04/12 03:24:14 jnelson Exp $ */
/*
 * alist.c -- resizeable arrays.
 *
 * Copyright © 1997, 1998 EPIC Software Labs
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
/* 
 * This file presumes a good deal of chicanery.  Specifically, it assumes
 * that your compiler will allocate disparate structures congruently as 
 * long as the members match as to their type and location.  This is
 * critically important for how this code works, and all hell will break
 * loose if your compiler doesnt do this.  Every compiler i know of does
 * it, which is why im assuming it, even though im not allowed to assume it.
 *
 * This file is hideous.  Ill kill each and every one of you who made
 * me do this. ;-)
 */

#define __need_cs_alist_hash__
#define __need_ci_alist_hash__
#include "irc.h"
#include "alist.h"
#include "ircaux.h"
#include "output.h"

u_32int_t	bin_ints;
u_32int_t	lin_ints;
u_32int_t	bin_chars;
u_32int_t	lin_chars;
u_32int_t	alist_searches;
u_32int_t	char_searches;


/* Function decls */
static void check_array_size (array *list);
void move_array_items (array *list, int start, int end, int dir);

/*
 * Returns an entry that has been displaced, if any.
 */
array_item *add_to_array (array *a, array_item *item)
{
	int 		count;
	int 		location = 0;
	array_item *	ret = NULL;
	u_32int_t	mask; 	/* Dummy var */

	if (a->hash == HASH_INSENSITIVE)
		item->hash = ci_alist_hash(item->name, &mask);
	else
		item->hash = cs_alist_hash(item->name, &mask);

	check_array_size(a);
	if (a->max)
	{
		find_array_item(a, item->name, &count, &location);
		if (count < 0)
		{
			ret = ARRAY_ITEM(a, location);
			a->max--;
		}
		else
			move_array_items(a, location, a->max, 1);
	}

	a->list[location] = item;
	a->max++;
	return ret;
}

/*
 * Returns the entry that has been removed, if any.
 */
array_item *remove_from_array (array *a, const char *name)
{
	int 	count, 
		location = 0;

	if (a->max)
	{
		find_array_item(a, name, &count, &location);
		if (count >= 0)
			return NULL;

		return array_pop(a, location);
	}
	return NULL;	/* Cant delete whats not there */
}

/* Remove the 'which'th item from the given array */
array_item *array_pop (array *a, int which)
{
	array_item *ret = NULL;

	if (which < 0 || which >= a->max)
		return NULL;

	ret = ARRAY_ITEM(a, which);
	move_array_items(a, which + 1, a->max, -1);
	a->max--;
	check_array_size(a);
	return ret;
}

array_item *array_lookup (array *a, const char *name, int wild, int rem)
{
	int 	count, 
		location;

	if (rem)
		return remove_from_array(a, name);
	else
		return find_array_item(a, name, &count, &location);
}

static void check_array_size (array *a)
{
	if (a->total_max && (a->total_max < a->max))
		panic(1, "array->max < array->total_max");

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

	RESIZE(a->list, array_item *, a->total_max);
}

/*
 * Move ``start'' through ``end'' array elements ``dir'' places up
 * in the array.  If ``dir'' is negative, move them down in the array.
 * Fill in the vacated spots with NULLs.
 */
void move_array_items (array *a, int start, int end, int dir)
{
	int 	i;

	if (dir > 0)
	{
		for (i = end; i >= start; i--)
			LARRAY_ITEM(a, i + dir) = ARRAY_ITEM(a, i);
		for (i = dir; i > 0; i--)
			LARRAY_ITEM(a, start + i - 1) = NULL;
	}
	else if (dir < 0)
	{
		for (i = start; i <= end; i++)
			LARRAY_ITEM(a, i + dir) = ARRAY_ITEM(a, i);
		for (i = end - dir + 1; i <= end; i++)
			LARRAY_ITEM(a, i) = NULL;
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
 * 	is set to the location in the array in which you could place the
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
array_item *
find_array_item (array *set, const char *name, int *cnt, int *loc)
{
	size_t		len = strlen(name);
	int		c = 0, 
			pos = 0, 
			tospot, /* :-) */
			min, 
			max;
	u_32int_t	mask;
	u_32int_t	hash;

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

	alist_searches++;

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
		bin_ints++;
		pos = (max - min) / 2 + min;
		c = (hash & mask) - (ARRAY_ITEM(set, pos)->hash & mask);
		if (c == 0) {
			bin_chars++;
			c = set->func(name, ARRAY_ITEM(set, pos)->name, len);
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
		bin_ints++;
		pos = (min - max) / 2 + max;  /* Don't ask */
		c = (hash & mask) - (ARRAY_ITEM(set, pos)->hash & mask);
		if (c == 0) {
			bin_chars++;
			c = set->func(name, ARRAY_ITEM(set, pos)->name, len);
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

	char_searches++;

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
	if (0 == (ARRAY_ITEM(set, min)->name)[len])
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
	return ARRAY_ITEM(set, min);
}

