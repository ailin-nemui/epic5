/*
 * list.h: header for list.c 
 *
 * Copyright 1990 Michael Sandrof
 * Copyright 1997 EPIC Software Labs
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 */

#ifndef __list_h__
#define __list_h__

/* Your type must be congruent to this type */
typedef	struct	list_stru
{
struct	list_stru *	next;
	char *		name;
	void *		d;
}	List;

void	add_item_to_list 	(List **, List *);
void	add_to_list 		(List **, const char *, void *);
List *	find_in_list 		(List *, const char *);
List *	remove_from_list 	(List **, const char *);
List *	remove_item_from_list	(List **, List *);

#endif /* _LIST_H */
