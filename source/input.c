/*
 * input.c: does the actual input line stuff... keeps the appropriate stuff
 * on the input line, handles insert/delete of characters/words... the whole
 * ball o wax.
 *
 * This file has been mostly rewritten and reorganized by now.  One of the
 * things we can do now is have completely independant input lines on each
 * screen, and there are really no global variables any more.  A lot of the
 * original code still lies about, as its still useful, but the macros have
 * made the original code hard to distinguish.
 *
 * Copyright 1990 Michael Sandrof
 * Copyright 1999 EPIC Software Labs
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 */

#define __need_term_flush__
#include "irc.h"
#include "alias.h"
#include "commands.h"
#include "exec.h"
#include "history.h"
#include "hook.h"
#include "input.h"
#include "ircaux.h"
#include "keys.h"
#include "screen.h"
#include "server.h"
#include "status.h"
#include "term.h"
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

/* 
 * These are sanity macros.  The file was completely unreadable before 
 * I put these in here.  I make no apologies for them.
 */
#define current_screen		last_input_screen
#define INPUT_CURSOR 		current_screen->input_cursor
#define INPUT_BUFFER 		current_screen->input_buffer
#define MIN_POS 		current_screen->buffer_min_pos
#define THIS_POS 		current_screen->buffer_pos
#define THIS_CHAR 		INPUT_BUFFER[THIS_POS]
#define MIN_CHAR 		INPUT_BUFFER[MIN_POS]
#define PREV_CHAR 		INPUT_BUFFER[THIS_POS-1]
#define NEXT_CHAR 		INPUT_BUFFER[THIS_POS+1]
#define ADD_TO_INPUT(x) 	strmcat(INPUT_BUFFER, (x), INPUT_BUFFER_SIZE);
#define INPUT_ONSCREEN 		current_screen->input_visible
#define INPUT_VISIBLE 		INPUT_BUFFER[INPUT_ONSCREEN]
#define ZONE			current_screen->input_zone_len
#define START_ZONE 		current_screen->input_start_zone
#define END_ZONE 		current_screen->input_end_zone
#define INPUT_PROMPT 		current_screen->input_prompt
#define INPUT_PROMPT_LEN 	current_screen->input_prompt_len
#define INPUT_LINE 		current_screen->input_line
#define CUT_BUFFER		cut_buffer
#define SET_CUT_BUFFER(x)	malloc_strcpy(&CUT_BUFFER, x);

#define BUILT_IN_KEYBINDING(x) void x (char key, char *string)


/* XXXX Only used here anyhow XXXX */
static int 	safe_puts (char *str, int len, int echo) 
{
	int i = 0;

	while (*str && i < len)
	{
		term_putchar(*str);
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
		if (screen->alive && is_cursor_in_display(screen))
		{
			output_screen = screen;
			last_input_screen = screen;
			term_move_cursor(INPUT_CURSOR, INPUT_LINE);
			term_flush();
			cursor_not_in_display(screen);
		}
	}
	output_screen = last_input_screen = oldscreen;
}

/*
 * update_input: does varying amount of updating on the input line depending
 * upon the position of the cursor and the update flag.  If the cursor has
 * move toward one of the edge boundaries on the screen, update_cursor()
 * flips the input line to the next (previous) line of text. The update flag
 * may be: 
 *
 * NO_UPDATE - only do the above bounds checking. 
 *
 * UPDATE_JUST_CURSOR - do bounds checking and position cursor where is should
 * be. 
 *
 * UPDATE_FROM_CURSOR - does all of the above, and makes sure everything from
 * the cursor to the right edge of the screen is current (by redrawing it). 
 *
 * UPDATE_ALL - redraws the entire line 
 */
void	update_input (int update)
{
	int	old_zone;
	char	*ptr, *ptr_free;
	int	len,
		free_it = 0,
		max;
	char	*prompt;
	int	echo = 1;
	Screen	*os = last_input_screen;
	Screen	*ns;
	Window	*saved_current_window = current_window;

	/*
	 * No input line in dumb or bg mode.
	 */
	if (dumb_mode || !foreground)
		return;

  for (ns = screen_list; ns; ns = ns->next)
  {
	if (!ns->alive)
		continue;	/* It's dead, Jim! */

	last_input_screen = ns;
	current_window = ns->current_window;

	/*
	 * Make sure the client thinks the cursor is on the input line.
	 */
	cursor_to_input();

	/*
	 * See if we're in a add_wait_prompt() call.  If we are, grab that
	 * current prompt, otherwise use the default input prompt.
	 */
	if (last_input_screen->promptlist)
		prompt = last_input_screen->promptlist->prompt;
	else
		prompt = input_prompt;

	/*
	 *
	 * GET THE INPUT PROMPT
	 *
	 */

	/*
	 * If we have a prompt, and we're supposed to update the input
	 * prompt, then we do need to expand the prompt.
	 */
	if (prompt && update != NO_UPDATE)
	{
		int	af;

		/*
		 * If the current window is query'ing an exec'd process,
		 * then we just get the current prompt for that process.
		 * Note that it is not malloced.
		 */
		if (is_valid_process(get_target_by_refnum(0)) != -1)
			ptr = get_prompt_by_refnum(0);

		/*
		 * Otherwise, we just expand the prompt as normal.
		 */
		else
		{
			ptr = expand_alias(prompt, empty_string, &af, NULL);
			free_it = 1;
		}

		/*
		 * If we're in an add_wait_prompt(), we see whether or not
		 * this is an "invisible" prompt.  If it is, we turn off the
		 * echo so what the user types doesnt show up.
		 */
		if (last_input_screen->promptlist)
			term_echo(last_input_screen->promptlist->echo);

		/*
		 * Mangle out any ansi chars or so forth.
		 */
		ptr_free = ptr;
		ptr = normalize_string(ptr, 0);	/* This should be ok */
		if (free_it)
			new_free(&ptr_free);
		free_it = 1;

		/*
		 * If the prompt has changed, or if there is no prompt...
		 */
		if (	(ptr && !INPUT_PROMPT) ||
			(!ptr && INPUT_PROMPT) ||
			strcmp(ptr, INPUT_PROMPT)	)
		{
			if (last_input_screen->input_prompt_malloc)
				new_free(&INPUT_PROMPT);

			last_input_screen->input_prompt_malloc = free_it;
			INPUT_PROMPT = ptr;
			INPUT_PROMPT_LEN = output_with_count(INPUT_PROMPT, 0, 0);
			update = UPDATE_ALL;
		}
		/*
		 * Prompt didnt change, so clean up our mess
		 */
		else
		{
			if (free_it)
				new_free(&ptr);
		}
	}


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

		START_ZONE = WIDTH;
		END_ZONE = last_input_screen->co - WIDTH;

		last_input_screen->old_co = last_input_screen->co;
		last_input_screen->old_li = last_input_screen->li;
	}

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
	START_ZONE = ((INPUT_PROMPT_LEN + THIS_POS - WIDTH) / ZONE) * ZONE + WIDTH;
	END_ZONE = START_ZONE + ZONE;

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
		INPUT_ONSCREEN = 0;
	else
		INPUT_ONSCREEN = START_ZONE - WIDTH - INPUT_PROMPT_LEN;

	/*
	 * And the cursor is simply how many characters away THIS_POS is
	 * from the first column on the screen.
	 */
	if (INPUT_ONSCREEN == 0)
		INPUT_CURSOR = INPUT_PROMPT_LEN + THIS_POS;
	else
		INPUT_CURSOR = THIS_POS - INPUT_ONSCREEN;

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
		if (INPUT_ONSCREEN == 0 && INPUT_PROMPT && *INPUT_PROMPT)
		{
			/*
			 * Forcibly turn on echo.
			 */
			int	echo = term_echo(1);

			/*
			 * Crop back the input prompt so it does not extend
			 * past the end of the zone.
			 */
			if (INPUT_PROMPT_LEN > (last_input_screen->co - WIDTH))
				INPUT_PROMPT_LEN = last_input_screen->co - WIDTH - 1;

			/*
			 * Output the prompt.
			 */
			output_with_count(INPUT_PROMPT, 0, 1);

			/*
			 * Turn the echo back to what it was before,
			 * and output the rest of the input buffer.
			 */
			term_echo(echo);
			safe_puts(INPUT_BUFFER, last_input_screen->co - INPUT_PROMPT_LEN, echo);
		}

		/*
		 * Otherwise we just output whatever we have.
		 */
		else if (echo)
			safe_puts(&(INPUT_VISIBLE), last_input_screen->co, echo);

		/*
		 * Clear the rest of the input line and reset the cursor
		 * to the current input position.
		 */
		term_clear_to_eol();
		term_move_cursor(INPUT_CURSOR, INPUT_LINE);
	}

	/*
	 * If we're just supposed to refresh whats to the right of the
	 * current logical position...
	 */
	else if (update == UPDATE_FROM_CURSOR)
	{
		/*
		 * Move the cursor to where its supposed to be,
		 * Figure out how much we can output from here,
		 * and then output it.
		 */
		term_move_cursor(INPUT_CURSOR, INPUT_LINE);
		max = last_input_screen->co - (THIS_POS - INPUT_ONSCREEN);
		if (INPUT_ONSCREEN == 0 && INPUT_PROMPT && *INPUT_PROMPT)
			max -= INPUT_PROMPT_LEN;

		if ((len = strlen(&(THIS_CHAR))) > max)
			len = max;
		safe_puts(&(THIS_CHAR), len, echo);
		term_clear_to_eol();
		term_move_cursor(INPUT_CURSOR, INPUT_LINE);
	}

	/*
	 * If we're just supposed to move the cursor back to the input
	 * line, then go ahead and do that.
	 */
	else if (update == UPDATE_JUST_CURSOR)
		term_move_cursor(INPUT_CURSOR, INPUT_LINE);

	/*
	 * Turn the terminal echo back on, and flush all of the output
	 * we may have done here.
	 */
	term_echo(1);
	term_flush();
    }
    last_input_screen = os;
    current_window = saved_current_window;
}


void 	change_input_prompt (int direction)
{
	/* XXXXXX THIS is totaly wrong. XXXXXX */
	if (!last_input_screen->promptlist)
	{
		strcpy(INPUT_BUFFER, last_input_screen->saved_input_buffer);
		THIS_POS = last_input_screen->saved_buffer_pos;
		MIN_POS = last_input_screen->saved_min_buffer_pos;
		*last_input_screen->saved_input_buffer = 0;
		last_input_screen->saved_buffer_pos = 0;
		last_input_screen->saved_min_buffer_pos = 0;
	}

	else if (direction == -1)
		;

	else if (!last_input_screen->promptlist->next)
	{
		strcpy(last_input_screen->saved_input_buffer, INPUT_BUFFER);
		last_input_screen->saved_buffer_pos = THIS_POS;
		last_input_screen->saved_min_buffer_pos = MIN_POS;
		*INPUT_BUFFER = 0;
		THIS_POS = MIN_POS = 0;
	}

	update_input(UPDATE_ALL);
}

/* input_move_cursor: moves the cursor left or right... got it? */
void	input_move_cursor (int dir)
{
	cursor_to_input();
	if (dir)
	{
		if (THIS_CHAR)
		{
			THIS_POS++;
			term_cursor_right();
		}
	}
	else
	{
		if (THIS_POS > MIN_POS)
		{
			THIS_POS--;
			term_cursor_left();
		}
	}
	update_input(NO_UPDATE);
}

/*
 * set_input: sets the input buffer to the given string, discarding whatever
 * was in the input buffer before 
 */
void	set_input (char *str)
{
	strmcpy(INPUT_BUFFER + MIN_POS, str, INPUT_BUFFER_SIZE - MIN_POS);
	THIS_POS = strlen(INPUT_BUFFER);
}

/*
 * get_input: returns a pointer to the input buffer.  Changing this will
 * actually change the input buffer.  This is a bad way to change the input
 * buffer tho, cause no bounds checking won't be done 
 */
char *	get_input (void)
{
	return &MIN_CHAR;
}

/* init_input: initialized the input buffer by clearing it out */
void	init_input (void)
{
	*INPUT_BUFFER = 0;
	THIS_POS = MIN_POS;
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
void	set_input_prompt (char *prompt)
{
	if (prompt)
		malloc_strcpy(&input_prompt, prompt);
	else if (input_prompt)
		malloc_strcpy(&input_prompt, empty_string);
	else
		return;

	update_input(UPDATE_ALL);
}


#define WHITESPACE(x) (my_isspace(x) || ispunct(x))

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
		THIS_POS++;

	/* Move past the whitespace to the start of the next word */
	while (THIS_CHAR && WHITESPACE(THIS_CHAR))
		THIS_POS++;

	update_input(UPDATE_JUST_CURSOR);
}

/* input_backward_word: move the cursor left on word in the input line */
BUILT_IN_KEYBINDING(input_backward_word)
{
	cursor_to_input();

	if (THIS_POS > MIN_POS)		/* Whatever */
	{
		/* If already at the start of a word, move back a position */
		if (!WHITESPACE(THIS_CHAR) && WHITESPACE(PREV_CHAR))
			THIS_POS--;

		/* Move to the start of the current whitespace */
		while ((THIS_POS > MIN_POS) && WHITESPACE(THIS_CHAR))
			THIS_POS--;

		/* Move to the start of the current word */
		while ((THIS_POS > MIN_POS) && !WHITESPACE(THIS_CHAR))
			THIS_POS--;

		/* If we overshot our goal, then move forward */
		/* But NOT if at start of input line! (July 6th, 1999) */
		if ((THIS_POS > MIN_POS) && WHITESPACE(THIS_CHAR))
			THIS_POS++;
	}

	update_input(UPDATE_JUST_CURSOR);
}

static void	input_delete_char_from_screen (void)
{
	/*
	 * Remove the current character from the screen's display.
	 *
	 * If we cannot do a character delete then we do a wholesale
	 * redraw of the input line (ugh). 
	 */
	if (!(termfeatures & TERM_CAN_DELETE))
		update_input(UPDATE_FROM_CURSOR);
	else
	{
		int	pos;

		/*
		 * Delete the character.  This is the simple part.
		 */
		term_delete(1);

		/*
		 * So right now we have a blank space at the right of the
		 * screen.  If there is a character in the input buffer that
		 * is out in that position, we need to find it and display it.
		 */
		if (INPUT_ONSCREEN == 0)		/* UGH! */
			pos = last_input_screen->co - INPUT_PROMPT_LEN - 1;
		else
			pos = INPUT_ONSCREEN + last_input_screen->co - 1;

		if (pos < strlen(INPUT_BUFFER))
		{
			term_move_cursor(last_input_screen->co - 1, INPUT_LINE);
			term_putchar(INPUT_BUFFER[pos]);
			term_move_cursor(INPUT_CURSOR, INPUT_LINE);
		}

		/* XXX - Very possibly, this is pointless */
		update_input(NO_UPDATE);
	}
}

/*
 * input_delete_character -- Deletes the character currently under the
 * 			     input cursor.
 */
BUILT_IN_KEYBINDING(input_delete_character)
{
	cursor_to_input();

	/*
	 * If we are at the end of the input buffer, the delete key has
	 * no effect.
	 */
	if (!THIS_CHAR)
		return;

	/* 
	 * Remove the current character from the logical buffer
	 * and also from the screen.
	 */
	ov_strcpy(&THIS_CHAR, &NEXT_CHAR);
	input_delete_char_from_screen();
}


/*
 * input_backspace -- Basically a combination of backward_character and
 *		      delete_character.  No, this is not significantly
 *		      more expensive than the old way.
 */
BUILT_IN_KEYBINDING(input_backspace)
{
	cursor_to_input();

	/* Only works when not at start of the input buffer */
	if (THIS_POS > MIN_POS)
	{
		/*
		 * Back up the logical buffer cursor, the logical screen
		 * cursor, the real screen cursor, and then do a delete.
		 */
		backward_character(0, NULL);
		input_delete_character(0, NULL);
	}
}

/*
 * input_beginning_of_line: moves the input cursor to the first character in
 * the input buffer 
 */
BUILT_IN_KEYBINDING(input_beginning_of_line)
{
	cursor_to_input();
	THIS_POS = MIN_POS;
	update_input(UPDATE_JUST_CURSOR);
}

/*
 * input_end_of_line: moves the input cursor to the last character in the
 * input buffer 
 */
BUILT_IN_KEYBINDING(input_end_of_line)
{
	cursor_to_input();
	THIS_POS = strlen(INPUT_BUFFER);
	update_input(UPDATE_JUST_CURSOR);
}

/*
 * This removes every character from the 'anchor' position to the current
 * position from the input line, and puts it into the cut buffer.  It does
 * the requisite redraw as well.
 */
void	cut_input (int anchor)
{
	char *	buffer;
	int	size;

	if (anchor < THIS_POS)
	{
		buffer = alloca((size = THIS_POS - anchor) + 1);
		strmcpy(buffer, &INPUT_BUFFER[anchor], size);
		malloc_strcpy(&cut_buffer, buffer);

		buffer = LOCAL_COPY(&THIS_CHAR);
		INPUT_BUFFER[anchor] = 0;
		ADD_TO_INPUT(buffer);
		THIS_POS = anchor;
	}
	else
	{
		buffer = alloca((size = anchor - THIS_POS) + 1);
		strmcpy(buffer, &THIS_CHAR, size);
		malloc_strcpy(&cut_buffer, buffer);

		buffer = LOCAL_COPY(&INPUT_BUFFER[anchor]);
		THIS_CHAR = 0;
		ADD_TO_INPUT(buffer);
	}

	update_input(UPDATE_ALL);
}

/*
 * To visualize:
 *
 *		This is the input buffer
 *			  ^			(the input cursor)
 * orig_pos points to the 'e'.
 * c is an 'e'.
 * THIS_POS is moved to the space between the 's' and the 't'.
 * The input buffer is changed to:
 *
 *		This is e input buffer
 *			^			(the input cursor)
 */
BUILT_IN_KEYBINDING(input_delete_to_previous_space)
{
	int	anchor;

	cursor_to_input();

	if (THIS_POS <= MIN_POS)
		return;

	anchor = THIS_POS;
	while (THIS_POS > MIN_POS && !my_isspace(PREV_CHAR))
		backward_character(0, NULL);
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

	if (THIS_POS <= MIN_POS)
		return;

	anchor = THIS_POS;
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

	anchor = THIS_POS;
	input_forward_word(0, NULL);
	cut_input(anchor);
}

/*
 * input_add_character: adds the character c to the input buffer, repecting
 * the current overwrite/insert mode status, etc 
 */
BUILT_IN_KEYBINDING(input_add_character)
{
	int	display_flag = NO_UPDATE;

	cursor_to_input();

	if (last_input_screen->promptlist)
		term_echo(last_input_screen->promptlist->echo);

	/* Don't permit the input buffer to get too big. */
	if (THIS_POS >= INPUT_BUFFER_SIZE)
	{
		term_echo(1);
		return;
	}

	/* XXX Is a get_int_var really neccesary here? */
	if (get_int_var(INSERT_MODE_VAR))
	{
		/*
		 * We are at NOT at the end of the input line
		 */
		if (THIS_CHAR)
		{
			char	*ptr;

			/*
			 * Add to logical buffer
			 */
			ptr = LOCAL_COPY(&(THIS_CHAR));
			THIS_CHAR = key;
			NEXT_CHAR = 0;
			ADD_TO_INPUT(ptr);

			/*
			 * Add to display screen
			 */
			if (termfeatures & TERM_CAN_INSERT)
				term_insert(key);
			else
			{
				term_putchar(key);
				if (NEXT_CHAR)
					display_flag = UPDATE_FROM_CURSOR;
				else
					display_flag = NO_UPDATE;
			}
		}
		else
		{
			/*
			 * Add to logical buffer
			 */
			THIS_CHAR = key;
			NEXT_CHAR = 0;

			/* Add to display screen */
			term_putchar(key);
		}
	}

	/* Overstrike mode.  Much simpler. */
	else
	{
		if (THIS_CHAR == 0)
			NEXT_CHAR = 0;
		THIS_CHAR = key;
		term_putchar(key);
	}

	THIS_POS++;
	update_input(display_flag);
	term_echo(1);
}

/* input_clear_to_eol: erases from the cursor to the end of the input buffer */
BUILT_IN_KEYBINDING(input_clear_to_eol)
{
	/* This doesnt really speak to the implementation, but it works.  */
	cursor_to_input();
	malloc_strcpy(&cut_buffer, &THIS_CHAR);
	THIS_CHAR = 0;
	term_clear_to_eol();
	update_input(NO_UPDATE);
}

/*
 * input_clear_to_bol: clears from the cursor to the beginning of the input
 * buffer 
 */
BUILT_IN_KEYBINDING(input_clear_to_bol)
{
	char	c = THIS_CHAR;

	cursor_to_input();

	THIS_CHAR = 0;
	malloc_strcpy(&cut_buffer, &MIN_CHAR);
	THIS_CHAR = c;

	set_input(LOCAL_COPY(&THIS_CHAR));
	THIS_POS = MIN_POS;
	term_move_cursor(INPUT_PROMPT_LEN, INPUT_LINE);
	term_clear_to_eol();
	update_input(UPDATE_FROM_CURSOR);
}

/*
 * input_clear_line: clears entire input line
 */
BUILT_IN_KEYBINDING(input_clear_line)
{
	cursor_to_input();

	malloc_strcpy(&cut_buffer, INPUT_BUFFER + MIN_POS);
	MIN_CHAR = 0;
	THIS_POS = MIN_POS;
	term_move_cursor(INPUT_PROMPT_LEN, INPUT_LINE);
	term_clear_to_eol();
	update_input(NO_UPDATE);
	if (get_int_var(HISTORY_CIRCLEQ_VAR))
		abort_history_browsing(0);
}

/*
 * input_transpose_characters: swaps the positions of the two characters
 * before the cursor position 
 */
BUILT_IN_KEYBINDING(input_transpose_characters)
{
	cursor_to_input();
	if (last_input_screen->buffer_pos > MIN_POS)
	{
		u_char	c1, c2;
		int	pos, end_of_line = 0;

		/*
		 * If we're in the middle of the input buffer,
		 * swap the character the cursor is on and the
		 * character before it
		 */
		if (THIS_CHAR)
			pos = THIS_POS;

		/*
		 * Else if the input buffer has at least two
		 * characters in it (and implicity we're at the end)
		 * then swap the two characters before the cursor
		 * XXX This strlen() is probably unnecesary.
		 */
		else if (strlen(INPUT_BUFFER) > MIN_POS + 2)
		{
			pos = THIS_POS - 1;
			end_of_line = 1;
		}

		/*
		 * Without two characters to swap, do nothing
		 */
		else
			return;


		/*
		 * Swap the two characters
		 */
		c1 = INPUT_BUFFER[pos];
		c2 = INPUT_BUFFER[pos] = INPUT_BUFFER[pos - 1];
		INPUT_BUFFER[pos - 1] = c1;

		/*
		 * Adjust the cursor and output the new chars.
		 */
		term_cursor_left();
		if (end_of_line)
			term_cursor_left();
		term_putchar(c1);
		term_putchar(c2);

		/*
		 * Move the cursor back onto 'c2', if we're not at
		 * the end of the input line.
		 */
		if (!end_of_line)
			term_cursor_left();

		/*
		 * Reset the internal cursor.
		 */
		update_input(NO_UPDATE);
	}
}


BUILT_IN_KEYBINDING(refresh_inputline)
{
	update_input(UPDATE_ALL);
}

/*
 * input_yank_cut_buffer: takes the contents of the cut buffer and inserts it
 * into the input line 
 */
BUILT_IN_KEYBINDING(input_yank_cut_buffer)
{
	char	*ptr = NULL;

	if (!cut_buffer)
		return;

	ptr = LOCAL_COPY(&THIS_CHAR);
	THIS_CHAR = 0;
	ADD_TO_INPUT(cut_buffer);
	ADD_TO_INPUT(ptr);
	update_input(UPDATE_FROM_CURSOR);

	THIS_POS += strlen(cut_buffer);
	if (THIS_POS > INPUT_BUFFER_SIZE)
		THIS_POS = INPUT_BUFFER_SIZE;
	update_input(UPDATE_JUST_CURSOR);
}


/* used with input_move_cursor */
#define RIGHT 1
#define LEFT 0

/* BIND functions: */
BUILT_IN_KEYBINDING(forward_character)
{
	input_move_cursor(RIGHT);
}

BUILT_IN_KEYBINDING(backward_character)
{
	input_move_cursor(LEFT);
}

BUILT_IN_KEYBINDING(backward_history)
{
	get_history(OLDER);		/* Cursor up -- older -- prev */
}

BUILT_IN_KEYBINDING(forward_history)
{
	get_history(NEWER);		/* Cursor down -- newer -- next */
}

BUILT_IN_KEYBINDING(toggle_insert_mode)
{
	set_var_value(INSERT_MODE_VAR, "TOGGLE");
}

BUILT_IN_KEYBINDING(send_line)
{
	int	server = from_server;
	char *	line = LOCAL_COPY(get_input());

	from_server = get_window_server(0);
	unhold_a_window(last_input_screen->current_window);
	MIN_CHAR = 0;
	THIS_POS = MIN_POS;

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
		/* Clear the input line before dispatching the command */
		update_input(UPDATE_ALL);

		if (do_hook(INPUT_LIST, "%s", line))
		{
			if (get_int_var(INPUT_ALIASES_VAR))
				parse_line(NULL, line, empty_string, 1, 0);
			else
				parse_line(NULL, line, NULL, 1, 0);
		}
	}

	from_server = server;
}



BUILT_IN_KEYBINDING(quote_char)
{
	last_input_screen->quote_hit = 1;
}

/* 
 * These four functions are boomerang functions, which allow the highlight
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
	yell("alt_tog!");
	input_add_character(ALT_TOG, string);
}

/* type_text: the BIND function TYPE_TEXT */
BUILT_IN_KEYBINDING(type_text)
{
	for (; *string; string++)
		input_add_character(*string, empty_string);
}

/*
 * clear_screen: the CLEAR_SCREEN function for BIND.  Clears the screen and
 * starts it if it is held 
 */
BUILT_IN_KEYBINDING(clear_screen)
{
	hold_mode(NULL, OFF, 1);
	clear_window_by_refnum(0);
}

BUILT_IN_KEYBINDING(input_unclear_screen)
{
	hold_mode(NULL, OFF, 1);
	unclear_window_by_refnum(0);
}

/* parse_text: the bindable function that executes its string */
BUILT_IN_KEYBINDING(parse_text)
{
	parse_line(NULL, string, empty_string, 0, 0);
}

BUILT_IN_KEYBINDING(cpu_saver_on)
{
	cpu_saver = 1;
	update_all_status();
}


/*
 * edit_char: handles each character for an input stream.  Not too difficult
 * to work out.
 */
void	edit_char (u_char key)
{
	void		(*func) 	(char, char *) = NULL;
	char *		ptr = NULL;
	u_char		extended_key;
	WaitPrompt *	oldprompt;
	u_char		dummy[2];
	int		xxx_return = 0;		/* XXXX Need i say more? */

	if (dumb_mode)
	{
#ifdef TIOCSTI
		ioctl(0, TIOCSTI, &key);
#else
		say("Sorry, your system doesnt support 'faking' user input...");
#endif
		return;
	}

	/* were we waiting for a keypress? */
	if (last_input_screen->promptlist && 
		last_input_screen->promptlist->type == WAIT_PROMPT_KEY)
	{
		dummy[0] = key, dummy[1] = 0;
		oldprompt = last_input_screen->promptlist;
		last_input_screen->promptlist = oldprompt->next;
		(*oldprompt->func)(oldprompt->data, dummy);
		new_free(&oldprompt->data);
		new_free(&oldprompt->prompt);
		new_free((char **)&oldprompt);

		set_input(empty_string);
		change_input_prompt(-1);
		xxx_return = 1;
	}

	/* 
	 * This is only used by /pause to see when a keypress event occurs,
	 * but not to impact how that keypress is handled at all.
	 */
	if (last_input_screen->promptlist && 
		last_input_screen->promptlist->type == WAIT_PROMPT_DUMMY)
	{
		oldprompt = last_input_screen->promptlist;
		last_input_screen->promptlist = oldprompt->next;
		(*oldprompt->func)(oldprompt->data, NULL);
		new_free(&oldprompt->data);
		new_free(&oldprompt->prompt);
		new_free((char **)&oldprompt);
	}

	if (xxx_return)
		return;

	/* If the high bit is set, mangle it as neccesary. */
	if (key & 0x80)
	{
		if (current_term->TI_meta_mode)
		{
			edit_char('\033');
			key &= ~0x80;
		}
		else if (!term_eight_bit())
			key &= ~0x80;
	}

	extended_key = key;

	/* If we just hit the quote character, add this character literally */
	if (last_input_screen->quote_hit)
	{
		last_input_screen->quote_hit = 0;
		input_add_character(extended_key, empty_string);
	}

	/* Otherwise find out what its binding is and dispatch it. */
	else
	{
		int	m = last_input_screen->meta_hit;
		int	i;

		if ((i = get_binding(m, key, &func, &ptr)))
		{
			if (m == 4 && i == 4)
				last_input_screen->meta_hit = 0;
			else
				last_input_screen->meta_hit = i;
		}
		else if (last_input_screen->meta_hit != 4)
			last_input_screen->meta_hit = 0;
		if (func)
			func(extended_key, SAFE(ptr));
	}
}

/*
 * type: The TYPE command.  This parses the given string and treats each
 * character as though it were typed in by the user.  Thus key bindings 
 * are used for each character parsed.  Special case characters are control 
 * character sequences, specified by a ^ follow by a legal control key.  
 * Thus doing "/TYPE ^B" will be as tho ^B were hit at the keyboard, 
 * probably moving the cursor backward one character.
 *
 * This was moved from keys.c, because it certainly does not belong there,
 * and this seemed a reasonable place for it to go for now.
 */
BUILT_IN_COMMAND(type)
{
	int	c;
	char	key;

	for (; *args; args++)
	{
		if (*args == '^')
		{
			args++;
			if (*args == '?')
				key = '\177';
			else if (*args)
			{
				c = *args;
				if (islower(c))
					c = toupper(c);
				if (c < 64)
				{
					say("Invalid key sequence: ^%c", c);
					return;
				}
				key = c - 64;
			}
			else
				break;
		}
		else
		{
			if (*args == '\\')
				args++;
			if (*args)
				key = *args;
			else
				break;
		}

		edit_char(key);
	}
}

