/*
 * words.c -- right now it just holds the stuff i wrote to replace
 * that beastie arg_number().  Eventually, i may move all of the
 * word functions out of ircaux and into here.  Now wouldnt that
 * be a beastie of a patch! Beastie! Beastie!
 *
 * Written by Jeremy Nelson
 * Copyright 1994 Jeremy Nelson
 * See the COPYRIGHT file for more information
 */

#include "irc.h"
#include "ircaux.h"

/*
 * search() looks for a character forward or backward from mark 
 */
char	*search (char *start, char **mark, char *chars, int how)
{
        if (!mark || !*mark)
                *mark = start;

        if (how > 0)   /* forward search */
        {
		*mark = sindex(*mark, chars);
		how--;
		for (;(how > 0) && *mark && **mark;how--)
			*mark = sindex(*mark+1, chars);
	}

	else if (how == 0)
		return NULL;

	else  /* how < 0 */
		*mark = rsindex(*mark, start, chars, -how);

	return *mark;
}

#define CHECK_EXTENDED_SUPPORT					\
	if (extended && ((x_debug & DEBUG_EXTRACTW) == 0))	\
		extended = 0;

/* 
 * Move to an absolute word number from start
 * First word is always numbered zero.
 */
const char *	real_move_to_abs_word (const char *start, const char **mark, int word, int extended)
{
	const char *	pointer = start;
	int 		counter = word;

	CHECK_EXTENDED_SUPPORT

	/* 
	 * This fixes a bug that counted leading spaces as
	 * a word, when theyre really not a word.... 
	 * (found by Genesis K.)
	 */
	while (*pointer && my_isspace(*pointer))
		pointer++;

	for (;counter > 0 && *pointer;counter--)
	{
		if (extended && *pointer == '"')
		{
			const char *	after;

			if (!(after = find_forward_quote(pointer, start)))
				panic("find_forward returned NULL [1]");
			if (*after)
				after++;
			pointer = after;
		}
		else
			while (*pointer && !my_isspace(*pointer))
				pointer++;

		while (*pointer && my_isspace(*pointer))
			pointer++;
	}

	if (mark)
		*mark = pointer;
	return pointer;
}

/* 
 * Move a relative number of words from the present mark 
 */
static const char *	move_word_rel (const char *start, const char **mark, int word, int extended)
{
	int 		counter = word;
	const char *	pointer = *mark;
	const char *	end = start + strlen(start);

	if (end == start) 	/* null string, return it */
		return start;

	CHECK_EXTENDED_SUPPORT
	if (counter > 0)
	{
	    for (;counter > 0 && pointer;counter--)
	    {
		if (extended && *pointer == '"')
		{
			const char *	after;

			if (!(after = find_forward_quote(pointer, start)))
				panic("find_forward returned NULL [2]");
			if (*after)
				after++;
			pointer = after;
		}
		else
			while (*pointer && !my_isspace(*pointer))
				pointer++;

		while (*pointer && my_isspace(*pointer)) 
			pointer++;
	    }
	}
	else if (counter == 0)
		pointer = *mark;
	else /* counter < 0 */
	{
	    for (; counter < 0 && pointer > start; counter++)
	    {
		if (extended && *pointer == '"')
		{
			const char *	before;

			if (!(before = find_backward_quote(pointer, start)))
				panic("find_backward returned NULL [2]");
			if (before > start)
				before--;
			pointer = before;
		}
		else
		    while (pointer >= start && !my_isspace(*pointer))
			pointer--;

		while (pointer > start && my_isspace(*pointer)) 
		    pointer--;
	    }

	    pointer++; /* bump up to the word we just passed */
	}

	if (mark)
		*mark = pointer;
	return pointer;
}

/*
 * extract2 is the word extractor that is used when its important to us
 * that 'firstword' get special treatment if it is negative (specifically,
 * that it refer to the "firstword"th word from the END).  This is used
 * basically by the ${n}{-m} expandos and by function_rightw(). 
 *
 * Note that because of a lot of flak, if you do an expando that is
 * a "range" of words, unless you #define STRIP_EXTRANEOUS_SPACES,
 * the "n"th word will be backed up to the first character after the
 * first space after the "n-1"th word.  That apparantly is what everyone
 * wants, so thats whatll be the default.  Those of us who may not like
 * that behavior or are at ambivelent can just #define it.
 */
char *	real_extract2 (const char *start, int firstword, int lastword, int extended)
{
	/* 
	 * If firstword or lastword is negative, then
	 * we take those values from the end of the string
	 */
	const char *	mark;
	const char *	mark2;
	char *		booya = NULL;

	CHECK_EXTENDED_SUPPORT
	/* If firstword is EOS, then the user wants the last word */
	if (firstword == EOS)
	{
		mark = start + strlen(start);
		mark = move_word_rel(start, &mark, -1, extended);
#ifndef NO_CHEATING
		/* 
		 * Really. the only case where firstword == EOS is
		 * when the user wants $~, in which case we really
		 * dont need to do all the following crud.  Of
		 * course, if there ever comes a time that the
		 * user would want to start from the EOS (when?)
		 * we couldnt make this assumption.
		 */
		return m_strdup(mark);
#endif
	}

	/*
	 * SOS is used when the user does $-n, all leading spaces
	 * are retained
	 */
	else if (firstword == SOS)
		mark = start;

	/* If the firstword is positive, move to that word */
	/* Special treatment for $X-, where X is out of range
	 * added by Colten Edwards, fixes the $1- bug.. */
	else if (firstword >= 0)
	{
		real_move_to_abs_word(start, &mark, firstword, extended);
		if (!*mark)
			return m_strdup(empty_string);
	}

	/* Otherwise, move to the firstwords from the end */
	else
	{
		mark = start + strlen(start);
		move_word_rel(start, &mark, firstword, extended);
	}

#ifndef STRIP_EXTRANEOUS_SPACES
	/* IF the user did something like this:
	 *	$n-  $n-m
	 * then include any leading spaces on the 'n'th word.
	 * this is the "old" behavior that we are attempting
	 * to emulate here.
	 */
#ifndef NO_CHEATING
	if (lastword == EOS || (lastword > firstword))
#else
	if (((lastword == EOS) && (firstword != EOS)) || (lastword > firstword))
#endif
	{
		while (mark > start && my_isspace(mark[-1]))
			mark--;
		if (mark > start)
			mark++;
	}
#endif

	/* 
	 * When we find the last word, we need to move to the 
         * END of the word, so that word 3 to 3, would include
	 * all of word 3, so we sindex to the space after the word
	 */
	if (lastword == EOS)
		mark2 = mark + strlen(mark);

	else 
	{
		if (lastword >= 0)
			real_move_to_abs_word(start, &mark2, lastword+1, extended);
		else
		{
			mark2 = start + strlen(start);
			move_word_rel(start, &mark2, lastword, extended);
		}

		while (mark2 > start && my_isspace(mark2[-1]))
			mark2--;
	}

	/* 
	 * If the end is before the string, then there is nothing
	 * to extract (this is perfectly legal, btw)
         */
	if (mark2 < mark)
		booya = m_strdup(empty_string);

	else
	{
		/*
		 * This is kind of tricky, because the string we are
		 * copying out of is const.  So we cant just null off
		 * the trailing character and m_strdup it.
		 */
		booya = strext(mark, mark2);
	}

	return booya;
}

/*
 * extract is a simpler version of extract2, it is used when we dont
 * want special treatment of "firstword" if it is negative.  This is
 * typically used by the word/list functions, which also dont care if
 * we strip out or leave in any whitespace, we just do what is the
 * fastest.
 */
char *	real_extract (char *start, int firstword, int lastword, int extended)
{
	/* 
	 * firstword and lastword must be zero.  If they are not,
	 * then they are assumed to be invalid  However, please note
	 * that taking word set (-1,3) is valid and contains the
	 * words 0, 1, 2, 3.  But word set (-1, -1) is an empty_string.
	 */
	char *	mark;
	char *	mark2;
	char *	booya = NULL;

	CHECK_EXTENDED_SUPPORT
	/* 
	 * Before we do anything, we strip off leading and trailing
	 * spaces. 
	 *
	 * ITS OK TO TAKE OUT SPACES HERE, AS THE USER SHOULDNT EXPECT
	 * THAT THE WORD FUNCTIONS WOULD RETAIN ANY SPACES. (That is
	 * to say that since the word/list functions dont pay attention
	 * to the whitespace anyhow, noone should have any problem with
	 * those ops removing bothersome whitespace when needed.)
	 */
	while (my_isspace(*start))
		start++;
	remove_trailing_spaces(start, 0);

	if (firstword == EOS)
	{
		mark = start + strlen(start);
		mark = (char *)move_word_rel(start, (const char **)&mark, -1, extended);
	}

	/* If the firstword is positive, move to that word */
	else if (firstword >= 0)
		real_move_to_abs_word(start, (const char **)&mark, firstword, extended);

	/* Its negative.  Hold off right now. */
	else
		mark = start;


	/* 
	 * When we find the last word, we need to move to the 
         * END of the word, so that word 3 to 3, would include
	 * all of word 3, so we sindex to the space after the word
 	 */
	/* EOS is a #define meaning "end of string" */
	if (lastword == EOS)
		mark2 = start + strlen(start);
	else 
	{
		if (lastword >= 0)
			real_move_to_abs_word(start, (const char **)&mark2, lastword+1, extended);
		else
			/* its negative -- thats not valid */
			return m_strdup(empty_string);

		while (mark2 > start && my_isspace(mark2[-1]))
			mark2--;
	}

	/*
	 * Ok.. now if we get to here, then lastword is positive, so
	 * we sanity check firstword.
	 */
	if (firstword < 0)
		firstword = 0;
	if (firstword > lastword)	/* this works even if fw was < 0 */
		return m_strdup(empty_string);

	/* 
	 * If the end is before the string, then there is nothing
	 * to extract (this is perfectly legal, btw)
         */
	if (mark2 < mark)
		return m_strdup(empty_string);

	booya = strext(mark, mark2);
	return booya;
}
