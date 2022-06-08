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

/*
 * add_to_list: This will add an element to a list.  
 *
 * The list is always sorted by 'name' ascendingly.  Duplicate 'name' values
 * are always retained, but are in an unspecified order, and result in name-lookups
 * (such as "find_in_list()" being in unspecified order).
 *
 * Unlike in C90, you can no longer create type-punned List-congruent objects.
 * You _must_ use the (List) object, and hang your data structure off of 'd'.
 *
 * The (List) object contains 
 *  (1) a link to the next (List) item, 
 *  (2) a unique key ("name"), and
 *  (3) a pointer to a data item.  Your data item may contain whatever you want.
 *
 * You are always responsible for the memory of the object and its data.
 * These routines do nothing but keep the list in sorted order.
 */
void 	add_item_to_list (List **list, List *add)
{
	List	*tmp,
		*last = NULL;

	for (tmp = *list; tmp; tmp = tmp->next)
	{
		if (my_stricmp(tmp->name, add->name) > 0)
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

void	add_to_list (List **list, const char *name, void *data)
{
	List *	tmp;

	tmp = new_malloc(sizeof(*tmp));
	tmp->name = malloc_strdup(name);
	tmp->d = data;
	add_item_to_list(list, tmp);
}

/*
 * find_in_list: This looks up the given name in the given list.  List and
 * name are as described above.  If wild is true, each name in the list is
 * used as a wild card expression to match name... otherwise, normal matching
 * is done 
 *
 * If multiple items with the same 'name' have been added to the list,
 * which one gets returned is unspecified.
 */
List *	find_in_list (List *list, const char *name)
{
	List	*tmp;

	for (tmp = list; tmp; tmp = tmp->next)
		if (my_stricmp(tmp->name, name) == 0)
			return (tmp);

	return NULL;
}

/*
 * remove_from_list: this remove the given name from the given list (again as
 * described above).  If found, it is removed from the list and returned
 * (memory is not deallocated).  If not found, null is returned. 
 */
List *	remove_from_list (List **list, const char *name)
{
	List	*tmp,
		*last = NULL;

	for (tmp = *list; tmp; tmp = tmp->next)
	{
		if (my_stricmp(tmp->name, name) == 0)
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

