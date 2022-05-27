/*
 * ara.h
 * Copyright 2022 EPIC Software Labs
 */

#ifndef __ara_h__
#define __ara_h__
#include "irc.h"
#include "ircaux.h"

typedef union AraItem 
{
	void *	d;
	void (*f)(void);
} AraItem;

typedef struct AraStru
{
	AraItem *list;		/* A bunch of pointers to things */
	size_t	size;		/* How big 'list' is (size >= max) */
	int	type;		/* Whether this is a data ara or func ara */
} ara;

#define ARA_TYPE_DATA	1
#define ARA_TYPE_FUNC	2

void	init_ara 		(ara *a, int ara_type, size_t maximum_location);
void	ara_set_item 		(ara *a, int location, AraItem data, int *errocode);
AraItem ara_get_item 		(ara *a, int location, int *errcode);
AraItem ara_update_item 	(ara *a, int location, AraItem data, int *errcode);
AraItem ara_remove_item 	(ara *a, int location, int *errcode);
int	traverse_all_ara_items 	(ara *a, int *location);
int	ara_max 		(ara *a);
int	ara_number 		(ara *a);
int	ara_find_item 		(ara *a, AraItem item);
int	ara_unshift 		(ara *a, AraItem new_item);
AraItem ara_shift 		(ara *a);
int	ara_push 		(ara *a, AraItem new_item);
AraItem ara_pop 		(ara *a);
AraItem ara_pop_item 		(ara *a, int location);
int	ara_insert_item 	(ara *a, int location, AraItem new_item);
int	ara_insert_before_item 	(ara *a, AraItem existing_item, AraItem new_item);
int	ara_insert_after_item 	(ara *a, AraItem existing_item, AraItem new_item);
int	ara_collapse 		(ara *a);

#endif
