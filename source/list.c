/*
 * list.c: some generic linked list managing stuff 
 *
 * Written By Michael Sandrof
 *
 * Copyright(c) 1990 
 *
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 */

#if 0
static	char	rcsid[] = "@(#)$Id: list.c,v 1.1.1.1 2000/12/05 00:11:57 jnelson Exp $";
#endif

#include "irc.h"
#include "list.h"
#include "ircaux.h"

__inline__ int	add_list_strcmp (List *item1, List *item2)
{
	return my_stricmp(item1->name, item2->name);
}

__inline__ int	list_strcmp (List *item1, const char *str)
{
	return my_stricmp(item1->name, str);
}

__inline__ int	list_match (List *item1, const char *str)
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
 * list_lookup: this routine just consolidates remove_from_list and
 * find_in_list.  I did this cause it fit better with some alread existing
 * code 
 */
List 	*list_lookup (List **list, const char *name, int wild, int remove)
{
	List	*tmp;

	if (remove)
		tmp = remove_from_list(list, name);
	else
		tmp = find_in_list(list, name, wild);

	return (tmp);
}

