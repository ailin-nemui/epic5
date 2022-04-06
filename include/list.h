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
struct	list_stru *	next;
	char *		name;
}	List;

void	add_to_list 		(List **, List *);
List *	find_in_list 		(List *, const char *, int);
List *	remove_from_list 	(List **, const char *);
List *	list_lookup 		(List **, const char *, int, int);
List *	remove_item_from_list	(List **, List *);

#define REMOVE_FROM_LIST 	1
#define USE_WILDCARDS 		1


typedef	struct	newlist_stru
{
struct	newlist_stru *	next;
	char *		name;
	void *		data;
}	NewList;

	void		add_to_newlist 		(NewList **, NewList *);
	NewList *	find_in_newlist 	(NewList *, const char *, int);
	NewList *	remove_from_newlist 	(NewList **, const char *);
	NewList *	newlist_lookup 		(NewList **, const char *, int, int);
	NewList *	remove_item_from_newlist (NewList **, NewList *);


#endif /* _LIST_H */
