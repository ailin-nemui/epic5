/* $EPIC: words.c,v 1.9 2003/07/10 23:56:01 jnelson Exp $ */
/*
 * words.c -- right now it just holds the stuff i wrote to replace
 * that beastie arg_number().  Eventually, i may move all of the
 * word functions out of ircaux and into here.  Now wouldnt that
 * be a beastie of a patch! Beastie! Beastie!
 *
 * Copyright © 1994 EPIC Software Labs.
 * All rights reserved.
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

#include "irc.h"
#include "ircaux.h"
#include "words.h"
#include "output.h"

/* 
 * If "extended" is 1, then honor /xdebug extractw.
 * If "extended" is other than 1 but not zero, imply /xdebug extractw.
 */
#define CHECK_EXTENDED_SUPPORT					\
	if (extended == DWORD_YES && 				\
			((x_debug & DEBUG_EXTRACTW) == 0))	\
		extended = DWORD_NEVER;				\
        else if (extended == DWORD_ALWAYS)			\
		extended = DWORD_YES;				\



#define risspace(c) (c == ' ')
 
/*
 * search() looks for a character forward or backward from mark 
 */
char *	search_for (char *start, char **mark, char *chars, int how)
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

/*
 * A "forward quote" is the first double quote we find that has a space
 * or the end of string after it; or the end of the string.
 */
static const char *	find_forward_character (const char *input, const char *start, const char *whats)
{
	int	simple;
	char	what;

	if (whats && whats[0] && whats[1] == 0)
	{
		simple = 1;
		what = whats[0];
	}
	else
	{
		simple = 0;
		what = 255;
	}

	/*
	 * An "extended word" is defined as:
	 *	<SPACE> <QUOTE> <ANY>* <QUOTE> <SPACE>
	 * <SPACE> := <WORD START> | <WORD END> | <" "> | <"\t"> | <"\n">
	 * <QUOTE> :- <'"'>	(from quotechar)
	 * <ANY>   := ANY ASCII CHAR 0 .. 256
	 */
	/* Make sure we are actually looking at a double-quote */
	if ( (simple == 1 && *input != what) ||
	     (simple == 0 && strchr(whats, *input) == NULL) )
		return NULL;

	/* 
	 * Make sure that the character before is the start of the
	 * string or that it is a space.
	 */
	if (input > start && !risspace(input[-1]))
		return NULL;

	/*
	 * Walk the string looking for a double quote.  Yes, we do 
	 * really have to check for \'s, still, because the following
	 * still is one word:
	 *			"one\" two"
	 * Once we find a double quote, then it must be followed by 
	 * either the end of string (chr 0) or a space.  If we find 
	 * that, return the position of the double-quote.
	 */
	for (input++; *input; input++)
	{
		if (input[0] == '\\' && input[1])
			input++;
		else if ( (simple == 1 && input[0] == what) ||
			  (simple == 0 && strchr(whats, *input)) )
		{
			if (input[1] == 0 || risspace(input[1]))
				return input;
		}
	}

	/*
	 * If we get all the way to the end of the string w/o finding 
	 * a matching double-quote, return the position of the (chr 0), 
	 * which is a special meaning to the caller. 
	 */
	return input;		/* Bumped the end of string. doh! */
}

/*
 * A "backward quote" is the first double quote we find, going backward,
 * that has a space or the start of string after it; or the start of 
 * the string.
 */
static const char *	find_backward_quote (const char *input, const char *start, const char *whats)
{
	const char *	saved_input = input;
	char		what;
	int		simple;

	if (whats && whats[0] && whats[1] == 0)
	{
		simple = 1;
		what = whats[0];
	}
	else
	{
		simple = 0;
		what = 255;
	}

	/*
	 * An "extended word" is defined as:
	 *	<SPACE> <QUOTE> <ANY>* <QUOTE> <SPACE>
	 * <SPACE> := <WORD START> | <WORD END> | <" "> | <"\t"> | <"\n">
	 * <QUOTE> :- <'"'>	(from quotechar)
	 * <ANY>   := ANY ASCII CHAR 0 .. 256
	 */
	/* Make sure we are actually looking at a double-quote */
	if ( (simple == 1 && *input != what) ||
	     (simple == 0 && strchr(whats, *input) == NULL) )
		return NULL;


	/* 
	 * Make sure that the character after is either the end of the
	 * string, or that it is a space.
	 */
	if (input[1] && !risspace(input[1]))
		return NULL;

	/*
	 * Walk the string looking for a double quote.  Yes, we do 
	 * really have to check for \'s, still, because the following
	 * still is one word:
	 *			"one\" two"
	 * Once we find a double quote, then it must be followed by 
	 * either the end of string (chr 0) or a space.  If we find 
	 * that, return the position of the double-quote.
	 */
	for (input--; input >= start; input--)
	{
		if ( (simple == 1 && *input == what && 
				(input > start && input[-1] == '\\')) ||
		     (simple == 0 && strchr(whats, *input) &&
				(input > start && input[-1] == '\\')) )
		{
			input--;
		}
		else if ( (simple == 1 && input[0] == what) ||
			  (simple == 0 && strchr(whats, *input)) )
		{
			if (input == start || risspace(input[-1]))
				return input;
		}
		else if (input == start)
			break;			/* Stop right here */
	}

	/*
	 * If we get all the way to the start of the string w/o finding 
	 * a matching double-quote, then THIS IS NOT AN EXTENDED WORD!
	 * We need to re-do this word entirely by starting over and looking
	 * for a normal word.
	 */
	input = saved_input;
	while (input > start && !risspace(input[0]))
		input--;

	if (risspace(input[0]))
		input++;		/* Just in case we've gone too far */

	return input;		/* Wherever we are is fine. */
}

static int	move_to_prev_word (const char **str, const char *start, int extended, const char *delims)
{
	char	what;
	int	simple;

	if (!str || !*str)
		return 0;

	if (delims && delims[0] && delims[1] == 0)
	{
		simple = 1;
		what = delims[0];
	}
	else
	{
		simple = 0;
		what = 255;
	}

	CHECK_EXTENDED_SUPPORT

	while ((*str) >= start && my_isspace(**str))
		(*str)--;

	if ((*str) > start && extended == DWORD_YES && 
	     ( (simple == 1 && **str == what) ||
	       (simple == 0 && strchr(delims, **str)) ) )
	{
		const char *	before;

		if (!(before = find_backward_quote(*str, start, delims)))
			panic("find_backward returned NULL [2]");
		if (before > start)
			before--;
		*str = before;
	}
	else
	    while ((*str) >= start && !my_isspace(**str))
		(*str)--;

	/* 
	 * This hack sucks, but it's necessary because we're doing
	 * something very dodgy by working our way backwards towards
	 * the front of a C string.  That's the way it goes.
	 *
	 * Basically, if we stop here, it's because we've hit the
	 * front of the string while we were in the middle of a 
	 * word.  So the start of the word is right here at the
	 * beginning of the string (no adjustment necessary)
	 */
	if ((*str) <= start)
	{
		(*str) = start;
		return 1;
	}

	/*
	 * We're not at the start of the string, so the start of 
	 * this word is at the next position.  Mark it and then
	 * go looking for the end of the previous word.
	 */
	while ((*str) > start && my_isspace(**str)) 
	    (*str)--;

	if ((*str) >= start && !my_isspace(**str))
		(*str)++;
	return 1;
}

static int	move_to_next_word (const char **str, const char *start, int extended, const char *delims)
{
	char	what;
	int	simple;

	if (!str || !*str)
		return 0;

	if (x_debug & DEBUG_EXTRACTW_DEBUG)
		yell(">>>> move_to_next_word: mark [%s], start [%s], extended [%d], delims [%s]", *str, start, extended, delims);

	if (delims && delims[0] && delims[1] == 0)
	{
		if (x_debug & DEBUG_EXTRACTW_DEBUG)
			yell(".... move_to_next_word: Simple processing");
		simple = 1;
		what = delims[0];
	}
	else
	{
		if (x_debug & DEBUG_EXTRACTW_DEBUG)
			yell(".... move_to_next_word: Expensive processing");
		simple = 0;
		what = 255;
	}

	CHECK_EXTENDED_SUPPORT

	if (x_debug & DEBUG_EXTRACTW_DEBUG)
	{
		yell(".... move_to_next_word: Extended word support requested [%d]", extended == DWORD_YES);
		yell(".... move_to_next_word: Extended word support for simple case valid [%d]", simple == 1 && **str == what);
		yell(".... move_to_next_word: Extended word support for complex case valid [%d]", simple == 0 && strchr(delims, **str));
	}

	if (extended == DWORD_YES && 
	     ( (simple == 1 && **str == what) ||
	       (simple == 0 && strchr(delims, **str)) ) )
	{
		const char *	after;

		if (!(after = find_forward_character(*str, start, delims)))
			panic("find_forward returned NULL [1]");
		if (*after)
			after++;
		*str = after;
	}
	else
		while (**str && !my_isspace(**str))
			(*str)++;

	while (**str && my_isspace(**str))
		(*str)++;

	return 1;
}

/* 
 * Move to an absolute word number from start
 * First word is always numbered zero.
 */
const char *	real_move_to_abs_word (const char *start, const char **mark, int word, int extended, const char *quotes)
{
	const char *	pointer = start;
	int 		counter = word;

	if (x_debug & DEBUG_EXTRACTW_DEBUG)
		yell(">>>> real_move_to_abs_word: start [%s], count [%d], extended [%d], quotes [%s]", start, word, extended, quotes);

	while (*pointer && my_isspace(*pointer))
		pointer++;

	if (x_debug & DEBUG_EXTRACTW_DEBUG)
		yell(".... real_move_to_abs_word: pointer [%s]", pointer);

	for (; counter > 0 && *pointer; counter--)
		move_to_next_word(&pointer, start, extended, quotes);

	if (x_debug & DEBUG_EXTRACTW_DEBUG)
		yell("<<<< real_move_to_abs_word: pointer [%s]", pointer);

	if (mark)
		*mark = pointer;
	return pointer;
}

/* 
 * Return the number of words in 'str' as defined by 'real_move_to_abs_word'
 */
int	count_words (const char *str, int extended, const char *quotes)
{
	const char *	pointer = str;
	int		counter = 0;

	while (*pointer && my_isspace(*pointer))
		pointer++;

	for (; *pointer; counter++)
		move_to_next_word(&pointer, str, extended, quotes);

	return counter;
}


/* 
 * Move a relative number of words from the present mark 
 */
ssize_t	move_word_rel (const char *start, const char **mark, int word, int extended, const char *quotes)
{
	int 		counter = word;
	const char *	pointer = *mark;
	const char *	end = start + strlen(start);

	if (end == start) 	/* null string, return it */
		return 0;

	if (counter > 0)
	{
	    for (;counter > 0 && *pointer;counter--)
		move_to_next_word(&pointer, start, extended, quotes);
	}
	else if (counter == 0)
		pointer = *mark;
	else /* counter < 0 */
	{
	    for (; counter < 0 && pointer > start; counter++)
		move_to_prev_word(&pointer, start, extended, quotes);
	}

	if (mark)
		*mark = pointer;

	return pointer - start;
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

	/* If firstword is EOS, then the user wants the last word */
	if (firstword == EOS)
	{
		mark = start + strlen(start);
		move_word_rel(start, &mark, -1, extended, "\"");
#ifndef NO_CHEATING
		/* 
		 * Really. the only case where firstword == EOS is
		 * when the user wants $~, in which case we really
		 * dont need to do all the following crud.  Of
		 * course, if there ever comes a time that the
		 * user would want to start from the EOS (when?)
		 * we couldnt make this assumption.
		 */
		return malloc_strdup(mark);
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
		real_move_to_abs_word(start, &mark, firstword, extended, "\"");
		if (!*mark)
			return malloc_strdup(empty_string);
	}

	/* Otherwise, move to the firstwords from the end */
	else
	{
		mark = start + strlen(start);
		move_word_rel(start, &mark, firstword, extended, "\"");
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
			real_move_to_abs_word(start, &mark2, lastword+1, extended, "\"");
		else
		{
			mark2 = start + strlen(start);
			move_word_rel(start, &mark2, lastword, extended, "\"");
		}

		while (mark2 > start && my_isspace(mark2[-1]))
			mark2--;
	}

	/* 
	 * If the end is before the string, then there is nothing
	 * to extract (this is perfectly legal, btw)
         */
	if (mark2 < mark)
		booya = malloc_strdup(empty_string);

	else
	{
		/*
		 * This is kind of tricky, because the string we are
		 * copying out of is const.  So we cant just null off
		 * the trailing character and malloc_strdup it.
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
	const char *	mark;
	const char *	mark2;
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
		move_word_rel(start, (const char **)&mark, -1, extended, "\"");
	}

	/* If the firstword is positive, move to that word */
	else if (firstword >= 0)
		real_move_to_abs_word(start, (const char **)&mark, firstword, extended, "\"");

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
			real_move_to_abs_word(start, (const char **)&mark2, lastword+1, extended, "\"");
		else
			/* its negative -- thats not valid */
			return malloc_strdup(empty_string);

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
		return malloc_strdup(empty_string);

	/* 
	 * If the end is before the string, then there is nothing
	 * to extract (this is perfectly legal, btw)
         */
	if (mark2 < mark)
		return malloc_strdup(empty_string);

	booya = strext(mark, mark2);
	return booya;
}
