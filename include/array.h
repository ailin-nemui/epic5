/* 
 * array.h -- header file for array.c
 *
 * Copyright 1993 Aaron Gifford
 * Copyright 1995 EPIC Software Labs
 * See the COPYRIGHT file for copyright information
 */

#ifndef __array_h__
#define __array_h__

	char *	function_indextoitem 	(char *);
	char *	function_itemtoindex 	(char *);
	char *	function_igetitem 	(char *);
	char *	function_getitem 	(char *);
	char *	function_setitem 	(char *);
	char *	function_finditem 	(char *);
	char *	function_matchitem 	(char *);
	char *	function_rmatchitem 	(char *);
	char *	function_getmatches 	(char *);
	char *	function_igetmatches 	(char *);
	char *	function_getrmatches 	(char *);
	char *	function_igetrmatches 	(char *);
	char *	function_delitem 	(char *);
	char *	function_numitems 	(char *);
	char *	function_getarrays 	(char *);
	char *	function_numarrays 	(char *);
	char *	function_delarray 	(char *);
	char *	function_ifinditem 	(char *);
	char *	function_ifindfirst 	(char *);
	char *	function_listarray	(char *);
	char *	function_gettmatch	(char *);

#endif

typedef struct an_array_struct {
        char **item;
        long *index;
        long size;
} an_array;
