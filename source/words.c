/* $EPIC: words.c,v 1.25 2014/03/26 20:44:57 jnelson Exp $ */
/*
 * words.c -- right now it just holds the stuff i wrote to replace
 * that beastie arg_number().  Eventually, i may move all of the
 * word functions out of ircaux and into here.  Now wouldnt that
 * be a beastie of a patch! Beastie! Beastie!
 *
 * Copyright © 1994, 2003 EPIC Software Labs.
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
 *
 * This should turn to YES if DWORD_YES and DEBUG_DWORD ...
 */
#define CHECK_EXTENDED_SUPPORT					\
	if (extended == DWORD_EXTRACTW)				\
	{							\
		if ((x_debug & DEBUG_EXTRACTW) == 0)		\
			extended = DWORD_NO;			\
		else						\
			extended = DWORD_YES;			\
	}							\
	else if (extended == DWORD_DWORDS)			\
	{							\
		if ((x_debug & DEBUG_DWORD) == 0)		\
			extended = DWORD_NO;			\
		else						\
			extended = DWORD_YES;			\
	}

/*
 * search() looks for a character forward or backward from mark 
 */
char *	search_for (char *start, char **mark, char *chars, int how)
{
	const unsigned char *s, *p;
	size_t	cpoffset = -1;

	if (!mark)
		return NULL;		/* Take THAT! */
        if (!*mark)
                *mark = start;

	s = *mark;
	p = chars;

        if (how > 0)   /* forward search */
		s = cpindex(s, p, how, &cpoffset);

	else if (how == 0)
		return NULL;

	else  /* how < 0 */
		s = rcpindex(s + strlen(s), start, p, -how, &cpoffset);

	*mark = (char *)(intptr_t)s;
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
	if (input > start && !isspace(input[-1]))
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
			if (input[1] == 0 || isspace(input[1]))
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
	if (input[1] && !isspace(input[1]))
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
			if (input == start || isspace(input[-1]))
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
	while (input > start && !isspace(input[0]))
		input--;

	if (isspace(input[0]))
		input++;		/* Just in case we've gone too far */

	return input;		/* Wherever we are is fine. */
}

/*
 * 'move_to_prev_word': Move a "mark" from its current position to the
 *	beginning of the "previous" word.
 *
 * Arguments:
 *  'str' - a pointer to a character pointer -- The initial value is the
 *	    "mark", and it will be changed upon return to the beginning
 *	    of the previous word.
 *  'start' - The start of the string that (*str) points to.
 *  'extended' - Whether double quoted words shall be supported
 *  'delims' - The types of double quoets to honor (if applicable)
 *
 * Return value:
 *  If (*str) points to 'start' or is NULL (there is no previous word)
 *	the return value is 0.
 *  Otherwise, the return value is 1.
 *
 * Notes:
 *  Regardless of whether 'start' is actually the start of the string that
 *    '*str' points to, this function will treat it as such and will never go
 *    backwards further than 'start'.
 *  If (*str) points to the nul that terminates 'start', then (*str) shall
 *    be set to the first character in the last word in 'start'.
 *  If (*str) points to the first character in any word, then (*str) shall
 *    be set to the first character in the full word BEFORE (*str).
 *  If (*str) points to the middle of a string, then (*str) shall be set to
 *    the first character IN THAT WORD (*str) points to.
 *  If (*str) points to a space, then (*str) shall be set to the first
 *    character in the word before the space.
 *  A "word" always begins on the second character after the end of a word
 *    because the first character after a word is a space (which is reserved
 *    because we might want to change it to a nul).  That means if there is
 *    more than one space between words, the first space belongs to the "left"
 *    word and all the rest of the spaces belong to the "right" word!
 *
 * XXX - The debugging printfs are ugly.
 */
static int	move_to_prev_word (const char **str, const char *start, int extended, const char *delims)
{
	char	what;
	int	simple;
	const char	*pos;

	if (!str || *str <= start)
		return 0;

	/* Overhead -- work out if "" support will be cheap or expensive */
	if (delims && delims[0] && delims[1] == 0) {
		if (x_debug & DEBUG_EXTRACTW_DEBUG)
			yell(".... move_to_prev_word: Simple processing");
		simple = 1;
		what = delims[0];
	} else {
		if (x_debug & DEBUG_EXTRACTW_DEBUG)
			yell(".... move_to_prev_word: Expensive processing");
		simple = 0;
		what = 255;
	}

	/* Overhead -- work out if we're doing "" support or not. */
	CHECK_EXTENDED_SUPPORT

	/* Start at the mark the user provided us */
	pos = *str;

	if (x_debug & DEBUG_EXTRACTW_DEBUG)
		yell(".... move_to_prev_word: Starting at [%s] (in [%s])", pos, start);

	/*
	 * XXX This is a hack, but what do i care?
	 * If we are pointing at the start of a string, then
	 * we want to go to the PREVIOUS word, so cheat by 
	 * stepping off the word.  This means if you want the
	 * last word, you need to point to the nul, not the last
	 * character before the nul!
	 */
	if (pos > start && isspace(pos[-1]))
		pos--;

	/*
	 * Skip over whitespace
	 */
	while (pos >= start && ((*pos == 0) || my_isspace(*pos)))
		pos--;

	/*
	 * In the above 'mark1' case (the normal case), we would be pointing
	 * at the last character in 'two'.  If this were a double quoted word
	 * then this would be a double quote of some sort, and this code
	 * checks for that.  If it finds a double quote, then it moves to the
	 * "matching" double quote.
	 */
	if (pos > start && extended == DWORD_YES && 
	     ( (simple == 1 && *pos == what) ||
	       (simple == 0 && strchr(delims, *pos)) ) )
	{
		const char *	before;

		if (x_debug & DEBUG_EXTRACTW_DEBUG)
			yell(".... move_to_prev_word: Handling extended word.");

		if (!(before = find_backward_quote(pos, start, delims)))
			panic(1, "find_backward returned NULL [2]");

		if (x_debug & DEBUG_EXTRACTW_DEBUG)
			yell(".... move_to_prev_word: Extended word begins at [%s] (of [%s])", before, start);

		/*
		 * "Before" either points at a double quote or it points
		 * at the start of the string.  If it points at a double
		 * quote, move back one position so it points at a space.
		 */
		if (before > start)
			before--;

		/* So our new mark is the space before the double quoted word */
		pos = before;

		if (x_debug & DEBUG_EXTRACTW_DEBUG)
			yell(".... move_to_prev_word: So the position before the extended word is [%s] (of [%s])", pos, start);
	}

	/* 
	 * If this is not a double quoted word, keep moving backwards until
	 * we find a space -- so our new mark is the space before the start
	 * of the word.
	 */
	else
	{
	    if (x_debug & DEBUG_EXTRACTW_DEBUG)
		yell(".... move_to_prev_word: Handling simple word.");

	    while (pos >= start && !my_isspace(*pos))
		pos--;

	    if (x_debug & DEBUG_EXTRACTW_DEBUG)
		yell(".... move_to_prev_word: So the position before the simple word is [%s] (of [%s])", pos, start);
	}

	/*
	 * If we hit the front of the string (*gulp*), set the return value
	 * (*str) to the start of the string and just punt right here.
	 */
	if (pos <= start)
	{
		if (x_debug & DEBUG_EXTRACTW_DEBUG)
			yell(".... move_to_prev_word: Ooops. we hit the start "
				"of the string.  Stopping here.");

		*str = start;
		return 1;
	}

	/*
	 * Slurp up spaces.
	 */
	else
	{
		while (*pos && isspace(*pos))
			pos++;

		while (pos > start && isspace(pos[0]) && isspace(pos[-1]))
			pos--;
	}
	
	if (x_debug & DEBUG_EXTRACTW_DEBUG)
		yell(".... move_to_prev_word: And after we're done, [%s] is "
			"the start of the previous word!", pos);

	*str = pos;
	return 1;
}

/*
 * 'move_to_next_word': Move a "mark" from its current position to the
 *	beginning of the "next" word.
 *
 * Arguments:
 *  'str' - a pointer to a character pointer -- The initial value is the
 *	    "mark", and it will be changed upon return to the beginning
 *	    of the previous word.
 *  'start' - The start of the string that (*str) points to.
 *  'extended' - Whether double quoted words shall be supported
 *  'delims' - The types of double quoets to honor (if applicable)
 *
 * Return value:
 *  If (*str) is NULL or points to the end of a string (there is no next
 *	word) the return value is 0.
 *  Otherwise, the return value is 1.
 *
 * Notes:
 *  Regardless of whether 'start' is actually the start of the string that
 *    '*str' points to, this function will treat it as such and will never go
 *    backwards further than 'start'.
 *  If (*str) points to the nul that terminates 'start', then (*str) shall
 *    be set to the first character in the last word in 'start'.
 *  If (*str) points to the first character in any word, then (*str) shall
 *    be set to the first character in the full word BEFORE (*str).
 *  If (*str) points to the middle of a string, then (*str) shall be set to
 *    the first character IN THAT WORD (*str) points to.
 *  If (*str) points to a space, then (*str) shall be set to the first
 *    character in the word before the space.
 *  A "word" always begins on the second character after the end of a word
 *    because the first character after a word is a space (which is reserved
 *    because we might want to change it to a nul).  That means if there is
 *    more than one space between words, the first space belongs to the "left"
 *    word and all the rest of the spaces belong to the "right" word!
 *  EXCEPT WHEN there are trailing spaces on a string and we are already in the
 *    last word (there is not a next word).  In that case, ths current word 
 *    claims all of the trailing spaces and (*str) is set to the trailing nul.
 *
 * XXX - The debugging printfs are ugly.
 */
static int	move_to_next_word (const char **str, const char *start, int extended, const char *delims)
{
	char	what;
	int	simple;
	const char *	pos;

	/*
	 * If there is not a word, then just stop right here.
	 */
	if (!str || !*str || !**str)
		return 0;

	if (x_debug & DEBUG_EXTRACTW_DEBUG)
		yell(">>>> move_to_next_word: mark [%s], start [%s], "
			"extended [%d], delims [%s]", *str, start, 
							extended, delims);

	/*
	 * If "delims" has only one character, then we can do the double
	 * quote support simply (by comapring characters), otherwise we have
	 * to do it by calling strchr() for each character.  Ick.
	 */
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

	/*
	 * Here we check to see if we even want to do extended word support.
	 * The user can always have the option to turn it off.
	 */
	CHECK_EXTENDED_SUPPORT

	if (x_debug & DEBUG_EXTRACTW_DEBUG)
	{
		yell(".... move_to_next_word: Extended word support requested [%d]", extended == DWORD_YES);
		yell(".... move_to_next_word: Extended word support for simple case valid [%d]", simple == 1 && **str == what);
		yell(".... move_to_next_word: Extended word support for complex case valid [%d]", simple == 0 && strchr(delims, **str));
	}

	/* Start at where the user asked, eh? */
	pos = *str;

	if (x_debug & DEBUG_EXTRACTW_DEBUG)
	    yell(".... move_to_next_word: starting at [%s]", pos);

	/*
	 * Always skip leading spaces
	 */
	while (*pos && isspace(*pos))
		pos++;

	if (x_debug & DEBUG_EXTRACTW_DEBUG)
	    yell(".... move_to_next_word: after skipping spaces, [%s]", pos);

	/*
	 * Now check to see if this is an extended word.  If it is an 
	 * extended word, move to the "end" of the word, which is the 
	 * matching puncutation mark.
	 */
	if (extended == DWORD_YES && 
	     ( (simple == 1 && *pos == what) ||
	       (simple == 0 && strchr(delims, *pos)) ) )
	{
		const char *	after;

		if (x_debug & DEBUG_EXTRACTW_DEBUG)
		    yell(".... move_to_next_word: handling extended word");

		if (!(after = find_forward_character(pos, start, delims)))
			panic(1, "find_forward returned NULL [1]");
		if (*after)
			after++;
		pos = after;

		if (x_debug & DEBUG_EXTRACTW_DEBUG)
		    yell(".... move_to_next_word: after extended word [%s]", pos);
	}
	/*
	 * If this is not an extended word, just skip to the next space.
	 */
	else
	{
		if (x_debug & DEBUG_EXTRACTW_DEBUG)
		    yell(".... move_to_next_word: handling simple word [%s]", pos);

		while (*pos && !my_isspace(*pos))
			pos++;

		if (x_debug & DEBUG_EXTRACTW_DEBUG)
		    yell(".... move_to_next_word: at next space: [%s]", pos);
	}

	if (*pos)
		pos++;

	if (x_debug & DEBUG_EXTRACTW_DEBUG)
	    yell("... move_to_next_word: next word starts with [%s]", pos);

	*str = pos;
	return 1;
}

/* 
 * 'real_move_to_abs_word' -- Find the start of the 'word'th word in 'start'.
 *
 * Arguments:
 *  'start' - The string we will look into
 *  'mark' - A pointer to a (char *) where we will set the return value.
 *		MAY BE NULL
 *  'word' - The number of the word we want to find
 *  'extended' - Whether or not to support double quoted words
 *  'quotes' - The types of double quotes we will support.
 * 
 * Return value:
 *  The return value is the beginning of the 'word'th word in 'start'.
 *   This may include leading whitespace which you will need to trim if
 *   you don't want surroundign whitespace.  This may include trailing
 *   whitespace if you ask for the last word and there is trailing whitespace.
 *
 * Notes:
 *  'mark' is not used as an input parameter.  Upon return, it is set to 
 *	the return value.
 *  The return value is suitable for passing to 'strext' if you wanted to 
 *	extract a string ending with the previous word!
 *  Word numbering always counts from 0.
 */
const char *	real_move_to_abs_word (const char *start, const char **mark, int word, int extended, const char *quotes)
{
	const char *	pointer = start;
	int 		counter = word;

	if (x_debug & DEBUG_EXTRACTW_DEBUG)
		yell(">>>> real_move_to_abs_word: start [%s], count [%d], extended [%d], quotes [%s]", start, word, extended, quotes);

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

	while (move_to_next_word(&pointer, str, extended, quotes))
		counter++;

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
 */
char *	real_extract2 (const char *start, int firstword, int lastword, int extended)
{
	const char *	mark;
	const char *	mark2;
	char *		retval;

	/*
	 * 'firstword == EOS' means we should return the last word
	 * This is used for $~.  Handle the whole shebang here.
	 */
	if (firstword == EOS)
	{
		/* Mark to the end of the string... */
		mark = start + strlen(start);

		/* And move to the start of that word */
		move_word_rel(start, &mark, -1, extended, "\"");

		return malloc_strdup(mark);
	}

	/*
	 * 'firstword == SOS' means the user did $-<num>.
	 * The start of retval is start of string.
	 */
	else if (firstword == SOS)
		mark = start;

	/*
	 * 'firstword is not negative' means user wants to start at
	 * the <firstword>th word.  Move the mark to that position.
	 *
	 * If the mark does not exist (ie, $10- when there are not
	 * 10 words), fail by returning the empty string.
	 */
	else if (firstword >= 0)
	{
		real_move_to_abs_word(start, &mark, firstword, extended, "\"");
		if (!*mark)
			return malloc_strdup(empty_string);
	}

	/*
	 * 'firstword is negative' means user wants to start at the
	 * <firstword>th word from the end.  Move the mark to that
	 * position.  This is used for $rightw() and stuff.
	 */
	else
	{
		mark = start + strlen(start);
		move_word_rel(start, &mark, firstword, extended, "\"");
	}

	/*****************************************************************/
	/*
	 * So now 'mark' points at the start of the string we want to
	 * return; this should include the spaces that lead up to the word.  
	 *
	 * Now we need to find the end of the string to return.
	 */

	/* 
	 * 'lastword is EOS' means the user did $<num>-, so cheat by 
	 * just returning the mark
	 */
	if (lastword == EOS)
	    return malloc_strdup(mark);

	/*
	 * 'lastword is nonnegative' means the user did $<num1>-<num2>,
	 * so we need to move to the start of the <num2 + 1> and go back
	 * one position -- easy, eh?
	 */
	else if (lastword >= 0)
	    real_move_to_abs_word(start, &mark2, lastword + 1, extended, "\"");

	/*
	 * 'lastword is negative' means the user wants all but the last
 	 * <num> words, so we move to the start of the <num>th word from 
	 * the end, and go back one character!
	 */
	else
	{
	    mark2 = start + strlen(start);
	    move_word_rel(start, &mark2, lastword, extended, "\"");
	}

	/*
	 * move back one position, because we are the start of the NEXT word.
	 */
	if (*mark2 && mark2 > start)
		mark2--;

	/* 
	 * If the end is before the string, then there is nothing
	 * to extract (this is perfectly legal, btw)
         */
	if (mark2 < mark)
		return malloc_strdup(empty_string);

	/*
	 * XXX Backwards compatability requires that $<num> not 
	 * have any leading spaces.
	 */
	if (firstword == lastword)
	{
		while (mark && *mark && isspace(*mark))
			mark++;
	}

	/*
	 * This is kind of tricky, because the string we are
	 * copying out of is const.  So we cant just null off
	 * the trailing character and malloc_strdup it.
	 */
	retval = strext(mark, mark2);

	/* 
	 * XXX Backwards compatability requires that $<num> not have
	 * any trailing spaces, even if it is the last word.
	 */
	if (firstword == lastword)
		remove_trailing_spaces(retval, 0);

	return retval;
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
	while (*start && my_isspace(*start))
		start++;
	remove_trailing_spaces(start, 0);

	if (firstword == EOS)
	{
		mark = start + strlen(start);
		move_word_rel(start, (const char **)&mark, -1, extended, "\"");
	}

	/* If the firstword is positive, move to that word */
	else if (firstword >= 0)
		real_move_to_abs_word(start, (const char **)&mark, 
					firstword, extended, "\"");

	/* Its negative.  Hold off right now. */
	else
		mark = start;


	/* 
	 * When we find the last word, we need to move to the 
         * END of the word, so that word 3 to 3, would include
	 * all of word 3, so we move to the space after the word
 	 */
	/* EOS is a #define meaning "end of string" */
	if (lastword == EOS)
		mark2 = start + strlen(start);
	else 
	{
		if (lastword >= 0)
			real_move_to_abs_word(start, (const char **)&mark2, 
						lastword+1, extended, "\"");
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
