/*
 * list.c: some generic linked list managing stuff 
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
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
#include "list.h"
#include "ircaux.h"
#include "reg.h"

static __inline__ int	add_list_strcmp (List *item1, List *item2)
{
	return my_stricmp(item1->name, item2->name);
}

static __inline__ int	list_strcmp (List *item1, const char *str)
{
	return my_stricmp(item1->name, str);
}

static __inline__ int	list_match (List *item1, const char *str)
{
	return wild_match(item1->name, str);
}

/*
 * add_to_list: This will add an element to a list.  The requirements for the
 * list are that the first element in each list structure be a pointer to the
 * next element in the list, and the second element in the list structure be
 * a pointer to a character (char *) which represents the sort key.  For
 * example 
 *
 * struct my_list{ struct my_list *next; char *name; <whatever else you want>}; 
 *
 * The parameters are:  "list" which is a pointer to the head of the list. "add"
 * which is a pre-allocated element to be added to the list.
 */
void 	add_to_list (List **list, List *add)
{
	List	*tmp,
		*last = NULL;

	for (tmp = *list; tmp; tmp = tmp->next)
	{
		if (add_list_strcmp(tmp, add) > 0)
			break;
		last = tmp;
	}

	if (last)
		last->next = add;
	else
		*list = add;

	add->next = tmp;
	return;
}

/*
 * find_in_list: This looks up the given name in the given list.  List and
 * name are as described above.  If wild is true, each name in the list is
 * used as a wild card expression to match name... otherwise, normal matching
 * is done 
 */
List	*find_in_list (List **list, const char *name, int wild)
{
	List	*tmp;
	int	best_match = 0,
		current_match;
	int	(*cmp_func) (List *, const char *);

	cmp_func = wild ? list_match : list_strcmp;

	if (wild)
	{
		List	*match = (List *) 0;

		for (tmp = *list; tmp; tmp = tmp->next)
			if ((current_match = cmp_func(tmp, name)) > best_match)
				match = tmp, best_match = current_match;

		return (match);
	}
	else
	{
		for (tmp = *list; tmp; tmp = tmp->next)
			if (cmp_func(tmp, name) == 0)
				return (tmp);
	}

	return ((List *) 0);
}

/*
 * remove_from_list: this remove the given name from the given list (again as
 * described above).  If found, it is removed from the list and returned
 * (memory is not deallocated).  If not found, null is returned. 
 */
List	*remove_from_list (List **list, const char *name)
{
	List	*tmp,
		*last = NULL;

	for (tmp = *list; tmp; tmp = tmp->next)
	{
		if (list_strcmp(tmp, name) == 0)
		{
			if (last)
				last->next = tmp->next;
			else
				*list = tmp->next;
			return (tmp);
		}
		last = tmp;
	}

	return ((List *) 0);
}

/*
 * remove_item_from_list: this remove the given item from the given list 
 * (again as described above).  If found, it is removed from the list and 
 * returned (memory is not deallocated).  If not found, null is returned. 
 */
List *	remove_item_from_list (List **list, List *item)
{
	List	*tmp,
		*last = NULL;

	for (tmp = *list; tmp; tmp = tmp->next)
	{
		if (tmp == item)
		{
			if (last)
				last->next = tmp->next;
			else
				*list = tmp->next;
			return (tmp);
		}
		last = tmp;
	}

	return NULL;
}
/*
 * list_lookup: this routine just consolidates remove_from_list and
 * find_in_list.  I did this cause it fit better with some alread existing
 * code 
 */
List 	*list_lookup (List **list, const char *name, int wild, int rem)
{
	List	*tmp;

	if (rem)
		tmp = remove_from_list(list, name);
	else
		tmp = find_in_list(list, name, wild);

	return (tmp);
}

