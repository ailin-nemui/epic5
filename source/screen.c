/*
 * screen.c
 *
 * Written By Matthew Green, based on window.c by Michael Sandrof
 * Copyright(c) 1993 Matthew Green
 * Significant modifications by Jeremy Nelson
 * Copyright 1997 EPIC Software Labs,
 * Significant additions by J. Kean Johnston
 * Copyright 1998 J. Kean Johnston, used with permission
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 */

#define __need_putchar_x__
#define __need_term_flush__
#include "irc.h"
#include "alias.h"
#include "exec.h"
#include "screen.h"
#include "window.h"
#include "output.h"
#include "vars.h"
#include "server.h"
#include "list.h"
#include "term.h"
#include "names.h"
#include "ircaux.h"
#include "input.h"
#include "log.h"
#include "hook.h"
#include "dcc.h"
#include "exec.h"
#include "status.h"
#include "commands.h"
#include "parse.h"
#include "newio.h"

#define CURRENT_WSERV_VERSION	4

/*
 * When some code wants to override the default lastlog level, and needs
 * to have some output go into some explicit window (such as for /xecho -w),
 * then while to_window is set to some window, *ALL* output goes to that
 * window.  Dont forget to reset it to NULL when youre done!  ;-)
 */
	Window	*to_window;

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
 * Ugh.  Dont ask.
 */
	int	normalize_never_xlate = 0;
	int	normalize_permit_all_attributes = 0;

/*
 * This file includes major work contributed by FireClown, and I am indebted
 * to him for the work he has graciously donated to the project.  The major
 * highlights of his work include:
 *
 * -- ^C codes have been changed to mIRC-order.  This is the order that
 *    BitchX uses as well, so those scripts that use ^CXX should work without
 *    changes now between epic and bitchx.
 * -- The old "ansi-order" ^C codes have been preserved, but in a different
 *    way.  If you do ^C30 through ^C37, you will set the foreground color
 *    (directly corresponding to the ansi codes for 30-37), and if you do 
 *    ^C40 through ^C47, you will set the background.  ^C50 through ^C57
 *    are reserved for bold-foreground, and blink-background.
 * -- $cparse() still outputs the "right" colors, so if you use $cparse(),
 *    then these changes wont affect you (much).
 * -- Colors and ansi codes are either graciously handled, or completely
 *    filtered out.  Anything that cannot be handled is removed, so there
 *    is no risk of dangerous codes making their way to your output.  This
 *    is accomplished by a low-grade ansi emulator that folds raw output 
 *    into an intermediate form which is used by the display routines.
 *
 * To a certain extent, the original code from  FireClown was not yet complete,
 * and it was evident that the code was in anticipation of some additional
 * future work.  We have completed much of that work, and we are very much
 * indebted to him for getting the ball rolling and supplying us with ideas. =)
 */


/* * * * * * * * * * * * * OUTPUT CHAIN * * * * * * * * * * * * * * * * * * *
 * To put a message to the "default" window, you must first call
 *	set_display_target(nick/channel, lastlog_level)
 * Then you may call 
 *	say(), output(), yell(), put_it(), put_echo(), etc.
 * When you are done, make sure to
 *	reset_display_target()
 *
 * To put a message to a specific, known window (you need it's refnum)
 * then you may just call directly:
 *	display_to(winref, ...)
 *
 * To put a series of messages to a specific, known window, (need it's refnum)
 * You must first call:
 *	message_to(winref)
 * Then you may call
 *	say(), ouitput(), yell(), put_it(), put_echo(), etc.
 * When you are done, make sure to
 *	message_to(-1);
 *
 * The 'display' (or 'display_to') functions are the main entry point for
 * all logical output from epic.  These functions then figure out what
 * window the output will go to and invoke its 'add' function.  From there,
 * whatever happens is implementation defined.
 *
 * This file implements the middle part of the "ircII window", everything
 * from the 'add' function to the low level terminal stuff.
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
static int 	rite 		    (Window *window, const unsigned char *str);
static void 	scroll_window 	    (Window *window);
static void 	add_to_window (Window *window, const unsigned char *str);
static void    window_disp (Window *window, const unsigned char *str, const unsigned char *orig_str);
static int 	add_to_display_list (Window *window, const unsigned char *str);

/*
 * XXX -- Full disclosure -- FireClown says it is completely the wrong
 * idea to do this (re-build attributes from scratch every time) because 
 * it causes those using slow terminals or slow connections more pain than
 * is absolutely neccesary.  While I admit that he has a lot more experience
 * than I do in all this, I'm not sure I have the ability to do all this 
 * "optimally" while ensuring 100% accuracy.  Maybe I'll luck out and he 
 * will provide something that will be optimal *and* 100% accurate. ;-)
 */

/*
 * "Attributes" were an invention for epic5, and the general idea was
 * to expunge from the output chain all of those nasty logical toggle settings
 * which never really did work correctly.  Rather than have half a dozen
 * functions all keep state about whether reverse/bold/underline/whatever is
 * on, or off, or what to do when it sees a toggle, we instead have one 
 * function (normalize_string) which walks the string *once* and outputs a
 * completely normalized output string.  The end result of this change is
 * that what were formally "toggle" attributes now are "always-on" attributes,
 * and to turn off an attribute, you need to do an ALL_OFF (^O) and then
 * turn on whatever attributes are left.  This is *significantly* easier
 * to parse, and yeilds much better results, at the expense of a few extra
 * bytes.
 *
 * Now on to the nitty gritty.  Every character has a fudamental set of
 * attributes that apply to it.  Each character has, by default, the same
 * set of fundamental attributes as the character before it.  In any case
 * where this is NOT true, an "attribute marker" is put into the normalized
 * output to indicate what the new fundamental attributes are.  These new
 * attributes continue to be used until another attribute marker is found.
 *
 * The "Attribute" structure is an internal structure that represents all
 * of the supported fundamental attributes.  This is the prefered method
 * for keeping state of the attributes of a line.  You can convert this
 * structure into an "attribute marker" by passing the string and an 
 * Attribute struct to 'display_attributes'.  The result is 5 bytes of
 * output, each byte has the high bit set (so str*() still work).  You can
 * also convert an Attribute struct to standard ircII attribute characters
 * by calling 'logical_attributes'.  The result will be an ALL_OFF (^O) 
 * followed by all of the attributes that are ON in the struct.  Finally,
 * you can suppress all attribute changes by calling ignore_attribute().
 * These functions are used by normalize_string() to for their appropriate
 * uses.
 *
 * You can read an attribute marker from a string and convert it back to
 * an Attribute struct by calling the read_attributes() function.  You can
 * actually perform the physical output operations neccesary to switch to
 * the values in an Attribute struct by calling term_attribute().  These
 * are used by various output routines for whatever reason.
 */
struct 	attributes {
	int	reverse;
	int	bold;
	int	blink;
	int	underline;
	int	altchar;
	int	color_fg;
	int	color_bg;
	int	fg_color;
	int	bg_color;
};
typedef struct attributes Attribute;

const char *all_off (void)
{
#ifdef NO_CHEATING
	Attribute 	a;
	static	char	retval[6];

	a->reverse = a->bold = a->blink = a->underline = a->altchar = 0;
	a->color_fg = a->fg_color = a->color_bg = a->bg_color = 0;
	display_attributes(retval, &a);
	return retval;
#else
	static	char	retval[6];
	retval[0] = '\006';
	retval[1] = retval[2] = retval[3] = retval[4] = 0x80;
	retval[5] = 0;
	return retval;
#endif
}

/* Put into 'output', an attribute marker corresponding to 'a' */
static size_t	display_attributes (u_char *output, Attribute *a)
{
	u_char	val1 = 0x80;
	u_char	val2 = 0x80;
	u_char	val3 = 0x80;
	u_char	val4 = 0x80;

	if (a->reverse)		val1 |= 0x01;
	if (a->bold)		val1 |= 0x02;
	if (a->blink)		val1 |= 0x04;
	if (a->underline)	val1 |= 0x08;
	if (a->altchar)		val1 |= 0x10;

	if (a->color_fg) {	val2 |= 0x01; val3 |= a->fg_color; }
	if (a->color_bg) {	val2 |= 0x02; val4 |= a->bg_color; }

	output[0] = '\006';
	output[1] = val1;
	output[2] = val2;
	output[3] = val3;
	output[4] = val4;
	output[5] = 0;
	return 5;
}
 
/* Put into 'output', logical characters so end result is 'a' */
static size_t	logic_attributes (u_char *output, Attribute *a)
{
	char	*str = output;
	size_t	count = 0;

	*str++ = ALL_OFF, count++;
	/* Colors need to be set first, always */
	if (a->color_fg)
	{
		*str++ = '\003', count++;
		*str++ = '3', count++;
		*str++ = '0' + a->fg_color, count++;
	}
	if (a->color_bg)
	{
		if (!a->color_fg)
			*str++ = '\003', count++;
		*str++ = ',', count++;
		*str++ = '4', count++;
		*str++ = '0' + a->bg_color, count++;
	}
	if (a->bold)
		*str++ = BOLD_TOG, count++;
	if (a->blink)
		*str++ = BLINK_TOG, count++;
	if (a->reverse)
		*str++ = REV_TOG, count++;
	if (a->underline)
		*str++ = UND_TOG, count++;
	if (a->altchar)
		*str++ = ALT_TOG, count++;
	return count;
}

/* Suppress any attribute changes in the output */
static size_t	ignore_attributes (u_char *output, Attribute *a)
{
	return 0;
}

/* Read an attribute marker from 'input', put results in 'a'. */
static int	read_attributes (const u_char *input, Attribute *a)
{
	if (!input)
		return -1;
	if (*input != '\006')
		return -1;
	if (!input[0] || !input[1] || !input[2] || !input[3] || !input[4])
		return -1;

	a->reverse = a->bold = a->blink = a->underline = a->altchar = 0;
	a->color_fg = a->fg_color = a->color_bg = a->bg_color = 0;

	input++;
	if (*input & 0x01)	a->reverse = 1;
	if (*input & 0x02)	a->bold = 1;
	if (*input & 0x04)	a->blink = 1;
	if (*input & 0x08)	a->underline = 1;
	if (*input & 0x10)	a->altchar = 1;

	input++;
	if (*input & 0x01) {	
		a->color_fg = 1; 
		a->fg_color = input[1] & 0x7F; 
	}
	if (*input & 0x02) {	
		a->color_bg = 1; 
		a->bg_color = input[2] & 0x7F; 
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

	if (a->color_fg) {	if (a->fg_color > 7) abort(); 
				else term_set_foreground(a->fg_color); }
	if (a->color_bg) {	if (a->bg_color > 7) abort();
				else term_set_background(a->bg_color); }
}

/* * * * * * * * * * * * * COLOR SUPPORT * * * * * * * * * * * * * * * * */
/*
 * This parses out a ^C control sequence.  Note that it is not acceptable
 * to simply slurp up all digits after a ^C sequence (either by calling
 * strtol(), or while (isdigit())), because people put ^C sequences right
 * before legit output with numbers (like the time in your status bar.)
 * Se we have to actually slurp up only those digits that comprise a legal
 * ^C code.
 */
const u_char *read_color_seq (const u_char *start, void *d)
{
	/* 
	 * The proper "attribute" color mapping is for each ^C lvalue.
	 * If the value is -1, then that is an illegal ^C lvalue.
	 */
	static	int	fore_conv[] = {
		 7,  0,  4,  2,  1,  3,  5,  1,		/*  0-7  */
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
		 7,  0,  4,  2,  1,  3,  5,  1,
		 3,  2,  6,  6,  4,  5,  0,  0,
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
		1,  0,  0,  0,  0,  0,  0,  1,
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

	/* Local variables, of course */
	const 	u_char *	ptr = start;
		int		c1, c2;
		Attribute *	a;
		Attribute	ad;
		int		fg;
		int		val;
		int		noval;

        /* Reset all attributes to zero */
        ad.bold = ad.underline = ad.reverse = ad.blink = ad.altchar = 0;
        ad.color_fg = ad.color_bg = ad.fg_color = ad.bg_color = 0;

	/* Copy the inward attributes, if provided */
	a = (d) ? (Attribute *)d : &ad;

	/*
	 * If we're passed a non ^C code, dont do anything.
	 */
	if (*ptr != '\003')
		return ptr;

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
		ptr++;
		if (*ptr == 0)
		{
			if (fg)
				a->color_fg = a->fg_color = 0;
			a->color_bg = a->bg_color = 0;
			a->bold = a->blink = 0;
			return ptr;
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
			return ptr + 2;
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
			return ptr;
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

			    /* FALLTHROUGH */
			}

			/* These can only take one character */
			case '6':
			case '7':
			case '8':
			case '9':
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
				a->blink = back_blink_conv[val];
				a->bg_color = back_conv[val];
			}
		}

		if (*ptr == ',')
			continue;
		break;
	}

	return ptr;
}

/**************************** STRIP ANSI ***********************************/
/*
 * Used as a translation table when we cant display graphics characters
 * or we have been told to do translation.  A no-brainer, with little attempt
 * at being smart.
 * (JKJ: perhaps we should allow a user to /set this?)
 */
static	u_char	gcxlate[256] = {
  '*', '*', '*', '*', '*', '*', '*', '*',
  '#', '*', '#', '*', '*', '*', '*', '*',
  '>', '<', '|', '!', '|', '$', '_', '|',
  '^', 'v', '>', '<', '*', '=', '^', 'v',
  ' ', '!', '"', '#', '$', '%', '&', '\'',
  '(', ')', '*', '+', ',', '_', '.', '/',
  '0', '1', '2', '3', '4', '5', '6', '7',
  '8', '9', ':', ';', '<', '=', '>', '?',
  '@', 'A', 'B', 'C', 'D', 'E', 'F', 'G',
  'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O',
  'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W',
  'Z', 'Y', 'X', '[', '\\', ']', '^', '_',
  '`', 'a', 'b', 'c', 'd', 'e', 'f', 'g',
  'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o',
  'p', 'q', 'r', 's', 't', 'u', 'v', 'w',
  'x', 'y', 'z', '{', '|', '}', '~', '?',
  'C', 'u', 'e', 'a', 'a', 'a', 'a', 'c',
  'e', 'e', 'e', 'i', 'i', 'i', 'A', 'A',
  'e', 'e', 'e', 'o', 'o', 'o', 'u', 'u',
  'y', 'O', 'U', 'C', '#', 'Y', 'P', 'f',
  'a', 'i', 'o', 'u', 'n', 'N', '^', '^',
  '?', '<', '>', '2', '4', '!', '<', '>',
  '#', '#', '#', '|', '|', '|', '|', '+',
  '+', '+', '+', '|', '+', '+', '+', '+',
  '+', '+', '+', '+', '-', '+', '+', '+',
  '+', '+', '+', '+', '+', '=', '+', '+',
  '+', '+', '+', '+', '+', '+', '+', '+',
  '+', '+', '+', '#', '-', '|', '|', '-',
  'a', 'b', 'P', 'p', 'Z', 'o', 'u', 't',
  '#', 'O', '0', 'O', '-', 'o', 'e', 'U',
  '*', '+', '>', '<', '|', '|', '/', '=',
  '*', '*', '*', '*', 'n', '2', '*', '*'
};

/*
 * State 0 is a "normal, printable character"
 * State 1 is an "eight bit character"
 * State 2 is an "escape character" (\033)
 * State 3 is a "color code character" (\003)
 * State 4 is an "attribute change character"
 * State 5 is a "ROM character" (\022)
 * State 6 is a "character that is never printable."
 * State 7 is a "beep"
 * State 8 is a "tab"
 * State 9 is a "non-destructive space"
 */
static	u_char	ansi_state[256] = {
/*	^@	^A	^B	^C	^D	^E	^F	^G */
	6,	6,	4,	3,	6,	4,	4,	7,  /* 000 */
/*	^H	^I	^J	^K	^L	^M	^N	^O */
	6,	8,	0,	6,	0,	6,	6,	4,  /* 010 */
/*	^P	^Q	^R	^S	^T	^U	^V	^W */
	6,	6,	5,	9,	4,	6,	4,	6,  /* 020 */
/*	^X	^Y	^Z	^[	^\	^]	^^	^_ */
	6,	6,	6,	2,	6,	6,	6,	4,  /* 030 */
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
	1,	1,	1,	1,	1,	1,	1,	1,  /* 200 */
	1,	1,	1,	1,	1,	1,	1,	1,  /* 210 */
	1,	1,	1,	1,	1,	1,	1,	1,  /* 220 */
	1,	1,	1,	1,	1,	1,	1,	1,  /* 230 */
	1,	1,	1,	1,	1,	1,	1,	1,  /* 240 */
	1,	1,	1,	1,	1,	1,	1,	1,  /* 250 */
	1,	1,	1,	1,	1,	1,	1,	1,  /* 260 */
	1,	1,	1,	1,	1,	1,	1,	1,  /* 270 */
	1,	1,	1,	1,	1,	1,	1,	1,  /* 300 */
	1,	1,	1,	1,	1,	1,	1,	1,  /* 310 */
	1,	1,	1,	1,	1,	1,	1,	1,  /* 320 */
	1,	1,	1,	1,	1,	1,	1,	1,  /* 330 */
	1,	1,	1,	1,	1,	1,	1,	1,  /* 340 */
	1,	1,	1,	1,	1,	1,	1,	1,  /* 350 */
	1,	1,	1,	1,	1,	1,	1,	1,  /* 360 */
	1,	1,	1,	1,	1,	1,	1,	1   /* 370 */
};

/*
 * This started off as a general ansi parser, and it worked for stuff that
 * was going out to the display, but it couldnt deal properly with ansi codes,
 * and so when I tried to use it for the status bar, it just all fell to 
 * pieces.  After working it over, I came up with this.  What this does
 * (believe it or not) is walk through and strip out all the ansi codes in 
 * the target string.  Any codes that we recognize as being safe (pretty much
 * just ^[[<number-list>m), are converted back into their logical characters
 * (eg, ^B, ^R, ^_, etc), and everything else is completely blown away.
 *
 * If "width" is not -1, then every "width" printable characters, a \n
 * marker is put into the output so you can tell where the line breaks
 * are.  Obviously, this is optional.  It is used by prepared_display 
 * and $leftpc().
 *
 * XXX Some have asked that i "space out" the outputs with spaces and return
 * but one row of output, so that rxvt will paste it as all one line.  Yea,
 * that might be nice, but that raises other, more thorny issues.
 */

/*
 * These macros help keep 8 bit chars from sneaking into the output stream
 * where they might be stripped out.
 */
#define this_char() (eightbit ? *str : (*str) & 0x7f)
#define next_char() (eightbit ? *str++ : (*str++) & 0x7f)
#define put_back() (str--)
#define nlchar '\n'

u_char *	normalize_string (const u_char *str, int logical)
{
	u_char *	output;
	u_char		chr;
	Attribute	a;
	int 		pos;
	int		maxpos;
	int 		args[10];
	int		nargs;
	int		i, n;
	int		ansi = get_int_var(DISPLAY_ANSI_VAR);
	int		gcmode = get_int_var(DISPLAY_PC_CHARACTERS_VAR);
	int		eightbit = term_eight_bit();
	int		beep_max, beep_cnt = 0;
	int		tab_max, tab_cnt = 0;
	int		nds_max, nds_cnt = 0;
	int		pc = 0;
	int		reverse, bold, blink, underline, altchar, color;
	size_t		(*attrout) (u_char *, Attribute *);

	/* Figure out how many beeps/tabs/nds's we can handle */
	if (!(beep_max  = get_int_var(BEEP_MAX_VAR)))
		beep_max = -1;
	if (!get_int_var(TAB_VAR))
		tab_max = -1;
	else if ((tab_max = get_int_var(TAB_MAX_VAR)) < 0)
		tab_max = -1;
	if (!(nds_max	= get_int_var(ND_SPACE_MAX_VAR)))
		nds_max = -1;
	if (normalize_permit_all_attributes)	/* XXXX */
		reverse = bold = blink = underline = altchar = color = 1;
	else
	{
		reverse 	= get_int_var(INVERSE_VIDEO_VAR);
		bold 		= get_int_var(BOLD_VIDEO_VAR);
		blink 		= get_int_var(BLINK_VIDEO_VAR);
		underline 	= get_int_var(UNDERLINE_VIDEO_VAR);
		altchar 	= get_int_var(ALT_CHARSET_VAR);
		color 		= get_int_var(COLOR_VAR);
	}
	if (logical == 0)
		attrout = display_attributes;	/* prep for screen output */
	else if (logical == 1)
		attrout = logic_attributes;	/* non-screen handlers */
	else if (logical == 2)
		attrout = ignore_attributes;	/* $stripansi() function */
	else if (logical == 3)
		attrout = display_attributes;	/* The status line */
	else
		panic("'logical == %d' is not valid.", logical);

	/* Reset all attributes to zero */
	a.bold = a.underline = a.reverse = a.blink = a.altchar = 0;
	a.color_fg = a.color_bg = a.fg_color = a.bg_color = 0;

	/* 
	 * The output string has a few extra chars on the end just 
	 * in case you need to tack something else onto it.
	 */
	maxpos = strlen(str);
	output = (u_char *)new_malloc(maxpos + 64);
	pos = 0;

	while ((chr = next_char()))
	{
	    if (pos > maxpos)
	    {
		maxpos += 64; /* Extend 64 chars at a time */
		RESIZE(output, unsigned char, maxpos + 64);
	    }

	    switch (ansi_state[chr])
	    {
		/*
		 * State 0 is a normal, printable ascii character
		 */
		case 0:
			output[pos++] = chr;
			pc++;
			break;

		/*
		 * State 1 is a high-bit character that may or may not
		 * need to be translated first.
		 * State 6 is an unprintable character that must be made
		 * unprintable (gcmode is forced to be 1)
		 */
		case 1:
		case 6:
		{
			int my_gcmode = gcmode;

			/*
			 * This is a very paranoid check to make sure that
			 * the 8-bit escape code doesnt elude us.
			 */
			if (chr == 27 + 128)
				chr = '[';

			if (ansi_state[chr] == 6)
				my_gcmode = 1;

			if (normalize_never_xlate)
				my_gcmode = 4;

			switch (my_gcmode)
			{
				/*
				 * In gcmode 5, translate all characters
				 */
				case 5:
				{
					output[pos++] = gcxlate[chr];
					break;
				}

				/*
				 * In gcmode 4, accept all characters
				 */
				case 4:
				{
					output[pos++] = chr;
					break;
				}

				/*
				 * In gcmode 3, accept or translate chars
				 */
				case 3:
				{
					if (termfeatures & TERM_CAN_GCHAR)
						output[pos++] = chr;
					else
						output[pos++] = gcxlate[chr];
					break;
				}

				/*
				 * In gcmode 2, accept or highlight xlate
				 */
				case 2:
				{
					if (termfeatures & TERM_CAN_GCHAR)
						output[pos++] = chr;
					else
					{
						/* <REV> char <REV> */
						a.reverse = !a.reverse;
						pos += attrout(output + pos, &a);
						output[pos++] = gcxlate[chr];
						a.reverse = !a.reverse;
						pos += attrout(output + pos, &a);
					}
					break;
				}

				/*
				 * gcmode 1 is "accept or reverse mangle"
				 * If youre doing 8-bit, it accepts eight
				 * bit characters.  If youre not doing 8 bit
				 * then it converts the char into something
				 * printable and then reverses it.
				 */
				case 1:
				{
					if (termfeatures & TERM_CAN_GCHAR)
						output[pos++] = chr;
					else if ((chr & 0x80) && eightbit)
						output[pos++] = chr;
					else
					{
						a.reverse = !a.reverse;
						pos += attrout(output + pos, &a);
						output[pos++] = 
							(chr | 0x40) & 0x7f;
						a.reverse = !a.reverse;
						pos += attrout(output + pos, &a);
					}
					break;
				}

				/*
				 * gcmode 0 is "always strip out"
				 */
				case 0:
					break;
			}
			pc++;
			break;
		}


		/*
		 * State 2 is the escape character
		 */
		case 2:
		{
		    /*
		     * The next thing we do is dependant on what the character
		     * is after the escape.  Be very conservative in what we
		     * allow.  In general, escape sequences shouldn't be very
		     * complex at this point.
		     * If we see an escape at the end of a string, just mangle
		     * it and dont bother with the rest of the expensive
		     * parsing.
		     */
		    if (!ansi || this_char() == 0)
		    {
			a.reverse = !a.reverse;
			pos += attrout(output + pos, &a);
			output[pos++] = '[';
			a.reverse = !a.reverse;
			pos += attrout(output + pos, &a);

			pc++;
			continue;
		    }

		    switch ((chr = next_char()))
		    {
			/*
			 * All these codes we just skip over.  We're not
			 * interested in them.
			 */

			/*
			 * These are two-character commands.  The second
			 * char is the argument.
			 */
			case ('#') : case ('(') : case (')') :
			case ('*') : case ('+') : case ('$') :
			case ('@') :
			{
				chr = next_char();
				if (chr == 0)
					put_back();	/* Bogus sequence */
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
			{
				break;		/* Don't do anything */
			}

			/*
			 * Swallow up graphics sequences...
			 */
			case ('G'):
			{
				while ((chr = next_char()) != 0 &&
					chr != ':')
					;
				if (chr == 0)
					put_back();
				break;
			}

			/*
			 * Not sure what this is, it's not supported by
			 * rxvt, but its supposed to end with an ESCape.
			 */
			case ('P') :
			{
				while ((chr = next_char()) != 0 &&
					chr != 033)
					;
				if (chr == 0)
					put_back();
				break;
			}

			/*
			 * Anything else, we just munch the escape and 
			 * leave it at that.
			 */
			default:
				put_back();
				break;


			/*
			 * Strip out Xterm sequences
			 */
			case (']') :
			{
				while ((chr = next_char()) != 0 && chr != 7)
					;
				if (chr == 0)
					put_back();
				break;
			}

			/*
			 * Now these we're interested in....
			 * (CSI sequences)
			 */
			case ('[') :
			{
	/* <<<<<<<<<<<< */
	/*
	 * Set up the arguments list
	 */
	nargs = 0;
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
	chr = this_char();
	if (chr >= '<' && chr <= '?')
		next_char();	/* skip it */


	/*
	 * Now pull the arguments off one at a time.  Keep pulling them 
	 * off until we find a character that is not a number or a semicolon.
	 * Skip everything else.
	 */
	for (nargs = 0; nargs < 10; str++)
	{
		n = 0;
		for (n = 0; isdigit(this_char()); next_char())
			n = n * 10 + (this_char() - '0');

		args[nargs++] = n;

		/*
		 * If we run out of code here, then we're totaly confused.
		 * just back out with whatever we have...
		 */
		if (!this_char())
		{
			output[pos] = output[pos + 1] = 0;
			return output;
		}

		if (this_char() != ';')
			break;
	}

	/*
	 * If we find a new ansi char, start all over from the top 
	 * and strip it out too
	 */
	if (this_char() == 033)
		continue;

	/*
	 * Support "spaces" (cursor right) code
	 */
	if (this_char() == 'a' || this_char() == 'C')
	{
		next_char();
		if (nargs >= 1)
		{
		       /*
			* Keep this within reality.
			*/
			if (args[0] > 256)
				args[0] = 256;

			/* This is just sanity */
			if (pos + args[0] > maxpos)
			{
				maxpos += args[0]; 
				RESIZE(output, u_char, maxpos + 64);
			}
			while (args[0]-- > 0)
			{
				if (nds_max > 0 && nds_cnt > nds_max)
					break;

				output[pos++] = ND_SPACE;
				pc++;
				nds_cnt++;
			}
		}
		break;
	}


	/*
	 * The 'm' command is the only one that we honor.  
	 * All others are dumped.
	 */
	if (next_char() != 'm')
		break;


	/*
	 * Walk all of the numeric arguments, plonking the appropriate 
	 * attribute changes as needed.
	 */
	for (i = 0; i < nargs; i++)
	{
	    switch (args[i])
	    {
		case 0:		/* Reset to default */
		{
			a.reverse = a.bold = 0;
			a.blink = a.underline = 0;
			a.altchar = 0;
			a.color_fg = a.color_bg = 0;
			a.fg_color = a.bg_color = 0;
			pos += attrout(output + pos, &a);
			break;
		}
		case 1:		/* bold on */
		{
			if (bold)
			{
				a.bold = 1;
				pos += attrout(output + pos, &a);
			}
			break;
		}
		case 2:		/* dim on -- not supported */
			break;
		case 4:		/* Underline on */
		{
			if (underline)
			{
				a.underline = 1;
				pos += attrout(output + pos, &a);
			}
			break;
		}
		case 5:		/* Blink on */
		case 26:	/* Blink on */
		{
			if (blink)
			{
				a.blink = 1;
				pos += attrout(output + pos, &a);
			}
			break;
		}
		case 6:		/* Blink off */
		case 25:	/* Blink off */
		{
			a.blink = 0;
			pos += attrout(output + pos, &a);
			break;
		}
		case 7:		/* Reverse on */
		{
			if (reverse)
			{
				a.reverse = 1;
				pos += attrout(output + pos, &a);
			}
			break;
		}
		case 21:	/* Bold off */
		case 22:	/* Bold off */
		{
			a.bold = 0;
			pos += attrout(output + pos, &a);
			break;
		}
		case 24:	/* Underline off */
		{
			a.underline = 0;
			pos += attrout(output + pos, &a);
			break;
		}
		case 27:	/* Reverse off */
		{
			a.reverse = 0;
			pos += attrout(output + pos, &a);
			break;
		}
		case 30: case 31: case 32: case 33: case 34: 
		case 35: case 36: case 37:	/* Set foreground color */
		{
			if (color)
			{
				a.color_fg = 1;
				a.fg_color = args[i] - 30;
				pos += attrout(output + pos, &a);
			}
			break;
		}
		case 39:	/* Reset foreground color to default */
		{
			if (color)
			{
				a.color_fg = 0;
				a.fg_color = 0;
				pos += attrout(output + pos, &a);
			}
			break;
		}
		case 40: case 41: case 42: case 43: case 44: 
		case 45: case 46: case 47:	/* Set background color */
		{
			if (color)
			{
				a.color_bg = 1;
				a.bg_color = args[i] - 40;
				pos += attrout(output + pos, &a);
			}
			break;
		}
		case 49:	/* Reset background color to default */
		{
			if (color)
			{
				a.color_bg = 0;
				a.bg_color = 0;
				pos += attrout(output + pos, &a);
			}
			break;
		}

		default:	/* Everything else is not supported */
			break;
	    }
	} /* End of for (handling esc-[...m) */
	/* >>>>>>>>>>> */
			} /* End of escape-[ code handling */
		    } /* End of ESC handling */
		    break;
	        } /* End of case 2 handling */


	        /*
	         * Skip over ^C codes, they're already normalized.
	         * well, thats not totaly true.  We do some mangling
	         * in order to make it work better
	         */
		case 3:
		{
			const u_char 	*end;

			put_back();
			end = read_color_seq(str, (void *)&a);

			/*
			 * XXX - This is a short-term hack to prevent an 
			 * infinite loop.  I need to come back and fix
			 * this the right way in the future.
			 *
			 * The infinite loop can happen when a character
			 * 131 is encountered when eight bit chars is OFF.
			 * We see a character 3 (131 with the 8th bit off)
			 * and so we ask skip_ctl_c_seq where the end of 
			 * that sequence is.  But since it isnt a ^c sequence
			 * it just shrugs its shoulders and returns the
			 * pointer as-is.  So we sit asking it where the end
			 * is and it says "its right here".  So there is a 
			 * need to check the retval of skip_ctl_c_seq to 
			 * actually see if there is a sequence here.  If there
			 * is not, then we just mangle this character.  For
			 * the record, char 131 is a reverse block, so that
			 * seems the most appropriate thing to put here.
			 */
			if (end == str)
			{
				/* Turn on reverse if neccesary */
				a.reverse = !a.reverse;
				pos += attrout(output + pos, &a);
				output[pos++] = ' ';
				a.reverse = !a.reverse;
				pos += attrout(output + pos, &a);

				pc++;
				next_char();	/* Munch it */
				break;
			}

			/* Move to the end of the string. */
			str = end;

			/* Suppress the color if no color is permitted */
			if (!color)
			{
				a.color_fg = a.color_bg = 0;
				a.fg_color = a.bg_color = 0;
				break;
			}

			/* Output the new attributes */
			pos += attrout(output + pos, &a);
			break;
		}

		/*
		 * State 4 is for the special highlight characters
		 */
		case 4:
		{
			put_back();
			switch (this_char())
			{
				case REV_TOG:   
					if (reverse)
						a.reverse = !a.reverse;
					break;
				case BOLD_TOG:  
					if (bold)
						a.bold = !a.bold;
					break;
				case BLINK_TOG:
					if (blink)
						a.blink = !a.blink;
					break;
				case UND_TOG:
					if (underline)
						a.underline = !a.underline;
					break;
				case ALT_TOG:
					if (altchar)
						a.altchar = !a.altchar;
					break;
				case ALL_OFF:
					a.reverse = a.bold = a.blink = 0;
					a.underline = a.altchar = 0;
					a.color_fg = a.color_bg = 0;
					a.bg_color = a.fg_color = 0;
					break;
				default:
					break;
			}

			pos += attrout(output + pos, &a);
			next_char();
			break;
		}

		case 5:
		{
			put_back();
			if (str[0] && str[1] && str[2] && str[3])
			{
				u_char	val = 0;

				next_char();
				val += next_char() - '0';
				val *= 10;
				val += next_char() - '0';
				val *= 10;
				val += next_char() - '0';
				output[pos++] = val;
			}
			else 
			{
				a.reverse = !a.reverse;
				pos += attrout(output + pos, &a);
				output[pos++] = 'R';
				a.reverse = !a.reverse;
				pos += attrout(output + pos, &a);
				next_char();
			}
			pc++;
			break;
		}

		case 7:      /* bell */
		{
			beep_cnt++;
			if ((beep_max == -1) || (beep_cnt > beep_max))
			{
				a.reverse = !a.reverse;
				pos += attrout(output + pos, &a);
				output[pos++] = 'G';
				a.reverse = !a.reverse;
				pos += attrout(output + pos, &a);
				pc++;
			}
			else
				output[pos++] = '\007';

			break;
		}

		case 8:		/* Tab */
		{
			tab_cnt++;
			if (tab_max < 0 || 
			    (tab_max > 0 && tab_cnt > tab_max))
			{
				a.reverse = !a.reverse;
				pos += attrout(output + pos, &a);
				output[pos++] = 'I';
				a.reverse = !a.reverse;
				pos += attrout(output + pos, &a);
				pc++;
			}
			else
			{
				int	len = 8 - (pc % 8);
				for (i = 0; i < len; i++)
				{
					output[pos++] = ' ';
					pc++;
				}
			}
			break;
		}

		case 9:		/* Non-destruct space */
		{
			nds_cnt++;

			/*
			 * Just swallop up any ND's over the max
			 */
			if ((nds_max > 0) && (nds_cnt > nds_max))
				;
			else
				output[pos++] = ND_SPACE;
			break;
		}

		default:
		{
			panic("Unknown normalize_string mode");
			break;
		}
	    } /* End of huge ansi-state switch */
	} /* End of while, iterating over input string */

	/* Terminate the output and return it. */
	if (logical == 0)
	{
		a.bold = a.underline = a.reverse = a.blink = a.altchar = 0;
		a.color_fg = a.color_bg = a.fg_color = a.bg_color = 0;
		pos += attrout(output + pos, &a);
	}
	output[pos] = output[pos + 1] = 0;
	return output;
}

/* 
 * XXX I'm not sure where this belongs, but for now it goes here.
 * This function takes a type-1 normalized string (with the attribute
 * markers) and converts them back to logical characters.  This is needed
 * for lastlog and the status line and so forth.
 */
u_char *	denormalize_string (const u_char *str)
{
	u_char *	output = NULL;
	size_t		maxpos;
	Attribute 	a;
	size_t		span;
	int		pos;

        /* Reset all attributes to zero */
        a.bold = a.underline = a.reverse = a.blink = a.altchar = 0;
        a.color_fg = a.color_bg = a.fg_color = a.bg_color = 0;

	/* 
	 * The output string has a few extra chars on the end just 
	 * in case you need to tack something else onto it.
	 */
	maxpos = strlen(str);
	output = (u_char *)new_malloc(maxpos + 64);
	pos = 0;

	while (*str)
	{
		if (pos > maxpos)
		{
			maxpos += 64; /* Extend 64 chars at a time */
			RESIZE(output, unsigned char, maxpos + 64);
		}
		switch (*str)
		{
		    case '\006':
		    {
			if (read_attributes(str, &a))
				continue;		/* Mangled */
			str += 5;

			span = logic_attributes(output + pos, &a);
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
unsigned char **prepare_display(const unsigned char *str,
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
		newline = 0;        /* Number of newlines           */
static	u_char 	**output = (unsigned char **)0;
const 	u_char	*ptr;
	u_char 	buffer[BIG_BUFFER_SIZE + 1],
		*cont_ptr,
		*cont = empty_string,
		c,
		*words,
		*pos_copy;
	Attribute	a;

	if (recursion)
		panic("prepare_display() called recursively");
	recursion++;

        /* Reset all attributes to zero */
        a.bold = a.underline = a.reverse = a.blink = a.altchar = 0;
        a.color_fg = a.color_bg = a.fg_color = a.bg_color = 0;

	do_indent = get_int_var(INDENT_VAR);
	if (!(words = get_string_var(WORD_BREAK_VAR)))
		words = ", ";
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

	/*
	 * Start walking through the entire string.
	 */
	for (ptr = str; *ptr && (pos < BIG_BUFFER_SIZE - 8); ptr++)
	{
		switch (*ptr)
		{
			case '\007':      /* bell */
				buffer[pos++] = *ptr;
				break;

			case '\n':      /* Forced newline */
			{
				newline = 1;
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
                                        buffer[pos++] = *ptr++;
                                        buffer[pos++] = *ptr++;
                                        buffer[pos++] = *ptr++;
                                        buffer[pos++] = *ptr++;
                                        buffer[pos++] = *ptr;
                                }
                                else
                                        abort();
                                continue;          /* Skip the column check */
                        }

			default:
			{
				if (!strchr(words, *ptr))
				{
					if (indent == -1)
						indent = col;
					buffer[pos++] = *ptr;
					col++;
					break;
				}
				/* FALLTHROUGH */
			}

			case ' ':
			case ND_SPACE:
			{
				if (indent == 0)
				{
					indent = -1;
					firstwb = pos;
				}
				word_break = pos;
				if (*ptr != ' ' && ptr[1] &&
				    (col + 1 < max_cols))
					word_break++;
				buffer[pos++] = *ptr;
				col++;
				break;
			}
		} /* End of switch (*ptr) */

		/*
		 * Must check for cols >= maxcols+1 becuase we can have a 
		 * character on the extreme screen edge, and we would still 
		 * want to treat this exactly as 1 line, and col has already 
		 * been incremented.
		 */
		if ((col > max_cols) || newline)
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
				word_break = pos;

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
			if (!*cont && (firstwb == word_break) && do_indent) 
				word_break = pos;

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
			if (!*cont)
			{
				int	lhs_count = 0;
				int	continued_count = 0;

				if (do_indent && (indent < (max_cols / 3)) &&
						(strlen(cont_ptr) < indent))
				{
					cont = alloca(indent+10); /* sb pana */
					sprintf(cont, "%-*s", indent, cont_ptr);
				}

				/*
				 * Otherwise, we just use /set continued_line, 
				 * whatever it is.
				 */
				else if (!*cont && *cont_ptr)
					cont = cont_ptr;

				cont = normalize_string(cont, 0);

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
				 */
				if (lhs_count <= continued_count)
					word_break = pos;

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
			while (buffer[word_break] == ' ' && word_break < pos)
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
			strlcpy(buffer, cont, BIG_BUFFER_SIZE / 2);
			display_attributes(buffer + strlen(buffer), &a);
			strlcat(buffer, pos_copy, BIG_BUFFER_SIZE / 2);

			pos = strlen(buffer);
			/* Watch this -- ugh. how expensive! :( */
			col = output_with_count(buffer, 0, 0);
			word_break = 0;
			newline = 0;

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

	buffer[pos++] = ALL_OFF;
	buffer[pos] = '\0';
	if (*buffer)
		malloc_strcpy((char **)&(output[line++]),buffer);

	recursion--;
	new_free(&output[line]);
	*lused = line - 1;
	return output;
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

	if (window->visible && foreground && window->display_size)
	{
		output_with_count(str, 1, 1);
		term_cr();
		term_newline();
	}

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
 */
int 	output_with_count (const unsigned char *str1, int clreol, int output)
{
	int 		beep = 0, 
			out = 0;
	Attribute	a;
	const unsigned char *str;

        /* Reset all attributes to zero */
        a.bold = a.underline = a.reverse = a.blink = a.altchar = 0;
        a.color_fg = a.color_bg = a.fg_color = a.bg_color = 0;

	for (str = str1; str && *str; str++)
	{
		switch (*str)
		{
			/* Attribute marker */
			case '\006':
			{
				if (read_attributes(str, &a))
					break;
				if (output)
					term_attribute(&a);
				str += 4;
				break;
			}

			/* Terminal beep */
			case '\007':
			{
				beep++;
				break;
			}

			/* Dont ask */
			case '\f':
			{
				if (output)
				{
					a.reverse = !a.reverse;
					term_attribute(&a);
					putchar_x('f');
					a.reverse = !a.reverse;
					term_attribute(&a);
				}
				out++;
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
				/*
				 * Note that 'putchar_x()' is safe here
				 * because normalize_string() has already 
				 * removed all of the nasty stuff that could 
				 * end up getting here.  And for those things
				 * that are nasty that get here, its probably
				 * because the user specifically asked for it.
				 */
				if (output)
					putchar_x(*str);
				out++;
				break;
			}
		}
	}

	if (output)
	{
		if (beep)
			term_beep();
		if (clreol)
			term_clear_to_eol();
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

	/*
	 * Just paranoia.
	 */
	if (!current_window)
	{
		puts(buffer);
		return;
	}

	if (dumb_mode)
	{
		add_to_lastlog(current_window, buffer);
		if (do_hook(WINDOW_LIST, "%u %s", current_window->refnum, buffer))
			puts(buffer);
		fflush(stdout);
		return;
	}

	if (in_window_command)
	{
		in_window_command = 0;	/* Inhibit looping! */
		update_all_windows();
		in_window_command = 1;
	}

	/*
	 * The highest priority is if we have explicitly stated what
	 * window we want this output to go to.
	 */
	if (to_window)
	{
		add_to_window(to_window, buffer);
		return;
	}

	/*
	 * The next priority is "LOG_CURRENT" which is the "default"
	 * level for all non-routed output.  That is meant to ensure that
	 * any extraneous error messages goes to a window where the user
	 * will see it.  All specific output (e.g. incoming server stuff) 
	 * is routed through one of the LOG_* levels, which is handled
	 * below.
	 */
	else 
	if (who_level == LOG_CURRENT && current_window->server == from_server)
	{
		add_to_window(current_window, buffer);
		return;
	}

	/*
	 * Next priority is if the output is targeted at a certain
	 * user or channel (used for /window bind or /window nick targets)
	 */
	else if (who_from)
	{
		tmp = NULL;
		while (traverse_all_windows(&tmp))
		{
			/*
			 * Check for /WINDOW CHANNELs that apply.
			 * (Any current channel will do)
			 */
			if (tmp->current_channel &&
				!my_stricmp(who_from, tmp->current_channel))
			{
				if (tmp->server == from_server)
				{
					add_to_window(tmp, buffer);
					return;
				}
			}

			/*
			 * Check for /WINDOW QUERYs that apply.
			 */
			if (tmp->query_nick &&
			   ( ((who_level == LOG_MSG || who_level == LOG_NOTICE
			    || who_level == LOG_DCC || who_level == LOG_CTCP
			    || who_level == LOG_ACTION)
				&& !my_stricmp(who_from, tmp->query_nick)
				&& from_server == tmp->server)
			  || ((who_level == LOG_DCC || who_level == LOG_CTCP
			    || who_level == LOG_ACTION)
				&& *tmp->query_nick == '='
				&& !my_stricmp(who_from, tmp->query_nick + 1))
			  || ((who_level == LOG_DCC || who_level == LOG_CTCP
			    || who_level == LOG_ACTION)
				&& *tmp->query_nick == '='
				&& !my_stricmp(who_from, tmp->query_nick))))
			{
				add_to_window(tmp, buffer);
				return;
			}
		}

		tmp = NULL;
		while (traverse_all_windows(&tmp))
		{
			/*
			 * Check for /WINDOW NICKs that apply
			 */
			if (from_server == tmp->server)
			{
				if (find_in_list((List **)&(tmp->nicks), 
					who_from, !USE_WILDCARDS))
				{
					add_to_window(tmp, buffer);
					return;
				}
			}
		}

		/*
		 * we'd better check to see if this should go to a
		 * specific window (i dont agree with this, though)
		 */
		if (from_server != -1 && is_channel(who_from))
		{
			if ((tmp = get_channel_window(who_from, from_server)))
			{
				add_to_window(tmp, buffer);
				return;
			}
		}
	}

	/*
	 * Check to see if this level should go to current window
	 */
	if ((current_window_level & who_level) && 
		current_window->server == from_server)
	{
		add_to_window(current_window, buffer);
		return;
	}

	/*
	 * Check to see if any window can claim this level
	 */
	tmp = NULL;
	while (traverse_all_windows(&tmp))
	{
		/*
		 * Check for /WINDOW LEVELs that apply
		 */
		if (((from_server == tmp->server) || (from_server == -1)) &&
		    (who_level & tmp->window_level))
		{
			add_to_window(tmp, buffer);
			return;
		}
	}

	/*
	 * If all else fails, if the current window is connected to the
	 * given server, use the current window.
	 */
	if (from_server == current_window->server)
	{
		add_to_window(current_window, buffer);
		return;
	}

	/*
	 * And if that fails, look for ANY window that is bound to the
	 * given server (this never fails if we're connected.)
	 */
	tmp = NULL;
	while (traverse_all_windows(&tmp))
	{
		if (tmp->server == from_server)
		{
			add_to_window(tmp, buffer);
			return;
		}
	}

	/*
	 * No window found for a server is usually because we're
	 * disconnected or not yet connected.
	 */
	add_to_window(current_window, buffer);
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
static void 	add_to_window (Window *window, const unsigned char *str)
{
	int	must_free = 0;
	char *	pend;
	char *	strval;

	if (window->server >= 0 && get_server_redirect(window->server))
		if (redirect_text(window->server, 
			        get_server_redirect(window->server),
				str, NULL, 0))
			return;

	if (!do_hook(WINDOW_LIST, "%u %s", window->refnum, str))
		return;

	if ((pend = get_string_var(OUTPUT_REWRITE_VAR)))
	{
		char	*prepend_exp;
		char	argstuff[10240];
		int	args_flag;

		/* First, create the $* list for the expando */
		snprintf(argstuff, 10240, "%u %s", 
				window->refnum, str);

		/* Now expand the expando with the above $* */
		prepend_exp = expand_alias(pend, argstuff,
					   &args_flag, NULL);

		str = prepend_exp;
		must_free = 1;
	}

	/* Normalize the line of output */
	strval = normalize_string(str, 0);

	/* Pass it off to the window */
	window_disp(window, strval, str);
	new_free(&strval);

	/*
	 * This used to be in rite(), but since rite() is a general
	 * purpose function, and this stuff really is only intended
	 * to hook on "new" output, it really makes more sense to do
	 * this here.  This also avoids the terrible problem of 
	 * recursive calls to split_up_line, which are bad.
	 */
	if (!window->visible)
	{
		/*
		 * This is for archon -- he wanted a way to have 
		 * a hidden window always beep, even if BEEP is off.
		 * XXX -- str has already been freed here! ACK!
		 */
		if (window->beep_always && strchr(str, '\007'))
		{
			Window *old_to_window;
			term_beep();
			old_to_window = to_window;
			to_window = current_window;
			say("Beep in window %d", window->refnum);
			to_window = old_to_window;
		}

		/*
		 * Tell some visible window about our problems 
		 * if the user wants us to.
		 */
		if (!(window->miscflags & WINDOW_NOTIFIED) &&
			who_level & window->notify_level)
		{
			window->miscflags |= WINDOW_NOTIFIED;
			if (window->miscflags & WINDOW_NOTIFY)
			{
				Window	*old_to_window;
				int	lastlog_level;

				lastlog_level = set_lastlog_msg_level(LOG_CRAP);
				old_to_window = to_window;
				to_window = current_window;
				say("Activity in window %d", 
					window->refnum);
				to_window = old_to_window;
				set_lastlog_msg_level(lastlog_level);
			}
			update_all_status();
		}
	}
	if (must_free)
		new_free(&str);

	cursor_in_display(window);
}

/*
 * The mid-level shim for output to all ircII type windows.
 *
 * By this point, the logical line 'str' is in the state it is going to be
 * put onto the screen.  We need to put it in our lastlog [XXX Should that
 * be done by the front end?] and process it through the display chopper
 * (prepare_display) which slices and dices the logical line into manageable
 * chunks, suitable for putting onto the display.  We then call our back end
 * function to do the actual physical output.
 */
static void    window_disp (Window *window, const unsigned char *str, const unsigned char *orig_str)
{
        u_char **       lines;
        int             cols;
	int		numl;

	add_to_log(window->log_fp, window->refnum, orig_str);
	add_to_lastlog(window, orig_str);

	if (window->screen)
		cols = window->screen->co - 1;	/* XXX HERE XXX */
	else
		cols = window->columns - 1;

	/* Suppress status updates while we're outputting. */
        for (lines = prepare_display(str, cols, &numl, 0); *lines; lines++)
	{
		if (add_to_display_list(window, *lines))
			rite(window, *lines);
	}
	term_flush();
	cursor_to_input();
}

/*
 * This puts the given string into a scratch window.  It ALWAYS suppresses
 * any further action (by returning a FAIL, so rite() is not called).
 */
static	int	add_to_scratch_window_display_list (Window *window, const unsigned char *str)
{
	Display *my_line, 
		*my_line_prev;
	int 	cnt;

	/* Must be a scratch window */
	if (window->scratch_line == -1)
		panic("This is not a scratch window.");

	/* Outputting to a 0 size scratch window is a no-op. */
	if (window->display_size == 0)
		return 0;

	/* Must be within the bounds of the size of the window */
	if (window->scratch_line >= window->display_size)
		panic("scratch_line is too big.");

	my_line = window->top_of_display;
	my_line_prev = NULL;

	/* 
	 * 'window->scratch_line' is set to the place where the next line
	 * of output will be placed.  However, the actual contents of the
	 * screen's display is allocated on the fly, and display_ip might
	 * closer than 'scratch_line' entries from 'top_of_display'.  So to
	 * forstall that possibility, this auto-grows the window display so
	 * that it contains at least 'scratch_line' entries.
	 */
	for (cnt = 0; cnt < window->scratch_line; cnt++)
	{
	    if (my_line == window->display_ip)
	    {
		/* This is not the first line being inserted */
		if (my_line_prev)
		{
			my_line_prev->next = new_display_line(my_line_prev);
			my_line = my_line_prev->next;
			my_line->next = window->display_ip;
			window->display_ip->prev = my_line;
			window->display_buffer_size++;
		}

		/* This is the first line being inserted... */
		else
		{
			/* 
			 * This is a really bad and awful hack.
			 * Is all this _really_ neccesary?
			 */

			if (window->top_of_display != window->display_ip)
				panic("top_of_display != display_ip");

			my_line = new_display_line(NULL);
			window->top_of_display = my_line;
			window->top_of_scrollback = my_line;

			my_line->next = window->display_ip;
			window->display_ip->prev = my_line;
#if 0
			window->display_buffer_size++;
#endif
		}
	    }

	    my_line_prev = my_line;
	    my_line = my_line->next;
	}

	/*
	 * Except that we only actually create the window display to be
	 * one entry less than it should be, so at this point, if my_line
	 * is the same as display_ip, then we have to create one more
	 * entry to hold the data.  XXX This is not handled very elegantly.
	 */
	if (my_line == window->display_ip)
	{
		my_line = new_display_line(window->display_ip->prev);
		if (window->display_ip->prev)
			window->display_ip->prev->next = my_line;
		my_line->next = window->display_ip;
		window->display_ip->prev = my_line;
		window->display_buffer_size++;
	}

	/*
	 * Alright then!  Copy the line of output into the buffer, and
	 * set the cursor to just after this line.
	 */
	malloc_strcpy(&my_line->line, str);
	window->cursor = window->scratch_line;

	/*
	 * Increment the scratch line to the next location.
	 */
	window->scratch_line++;
	if (window->scratch_line >= window->display_size)
		window->scratch_line = 0;	/* Just go back to top */

	/*
	 * For /window scroll off, clear the next line as long as it is
	 * not the display_ip entry.
	 */
	if (window->noscroll)
	{
		if (window->scratch_line == 0)
			my_line = window->top_of_display;
		else if (my_line->next != window->display_ip)
			my_line = my_line->next;
		else
			my_line = NULL;

		if (my_line)
			malloc_strcpy(&my_line->line, empty_string);
	}

	/*
	 * If this is a visible window, output the new line.
	 * If this is a non-scrolling window, clear the next line.
	 */
	if (window->visible)
	{
		window->cursor = window->scratch_line;
		window->screen->cursor_window = window;
		term_move_cursor(0, window->top + window->cursor);
		term_clear_to_eol();
		output_with_count(str, 1, 1);
		term_all_off();
		if (window->noscroll)
		{
		    term_move_cursor(0, window->top + window->scratch_line);
		    term_clear_to_eol();
		}
	}

	window->scratch_line++;
	if (window->scratch_line >= window->display_size)
		window->scratch_line = 0;	/* Just go back to top */
	window->cursor = window->scratch_line;
	return 0;		/* Express a failure */
}


/*
 * This function adds an item to the window's display list.  If the item
 * should be displayed on the screen, then 1 is returned.  If the item is
 * not to be displayed, then 0 is returned.  This function handles all
 * the hold_mode stuff.
 */
static int 	add_to_display_list (Window *window, const unsigned char *str)
{
	/*
	 * If this is a scratch window, do that somewhere else
	 */
	if (window->scratch_line != -1)
		return add_to_scratch_window_display_list(window, str);


	/* Add to the display list */
	window->display_ip->next = new_display_line(window->display_ip);
	malloc_strcpy(&window->display_ip->line, str);
	window->display_ip = window->display_ip->next;
	window->display_buffer_size++;
	window->distance_from_display++;

	/*
	 * IF:
	 * We are at the bottom of the screen AND scrollback mode is on
	 * OR:
	 * We are in hold mode and we have displayed at least a full screen
	 * of material
	 * THEN:
	 * We defeat the output.
	 */
	if (((window->distance_from_display > window->display_size) &&
						window->scrollback_point) ||
		(window->hold_mode &&
			(++window->held_displayed > window->display_size)) ||
		(window->holding_something && window->lines_held))
	{
		if (!window->holding_something)
			window->holding_something = 1;

		window->lines_held++;
		if (window->lines_held >= window->last_lines_held + 10)
		{
			update_window_status(window, 0);
			window->last_lines_held = window->lines_held;
		}
		return 0;
	}


	/*
	 * Before outputting the line, we snip back the top of the
	 * display buffer.  (The fact that we do this only when we get
	 * to here is what keeps whats currently on the window always
	 * active -- once we get out of hold mode or scrollback mode, then
	 * we truncate the display buffer at that point.)
	 */
	while (window->display_buffer_size > window->display_buffer_max)
	{
		Display *next = window->top_of_scrollback->next;
		delete_display_line(window->top_of_scrollback);
		window->top_of_scrollback = next;
		window->display_buffer_size--;
	}

	/* Ok.  Go ahead and print it */
	return 1;
}

/*
 * scroll_window: Given a window, this is responsible for making sure that
 * the cursor is placed onto the "next" line.  If the window is full, then
 * it will scroll the window as neccesary.  The cursor is always set to the
 * correct place when this returns.
 */
static void 	scroll_window (Window *window)
{
	if (dumb_mode)
		return;

	/*
	 * If the cursor is beyond the window then we should probably
	 * look into scrolling.
	 */
	if (window->cursor == window->display_size)
	{
		int scroll, i;

		/*
		 * If we ever need to scroll a window that is in scrollback
		 * or in hold_mode, then that means either display_window isnt
		 * doing its job or something else is completely broken.
		 * Probably shouldnt be fatal, but i want to trap these.
		 */
		if (window->holding_something || window->scrollback_point)
			panic("Cant scroll this window!");


		/* Scroll by no less than 1 line */
		if ((scroll = get_int_var(SCROLL_LINES_VAR)) <= 0)
			scroll = 1;

		/* Adjust the top of the logical display */
		for (i = 0; i < scroll; i++)
		{
			window->top_of_display = window->top_of_display->next;
			window->distance_from_display--;
		}

		/* Adjust the top of the physical display */
		if (window->visible && foreground && window->display_size)
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
	if (window->visible && window->display_size)
	{
		window->screen->cursor_window = window;
		term_move_cursor(0, window->top + window->cursor);
		term_clear_to_eol();
	}
}

/* * * * * CURSORS * * * * * */
/*
 * cursor_not_in_display: This forces the cursor out of the display by
 * setting the cursor window to null.  This doesn't actually change the
 * physical position of the cursor, but it will force rite() to reset the
 * cursor upon its next call 
 */
void 	cursor_not_in_display (Screen *s)
{
	if (!s)
		s = output_screen;
	if (s->cursor_window)
		s->cursor_window = NULL;
}

/*
 * cursor_in_display: this forces the cursor_window to be the
 * current_screen->current_window. 
 * It is actually only used in hold.c to trick the system into thinking the
 * cursor is in a window, thus letting the input updating routines move the
 * cursor down to the input line.  Dumb dumb dumb 
 */
void 	cursor_in_display (Window *w)
{
	if (!w)
		w = current_window;
	if (w->screen)
		w->screen->cursor_window = w;
}

/*
 * is_cursor_in_display: returns true if the cursor is in one of the windows
 * (cursor_window is not null), false otherwise 
 */
int 	is_cursor_in_display (Screen *screen)
{
	if (!screen && current_window->screen)
		screen = current_window->screen;

	return (screen->cursor_window ? 1 : 0);
}


/* * * * * * * SCREEN UDPATING AND RESIZING * * * * * * * * */
/*
 * repaint_window: redraw the "m"th through the "n"th part of the window.
 * If end_line is -1, then that means clear the display if any of it appears
 * after the end of the scrollback buffer.
 */
void 	repaint_window (Window *w, int start_line, int end_line)
{
	Window *window = (Window *)w;
	Display *curr_line;
	int 	count;
	int 	clean_display = 0;

	if (dumb_mode || !window->visible)
		return;

	if (!window)
		window = current_window;

	if (end_line == -1)
	{
		end_line = window->display_size;
		clean_display = 1;
	}

	curr_line = window->top_of_display;
	for (count = 0; count < start_line; count++)
		curr_line = curr_line->next;

	global_beep_ok = 0;		/* Suppress beeps */
	window->cursor = start_line;
	for (count = start_line; count < end_line; count++)
	{
		rite(window, curr_line->line);

		if (curr_line == window->display_ip)
			break;

		curr_line = curr_line->next;
	}
	global_beep_ok = 1;		/* Suppress beeps */

	/*
	 * If "end_line" is -1, then we're supposed to clear off the rest
	 * of the display, if possible.  Do that here.
	 */
	if (clean_display)
	{
		for (; count < end_line - 1; count++)
		{
			term_clear_to_eol();
			term_newline();
		}
	}

	recalculate_window_cursor(window);	/* XXXX */
}


/* * * * * * * * * * * * * * SCREEN MANAGEMENT * * * * * * * * * * * * */
/*
 * create_new_screen creates a new screen structure. with the help of
 * this structure we maintain ircII windows that cross screen window
 * boundaries.
 */
Screen *create_new_screen (void)
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
	new_s->cursor_window = NULL;
	new_s->current_window = NULL;
	new_s->visible_windows = 0;
	new_s->window_stack = NULL;
	new_s->meta_hit = 0;
	new_s->quote_hit = 0;
	new_s->fdout = 1;
	new_s->fpout = stdout;
	new_s->fdin = 0;
	if (use_input)
		new_open(0);
	new_s->fpin = stdin;
	new_s->control = -1;
	new_s->wserv_version = 0;
	new_s->alive = 1;
	new_s->promptlist = NULL;
	new_s->redirect_name = NULL;
	new_s->redirect_token = NULL;
	new_s->tty_name = (char *) 0;
	new_s->li = current_term->TI_lines;
	new_s->co = current_term->TI_cols;
	new_s->old_li = 0; 
	new_s->old_co = 0;
	new_s->redirect_server = -1;

	new_s->buffer_pos = new_s->buffer_min_pos = 0;
	new_s->input_buffer[0] = '\0';
	new_s->input_cursor = 0;
	new_s->input_visible = 0;
	new_s->input_start_zone = 0;
	new_s->input_end_zone = 0;
	new_s->input_prompt = NULL;
	new_s->input_prompt_len = 0;
	new_s->input_prompt_malloc = 0;
	new_s->input_line = 23;

	last_input_screen = new_s;

	if (!main_screen)
		main_screen = new_s;

	init_input();
	return new_s;
}


#ifdef WINDOW_CREATE
Window	*create_additional_screen (void)
{
        Window  	*win;
        Screen  	*oldscreen, *new_s;
        char    	*displayvar,
                	*termvar;
        int     	screen_type = ST_NOTHING;
        struct  sockaddr_in new_socket;
	int		new_cmd;
	fd_set		fd_read;
	struct	timeval	timeout;
	pid_t		child;
	unsigned short 	port;
	int		new_sock_size;
	char *		wserv_path;

	if (!use_input)
		return NULL;

	if (!(wserv_path = get_string_var(WSERV_PATH_VAR)))
	{
		say("You need to /SET WSERV_PATH before using /WINDOW CREATE");
		return NULL;
	}

	/* Check for X first. */
	if ((displayvar = getenv("DISPLAY")))
	{
		if (!(termvar = getenv("TERM")))
			say("I don't know how to create new windows for this terminal");
		else
			screen_type = ST_XTERM;
	}

	/*
	 * Environment variable STY has to be set for screen to work..  so it is
	 * the best way to check screen..  regardless of what TERM is, the 
	 * execpl() for screen won't just open a new window if STY isn't set,
	 * it will open a new screen process, and run the wserv in its first
	 * window, not what we want...  -phone
	 */
	if (screen_type == ST_NOTHING && getenv("STY"))
		screen_type = ST_SCREEN;


	if (screen_type == ST_NOTHING)
	{
		say("I don't know how to create new windows for this terminal");
		return (Window *) 0;
	}

        say("Opening new %s...",
                screen_type == ST_XTERM ?  "window" :
                screen_type == ST_SCREEN ? "screen" :
                                           "wound" );

	port = 0;
	new_cmd = connect_by_number(NULL, &port, SERVICE_SERVER, PROTOCOL_TCP);
	if (new_cmd < 0)
	{
		yell("Couldnt establish server side -- error [%d] [%s]", 
				new_cmd, my_strerror(errno));
		return NULL;
	}

	oldscreen = current_window->screen;
	new_s = create_new_screen();

	/*
	 * At this point, doing a say() or yell() or anything else that would
	 * output to the screen will cause a refresh of the status bar and
	 * input line.  new_s->current_window is NULL after the above line,
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
			char *opts;
			char *xterm;
			char *args[64], **args_ptr = args;
			char geom[32];
			int i;

			setuid(getuid());
			setgid(getgid());
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

			if (screen_type == ST_SCREEN)
			{
			    opts = m_strdup(get_string_var(SCREEN_OPTIONS_VAR));
			    *args_ptr++ = "screen";
			    while (opts && *opts)
				*args_ptr++ = new_next_arg(opts, &opts);
			}
			else if (screen_type == ST_XTERM)
			{
			    snprintf(geom, 31, "%dx%d", 
				oldscreen->co + 1, 
				oldscreen->li);

			    opts = m_strdup(get_string_var(XTERM_OPTIONS_VAR));
			    if (!(xterm = getenv("XTERM")))
				if (!(xterm = get_string_var(XTERM_VAR)))
				    xterm = "xterm";

			    *args_ptr++ = xterm;
			    *args_ptr++ = "-geometry";
			    *args_ptr++ = geom;
			    while (opts && *opts)
				*args_ptr++ = new_next_arg(opts, &opts);
			    *args_ptr++ = "-e";
			}

			*args_ptr++ = wserv_path;
			*args_ptr++ = "localhost";
			*args_ptr++ = m_strdup(ltoa((long)port));
			*args_ptr++ = NULL;

			execvp(args[0], args);
			_exit(0);
		}
	}

	/* All the rest of this is the parent.... */
	new_sock_size = sizeof(new_socket);
	FD_ZERO(&fd_read);
	FD_SET(new_cmd, &fd_read);
	timeout.tv_sec = (time_t) 10;
	timeout.tv_usec = 0;

	/* 
	 * This infinite loop sb kanan to allow us to trap transitory
	 * error signals
	 */
	for (;;)

	/* 
	 * You need to kill_screen(new_s) before you do say() or yell()
	 * if you know what is good for you...
	 */
	switch (select(new_cmd + 1, &fd_read, NULL, NULL, &timeout))
	{
	    case -1:
	    {
		if ((errno == EINTR) || (errno == EAGAIN))
			continue;
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
			new_s->fdin = accept(new_cmd, 
					(struct sockaddr *)&new_socket,
					&new_sock_size);
			if ((new_s->fdout = new_s->fdin) < 0)
			{
				close(new_cmd);
				kill_screen(new_s);
				yell("Couldn't establish data connection "
					"to new screen");
				return NULL;
			}
			new_open(new_s->fdin);
			new_s->fpin = new_s->fpout = fdopen(new_s->fdin, "r+");
			continue;
		}
		else
		{
			new_s->control = accept(new_cmd,
					(struct sockaddr *)&new_socket,
					&new_sock_size);
			close(new_cmd);
			if (new_s->control < 0)
			{
                                kill_screen(new_s);
                                yell("Couldn't establish control connection "
                                        "to new screen");
                                return NULL;
                        }

			new_open(new_s->control);

                        if (!(win = new_window(new_s)))
                                panic("WINDOW is NULL and it shouldnt be!");
                        return win;
		}
	    }
	}
	return NULL;
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
	while ((window = screen->window_list))
	{
		screen->window_list = window->next;
		add_to_invisible_list(window);
	}

	/* Take out some of the garbage left around */
	screen->current_window = NULL;
	screen->window_list = NULL;
	screen->window_list_end = NULL;
	screen->cursor_window = NULL;
	screen->last_window_refnum = -1;
	screen->visible_windows = 0;
	screen->window_stack = NULL;
	screen->fpin = NULL;
	screen->fpout = NULL;
	screen->fdin = -1;
	screen->fdout = -1;
	new_free(&screen->input_prompt);

	/* Dont fool around. */
	if (last_input_screen == screen)
		last_input_screen = main_screen;

	screen->alive = 0;
	make_window_current(NULL);
	say("The screen is now dead.");
}
#endif /* WINDOW_CREATE */


/* * * * * * * * * * * * * USER INPUT HANDLER * * * * * * * * * * * */
void 	do_screens (fd_set *rd)
{
	Screen *screen;
	char 	buffer[IO_BUFFER_SIZE + 1];

	if (use_input)
	for (screen = screen_list; screen; screen = screen->next)
	{
		if (!screen->alive)
			continue;

		if (screen->control != -1 && 
		    FD_ISSET(screen->control, rd))	/* Wserv control */
		{
			FD_CLR(screen->control, rd);

			if (dgets(buffer, screen->control, 1) < 0)
			{
				kill_screen(screen);
				yell("Error from remote screen [%d].", dgets_errno);
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
				refresh_a_screen(screen);
			}
			else if (!strncmp(buffer, "version=", 8))
			{
				int     version;
				version = atoi(buffer + 8);
				if (version != CURRENT_WSERV_VERSION)
				{   
				    yell("WSERV version %d is incompatable with this binary",
						version);
				    kill_screen(screen);
				}
				screen->wserv_version = version;
			}
		}

		if (FD_ISSET(screen->fdin, rd))
		{
			int	server;

			FD_CLR(screen->fdin, rd);	/* No more! */

			if (screen != main_screen && screen->wserv_version == 0)
			{
				kill_screen(screen);
				yell("The WSERV used to create this new screen is too old.");
				return;
			}

			/*
			 * This section of code handles all in put from 
			 * the terminal(s) connected to ircII.  Perhaps the 
			 * idle time *shouldn't* be reset unless its not a 
			 * screen-fd that was closed..
			 */
			get_time(&idle_time);
			if (cpu_saver)
			{
				cpu_saver = 0;
				update_all_status();
			}

			server = from_server;
			last_input_screen = screen;
			output_screen = screen;
			make_window_current(screen->current_window);
			from_server = current_window->server;

			if (dumb_mode)
			{
				if (dgets(buffer, screen->fdin, 1) < 0)
				{
					say("IRCII exiting on EOF from stdin");
					irc_exit(1, "EPIC - EOF from stdin");
				}

				if (strlen(buffer))
					buffer[strlen(buffer) - 1] = 0;
				if (get_int_var(INPUT_ALIASES_VAR))
					parse_line(NULL, buffer, empty_string, 1, 0);
				else
					parse_line(NULL, buffer, NULL, 1, 0);
			}
			else
			{
				char	loc_buffer[BIG_BUFFER_SIZE + 1];
				int	n, i;

				/*
				 * Read in from stdin.
				 */
				if ((n = read(screen->fdin, loc_buffer, BIG_BUFFER_SIZE)) > 0)
				{
					for (i = 0; i < n; i++)
						edit_char(loc_buffer[i]);
				}

#ifdef WINDOW_CREATE
				/*
				 * if the current screen isn't the main screen,
				 * then the socket to the current screen must have
				 * closed, so we call kill_screen() to handle 
				 * this - phone, jan 1993.
				 * but not when we arent running windows - Fizzy, may 1993
				 * if it is the main screen we got an EOF on, we exit..
				 * closed tty -> chew cpu -> bad .. -phone, july 1993.
				 */
				else if (screen != main_screen)
					kill_screen(screen);
#endif

				/*
				 * If n == 0 or n == -1 at this point, then the read totally
				 * died on us.  This is almost without exception caused by
				 * the ctty being revoke(2)d on us.  4.4BSD guarantees that a
				 * revoke()d ctty will read an EOF, while i believe linux 
				 * fails with EBADF.  In either case, a read failure on the
				 * main screen is totaly fatal.
				 */
				else
					irc_exit(1, "Hey!  Where'd my controlling terminal go?");

			} 
			from_server = server;
		}
	} 
} 


/* * * * * * * * * INPUT PROMPTS * * * * * * * * * * */
/* 
 * add_wait_prompt:  Given a prompt string, a function to call when
 * the prompt is entered.. some other data to pass to the function,
 * and the type of prompt..  either for a line, or a key, we add 
 * this to the prompt_list for the current screen..  and set the
 * input prompt accordingly.
 *
 * XXXX - maybe this belongs in input.c? =)
 */
void 	add_wait_prompt (char *prompt, void (*func)(), char *data, int type, int echo)
{
	WaitPrompt **AddLoc,
		   *New;

	New = (WaitPrompt *) new_malloc(sizeof(WaitPrompt));
	New->prompt = m_strdup(prompt);
	New->data = m_strdup(data);
	New->type = type;
	New->echo = echo;
	New->func = func;
	New->next = NULL;
	for (AddLoc = &current_window->screen->promptlist; *AddLoc;
			AddLoc = &(*AddLoc)->next);
	*AddLoc = New;
	if (AddLoc == &current_window->screen->promptlist)
		change_input_prompt(1);
}


/* * * * * * * * * * * * * * * * * COLOR SUPPORT * * * * * * * * * * */
/*
 * This parses out a ^C control sequence.  Note that it is not acceptable
 * to simply slurp up all digits after a ^C sequence (either by calling
 * strtol(), or while (isdigit())), because people put ^C sequences right
 * before legit output with numbers (like the time in your status bar.)
 * Se we have to actually slurp up only those digits that comprise a legal
 * ^C code.
 */
const u_char *skip_ctl_c_seq (const u_char *start, int *lhs, int *rhs)
{
const 	u_char 	*after = start;
	u_char	c1, c2;
	int *	val;
	int	lv1, rv1;

	/*
	 * For our sanity, just use a placeholder if the caller doesnt
	 * care where the end of the ^C code is.
	 */
	if (!lhs)
		lhs = &lv1;
	if (!rhs)
		rhs = &rv1;

	*lhs = *rhs = -1;

	/*
	 * If we're passed a non ^C code, dont do anything.
	 */
	if (*after != '\003')
		return after;

	/*
	 * This is a one-or-two-time-through loop.  We find the  maximum 
	 * span that can compose a legit ^C sequence, then if the first 
	 * nonvalid character is a comma, we grab the rhs of the code.  
	 */
	val = lhs;
	for (;;)
	{
		/*
		 * If its just a lonely old ^C, then its probably a terminator.
		 * Just skip over it and go on.
		 */
		after++;
		if (*after == 0)
			return after;

		/*
		 * Check for the very special case of a definite terminator.
		 * If the argument to ^C is -1, then we absolutely know that
		 * this ends the code without starting a new one
		 */
		if (after[0] == '-' && after[1] == '1')
			return after + 2;

		/*
		 * Further checks against a lonely old naked ^C.
		 */
		if (!isdigit(after[0]) && after[0] != ',')
			return after;


		/*
		 * Code certainly cant have more than two chars in it
		 */
		c1 = after[0];
		c2 = after[1];

		/*
		 * Our action depends on the char immediately after the ^C.
		 */
		switch (c1)
		{
			/* 
			 * 0X -> 0X <stop> for all numeric X
			 */
			case '0':
				after++;
				*val = c1 - '0';
				if (c2 >= '0' && c2 <= '9')
				{
					after++;
					*val = *val * 10 + (c2 - '0');
				}
				break;

			/* 
			 * 1X -> 1 <stop> X if X >= '7' 
			 * 1X -> 1X <stop>  if X < '7'
			 */
			case '1':
				after++;
				*val = c1 - '0';
				if (c2 >= '0' && c2 < '7')
				{
					after++;
					*val = *val * 10 + (c2 - '0');
				}
				break;

			/*
			 * 3X -> 3 <stop> X if X >= '8'
			 * 3X -> 3X <stop>  if X < '8'
			 * (Same for 4X and 5X)
			 */
			case '3':
			case '4':
				after++;
				*val = c1 - '0';
				if (c2 >= '0' && c2 < '8')
				{
					after++;
					*val = *val * 10 + (c2 - '0');
				}
				break;

			case '5':
				after++;
				*val = c1 - '0';
				if (c2 >= '0' && c2 < '9')
				{
					after++;
					*val = *val * 10 + (c2 - '0');
				}
				break;

			/*
			 * Y -> Y <stop> for any other numeric Y.
			 */
			case '2':
			case '6':
			case '7':
			case '8':
			case '9':
				*val = (c1 - '0');
				after++;
				break;

			/*
			 * Y -> <stop> Y for any other nonnumeric Y
			 */
			default:
				break;
		}

		if (val == lhs)
		{
			val = rhs;
			if (*after == ',')
				continue;
		}
		break;
	}

	return after;
}

