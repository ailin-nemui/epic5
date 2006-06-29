/*
 * list.h: header for list.c 
 *
 * Copyright 1990 Michael Sandrof
 * Copyright 1997 EPIC Software Labs
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 */

#ifndef __list_h__
#define __list_h__

typedef	struct	list_stru
{
	struct	list_stru	*next;
	char	*name;
}	List;

void	add_to_list 		(List **, List *);
List	*find_in_list 		(List **, const char *, int);
List	*remove_from_list 	(List **, const char *);
List	*list_lookup 		(List **, const char *, int, int);
List *  remove_item_from_list	(List **list, List *item);

#define REMOVE_FROM_LIST 	1
#define USE_WILDCARDS 		1

#endif /* _LIST_H */
