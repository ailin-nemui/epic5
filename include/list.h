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
}	MAY_ALIAS List;

/* Don't use the internal API - Use the macros below */
void	add_to_list 		(List **, List *);
List *	find_in_list 		(List *, const char *, int);
List *	remove_from_list 	(List **, const char *);
List *	list_lookup 		(List **, const char *, int, int);
List *	remove_item_from_list	(List **, List *);

#define REMOVE_FROM_LIST 	1
#define USE_WILDCARDS 		1

/* 
 * These are c99 macros that implement type punning.
 * See https://www.cocoawithlove.com/2008/04/using-pointers-to-recast-in-c-is-bad.html 
 */
#define CAST_TO_LISTP(x) (((union {__typeof__(x) a; List **b;})x).b)
#define CAST_TO_LIST(x) (((union {__typeof__(x) a; List *b;})x).b)
#define CAST_FROM_LIST(x, y) (x = ((union {__typeof__(x) a; List *b;})y).a)

#if 0
/* Use these macros, they're c99-safe */
#define ADD_TO_LIST_(l, i) 				add_to_list(CAST_TO_LISTP(l), CAST_TO_LIST(i))
#define FIND_IN_LIST_(retval, list, str, flags) 	CAST_FROM_LIST(retval, find_in_list(CAST_TO_LIST(list), str, flags))
#define EXISTS_IN_LIST_(list, str, flags)		(find_in_list(CAST_TO_LIST(list), str, flags) != NULL)
#define REMOVE_FROM_LIST_(retval, list, str) 		CAST_FROM_LIST(retval, remove_from_list(CAST_TO_LISTP(list), str))
#define LIST_LOOKUP_(retval, list, str, flag1, flag2)	CAST_FROM_LIST(retval, list_lookup(CAST_TO_LISTP(list), str, flag1, flag2))
#define REMOVE_ITEM_FROM_LIST_(l, i)			remove_item_from_list(CAST_TO_LISTP(l), CAST_TO_LIST(i))
#else
#define ADD_TO_LIST_(l, i) 				(add_to_list(l, i))
#define FIND_IN_LIST_(retval, list, str, flags) 	(retval = find_in_list(list, str, flags))
#define EXISTS_IN_LIST_(list, str, flags)		(find_in_list(list, str, flags) != NULL)
#define REMOVE_FROM_LIST_(retval, list, str) 		(retval = remove_from_list(list, str))
#define LIST_LOOKUP_(retval, list, str, flag1, flag2)	(retval = list_lookup(list, str, flag1, flag2))
#define REMOVE_ITEM_FROM_LIST_(l, i)			(remove_item_from_list(l, i))
#endif

#endif /* _LIST_H */
