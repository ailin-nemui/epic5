/*
 * screen.c
 *
 * Copyright (c) 1993-1996 Matthew Green.
 * Copyright 1998 J. Kean Johnston, used with permission
 * Copyright 1997, 2015 EPIC Software Labs.
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
 * This file includes major work contributed by FireClown (J. Kean Johnston), 
 * and I am indebted to him for the work he has graciously donated to the 
 * project.  Without his contributions, EPIC's robust handling of colors and 
 * escape sequences would never have been possible.
 */

#define __need_term_h__
#define __need_putchar_x__
#include "irc.h"
#include "alias.h"
#include "clock.h"
#include "exec.h"
#include "screen.h"
#include "window.h"
#include "output.h"
#include "vars.h"
#include "server.h"
#include "list.h"
#include "termx.h"
#include "names.h"
#include "ircaux.h"
#include "input.h"
#include "log.h"
#include "hook.h"
#include "dcc.h"
#include "status.h"
#include "commands.h"
#include "parse.h"
#include "newio.h"
#include <sys/ioctl.h>

#define CURRENT_WSERV_VERSION	4

#if 0
/*
 * When some code wants to override the default lastlog level, and needs
 * to have some output go into some explicit window (such as for /xecho -w),
 * then while to_window is set to some window, *ALL* output goes to that
 * window.  Dont forget to reset it to NULL when youre done!  ;-)
 */
	Window	*to_window;
#endif

/*
 * When all else fails, this is the screen that is attached to the controlling
 * terminal, and we know *that* screen will always be there.
 */
	Screen	*main_screen;

/*
 * This is the screen in which we last handled an input event.  This takes
 * care of the input duties that "current_screen" used to handle.
 */
	Screen	*last_input_screen;

/*
 * This is used to set the default output device for tputs_x().  This takes
 * care of the output duties that "current_screen" used to handle.  Since
 * the input screen and output screen are independant, it isnt impossible 
 * for a command in one screen to cause output in another.
 */
	Screen	*output_screen;

/*
 * The list of all the screens we're handling.  Under most cases, there's 
 * only one screen on the list, "main_screen".
 */
	Screen	*screen_list = NULL;

/*
 * How things output to the display get mangled (set via /set mangle_display)
 */
	int	display_line_mangler = 0;


/* * * * * * * * * * * * * OUTPUT CHAIN * * * * * * * * * * * * * * * * * * *
 * The front-end api to output stuff to windows is:
 *
 * 1) Set the window, either directly or indirectly:
 *     a) Directly with		l = message_setall(window, target, level);
 *     b) Indirectly with	l = message_from(target, level);
 * 2) Call an output routine:
 *	say(), output(), yell(), put_it(), put_echo(), etc.
 * 3) Reset the window:
 *     b) Indirectly with	pop_message_from(l);
 *
 * This file implements the middle part of the "ircII window", everything
 * that sits behind the say/output/yell/put_it/put_echo functions, and in
 * front of the low-level terminal stuff.
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
static void 	do_screens	(int fd);
static int 	rite 		(Window *, const unsigned char *);
static void 	scroll_window   (Window *);
static void 	add_to_window	(int, const unsigned char *);
static	int	ok_to_output	(Window *);
static	void	edit_codepoint (u_32int_t key);
static ssize_t read_esc_seq     (const unsigned char *, void *, int *);
static ssize_t read_color_seq   (const unsigned char *, void *d, int);
static ssize_t read_color256_seq  (const unsigned char *, void *d);
static void	destroy_prompt (Screen *s, WaitPrompt **oldprompt);


/*
 * "Attributes" were an invention for epic5, and the general idea was
 * to handle all character markups (bold/color/reverse/etc) not as toggle
 * switches, but as absolute settings, handled inline.
 *
 * The function normalize_string() converts all of the character markup
 * toggle characters (^B, ^C, ^V, etc) into Attributes.  Nothing further
 * in the output chain needs to know about highlight toggle chars.
 *
 * Attributes are expressed in two formats, an umarshalled form and a 
 * marshalled form.  The unmarshalled form is (struct attributes) and is
 * a struct of 9 eight-bit ints which hold toggle switches for which 
 * attributes are currently active.  The marshalled form is 5 bytes which
 * can be stored in a C string.
 *
 * An unmarshalled attribute can be injected into a C string using the 
 * display_attributes() function.  A marshalled attribute can be extracted
 * from a C string using read_attributes().
 *
 * The logical_attributes() function will marshall an attribute into the
 * logical (un-normalized) equivalents.   The ignore_attributes() function
 * is a function that essentially ignores/strips attribute changes.
 */
struct 	attributes {
	unsigned char	reverse;
	unsigned char	bold;
	unsigned char	blink;
	unsigned char	underline;
	unsigned char	altchar;
	unsigned char	color_fg;
	unsigned char	color_bg;
	unsigned char	fg_color;
	unsigned char	bg_color;
	unsigned char	italic;
};
typedef struct attributes Attribute;

const unsigned char *all_off (void)
{
#ifdef NO_CHEATING
	Attribute 	old_a, a;
	static	unsigned char	retval[6];

	a->reverse = a->bold = a->blink = a->underline = a->altchar = 0;
	a->color_fg = a->fg_color = a->color_bg = a->bg_color = 0;
	a->italic = 0;
	*old_a = *a;
	display_attributes(retval, &old_a, &a);
	return retval;
#else
	static	char	retval[6];
	retval[0] = '\006';
	retval[1] = retval[2] = retval[3] = retval[4] = 0x80;
	retval[5] = 0;
	return retval;
#endif
}

/*
 * These are "attribute changer" functions.  They work like this:
 *	output		An output (string) buffer to write the change to
 *	old_a		The previous (Attribute *)
 *	new_a		The new (Attribute *)
 * Each function should write any changes between "old_a" and "new_a" to 
 * output in whatever way is appropriate, and should copy new_a to old_a 
 * before returning.  The special case is when "old_a" is NULL, which 
 * should be treated as an explicit "all off" before handling "a".
 */
static size_t	display_attributes (unsigned char *output, Attribute *old_a, Attribute *a)
{
	unsigned char	val1 = 0x80;
	unsigned char	val2 = 0x80;
	unsigned char	val3 = 0x80;
	unsigned char	val4 = 0x80;

	/*
	 * A "Display attribute" is a \006 followed by four bytes.
	 * Each of the four bytes have the high bit set (to avoid nuls)
	 * Each of the four bytes thus contain 7 bits of information
	 */

	/* 
	 * Byte 1 uses its 7 bits for 7 attribute toggles
	 *	1	Reverse
	 *	2	Bold
	 *	3	Blink
	 *	4	Underline
	 *	5	AltCharSet 
	 *	6	Italics
	 *	7 	[Reserved for future expansion]
	 */
	if (a->reverse)		val1 |= 0x01;
	if (a->bold)		val1 |= 0x02;
	if (a->blink)		val1 |= 0x04;
	if (a->underline)	val1 |= 0x08;
	if (a->altchar)		val1 |= 0x10;
	if (a->italic)		val1 |= 0x20;

	/*
	 * Byte 2 holds color information
	 *	1	Foreground Color turned on
	 *	2	Background Color turned on
	 *	3	The foreground color has high bit set (see below)
	 *	4	The background color has high bit set (see below)
	 *	5	[Reserved for future expansion]
	 *	6	[Reserved for future expansion]
	 *	7	[Reserved for future expansion]
	 *
	 * Byte 3 holds the low 7 bits of the foreground color.
	 *	The high bit is stored in Byte 2, bit 3
	 *
	 * Byte 4 holds the low 7 bits of the background color.
	 *	The high bit is stored in Byte 2, bit 3.
	 */

	if (a->color_fg) {	val2 |= 0x01; val3 |= (a->fg_color & 0x7F); }
	if (a->color_bg) {	val2 |= 0x02; val4 |= (a->bg_color & 0x7F); }
	if (a->color_fg && a->fg_color >= 0x80)	{ val2 |= 0x04; }
	if (a->color_bg && a->bg_color >= 0x80) { val2 |= 0x08; }

	output[0] = '\006';
	output[1] = val1;
	output[2] = val2;
	output[3] = val3;
	output[4] = val4;
	output[5] = 0;

	*old_a = *a;
	return 5;
}
 
/* Put into 'output', logical characters so end result is 'a' */
static size_t	logic_attributes (unsigned char *output, Attribute *old_a, Attribute *a)
{
	char	*str = output;
	size_t	count = 0;
	Attribute dummy;

	if (old_a == NULL)
	{
		old_a = &dummy;
		old_a->reverse = old_a->bold = old_a->blink = 0;
		old_a->underline = old_a->altchar = 0;
		old_a->italic = 0;
		old_a->color_fg = old_a->fg_color = 0;
		old_a->color_bg = old_a->bg_color = 0;
		*str++ = ALL_OFF, count++;
	}

	if (a->reverse == 0 && a->bold == 0 && a->blink == 0 &&
	    a->underline == 0 && a->altchar == 0 && a->italic == 0 &&
	    a->fg_color == 0 && a->bg_color == 0 && 
	    a->color_bg == 0 && a->color_fg == 0)
	{
	    if (old_a->reverse != 0 || old_a->bold != 0 || old_a->blink != 0 ||
		old_a->underline != 0 || old_a->altchar != 0 || old_a->italic ||
		old_a->fg_color != 0 || old_a->bg_color != 0 || 
		old_a->color_bg != 0 || old_a->color_fg != 0)
	    {
		*str++ = ALL_OFF;
		*old_a = *a;
		return 1;
	    }
	}

	/* Colors need to be set first, always */
	if (a->color_fg != old_a->color_fg || a->fg_color != old_a->fg_color ||
	    a->color_bg != old_a->color_bg || a->bg_color != old_a->bg_color)
	{
		*str++ = '\030', count++;

		if (a->color_fg != old_a->color_fg || a->fg_color != old_a->fg_color)
		{
			if (a->color_fg)
				count += hex256(a->fg_color, &str);
			else
			{
				*str++ = '-', count++;
				*str++ = '1', count++;
			}
		}
		if (a->color_bg != old_a->color_bg || a->bg_color != old_a->bg_color)
		{
			*str++ = ',', count++;
			if (a->color_bg)
				count += hex256(a->bg_color, &str);
			else
			{
				*str++ = '-', count++;
				*str++ = '1', count++;
			}
		}
	}
	if (old_a->bold != a->bold)
		*str++ = BOLD_TOG, count++;
	if (old_a->blink != a->blink)
		*str++ = BLINK_TOG, count++;
	if (old_a->reverse != a->reverse)
		*str++ = REV_TOG, count++;
	if (old_a->underline != a->underline)
		*str++ = UND_TOG, count++;
	if (old_a->altchar != a->altchar)
		*str++ = ALT_TOG, count++;
	if (old_a->italic != a->italic)
		*str++ = ITALIC_TOG, count++;

	*old_a = *a;
	return count;
}

/* Suppress any attribute changes in the output */
static size_t	ignore_attributes (unsigned char *output, Attribute *old_a, Attribute *a)
{
	return 0;
}


/* Read an attribute marker from 'input', put results in 'a'. */
static int	read_attributes (const unsigned char *input, Attribute *a)
{
	if (!input)
		return -1;

	/* 
	 * This used to check for *input != '\006' but it was inconvenient
	 * to enforce that, so now it's just an optional check.
	 */
	if (*input == '\006')
		input++;

	if (!input[0] || !input[1] || !input[2] || !input[3])
		return -1;

	a->reverse = a->bold = a->blink = a->underline = a->altchar = 0;
	a->italic = 0;
	a->color_fg = a->fg_color = a->color_bg = a->bg_color = 0;

	if (*input & 0x01)	a->reverse = 1;
	if (*input & 0x02)	a->bold = 1;
	if (*input & 0x04)	a->blink = 1;
	if (*input & 0x08)	a->underline = 1;
	if (*input & 0x10)	a->altchar = 1;
	if (*input & 0x20)	a->italic = 1;

	input++;
	if (*input & 0x01) {	
		a->color_fg = 1; 
		a->fg_color = input[1] & 0x7f;
		if (*input & 0x04)
			a->fg_color += 0x80;
	}
	if (*input & 0x02) {	
		a->color_bg = 1; 
		a->bg_color = input[2] & 0x7f;
		if (*input & 0x08)
			a->bg_color += 0x80;
	}

	return 0;
}

/* Invoke all of the neccesary functions so output attributes reflect 'a'. */
static void	term_attribute (Attribute *a)
{
	term_all_off();
	if (a->reverse)		term_standout_on();
	if (a->bold)		term_bold_on();
	if (a->blink)		term_blink_on();
	if (a->underline)	term_underline_on();
	if (a->altchar)		term_altcharset_on();
	if (a->italic)		term_italics_on();

	/* XXXX This is where you'd put in a shim to globally defeat colors */
	if (!(x_debug & DEBUG_NO_COLOR))
	{
		if (a->color_fg) { 
			if (a->bold && get_int_var(BROKEN_AIXTERM_VAR))
				term_set_bold_foreground(a->fg_color);
			else
				term_set_foreground(a->fg_color); 
		}
		if (a->color_bg) { 
			if (a->bold && get_int_var(BROKEN_AIXTERM_VAR))
				term_set_bold_background(a->bg_color);
			else
				term_set_background(a->bg_color); 
		}
	}
}

/* * * * * * * * * * * * * COLOR SUPPORT * * * * * * * * * * * * * * * * */
/*
 * read_color_seq -- Parse out and count the length of a ^C color sequence
 * Arguments:
 *	start     - A string beginning with ^C that represents a color sequence
 *	d         - An (Attribute *) [or NULL] that shall be modified by the
 *		    color sequence.
 *	blinkbold - The value of /set bold_does_bright_blink
 * Return Value:
 *	The length of the ^C color sequence, such that (start + retval) is
 *	the first character that is not part of the ^C color sequence.
 *	In no case does the return value pass the string's terminating null.
 *
 * Note:
 *	Unlike some other clients, EPIC does not simply slurp up all digits 
 *	after a ^C sequence (either by calling strtol() or while (isdigit()),
 *	because some people put ^C sequences before legitimate output with 
 * 	numbers (like the time on your status bar).  This function is very
 *	careful only to consume the characters that represent a bona fide 
 *	^C code.  This means things like "^C49" resolve to "^C4" + "9"
 *
 * DO NOT USE ANY OTHER FUNCTION TO PARSE ^C CODES.  YOU HAVE BEEN WARNED!
 */
static ssize_t	read_color_seq (const unsigned char *start, void *d, int blinkbold)
{
	/* 
	 * The proper "attribute" color mapping is for each ^C lvalue.
	 * If the value is -1, then that is an illegal ^C lvalue.
	 */
	static	int	fore_conv[] = {
		 7,  0,  4,  2,  1,  1,  5,  3,		/*  0-7  */
		 3,  2,  6,  6,  4,  5,  0,  7,		/*  8-15 */
		 7, -1, -1, -1, -1, -1, -1, -1, 	/* 16-23 */
		-1, -1, -1, -1, -1, -1,  0,  1, 	/* 24-31 */
		 2,  3,  4,  5,  6,  7, -1, -1,		/* 32-39 */
		-1, -1, -1, -1, -1, -1, -1, -1,		/* 40-47 */
		-1, -1,  0,  1,  2,  3,  4,  5, 	/* 48-55 */
		 6,  7,	-1, -1, -1			/* 56-60 */
	};
	/* 
	 * The proper "attribute" color mapping is for each ^C rvalue.
	 * If the value is -1, then that is an illegal ^C rvalue.
	 */
	static	int	back_conv[] = {
		 7,  0,  4,  2,  1,  1,  5,  3,
		 3,  2,  6,  6,  4,  5,  0,  7,
		 7, -1, -1, -1, -1, -1, -1, -1,
		-1, -1, -1, -1, -1, -1, -1, -1,
		-1, -1, -1, -1, -1, -1, -1, -1,
		 0,  1,  2,  3,  4,  5,  6,  7, 
		-1, -1,  0,  1,  2,  3,  4,  5,
		 6,  7, -1, -1, -1
	};

	/*
	 * Some lval codes represent "bold" colors.  That actually reduces
	 * to ^C<non bold> + ^B, so that if you do ^B later, you get the
	 * <non bold> color.  This table indicates whether a ^C code 
	 * turns bold ON or OFF.  (Every color does one or the other)
	 */
	static	int	fore_bold_conv[] =  {
		1,  0,  0,  0,  1,  0,  0,  0,
		1,  1,  0,  1,  1,  1,  1,  0,
		1,  0,  0,  0,  0,  0,  0,  0,
		0,  0,  0,  0,  0,  0,  0,  0,
		0,  0,  0,  0,  0,  0,  0,  0,
		0,  0,  0,  0,  0,  0,  0,  0,
		0,  0,  1,  1,  1,  1,  1,  1,
		1,  1,  0,  0,  0
	};
	/*
	 * Some rval codes represent "blink" colors.  That actually reduces
	 * to ^C<non blink> + ^F, so that if you do ^F later, you get the
	 * <non blink> color.  This table indicates whether a ^C code 
	 * turns blink ON or OFF.  (Every color does one or the other)
	 */
	static	int	back_blink_conv[] = {
		0,  0,  0,  0,  0,  0,  0,  0,
		0,  0,  0,  0,  0,  0,  0,  0,
		0,  0,  0,  0,  0,  0,  0,  0,
		0,  0,  0,  0,  0,  0,  0,  0,
		0,  0,  0,  0,  0,  0,  0,  0,
		0,  0,  0,  0,  0,  0,  0,  0,
		0,  0,  1,  1,  1,  1,  1,  1,
		1,  1,  0,  0,  0
	};
	/*
	 * If /set term_does_bright_blink is on, this will be used instead
	 * of back_blink_conv.  On an xterm, this will cause the background
	 * to be bold.
	 */
	static	int	back_bold_conv[] = {
		1,  0,  0,  0,  1,  0,  0,  0,
		1,  1,  0,  1,  1,  1,  1,  0,
		1,  0,  0,  0,  0,  0,  0,  0,
		0,  0,  0,  0,  0,  0,  0,  0,
		0,  0,  0,  0,  0,  0,  0,  0,
		0,  0,  0,  0,  0,  0,  0,  0,
		0,  0,  1,  1,  1,  1,  1,  1,
		1,  1,  0,  0,  0
	};
	/*
	 * And switch between the two.
	 */
	int *	back_blinkbold_conv = blinkbold ? back_bold_conv : 
						  back_blink_conv;

	/* Local variables, of course */
	const 	unsigned char *	ptr = start;
		int		c1, c2;
		Attribute *	a;
		Attribute	ad;
		int		fg;
		int		val;
		int		noval;

        /* Reset all attributes to zero */
        ad.bold = ad.underline = ad.reverse = ad.blink = ad.altchar = 0;
        ad.color_fg = ad.color_bg = ad.fg_color = ad.bg_color = 0;
	ad.italic = 0;

	/* Copy the inward attributes, if provided */
	a = (d) ? (Attribute *)d : &ad;

	/*
	 * Originally this was a check for *ptr == '\003' but it was
	 * inconvenient to pass in the ^C here for prepare_display, so
	 * i'm making this check optional.
	 */
	if (*ptr == COLOR_TAG)
		ptr++;

	/*
	 * This is a one-or-two-time-through loop.  We find the maximum
	 * span that can compose a legit ^C sequence, then if the first
	 * nonvalid character is a comma, we grab the rhs of the code.
	 */
	for (fg = 1; ; fg = 0)
	{
		/*
		 * If its just a lonely old ^C, then its probably a terminator.
		 * Just skip over it and go on.
		 */
		if (*ptr == 0)
		{
			if (fg)
				a->color_fg = a->fg_color = 0;
			a->color_bg = a->bg_color = 0;
			a->bold = a->blink = 0;
			return ptr - start;
		}

		/*
		 * Check for the very special case of a definite terminator.
		 * If the argument to ^C is -1, then we absolutely know that
		 * this ends the code without starting a new one
		 */
		/* XXX *cough* is 'ptr[1]' valid here? */
		else if (ptr[0] == '-' && ptr[1] == '1')
		{
			if (fg)
				a->color_fg = a->fg_color = 0;
			a->color_bg = a->bg_color = 0;
			a->bold = a->blink = 0;
			return (ptr + 2) - start;
		}

		/*
		 * Further checks against a lonely old naked ^C.
		 */
		else if (!isdigit(ptr[0]) && ptr[0] != ',')
		{
			if (fg)
				a->color_fg = a->fg_color = 0;
			a->color_bg = a->bg_color = 0;
			a->bold = a->blink = 0;
			return ptr - start;
		}


		/*
		 * Code certainly cant have more than two chars in it
		 */
		c1 = ptr[0];
		c2 = ptr[1];
		val = 0;
		noval = 0;

#define mkdigit(x) ((x) - '0')

		/* Our action depends on the char immediately after the ^C. */
		switch (c1)
		{
			/* These might take one or two characters */
			case '0':
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			{
			    if (c2 >= '0' && c2 <= '9')
			    {
				int	val1;
				int	val2;

			        ptr++;
				val1 = mkdigit(c1);
				val2 = mkdigit(c1) * 10 + mkdigit(c2);

				if (fg)
				{
					if (fore_conv[val2] == -1)
						val = val1;
					else
						val = val2, ptr++;
				}
				else
				{
					if (back_conv[val2] == -1)
						val = val1;
					else
						val = val2, ptr++;
				}
				break;
			    }

			    FALLTHROUGH
			    /* FALLTHROUGH */
			    /* 
			     * Fallthrough if 1st digit is 0-5
			     * and the 2nd digit is not a number.
			     * This is a 1 digit color code.
			     */
			}

			/* This might take one or two characters (sigh) */
			case '9':
			{
				/* Ignore color 99. sigh. */
				if (c1 == '9' && c2 == '9')
				{
					ptr += 2;
					noval = 1;
					break;
				}

				FALLTHROUGH
				/* FALLTHROUGH */
				/*
				 * Fallthrough if 1st digit is 0-5
				 * and the 2nd digit is not a number,
				 * or if 1st digit is 9, the 2nd digit 
				 * is not 9.
				 */
			}

			/* These can only take one character */
			case '6':
			case '7':
			case '8':
			{
				ptr++;

				val = mkdigit(c1);
				break;
			}

					

			/*
			 * Y -> <stop> Y for any other nonnumeric Y
			 */
			default:
			{
				noval = 1;
				break;
			}
		}

		if (noval == 0)
		{
			if (fg)
			{
				a->color_fg = 1;
				a->bold = fore_bold_conv[val];
				a->fg_color = fore_conv[val];
			}
			else
			{
				a->color_bg = 1;
				a->blink = back_blinkbold_conv[val];
				a->bg_color = back_conv[val];
			}
		}

		if (fg && *ptr == ',')
		{
			ptr++;
			continue;
		}
		else
			break;
	}

	return ptr - start;
}

/*
 * read_color256_seq -- Parse out and count the length of a ^X color sequence
 * Arguments:
 *	start     - A string beginning with ^C that represents a color sequence
 *	d         - An (Attribute *) [or NULL] that shall be modified by the
 *		    color sequence.
 * Return Value:
 *	The length of the ^X color sequence, such that (start + retval) is
 *	the first character that is not part of the ^X color sequence.
 *	In no case does the return value pass the string's terminating null.
 *
 * DO NOT USE ANY OTHER FUNCTION TO PARSE ^X CODES.  YOU HAVE BEEN WARNED!
 */
static ssize_t	read_color256_seq (const unsigned char *start, void *d)
{
	/* Local variables, of course */
	const 	unsigned char *	ptr = start;
		int		c1, c2;
		Attribute *	a;
		Attribute	ad;
		int		fg;
		int		val;
		int		set;

        /* Reset all attributes to zero */
        ad.bold = ad.underline = ad.reverse = ad.blink = ad.altchar = 0;
        ad.color_fg = ad.color_bg = ad.fg_color = ad.bg_color = 0;
	ad.italic = 0;

	/* Copy the inward attributes, if provided */
	a = (d) ? (Attribute *)d : &ad;

	/*
	 * Originally this was a check for *ptr == '\030' but it was
	 * inconvenient to pass in the ^C here for prepare_display, so
	 * i'm making this check optional.
	 */
	if (*ptr == COLOR256_TAG)
		ptr++;

	/*
	 * This is a one-or-two-time-through loop.  We find the maximum
	 * span that can compose a legit ^C sequence, then if the first
	 * nonvalid character is a comma, we grab the rhs of the code.
	 */
	for (fg = 1; ; fg = 0)
	{
		/*
		 * If its just a lonely old ^X, then its probably a terminator.
		 * Just skip over it and go on.
		 * XXX Note -- this is invalid; but we're tolerant because
		 * it's unambiguous.
		 */
		if (*ptr == 0)
		{
			if (fg)
				a->color_fg = a->fg_color = 0;
			a->color_bg = a->bg_color = 0;
			return ptr - start;
		}

		/*
		 * Check for the very special case of a definite terminator.
		 * If the argument to ^X is -1, then we absolutely know that
		 * this ends the code without starting a new one
		 */
		else if (ptr[0] == '-' && ptr[1] == '1')
		{
			if (fg)
				a->color_fg = a->fg_color = 0;
			a->color_bg = a->bg_color = 0;
			return (ptr + 2) - start;
		}

		/*
		 * Further checks against a lonely old naked ^X.
		 * XXX Note -- this is invalid; but we're tolerant 
		 * (even though it's ambiguous)
		 */
		else if (check_xdigit(ptr[0]) == -1 && ptr[0] != ',')
		{
			if (fg)
				a->color_fg = a->fg_color = 0;
			a->color_bg = a->bg_color = 0;
			return ptr - start;
		}


		/*
		 * Code certainly cant have more than three chars in it
		 */
		c2 = 0;
		if ((c1 = ptr[0]))
			c2 = ptr[1];

		/*
		 * If there is a leading comma, then no color here.
		 */
		if (fg && c1 == ',')
		{
			ptr++;
			continue;
		}

		/*
		 * Highest priority -- check for two hex digits
		 */
		else if (check_xdigit(c1) != -1 && check_xdigit(c2) != -1)
		{
			set = 1;
			val = check_xdigit(c1) * 16 + check_xdigit(c2);
			ptr += 2;
		}
		/*
		 * Third, check for one hex digit
		 */
		else if (check_xdigit(c1) != -1)
		{
			set = 1;
			val = check_xdigit(c1);
			ptr++;
		}
		/*
		 * Fourth, check for an explicit "-1"
		 */
		else if (c1 == '-' && c2 == '1')
		{
			set = 0;
			val = 0;
			ptr += 2;
		}
		else
		{
			set = 0;
			val = 0;
		}


		if (fg)
		{
			a->color_fg = set;
			a->fg_color = val;
		}
		else
		{
			a->color_bg = set;
			a->bg_color = val;
		}

		if (fg && *ptr == ',')
		{
			ptr++;
			continue;
		}
		else
			break;
	}

	return ptr - start;
}

/**************************************************************************/
/*
 * read_esc_seq -- Parse out and count the length of an escape (ansi) sequence
 * Arguments:
 *	start     - A string beginning with ^[ that represents an ansi sequence
 *	ptr_a     - An (Attribute *) [or NULL] that shall be modified by the
 *		    ansi sequence.
 *	nd_spaces - [OUTPUT] The number of ND_SPACES the sequence generates.
 * Return Value:
 *	The length of the escape (ansi) sequence, such that (start + retval) is
 *	the first character that is not part of the escape (ansi) sequence.
 *	In no case does the return value pass the string's terminating null.
 *
 * Note:
 *	EPIC supports the "Non-destructive space" escape sequence (^[[10C)
 *	which is used by ascii artists.  If this is used, then "nd_spaces" is
 *	set to the value, otherwise it is set to 0.
 *
 *	All escape sequences are parsed, but not all escape sequences are 
 *	honored.  If a dishonored escape sequence is encountered, then its
 *	length is counted, but 'ptr_a' will be unchanged.
 *
 * DO NOT USE ANY OTHER FUNCTION TO PARSE ESCAPES.  YOU HAVE BEEN WARNED!
 */
static ssize_t	read_esc_seq (const unsigned char *start, void *ptr_a, int *nd_spaces)
{
	Attribute *	a = NULL;
	Attribute 	safe_a;
	int 		args[10];
	int		nargs;
	unsigned char 	chr;
	const unsigned char *	str;
	ssize_t		len;

	if (ptr_a == NULL)
		a = &safe_a;
	else
		a = (Attribute *)ptr_a;

	*nd_spaces = 0;
	str = start;
	len = 0;

	switch (start[len])
	{
	    /*
	     * These are two-character commands.  The second
	     * char is the argument.
	     */
	    case ('#') : case ('(') : case (')') :
	    case ('*') : case ('+') : case ('$') :
	    case ('@') :
	    {
		if (start[len+1] != 0)
			len++;
		break;
	    }

	    /*
	     * These are just single-character commands.
	     */
	    case ('7') : case ('8') : case ('=') :
	    case ('>') : case ('D') : case ('E') :
	    case ('F') : case ('H') : case ('M') :
	    case ('N') : case ('O') : case ('Z') :
	    case ('l') : case ('m') : case ('n') :
	    case ('o') : case ('|') : case ('}') :
	    case ('~') : case ('c') :
	    default:
	    {
		break;		/* Don't do anything */
	    }

	    /*
	     * Swallow up graphics sequences...
	     */
	    case ('G'):
	    {
		while ((chr = start[++len]) && chr != ':')
			;
		if (chr == 0)
			len--;
		break;
	    }

	    /*
	     * Not sure what this is, it's not supported by
	     * rxvt, but its supposed to end with an ESCape.
	     */
	    case ('P') :
	    {
		while ((chr = start[++len]) && chr != 033)
			;
		if (chr == 0)
			len--;
		break;
	    }

	    /*
	     * Strip out Xterm sequences
	     */
	    case (']') :
	    {
		while ((chr = start[++len]) && chr != 7)
			;
		if (chr == 0)
			len--;
		break;
	    }

	    case ('[') :
	    {
start_over:

	    /*
	     * Set up the arguments list
	     */
	    args[0] = args[1] = args[2] = args[3] = 0;
	    args[4] = args[5] = args[6] = args[7] = 0;
	    args[8] = args[9] = 0;
        
	   /*
	    * This stuff was taken/modified/inspired by rxvt.  We do it this 
	    * way in order to trap an esc sequence that is embedded in another
	    * (blah).  We're being really really really paranoid doing this, 
	    * but it is for the best.
	    */

	   /*
	    * Check to see if the stuff after the command is a "private" 
	    * modifier.  If it is, then we definitely arent interested.
	    *   '<' , '=' , '>' , '?'
	    */
	   chr = start[len];
	   if (chr >= '<' && chr <= '?')
		(void)0;		/* Skip it */

	   /*
	    * Now pull the arguments off one at a time.  Keep pulling them 
	    * off until we find a character that is not a number or a semicolon.
	    * Skip everything else.
	    */
	   for (nargs = 0; nargs < 10; str++)
	   {
		int n = 0;

		len++;
		for (n = 0; isdigit(start[len]); len++)
			n = n * 10 + (start[len] - '0');

		args[nargs++] = n;

		/*
		 * If we run out of code here, then we're totaly confused.
		 * just back out with whatever we have...
		 */
		if (!start[len])
			return len;

		if (start[len] != ';')
			break;
	    }

	    /*
	     * If we find a new ansi char, start all over from the top 
	     * and strip it out too
	     */
	    if (start[len] == 033)
		goto start_over;

	    /*
	     * Support "spaces" (cursor right) code
	     */
	    else if (start[len] == 'a' || start[len] == 'C')
	    {
		len++;
		if (nargs >= 1)
		{
		    /* Keep this within reality.  */
		    if (args[0] > 256)
			args[0] = 256;
		    *nd_spaces = args[0];
		}
	    }

	    /*
	     * Walk all of the numeric arguments, plonking the appropriate 
	     * attribute changes as needed.
	     */
	    else if (start[len] == 'm')
	    {
		int	i;

		len++;
		for (i = 0; i < nargs; i++)
		{
		    switch (args[i])
		    {
			case 0:		/* Reset to default */
			{
				a->reverse = a->bold = 0;
				a->blink = a->underline = 0;
				a->altchar = a->italic = 0;
				a->color_fg = a->color_bg = 0;
				a->fg_color = a->bg_color = 0;
				break;
			}
			case 1:		/* bold on */
				a->bold = 1;
				break;
			case 2:		/* dim on -- not supported */
				break;
			case 3:		/* Italics on -- not supported */
				break;
			case 4:		/* Underline on */
				a->underline = 1;
				break;
			case 5:		/* Blink on */
			case 26:	/* Blink on */
				a->blink = 1;
				break;
			case 6:		/* Blink off */
			case 25:	/* Blink off */
				a->blink = 0;
				break;
			case 7:		/* Reverse on */
				a->reverse = 1;
				break;
			case 21:	/* Bold off */
			case 22:	/* Bold off */
				a->bold = 0;
				break;
			case 23:	/* Italics off -- not supported */
				break;
			case 24:	/* Underline off */
				a->underline = 0;
				break;
			case 27:	/* Reverse off */
				a->reverse = 0;
				break;
			case 30: case 31: case 32: case 33: case 34: case 35:
			case 36: case 37:	/* Set foreground color */
			{
				a->color_fg = 1;
				a->fg_color = args[i] - 30;
				break;
			}
			case 38:	/* Set 256 fg color */
			{
				/* 38 takes at least one argument */
				if (i == nargs)
					break;		/* Invalid */
				i++;

				/* 38-5 takes 1 argument */
				if (args[i] == 5)
				{
					if (i == nargs)
					    break;	/* Invalid */
					i++;

					a->color_fg = 1;
					a->fg_color = args[i];
					break;		/* Invalid */
				}
				else if (args[i] == 2)
				{
					int	r, g, b, c;

					if (i + 3 >= nargs)
					    break;	/* Invalid */

					r = args[++i];
					g = args[++i];
					b = args[++i];
					c = rgb_to_256(r, g, b);
					a->color_fg = 1;
					a->fg_color = c;
					break;
				}

				break;
			}
			case 39:	/* Reset foreground color to default */
			{
				a->color_fg = 0;
				a->fg_color = 0;
				break;
			}
			case 40: case 41: case 42: case 43: case 44: case 45:
			case 46: case 47:	/* Set background color */
			{
				a->color_bg = 1;
				a->bg_color = args[i] - 40;
				break;
			}
			case 48:	/* Set 256 bg color */
			{
				/* 48 takes at least one argument */
				if (i == nargs)
					break;		/* Invalid */
				i++;

				/* 48-5 takes 1 argument */
				if (args[i] == 5)
				{
					if (i == nargs)
					    break;	/* Invalid */

					a->color_bg = 1;
					a->bg_color = args[++i];
					break;		/* Invalid */
				}
				else if (args[i] == 2)
				{
					int	r, g, b, c;

					if (i + 3 >= nargs)
					    break;	/* Invalid */

					r = args[++i];
					g = args[++i];
					b = args[++i];
					c = rgb_to_256(r, g, b);
					a->color_bg = 1;
					a->bg_color = c;
					break;
				}

				break;
			}
			case 49:	/* Reset background color to default */
			{
				a->color_bg = 0;
				a->bg_color = 0;
				break;
			}

			/* 
			 * Some emulators are incapable of supporting 
			 * bold/blink+colors and require you to use these 
			 * irregular/non-standard numbers.
			 */
			case 90: case 91: case 92: case 93: case 94: case 95:
			case 96: case 97:	/* Set bright foreground color */
			{
				a->bold = 1;
				a->color_fg = 1;
				a->fg_color = args[i] - 90;
				break;
			}
			case 100: case 101: case 102: case 103: case 104: case 105:
			case 106: case 107:	/* Set bright (blink) background color */
			{
				a->blink = 1;
				a->color_bg = 1;
				a->bg_color = args[i] - 100;
				break;
			}


			default:	/* Everything else is not supported */
				break;
		    }
		} /* End of for loop */
	    } /* End of handling esc-[...m */
	    } /* End of case esc-[ */
	} /* End of case esc */

	/* All other escape sequences are ignored! */
	return len;
}

/**************************** STRIP ANSI ***********************************/
/*
 * The THREE FUNCTIONS OF DOOM
 *
 * 1) normalize_string -- given any arbitrary string, make it "safe" to use.
 * 2) prepare_display -- given a "safe" string, break it into lines
 * 3) output_with_count -- given a broken "safe" string, actually output it.
 */

/*
 * State 0 is a "normal, printable character" (8 bits included)
 * State 1 is a "C1 character", aka a control character with high bit set.
 * State 2 is an "escape character" (\033)
 * State 3 is a "color code character" (\003 aka COLOR_TAG)
 * State 4 is an "attribute change character"
 * State 5 is a "suppressed character" (always stripped)
 * State 6 is a "character that is never printable."
 * State 7 is a "beep"
 * State 8 is a "tab"
 * State 9 is a "non-destructive space"
 * State 10 is a "256 color code character" (\030 aka COLOR256_TAG)
 */
static	unsigned char	ansi_state[128] = {
/*	^@	^A	^B	^C	^D	^E	^F	^G(\a) */
	6,	6,	4,	3,	6,	4,	4,	7,  /* 000 */
/*	^H	^I	^J(\n)	^K	^L(\f)	^M(\r)	^N	^O */
	6,	8,	0,	6,	6,	5,	6,	4,  /* 010 */
/*	^P	^Q	^R	^S	^T	^U	^V	^W */
	4,	6,	6,	9,	6,	6,	4,	6,  /* 020 */
/*	^X	^Y	^Z	^[	^\	^]	^^	^_ */
	10,	6,	6,	2,	6,	6,	6,	4,  /* 030 */
	0,	0,	0,	0,	0,	0,	0,	0,  /* 040 */
	0,	0,	0,	0,	0,	0,	0,	0,  /* 050 */
	0,	0,	0,	0,	0,	0,	0,	0,  /* 060 */
	0,	0,	0,	0,	0,	0,	0,	0,  /* 070 */
	0,	0,	0,	0,	0,	0,	0,	0,  /* 100 */
	0,	0,	0,	0,	0,	0,	0,	0,  /* 110 */
	0,	0,	0,	0,	0,	0,	0,	0,  /* 120 */
	0,	0,	0,	0,	0,	0,	0,	0,  /* 130 */
	0,	0,	0,	0,	0,	0,	0,	0,  /* 140 */
	0,	0,	0,	0,	0,	0,	0,	0,  /* 150 */
	0,	0,	0,	0,	0,	0,	0,	0,  /* 160 */
	0,	0,	0,	0,	0,	0,	0,	0,  /* 170 */
};

/*
	A = Character or sequence converted into an attribute
	M = Character mangled
	S = Character stripped, sequence (if any) NOT stripped
	X = Character stripped, sequence (if any) also stripped
	T = Transformed into other (safe) chars
	- = No transformation

					Type
    			0    1    2    3    4    5    6    7    8    9  10
(Default)		-    -    -    -    A    -    -    T    T    T   -
NORMALIZE		-    -    A    A    -    X    M    -    -    -   A
MANGLE_ESCAPES		-    -    S    -    -    -    -    -    -    -   -
STRIP_COLOR		-    -    -    X    -    -    -    -    -    -   X
STRIP_*			-    -    -    -    X    -    -    -    -    -   -
STRIP_UNPRINTABLE	-    X    S    S    X    X    X    X    -    -   S
STRIP_OTHER		X    -    -    -    -    -    -    -    X    X   -
(/SET ALLOW_C1)		-    X    -    -    -    -    -    -    -    -   -
*/

/*
 * new_normalize_string -- Transform an untrusted input string into something
 *				we can trust.
 * Arguments:
 *   str	An untrusted input string
 *   logical	How attribute changes should look in the output:
 *		0	Marshalled form, suitable for displaying
 *		1	Un-normalized form, suitable for the user (ie, ^B/^V)
 *		2	Stripped out entirely
 *		3	Marshalled form, especially for the status bar.
 *   mangler	How we want the string to be transformed
 *		The above chart shows how the different types of characters
 *		are transformed by the different mangler types.  There are
 *		three ambiguous cases, which are resolved as such:
 *		Type 2:
 *			MANGLE_ESCAPES has the first priority, then
 *			NORMALIZE is next, finally STRIP_UNPRINTABLE.
 *		Type 3:
 *			STRIP_UNPRINTABLE has the first priority, then
 *			NORMALIZE and STRIP_COLOR.  You need to use both 
 *			NORMALIZE and STRIP_COLOR to strip color changes 
 *			in color sequences
 *		Type 6:
 *			STRIP_UNPRINTABLE has higher priority than NORMALIZE.
 *
 * Furthermore, the following two sets affect behavior:
 *	  /SET ALLOW_C1_CHARS
 *		ON  == Type 1 chars are treated as Type 0 chars (safe)
 *		OFF == Type 1 chars are treated as Type 5 chars (unsafe)
 *	  /SET TERM_DOES_BRIGHT_BLINK
 *		???
 *
 * Return Value:
 *	A new trusted string, that has been subjected to the transformations
 *	in "mangler", with attribute changes represented in the "logical"
 *	format.
 */
unsigned char *	new_normalize_string (const unsigned char *str, int logical, int mangle)
{
	unsigned char *	output;
	unsigned char	chr;
	Attribute	a, olda;
	int 		pos;
	int		maxpos;
	int		i;
	int		pc = 0;
	int		mangle_escapes, normalize;
	int		strip_reverse, strip_bold, strip_blink, 
			strip_underline, strip_altchar, strip_color, 
			strip_all_off, strip_nd_space, boldback;
	int		strip_unprintable, strip_other, strip_c1, strip_italic;
	int		codepoint, state, cols;
	char 		utf8str[16], *x;
	size_t		(*attrout) (unsigned char *, Attribute *, Attribute *) = NULL;

	mangle_escapes 	= ((mangle & MANGLE_ESCAPES) != 0);
	normalize	= ((mangle & NORMALIZE) != 0);

	strip_color 	= ((mangle & STRIP_COLOR) != 0);
	strip_reverse 	= ((mangle & STRIP_REVERSE) != 0);
	strip_underline	= ((mangle & STRIP_UNDERLINE) != 0);
	strip_bold 	= ((mangle & STRIP_BOLD) != 0);
	strip_blink 	= ((mangle & STRIP_BLINK) != 0);
	strip_nd_space	= ((mangle & STRIP_ND_SPACE) != 0);
	strip_altchar 	= ((mangle & STRIP_ALT_CHAR) != 0);
	strip_all_off 	= ((mangle & STRIP_ALL_OFF) != 0);
	strip_unprintable = ((mangle & STRIP_UNPRINTABLE) != 0);
	strip_other	= ((mangle & STRIP_OTHER) != 0);
	strip_italic	= ((mangle & STRIP_ITALIC) != 0);

	strip_c1	= !get_int_var(ALLOW_C1_CHARS_VAR);
	boldback	= get_int_var(TERM_DOES_BRIGHT_BLINK_VAR);

	if (logical == 0)
		attrout = display_attributes;	/* prep for screen output */
	else if (logical == 1)
		attrout = logic_attributes;	/* non-screen handlers */
	else if (logical == 2)
		attrout = ignore_attributes;	/* $stripansi() function */
	else if (logical == 3)
		attrout = display_attributes;	/* The status line */
	else
		panic(1, "'logical == %d' is not valid.", logical);

	/* Reset all attributes to zero */
	a.bold = a.underline = a.reverse = a.blink = a.altchar = 0;
	a.italic = 0;
	a.color_fg = a.color_bg = a.fg_color = a.bg_color = 0;
	olda = a;

	/* 
	 * The output string has a few extra chars on the end just 
	 * in case you need to tack something else onto it.
	 */
	maxpos = strlen(str);
	output = (unsigned char *)new_malloc(maxpos + 192);
	pos = 0;

	while ((codepoint = next_code_point(&str, 1)) > 0)
	{
	    if (pos > maxpos - 8)
	    {
		maxpos += 192; /* Extend 192 chars at a time */
		RESIZE(output, unsigned char, maxpos + 192);
	    }

	    /* Normal unicode code points are always permitted */
	    if (codepoint >= 0xA0)		/* Valid unicode points */
		state = 0;
	    else if (codepoint >= 0x80)		/* C1 chars */
		state = 1;
	    else
		state = ansi_state[codepoint];

	    switch (state)
	    {
		/*
		 * Unicode does not define any code points in the
		 * U+0080 - U+009F zone ("C1 chars") and many terminal
		 * emulators do weird things with them.   So we should
		 * probably just strip them out.
		 */
		case 1:	     	/* C1 Chars */
		case 5:		/* Unprintable chars */
		{
			if (strip_unprintable)
				break;
			if (state == 5 && normalize)
				break;
			if (state == 1 && strip_c1)
			{
				codepoint = (codepoint | 0x60) & 0x7F;
abnormal_char:
				a.reverse = !a.reverse;
				pos += attrout(output + pos, &olda, &a);
				output[pos++] = (char)(codepoint);
				a.reverse = !a.reverse;
				pos += attrout(output + pos, &olda, &a);
				pc += 1;
				break;
			}
			FALLTHROUGH
			/* FALLTHROUGH */
		}

		/*
		 * State 0 are characters that are permitted under all
		 * circumstances.
		 */
		case 0:
		{
			if (strip_other)
				break;

normal_char:
			cols = codepoint_numcolumns(codepoint);
			if (cols == -1)
				cols = 0;
			pc += cols;

			ucs_to_utf8(codepoint, utf8str, sizeof(utf8str));
			for (x = utf8str; *x; x++)
				output[pos++] = *x;

			break;
		}

		/*
		 * State 6 is a control character (without high bit set)
		 * that doesn't have a special meaning to ircII.
		 */
		case 6:
		{
			/*
			 * \f is a special case, state 0, for status bar.  
			 * Either I special case it here, or in 
			 * output_to_count.  I prefer here.
			 */
			if (logical == 3 && codepoint == '\f')
				goto normal_char;

			if (strip_unprintable)
				break;

			/* XXX Sigh -- I bet anything this is wrong */
			if (termfeatures & TERM_CAN_GCHAR)
				goto normal_char;

			if (normalize)
			{
				codepoint = (codepoint | 0x40) & 0x7F;
				goto abnormal_char;
			}

			goto normal_char;
		}

		/*
		 * State 2 is the escape character
		 */
		case 2:
		{
		    int	nd_spaces = 0;
		    ssize_t	esclen;

		    if (mangle_escapes == 1)
		    {
			codepoint = '[';
			goto abnormal_char;
		    }

		    if (normalize == 1)
		    {
			esclen = read_esc_seq(str, (void *)&a, &nd_spaces);

			if (nd_spaces != 0 && !strip_nd_space)
			{
			    /* This is just sanity */
			    if (pos + nd_spaces > maxpos)
			    {
				maxpos += nd_spaces; 
				RESIZE(output, unsigned char, maxpos + 192);
			    }
			    while (nd_spaces-- > 0)
			    {
				output[pos++] = ND_SPACE;
				pc++;
			    }
			    str += esclen;
			    break;		/* attributes can't change */
			}

			if (a.reverse && strip_reverse)		a.reverse = 0;
			if (a.bold && strip_bold)		a.bold = 0;
			if (a.blink && strip_blink)		a.blink = 0;
			if (a.underline && strip_underline)	a.underline = 0;
			if (a.altchar && strip_altchar)		a.altchar = 0;
			if (a.italic && strip_italic)		a.italic = 0;
			if (strip_color)
			{
				a.color_fg = a.color_bg = 0;
				a.fg_color = a.bg_color = 0;
			}
			pos += attrout(output + pos, &olda, &a);
			str += esclen;
			break;
		    }

		    if (strip_unprintable)
			break;

		    goto normal_char;
		}

	        /*
		 * Normalize ^C codes...  And ^X codes...
	         */
		case 3:
		case 10:
		{
		   if (strip_unprintable)
			break;

		   if (strip_color || normalize)
		   {
			ssize_t	len = 0;

			if (state == 3)
				len = read_color_seq(str, (void *)&a, boldback);
			else 
				len = read_color256_seq(str, (void *)&a);

			str += len;

			/* Suppress the color if no color is permitted */
			if (a.bold && strip_bold)		a.bold = 0;
			if (a.blink && strip_blink)		a.blink = 0;
			if (strip_color)
			{
				a.color_fg = a.color_bg = 0;
				a.fg_color = a.bg_color = 0;
			}

			/* Output the new attributes */
			pos += attrout(output + pos, &olda, &a);
			break;
		    }

		    goto normal_char;
		}

		/*
		 * State 4 is for the special highlight characters
		 */
		case 4:
		{
		    if (strip_unprintable)
			break;

		    switch (codepoint)
		    {
			case REV_TOG:
				if (!strip_reverse)
					a.reverse = !a.reverse;
				break;
			case BOLD_TOG:
				if (!strip_bold)
					a.bold = !a.bold;
				break;
			case BLINK_TOG:
				if (!strip_blink)
					a.blink = !a.blink;
				break;
			case UND_TOG:
				if (!strip_underline)
					a.underline = !a.underline;
				break;
			case ALT_TOG:
				if (!strip_altchar)
					a.altchar = !a.altchar;
				break;
			case ITALIC_TOG:
				if (!strip_italic)
					a.italic = !a.italic;
				break;
			case ALL_OFF:
				if (!strip_all_off)
				{
				    a.reverse = a.bold = a.blink = 0;
				    a.underline = a.altchar = 0;
				    a.italic = 0;
				    a.color_fg = a.color_bg = 0;
				    a.bg_color = a.fg_color = 0;
				    pos += attrout(output + pos, &olda, &a);
				    olda = a;
				}
				break;
			default:
				break;
		    }

		    /* After ALL_OFF, this is a harmless no-op */
		    pos += attrout(output + pos, &olda, &a);
		    break;
		}

		case 7:      /* bell */
		{
			if (strip_unprintable)
				break;

			output[pos++] = '\007';
			break;
		}

		case 8:		/* Tab */
		{
			int	len = 8 - (pc % 8);

			if (strip_other)
				break;

			for (i = 0; i < len; i++)
			{
				output[pos++] = ' ';
				pc++;
			}
			break;
		}

		case 9:		/* Non-destruct space */
		{
			if (strip_other)
				break;
			if (strip_nd_space)
				break;

			output[pos++] = ND_SPACE;
			break;
		}

		default:
		{
			panic(1, "Unknown normalize_string mode");
			break;
		}
	    } /* End of huge ansi-state switch */
	} /* End of while, iterating over input string */

	/* Terminate the output and return it. */
	if (logical == 0)
	{
		a.bold = a.underline = a.reverse = a.blink = a.altchar = 0;
		a.italic = 0;
		a.color_fg = a.color_bg = a.fg_color = a.bg_color = 0;
		pos += attrout(output + pos, &olda, &a);
	}
	output[pos] = output[pos + 1] = 0;
	return output;
}


/* 
 * XXX I'm not sure where this belongs, but for now it goes here.
 * This function converts a type-0 normalized string (with the attribute
 * markers) into a type-1 normalized string (with the logical characters 
 * the user understands).  
 *
 * denormalize_string - Convert a Type 0 Normalized String into Type 1
 *
 * Arguments 
 *	str 	- A Type 0 normalized string (ie, returned by 
 *			new_normalize_string() with logical == 0 or 3)
 * Return Value:
 *	A Type 1 normalized string (with ^B, ^V, ^C, ^_s, etc)
 */
unsigned char *	denormalize_string (const unsigned char *str)
{
	unsigned char *	output = NULL;
	size_t		maxpos;
	Attribute 	olda, a;
	size_t		span;
	size_t		pos;

        /* Reset all attributes to zero */
        a.bold = a.underline = a.reverse = a.blink = a.altchar = 0;
        a.color_fg = a.color_bg = a.fg_color = a.bg_color = 0;
	a.italic = 0;
	olda = a;

	/* 
	 * The output string has a few extra chars on the end just 
	 * in case you need to tack something else onto it.
	 */
	if (!str)
		str = "<denormalize_string was called with NULL>";

	maxpos = strlen(str);
	output = (unsigned char *)new_malloc(maxpos + 192);
	pos = 0;

	while (*str)
	{
		if (pos > maxpos)
		{
			maxpos += 192; /* Extend 192 chars at a time */
			RESIZE(output, unsigned char, maxpos + 192);
		}
		switch (*str)
		{
		    case '\006':
		    {
			if (read_attributes(str, &a))
				continue;		/* Mangled */
			str += 5;

			span = logic_attributes(output + pos, &olda, &a);
			pos += span;
			break;
		    }
		    default:
		    {
			output[pos++] = *str++;
			break;
		    }
		}
	}
	output[pos] = 0;
	return output;
}

/* 
 * XXX I'm not sure where this belongs, but for now it goes here.
 * This function converts a type-0 normalized string (with the attribute
 * markers) into plain text (with no attributes)
 *
 * normalized_string_to_plain_text - Convert a Type 0 Normalized String into Type 1
 *
 * Arguments 
 *	str 	- A Type 0 normalized string (ie, returned by 
 *			new_normalize_string() with logical == 0 or 3)
 * Return Value:
 *	Just the plain text from the string.
 */
unsigned char *	normalized_string_to_plain_text (const unsigned char *str)
{
	unsigned char *	output = NULL;
	size_t		maxpos;
	Attribute 	olda, a;
	size_t		span;
	size_t		pos;

        /* Reset all attributes to zero */
        a.bold = a.underline = a.reverse = a.blink = a.altchar = 0;
        a.color_fg = a.color_bg = a.fg_color = a.bg_color = 0;
	a.italic = 0;
	olda = a;

	/* 
	 * The output string has a few extra chars on the end just 
	 * in case you need to tack something else onto it.
	 */
	if (!str)
		str = "<normalized_string_to_plain_text was called with NULL>";

	maxpos = strlen(str);
	output = (unsigned char *)new_malloc(maxpos + 192);
	pos = 0;

	while (*str)
	{
		if (pos > maxpos)
		{
			maxpos += 192; /* Extend 192 chars at a time */
			RESIZE(output, unsigned char, maxpos + 192);
		}
		switch (*str)
		{
		    case '\006':
		    {
			if (read_attributes(str, &a))
				continue;		/* Mangled */
			str += 5;
			break;
		    }
		    default:
		    {
			output[pos++] = *str++;
			break;
		    }
		}
	}
	output[pos] = 0;
	return output;
}



/*
 * Prepare_display -- this is a new twist on FireClown's original function.
 * We dont do things quite the way they were explained in the previous 
 * comment that used to be here, so here's the rewrite. ;-)
 *
 * This function is used to break a logical line of display into some
 * number of physical lines of display, while accounting for various kinds
 * of display codes.  The logical line is passed in the 'orig_str' variable,
 * and the width of the physical display is passed in 'max_cols'.   If 
 * 'lused' is not NULL, then it points at an integer that specifies the 
 * maximum number of lines that should be prepared.  The actual number of 
 * lines that are prepared is stored into 'lused'.  The 'flags' variable
 * specifies some extra options, the only one of which that is supported
 * right now is "PREPARE_NOWRAP" which indicates that you want the function
 * to break off the text at 'max_cols' and not to "wrap" the last partial
 * word to the next line. ($leftpc() depends on this)
 */
#define SPLIT_EXTENT 40
unsigned char **prepare_display (int window,
				 const unsigned char *str,
                                 int max_cols,
                                 int *lused,
                                 int flags)
{
static 	int 	recursion = 0, 
		output_size = 0;
	int 	pos = 0,            /* Current position in "buffer" */
		col = 0,            /* Current column in display    */
		word_break = 0,     /* Last end of word             */
		indent = 0,         /* Start of second word         */
		firstwb = 0,	    /* Buffer position of second word */
		line = 0,           /* Current pos in "output"      */
		do_indent,          /* Use indent or continued line? */
		my_newline = 0;        /* Number of newlines           */
static	unsigned char 	**output = (unsigned char **)0;
const 	unsigned char	*ptr;
	unsigned char 	buffer[BIG_BUFFER_SIZE + 1],
			c,
			*pos_copy;
	unsigned char 	*str_free = NULL;
const	unsigned char	*first_ptr;
	unsigned char	*first = NULL;
const	unsigned char	*cont_ptr;
	unsigned char	*cont = NULL;
	const char 	*words;
	Attribute	a, olda;
	Attribute	saved_a;
	unsigned char	*cont_free = NULL;
	int		codepoint;
	char		utf8str[16];
	char *		x;
	int		cols;

	if (recursion)
		panic(1, "prepare_display() called recursively");
	recursion++;

        /* Reset all attributes to zero */
        a.bold = a.underline = a.reverse = a.blink = a.altchar = 0;
        a.color_fg = a.color_bg = a.fg_color = a.bg_color = 0;
	a.italic = 0;
        saved_a.bold = saved_a.underline = saved_a.reverse = 0;
	saved_a.blink = saved_a.altchar = 0;
        saved_a.color_fg = saved_a.color_bg = saved_a.fg_color = 0;
	saved_a.bg_color = saved_a.italic = 0;

	if (window < 0)
		do_indent = get_int_var(INDENT_VAR);
	else
		do_indent = get_window_indent(window);
	if (!(words = get_string_var(WORD_BREAK_VAR)))
		words = " \t";
	if (!(cont_ptr = get_string_var(CONTINUED_LINE_VAR)))
		cont_ptr = empty_string;

	buffer[0] = 0;

	if (!output_size)
	{
		int 	new_i = SPLIT_EXTENT;
		RESIZE(output, char *, new_i);
		while (output_size < new_i)
			output[output_size++] = 0;
	}

	if (!(first_ptr = get_string_var(FIRST_LINE_VAR)))
		first_ptr = empty_string;
	malloc_strcpy((char **)&str_free, first_ptr);
	malloc_strcat((char **)&str_free, str);
	str = str_free;

	/*
	 * Start walking through the entire string.
	 */
	for (ptr = str; *ptr && (pos < BIG_BUFFER_SIZE - 8); )
	{
		codepoint = next_code_point(&ptr, 1);

		switch (codepoint)
		{
		   case '\007':      /* bell */
			buffer[pos++] = 7;
			break;

		    case '\n':      /* Forced newline */
		    {
			my_newline = 1;
			if (indent == 0)
				indent = -1;
			word_break = pos;
			break; /* case '\n' */
		    }

		    /* Attribute changes -- copy them unmodified. */
		    case '\006':
		    {
			if (read_attributes(ptr, &a) == 0)
			{
				buffer[pos++] = 6;
				buffer[pos++] = *ptr++;
				buffer[pos++] = *ptr++;
				buffer[pos++] = *ptr++;
				buffer[pos++] = *ptr++;
			}
			else
				abort();

			/*
			 * XXX This isn't a hack, but it _is_ ugly!
			 * Because I'm too lazy to find a better place
			 * to put this (down among the line wrapping
			 * logic would be a good place), I take the
			 * cheap way out by "saving" any attribute
			 * changes that occur prior to the first space
			 * in a line.  If there are no spaces for the
			 * rest of the line, then this *is* the saved
			 * attributes we will need to start the next
			 * line.  This fixes an abort().
			 */
			if (word_break == 0)
				saved_a = a;

			continue;          /* Skip the column check */
		    }

		    default:
		    {
			const unsigned char *	xptr;
			int	cp;
			int	spacechar = 0;

			xptr = words;
			while ((cp = next_code_point(&xptr, 1)))
			{
				if (codepoint == cp)
				{
					spacechar = 1;
					word_break = pos;
					break;
				}
			}

			if (spacechar == 0)
			{
				if (indent == -1)
					indent = col;

				cols = codepoint_numcolumns(codepoint);
				if (cols == -1)
					cols = 0;
				col += cols;

				ucs_to_utf8(codepoint, utf8str, sizeof(utf8str));
				for (x = utf8str; *x; x++)
					buffer[pos++] = *x;
				break;
			}
			/* 
			 * The above is only for non-whitespace.
			 * whitespace is...
			 */
			FALLTHROUGH
			/* FALLTHROUGH */
		    }

		    case ' ':
		    case ND_SPACE:
		    {
			const unsigned char *x2;
			int	ncp;
			int	old_pos = pos;

			/* 
			 * 'ptr' points at the character after the
			 * one we are evaluating: so 'x2' is used to
			 * avoid changing ptr.  Therefore 'ncp' is the
			 * "next code point" after the space.
			 */
			x2 = ptr;
			ncp = next_code_point(&x2, 1);

			if (indent == 0)
			{
				indent = -1;
				firstwb = pos;
			}

			saved_a = a;

			/* XXX Sigh -- exactly the same as above. */
			cols = codepoint_numcolumns(codepoint);
			if (cols == -1)
				cols = 0;
			col += cols;

			ucs_to_utf8(codepoint, utf8str, sizeof(utf8str));
			for (x = utf8str; *x; x++)
				buffer[pos++] = *x;

			/*
			 * A space always breaks here.
			 *
			 * A non-space breaks at the character AFTER the
			 * break character (ie, the breaking character stays
			 * on the first line) unless the non-space character
			 * would be the rightmost column, in which case it
			 * breaks here (and slips to the 2nd line)
			 */
			if (codepoint == ' ')
				word_break = old_pos;
			else if (ncp && (col + 1 < max_cols))
				word_break = pos;
			else
				word_break = old_pos;

			break;
		    }
		} /* End of switch (codepoint) */

		/*
		 * Must check for cols >= maxcols+1 becuase we can have a 
		 * character on the extreme screen edge, and we would still 
		 * want to treat this exactly as 1 line, and col has already 
		 * been incremented.
		 */
		if ((col > max_cols) || my_newline)
		{
			/*
			 * We just incremented col, but we need to decrement
			 * it in order to keep the count correct!
			 *		--zinx
			 */
			if (col > max_cols)
				col--;

			/*
			 * XXX Hackwork and trickery here.  In the very rare
			 * case where we end the output string *exactly* at
			 * the end of the line, then do not do any more of
			 * the following handling.  Just punt right here.
			 */
			if (ptr[1] == 0)
				break;		/* stop all processing */

			/*
			 * Default the end of line wrap to the last character
			 * we parsed if there were no spaces in the line, or
			 * if we're preparing output that is not to be
			 * wrapped (such as for counting output length.
			 */
			if (word_break == 0 || (flags & PREPARE_NOWRAP))
				word_break = pos - 1;


			/*
			 * XXXX Massive hackwork here.
			 *
			 * Due to some ... interesting design considerations,
			 * if you have /set indent on and your first line has
			 * exactly one word seperation in it, then obviously
			 * there is a really long "word" to the right of the 
			 * first word.  Normally, we would just break the 
			 * line after the first word and then plop the really 
			 * big word down to the second line.  Only problem is 
			 * that the (now) second line needs to be broken right 
			 * there, and we chew up (and lose) a character going 
			 * through the parsing loop before we notice this.
			 * Not good.  It seems that in this very rare case, 
			 * people would rather not have the really long word 
			 * be sent to the second line, but rather included on 
			 * the first line (it does look better this way), 
			 * and so we can detect this condition here, without 
			 * losing a char but this really is just a hack when 
			 * it all comes down to it.  Good thing its cheap. ;-)
			 */
			if (!cont && (firstwb == word_break) && do_indent) 
				word_break = pos - 1;

			/*
			 * If we are approaching the number of lines that
			 * we have space for, then resize the master line
			 * buffer so we dont run out.
			 */
			if (line >= output_size - 3)
			{
				int new_i = output_size + SPLIT_EXTENT;
				RESIZE(output, char *, new_i);
				while (output_size < new_i)
					output[output_size++] = 0;
			}

			/* XXXX HACK! XXXX HACK! XXXX HACK! XXXX */
			/*
			 * Unfortunately, due to the "line wrapping bug", if
			 * you have a really long line at the end of the first
			 * line of output, and it needs to be wrapped to the
			 * second line of input, we were blindly assuming that
			 * it would fit on the second line, but that may not
			 * be true!  If the /set continued_line jazz ends up
			 * being longer than whatever was before the wrapped
			 * word on the first line, then the resulting second
			 * line would be > max_cols, causing corruption of the
			 * display (eg, the status bar gets written over)!
			 *
			 * To counteract this bug, at the end of the first
			 * line, we calcluate the continued line marker
			 * *before* we commit the first line.  That way, we
			 * can know if the word to be wrapped will overflow
			 * the second line, and in such case, we break that
			 * word precisely at the current point, rather than
			 * at the word_break point!  This prevents the
			 * "line wrap bug", albeit in a confusing way.
			 */

			/*
			 * Calculate the continued line marker.  This is
			 * a little bit tricky because we cant figure it out
			 * until after the first line is done.  The first
			 * time through, cont == empty_string, so if !*cont,
			 * we know it has not been initialized.
			 *
			 * So if it has not been initialized and /set indent
			 * is on, and the place to indent is less than a third
			 * of the screen width and /set continued_line is
			 * less than the indented width, then we pad the 
			 * /set continued line value out to the appropriate
			 * width.
			 */
			if (!cont)
			{
				int	lhs_count = 0;
				int	continued_count = 0;

				/* Because Blackjac asked me to */
				/* The test against 0 is for lines w/o spaces */
				if (indent > max_cols / 3 || indent <= 0)
					indent = max_cols / 3;

				if (do_indent)
				{
				    unsigned char *fixedstr;

				    fixedstr = prepare_display2(cont_ptr, 
							indent, 0, ' ', 1);
				    cont = LOCAL_COPY(fixedstr);
				    new_free(&fixedstr);
				}

				/*
				 * Otherwise, we just use /set continued_line, 
				 * whatever it is.
				 */
				else /* if ((!cont || !*cont) && *cont_ptr) */
					cont = LOCAL_COPY(cont_ptr);

				cont_free = cont = new_normalize_string(cont, 
						0, display_line_mangler);

				/*
				 * XXXX "line wrap bug" fix.  If we are here,
				 * then we are between the first and second
				 * lines, and we might have a word that does
				 * not fit on the first line that also does
				 * not fit on the second line!  We check for
				 * that right here, and if it won't fit on
				 * the next line, we revert "word_break" to
				 * the current position.
				 *
				 * THIS IS UNFORTUNATELY VERY EXPENSIVE! :(
				 */
				c = buffer[word_break];
				buffer[word_break] = 0;
				lhs_count = output_with_count(buffer, 0, 0);
				buffer[word_break] = c;
				continued_count = output_with_count(cont, 0, 0);

				/* 
				 * Chop the line right here if it will
				 * overflow the next line.
				 *
				 * Save the attributes, too! (05/29/02)
				 *
				 * XXX Saving the attributes may be 
				 * spurious but i'm erring on the side
				 * of caution for the moment.
				 */
				if (lhs_count <= continued_count) {
					word_break = pos;
					saved_a = a;
				}

				/*
				 * XXXX End of nasty nasty hack.
				 */
			}

			/*
			 * Now we break off the line at the last space or
			 * last char and copy it off to the master buffer.
			 */
			c = buffer[word_break];
			buffer[word_break] = 0;
			malloc_strcpy((char **)&(output[line++]), buffer);
			buffer[word_break] = c;


			/*
			 * Skip over all spaces that occur after the break
			 * point, up to the right part of the screen (where
			 * we are currently parsing).  This is what allows
			 * lots and lots of spaces to take up their room.
			 * We let spaces fill in lines as much as neccesary
			 * and if they overflow the line we let them bleed
			 * to the next line.
			 */
			while (word_break < pos && buffer[word_break] == ' ')
				word_break++;

			/*
			 * At this point, we still have some junk left in
			 * 'buffer' that needs to be moved to the next line.
			 * But of course, all new lines must be prefixed by
			 * the /set continued_line and /set indent stuff, so
			 * we copy off the stuff we have to a temporary
			 * buffer, copy the continued-line stuff into buffer
			 * and then re-append the junk into buffer.  Then we
			 * fix col and pos appropriately and continue parsing 
			 * str...
			 */
			/* 'pos' has already been incremented... */
			buffer[pos] = 0;
			pos_copy = LOCAL_COPY(buffer + word_break);
			strlcpy(buffer, cont, sizeof(buffer) / 2);
			display_attributes(buffer + strlen(buffer), &olda, &saved_a);
			strlcat(buffer, pos_copy, sizeof(buffer) / 2);
			display_attributes(buffer + strlen(buffer), &olda, &a);

			pos = strlen(buffer);
			/* Watch this -- ugh. how expensive! :( */
			col = output_with_count(buffer, 0, 0);
			word_break = 0;
			my_newline = 0;

			/*
			 * The 'lused' argument allows us to truncate the
			 * parsing at '*lused' lines.  This is most helpful
			 * for the $leftpc() function, which sets a logical
			 * screen width and asks us to "output" one line.
			 */
			if (*lused && line >= *lused)
			{
				*buffer = 0;
				break;
			}
		} /* End of new line handling */
	} /* End of (ptr = str; *ptr && (pos < BIG_BUFFER_SIZE - 8); ptr++) */

        /* Reset all attributes to zero */
        a.bold = a.underline = a.reverse = a.blink = a.altchar = 0;
	a.italic = 0;
        a.color_fg = a.color_bg = a.fg_color = a.bg_color = 0;
	pos += display_attributes(buffer + pos, &olda, &a);
	buffer[pos] = '\0';
	if (*buffer)
		malloc_strcpy((char **)&(output[line++]),buffer);

	recursion--;
	new_free(&output[line]);
	new_free(&cont_free);
	new_free(&str_free);
	*lused = line - 1;
	return output;
}

/*
 * An evil bastard child hack that pulls together the important parts of
 * prepare_display(), fix_string_width(), and output_with_count().
 *
 * This function is evil because it was spawned by copying the above 
 * functions and they were not refactored to make use of this function,
 * which they would do if I were not doing this in a rush.
 *
 * If you change the above three functions, you would do well to make sure to 
 * adjust this function, for if you do not, then HIGGLEDY PIGGLEDY WILL ENSUE.
 *
 * XXX - This is a bletcherous inelegant hack and i hate it.
 *
 * This function is used for:
 *	1. Toplines
 *	2. Continuation line markers
 */
unsigned char *prepare_display2 (const unsigned char *orig_str, int max_cols, int allow_truncate, char fillchar, int denormalize)
{
	int 	pos = 0,            /* Current position in "buffer" */
		col = 0,            /* Current column in display    */
		line = 0,           /* Current pos in "output"      */
		my_newline = 0;        /* Number of newlines           */
	unsigned char 	*str = NULL;
	unsigned char	*retval = NULL;
	size_t		clue = 0;
const 	unsigned char	*ptr;
	unsigned char 	buffer[BIG_BUFFER_SIZE + 1];
	Attribute	a;
	unsigned char *	real_retval;
	int		codepoint;
	char 		utf8str[16];
	char *		x;
	int		cols;

        /* Reset all attributes to zero */
        a.bold = a.underline = a.reverse = a.blink = a.altchar = 0;
        a.color_fg = a.color_bg = a.fg_color = a.bg_color = 0;
	a.italic = 0;

        str = new_normalize_string(orig_str, 3, NORMALIZE);
	buffer[0] = 0;

	/*
	 * Start walking through the entire string.
	 */
	for (ptr = str; *ptr && (pos < BIG_BUFFER_SIZE - 8); )
	{
	    codepoint = next_code_point(&ptr, 1);

	    switch (codepoint)
	    {
		case 7:		/* Bell */
			buffer[pos++] = '\007';
			break;

		case 10:	/* Forced newline */
			my_newline = 1;
			break;

		/* Attribute changes -- copy them unmodified. */
		case 6:
		{
			if (read_attributes(ptr, &a) == 0)
			{
				buffer[pos++] = '\006';
				buffer[pos++] = *ptr++;
				buffer[pos++] = *ptr++;
				buffer[pos++] = *ptr++;
				buffer[pos++] = *ptr++;
			}
			else
				abort();

			continue;          /* Skip the column check */
		}

		/* Any other printable character */
		default:
		{
			/* How many columns does this codepoint take? */
			cols = codepoint_numcolumns(codepoint);
			if (cols == -1)
				cols = 0;
			col += cols;

			ucs_to_utf8(codepoint, utf8str, sizeof(utf8str));
			for (x = utf8str; *x; x++)
				buffer[pos++] = *x;

			break;
		}
	    } /* End of switch (*ptr) */

	    /*
	     * Must check for cols >= maxcols+1 becuase we can have a 
	     * character on the extreme screen edge, and we would still 
	     * want to treat this exactly as 1 line, and col has already 
	     * been incremented.
	     */
	    if ((allow_truncate && col > max_cols) || my_newline)
			break;
	}
	buffer[pos] = 0;
	malloc_strcpy_c((char **)&retval, buffer, &clue);

	/*
	 * If we get here, either we have slurped up 'max_cols' cols, or
	 * we hit a newline, or the string was too short.
	 */
	if (col < max_cols && fillchar != '\0')
	{
		char fillstr[2];
		fillstr[0] = fillchar;
		fillstr[1] = 0;

		/* XXX One col per byte assumption! */
		while (col++ < max_cols)
			malloc_strcat_c((char **)&retval, fillstr, &clue);
	}

	if (denormalize)
		real_retval = denormalize_string(retval);
	else
		real_retval = retval, retval = NULL;

	new_free(&retval);
	new_free(&str);
	return real_retval;
}

/*
 * rite: This is the primary display wrapper to the 'output_with_count'
 * function.  This function is called whenever a line of text is to be
 * displayed to an irc window.  It is assumed that the cursor has been
 * placed in the appropriate position before this function is called.
 *
 * This function will "scroll" the target window.  Note that "scrolling"
 * is both a logical and physical act.  The window needs to be told that
 * a line is going to be output, and so it needs to be able to adjust its
 * top_of_display pointer; the hardware terminal also needs to be scrolled
 * so that there is room to put the new text.  scroll_window() handles both
 * of these tasks for us.
 *
 * output_with_count() actually calls putchar_x() for each character in
 * the string, doing the physical output.  It also emits any attribute
 * markers that are in the string.  It does do a clear-to-line, but it does
 * NOT move the cursor away from the end of the line.  We do that after it
 * has returned.
 *
 * This function is used by both irciiwin_display, and irciiwin_repaint.
 * Dont ever 'fold' it in anywhere.
 *
 * The arguments:
 *	window		- The target window for the output
 *	str		- What is to be outputted
 */
static int 	rite (Window *window, const unsigned char *str)
{
	output_screen = window->screen;
	scroll_window(window);

	if (window->screen && window->display_lines)
		output_with_count(str, 1, foreground);

	window->cursor++;
	return 0;
}

/*
 * This is the main physical output routine.  In its most obvious
 * use, 'cleareol' and 'output' is 1, and it outputs 'str' to the main
 * display (controlled by output_screen), outputting any attribute markers
 * that it finds along the way.  The return value is the number of physical
 * printable characters output.  However, if 'output' is 0, then no actual
 * output is performed, but the counting still takes place.  If 'clreol'
 * is 0, then the rest of the line is not cleared after 'str' has been
 * completely output.  If 'output' is 0, then clreol is ignored.
 *
 * In some cases, you may want to output in multiple calls, and "all_off"
 * should be set to 1 when you're all done with the end of the 
 * If 'output' is 1 and 'all_off' is 1, do a term_all_off() when the output
 * is done.  If 'all_off' is 0, then don't do an all_off, because
 */
size_t 	output_with_count (const unsigned char *str1, int clreol, int output)
{
	int 		beep = 0;
	size_t		out = 0;
	Attribute	a;
	const unsigned char *	str;
	int		codepoint;
	char		utf8str[16];
	char *		x;
	int		cols;

        /* Reset all attributes to zero */
        a.bold = a.underline = a.reverse = a.blink = a.altchar = 0;
        a.color_fg = a.color_bg = a.fg_color = a.bg_color = 0;
	a.italic = 0;

	if (output)
		if (clreol)
			term_clear_to_eol();

	for (str = str1; str && *str; )
	{
	    /* On any error, just stop. */
	    if ((codepoint = next_code_point(&str, 1)) == -1)
		break;

	    switch (codepoint)
	    {
		/* Attribute marker */
		case 6:
		{
			if (read_attributes(str, &a))
				break;
			if (output)
				term_attribute(&a);
			str += 4;
			break;
		}

		/* Terminal beep */
		case 7:
		{
			beep++;
			break;
		}

		/* Non-destructive space */
		case ND_SPACE:
		{
			if (output)
				term_cursor_right();
			out++;		/* Ooops */
			break;
		}

		/* Any other printable character */
		default:
		{
			/* C1 chars will be ignored unless you want them */
			if (codepoint >= 0x80 && codepoint < 0xA0)
			{
				if (!get_int_var(ALLOW_C1_CHARS_VAR))
					break;
			}

			/* How many columns does this codepoint take? */
			cols = codepoint_numcolumns(codepoint);
			if (cols < 0)
				cols = 0;
			out += cols;

			/*
			 * Note that 'putchar_x()' is safe here because 
			 * normalize_string() has already removed all of the 
			 * nasty stuff that could end up getting here.  And
			 * for those things that are nasty that get here, its 
			 * probably because the user specifically asked for it.
			 */
			if (output)
			{
				ucs_to_console(codepoint, utf8str, sizeof(utf8str));
				for (x = utf8str; *x; x++)
					putchar_x(*x);
			}

			break;
		}
	    }
	}

	if (output)
	{
		if (beep)
			term_beep();
		term_all_off();		/* Clean up after ourselves! */
	}

	return out;
}


/*
 * add_to_screen: This adds the given null terminated buffer to the screen.
 * That is, it routes the line to the appropriate window.  It also handles
 * /redirect handling.
 */
void 	add_to_screen (const unsigned char *buffer)
{
	Window *tmp = NULL;
	int	window;
	int	w = 0;

	/*
	 * Just paranoia.
	 */
	if (!get_window_by_refnum(0))
	{
		puts(buffer);
		return;
	}

	if (dumb_mode)
	{
		add_to_lastlog(get_window_refnum(0), buffer);
		if (privileged_output || 
		    do_hook(WINDOW_LIST, "%u %s", get_window_user_refnum(0), buffer))
			puts(buffer);
		fflush(stdout);
		return;
	}

	/* All windows MUST be "current" before output can occur */
	update_all_windows();

	/*
	 * The highest priority is if we have explicitly stated what
	 * window we want this output to go to.
	 */
	if ((window = get_to_window()) > 0)
	{
		if (window_is_valid(window))
		{
			add_to_window(window, buffer);
			return;
		}
	}

	/*
	 * The next priority is "LEVEL_NONE" which is only ever
	 * used by the /WINDOW command, but I'm not even sure it's very
	 * useful.  Maybe I'll think about this again later.
	 */
	else if ((get_who_level() == LEVEL_NONE) && 
	        ((window = get_server_current_window(from_server)) > -1) && 
                (window_is_valid(window)))
	{
		add_to_window(window, buffer);
		return;
	}

	/*
	 * Next priority is if the output is targeted at a certain
	 * user or channel (used for /window channel or /window add targets)
	 */
	else if (get_who_from())
	{
	    if (is_channel(get_who_from()))
	    {
		int	w;

		if (from_server == NOSERV)
		    panic(0, "Output to channel [%s:NOSERV]: %s", get_who_from(), buffer);

		w = get_channel_window(get_who_from(), from_server);
	        if (window_is_valid(w))
		{
		    add_to_window(w, buffer);
		    return;
		}
	    }
	    else
	    {
		w = 0;
		while (traverse_all_windows2(&w))
		{
		    /* Must be for our server */
		    if (get_who_level() != LEVEL_DCC && (get_window_server(w) != from_server))
			continue;

		    /* Must be on the nick list */
		    if (!find_in_list((List *)get_window_nicks(w), get_who_from(), !USE_WILDCARDS))
			continue;

		    add_to_window(w, buffer);
		    return;
		}
	    }
	}

	/*
	 * Check to see if this level should go to current window
	 */
	if ((mask_isset(&current_window_mask, get_who_level())) &&
	    ((window = get_server_current_window(from_server)) > -1) && 
            (window_is_valid(window)))
	{
		add_to_window(window, buffer);
		return;
	}

	/*
	 * Check to see if any window can claim this level
	 */
	w = 0;
	while (traverse_all_windows2(&w))
	{
		Mask	window_mask;
		get_window_mask(w, &window_mask);

		/*
		 * Check for /WINDOW LEVELs that apply
		 */
		if (get_who_level() == LEVEL_DCC && 
			mask_isset(&window_mask, get_who_level()))
		{
			add_to_window(w, buffer);
			return;
		}
		if ((from_server == get_window_server(w) || from_server == NOSERV)
			&& mask_isset(&window_mask, get_who_level()))
		{
			add_to_window(w, buffer);
			return;
		}
	}

	/*
	 * If all else fails, if the current window is connected to the
	 * given server, use the current window.
	 */
	if (get_window_server(0) == from_server)
	{
		add_to_window(0, buffer);
		return;
	}

	/*
	 * And if that fails, look for ANY window that is bound to the
	 * given server (this never fails if we're connected.)
	 */
	w = 0;
	while (traverse_all_windows2(&w))
	{
		if (get_window_server(w) != from_server)
			continue;

		add_to_window(w, buffer);
		return;
	}

	/*
	 * No window found for a server is usually because we're
	 * disconnected or not yet connected.
	 */
	add_to_window(0, buffer);
	return;
}

/*
 * add_to_window: Given a window and a line to display, this handles all
 * of the window-level stuff like the logfile, the lastlog, splitting
 * the line up into rows, adding it to the display (scrollback) buffer, and
 * if we're invisible and the user wants notification, we handle that too.
 *
 * add_to_display_list() handles the *composure* of the buffer that backs the
 * screen, handling HOLD_MODE, trimming the scrollback buffer if it gets too
 * big, scrolling the window and moving the top_of_window pointer as neccesary.
 * It also tells us if we should display to the screen or not.
 *
 * rite() handles the *appearance* of the display, writing to the screen as
 * neccesary.
 */
static void 	add_to_window (int window_, const unsigned char *str)
{
	const char *	pend;
	unsigned char *	strval;
	unsigned char *	free_me = NULL;
        unsigned char **       my_lines;
        int             cols;
	int		numl = 0;
	intmax_t	refnum;
	char *		rewriter = NULL;
	int		mangler = 0;
	Window *	window = get_window_by_refnum_direct(window_);

	if (get_server_redirect(window->server))
		if (redirect_text(window->server, 
			        get_server_redirect(window->server),
				str, NULL, 0))
			return;

	if (!privileged_output)
	{
	   static int recursion = 0;

	   if (!do_hook(WINDOW_LIST, "%u %s", window->user_refnum, str))
		return;

	   /* 
	    * If output rewriting causes more output, (such as a stray error
	    * message) allow a few levels of nesting [just to be kind], but 
	    * cut the recursion off at its knees at 5 levels.  This is an 
	    * entirely arbitrary value.  Change it if you wish.
	    * (Recursion detection by larne in epic4-2.1.3)
	    */
	    recursion++;
	    if (recursion < 5 && (pend = get_string_var(OUTPUT_REWRITE_VAR)))
	    {
		unsigned char	argstuff[10240];

		/* Create $* and then expand with it */
		snprintf(argstuff, 10240, "%u %s", window->user_refnum, str);
		str = free_me = expand_alias(pend, argstuff);
	    }
	    recursion--;
	}

	/* Add to logs + lastlog... */
	if (window->log_rewrite)
		rewriter = window->log_rewrite;
	if (window->log_mangle)
		mangler = window->log_mangle;
	add_to_log(0, window->log_fp, window->refnum, str, mangler, rewriter);
	add_to_logs(window->refnum, from_server, get_who_from(), get_who_level(), str);
	refnum = add_to_lastlog(window->refnum, str);

	/* Add to scrollback + display... */
	cols = window->my_columns;	/* Don't -1 this! already -1'd */
	strval = new_normalize_string(str, 0, display_line_mangler);
        for (my_lines = prepare_display(window->refnum, strval, cols, &numl, 0); *my_lines; my_lines++)
	{
		if (add_to_scrollback(window->refnum, *my_lines, refnum))
		    if (ok_to_output(window))
			rite(window, *my_lines);
	}
	new_free(&strval);

	/* Check the status of the window and scrollback */
	trim_scrollback(window->refnum);

	cursor_to_input();

	/*
	 * Handle special cases for output to hidden windows -- A beep to
	 * a hidden window with /window beep_always on results in a real beep 
	 * and a message to the current window.  Output to a hidden window 
	 * with /window notify on results in a message to the current window 
	 * and a status bar redraw.
	 *
	 * /XECHO -F sets "do_window_notifies" which overrules this.
	 */
	if (!window->screen && do_window_notifies)
	{
	    const char *type = NULL;

            /* /WINDOW BEEP_ALWAYS added for archon.  */
	    if (window->beep_always && strchr(str, '\007'))
	    {
		type = "Beep";
		term_beep();
	    }
	    if (!(window->notified) &&
			mask_isset(&window->notify_mask, get_who_level()))
	    {
		window->notified = 1;
	    	do_hook(WINDOW_NOTIFIED_LIST, "%u %s", window->user_refnum, level_to_str(get_who_level()));
		if (window->notify_when_hidden)
			type = "Activity";
		update_all_status();
	    }

	    if (type)
	    {
		int l = message_setall(get_window_refnum(0), get_who_from(), get_who_level());
		say("%s in window %d", type, window->user_refnum);
		pop_message_from(l);
	    }
	}
	if (free_me)
		new_free(&free_me);
}

/*
 * add_to_window_scrollback: XXX -- doesn't belong here. oh well.
 * This unifies the important parts of add_to_window and window_disp
 * for the purpose of reconstituting the scrollback of a window after
 * a resize event.
 */
void 	add_to_window_scrollback (int window, const unsigned char *str, intmax_t refnum)
{
	unsigned char *	strval;
        unsigned char **       my_lines;
        int             cols;
	int		numl = 0;

	/* Normalize the line of output */
	cols = get_window_my_columns(window);			/* Don't -1 this! Already -1'd! */
	strval = new_normalize_string(str, 0, display_line_mangler);
        for (my_lines = prepare_display(window, strval, cols, &numl, 0); *my_lines; my_lines++)
		add_to_scrollback(window, *my_lines, refnum);
	new_free(&strval);
}

/*
 * This returns 1 if the window does not need to scroll for new output.
 * This returns 0 if the window does need to scroll for new output.
 *
 * This call should be used to guard calls to rite(), because rite() will
 * call scroll_window() if the window is full.  Scroll_window() will panic 
 * if the window is not using the "scrolling" view.  Therefore, this function
 * differentiates between a window that is full because it is in hold mode or
 * scrollback, and a window that is full and can be scrolled.
 */
static int	ok_to_output (Window *window)
{
	/*
	 * Output is ok as long as the three top of displays all are 
	 * within a screenful of the insertion point!
	 */
	if (window->scrollback_top_of_display)
	{
	    if (window->scrollback_distance_from_display_ip > window->display_lines)
		return 0;	/* Definitely no output here */
	}

	if (get_window_hold_mode(window->refnum))
	{
	    if (window->holding_distance_from_display_ip > window->display_lines)
		return 0;	/* Definitely no output here */
	}

	return 1;		/* Output is authorized */
}

/*
 * scroll_window: Given a window, this is responsible for making sure that
 * the cursor is placed onto the "next" line.  If the window is full, then
 * it will scroll the window as neccesary.  The cursor is always set to the
 * correct place when this returns.
 *
 * This is only ever (to be) called by rite(), and you must always call
 * ok_to_output() before you call rite().  If you do not call ok_to_output(),
 * this function will panic if the window needs to be scrolled.
 */
static void 	scroll_window (Window *window)
{
	if (dumb_mode)
		return;

	if (window->cursor > window->display_lines)
		panic(1, "Window [%d]'s cursor [%d] is off the display [%d]",
			window->user_refnum, window->cursor, window->display_lines);

	/*
	 * If the cursor is beyond the window then we should probably
	 * look into scrolling.
	 */
	if (window->cursor == window->display_lines)
	{
		int scroll;

		/*
		 * If we ever need to scroll a window that is in scrollback
		 * or in hold_mode, then that means either display_window isnt
		 * doing its job or something else is completely broken.
		 * Probably shouldnt be fatal, but i want to trap these.
		 */
		if (window->holding_distance_from_display_ip > 
						window->display_lines)
			panic(1, "Can't output to window [%d] "
				"because it is holding stuff: [%d] [%d]", 
				window->user_refnum, 
				window->holding_distance_from_display_ip, 
				window->display_lines);

		if (window->scrollback_distance_from_display_ip > 
						window->display_lines)
			panic(1, "Can't output to window [%d] "
				"because it is scrolling back: [%d] [%d]", 
				window->user_refnum, 
				window->scrollback_distance_from_display_ip, 
				window->display_lines);

		/* Scroll by no less than 1 line */
		if ((scroll = window->scroll_lines) <= 0)
		    if ((scroll = get_int_var(SCROLL_LINES_VAR)) <= 0)
			scroll = 1;
		if (scroll > window->display_lines)
			scroll = window->display_lines;

		/* Adjust the top of the physical display */
		if (window->screen && foreground && window->display_lines)
		{
			term_scroll(window->top,
				window->top + window->cursor - 1, 
				scroll);
		}

		/* Adjust the cursor */
		window->cursor -= scroll;
	}

	/*
	 * Move to the new line and wipe it
	 */
	if (window->screen && window->display_lines)
	{
		term_move_cursor(0, window->top + window->cursor);
		term_clear_to_eol();
	}
}

/* * * * * * * SCREEN UDPATING AND RESIZING * * * * * * * * */
/*
 * repaint_window_body: redraw the entire window's scrollable region
 * The old logic for doing a partial repaint has been removed with prejudice.
 */
void 	repaint_window_body (Window *window)
{
	Display *curr_line;
	int 	count;

	if (!window)
		window = get_window_by_refnum(0);

	if (dumb_mode || !window->screen)
		return;

	global_beep_ok = 0;		/* Suppress beeps */

	if (window->scrollback_distance_from_display_ip > window->holding_distance_from_display_ip)
	{
	    if (window->scrolling_distance_from_display_ip >= window->scrollback_distance_from_display_ip)
		curr_line = window->scrolling_top_of_display;
	    else
		curr_line = window->scrollback_top_of_display;
	}
	else
	{
	    if (window->scrolling_distance_from_display_ip >= window->holding_distance_from_display_ip)
		curr_line = window->scrolling_top_of_display;
	    else
		curr_line = window->holding_top_of_display;
	}

	if (window->screen && window->toplines_showing)
	    for (count = 0; count < window->toplines_showing; count++)
	    {
		int	numls = 1;
		unsigned char **my_lines;
		unsigned char *n, *widthstr;
		const unsigned char *str;

		if (!(str = window->topline[count]))
			str = empty_string;

		term_move_cursor(0, window->top - window->toplines_showing + count);
		term_clear_to_eol();

		widthstr = prepare_display2(str, window->my_columns, 1, ' ', 0);
		output_with_count(widthstr, 1, foreground);
		new_free(&widthstr);

/*
		n = new_normalize_string(widthstr, 0, display_line_mangler);
		my_lines = prepare_display(window->refnum, n, cols, &numls, PREPARE_NOWRAP);
		if (*my_lines)
			output_with_count(*my_lines, 1, foreground);
		new_free(&n);
*/
	   }

	window->cursor = 0;
	for (count = 0; count < window->display_lines; count++)
	{
		rite(window, curr_line->line);

		/*
		 * Clean off the rest of this window.
		 */
		if (curr_line == window->display_ip)
		{
			window->cursor--;		/* Bumped by rite */
			for (; count < window->display_lines; count++)
			{
				term_clear_to_eol();
				term_newline();
			}
			break;
		}

		curr_line = curr_line->next;
	}

	global_beep_ok = 1;		/* Suppress beeps */
}


/* * * * * * * * * * * * * * SCREEN MANAGEMENT * * * * * * * * * * * * */
/*
 * create_new_screen creates a new screen structure. with the help of
 * this structure we maintain ircII windows that cross screen window
 * boundaries.
 *
 * The new screen is stored in "last_input_screen"!
 */
void	create_new_screen (void)
{
	Screen	*new_s = NULL, *list;
	static	int	refnumber = 0;

	for (list = screen_list; list; list = list->next)
	{
		if (!list->alive)
		{
			new_s = list;
			break;
		}

		if (!list->next)
			break;		/* XXXX */
	}

	if (!new_s)
	{
		new_s = (Screen *)new_malloc(sizeof(Screen));
		new_s->screennum = ++refnumber;
		new_s->next = NULL;
		if (list)
			list->next = new_s;
		else
			screen_list = new_s;
	}

	new_s->last_window_refnum = 1;
	new_s->window_list = NULL;
	new_s->window_list_end = NULL;
	new_s->input_window = -1;
	new_s->visible_windows = 0;
	new_s->window_stack = NULL;
	new_s->last_press.tv_sec = new_s->last_press.tv_usec  = 0;
	new_s->last_key = NULL;
	new_s->quote_hit = 0;
	new_s->fdout = 1;
	new_s->fpout = stdout;
#ifdef WITH_THREADED_STDOUT
	new_s->tio_file = tio_open(new_s->fpout);
#endif
	new_s->fdin = 0;
	if (use_input)
		new_open(0, do_screens, NEWIO_READ, 0, -1);
	new_s->fpin = stdin;
	new_s->control = -1;
	new_s->wserv_version = 0;
	new_s->alive = 1;
	new_s->promptlist = NULL;
	new_s->tty_name = (char *) 0;
	new_s->li = current_term->TI_lines;
	new_s->co = current_term->TI_cols;
	new_s->old_li = 0; 
	new_s->old_co = 0;

	new_s->il = new_malloc(sizeof(InputLine));
	new_s->il->input_buffer[0] = '\0';
	new_s->il->first_display_char = 0;
	new_s->il->number_of_logical_chars = 0;
	new_s->il->input_prompt_raw = NULL;
	new_s->il->input_prompt = malloc_strdup(empty_string);
        new_s->il->input_prompt_len = 0;
	new_s->il->input_line = 23;

        new_s->il->ind_left = malloc_strdup(empty_string);
        new_s->il->ind_left_len = 0;
        new_s->il->ind_right = malloc_strdup(empty_string);
        new_s->il->ind_right_len = 0;
	new_s->il->echo = 1;

	last_input_screen = new_s;

	if (!main_screen)
		main_screen = new_s;

	init_input();
}


#define ST_NOTHING      -1
#define ST_SCREEN       0
#define ST_XTERM        1
#define ST_TMUX		2
Window	*create_additional_screen (void)
{
#ifdef NO_JOB_CONTROL
	yell("Your system doesn't support job control, sorry.");
	return NULL;
#else
        Window  	*win;
        Screen  	*oldscreen, *new_s;
        int     	screen_type = ST_NOTHING;
	ISA		local_sockaddr;
        ISA		new_socket;
	int		new_cmd;
	pid_t		child;
	unsigned short 	port;
	socklen_t	new_sock_size;
	const char *	wserv_path;

	char 		subcmd[128];
	char *		opts;
	const char *	xterm;
	char *		args[64];
	char **		args_ptr = args;
	char 		geom[32];
	int 		i;


	/* Don't "move" this down! It belongs here. */
	oldscreen = get_window_screen(0);

	if (!use_input)
		return NULL;

	if (!(wserv_path = get_string_var(WSERV_PATH_VAR)))
	{
		say("You need to /SET WSERV_PATH before using /WINDOW CREATE");
		return NULL;
	}

	/*
	 * Environment variable STY has to be set for screen to work..  so it is
	 * the best way to check screen..  regardless of what TERM is, the 
	 * execpl() for screen won't just open a new window if STY isn't set,
	 * it will open a new screen process, and run the wserv in its first
	 * window, not what we want...  -phone
	 */
	if (getenv("STY") && getenv("DISPLAY"))
	{
		const char *p = get_string_var(WSERV_TYPE_VAR);
		if (p && !my_stricmp(p, "SCREEN"))
			screen_type = ST_SCREEN;
		else if (p && !my_stricmp(p, "TMUX"))
			screen_type = ST_SCREEN;
		else if (p && !my_stricmp(p, "XTERM"))
			screen_type = ST_XTERM;
		else
			screen_type = ST_SCREEN;	/* Sucks to be you */
	}
	else if (getenv("TMUX") && getenv("DISPLAY"))
	{
		const char *p = get_string_var(WSERV_TYPE_VAR);
		if (p && !my_stricmp(p, "SCREEN"))
			screen_type = ST_TMUX;
		else if (p && !my_stricmp(p, "TMUX"))
			screen_type = ST_TMUX;
		else if (p && !my_stricmp(p, "XTERM"))
			screen_type = ST_XTERM;
		else
			screen_type = ST_TMUX;	/* Sucks to be you */
	}
	else if (getenv("TMUX"))
		screen_type = ST_TMUX;
	else if (getenv("STY"))
		screen_type = ST_SCREEN;
	else if (getenv("DISPLAY") && getenv("TERM"))
		screen_type = ST_XTERM;
	else
	{
		say("I don't know how to create new windows for this terminal");
		return NULL;
	}

	if (screen_type == ST_SCREEN)
		say("Opening new screen...");
	else if (screen_type == ST_TMUX)
		say("Opening new tmux...");
	else if (screen_type == ST_XTERM)
		say("Opening new window...");
	else
		panic(1, "Opening new wound");

	local_sockaddr.sin_family = AF_INET;
#ifndef INADDR_LOOPBACK
#define INADDR_LOOPBACK 0x7f000001
#endif
	local_sockaddr.sin_addr.s_addr = htonl((INADDR_ANY));
	local_sockaddr.sin_port = 0;

	if ((new_cmd = client_bind((SA *)&local_sockaddr, sizeof(local_sockaddr))) < 0)
	{
		yell("Couldn't establish server side of new screen");
		return NULL;
	}
	port = ntohs(local_sockaddr.sin_port);

	/* Create the command line arguments... */
	if (screen_type == ST_SCREEN)
	{
	    opts = malloc_strdup(get_string_var(SCREEN_OPTIONS_VAR));
	    *args_ptr++ = malloc_strdup("screen");
	    while (opts && *opts)
		*args_ptr++ = malloc_strdup(new_next_arg(opts, &opts));
	    *args_ptr++ = malloc_strdup(wserv_path);
	    *args_ptr++ = malloc_strdup("localhost");
	    *args_ptr++ = malloc_strdup(ltoa((long)port));
	    *args_ptr++ = NULL;
	}
	else if (screen_type == ST_XTERM)
	{
	    snprintf(geom, 31, "%dx%d", 
		oldscreen->co + 1, 
		oldscreen->li);

	    opts = malloc_strdup(get_string_var(XTERM_OPTIONS_VAR));
	    if (!(xterm = getenv("XTERM")))
		if (!(xterm = get_string_var(XTERM_VAR)))
		    xterm = "xterm";

	    *args_ptr++ = malloc_strdup(xterm);
	    *args_ptr++ = malloc_strdup("-geometry");
	    *args_ptr++ = malloc_strdup(geom);
	    while (opts && *opts)
		*args_ptr++ = malloc_strdup(new_next_arg(opts, &opts));
	    *args_ptr++ = malloc_strdup("-e");
	    *args_ptr++ = malloc_strdup(wserv_path);
	    *args_ptr++ = malloc_strdup("localhost");
	    *args_ptr++ = malloc_strdup(ltoa((long)port));
	    *args_ptr++ = NULL;
	}
	else if (screen_type == ST_TMUX)
	{
	    snprintf(subcmd, 127, "%s %s %hu", 
			wserv_path, "localhost", port);

	    *args_ptr++ = malloc_strdup("tmux");

	    opts = malloc_strdup(get_string_var(TMUX_OPTIONS_VAR));
	    while (opts && *opts)
		*args_ptr++ = malloc_strdup(new_next_arg(opts, &opts));

	    *args_ptr++ = malloc_strdup("new-window");
	    *args_ptr++ = malloc_strdup(subcmd);
	    *args_ptr++ = NULL;
	}

	/* Now create a new screen */
	create_new_screen();
	new_s = last_input_screen;

	/*
	 * At this point, doing a say() or yell() or anything else that would
	 * output to the screen will cause a refresh of the status bar and
	 * input line.  new_s->input_window is -1 after the above line,
	 * so any attempt to reference $C or $T will be to NULL pointers,
	 * which will cause a crash.  For various reasons, we can't fire up
	 * a new window this early, so its just easier to make sure we don't
	 * output anything until kill_screen() or new_window() is called first.
	 * You have been warned!
	 */
	switch ((child = fork()))
	{
		case -1:
		{
			kill_screen(new_s);
			say("I couldnt fire up a new wserv process");
			break;
		}

		case 0:
		{
			if (setgid(getgid()))
				_exit(0);
			if (setuid(getuid()))
				_exit(0);
			setsid();

			/*
			 * Make sure that no inhereted file descriptors
			 * are left over past the exec.  xterm will reopen
			 * any fd's that it is interested in.
			 * (Start at three sb kanan).
			 */
			for (i = 3; i < 256; i++)
				close(i);

			/*
			 * Try to restore some sanity to the signal
			 * handlers, since theyre not really appropriate here
			 */
			my_signal(SIGINT,  SIG_IGN);
			my_signal(SIGSEGV, SIG_DFL);
			my_signal(SIGBUS,  SIG_DFL);
			my_signal(SIGABRT, SIG_DFL);

			execvp(args[0], args);
			_exit(0);
		}
	}

	/* All the rest of this is the parent.... */
	new_sock_size = sizeof(new_socket);

	/* 
	 * This infinite loop sb kanan to allow us to trap transitory
	 * error signals
	 */
	for (;;)

	/* 
	 * You need to kill_screen(new_s) before you do say() or yell()
	 * if you know what is good for you...
	 */
	switch (my_isreadable(new_cmd, 10))
	{
	    case -1:
	    {
		if ((errno == EINTR) || (errno == EAGAIN))
			continue;
		FALLTHROUGH
		/* FALLTHROUGH */
	    }
	    case 0:
	    {
		int 	old_errno = errno;
		int 	errnod = get_child_exit(child);

		close(new_cmd);
		kill_screen(new_s);
		kill(child, SIGKILL);
		if (new_s->fdin != 0)
		{
			say("The wserv only connected once -- it's probably "
			    "an old, incompatable version.");
		}

                yell("child %s with %d", (errnod < 1) ? "signaled" : "exited",
                                         (errnod < 1) ? -errnod : errnod);
		yell("Errno is %d", old_errno);
		return NULL;
	    }
	    default:
	    {
		if (new_s->fdin == 0) 
		{
			new_s->fdin = accept(new_cmd, (SA *)&new_socket,
						&new_sock_size);
			if ((new_s->fdout = new_s->fdin) < 0)
			{
				close(new_cmd);
				kill_screen(new_s);
				yell("Couldn't establish data connection "
					"to new screen");
				return NULL;
			}
			new_open(new_s->fdin, do_screens, NEWIO_RECV, 1, -1);
			new_s->fpin = new_s->fpout = fdopen(new_s->fdin, "r+");
#ifdef WITH_THREADED_STDOUT
			new_s->tio_file = tio_open(new_s->fpout);
#endif
			continue;
		}
		else
		{
			new_s->control = accept(new_cmd, (SA *)&new_socket,
						&new_sock_size);
			close(new_cmd);
			if (new_s->control < 0)
			{
                                kill_screen(new_s);
                                yell("Couldn't establish control connection "
                                        "to new screen");
                                return NULL;
                        }

			new_open(new_s->control, do_screens, NEWIO_RECV, 1, -1);

                        if (!(win = new_window(new_s)))
                                panic(1, "WINDOW is NULL and it shouldnt be!");
                        return win;
		}
	    }
	}
	return NULL;
#endif
}

/* Old screens never die. They just fade away. */
void 	kill_screen (Screen *screen)
{
	Window	*window;

	if (!screen)
	{
		say("You may not kill the hidden screen.");
		return;
	}
	if (main_screen == screen)
	{
		say("You may not kill the main screen");
		return;
	}
	if (screen->fdin)
	{
		if (use_input)
			screen->fdin = new_close(screen->fdin);
		close(screen->fdout);
		close(screen->fdin);
	}
	if (screen->control)
		screen->control = new_close(screen->control);
	while ((window = screen->window_list))
	{
		screen->window_list = window->next;
		add_to_invisible_list(window->refnum);
	}

#ifdef WITH_THREADED_STDOUT
	tio_close(screen->tio_file);
#endif

	/* Take out some of the garbage left around */
	screen->input_window = -1;
	screen->window_list = NULL;
	screen->window_list_end = NULL;
	screen->last_window_refnum = -1;
	screen->visible_windows = 0;
	screen->window_stack = NULL;
	screen->fpin = NULL;
	screen->fpout = NULL;
	screen->fdin = -1;
	screen->fdout = -1;

	/* XXX Should do a proper wiping of the input line */
	new_free(&screen->il->input_prompt);

	/* Dont fool around. */
	if (last_input_screen == screen)
		last_input_screen = main_screen;

	screen->alive = 0;
	make_window_current_by_refnum(0);
	say("The screen is now dead.");
}

int     number_of_windows_on_screen (Screen *screen)
{
        return screen->visible_windows;
}


/* * * * * * * * * * * * * USER INPUT HANDLER * * * * * * * * * * * */
/*
 * do_screens - A NewIO callback to handle input from "a screen" 
 *		(ie, stdin, or a wserv)
 *	fd - the file descriptor that is Ready
 *
 * Process:
 *  1. Identify the screen that 'fd' belongs to.
 *  2. If this is the control fd for a screen, process the control message
 *  3. If this is the data fd for a screen,
 *	a. Establish the context for input (screen/window/server)
 *	b. Read all the bytes
 *	c. Publish the bytes one at a time through edit_char() which will 
 *	   assemble them into codepoints (using iconv) and inject them.
 */
static void 	do_screens (int fd)
{
	Screen *screen;
	char 	buffer[IO_BUFFER_SIZE + 1];
	int	saved_from_server;
	int	n, i;
	int	proto;

	saved_from_server = from_server;

	if (use_input)
	for (screen = screen_list; screen; screen = screen->next)
	{
		if (!screen->alive)
			continue;

		/*
		 * Handle input from a WSERV screen's control channel
		 */
		if (screen->control != -1 && screen->control == fd)
		{
		    if (dgets(screen->control, buffer, IO_BUFFER_SIZE, 1) < 0)
		    {
			kill_screen(screen);
			yell("Error from remote screen.");
			continue;
		    }

		    if (!strncmp(buffer, "tty=", 4))
			malloc_strcpy(&screen->tty_name, buffer + 4);
		    else if (!strncmp(buffer, "geom=", 5))
		    {
			char *ptr;
			if ((ptr = strchr(buffer, ' ')))
				*ptr++ = 0;
			screen->li = atoi(buffer + 5);
			screen->co = atoi(ptr);
			recalculate_windows(screen);
		    }
		    else if (!strncmp(buffer, "version=", 8))
		    {
			int     version;
			version = atoi(buffer + 8);
			if (version != CURRENT_WSERV_VERSION)
			{
			    yell("WSERV version %d is incompatable "
					"with this binary", version);
			    kill_screen(screen);
			}
			screen->wserv_version = version;
		    }
		}


		/*
		 * Handle user input from any screen's fdin channel
		 */
		if (screen->fdin != fd)
			continue;

		/*
		 * If normal data comes in, and the wserv didn't tell me
		 * what its version is, then throw it out of the system.
		 */
		if (screen != main_screen && screen->wserv_version == 0)
		{
			kill_screen(screen);
			yell("The WSERV used to create this new screen "
				"is too old.");
			return;		/* Bail out entirely */
		}

		/* Reset the idleness data for the user */
		get_time(&idle_time);
		if (cpu_saver)
			reset_system_timers();

		/* 
		 * Set the global processing context for the client to the 
		 * processing context of the screen's current input window.
		 */
		last_input_screen = screen;
		output_screen = screen;
		make_window_current_by_refnum(screen->input_window);
		from_server = get_window_server(0);

		/*
		 * PRIVMSG/NOTICE restrictions are suspended
		 * when you're typing something.
		 */
		proto = get_server_protocol_state(from_server);
		set_server_protocol_state(from_server, 0);

		/*
		 * We create a 'stack' of current windows so whenever a 
		 * window is killed, we know which window was the current
		 * window immediately prior to it.
		 */
		set_window_priority(0, current_window_priority++);

		/* Dumb mode only accepts complete lines from the user */
		if (dumb_mode)
		{
			if (dgets(screen->fdin, buffer, IO_BUFFER_SIZE, 1) < 0)
			{
				say("IRCII exiting on EOF from stdin");
				irc_exit(1, "EPIC - EOF from stdin");
			}

			if (strlen(buffer))
				buffer[strlen(buffer) - 1] = 0;
			parse_statement(buffer, 1, NULL);
		}

		/* Ordinary full screen input is handled one byte at a time */
		else if ((n = dgets(screen->fdin, buffer, BIG_BUFFER_SIZE, -1)) > 0)
		{
			for (i = 0; i < n; i++)
				translate_user_input(buffer[i]);
		}

		/* An EOF/error error on a wserv screen kills that screen */
		else if (screen != main_screen)
			kill_screen(screen);

		/* An EOF/error on main screen kills the whole client. */
		else
			irc_exit(1, "Hey!  Where'd my controlling terminal go?");

		set_server_protocol_state(from_server, proto);
	}

	from_server = saved_from_server;
} 

/*
 * Each byte received by user input goes through this function.
 * This function needs to decide what to do -- do we accumulate
 * a UTF8 string, convert it to UCS32, and send it to edit_char()?
 * or do we translate it based on /set translation and send that
 * to edit_char()?  Perhaps we have to apply /set'ings, etc.
 */
void	translate_user_input (unsigned char byte)
{
static	unsigned char	workbuf[32];
static	size_t		workbuf_idx = 0;
const	unsigned char *	s;
	char		dest_ptr[32] = { 0 };
	size_t		dest_left;
	int		codepoint;
	char *		in;
	size_t		inlen;
	char *		out;
	size_t		outlen;
	iconv_t		xlat;
	int		n;
const 	char *		enc;
static	int		prev_char = -1;
static	int		suspicious = 0;
static	int		never_warn_again = 0;

	/*
	 * This is "impossible", but I'm just being safe.
	 */
	if (workbuf_idx > 30)
	{
		/* Whatever we have isn't useful. flush it. */
		workbuf_idx = 0;
		*workbuf = 0;
		prev_char = -1;
		suspicious = 0;
		return;
	}

	workbuf[workbuf_idx++] = byte;
	workbuf[workbuf_idx] = 0;

	in = workbuf;
	inlen = workbuf_idx;
	/* Must leave at least one \0 at the end of the buffer, as
	 * next_code_point() expects a nul-terminated string. */
	out = dest_ptr;
	outlen = sizeof dest_ptr - 1;

	enc = find_recoding("console", &xlat, NULL);

	/* Very crude, ad-hoc check for UTF8 type things */
	if (strcmp(enc, "UTF-8") &&  (prev_char == -1 || ((prev_char & 0x80) == 0x80)))
	{
	    if ((prev_char & 0xE0) == 0xC0)
	    {
		if ((byte & 0xC0) == 0x80)
		  suspicious++;
		else
		  suspicious = 0;
	    }
	    if ((prev_char & 0xF0) == 0xE0)
	    {
		if ((byte & 0xC0) == 0x80)
		  suspicious++;
		else
		  suspicious = 0;
	    }
	    if ((prev_char & 0xF8) == 0xF0)
	    {
		if ((byte & 0xC0) == 0x80)
		   suspicious++;
		else
		   suspicious = 0;
	    }
	    if (suspicious == 3 && never_warn_again == 0)
	    {
		yell("Your /ENCODING CONSOLE is %s but you seem to be typing UTF-8.", enc);
		yell("   Use %s/ENCODING CONSOLE UTF-8.%s if you really are using UTF-8", BOLD_TOG_STR, BOLD_TOG_STR);
		never_warn_again = 1;
	    }
	}
	prev_char = byte;

	/* Convert whatever the user is typing into UTF8 */
	if (iconv(xlat, &in, &inlen, &out, &outlen) != 0)
	{
		if (errno == EILSEQ)
		{
			yell("Your /ENCODING CONSOLE is %s which is wrong.", enc);
			yell("   Use %s/ENCODING CONSOLE ISO-8859-1.%s or whatever is correct for you", BOLD_TOG_STR, BOLD_TOG_STR);
			workbuf_idx = 0;
			workbuf[0] = 0;
			return;
		}
		else if  (errno == EINVAL)
		{
			return;		/* Not enough to convert yet */
		}
	}

	/* Now inject the converted (utf8) bytes into the input stream. */
	s = (const unsigned char *)dest_ptr;
	codepoint = next_code_point(&s, 1);
	if (codepoint > -1)
	{
		/* Clear the buffer BEFORE dispatching the results */
		workbuf_idx = 0;
		workbuf[0] = 0;
		edit_codepoint(codepoint);
	}
}

/*
 * This should be called once for each codepoint we receive.
 * This means a utf8 string converted into UCS32.  We don't support
 * that quite yet, so this is just a placeholder.
 *
 * edit_char: handles each character for an input stream.  Not too difficult
 * to work out.
 */
static	void	edit_codepoint (u_32int_t key)
{
	int	old_quote_hit;

        if (dumb_mode)
                return;

        /*
         * This is only used by /pause to see when a keypress event occurs,
         * but not to impact how that keypress is handled at all.
         */
        if (last_input_screen->promptlist &&
            last_input_screen->promptlist->type == WAIT_PROMPT_DUMMY)
		fire_wait_prompt(key);

        /* were we waiting for a keypress? */
        if (last_input_screen->promptlist && 
                last_input_screen->promptlist->type == WAIT_PROMPT_KEY)
        {
		fire_wait_prompt(key);
		return;
        }

	/*
	 * New: handle_keypress now takes "quote" argument, where it will
	 * treat this key as though it were bound to self_insert, rather
	 * than whatever it is.  This will allow me to (eventually) implement
	 * a self_insert toggle mode.   In any case, we always clear the
	 * quote_hit flag when we're done. (Is it cheaper to unconditionally
	 * set it to 0, or to do a test-and-set?  Does it matter?)
	 */
	old_quote_hit = last_input_screen->quote_hit;

	last_input_screen->last_key = handle_keypress(
		last_input_screen->last_key,
		last_input_screen->last_press, key,
		last_input_screen->quote_hit);
	get_time(&last_input_screen->last_press);

	if (old_quote_hit)
		last_input_screen->quote_hit = 0;
}


void	fire_wait_prompt (u_32int_t key)
{
	WaitPrompt *	oldprompt;
        unsigned char   utf8str[8];

	oldprompt = last_input_screen->promptlist;
	last_input_screen->promptlist = oldprompt->next;
	last_input_screen->il = oldprompt->saved_input_line;
	update_input(last_input_screen, UPDATE_ALL);

	ucs_to_utf8(key, utf8str, sizeof(utf8str));
	(*oldprompt->func)(oldprompt->data, utf8str);
	destroy_prompt(last_input_screen, &oldprompt);

}

void	fire_normal_prompt (const char *utf8str)
{
	WaitPrompt *	oldprompt;

	oldprompt = last_input_screen->promptlist;
	last_input_screen->promptlist = oldprompt->next;
	last_input_screen->il = oldprompt->saved_input_line;
	update_input(last_input_screen, UPDATE_ALL);

	(*oldprompt->func)(oldprompt->data, utf8str);
	destroy_prompt(last_input_screen, &oldprompt);
}


/* * * * * * * * * INPUT PROMPTS * * * * * * * * * * */
/* 
 * add_wait_prompt: Start a modal input prompt, which interrupts the user
 *  and requires them to answer a prompt before continuing.
 *
 * This function is asynchronous (it returns immediately, and your function
 * will be called some time later), so make sure to structure your code
 * appropriately.
 *
 * Users don't like being interrupted by modal inputs, so you shouldn't use
 * this function unless it's for something so critical that the user 
 * shouldn't continue without answering.
 *
 * As of the time of this writing (10/13) your callback function does not
 * need to be utf8 aware, but that is going to change very soon.
 *
 * Arguments:
 *	prompt	The prompt to display to the user.  This will interrupt
 *		the normal input processing, so it is a "modal input"
 *	func	A function to call back when the user has answered the 
 *		prompt.  It will be passed back two arguments:
 *		1. A pointer to "data" (see next)
 *		2. A C string (possibly encoded in UTF8 in the future)
 *	   	containing the user's response.
 *  	data	A payload of data to be passed to your callback.
 *		**NOTE -- It is important that this be new_malloc()ed memory.
 *		YOU are responsible for new_free()ing it in the callback.
 *		This may be NULL.
 *  	type	One of:
 *		WAIT_PROMPT_LINE - An entire line of input (up through SENDLINE)
 *		WAIT_PROMPT_KEY - The next keypress (possibly UTF8 code point)
 *		WAIT_PROMPT_DUMMY - Internal use only (for /PAUSE)
 *	echo	Whether to display the characters as they are typed (1) or to
 *	   	not display the characters (0).  Useful for passwords.
 *	   	NOTE - Even if echo == 0, the cursor may move each keypress,
 *	   	so this isn't a serious security protection.
 *
 * XXX - Prompts are basically global -- they don't know about windows, and
 *       therefore they don't know about servers.  That is probably wrong.  
 *       You don't want your callback firing off in the wrong context, do you?
 *	 Suggestion: "data" should be a struct that contains window and server 
 * 	 context information.
 */
void 	add_wait_prompt (const char *prompt, void (*func)(char *data, const char *utf8str), const char *data, int type, int echo)
{
	WaitPrompt **AddLoc,
		   *New;
	Screen *	s, *old_last_input_screen;;

	old_last_input_screen = last_input_screen;

	if (get_window_screen(0))
		s = get_window_screen(0);
	else
		s = main_screen;

	last_input_screen = s;
	New = (WaitPrompt *) new_malloc(sizeof(WaitPrompt));
	New->data = malloc_strdup(data);
	New->type = type;
	New->func = func;

	New->my_input_line = (InputLine *)new_malloc(sizeof(InputLine));
        New->my_input_line->input_buffer[0] = '\0';
        New->my_input_line->first_display_char = 0;
        New->my_input_line->number_of_logical_chars = 0;
	New->my_input_line->input_prompt_raw = malloc_strdup(prompt);
        New->my_input_line->input_prompt = malloc_strdup(empty_string);
        New->my_input_line->input_prompt_len = 0;
        New->my_input_line->input_line = 23;
        New->my_input_line->ind_left = malloc_strdup(empty_string);
        New->my_input_line->ind_left_len = 0;
        New->my_input_line->ind_right = malloc_strdup(empty_string);
        New->my_input_line->ind_right_len = 0;
	New->my_input_line->echo = echo;
	New->saved_input_line = s->il;
	s->il = New->my_input_line;

	New->next = s->promptlist;
	s->promptlist = New;

	init_input();
	update_input(last_input_screen, UPDATE_ALL);
	last_input_screen = old_last_input_screen;
}

static void	destroy_prompt (Screen *s, WaitPrompt **oldprompt)
{
	/* s->il = (*oldprompt)->saved_input_line; */

	new_free(&(*oldprompt)->my_input_line->ind_right);
	new_free(&(*oldprompt)->my_input_line->ind_left);
	new_free(&(*oldprompt)->my_input_line->input_prompt);
	new_free(&(*oldprompt)->my_input_line->input_prompt_raw);
	new_free((char **)&(*oldprompt)->my_input_line);
	new_free(&(*oldprompt)->data);
	new_free((char **)oldprompt);

	update_input(last_input_screen, UPDATE_ALL);
}

/* 
 * chop_columns - Remove the first 'num' columns from 'str'.
 * Arguments:
 *	str	- A pointer to a Type 0 normalized string
 *		  Passing in a non-normalized string will probably crash
 * Return Value:
 *	'str' is changed to point to the start of the 'num'th column
 *	in the original value of 'str'.
 *
 * Example:
 *	*str = "one two three"
 *	num = 2
 * results in
 *	*str = "e two three"
 *
 * This is modeled after output_with_count, and denormalize_string().
 * All these functions should be refactored somehow.  Too much copypasta!
 */
void	chop_columns (unsigned char **str, size_t num)
{
	char 	*s, *x;
	int	i, d, c;
	int	codepoint, cols;

	if (!str || !*str)
		return;

	for (s = *str; s && *s; s = x)
	{
		/* 
		 * This resyncs to UTF-8; 
		 * In the worst case, eventually codepoint == 0 at EOS.
		 */
		x = s;
		while ((codepoint = next_code_point((const unsigned char **)&x, 0)) == -1)
			x++;

		/* 
		 * \006 is the "attribute marker" which is followed by
		 * four bytes which we don't care about.  the whole
	 	 * thing takes up 0 columns.
		 */
		if (codepoint == 6)
		{
			for (i = 0; i < 4; i++)
			{
				x++;
				if (!*x)
					break;
			}
			continue;
		}
		/* \007 is the beep -- and we don't care. */
		else if (codepoint == 7)
			continue;
		/* The ND_SPACE does take up a column */
		else if (codepoint == ND_SPACE)
			cols = 1;
		/* Everything else is evaluated by codepoint_numcolumns */
		else 
		{
			cols = codepoint_numcolumns(codepoint);
			if (cols == -1)
				cols = 0;
		}

		/*
		 * NOW -- here is the tricky part....
		 *
		 * Why did we do the above on 'x' ?
		 * Because we need to keep slurping up code points
		 * after 'num == 0' because of things like continuation
		 * points and highlight chars.
		 * So once num == 0 *AND* we have a cp that takes up a column,
		 * that's where we stop.
		 */
		if (num <= 0 && cols > 0)
		{
			break;	/* Remember, DON'T include the char we 
				 * just evaluated! */
		}

		num -= cols;
	}

	*str = s;
}


/*
 * I suppose this is cheating -- but since it is only used by 
 * $fix_width(), do i really care?
 */
void	chop_final_columns (unsigned char **str, size_t num)
{
	char 	*s, *x;
	int	i, d, c;
	int	cols, codepoint;
	size_t	numcols;

	if (!str || !*str)
		return;

	/* 
	 * Why do i go forward rather than backward?
	 * The attribute marker includes 4 printable chars,
	 * so we cannot just evalute the length of a string
	 * from the end->start without accounting for that.
	 * so it just makes more sense to go start->end
	 * even though that's not optimally efficient.
	 */
	for (s = *str; s && *s; s = x)
	{
		/* 
		 * This resyncs to UTF-8; 
		 * In the worst case, eventually codepoint == 0 at EOS.
		 */
		x = s;
		while ((codepoint = next_code_point((const unsigned char **)&x, 0)) == -1)
			x++;

		/* 
		 * \006 is the "attribute marker" which is followed by
		 * four bytes which we don't care about.  the whole
	 	 * thing takes up 0 columns.
		 */
		if (codepoint == 6)
		{
			x += 4;
			continue;
		}
		/* \007 is the beep -- and we don't care. */
		else if (codepoint == 7)
			continue;
		else
		{
			/* Skip unprintable chars */
			cols = codepoint_numcolumns(codepoint);
			if (cols == -1)
				continue;

			/* 
			 * Now we're looking at a printable char. 
			 * Is string, starting at this char, <= our target? 
			 * If so, then we're done. 
			 */
			numcols = output_with_count(s, 0, 0);
			if (numcols <= num)
			{
				/* Truncate and stop */
				*s = 0;
				break;
			}
		}
	}
}

int	parse_mangle (const char *value, int nvalue, char **rv)
{
	char	*str1, *str2;
	char	*copy;
	char	*nv = NULL;

	if (rv)
		*rv = NULL;

	if (!value)
		return 0;

	copy = LOCAL_COPY(value);

	while ((str1 = new_next_arg(copy, &copy)))
	{
		while (*str1 && (str2 = next_in_comma_list(str1, &str1)))
		{
			     if (!my_strnicmp(str2, "ALL_OFF", 4))
				nvalue |= STRIP_ALL_OFF;
			else if (!my_strnicmp(str2, "-ALL_OFF", 5))
				nvalue &= ~(STRIP_ALL_OFF);
			else if (!my_strnicmp(str2, "ALL", 3))
				nvalue = (0x7FFFFFFF ^ (MANGLE_ESCAPES) ^ (STRIP_OTHER) ^ (STRIP_UNPRINTABLE));
			else if (!my_strnicmp(str2, "-ALL", 4))
				nvalue = 0;
			else if (!my_strnicmp(str2, "ALT_CHAR", 3))
				nvalue |= STRIP_ALT_CHAR;
			else if (!my_strnicmp(str2, "-ALT_CHAR", 4))
				nvalue &= ~(STRIP_ALT_CHAR);
			else if (!my_strnicmp(str2, "ANSI", 2))
				nvalue |= NORMALIZE;
			else if (!my_strnicmp(str2, "-ANSI", 3))
				nvalue &= ~(NORMALIZE);
			else if (!my_strnicmp(str2, "BLINK", 2))
				nvalue |= STRIP_BLINK;
			else if (!my_strnicmp(str2, "-BLINK", 3))
				nvalue &= ~(STRIP_BLINK);
			else if (!my_strnicmp(str2, "BOLD", 2))
				nvalue |= STRIP_BOLD;
			else if (!my_strnicmp(str2, "-BOLD", 3))
				nvalue &= ~(STRIP_BOLD);
			else if (!my_strnicmp(str2, "COLOR", 1))
				nvalue |= STRIP_COLOR;
			else if (!my_strnicmp(str2, "-COLOR", 2))
				nvalue &= ~(STRIP_COLOR);
			else if (!my_strnicmp(str2, "ESCAPE", 1))
				nvalue |= MANGLE_ESCAPES;
			else if (!my_strnicmp(str2, "-ESCAPE", 2))
				nvalue &= ~(MANGLE_ESCAPES);
			else if (!my_strnicmp(str2, "ND_SPACE", 2))
				nvalue |= STRIP_ND_SPACE;
			else if (!my_strnicmp(str2, "-ND_SPACE", 3))
				nvalue &= ~(STRIP_ND_SPACE);
			else if (!my_strnicmp(str2, "NORMALIZE", 3))
				nvalue |= NORMALIZE;
			else if (!my_strnicmp(str2, "-NORMALIZE", 4))
				nvalue &= ~(NORMALIZE);
			else if (!my_strnicmp(str2, "NONE", 2))
				nvalue = 0;
			else if (!my_strnicmp(str2, "OTHER", 2))
				nvalue |= STRIP_OTHER;
			else if (!my_strnicmp(str2, "-OTHER", 3))
				nvalue &= ~(STRIP_OTHER);
			else if (!my_strnicmp(str2, "REVERSE", 2))
				nvalue |= STRIP_REVERSE;
			else if (!my_strnicmp(str2, "-REVERSE", 3))
				nvalue &= ~(STRIP_REVERSE);
			else if (!my_strnicmp(str2, "UNDERLINE", 3))
				nvalue |= STRIP_UNDERLINE;
			else if (!my_strnicmp(str2, "-UNDERLINE", 4))
				nvalue &= ~(STRIP_UNDERLINE);
			else if (!my_strnicmp(str2, "UNPRINTABLE", 3))
				nvalue |= STRIP_UNPRINTABLE;
			else if (!my_strnicmp(str2, "-UNPRINTABLE", 4))
				nvalue &= ~(STRIP_UNPRINTABLE);
		}
	}

	if (rv)
	{
		if (nvalue & MANGLE_ESCAPES)
			malloc_strcat_wordlist(&nv, space, "ESCAPE");
		if (nvalue & NORMALIZE)
			malloc_strcat_wordlist(&nv, space, "NORMALIZE");
		if (nvalue & STRIP_COLOR)
			malloc_strcat_wordlist(&nv, space, "COLOR");
		if (nvalue & STRIP_REVERSE)
			malloc_strcat_wordlist(&nv, space, "REVERSE");
		if (nvalue & STRIP_UNDERLINE)
			malloc_strcat_wordlist(&nv, space, "UNDERLINE");
		if (nvalue & STRIP_BOLD)
			malloc_strcat_wordlist(&nv, space, "BOLD");
		if (nvalue & STRIP_BLINK)
			malloc_strcat_wordlist(&nv, space, "BLINK");
		if (nvalue & STRIP_ALT_CHAR)
			malloc_strcat_wordlist(&nv, space, "ALT_CHAR");
		if (nvalue & STRIP_ND_SPACE)
			malloc_strcat_wordlist(&nv, space, "ND_SPACE");
		if (nvalue & STRIP_ALL_OFF)
			malloc_strcat_wordlist(&nv, space, "ALL_OFF");
		if (nvalue & STRIP_UNPRINTABLE)
			malloc_strcat_wordlist(&nv, space, "UNPRINTABLE");
		if (nvalue & STRIP_OTHER)
			malloc_strcat_wordlist(&nv, space, "OTHER");

		*rv = nv;
	}

	return nvalue;
}

