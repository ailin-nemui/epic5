/* $EPIC: input.c,v 1.54 2008/03/29 19:00:16 jnelson Exp $ */
/*
 * input.c: does the actual input line stuff... keeps the appropriate stuff
 * on the input line, handles insert/delete of characters/words... the whole
 * ball o wax.
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright © 1999, 2003 EPIC Software Labs.
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

#if 0
typedef struct InputItem {
	struct InputItem *next;
	struct InputItem *prev;

	wchar_t	character;
	int	column;
	int	num_columns;
} InputItem;

InputItem *	new_input_item (wchar_t wc)
{
	InputItem *ii;

	ii = (InputItem *)new_malloc(sizeof InputItem);
	ii->next = NULL;
	ii->prev = NULL;
	ii->character = wc;
	ii->column = -1;
	ii->num_columns = wcwidth(wc);
}

void	delete_input_item (InputItem **ii)
{
	(*ii)->next = NULL;
	(*ii)->prev = NULL;
	(*ii)->character = L'\0';
	(*ii)->column = -1;
	(*ii)->num_columns = -1;
	new_free((char **)ii);
}

void	add_item_to_input (InputItem *prev, InputItem *item)
{
	InputItem *next;
	prev->next = item;
	item->prev = prev;
	next->prev = item;
	item->next = next;
	renumber_columns(item);
}

void	renumber_columns (InputItem *item);
{
	for (; item; item = item->next)
	    item->column = item->prev->column + item->prev->num_columns;
}

void	delete_input_item_chain (InputItem **ii)
{
	if ((*ii)->next)
		delete_input_item_chain((*ii)->next);
	if ((*ii)->next)
		panic("delete_input_item_chain didn't delete next item");
	(*ii)->prev->next = NULL;
	(*ii)->prev = NULL;
	delete_input_item(ii);
}

void	set_cut_anchor (InputItem *i)
{
}

void	cut_input_line (InputItem *first, InputItem *last)
{
}
#endif

/* 
 * These are sanity macros.  The file was completely unreadable before 
 * I put these in here.  I make no apologies for them.
 *
 * current_screen	The screen we are working on
 * INPUT_BUFFER		The input buffer for the screen we are working on.
 * PHYSICAL_CURSOR	The column the physical cursor is in
 * LOGICAL_CURSOR	The offset in the input buffer the cursor is at
 *			*** Note: If you change the logical cursor, you 
 *				  must call update_input()!
 * THIS_CHAR		The character under the cursor
 * PREV_CHAR		The character before the cursor
 *			*** Note: You must not use this macro unless you
 *			          have checked LOGICAL_CURSOR > 0 first!
 * NEXT_CHAR		The character after the cursor
 *			*** Note: You must not use this macro unelss you
 *				   have checked THIS_CHAR != 0 first!
 * ADD_TO_INPUT		Add some text to the end of the input buffer
 *			*** Note: This does not move the cursor!  You must
 *			          adjust the cursor and call update_input()!
 * INPUT_VISIBLEPOS	The offset of the first char that's visible.
 * INPUT_VISIBLE	A pointer to the visible part of the input line.
 * ZONE			The input line is divided into sections of "zone" cols
 * START_ZONE		The offset of the first char in the current zone.
 *
 * INPUT_PROMPT		The normalized expanded value of 'input_prompt'.
 * INPUT_PROMPT_LEN	How many columns INPUT_PROMPT takes.
 * INPUT_LINE		The line on the terminal where the input line is.
 * CUT_BUFFER		The saved cut buffer (for deletes)
 * SET_CUT_BUFFER	Reset the cut buffer to a new string.
 */

#define current_screen		last_input_screen
#define INPUT_BUFFER 		current_screen->input_buffer
#define PHYSICAL_CURSOR 	current_screen->input_cursor
#define LOGICAL_CURSOR 		current_screen->buffer_pos
#define CURSOR_RIGHT		LOGICAL_CURSOR++
#define CURSOR_LEFT		LOGICAL_CURSOR--
#define THIS_CHAR 		INPUT_BUFFER[LOGICAL_CURSOR]
#define PREV_CHAR 		INPUT_BUFFER[LOGICAL_CURSOR-1]
#define NEXT_CHAR 		INPUT_BUFFER[LOGICAL_CURSOR+1]
#define ADD_TO_INPUT(x) 	strlcat(INPUT_BUFFER, (x), sizeof INPUT_BUFFER);
#define INPUT_VISIBLEPOS	current_screen->input_visible
#define SHOW_PROMPT		INPUT_VISIBLEPOS = 0
#define SHOWING_PROMPT		(INPUT_VISIBLEPOS == 0)
#define INPUT_VISIBLE 		INPUT_BUFFER[INPUT_VISIBLEPOS]
#define ZONE			current_screen->input_zone_len
#define START_ZONE 		current_screen->input_start_zone
#define INPUT_PROMPT 		current_screen->input_prompt
#define INPUT_PROMPT_LEN 	current_screen->input_prompt_len
#define INPUT_LINE 		current_screen->input_line
#define CUT_BUFFER		cut_buffer
#define SET_CUT_BUFFER(x)	malloc_strcpy((char **)&CUT_BUFFER, x);

#define num_cols(x)		strlen(x)

/* XXXX Only used here anyhow XXXX */
static int 	safe_puts (char *str, int numcols, int echo) 
{
	int i = 0;

	while (*str && i < numcols)
	{
		term_inputline_putchar(*str);
		str++, i++;
	}
	return i;
}

/* cursor_to_input: move the cursor to the input line, if not there already */
void 	cursor_to_input (void)
{
	Screen *oldscreen = last_input_screen;
	Screen *screen;

	if (!foreground)
		return;		/* Dont bother */

	for (screen = screen_list; screen; screen = screen->next)
	{
		if (screen->alive)
		{
			output_screen = screen;
			last_input_screen = screen;
			term_move_cursor(PHYSICAL_CURSOR, INPUT_LINE);
			term_flush();
			cursor_not_in_display(screen);
		}
	}
	output_screen = last_input_screen = oldscreen;
}

/*
 * update_input: Perform various housekeeping duties on the input line
 *
 *  1) Recalculate the prompt (not done with CHECK_ZONES)
 *     If the prompt has changed, do an unconditional UPDATE_ALL.
 *  2) If the number of columns changed, recalculate the zone boundaries.
 *  3) Determine if the input line needs to be scrolled left or right
 *     If it does, do an unconditional UPDATE_ALL.
 *  4) Recalculate the physical cursor
 *
 * Then we do the actual redrawing:
 *  CHECK_ZONES: Do nothing further
 *  UPDATE_ALL: 1) Clear the input line
 *              2) If the prompt is visible, draw it
 *		3) Draw whatever part of the input line is visible.
 *  UPDATE_FROM_CURSOR: Redraw the input line starting at the cursor
 *  UPDATE_JUST_CURSOR: I changed the logical cursor, update physical cursor.
 *
 */
void	update_input (void *which_screen, int update)
{
	int	old_zone;
	char	*ptr, *ptr_free;
	int	max;
	const char	*prompt;
	int	do_echo = 1;
	Screen	*os = last_input_screen;
	Screen	*ns;
	Window	*saved_current_window = current_window;
	int	cols_used;
	int	original_update;

	/*
	 * No input line in dumb or bg mode.
	 */
	if (dumb_mode || !foreground)
		return;

	original_update = update;
  for (ns = screen_list; ns; ns = ns->next)
  {
	/* XXXX This is a HIDEOUS abuse of the language, but I don't care! */
	if (which_screen && (Screen *)which_screen != ns)
		ns = (Screen *)which_screen;

	if (!ns->alive)
		continue;	/* It's dead, Jim! */

	last_input_screen = ns;
	current_window = ns->current_window;
	update = original_update;

	/*
	 * Make sure the client thinks the cursor is on the input line.
	 */
/*
	cursor_to_input();
*/

	/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
	/*
	 * Some updates require us to recalculate the input prompt.
	 * If the input prompt /has/ changed, then the requested update
	 * is upgraded to UPDATE_ALL.
	 *
	 * CHECK_ZONES updates do not require recalculating the prompt,
	 * since those operations do not affect the prompt.
	 */


	if (update == UPDATE_JUST_CURSOR || 
	    update == UPDATE_FROM_CURSOR ||
	    update == UPDATE_ALL )
	{
		/*
		 * If the current window is query'ing an exec'd process,
		 * then we just get the current prompt for that process.
		 * This hardly ever happens, so we malloc() this to make
		 * the code below much simpler.
		 */
		if (last_input_screen->promptlist)
		{
			prompt = last_input_screen->promptlist->prompt;
			do_echo = last_input_screen->promptlist->echo;
		}
		else if (is_valid_process(get_target_by_refnum(0)) != -1)
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
			INPUT_PROMPT_LEN = output_with_count(INPUT_PROMPT, 0,0);
			update = UPDATE_ALL;
		}

		new_free(&ptr);
	}


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
	    (last_input_screen->co != last_input_screen->old_co))
	{
		/*
		 * The input line is always the bottom line
		 */
		INPUT_LINE = last_input_screen->li - 1;

		/*
		 * The "zone" is the range in which when you type, the
		 * input line does not scroll.  It is WIDTH chars in from
		 * either side of the display.
		 */
		ZONE = last_input_screen->co - (WIDTH * 2);
		if (ZONE < 10)
			ZONE = 10;		/* Take that! */
		START_ZONE = -1;		/* Force a full update */

		last_input_screen->old_co = last_input_screen->co;
		last_input_screen->old_li = last_input_screen->li;
	}

	/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
	/*
	 * About zones:
	 * The input line is divided into "zones".  A "zone" is set above,
	 * and is the width of the screen minus 20 (by default).  The input
	 * line, as displayed, is therefore composed of the current "zone",
	 * plus 10 characters from the previous zone, plus 10 characters 
	 * from the next zone.  When the cursor moves to an adjacent zone,
	 * (by going into column 9 from the right or left of the edge), the
	 * input line is redrawn.  There is one catch.  The first "zone"
	 * includes the first ten characters of the input line.
	 */
	old_zone = START_ZONE;

	/*
	 * The BEGINNING of the current "zone" is a calculated value:
	 *	The number of characters since the origin of the input buffer
	 *	is the number of printable chars in the input prompt plus the
	 *	current position in the input buffer.  We subtract from that
	 * 	the WIDTH delta to take off the first delta, which doesnt
	 *	count towards the width of the zone.  Then we divide that by
	 * 	the size of the zone, to get an integer, then we multiply it
	 * 	back.  This gives us the first character on the screen.  We
	 *	add WIDTH to the result in order to get the start of the zone
	 *	itself.
	 * The END of the current "zone" is just the beginning plus the width.
	 * If we have moved to an different "zone" since last time, we want to
	 * 	completely redraw the input line.
	 */
	/* XXX 1 Col per byte assumption */
	START_ZONE = WIDTH + (((INPUT_PROMPT_LEN + LOGICAL_CURSOR - WIDTH) 
					/ ZONE) * ZONE);

	if (old_zone != START_ZONE)
		update = UPDATE_ALL;

	/*
	 * Now that we know where the "zone" is in the input buffer, we can
	 * easily calculate where where we want to start displaying stuff
	 * from the INPUT_BUFFER.  If we're in the first "zone", then we will
	 * output from the beginning of the buffer.  If we're not in the first
	 * "zone", then we will begin to output from 10 characters to the
	 * left of the zone, after adjusting for the length of the prompt.
	 */
	if (START_ZONE == WIDTH)
		SHOW_PROMPT;
	else {
	    /* XXX 1 Col per byte assumption */
	    if ((INPUT_VISIBLEPOS = START_ZONE - WIDTH - INPUT_PROMPT_LEN) < 0)
		SHOW_PROMPT;
	}

	/*
	 * If we're showing the prompt (INPUT_VISIBLEPOS == 0) then
	 * the physical cursor is the logical cursor adjusted for how
	 * much room the prompt takes up.
	 *
	 * If we're not showing the prompt (INPUT_VISIBLEPOS != 0) then
	 * the physical cursor is just how far away the logical cursor is
	 * from the first visible character.
	 */
	/* XXX 1 Col per byte assumption */
	if (SHOWING_PROMPT)
		PHYSICAL_CURSOR = INPUT_PROMPT_LEN + LOGICAL_CURSOR;
	else
		PHYSICAL_CURSOR = LOGICAL_CURSOR - INPUT_VISIBLEPOS;

	/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

	/*
	 * If the cursor moved, or if we're supposed to do a full update,
	 * then redraw the entire input line.
	 */
	if (update == UPDATE_ALL)
	{
		/*
		 * Move the cursor to the start of the input line
		 */
		term_move_cursor(0, INPUT_LINE);

		/*
		 * If the input line is NOT empty, and we're starting the
		 * display at the beginning of the input buffer, then we
		 * output the prompt first.
		 */
		if (SHOWING_PROMPT)
		{
			int	old_do_echo;

			/* Forcibly output the prompt */
			old_do_echo = term_echo(1);

			/*
			 * Figure out how many cols we will give to the
			 * prompt.  If the prompt is too long, we will
			 * clobber it below.  If the prompt won't fit, then
			 * we will clobber the whole thing.
			 */
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

			term_echo(old_do_echo);
		}
		else
			cols_used = 0;

		/*
		 * Turn the echo back to what it was before,
		 * and output the rest of the input buffer.
		 */
		term_echo(do_echo);
		safe_puts(&INPUT_VISIBLE, 
			     last_input_screen->co - cols_used, do_echo);

		/*
		 * Clear the rest of the input line and reset the cursor
		 * to the current input position.
		 */
		term_clear_to_eol();
		update = UPDATE_JUST_CURSOR;
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
		/* XXX 1 Col per byte assumption */
		max = last_input_screen->co - (LOGICAL_CURSOR - INPUT_VISIBLEPOS);
		if (SHOWING_PROMPT)
			max -= INPUT_PROMPT_LEN;

		term_echo(do_echo);
		safe_puts(&(THIS_CHAR), max, do_echo);
		term_clear_to_eol();
		update = UPDATE_JUST_CURSOR;
	}

	/*
	 * If we're just supposed to move the cursor back to the input
	 * line, then go ahead and do that.
	 */
	if (update == UPDATE_JUST_CURSOR)
	{
		term_move_cursor(PHYSICAL_CURSOR, INPUT_LINE);
		cursor_not_in_display(last_input_screen);
		update = 0;
	}

	/*
	 * Turn the terminal echo back on, and flush all of the output
	 * we may have done here.
	 */
	term_echo(1);
	term_flush();

	/* XXXX HIDEOUS! Ick! Eww! */
	if (which_screen && (Screen *)which_screen == ns)
		break;
    }
    last_input_screen = os;
    current_window = saved_current_window;
}


void 	change_input_prompt (int direction)
{
	/* XXXXXX THIS is totaly wrong. XXXXXX */
	if (!last_input_screen->promptlist)
	{
		strlcpy(INPUT_BUFFER, last_input_screen->saved_input_buffer, 
						sizeof INPUT_BUFFER);
		LOGICAL_CURSOR = last_input_screen->saved_buffer_pos;
		*last_input_screen->saved_input_buffer = 0;
		last_input_screen->saved_buffer_pos = 0;
	}

	else if (direction == -1)
		;

	else if (!last_input_screen->promptlist->next)
	{
		strlcpy(last_input_screen->saved_input_buffer, INPUT_BUFFER, 
						sizeof INPUT_BUFFER);
		last_input_screen->saved_buffer_pos = LOGICAL_CURSOR;
		*INPUT_BUFFER = 0;
		LOGICAL_CURSOR = 0;
	}

	update_input(last_input_screen, UPDATE_ALL);
}

/* input_move_cursor: moves the cursor left or right... got it? */
void	input_move_cursor (int dir)
{
	if (dir > 0)
	{
		while (dir-- > 0)
			if (THIS_CHAR)
				CURSOR_RIGHT;
	}
	else if (dir < 0)
	{
		while (dir++ < 0) 
			if (LOGICAL_CURSOR > 0)
				CURSOR_LEFT;
	}
}

/*
 * set_input: sets the input buffer to the given string, discarding whatever
 * was in the input buffer before 
 */
void	set_input (const char *str)
{
	strlcpy(INPUT_BUFFER, str, INPUT_BUFFER_SIZE);
	LOGICAL_CURSOR = strlen(INPUT_BUFFER);
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
	LOGICAL_CURSOR = 0;
}

/* get_input_prompt: returns the current input_prompt */
char *	get_input_prompt (void)
{ 
	return input_prompt;
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


#define WHITESPACE(x) (isspace(x) || ispunct(x))

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
	cursor_to_input();

	/* Move to the end of the current word to the whitespace */
	while (THIS_CHAR && !WHITESPACE(THIS_CHAR))
		input_move_cursor(1);

	/* Move past the whitespace to the start of the next word */
	while (THIS_CHAR && WHITESPACE(THIS_CHAR))
		input_move_cursor(1);

	update_input(last_input_screen, UPDATE_JUST_CURSOR);
}

/* input_backward_word: move the cursor left on word in the input line */
BUILT_IN_KEYBINDING(input_backward_word)
{
	cursor_to_input();

	if (LOGICAL_CURSOR > 0)		/* Whatever */
	{
		/* If already at the start of a word, move back a position */
		if (!WHITESPACE(THIS_CHAR) && WHITESPACE(PREV_CHAR))
			input_move_cursor(-1);

		/* Move to the start of the current whitespace */
		while ((LOGICAL_CURSOR > 0) && WHITESPACE(THIS_CHAR))
			input_move_cursor(-1);

		/* Move to the start of the current word */
		while ((LOGICAL_CURSOR > 0) && !WHITESPACE(THIS_CHAR))
			input_move_cursor(-1);

		/* If we overshot our goal, then move forward */
		/* But NOT if at start of input line! (July 6th, 1999) */
		if ((LOGICAL_CURSOR > 0) && WHITESPACE(THIS_CHAR))
			input_move_cursor(1);
	}

	update_input(last_input_screen, UPDATE_JUST_CURSOR);
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
	ov_strcpy(&THIS_CHAR, &NEXT_CHAR);
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
	LOGICAL_CURSOR = 0;
	update_input(last_input_screen, UPDATE_JUST_CURSOR);
}

/*
 * input_end_of_line: moves the input cursor to the last character in the
 * input buffer 
 */
BUILT_IN_KEYBINDING(input_end_of_line)
{
	LOGICAL_CURSOR = strlen(INPUT_BUFFER);
	update_input(last_input_screen, UPDATE_JUST_CURSOR);
}

/*
 * This removes every character from the 'anchor' position to the current
 * position from the input line, and puts it into the cut buffer.  It does
 * the requisite redraw as well.
 */
static void	cut_input (int anchor)
{
	char *	buffer;
	size_t	size;

	if (anchor < LOGICAL_CURSOR)
	{
		size = LOGICAL_CURSOR - anchor;
		buffer = alloca(size + 1);
		strlcpy(buffer, &INPUT_BUFFER[anchor], size + 1);
		SET_CUT_BUFFER(buffer);

		buffer = LOCAL_COPY(&THIS_CHAR);
		INPUT_BUFFER[anchor] = 0;
		ADD_TO_INPUT(buffer);
		LOGICAL_CURSOR = anchor;
	}
	else
	{
		size = anchor - LOGICAL_CURSOR;
		buffer = alloca(size + 1);
		strlcpy(buffer, &THIS_CHAR, size + 1);
		SET_CUT_BUFFER(buffer);

		buffer = LOCAL_COPY(&INPUT_BUFFER[anchor]);
		THIS_CHAR = 0;
		ADD_TO_INPUT(buffer);
	}

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
		input_move_cursor(-1);
	cut_input(anchor);
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
	cut_input(anchor);
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
	cut_input(anchor);
}

/*
 * input_add_character: adds the character c to the input buffer, repecting
 * the current overwrite/insert mode status, etc 
 */
BUILT_IN_KEYBINDING(input_add_character)
{
	/* Don't permit the input buffer to get too big. */
	if (LOGICAL_CURSOR >= INPUT_BUFFER_SIZE)
		return;

	/*
	 * If we are NOT at the end of the line, and we're inserting
	 * then insert the char (expensive!)
	 */
	if (THIS_CHAR && get_int_var(INSERT_MODE_VAR))
	{
		char *ptr = LOCAL_COPY(&(THIS_CHAR));
		THIS_CHAR = (unsigned char)key;
		NEXT_CHAR = 0;
		ADD_TO_INPUT(ptr);
	}

	/*
	 * Otherwise, we're either at the end of the input buffer,
	 * or we're in overstrike mode.
	 */
	else
	{
		if (THIS_CHAR == 0)
			NEXT_CHAR = 0;
		THIS_CHAR = (unsigned char)key;
	}

	update_input(last_input_screen, UPDATE_FROM_CURSOR);
	input_move_cursor(1);
	update_input(last_input_screen, UPDATE_JUST_CURSOR);
}

/* input_clear_to_eol: erases from the cursor to the end of the input buffer */
BUILT_IN_KEYBINDING(input_clear_to_eol)
{
	/* This doesnt really speak to the implementation, but it works.  */
	SET_CUT_BUFFER(&THIS_CHAR);
	THIS_CHAR = 0;
	update_input(last_input_screen, UPDATE_FROM_CURSOR);
}

/*
 * input_clear_to_bol: clears from the cursor to the beginning of the input
 * buffer 
 */
BUILT_IN_KEYBINDING(input_clear_to_bol)
{
	char	c = THIS_CHAR;
	char	*copy;

	THIS_CHAR = 0;
	SET_CUT_BUFFER(INPUT_BUFFER);
	THIS_CHAR = c;

	copy = LOCAL_COPY(&THIS_CHAR);
	set_input(copy);

	/*
	 * set_input() leaves the cursor at the end of the new line,
	 * so we have to move it back.
	 */
	LOGICAL_CURSOR = 0;
	update_input(last_input_screen, UPDATE_JUST_CURSOR);
}

/*
 * input_clear_line: clears entire input line
 */
BUILT_IN_KEYBINDING(input_clear_line)
{
	/* Only copy if there is input. -wd */
	if (*INPUT_BUFFER)
		SET_CUT_BUFFER(INPUT_BUFFER);

	*INPUT_BUFFER = 0;
	LOGICAL_CURSOR = 0;
	update_input(last_input_screen, UPDATE_FROM_CURSOR);
}

/*
 * input_reset_line: clears entire input line, suitable for use in tabscripts
 * This does not mangle the cutbuffer, so you can use it to replace the input
 * line w/o any deleterious effects!
 */
BUILT_IN_KEYBINDING(input_reset_line)
{
	*INPUT_BUFFER = 0;
	LOGICAL_CURSOR = 0;

	if (!string)
		set_input(empty_string);
	else
		set_input(string);	/* This calls update_input() */
}


/*
 * input_transpose_characters: move the character before the cursor to
 * the position after the cursor.
 */
BUILT_IN_KEYBINDING(input_transpose_characters)
{
	unsigned char	this_char, prev_char;

	/* If we're at the end of input, move back to the last char */
	if (!THIS_CHAR && LOGICAL_CURSOR > 1)
		input_move_cursor(-1);

	/* Do nothing if there are not two characters to swap. */
	if (LOGICAL_CURSOR < 2)
		return;

	/* Swap the character under cursor with character before */
	this_char = THIS_CHAR;
	prev_char = PREV_CHAR;
	THIS_CHAR = prev_char;
	PREV_CHAR = this_char;

	/* Then redraw ahoy! */
	update_input(last_input_screen, UPDATE_FROM_CURSOR);
}


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

	if (!CUT_BUFFER)
		return;

	ptr = LOCAL_COPY(&THIS_CHAR);
	THIS_CHAR = 0;
	ADD_TO_INPUT(CUT_BUFFER);
	ADD_TO_INPUT(ptr);
	update_input(last_input_screen, UPDATE_FROM_CURSOR);

	input_move_cursor(strlen(cut_buffer));
	if (LOGICAL_CURSOR > INPUT_BUFFER_SIZE)
		LOGICAL_CURSOR = INPUT_BUFFER_SIZE;
	update_input(last_input_screen, UPDATE_JUST_CURSOR);
}


/* used with input_move_cursor */
#define RIGHT 1
#define LEFT 0

/* BIND functions: */
BUILT_IN_KEYBINDING(forward_character)
{
	input_move_cursor(1);
	update_input(last_input_screen, UPDATE_JUST_CURSOR);
}

BUILT_IN_KEYBINDING(backward_character)
{
	input_move_cursor(-1);
	update_input(last_input_screen, UPDATE_JUST_CURSOR);
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
	LOGICAL_CURSOR = 0;
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

	if (last_input_screen->promptlist && 
		last_input_screen->promptlist->type == WAIT_PROMPT_LINE)
	{
		WaitPrompt *	OldPrompt;

		OldPrompt = last_input_screen->promptlist;
		last_input_screen->promptlist = OldPrompt->next;
		(*OldPrompt->func)(OldPrompt->data, line);
		new_free(&OldPrompt->data);
		new_free(&OldPrompt->prompt);
		new_free((char **)&OldPrompt);
		change_input_prompt(-1);
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
BUILT_IN_KEYBINDING(clear_screen)
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

/* type_text: the BIND function TYPE_TEXT */
BUILT_IN_KEYBINDING(type_text)
{
	for (; string && *string; string++)
		input_add_character(*string, NULL);
}

/* parse_text: the bindable function that executes its string */
BUILT_IN_KEYBINDING(parse_text)
{
	int	old = system_exception;

	if (string)
		runcmds(string, empty_string);
	system_exception = old;
}

