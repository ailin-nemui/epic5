/*
 * alist.h -- resizeable arrays
 * Copyright 1997 EPIC Software Labs
 */

#ifndef __alist_h__
#define __alist_h__
#include "irc.h"
#include "ircaux.h"

#ifdef __need_cs_alist_hash__
/* 
 * This hash routine is for case sensitive keys.  Specifically keys that
 * have been prefolded to an apppropriate case.
 */
static __inline u_32int_t  cs_alist_hash (const char *s, u_32int_t *mask)
{
	u_32int_t	x = 0;

	if (s[0] != 0)
	{
		if (s[1] == 0)
			x = (s[0] << 24), 
				*mask = 0xff000000;
		else if (s[2] == 0)
			x = (s[0] << 24) | (s[1] << 16), 
				*mask = 0xffff0000;
		else
			x = (s[0] << 24) | (s[1] << 16) | (s[2] << 8) | s[3], 
				(*mask = 0xffffff00 | (s[3] ? 0xff : 0x00));
	}
	else
		*mask = 0;
	return x;
}
#endif

#ifdef __need_ci_alist_hash__
extern unsigned char *stricmp_tables[2];
/*
 * This hash routine is for case insensitive keys.  Specifically keys that
 * cannot be prefolded to an appropriate case but are still insensitive
 */
static __inline u_32int_t  ci_alist_hash (const char *s, u_32int_t *mask)
{
	u_32int_t	x = 0;

	if (s[0] != 0)
	{
	    if (s[1] == 0)
		x = (stricmp_tables[0][(int)s[0]] << 24), 
				*mask = 0xff000000;
	    else if (s[2] == 0)
		x = ((stricmp_tables[0][(int)(unsigned char)s[0]] << 24) | 
		     (stricmp_tables[0][(int)(unsigned char)s[1]] << 16)), 
				*mask = 0xffff0000;
	    else
		x = ((stricmp_tables[0][(int)(unsigned char)s[0]] << 24) | 
		     (stricmp_tables[0][(int)(unsigned char)s[1]] << 16) | 
		     (stricmp_tables[0][(int)(unsigned char)s[2]] << 8) | 
		     (stricmp_tables[0][(int)(unsigned char)s[3]])),
				(*mask = 0xffffff00 | (s[3] ? 0xff : 0x00));
	}
	else
	    *mask = 0;

	return x;
}
#endif

typedef struct 
{
	char *		name;
	u_32int_t	hash;
	void *		data;
} array_item_;

typedef int       (*alist_func) (const unsigned char *, const unsigned char *, size_t);
typedef enum {
	HASH_INSENSITIVE,
	HASH_SENSITIVE
} hash_type;

/*
 * This is the actual list, that contains structs that are of the
 * form described above.  It contains the current size and the maximum
 * size of the array.
 */
typedef struct
{
	array_item_ **	list;
	int 		max;
	int 		total_max;
	alist_func 	func;
	hash_type 	hash;
} MAY_ALIAS array;

void *	add_to_array 		(array *, const char *, void *);
void *	remove_from_array 	(array *, const char *);
void *	array_lookup 		(array *, const char *, int, int);
void *	find_array_item 	(array *, const char *, int *, int *);
void *	array_pop		(array *, int);

#define ARRAY_ITEM(array, loc) ((array_item_ *) ((array) -> list [ (loc) ]))
#define LARRAY_ITEM(array, loc) (((array) -> list [ (loc) ]))

/* Written by panasync */
/* Re-written by CE */
#define GET_SOME_ARRAY_NAMES_FUNCTION(fn, array, test)                        \
char *(fn)(const char *str)                                                   \
{                                                                             \
	int i;                                                                \
	char *ret = NULL;                                                     \
	size_t rclue = 0;                                                     \
	for (i = 0; (test); ++i)                                              \
		if (!str || !*str || wild_match(str, (array)))                \
			malloc_strcat_wordlist_c(&ret, space, (array), &rclue);\
	return ret ? ret : malloc_strdup(empty_string);                       \
}
#define GET_ARRAY_NAMES_FUNCTION(fn, array) GET_SOME_ARRAY_NAMES_FUNCTION((fn), ((array.list)[i]->name), (i < (array).max))
#define GET_BUCKET_NAMES_FUNCTION(fn, bucket) GET_SOME_ARRAY_NAMES_FUNCTION((fn), ((bucket)->list[i].name), (i < (bucket)->numitems))

#endif
