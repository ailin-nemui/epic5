/*
 * alist.h -- resizeable arrays (dicts)
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
static __inline uint32_t  cs_alist_hash (const char *s, uint32_t *mask)
{
	uint32_t	x = 0;

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
static __inline uint32_t  ci_alist_hash (const char *s, uint32_t *mask)
{
	uint32_t	x = 0;

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
	uint32_t	hash;
	void *		data;
} alist_item_;

typedef int       (*alist_func) (const char *, const char *, size_t);
typedef enum {
	HASH_INSENSITIVE,
	HASH_SENSITIVE
} hash_type;

/*
 * This is the actual list, that contains structs that are of the
 * form described above.  It contains the current size and the maximum
 * size of the alist.
 */
typedef struct
{
	alist_item_ **	list;
	int 		max;
	int 		total_max;
	alist_func 	func;
	hash_type 	hash;
} alist;

void *	add_to_alist 		(alist *, const char *, void *);
void *	remove_from_alist 	(alist *, const char *);
void *	alist_lookup 		(alist *, const char *, int, int);
void *	find_alist_item 	(alist *, const char *, int *, int *);
void *	alist_pop		(alist *, int);
void *  get_alist_item 		(alist *, int);

#endif
