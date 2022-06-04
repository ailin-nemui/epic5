/*
 * array.c -- Karll's Array Suite
 *
 * Copyright 1993, 2003 Aaron Gifford and others.
 * All rights reserved, used with permission.
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
/*
	DATE OF THIS VERSION:
	---------------------
	Sat Nov 27 23:00:20 MST 1993

	NEW SINCE 20 NOV 1993:
	----------------------
  	Two new functions were added: GETMATCHES() and GETRMATCHES()

	BUGS:
	-----
  	I had a script that used these functions that caused ircII to crash 
	once or twice, but I have been unable to trace the cause or reproduce 
	the crash.  I would appreciate any help or info if anyone else 
	experiences similar problems.  This IS my first time using writing code 
	to work with ircII code.

	ARGUMENTS:
	----------
  	array_name  : A string of some sort (no spaces, case insensitive) 
		identifying an array, either an existing array, or a new array.

  	item_number : A number or index into the array.  All array items are 
		numbered beginning at zero and continuing sequentially.  The 
		item number can be an existing item of an existing array, a 
		new item in an existing array (if the previous greatest item 
		number was 5, then the new item number must be 6, maintaining 
		the sequence), or a new item (must be item zero) in a new array.
  	data_...    : Basically any data you want.  It is stored internally as a
                character string, and in indexed (more to come) internally
                using the standard C library function strcmp().

	FUNCTIONS:
	----------
 	SETITEM(array_name item_number data_to_be_stored)
    	Use SETITEM to set an existing item of an existing array to a new value,
    	to create a new value in an existing array (see note about item_number),
    	or to create a new array (see note about item_number above).
    	RETURNS: 0 (zero) if it successfully sets and existing item in the array
             	 1 if it successfully creates a new array
             	 2 if it successfully adds a new item to an existing array 
            	-1 if it was unable to find the array specified (item_number > 0)
            	-2 if it was unable to find the item in an existing array
               		(item_number was too large)

  	GETITEM(array_name item_number)
    	Use this to retrieve previously stored data.
    	RETURNS: the data requested
             OR an empty string if the array did not exist or the item did not.

  	NUMITEMS(array_name)
    	RETURNS: the number of items in the array
             OR zero if the array name is invalid.  Useful for auto-adding to
             an array:
                 alias ADDDATA {
                     if (SETITEM(my-array $NUMITEMS(my-array) $*) >= 0) {
                         echo ADDED DATA
                     } {
                         echo FAILED TO ADD DATA
                     }
                 }
 
  	DELITEM(array_name item_number)
    	This deletes the item requested from the array.  If it is the last item
    	(item zero), it deletes the array.  If there are items numbered higher
    	than this item, they will each be moved down.  So if we had a 25 item
    	array called "MY-ARRAY" and deleted item 7, items 8 through 24 (remember
    	that a 25 item array is numbered 0 to 24) would be moved down and become
    	items 7 through 23.
    	RETURNS:  Zero on success,
              -1 if unable to find the array,
              -2 if unable find the item.

	DELITEMS(array_name item_number ...)
	This functions exactly like DELITEM() but it deletes any number of items
	and performs better for deleting larger numbers of items.  Note that
	DELITEM() still performs better for deleting a single item.

  	MATCHITEM(array_name pattern)
    	Searches through the items in the array for the item that best matches 
	the pattern, much like the MATCH() function does.
    	RETURNS: zero or a positive number which is the item_number of the match
             OR -1 if unable to find the array,
             OR -2 if no match was found in the array

  	RMATCHITEM(array_name data_to_look_for)
    	This treats the items in the array as patterns, searching for the 
	pattern in the array that best matches the data_to_look_for, working 
	similarly to the RMATCH() function.
    	RETURNS: zero or a positive number which is the item_number of the match
             OR -1 if unable to find the array,
             OR -2 if no match was found in the array

  	GETMATCHES(array_name pattern)
    	Seeks all array items that match the pattern.
   	RETURNS: A string containing a space-separated list of item numbers of
             array elements that match the pattern, or a null string if no
             matches were found, or if the array was not found.

  	GETRMATCHES(array_name data_to_look_for)
    	Treats all array items as patterns, and seeks to find all patterns that
    	match the data_to_look_for.
    	RETURNS: A string containing a space-separated list of item numbers of
             array elements that match the data, or a null string if no
             matches were found, or if the array was not found.

  	FINDITEM(array_name data_to_search_for)
    	This does a binary search on the items stored in the array and returns
    	the item number of the data.  It is an EXACT MATCH search.  It is highly
    	case sensitive, using C's strcmp() and not IRCII's caseless comparison
   	functions.  I did it this way, because I needed it, so there!  ;)
    	RETURNS: zero or a positive number on success -- this number IS the
             item_number of the first match it found
             OR -1 if unable to find the array,
             OR -2 if the item was not found in the array.

  	IGETITEM(array_name index_number)
    	This is exactly like GETITEM() except it uses a index number in the same
   	range as the item_number's.  It returns the item that corresponds to the
    	internal alphabetized index that these functions maintain.  Thus if you
    	access items 0 to 24 of "MY-ARRAY" with this function, you would observe
    	that the items returned came in an almost alphabetized manner.  They 
	would not be truly alphabetized because the ordering is done using 
	strcmp() which is case sensitive.
    	RETURNS: the data to which the index refers
             OR an empty string on failure to find the array or index.

  	INDEXTOITEM(array_name index_number)
    	This converts an index_number to an item_number.
    	RETURNS: the item_number that corresponds to the index_number for 
		the array
             OR -1 if unable to find the array,
             OR -2 if the index_number was invalid

  	ITEMTOINDEX(array_name item_number)
    	This converts an item_number to an index_number.
    	RETURNS: the index_number that corresponds to the item_number for 
		the array
             OR -1 if unable to find the array,
             OR -2 if the item_number was invalid

  	DELARRAY(array_name)
    	This deletes all items in an array.
    	RETURNS:  zero on success, -1 if it couldn't find the array.

  	NUMARRAYS()
    	RETURNS: the number of arrays that currently exist.

  	GETARRAYS()
    	RETURNS: a string consisting of all the names of all the arrays 
		 separated by spaces.

Thanks, 
Aaron Gifford
Karll on IRC
<agifford@sci.dixie.edu>

*/
/*
 * FILE:       array.c
 * WRITTEN BY: Aaron Gifford (Karll on IRC)
 * DATE:       Sat Nov 27 23:00:20 MST 1993
 */

#include "irc.h"
#include "array.h"
#include "ircaux.h"
#include "output.h"
#include "functions.h"
#include "words.h"
#include "reg.h"
#undef BUILT_IN_FUNCTION
#define BUILT_IN_FUNCTION(x, y) char * x (char * y)
#undef index			/* doh! */

#define ARRAY_THRESHOLD	10

#if 0
typedef struct an_array_struct {
	char **item;
	long *index;
	long size;
} an_array;
#endif

static an_array array_info = {
        NULL,
        NULL,
        0L,
	1
};

static an_array *array_array = NULL;
an_array *qsort_array;

static int compare_indices (const void *a1, const void *a2)
{
	int result;

	result = strcmp(qsort_array->item[*(const long *)a1],
			qsort_array->item[*(const long *)a2]);

	/* array is (to be) sorted by name, then by item number. */
	if (result)
		return result;
	else
		return *(const long *)a1 - *(const long *)a2;
}

static void sort_indices (an_array *array)
{
	qsort_array = array;
	qsort(array->index, array->size, sizeof(long *), compare_indices);
	array->unsorted = 0;
}

#define SORT_INDICES(arrayp) {if ((arrayp)->unsorted) sort_indices((arrayp));}

/*
 * find_item() does a binary search of array.item[] using array.index[]
 * to find an exact match of the string *find.  If found, it returns the item
 * number (array.item[item_number]) of the match.  Otherwise, it returns a
 * negative number.  The negative number, if made positive again, and then
 * having 1 subtracted from it, will be the item_number where the string *find
 * should be inserted into the array.item[].  The function_setitem() makes use
 * of this attribute.
 */
/*
 * NOTE:  The new item argument is advisory, and only matters for callers
 * that plan to use the return value for updating the index array, such as
 * set_item and del_item, and is an indicator of where the caller intends
 * to insert the given string in the array.  The intent is to cause the
 * indices to be sorted by name then item.
 */
#define FINDIT(fn, test, pre)                                            \
static long	(fn) (an_array *array, char *find, long item)            \
{                                                                        \
	long top, bottom, key, cmp;                                      \
	int len = (pre);                                                 \
	(void)len;	/* Eliminate a specious warning from gcc. */	 \
                                                                         \
	top = array->size - 1;                                           \
	bottom = 0;                                                      \
                                                                         \
	SORT_INDICES(array);                                             \
                                                                         \
	while (top >= bottom)                                            \
	{                                                                \
		key = (top + bottom) / 2;                                \
		cmp = (test);                                            \
		if (cmp == 0)                                            \
			cmp = item < 0 ? cmp : item - array->index[key]; \
		if (cmp == 0)                                            \
			return key;                                      \
		if (cmp < 0)                                             \
			top = key - 1;                                   \
		else                                                     \
			bottom = key + 1;                                \
	}                                                                \
	return ~bottom;                                                  \
}
FINDIT(find_item, strcmp(find, array->item[array->index[key]]), 0)
FINDIT(find_items, strncmp(find, array->item[array->index[key]], len), strlen(find))
#undef FINDIT

/*
 * insert_index() takes a valid index (newIndex) and inserts it into the array
 * **index, then increments the *size of the index array.
 */
static void	insert_index (long **idx, long *size, long newIndex)
{
	long cnt;

	if (*size)
		RESIZE(*idx, long, *size + 1);
	else
	{
		*idx = (long *)new_malloc(sizeof(long));
		newIndex = 0;
	}
	
	for (cnt = *size; cnt > newIndex; cnt--)
		(*idx)[cnt] = (*idx)[cnt - 1];
	(*idx)[newIndex] = *size;
	(*size)++;
}

/*
 * move_index() moves the array.index[] up or down to make room for new entries
 * or to clean up so an entry can be deleted.
 */
static void		move_index (an_array *array, long oldindex, long newindex)
{
	long temp;

	if (newindex > oldindex)
		newindex--;
	if (newindex == oldindex)
		return;
	
	temp = array->index[oldindex];

	if (oldindex < newindex)
		for (; oldindex < newindex; oldindex++)
			array->index[oldindex] = array->index[oldindex + 1];
	else
		for(; oldindex > newindex; oldindex--)
			array->index[oldindex] = array->index[oldindex - 1];
	
	array->index[newindex] = temp;
}

/*
 * find_index() attempts to take an item and discover the index number
 * that refers to it.  find_index() assumes that find_item() WILL always return
 * a positive or zero result (be successful) because find_index() assumes that
 * the item is valid, and thus a find will be successful.  I don't know what
 * value ARRAY_THRESHOLD ought to be.  I figured someone smarter than I am
 * could figure it out and tell me or tell me to abandon the entire idea.
 */
static long		find_index (an_array *array, long item)
{
	long srch = 0;

	if (array->size >= ARRAY_THRESHOLD)
	{
		srch = find_item(array, array->item[item], item);
		qsort_array = array;
		while (srch >= 0 && !compare_indices(&array->index[srch], &item))
			srch--;
		srch++;
	}
	while(array->index[srch] != item && srch < array->size)
		srch++;

	if (srch < 0 || srch >= array->size)
		say("ERROR in find_index(): ! 0 <= %ld < %ld", srch, array->size);	
	return srch;
}

/*
 * get_array() searches and finds the array referenced by *name.  It returns
 * a pointer to the array, or a null pointer on failure to find it.
 */
an_array *	get_array (char *name)
{
	long idx;

	if (array_info.size && *name)
        {
                upper(name);
                if ((idx = find_item(&array_info, name, -1)) >= 0)
                        return &array_array[array_info.index[idx]];
	}
	return NULL;
}

/*
 * delete_array() deletes the contents of an entire array.  It assumes that
 * find_item(array_info, name) will succeed and return a valid zero or positive
 * value.
 */
static void		delete_array (char *name)
{
        char **ptr;
        long cnt;
        long idx;
        long item;
        an_array *array;

        idx = find_item(&array_info, name, -1);
        item = array_info.index[idx];
        array = &array_array[item];
        for (ptr=array->item, cnt=0; cnt < array->size; cnt++, ptr++)
                new_free((char **)ptr);
        new_free((char **)&array->item);
        new_free((char **)&array->index);
        new_free((char **)&array_info.item[item]);

        if (array_info.size > 1)
        {
                for(cnt = 0; cnt < array_info.size; cnt++)
                        if (array_info.index[cnt] > item)
                                (array_info.index[cnt])--;

                move_index(&array_info, idx, array_info.size);
                array_info.size--;
                for(ptr=&array_info.item[item], cnt=item; cnt < array_info.size; cnt++, ptr++, array++)
                {
                        *ptr = *(ptr + 1);
                        *array = *(array + 1);
                }
				RESIZE(array_info.item, char *, array_info.size);
				RESIZE(array_info.index, long, array_info.size);
				RESIZE(array_array, an_array, array_info.size);
        }
        else
        {
                new_free((char **)&array_info.item);
                new_free((char **)&array_info.index);
                new_free((char **)&array_array);
                array_info.size = 0;
        }
}

/*
 * This was once the inner loop of SETITEM.
 * The documentation for it still applies.
 */
int set_item (char* name, long item, char* input, int unsorted)
{
	long idx = 0;
	long oldindex;
	an_array *array;
	int result = -1;
	if (array_info.size && ((idx = find_item(&array_info, name, -1)) >= 0))
	{
		array =  &array_array[array_info.index[idx]];
		result = -2;
		if (item < array->size)
		{
			if (unsorted || array->unsorted) {
				array->unsorted = 1;
			} else {
				oldindex = find_index(array, item);
				idx = find_item(array, input, item);
				idx = (idx >= 0) ? idx : (-idx) - 1;
				move_index(array, oldindex, idx);
			}
			malloc_strcpy(&array->item[item], input);
			result = 0;
		}
		else if (item == array->size)
		{
			RESIZE(array->item, char *, array->size + 1);
			array->item[item] = NULL;
			malloc_strcpy(&array->item[item], input);
			if (unsorted || array->unsorted) {
				array->unsorted = 1;
				idx = item;
			} else {
				idx = find_item(array, input, item);
				idx = (idx >= 0) ? idx : (-idx) - 1;
			}
			insert_index(&array->index, &array->size, idx);
			result = 2;
		}
	}
	else
	{
		if (item == 0)
		{
			RESIZE(array_array, an_array, array_info.size + 1);
			array = &array_array[array_info.size];
			array->size = 1;
			array->item = (char **)new_malloc(sizeof(char *));
			array->index = (long *)new_malloc(sizeof(long));
			array->item[0] = NULL;
			array->index[0] = 0;
			array->unsorted = 1;
			malloc_strcpy(&array->item[0], input);
			RESIZE(array_info.item, char *, array_info.size + 1);
			array_info.item[array_info.size] = NULL;
			malloc_strcpy(&array_info.item[array_info.size], name);
			insert_index(&array_info.index, &array_info.size, (-idx) - 1);
			result = 1;
		}
	}
	return result;
}

/*
 * Now for the actual alias functions
 * ==================================
 */

/*
 * function_matchitem() attempts to match a pattern to the contents of an array
 * RETURNS -1 if it cannot find the array, or -2 if no matches occur
 */
#define MATCHITEM(fn, wm1, wm2, ret)                                          \
BUILT_IN_FUNCTION((fn), input)                                                \
{                                                                             \
	char	*name;                                                        \
	long	idx;                                                        \
	an_array *array;                                                      \
	long	current_match;                                                \
	long	best_match = 0;                                               \
	long	match = -1;                                                   \
                                                                              \
	if ((name = next_arg(input, &input)) && (array = get_array(name)))    \
	{                                                                     \
		match = -2;                                                   \
		for (idx = 0; idx < array->size; idx++)                 \
		{                                                             \
			if ((current_match = wild_match((wm1), (wm2))) > best_match) \
			{                                                     \
				match = idx;                                \
				best_match = current_match;                   \
			}                                                     \
		}                                                             \
		do ret while (0);					      \
	}                                                                     \
                                                                              \
	RETURN_INT(match);                                                    \
}
MATCHITEM(function_matchitem, input, array->item[idx], {})
MATCHITEM(function_rmatchitem, array->item[idx], input, {})
MATCHITEM(function_gettmatch, input, array->item[idx], {if (match >= 0) RETURN_STR(array->item[match]);})
#undef MATCHITEM

/*
 * function_getmatches() attempts to match a pattern to the contents of an
 * array and returns a list of item_numbers of all items that match the pattern
 * or it returns an empty string if not items matches or if the array was not
 * found.
 */
#define GET_MATCHES(fn, wm1, wm2, pre)                                       \
BUILT_IN_FUNCTION((fn), input)                                               \
{                                                                            \
	char    *result = NULL;                                        \
	size_t	resclue = 0;                                                 \
	char    *name = NULL;                                          \
	long    idx;                                                       \
	an_array *array;                                                     \
                                                                             \
	if ((name = next_arg(input, &input)) &&                              \
	    (array = get_array(name)) && input)                              \
	{                                                                    \
	    do pre while (0);                                                \
	    for (idx = 0; idx < array->size; idx++)                    \
		if (wild_match((wm1), (wm2)) > 0)                            \
		    malloc_strcat_wordlist_c(&result, space, ltoa(idx), &resclue);       \
	}                                                                    \
                                                                             \
	RETURN_MSTR(result);                                                 \
}
GET_MATCHES(function_getmatches, input, array->item[idx], {})
GET_MATCHES(function_getrmatches, array->item[idx], input, {})
GET_MATCHES(function_igetmatches, input, array->item[array->index[idx]], SORT_INDICES(array))
GET_MATCHES(function_igetrmatches, array->item[array->index[idx]], input, SORT_INDICES(array))
#undef GET_MATCHES


/*
 * function_numitems() returns the number of items in an array, or -1 if unable
 * to find the array
 */
BUILT_IN_FUNCTION(function_numitems, input)
{
        char *name = NULL;
	an_array *array;
	long items = 0;

        if ((name = next_arg(input, &input)) && (array = get_array(name)))
                items = array->size;

	RETURN_INT(items);
}

/*
 * function_getitem() returns the value of the specified item of an array, or
 * returns an empty string on failure to find the item or array
 */
#define GETITEM(fn, ret, pre)                                                \
BUILT_IN_FUNCTION((fn), input)                                               \
{                                                                            \
	char *name = NULL;                                             \
	char *itemstr = NULL;                                          \
	long item;                                                           \
	an_array *array;                                                     \
	char *retval = NULL;                                           \
	size_t rvclue = 0;                                                   \
                                                                             \
	if ((name = next_arg(input, &input)) && (array = get_array(name)))   \
	{                                                                    \
		do pre while (0);                                            \
		while ((itemstr = next_arg(input, &input)))                  \
		{                                                            \
			item = my_atol(itemstr);                             \
			if (item >= 0 && item < array->size)                 \
				malloc_strcat_wordlist_c(&retval, space, (ret), &rvclue);  \
		}                                                            \
	}                                                                    \
	RETURN_MSTR(retval);                                                 \
}
GETITEM(function_getitem, array->item[item], {})
GETITEM(function_igetitem, array->item[array->index[item]], SORT_INDICES(array))
#undef GETITEM

/*
 * function_setitem() sets an item of an array to a value, or creates a new
 * array if the array doesn not already exist and the item number is zero, or
 * it adds a new item to an existing array if the item number is one more than
 * the prevously largest item number in the array.
 * RETURNS: 0 on success
 *          1 on success if a new item was added to an existing array
 *          2 on success if a new array was created and item zero was set
 *         -1 if it is unable to find the array (and item number was not zero)
 *         -2 if it was unable to find the item (item < 0 or item was greater
 *            than 1 + the prevous maximum item number
 */
#define FUNCTION_SETITEM(fn, unsorted)                                     \
BUILT_IN_FUNCTION((fn), input)                                             \
{                                                                          \
	char *name = NULL;                                           \
	char *itemstr = NULL;                                        \
	long item;                                                         \
	int result = -1;                                                   \
                                                                           \
	if ((name = next_arg(input, &input)))                              \
	{                                                                  \
	    if (strlen(name) && (itemstr = next_arg(input, &input)))       \
	    {                                                              \
		item = my_atol(itemstr);                                   \
		if (item >= 0)                                             \
		{                                                          \
		    upper(name);                                           \
		    result = set_item(name, item, input, (unsorted));      \
		}                                                          \
	    }                                                              \
	}                                                                  \
	RETURN_INT(result);                                                \
}
FUNCTION_SETITEM(function_setitem, 0)
FUNCTION_SETITEM(function_usetitem, 1)
#undef FUNCTION_SETITEM

/*
 * function_getarrays() returns a string containg the names of all currently
 * existing arrays separated by spaces
 */
BUILT_IN_FUNCTION(function_getarrays, input)
{
	long idx;
	char *result = NULL;
	size_t	resclue = 0;

	for (idx = 0; idx < array_info.size; idx++)
		if (!input || !*input || wild_match(input, array_info.item[array_info.index[idx]]))
			malloc_strcat_wordlist_c(&result, space, array_info.item[array_info.index[idx]], &resclue);

	if (!result)
		RETURN_EMPTY;

	return result;
}

/*
 * function_numarrays() returns the number of currently existing arrays
 */
BUILT_IN_FUNCTION(function_numarrays, input)
{
	RETURN_INT(array_info.size);
}

/*
 * function_finditem() does a binary search and returns the item number of
 * the string that exactly matches the string searched for, or it returns
 * -1 if unable to find the array, or -2 if unable to find the item.
 *
 * function_ifinditem() does a binary search and returns the index number of
 * the string that exactly matches the string searched for, or it returns
 * -1 if unable to find the array, or -2 if unable to find the item.
 */
#define FINDI(func, search, trans1, trans2)                                 \
BUILT_IN_FUNCTION((func), input)                                            \
{                                                                           \
        char    *name = NULL;                                         \
        an_array *array;                                                    \
	long	item = -1;                                                  \
                                                                            \
	if ((name = next_arg(input, &input)) && (array = get_array(name)))  \
        {                                                                   \
		if (input)                                                  \
		{                                                           \
			item = (search)(array, input, -1);                  \
			item = (item >= 0) ? (trans1) : (trans2);           \
		}                                                           \
        }                                                                   \
	RETURN_INT(item);                                                   \
}
FINDI(function_finditem, find_item, array->index[item], -2)
FINDI(function_ifinditem, find_item, item, -2)
FINDI(function_finditems, find_items, array->index[item], ~(array->index[~item]))
FINDI(function_ifinditems, find_items, item, item)
#undef FINDI

/*
 * function_indextoitem() converts an index number to an item number for the
 * specified array.  It returns a valid item number, or -1 if unable to find
 * the array, or -2 if the index was invalid.
 */
#define I2I(fn, op)                                                        \
BUILT_IN_FUNCTION((fn), input)                                             \
{                                                                          \
	char *name = NULL;                                           \
	char *itemstr = NULL;                                        \
	long item;                                                         \
	an_array *array;                                                   \
	long found = -1;                                                   \
	char *ret = NULL;                                            \
	size_t clue = 0;                                                   \
                                                                           \
	if ((name = next_arg(input, &input)) && (array = get_array(name))) \
	{                                                                  \
		SORT_INDICES(array);                                       \
		found = -2;                                                \
		while ((itemstr = next_arg(input, &input)))                \
		{                                                          \
			long idx = -2;                                   \
			item = my_atol(itemstr);                           \
			if (item >= 0 && item < array->size)               \
				idx = (op);                              \
			malloc_strcat_wordlist_c(&ret, space, ltoa(idx), &clue);       \
		}                                                          \
	}                                                                  \
	if (ret)                                                           \
		RETURN_MSTR(ret);                                          \
	else                                                               \
		RETURN_INT(found);                                         \
}
I2I(function_indextoitem, array->index[item])
I2I(function_itemtoindex, find_index(array, item))
#undef I2I

/*
 * function_delitem() deletes an item of an array and moves the contents of the
 * array that were stored "above" the item down by one.  It returns 0 (zero)
 * on success, -1 if unable to find the array, -2 if unable to find the item.
 * Also, if the item is the last item in the array, it deletes the array.
 */
/*
 * It seems to me that if this function were to accept multiple items for
 * arguments, we could erase them all at once and have a much smaller
 * best case performance problem. XXX
 */
BUILT_IN_FUNCTION(function_delitem, input)
{
	char *name;
	char *itemstr;
	char **strptr;
	long item;
	long cnt;
	long oldindex;
	long more;
	an_array *array;
	long found = -1;

	if ((name = next_arg(input, &input)) && (array = get_array(name)))
	{
		found = -2;
		while ((itemstr = next_arg(input, &input)))
		{
			item = my_atol(itemstr);
			if (item >= 0 && item < array->size)
			{
				found = 0;
				if (array->size == 1)
				{
					delete_array(name);
					break;
				}
				else
				{
					if (item == array->index[item])
						oldindex = item;
					else
						oldindex = find_index(array, item);
					more = array->size - item;
					for (cnt = array->size; --cnt >= 0 && more;)
						if (array->index[cnt] >= item)
							more--,
							(array->index[cnt])--;
					move_index(array, oldindex, array->size);
					new_free(&array->item[item]);
					array->size--;
					for (strptr=&(array->item[item]), cnt=item; 
							cnt < array->size; 
							cnt++, strptr++)
						*strptr = *(strptr + 1);
					RESIZE(array->item, char *, array->size);
					RESIZE(array->index, long, array->size);
				}
			}
		}
	}
	RETURN_INT(found);
}

/*
 * $delitems() works like $delitem() except that it throws the array into
 * unsorted mode and performs better for larger numbers of items.
 */
BUILT_IN_FUNCTION(function_delitems, input)
{
	char *name;
	char *itemstr;
	long item;
	long cnt;
	long new = 0;
	an_array *array;
	long found = -1;
	int deleted = 0;

	if ((name = next_arg(input, &input)) && (array = get_array(name)))
	{
		found = -2;
		while ((itemstr = next_arg(input, &input)))
		{
			item = my_atol(itemstr);
			if (item >= 0 && item < array->size && array->item[item])
			{
				found = 0;
				deleted++;
				if (deleted >= array->size)
				{
					deleted = 0;
					delete_array(name);
					break;
				}
				new_free(&array->item[item]);
			}
		}
		if (deleted)
		{
			for (cnt = 0; cnt < array->size; cnt++)
				if (array->item[cnt])
					array->item[new++] = array->item[cnt];
			array->unsorted = 1;
			array->size -= deleted;
			for (cnt = 0; cnt < array->size; cnt++)
				(array->index[cnt]) = cnt;
			RESIZE(array->item, char *, array->size);
			RESIZE(array->index, long, array->size);
		}
	}
	RETURN_INT(found);
}

/*
 * function_delarray() deletes the entire contents of the array using the
 * delete_array() function above.  It returns 0 on success, -1 on failure.
 */
BUILT_IN_FUNCTION(function_delarray, input)
{
        char *name;
        long found = -1;

	if ((name = next_arg(input, &input)) && (get_array(name)))
        {
                delete_array(name);
		found = 0;
	}
	RETURN_INT(found);
}
/*
 * function_ifindfirst() returns the first index of an exact match with the
 * search string, or returns -2 if unable to find the array, or -1 if unable
 * to find any matches.
 */
BUILT_IN_FUNCTION(function_ifindfirst, input)
{
        char    *name;
        an_array *array;
        long    item = -1;

        if ((name = next_arg(input, &input)) && (array = get_array(name)))
        {
		if (*input)
		{
			if ((item = find_item(array, input, -1)) < 0)
				item = -2;
			else
			{
				while (item >= 0 && !strcmp(array->item[array->index[item]], input))
					item--;
				item++;
			}
		}
        }
	RETURN_INT(item);
}

/*
 * Contributed by Colten Edwards (panasync).
 * March 21, 1998
 */
BUILT_IN_FUNCTION(function_listarray, input)
{
	char	*name;
	an_array *array;
	long	idx;
	char	*result = NULL;
	size_t	resclue = 0;

	if ((name = next_arg(input, &input)) && (array = get_array(name)))
	{
		const char *separator = (input && *input) ? new_next_arg(input, &input) : space;

		for (idx = 0; idx < array->size; idx++)
			malloc_strcat_wordlist_c(&result, separator, array->item[idx], &resclue);
	}
	return result ? result : malloc_strdup(empty_string);
}


/*
<shade> gettmatch(users % user@host *) would match the userhost mask in the
          second word of the array
 */
#if 0
BUILT_IN_FUNCTION(function_gettmatch, input)
{
	char 	*name;
	an_array *array;
	char 	*ret = NULL;

        if ((name = next_arg(input, &input)) && (array = get_array(name)))
        {
		if (*input)
          	{
			int idx, current_match;
			int best_match = 0;
			int match = -1;
                        for (idx = 0; idx < array->size; idx++)
                        {
                                if ((current_match = wild_match(input, array->item[idx])) > best_match)
                                {
                                        match = idx;
                                        best_match = current_match;
                                }
                        }
			if (match != -1)
				ret = array->item[match];

		}
	}
	RETURN_STR(ret);
}
#endif
