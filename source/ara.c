/*
 * ara.c -- a resizable array
 *
 * Copyright 2022 EPIC Software Labs
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. All redistributions, whether of source code or in binary form must
 *    retain the the above copyright notice, the above paragraph (the one
 *    permitting redistribution), this list of conditions, and the following
 *    disclaimer.
 * 2. The names of the author(s) may not be used to endorse or promote 
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
#include "irc.h"
#include "ara.h"
#include "ircaux.h"
#include "output.h"

static	void	ara_insert (ara *a, int location);
static	void	ara_delete (ara *a, int location);
static	void	ara_enlarge_list (ara *a, size_t new_size);

/*
 * Make 'location' available, by moving (location, end) to (location+1, end+1)
 * Does not change (0, location - 1)
 * Changes (location) to NULL
 *
 * If 'a' is "full" it is enlarged by 10 spots first.
 * If 'location' is invalid 
 */
static	void	ara_insert (ara *a, int location)
{
	size_t	i;

	if (location < 0 || (size_t)location >= a->size)
		return;
	else if (a->size == 0 || a->list == NULL)
		ara_enlarge_list(a, a->size + 10);
	else if (a->type == ARA_TYPE_DATA && a->list[a->size - 1].d != NULL)
		ara_enlarge_list(a, a->size + 10);
	else if (a->list[a->size - 1].f != NULL)
		ara_enlarge_list(a, a->size + 10);

	for (i = a->size; i > (size_t)location; i--)
		a->list[i] = a->list[i - 1];

	i = location;
	if (a->type == ARA_TYPE_DATA)
		a->list[i].d = NULL;
	else
		a->list[i].f = NULL;
}

/*
 * Fill in a gap at 'location' by moving (location + 1, end) to (location, end-1)
 * Does not change (0, location - 1)
 * Obviously it overwites (location)
 */
static	void	ara_delete (ara *a, int location)
{
	size_t 	i;

	if (location < 0 || (size_t)location >= a->size)
		return;
	if (a->size == 0 || a->list == NULL)
		return;
}

static	void	ara_enlarge_list (ara *a, size_t new_size)
{
	size_t	r;
	AraItem *new_list;
	AraItem *old_list;

	new_list = (AraItem *)new_malloc(sizeof(AraItem) * new_size);
	if (a->type == ARA_TYPE_DATA)
	{
		for (r = 0; r < a->size; r++)
			new_list[r].d = a->list[r].d;
		for (r = a->size; r < new_size; r++)
			new_list[r].d = NULL;
	}
	else
	{
		for (r = 0; r < a->size; r++)
			new_list[r].f = a->list[r].f;
		for (r = a->size; r < new_size; r++)
			new_list[r].f = NULL;
	}

	old_list = a->list;
	a->list = new_list;
	a->size = new_size;

	if (old_list)
		new_free((char **)&old_list);
}

/*
 * Copy 'one' to 'two'.  
 */
static	void	ara_copy (ara *one, ara *two)
{
	size_t	i;

	if (two->size < one->size)
		ara_enlarge_list(two, one->size + 10);

	for (i = 0; i < one->size; i++)
		two[i] = one[i];
}

void	init_ara (ara *a, int type, size_t maximum_location)
{
	a->list = NULL;
	a->size = 0;
	a->type = type;

	ara_enlarge_list(a, maximum_location + 1);
}

/*
 * Set the 'location'th element in 'a' to 'data'
 * If errcode is not NULL:
 *	 1 - The value was successfully set
 *	 0 - The value was not successfully set because there was already a value there
 *	-1 - 'a' was NULL or the value of 'location' was invalid (not successful)
 */
void	ara_set_item (ara *a, int location, AraItem data, int *errorcode)
{
	size_t	idx;

	if (!a || location < 0)
	{
		*errorcode = -1;
		return;
	}

	idx = (size_t)location;
	if (idx >= a->size)
		ara_enlarge_list(a, location + 10);

	if (a->type == ARA_TYPE_DATA)
	{
		if (a->list[idx].d != NULL)
		{
			*errorcode = 0;
		}
		else
		{
			a->list[idx].d = data.d;
			*errorcode = 1;
		}
	}
	else
	{
		if (a->list[idx].f != NULL)
		{
			*errorcode = 0;
		}
		else
		{
			a->list[idx].f = data.f;
			*errorcode = 1;
		}
	}
}

/*
 * Return the 'location'th element in 'a'
 * If errcode is not NULL:
 *	 1 - The value 'location' is valid and contains a value (returned) 
 *	 0 - The value 'location' is valid but it had no value (returned NULL)
 *	-1 - 'a' was NULL or the value of 'location' was invalid (returned NULL)
 */
AraItem	ara_get_item (ara *a, int location, int *errorcode)
{
	size_t	idx;
	AraItem nothing;

	if (!a || location < 0 || (size_t)location >= a->size)
	{
		*errorcode = -1;
		if (!a || a->type == ARA_TYPE_DATA)
			nothing.d = NULL;
		else
			nothing.f = NULL;
		return nothing;
	}

	idx = (size_t)location;
	if (a->type == ARA_TYPE_DATA)
	{
		if (a->list[idx].d == NULL)
			*errorcode = 0;
		else
			*errorcode = 1;
	}
	else
	{
		if (a->list[idx].f == NULL)
			*errorcode = 0;
		else
			*errorcode = 1;
	}
	return a->list[idx];
}

/*
 * Set the 'location'th element in 'a' to 'data' and return the previous value
 * If errcode is not NULL:
 *	 1 - The value was successfully updated (previous value returned)
 *	 0 - <Reserved>
 *	-1 - 'a' was NULL or the value of 'location' was invalid
 */
AraItem	ara_update_item (ara *a, int location, AraItem data, int *errcode)
{
	size_t	idx;
	AraItem retval;

	if (!a || location < 0 || (size_t)location >= a->size)
	{
		*errcode = -1;
		if (!a || a->type == ARA_TYPE_DATA)
			retval.d = NULL;
		else
			retval.f = NULL;
		return retval;
	}

	idx = (size_t)location;
	if (a->type == ARA_TYPE_DATA)
	{
		retval.d = a->list[idx].d;
		a->list[idx].d = data.d;
	}
	else
	{
		retval.f = a->list[idx].f;
		a->list[idx].f = data.f;
	}
	*errcode = 1;
	return retval;
}

/*
 * Clear the 'location'th element in 'a' and return the previous value
 * If errcode is not NULL:
 *	 1 - The value was successfully removed (previous value returned)
 *	 0 - The value was not cleared because there was no previous value (returned NULL)
 *	-1 - 'a' was NULL or the value of 'location' was invalid
 */
AraItem	ara_remove_item (ara *a, int location, int *errcode)
{
	size_t	idx;
	AraItem retval;

	retval = ara_get_item(a, location, errcode);
	if (*errcode <= 0)
	{
		if (a->type == ARA_TYPE_DATA)
			retval.d = NULL;
		else
			retval.f = NULL;
	}
	else
	{
		if (a->type == ARA_TYPE_DATA)
			a->list[location].d = NULL;
		else
			a->list[location].f = NULL;

		*errcode = 1;
	}
	return retval;
}


/*
 * Return the next element in 'ara' after 'location'
 * 	If *location == -1	-> begin at ara location 0 (to start iterating)
 * 	Otherwise		-> begin at *location + 1
 * Returns:
 *	-1 - 'a' was NULL or 'location' was NULL
 *	 0 - There were no further values (*location is not changed - do not use)
 *	 1 - The value in "*location" was set -- keep calling!
 */
int	traverse_all_ara_items (ara *a, int *location)
{
	size_t	idx;

	if (!a || a->size <= 0 || !a->list || (*location) < -1)
		return -1;

	for (;;)
	{
		if ((size_t)*location >= a->size - 1)
			return 0;
		(*location)++;

		if (a->type == ARA_TYPE_DATA)
		{
			if (a->list[*location].d != NULL)
				return 1;
		}
		else
		{
			if (a->list[*location].f != NULL)
				return 1;
		}
	}
}

/*
 * Return the maximum valid value for 'location' in 'a'
 * Returns:
 *	 -1 - 'a' is NULL, or there are no valid locations in 'a'
 *     >= 0 - The maximum value you may pass as 'location'
 *	      (Note: This is not the number of items in the
 *	       ara!  If there is only one item, then its 
 *	       location is '0'!)
 */
int	ara_max (ara *a)
{
	size_t	idx;

	if (!a || a->size == 0 || a->list == NULL)
		return -1;

	for (idx = a->size - 1; ; idx--)
	{
		if (a->type == ARA_TYPE_DATA && a->list[idx].d != NULL)
			return idx;
		else if (a->type == ARA_TYPE_FUNC && a->list[idx].f != NULL)
			return idx;

		if (idx == 0)
			return -1;
	}

	return -1;
}

/*
 * Return the number of valid values in 'a'
 * Returns:
 *	-1 - 'a' is NULL
 *	>0 - The number of non-null 'locations' in 'a' that have been set
 */
int	ara_number (ara *a)
{
	size_t	idx;
	size_t	count = 0;

	if (!a)
		return -1;

	for (idx = count = 0; idx < a->size; idx++)
	{
		if (a->type == ARA_TYPE_DATA && a->list[idx].d != NULL)
			count++;
		else if (a->type == ARA_TYPE_FUNC && a->list[idx].f != NULL)
			count++;
	}
	return count;
}

/* Return the location of 'item' in ara, or -1 if not found */
int	ara_find_item (ara *a, AraItem item)
{
	int	retval = -1;
	size_t	idx;

	if (!a)
		return -1;

	for (idx = 0; idx < a->size; idx++)
	{
		if (a->type == ARA_TYPE_DATA && a->list[idx].d == item.d)
			return (int)idx;
		else if (a->type == ARA_TYPE_FUNC && a->list[idx].f == item.f)
			return (int)idx;
	}
	return -1;
}

/* Add 'new_item' to location 0 in ara; push everything else up one */
int	ara_unshift (ara *a, AraItem new_item)
{
	int	errorcode = 0;

	ara_insert(a, 0);
	ara_set_item(a, 0, new_item, &errorcode);
	return errorcode;
}

/* Remove+return location 0 and shift everything down one */
AraItem	ara_shift (ara *a)
{
	AraItem	retval;
	int	errcode = 0;

	retval = ara_get_item(a, 0, &errcode);
	ara_delete(a, 0);
	return retval;
}

/* Add 'new_item' to the end of ara */
int	ara_push (ara *a, AraItem new_item)
{
	int	location;
	int	errcode = 0;

	if ((location = ara_max(a)) < 0)
	{
		ara_enlarge_list(a, 10);
		ara_set_item(a, 0, new_item, &errcode);
	}
	else
		ara_set_item(a, location, new_item, &errcode);

	return errcode;
}

/* Remove+return the last item in the ara */
AraItem	ara_pop (ara *a)
{
	int	location;
	AraItem	retval;

	if ((location = ara_max(a)) < 0)
	{
		if (a->type == ARA_TYPE_DATA)
			retval.d = NULL;
		else
			retval.f = NULL;
		return retval;
	}
	else
		return ara_pop_item(a, location);
}

/* Remove+return the 'location'th item in the ara */
AraItem	ara_pop_item (ara *a, int location)
{
	size_t	idx;
	AraItem retval;
	int	errcode = 0;

	retval = ara_get_item(a, location, &errcode);
	ara_delete(a, location);
	return retval;
}

/* Insert 'new_item' to 'location'th spot, move everything after location down one spot */
int	ara_insert_item (ara *a, int location, AraItem new_item)
{
	int	errcode = 0;

	ara_insert(a, location);
	ara_set_item(a, location, new_item, &errcode);
	return errcode;
}

/* Insert 'new_item' before 'existing_item', moving everything down one spot */
int	ara_insert_before_item (ara *a, AraItem existing_item, AraItem new_item)
{
	int	errcode = 0;
	int	location;

	if ((location = ara_find_item(a, existing_item)) < 0)
		return -1;

	ara_insert(a, location);
	ara_set_item(a, location, new_item, &errcode);
	return errcode;
}

/* Insert 'new_item' after 'existing_item', moving everything down one spot */
int	ara_insert_after_item (ara *a, AraItem existing_item, AraItem new_item)
{
	int	errcode = 0;
	int	location;

	if ((location = ara_find_item(a, existing_item)) < 0)
		return -1;

	ara_insert(a, location + 1);
	ara_set_item(a, location + 1, new_item, &errcode);
	return errcode;
}

/* Find any gaps in the ara and fill them in */
int	ara_collapse (ara *a)
{
	size_t	idx;

	if (!a)
		return -1;

	for (idx = 0; idx < a->size; idx++)
	{
		if (a->type == ARA_TYPE_DATA)
		{
			if (a->list[idx].d == NULL)
				ara_delete(a, idx);
		}
		else
		{
			if (a->list[idx].f == NULL)
				ara_delete(a, idx);
		}
	}
	return 1;
}

