/*
 * input.c: does the actual input line stuff... keeps the appropriate stuff
 * on the input line, handles insert/delete of characters/words... the whole
 * ball o wax.
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright 1999, 2015 EPIC Software Labs.
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
/*
 * This file has been mostly rewritten and reorganized by now.  One of the
 * things we can do now is have completely independant input lines on each
 * screen, and there are really no global variables any more.  A lot of the
 * original code still lies about, as its still useful, but the macros have
 * made the original code hard to distinguish.
 */

#define __need_term_flush__
#include "irc.h"
#include "alias.h"
#include "clock.h"
#include "commands.h"
#include "exec.h"
#include "functions.h"
#include "hook.h"
#include "input.h"
#include "ircaux.h"
#include "keys.h"
#include "screen.h"
#include "server.h"
#include "status.h"
#include "termx.h"
#include "vars.h"
#include "window.h"
#include "output.h"
#include <sys/ioctl.h>		/* XXX ugh */

/* used with input_move_cursor */
#define RIGHT 1
#define LEFT 0

static int	input_move_cursor (int dir, int refresh);

/*
 * This is how close you have to be to the edge of the screen before the
 * input line scrolls to the adjacent zone.  A detailed discussion of zones
 * can be found below.
 */
	const int	WIDTH = 10;

/*
 * input_prompt: This is the global default for the raw, unexpanded 
 * input prompt for all input lines, as determined by /set input_prompt.
 */
	static	char *	input_prompt;

/* 
 * These are sanity macros.  The file was completely unreadable before 
 * I put these in here.  I make no apologies for them.
 *
 * current_screen	The screen we are working on
 * INPUT_BUFFER		The input buffer for the screen we are working on.
 * LOGICAL_CHARS
 * LOGICAL_COLUMN
 * START
 * LOGICAL_CURSOR
 * LOGICAL_LOCATION
 * NEXT_LOGICAL_LOCATION
 * PREV_LOGICAL_LOCATION
 * CURSOR_SPOT
 * NEXT_SPOT
 * PREV_SPOT
 * PHYSICAL_CURSOR	The column the physical cursor is in
 * THIS_CHAR
 * PREV_CHAR
 * NEXT_CHAR
 * ADD_TO_INPUT
 * CURSOR_RIGHT
 * CURSOR_LEFT
 * INPUT_PROMPT		The normalized expanded value of 'input_prompt'.
 * INPUT_PROMPT_LEN	How many columns INPUT_PROMPT takes.
 * IND_LEFT
 * IND_LEFT_LEN
 * IND_RIGHT
 * IND_RIGHT_LEN
 * INPUT_LINE		The line on the terminal where the input line is.
 * CUT_BUFFER		The saved cut buffer (for deletes)
 */

#define current_screen		last_input_screen

/*
 * This is a UTF8 C string containing the input line.
 * Although ultimately this is the master reference copy of the input line,
 * any time you change stuff in it, you need to call retokenize_input_line()
 * to refresh the metadata and update_input() to refresh the screen.
 */
#define INPUT_BUFFER 		current_screen->il->input_buffer

/*
 * A "LOGICAL CHARACTER" is one or more unicode code points that represent
 * a single glyph to the user.  A glyph might take up 1 or 2 columns.
 * A glyph might require multiple code points if the system uses continuation
 * characters (Mac OS X) instead of composed characters (everybody else)
 *
 * When the user hits <backspace> they expect the previous glyph to be deleted,
 * which might be a surprisingly complicated thing to determine.
 *
 * Therefore, each glyph is associated with metadata pointing at INPUT_BUFFER
 *
 * 	logical_chars[n]	This glyph is stored in UTF8 at
 *				   input_buffer + logical_chars[n]
 *	logical_location[n]	This glyph starts at column <X>
 *				  when you count all logical columns in the
 *				  input line starting from 0.  
 *	first_display_glyph	The first logical character that is currently
 *				  visible on the screen.
 *
 */
#define LOGICAL_CHARS		current_screen->il->logical_chars
#define LOGICAL_COLUMN		current_screen->il->logical_columns
#define START			current_screen->il->first_display_char
#define MAXCOLS			current_screen->il->number_of_logical_chars

/*
 * The "LOGICAL CURSOR" is wherever the input point is.  Most of the
 * time it is at the end (pointing at the NUL at the end of INPUT_BUFFER)
 * but it can point at any glyph.  We always keep the physical cursor on
 * top of the logical cursor.
 */
#define LOGICAL_CURSOR          current_screen->il->logical_cursor

/* This is the offset into INPUT_BUFFER where the logical cursor starts */
#define LOGICAL_LOCATION	LOGICAL_CHARS[LOGICAL_CURSOR]

/* 
 * This is the offset into INPUT_BUFFER where the character after the
 * logical cursor starts.  Note that continuation characters mean that
 * this spot may be several code points away!  That's why we tokenize
 * the input and keep these in arrays.
 */
#define NEXT_LOGICAL_LOCATION	LOGICAL_CHARS[LOGICAL_CURSOR + 1]
#define PREV_LOGICAL_LOCATION	LOGICAL_CHARS[LOGICAL_CURSOR - 1]

/* 
 * These are (char *) pointers to where the input cursor currently lives.
 * (and the next glyph, and the previous glyph)
 * IN THEORY you could operate on the input buffer by doing string things
 * on this pointer.  BE VERY CAREFUL YOU DON'T SCREW IT UP!
 *
 * IF YOU CHANGE THE INPUT BUFFER YOU _MUST_ CALL retokenize_input()
 * AND YOU _MUST_ CALL update_input()!
 */
#define CURSOR_SPOT		(INPUT_BUFFER + LOGICAL_LOCATION)
#define NEXT_SPOT		(INPUT_BUFFER + NEXT_LOGICAL_LOCATION)
#define PREV_SPOT		(INPUT_BUFFER + PREV_LOGICAL_LOCATION)

/* 
 * The physical cursor is the distance of the logical cursor from whatever
 * glyph is "first", plus the input prompt plus the left indicator.
 */
#define PHYSICAL_CURSOR         LOGICAL_COLUMN[LOGICAL_CURSOR] - LOGICAL_COLUMN[START] + INPUT_PROMPT_LEN + (START != 0 ? IND_LEFT_LEN : 0)

/*
 * This returns the FIRST CODE POINT of the character under the cursor.
 * This is suitable for checking if it's a space or an alphanumeric or
 * to see how many columns this glyph takes up, that kind of stuff...
 * But it isn't suitable for actually copying stuff around!
 * This is an rvalue on purpose to keep you from assigning to it.
 */
#define THIS_CHAR 		grab_codepoint(CURSOR_SPOT)
#define PREV_CHAR 		grab_codepoint(PREV_SPOT)
#define NEXT_CHAR		grab_codepoint(NEXT_SPOT)


/*
 * Add a byte to the end of the input line.
 * You should have done *CURSOR_SPOT = 0, if you're modifying
 * the middle of the input buffer.
 * You should have done something "CURSOR_SPOT" -- ie, move it to the
 * cut buffer or saved it to a local string, so you can add it back.
 * 
 * AFTER YOU CALL ADD_TO_INPUT() YOU _MUST_ CALL retokenize_input().
 * AND YOU _MUST_ CALL update_input()!
 */
#define ADD_TO_INPUT(x) 	strlcat(INPUT_BUFFER, (x), sizeof INPUT_BUFFER);

/*
 * Moving the cursor is as easy as changing the logical cursor.
 * HOWEVER IF YOU DO THIS YOU _MUST_ CALL update_input()!
 * Update_input() is what updates the cursor on the user's screen.
 */
#define CURSOR_RIGHT            LOGICAL_CURSOR++
#define CURSOR_LEFT             LOGICAL_CURSOR--


#define INPUT_PROMPT 		current_screen->il->input_prompt
#define INPUT_PROMPT_LEN 	current_screen->il->input_prompt_len

#define IND_LEFT                current_screen->il->ind_left
#define IND_LEFT_LEN            current_screen->il->ind_left_len

#define IND_RIGHT               current_screen->il->ind_right
#define IND_RIGHT_LEN           current_screen->il->ind_right_len

/* 
 * This is the physical line on the screen where the input line is. 
 * IE, if the terminal is 46 lines tall, then this is 46.
 */
#define INPUT_LINE 		current_screen->il->input_line
#define CUT_BUFFER		cut_buffer


BUILT_IN_KEYBINDING(debug_input_line)
{
	int	i;
	char	*s;
	int	offset, column;
	int	codepoint;
	char	buffer[2048];
	char	byte[8];

	*buffer = 0;
	for (i = 0; i < INPUT_BUFFER_SIZE; i++)
	{
		snprintf(byte, sizeof(byte), "%X", INPUT_BUFFER[i]);
		strlcat(buffer, " ", sizeof(buffer));
		strlcat(buffer, byte, sizeof(buffer));
		if (INPUT_BUFFER[i] == 0)
			break;
	}

	yell("INPUT LINE is: %s [%s]", INPUT_BUFFER, buffer);

	for (i = 0; i < INPUT_BUFFER_SIZE; i++)
	{
		offset = LOGICAL_CHARS[i];
		column = LOGICAL_COLUMN[i];
		codepoint = grab_codepoint(INPUT_BUFFER + offset);

		if (i == START)
			yell("LC %02d: START OF DISPLAY HERE", i);
		yell("LC %02d: Offset %d, Columns %d, Codepoint %x",
			i, offset, column, codepoint);
		if (i == LOGICAL_CURSOR)
			yell("LC %02d: CURSOR HERE", i);
		if (codepoint == 0)
		{
			yell("LC %02d: END HERE", i);
			break;
		}
	}

	yell("PHYSICAL CURSOR AT %d %d", PHYSICAL_CURSOR, INPUT_LINE);
}

int	cursor_position (void *vp)
{
	Screen *s = (Screen *)vp;
	if (!s)
		return -1;

	/* 
	 * This used to return a byte offset into the input line,
	 * because of $mid() and stuff.  But now that those funcs
	 * are all utf8 aware, we want to actually return the logical
	 * character itself.
	 */
	/*return s->il->logical_chars[s->il->logical_cursor]; */
	return s->il->logical_cursor;
}

#define LOGICAL_LOCATION	LOGICAL_CHARS[LOGICAL_CURSOR]

/* XXXX Only used here anyhow XXXX */
/*
 * safe_puts -- output only 'numcols' columns of bytes from 'str'.
 *
 * Arguments:
 *	str	- A string to output
 *	numcols	- The maximum number of columns to output
 */
static int 	safe_puts (const unsigned char *str, int numcols)
{
	int 	i = 0;
	const unsigned char *s, *x;
	unsigned char	utf8str[8];
	int	code_point;
	int	cols;
	int	allow_c1_chars = -1;

	s = str;
	while ((code_point = next_code_point(&s, 1)))
	{
		/* C1 chars have to be checked */
		if (code_point >= 0x80 && code_point <= 0x9F)
		{
			if (allow_c1_chars == -1)
			    allow_c1_chars = get_int_var(ALLOW_C1_CHARS_VAR);

			/* We don't output C1 chars */
			if (!allow_c1_chars)
				continue;
		}

		if ((cols = codepoint_numcolumns(code_point)) == -1)
			cols = 1;

		if (i + cols > numcols)
			break;

		/* Convert code_point from utf8 to users encoding */
		ucs_to_console(code_point, utf8str, sizeof(utf8str));

		for (x = utf8str; *x; x++)
			term_inputline_putchar(*x);

		i += cols;
	}

	return i;
}

/* cursor_to_input: move the cursor to the input line, if not there already */
void 	cursor_to_input (void)
{
	Screen *oldscreen = last_input_screen;
	Screen *screen;
static	int	recursive = 0;

	if (recursive)
		return;

	if (!foreground)
		return;		/* Dont bother */

	recursive = 1;
	for (screen = screen_list; screen; screen = screen->next)
	{
		if (screen->alive)
		{
			output_screen = screen;
			last_input_screen = screen;
/*yell("moving cursor to %d %d", PHYSICAL_CURSOR, INPUT_LINE); */
			term_move_cursor(PHYSICAL_CURSOR, INPUT_LINE);
			term_flush();
		}
	}
	output_screen = last_input_screen = oldscreen;
	recursive = 0;
}

/*
 * This function populates the "logical_chars" and "logical_columns"
 * arrays starting from the 'start'th logical char.  If you want to
 * redo the whole thing, then start should be 0.
 *
 * XXX For now, we ignore 'start' and always redo the whole thing.
 */
static int	retokenize_input (int start)
{
	const unsigned char *str;
	const unsigned char *s, *old_s;
	int	cols;
	int	codepoint;
	int	current_column;

	start = 0;
	current_column = 0;
	old_s = s = str = INPUT_BUFFER;

	while (s && *s)
	{
		codepoint = next_code_point(&s, 1);
		cols = codepoint_numcolumns(codepoint);
		/* Invalid chars are probably highlights */
		if (cols == -1)
			cols = 1;

		if (cols == 0)
			/* skip over continuation chars */;
		else
		{
			LOGICAL_CHARS[start] = old_s - INPUT_BUFFER;
			LOGICAL_COLUMN[start] = current_column;

			current_column += cols;
			start++;
			LOGICAL_CHARS[start] = 9999;
		}
		old_s = s;
	}

	/* Set down the null */
	LOGICAL_CHARS[start] = s - INPUT_BUFFER;
	LOGICAL_COLUMN[start] = current_column;
	MAXCOLS = start;

	while (++start < INPUT_BUFFER_SIZE)
	{
		LOGICAL_CHARS[start] = 9999;
		LOGICAL_COLUMN[start] = 0;
		start++;
	}

	return 0;
}


/*
 * update_input: Refresh the input line so what is on screen agrees with
 *		 what is in memory.
 *
 * The input line is operated on 'asynchronously' meaning you can perform
 * many operations in a batch, and then call update_input() when you're
 * done and it will sort out what the screen needs to look like.
 *
 * There are three broad types of changes you can make:
 *	1. UPDATE_JUST_CURSOR - I moved the cursor, but didn't change anything
 *	2. UPDATE_FROM_CURSOR - I changed something at or after the cursor
 *	3. UPDATE_ALL - I changed something before the cursor, OR
 *			An external event happened that I think affects 
 *			the input line (such as joining a channel)
 *
 * The results of these three are:
 *	UPDATE_ALL	- The input line is re-compiled and unconditionally 
 *			  redrawn.  This causes flicker, so we try to avoid 
 *			  doing this unnecessarily.
 *	UPDATE_FROM_CURSOR - The input line is re-compiled starting from
 *			     the cursor and redrawn fromthe cursor
 *	UPDATE_JUST_CURSOR - The cursor is moved; nothing else is changed.
 *
 * In addition to all of the above, we always perform these checks:
 *	1. Has the prompt changed?
 *	2. Has the left indicator changed?
 *	3. Has the right indicator changed?
 *	4. Has the size of the screen changed?
 *	5. Is the cursor too close to the left edge?
 *	6. Is the cursor too close to the right edge?
 * If any of these apply, a corrective measure is taken, and then the
 * refresh is upgraded to UPDATE_ALL.
 *
 * Clear as mud?
 */
void	update_input (void *which_screen, int update)
{
	char	*ptr, *ptr_free;
	int	max;
const char *	prompt;
	int	do_echo, old_do_echo;
	Screen	*os, *ns;
	Window 	*saved_current_window;
	int	cols_used;
	int	original_update;
	Screen	*oos;

	/*
	 * No input line in dumb or bg mode.
	 */
	if (dumb_mode || !foreground)
		return;

	/* Save the state of things */
	os = last_input_screen;
	saved_current_window = current_window;
	original_update = update;
	oos = output_screen;

        for (ns = screen_list; ns; ns = ns->next)
	{

	/* <<<< INDENTED BACK ONE TAB FOR MY SANITY <<<<< */
	/* XXX This is an ugly way to do this. */
	if (which_screen && (Screen *)which_screen != ns)
		continue;	/* Only update this screen */

	/* Ignore inactive screens. */
	if (!ns->alive)
		continue;

	/* XXX The (mis)use of last_input_screen is lamentable */
	last_input_screen = ns;
	output_screen = ns;
	current_window = ns->current_window;
	update = original_update;
	do_echo = last_input_screen->il->echo;

	/*
	 * FIRST OFF -- Recalculate the metadata
	 *	1. Has the raw prompt changed?
	 *	2. Has the resulting expanded prompt changed?
	 *	3. Has the resulting expanded left indicator changed?
	 *	4. Has the resulting expanded right indicator changed?
	 *	5. Has the size of the screen changed?
	 *
	 * If any of these four are true, we must regenerate zones
	 * We know which logical cursor the cursor sits on, we just
	 * have to figure out where that lives
	 *
	 *
	 * THE CHEATERS GUIDE TO ZONES
	 *  I thought it would be easy to just calculate the 'zone' that
	 *  every logical character would be in, but there are three
	 *  complications:
	 *	1. Changing between zones doesn't happen very much
	 *	2. The size of zones can change frequently (prompt/indicators)
	 *	3. The condition required to switch zones is easily described
	 *  
	 * WHEN TO SWITCH ZONES:
	 *	Let X be the first logical character being displayed (saved)
	 *	Let Y be the width of the scrollable zone (saved/calculated)
	 *		Y = WIDTH OF SCREEN
	 *		Y -= WIDTH OF PROMPT
	 *		IF X > 0
	 *			Y -= WIDTH OF LEFT INDICATOR
	 *		END IF
	 *		IF character under (X + Y)
	 *			Y -= WIDTH OF RIGHT INDICATOR
	 *		END IF
	 *
	 *	The cursor is allowed to be within (X + 10) and (Y - 10),
	 *	with the exception of X = 0, then (X) and (Y - 10).
	 *	This means the typable area is either (Y - 10) the first zone
	 *	or (Y - 20) other zones).
	 *
	 *	Therefore, given a logical cursor position, we can calculate
	 *	a new X.
	 *
	 *	# CALCULATE NEW X
	 *	IF logical_column(CURRENT) < Y - 10
	 *		LET new X = 0
	 *	ELSE IF X > 0 AND distance(lc(X), lc(CURRENT)) < 10)
	 *		# SHIFT LEFT
	 *		LET new X = Z : distance(lc(X), lc(Z)) = Y
	 *		IF new X < 0
	 *			new X = 0
	 *		END
	 *	ELSE IF distance(CURRENT, Y) < 10
	 *		# SHIFT RIGHT
	 *		IF X = 0
	 *			LET new X = Y - 20
	 *		ELSE
	 *			LET new X = X + Y
	 *		END
	 *	END
	 *
	 *
	 * Thus, after moving the cursor, we must recalculate all of the
	 * above things.  Moving the cursor could be a cursor press, but
	 * also typing.
	 *
	 * After the new X is calculated, if it is different from the old X,
	 * then the input line must be completely redrawn.
	 *
	 * Redrawing the input line involves 
	 *	1. Determining where the first byte lives
	 *	2. Determining which column the first byte lives
	 *	3. Determining how many bytes must be output
	 *	4. Determining which column the cursor goes into
	 */


	/*
	 * Now we need to retokenize the input.
	 * UPDATE will tell us where to start from (from the cursor or all)
	 * Walk the input buffer, and make a note of the byte offset where
	 * each column begins
	 */

	/* 
	 * The input line is a utf8 C string
	 * Decomposed into a sequence of LOGICAL CHARACTERS
	 * Which are composed of ONE OR MORE UNICODE CODE POINTS
	 * Which take up ZERO OR MORE COLUMNS
	 *
	 * char *input_line[]		The UTF8 C string
	 * int logical_chars[]		Logical chars -> input_line[x]
	 * int logical_column[]		Logical chars -> Logical column
	 * int logical_zone[]		Logical chars -> zone
	 * 
	 * A "zone" is a segment of the input line that is displayed 
	 * at once.  When you move the cursor to an adjacent "zone" then
	 * the input line is completely redrawn.  Zones are numbered from 0.
	 * Zone 0 contains the start of the input line.  the final zone
	 * is whatever zone contains the terminal nul. 
	 *
	 * WHAT IS DISPLAYED:
	 * [prompt] [left indicator^] [spill-over from zone N-1]
	 *		[zone N] 
	 *		[spill-over from zone-N] [right indicator^^]
	 *    ^ [Left Indicator] is not shown for segment 0, which means
	 *	 zone 0 is longer than other zones.
	 *    ^^ [Right Indicator] is not shown for the final zone.
	 */

	/*
	 * No matter what happens, we check the input prompt, the left 
	 * and right indicators.  XXX Sigh!  This is too expensive!
	 * If anything changes, we upgrade to "UPDATE_ALL".
	 */

	/*
	 * If the current window is query'ing an exec'd process,
	 * then we just get the current prompt for that process.
	 * This hardly ever happens, so we malloc() this to make
	 * the code below much simpler.
	 */
	if (last_input_screen->il->input_prompt_raw)
		prompt = last_input_screen->il->input_prompt_raw;
	else if (is_valid_process(get_target_by_refnum(0)))
		prompt = get_prompt_by_refnum(0);
	else
		prompt = input_prompt;

	ptr_free = expand_alias(prompt, empty_string);
	ptr = new_normalize_string(ptr_free, 0, display_line_mangler);
	new_free(&ptr_free);

	/*
	 * If the prompt has changed, update the screen, and count
	 * the number of columns the new prompt takes up.  If the
	 * prompt changes, we have to redraw the entire input line
	 * from scratch (since stuff probably has moved)
	 */
	if (strcmp(ptr, INPUT_PROMPT))
	{
		malloc_strcpy((char **)&INPUT_PROMPT, ptr);
		INPUT_PROMPT_LEN = output_with_count(INPUT_PROMPT, 0, 0);
		update = UPDATE_ALL;
	}

	new_free(&ptr);

	ptr_free = expand_alias(get_string_var(INPUT_INDICATOR_LEFT_VAR), 
					empty_string);
	ptr = new_normalize_string(ptr_free, 0, display_line_mangler);
	new_free(&ptr_free);

	if (strcmp(ptr, IND_LEFT))
	{
		malloc_strcpy((char **)&IND_LEFT, ptr);
		IND_LEFT_LEN = output_with_count(IND_LEFT, 0, 0);
		update = UPDATE_ALL;
	}

	new_free(&ptr);

	ptr_free = expand_alias(get_string_var(INPUT_INDICATOR_RIGHT_VAR), 
					empty_string);
	ptr = new_normalize_string(ptr_free, 0, display_line_mangler);
	new_free(&ptr_free);

	if (strcmp(ptr, IND_RIGHT))
	{
		malloc_strcpy((char **)&IND_RIGHT, ptr);
		IND_RIGHT_LEN = output_with_count(IND_RIGHT, 0, 0);
		update = UPDATE_ALL;
	}

	new_free(&ptr);


	/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
	/*
	 *
	 * HAS THE SCREEN CHANGED SIZE SINCE THE LAST TIME?
	 *
	 */

	/*
	 * If the screen has resized, then we need to re-compute the
	 * side-to-side scrolling effect.
	 */
	if ((last_input_screen->li != last_input_screen->old_li) ||
	    (last_input_screen->co != last_input_screen->old_co) ||
	    (INPUT_LINE != last_input_screen->li))
	{
		/*
		 * The input line is always the bottom line
		 */
		INPUT_LINE = last_input_screen->li - 1;

		last_input_screen->old_co = last_input_screen->co;
		last_input_screen->old_li = last_input_screen->li;
	}


	/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
	/*
	 * IS THE CURSOR TOO NEAR THE LEFT OR RIGHT EDGE OF SCREEN?
	 *
	 * XXX This should not be done unconditionally.
	 */
	if (1)
	{
		int	targetcol;
		int	x;
		int	totalcols;
		int	zone0_overflow = 0;
		int	OLD_START = START;

		/*
		 * Calculate the "zone" that the cursor is in.
		 *
		 * The challenge here is the physical screen is
		 * divided into three parts
		 *
		 * LEFT PART: 	Input Prompt
		 *		Left Indicator (maybe)
		 * RIGHT PART:	Right Indicator (maybe)
		 *
		 * The space for the input line is the width of the screen
		 * minus the size of those two parts.  The "maybe" parts
		 * throw a monkey wrench since the space for the input line
		 * very much depends on where we are, and where we decide
		 * to put the START! 
		 *
		 * As a nod to history, we try to make the zones symmetrical
		 * (ie, if you cursor right off the end of zone 0, you will
		 * be put in zone 1 in a place where if you do a cursor left
		 * you will go back to the end of zone 0); and that the zone
		 * you're in does not depend on where the cursor is.
		 *
		 *	Width Zone 0 is columns - prompt - (right indicator)?
		 *	Width Zone 1+ is columns - prompt - left indicator - 
		 *					(right indicator)?
		 *
		 * So the only question for Zone 0 is, is the width of the
		 * entire input line wider than columns - prompt?
		 *	
		 */
		totalcols = input_column_count(INPUT_BUFFER);
		if (totalcols > last_input_screen->co - INPUT_PROMPT_LEN)
			zone0_overflow = 1;

		/*
		 * Is the cursor in zone 0?
		 */
		if (LOGICAL_COLUMN[LOGICAL_CURSOR] < 
			last_input_screen->co - 
				INPUT_PROMPT_LEN - 
				WIDTH -
				zone0_overflow * IND_RIGHT_LEN)
		{
			targetcol = 0;
		}

		/*
		 * Is the cursor in zone 1? 
		 */
		else
		{
			int	zone0_length;
			int	net_columns;
			int	zone_length;
			int	zone;

			/* We know how big zone 0 is */
			zone0_length = last_input_screen->co - 
					INPUT_PROMPT_LEN - 
					WIDTH -
					IND_RIGHT_LEN;

			/* 
			 * After we subtract zone0 (which is wider
			 * than the other columns, how many cols do
			 * we still have to account for?
			 */
			net_columns = LOGICAL_COLUMN[LOGICAL_CURSOR] -
					zone0_length;

			/* 
			 * Zones 1-N are the same width.  Yes, I know
			 * that adding IND_RIGHT_LEN is cheating, but
			 * that's a very small thing.  I think?
			 */
			zone_length = last_input_screen->co - 
					INPUT_PROMPT_LEN -
					WIDTH -
					IND_LEFT_LEN -
					IND_RIGHT_LEN;

			/*
			 * Our new ZONE is rounded down from however
			 * many columns we have left by how many we can
			 * cram into every zone.
			 */
			zone = net_columns / zone_length;

			/*
			 * The new START is WIDTH chars before the
			 * start of the ZONE we are in (because you
			 * aren't permitted to cursor left to the 
			 * WIDTH chars -- that takes you to the previous
			 * zone, got it?
			 */
			targetcol = (zone0_length + zone * zone_length) - WIDTH;
		}

		for (x = 0; x <= LOGICAL_CURSOR; x++)
		{
			if (LOGICAL_COLUMN[x] >= targetcol)
			{
				START = x;
				if (START != OLD_START)
					update = UPDATE_ALL;
				break;
			}
		}
	} 

	/*
	 * OK.  Now let's update the screen!
	 */
	if (update == UPDATE_ALL)
	{
		/*
		 * Move the cursor to the start of the input line
		 */
		term_move_cursor(0, INPUT_LINE);

		/* Forcibly output the prompt */
		old_do_echo = term_echo(1);

		/*
		 * Figure out how many cols we will give to the
		 * prompt.  If the prompt is too long, we will
		 * clobber it below.  If the prompt won't fit, then
		 * we will clobber the whole thing.
		 */
		if (START!=0)
			cols_used = INPUT_PROMPT_LEN + IND_LEFT_LEN;
		else
			cols_used = INPUT_PROMPT_LEN;

		if (cols_used > (last_input_screen->co - WIDTH))
		{
			cols_used = last_input_screen->co - WIDTH;
			if (cols_used < 0)
				cols_used = 0;
		}

		/* Only output the prompt if there's room for it. */
		if (cols_used > 0)
			output_with_count(INPUT_PROMPT, 0, 1);

		if (START!=0)
			output_with_count(IND_LEFT, 0, 1);

		term_echo(old_do_echo);

		/*
		 * Turn the echo back to what it was before,
		 * and output the rest of the input buffer.
		 */
		term_echo(do_echo);

		if (input_column_count(INPUT_BUFFER + LOGICAL_CHARS[START]) >
				last_input_screen->co - cols_used)
		{
			cols_used += IND_RIGHT_LEN;
			safe_puts(INPUT_BUFFER + LOGICAL_CHARS[START],
				  last_input_screen->co - cols_used);
			output_with_count(IND_RIGHT, 0, 1);
		}
		else 
			safe_puts(INPUT_BUFFER + LOGICAL_CHARS[START],
				  last_input_screen->co - cols_used);

		term_echo(old_do_echo);

		/*
		 * Clear the rest of the input line and reset the cursor
		 * to the current input position.
		 */
		term_clear_to_eol();
		term_flush();
		term_move_cursor(PHYSICAL_CURSOR, INPUT_LINE);
		term_flush();
	}

	/*
	 * If we're just supposed to refresh whats to the right of the
	 * current logical position...
	 */
	if (update == UPDATE_FROM_CURSOR)
	{
		/*
		 * Move the cursor to where its supposed to be,
		 * Figure out how much we can output from here,
		 * and then output it.
		 */
		term_move_cursor(PHYSICAL_CURSOR, INPUT_LINE);
		term_flush();
		/* XXX 1 Col per byte assumption */

		/* don't ask me why these needed to be split up */
		max = last_input_screen->co;
		max -= PHYSICAL_CURSOR;

		old_do_echo = term_echo(do_echo);

		if (input_column_count(INPUT_BUFFER + LOGICAL_LOCATION) > max) 
		{
			max -= IND_RIGHT_LEN;
			safe_puts(CURSOR_SPOT, max);
			output_with_count(IND_RIGHT, 0, 1);
		} 
		else 
		{
			safe_puts(CURSOR_SPOT, max);
		}

		term_echo(old_do_echo);
		term_clear_to_eol();
		term_flush();
	}

	if (update == UPDATE_JUST_CURSOR)
	{
		term_move_cursor(PHYSICAL_CURSOR, INPUT_LINE);
		term_flush();
	}

	/*
	 * Turn the terminal echo back on, and flush all of the output
	 * we may have done here.
	 */
	term_echo(1);
	term_flush();

	/* <<<<< END INDENT ONE TAB BACK FOR MY SANITY <<<<<< */
	}

	output_screen = oos;
        last_input_screen = os;
	current_window = saved_current_window;
}


/*
 * input_move_cursor -- Move the cursor LEFT or RIGHT.
 *
 * Arguments:
 *	dir	- Must either be LEFT (-1) or RIGHT (1)
 *	refresh	- 0 if you don't want me to call update_input.
 *		     This might happen if you intend to call me multiple times.
 *		     YOU ARE RESPONSIBLE FOR CALLING UPDATE_INPUT() AFTER YOU
 *		     ARE DONE!
 *		  1 if I should call update_input
 *
 * Return value:
 *	1 if I moved the logical cursor
 *	0 if I didn't move the logical cursor (because i'm at the end already)
 */
static int	input_move_cursor (int direction, int refresh)
{
	int	moved = 1;

	if (direction == RIGHT)
	{
		/* Stop if we hit the end of the input line */
		if (!THIS_CHAR) 
			return 0;

		CURSOR_RIGHT;
	}
	else if (direction == LEFT)
	{
		/* Stop if we hit the start of the input line */
		if (LOGICAL_CURSOR == 0)
			return 0;

		CURSOR_LEFT;
	}
	else
	   panic(1, "update_input_cursor not called with RIGHT or LEFT (%d)", direction);

	if (refresh)
		update_input(last_input_screen, UPDATE_JUST_CURSOR);
	return moved;
}

/*
 * set_input: sets the input buffer to the given string, discarding whatever
 * was in the input buffer before 
 */
static void	set_input (const char *str)
{
	size_t len;

	strlcpy(INPUT_BUFFER, str, INPUT_BUFFER_SIZE);
	START = 0;
	LOGICAL_CURSOR = 0;

	retokenize_input(LOGICAL_CURSOR);
	while ((input_move_cursor(RIGHT, 0)))
		;
	update_input(last_input_screen, UPDATE_ALL);

}

/*
 * get_input: returns a pointer to the input buffer.  Changing this will
 * actually change the input buffer.  This is a bad way to change the input
 * buffer tho, cause no bounds checking won't be done 
 */
char *	get_input (void)
{
	return INPUT_BUFFER;
}

/* init_input: initialized the input buffer by clearing it out */
void	init_input (void)
{
	*INPUT_BUFFER = 0;
        START = 0;
        LOGICAL_CURSOR = 0;
        memset(INPUT_BUFFER, 0, sizeof(INPUT_BUFFER));
	retokenize_input(LOGICAL_CURSOR);
}

/*
 * set_input_prompt: sets a prompt that will be displayed in the input
 * buffer.  This prompt cannot be backspaced over, etc.  It's a prompt.
 * Setting the prompt to null uses no prompt 
 */
void	set_input_prompt (void *stuff)
{
	VARIABLE *v;
	const char *prompt;

	v = (VARIABLE *)stuff;
	prompt = v->string;

	if (prompt)
		malloc_strcpy(&input_prompt, prompt);
	else if (input_prompt)
		malloc_strcpy(&input_prompt, empty_string);
	else
		return;

	update_input(NULL, UPDATE_ALL);
}


/* Please define 'spaces' as get_string_var(WORD_BREAK_VAR). */
#define WHITESPACE(x)  ((x < 127) && strchr(spaces, (x & 0x7F)) != NULL)

/* 
 * Why did i put these in this file?  I dunno.  But i do know that the ones 
 * in edit.c didnt have to be here, and i knew the ones that were here DID 
 * have to be here, so i just moved them all to here, so that they would all
 * be in the same place.  Easy enough. (jfn, june 1995)
 */

/*
 * input_forward_word: move the input cursor forward one word in the input
 * line 
 */

BUILT_IN_KEYBINDING(input_forward_word)
{
	const char *spaces;

        if (!(spaces = get_string_var(WORD_BREAK_VAR)))
                spaces = " \t";

	cursor_to_input();

	/* Move to the end of the current word to the whitespace */
	while (THIS_CHAR && !WHITESPACE(THIS_CHAR))
		input_move_cursor(RIGHT, 1);

	/* Move past the whitespace to the start of the next word */
	while (THIS_CHAR && WHITESPACE(THIS_CHAR))
		input_move_cursor(RIGHT, 1);
}

/* input_backward_word: move the cursor left on word in the input line */
BUILT_IN_KEYBINDING(input_backward_word)
{
	const char *spaces;

        if (!(spaces = get_string_var(WORD_BREAK_VAR)))
                spaces = " \t";

	cursor_to_input();

	/* If already at the start of a word, move back a position */
	if (LOGICAL_CURSOR > 0 && !WHITESPACE(THIS_CHAR) && WHITESPACE(PREV_CHAR))
		input_move_cursor(LEFT, 1);

	/* Move to the start of the current whitespace */
	while (LOGICAL_CURSOR > 0 && WHITESPACE(THIS_CHAR))
		input_move_cursor(LEFT, 1);

	/* Move to the start of the current word */
	while (LOGICAL_CURSOR > 0 && !WHITESPACE(THIS_CHAR))
		input_move_cursor(LEFT, 1);

	/* If we overshot our goal, then move forward */
	/* But NOT if at start of input line! (July 6th, 1999) */
	if (LOGICAL_CURSOR > 0 && (THIS_CHAR) && WHITESPACE(THIS_CHAR))
		input_move_cursor(RIGHT, 1);
}

/*
 * input_delete_character -- Deletes the character currently under the
 * 			     input cursor.
 */
BUILT_IN_KEYBINDING(input_delete_character)
{
	/* Delete key does nothing at end of input */
	if (!THIS_CHAR)
		return;

	/* Whack the character under cursor and redraw input line */
	ov_strcpy(CURSOR_SPOT, NEXT_SPOT);
	retokenize_input(LOGICAL_CURSOR);
	update_input(last_input_screen, UPDATE_FROM_CURSOR);
}


/*
 * input_backspace -- Basically a combination of backward_character and
 *		      delete_character.  No, this is not significantly
 *		      more expensive than the old way.
 */
BUILT_IN_KEYBINDING(input_backspace)
{
	/* Backspace key does nothing at start of input */
	if (LOGICAL_CURSOR == 0)
		return;

	/* Do a cursor-left, followed by a delete. */
	backward_character(0, NULL);
	input_delete_character(0, NULL);
}

/*
 * input_beginning_of_line: moves the input cursor to the first character in
 * the input buffer 
 */
BUILT_IN_KEYBINDING(input_beginning_of_line)
{
	START = 0;
        LOGICAL_CURSOR=0;

	update_input(last_input_screen, UPDATE_ALL);
}

/*
 * input_end_of_line: moves the input cursor to the last character in the
 * input buffer 
 */
BUILT_IN_KEYBINDING(input_end_of_line)
{
	while ((input_move_cursor(RIGHT, 0)))
		;

	update_input(last_input_screen, UPDATE_ALL);
}

/*
 * This removes every code ponit between 'start' and 'end' (inclusive) 
 * The cursor is moved to 'start' because that is where the cut was
 * made as far as the user sees.
 */
static void	cut_input (int start, int end)
{
	char *	buffer;
	size_t	size;
	int	x;
	int	startpos;
	int	endpos;

	startpos = LOGICAL_CHARS[start];
	/* We need to sanity check 'end' here. */
	if (end > MAXCOLS)
		end = MAXCOLS;
	endpos = LOGICAL_CHARS[end + 1];
	strext2(&CUT_BUFFER, INPUT_BUFFER, startpos, endpos);

	LOGICAL_CURSOR = start;
	retokenize_input(start);
	update_input(last_input_screen, UPDATE_ALL);
}

/*
 * To visualize:
 *
 *		This is the input buffer
 *			  ^			(the input cursor)
 * orig_pos points to the 'e'.
 * c is an 'e'.
 * LOGICAL_CURSOR is moved to the space between the 's' and the 't'.
 * The input buffer is changed to:
 *
 *		This is e input buffer
 *			^			(the input cursor)
 */
BUILT_IN_KEYBINDING(input_delete_to_previous_space)
{
	int	anchor;

	cursor_to_input();

	if (LOGICAL_CURSOR <= 0)
		return;

	anchor = LOGICAL_CURSOR;
	while (LOGICAL_CURSOR > 0 && !isspace(PREV_CHAR))
		input_move_cursor(LEFT, 0);
	cut_input(LOGICAL_CURSOR, anchor);
}

/*
 * input_delete_previous_word: deletes from the cursor backwards to the next
 * space character.  This is probably going to be the same effect as 
 * delete_to_previous_space, but hey -- you know.
 */
BUILT_IN_KEYBINDING(input_delete_previous_word)
{
	int	anchor;

	cursor_to_input();

        if (LOGICAL_CURSOR <= 0)
		return;

	anchor = LOGICAL_CURSOR;
	input_backward_word(0, NULL);
	cut_input(LOGICAL_CURSOR, anchor);
}

/*
 * input_delete_next_word: deletes from the cursor to the end of the next
 * word 
 */
BUILT_IN_KEYBINDING(input_delete_next_word)
{
	int	anchor;

	cursor_to_input();

	if (!THIS_CHAR)
		return;

	anchor = LOGICAL_CURSOR;
	input_forward_word(0, NULL);
	cut_input(anchor, LOGICAL_CURSOR - 1);
}

/*
 * input_add_character: adds the codepoint to the input buffer, repecting
 * the current overwrite/insert mode status, etc 
 *
 * Arguments
 *	key	- A code poing to be added at the cursor point
 *	string	- unused
 */
BUILT_IN_KEYBINDING(input_add_character)
{
	int	numcols;
	int	numbytes;
	unsigned char utf8str[8];
	int	utf8strlen;

#if 0
	/* This test isn't used, but should it be? */
	if ((numcols = codepoint_numcolumns(key)) == -1)
		numcols = 1;
#endif

	utf8strlen = ucs_to_utf8(key, utf8str, sizeof(utf8str));

	/* Don't permit the input buffer to get too big. */
	if (strlen(INPUT_BUFFER) + utf8strlen  >= INPUT_BUFFER_SIZE)
		return;

	/*
	 * If we are NOT at the end of the line, and we're inserting
	 * then insert the char (expensive!)
	 */
	if (THIS_CHAR && get_int_var(INSERT_MODE_VAR))
	{
		char *ptr = LOCAL_COPY(CURSOR_SPOT);
		*CURSOR_SPOT = 0;
		ADD_TO_INPUT(utf8str);
		ADD_TO_INPUT(ptr);
	}

	/*
	 * Otherwise, we're either at the end of the input buffer,
	 * or we're in overstrike mode.
	 */
	else if (THIS_CHAR == 0)
	{
		ADD_TO_INPUT(utf8str);
	}
	else
	{
		char *ptr = LOCAL_COPY(NEXT_SPOT);
		*CURSOR_SPOT = 0;
		ADD_TO_INPUT(utf8str);
		ADD_TO_INPUT(ptr);
	}

	retokenize_input(LOGICAL_CURSOR);
	update_input(last_input_screen, UPDATE_FROM_CURSOR);
	input_move_cursor(RIGHT, 1);
}

/* input_clear_to_eol: erases from the cursor to the end of the input buffer */
BUILT_IN_KEYBINDING(input_clear_to_eol)
{
	/* This doesnt really speak to the implementation, but it works.  */
	/* Definitely not right? */
	cut_input(LOGICAL_CURSOR, 9999);
}

/*
 * input_clear_to_bol: clears from the cursor to the beginning of the input
 * buffer 
 */
BUILT_IN_KEYBINDING(input_clear_to_bol)
{
	char	c;
	char	*copy;

	/* XXX Definitely not right */
	cut_input(0, LOGICAL_CURSOR  - 1);

	LOGICAL_CURSOR = 0;
	update_input(last_input_screen, UPDATE_ALL);
}

/*
 * input_clear_line: clears entire input line
 */
BUILT_IN_KEYBINDING(input_clear_line)
{
	/* Only copy if there is input. -wd */
	if (*INPUT_BUFFER)
		cut_input(0, 999);

        memset(INPUT_BUFFER, 0, sizeof(INPUT_BUFFER));
	LOGICAL_CURSOR = 0;
        update_input(last_input_screen, UPDATE_ALL);
}

/*
 * input_reset_line: clears entire input line, suitable for use in tabscripts
 * This does not mangle the cutbuffer, so you can use it to replace the input
 * line w/o any deleterious effects!
 */
BUILT_IN_KEYBINDING(input_reset_line)
{
	/* XXX This code is used in many places */
	*INPUT_BUFFER = 0;
	START = 0;
        LOGICAL_CURSOR=0;
        memset(INPUT_BUFFER, 0, sizeof(INPUT_BUFFER));
        update_input(last_input_screen, UPDATE_FROM_CURSOR);

        if (!string)
		set_input(empty_string);
	else
		set_input(string);	/* This calls update_input() */
}


#if 0
/*
 * input_transpose_characters: move the character before the cursor to
 * the position after the cursor.
 */
BUILT_IN_KEYBINDING(input_transpose_characters)
{
	/*
	 * This is tricky because the cursor never moves and there are three cases:
	 *
	 * 1. Cursor is at char 0: swap 0 and 1 
	 *	A_ B C		->	B_ A C
	 * 2. Cursor is over a char: swap char under cursor and char before cursor
	 *	A B_ C		->	B A_ C
	 * 3. Cursor is at end (not on char): swap char before cursor and char before that
	 *	A B C _		->	A C B _
	 */

	/* Case 1 -- swap char 0 and 1 */
	if (LOGICAL_CURSOR == 0 && THIS_CHAR && NEXT_CHAR)
	{
		int	tc, nc;

		tc = THIS_CHAR;
		nc = NEXT_CHAR;
		*THIS_SPOT = nc;
		*NEXT_SPOT = tc;
	}

	/* Case 2 -- cursor over char, but not the first char */
	else if (LOGICAL_CURSOR > 0 && THIS_CHAR && PREV_CHAR)
	{
		int	tc, pc;

		tc = THIS_CHAR;
		pc = PREV_CHAR;
		*THIS_SPOT = pc;
		*PREV_SPOT = tc;
	}

	/* Case 3 -- cursor at end */
	else if (LOGICAL_CURSOR > 1 && !THIS_CHAR && PREV_CHAR && PREV_PREV_CHAR)
	{
		int	pc, ppc;

		pc = PREV_CHAR;
		ppc = PREV_PREV_CHAR;
		*THIS_SPOT = ppc;
		*PREV_PREV_SPOT = pc;
	}

	/* In all other cases, this is a no-op. */
	update_input(last_input_screen, UPDATE_ALL);
}
#endif


BUILT_IN_KEYBINDING(refresh_inputline)
{
	update_input(NULL, UPDATE_ALL);
}

/*
 * input_yank_cut_buffer: takes the contents of the cut buffer and inserts it
 * into the input line 
 */
BUILT_IN_KEYBINDING(input_yank_cut_buffer)
{
	char	*ptr = NULL;
	int	x;

	if (!CUT_BUFFER)
		return;

	ptr = LOCAL_COPY(CURSOR_SPOT);
	*CURSOR_SPOT = 0;
	ADD_TO_INPUT(CUT_BUFFER);
	ADD_TO_INPUT(ptr);
	retokenize_input(LOGICAL_CURSOR);

	/* XXX strlen(cut_buffer) is wrong */
	for (x = input_column_count(CUT_BUFFER); x > 0; x--)
		input_move_cursor(RIGHT, 0);

	update_input(last_input_screen, UPDATE_ALL);
}



/* BIND functions: */
BUILT_IN_KEYBINDING(forward_character)
{
	input_move_cursor(RIGHT, 1);
}

BUILT_IN_KEYBINDING(backward_character)
{
	input_move_cursor(LEFT, 1);
}

BUILT_IN_KEYBINDING(send_line)
{
	int	server = from_server;
	char *	line;
	int	holding_already;
	int	do_unscroll;

	from_server = get_window_server(0);
	line = LOCAL_COPY(INPUT_BUFFER);

	/* Clear the input line before dispatching the command */
	*INPUT_BUFFER = 0;
        START = 0;
        LOGICAL_CURSOR = 0;
        memset(INPUT_BUFFER, 0, sizeof(INPUT_BUFFER));

	update_input(last_input_screen, UPDATE_ALL);

	holding_already = window_is_holding(last_input_screen->current_window);
	do_unscroll = window_is_scrolled_back(last_input_screen->current_window);

	/*
	 * Hold_mode is weird.  Hold_mode gets even weirder when you're in
	 * scrollback mode.  Hold_mode users would expect that if they hit
	 * <enter> when in scrollback mode, that they would scroll down, but
	 * they would NOT unhold stuff.  But then you get into the problem of
	 * whether to do the scrolldown before or after you run the command.
	 */
	if (holding_already == 0)
		unhold_a_window(last_input_screen->current_window);

	/* XXX This should be rolled together with fire_wait_prompt */
	if (last_input_screen->promptlist && 
		last_input_screen->promptlist->type == WAIT_PROMPT_LINE)
	{
		fire_normal_prompt(line);
	}
	else
	{
		int old = system_exception;

		if (do_hook(INPUT_LIST, "%s", line))
			parse_statement(line, 1, NULL);

		system_exception = old;
	}

	if (holding_already == 1)
		unhold_a_window(last_input_screen->current_window);

	if (last_input_screen->current_window->holding_top_of_display &&
			do_unscroll == 1)
		scrollback_forwards(0, NULL);	/* XXX - Keybinding */

	from_server = server;
}

/****************************************************************************/
/*
 * None of the following keybindings have anything to do with the input line.
 * But they live here, because all of the keybindings are in this file.
 * Maybe some day I'll farm out the keybindings to the four corners.
 * Maybe I'll convince^H^H^H^H^H black into scripting them.
 */

/* This keybinding should be scripted.  */
BUILT_IN_KEYBINDING(toggle_insert_mode)
{
	char *	toggle;

	toggle = alloca(7);
	strlcpy(toggle, "TOGGLE", 7);
	set_var_value(INSERT_MODE_VAR, toggle, 1);
}

/* This keybinding should be scripted. */
BUILT_IN_KEYBINDING(my_clear_screen)
{
	clear_window_by_refnum(0, 1);
}

/* This keybinding should be scripted. */
BUILT_IN_KEYBINDING(input_unclear_screen)
{
	unclear_window_by_refnum(0, 1);
}


/* This keybinding should be in screen.c */
BUILT_IN_KEYBINDING(quote_char)
{
	last_input_screen->quote_hit = 1;
}

/* 
 * These six functions are boomerang functions, which allow the highlight
 * characters to be bound by simply having these functions put in the
 * appropriate characters when you press any key to which you have bound
 * that highlight character. >;-)
 */
BUILT_IN_KEYBINDING(insert_bold)
{
	input_add_character(BOLD_TOG, string);
}

BUILT_IN_KEYBINDING(insert_reverse)
{
	input_add_character(REV_TOG, string);
}

BUILT_IN_KEYBINDING(insert_underline)
{
	input_add_character(UND_TOG, string);
}

BUILT_IN_KEYBINDING(highlight_off)
{
	input_add_character(ALL_OFF, string);
}

BUILT_IN_KEYBINDING(insert_blink)
{
	input_add_character(BLINK_TOG, string);
}

BUILT_IN_KEYBINDING(insert_altcharset)
{
	input_add_character(ALT_TOG, string);
}

BUILT_IN_KEYBINDING(insert_italic)
{
	input_add_character(ITALIC_TOG, string);
}

/* type_text: the BIND function TYPE_TEXT */
BUILT_IN_KEYBINDING(type_text)
{
	char *	ptr;
	int	x, totalcols;

	ptr = LOCAL_COPY(CURSOR_SPOT);
	*CURSOR_SPOT = 0;
	ADD_TO_INPUT(string);
	ADD_TO_INPUT(ptr);
	retokenize_input(LOGICAL_CURSOR);

	/* Now skip over what we just typed. */
	totalcols = input_column_count(string);
	for (x = totalcols; x > 0; x--)
		input_move_cursor(RIGHT, 0);

	update_input(last_input_screen, UPDATE_ALL);
}

/* parse_text: the bindable function that executes its string */
BUILT_IN_KEYBINDING(parse_text)
{
	int	old = system_exception;

	if (string)
		runcmds(string, empty_string);
	system_exception = old;
}

BUILT_IN_FUNCTION(function_inputctl, input)
{
	char 	*verb, *domain;
	int	verblen, domainlen;
        char    *ret = NULL;
        int     old_status_update;

        GET_FUNC_ARG(verb, input);
        verblen = strlen(verb);
        if (!my_strnicmp(verb, "GET", verblen)) {
	    GET_FUNC_ARG(domain, input)
	    domainlen = strlen(domain);

	    if (!my_strnicmp(domain, "CUTBUFFER", domainlen)) {
		RETURN_STR((const char *)CUT_BUFFER);
	    } 
	}
        else if (!my_strnicmp(verb, "SET", verblen)) {
	    GET_FUNC_ARG(domain, input)
	    domainlen = strlen(domain);

	    if (!my_strnicmp(domain, "CUTBUFFER", domainlen)) {
		malloc_strcpy((char **)&CUT_BUFFER, input);
		RETURN_STR((const char *)CUT_BUFFER);
	    } 
	}

	RETURN_EMPTY;
}

