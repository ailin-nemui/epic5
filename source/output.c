/*
 * output.c: handles a variety of tasks dealing with the output from the irc
 * program 
 *
 * Copyright 1990 Michael Sandrof
 * Copyright 1997 EPIC Software Labs
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 */

#include "irc.h"

#include <sys/stat.h>

#include "output.h"
#include "vars.h"
#include "input.h"
#include "term.h"
#include "lastlog.h"
#include "window.h"
#include "screen.h"
#include "hook.h"
#include "ctcp.h"
#include "log.h"
#include "ircaux.h"
#include "alias.h"

/* make this buffer *much* bigger than needed */
#define OBNOXIOUS_BUFFER_SIZE BIG_BUFFER_SIZE * 10
static	char	putbuf[OBNOXIOUS_BUFFER_SIZE + 1];

/* 
 * unflash: sends a ^[c to the screen
 * Must be defined to be useful, cause some vt100s really *do* reset when
 * sent this command. >;-)
 * Now that you can send ansi sequences, this is much less inportant.. 
 */
void unflash (void)
{
#ifdef HARD_UNFLASH
	fwrite("\033c", 2, 1, stdout);		/* hard reset */
#else
	fwrite("\033)0", 3, 1, stdout);		/* soft reset */
#endif
}

/* sig_refresh_screen: the signal-callable version of refresh_screen */
SIGNAL_HANDLER(sig_refresh_screen)
{
	need_redraw = 1;
#if 0
	refresh_a_screen(main_screen);
#endif
}

/*
 * refresh_screen: Whenever the REFRESH_SCREEN function is activated, this
 * swoops into effect 
 */
void refresh_screen (char dumb, char *dumber)
{
	refresh_a_screen(current_window->screen);
}

void	refresh_a_screen (Screen *screen)
{
	unflash();
	term_clear_screen();

	if (screen != main_screen || term_resize())
		recalculate_windows(current_window->screen);
	else
		redraw_all_windows();

	if (need_redraw)
		need_redraw = 0;

	update_all_windows();
	update_input(UPDATE_ALL);
}

/* extern_write -- controls whether others may write to our terminal or not. */
/* This is basically stolen from bsd -- so its under the bsd copyright */
BUILT_IN_COMMAND(extern_write)
{
	char *tty;
	struct stat sbuf;
	const int OTHER_WRITE = 020;

	if (!(tty = ttyname(2)))
	{
		yell("Could not figure out the name of your tty device!");
		return;
	}
	if (stat(tty, &sbuf) < 0)
	{
		yell("Could not get the information about your tty device!");
		return;
	}
	if (!args || !*args)
	{
		if (sbuf.st_mode & 020)
			say("Mesg is 'y'");
		else
			say("Mesg is 'n'");
		return;
	}
	switch (args[0])
	{
		case 'y' :
		{
			if (chmod(tty, sbuf.st_mode | OTHER_WRITE) < 0)
			{
				yell("Could not set your tty's mode!");
				return;
			}
			break;
		}
		case 'n' :
		{
			if (chmod(tty, sbuf.st_mode &~ OTHER_WRITE) < 0)
			{
				yell("Could not set your tty's mode!");
				return;
			}
			break;
		}
		default :
			say("Usage: /%s [Y|N]", command);
	}
}

/* init_windows:  */
int	init_screen (void)
{
	/*
	 * System dependant stuff
	 */
	if (term_init())
		return -1;

	term_clear_screen();
	term_resize();

	/*
	 * System independant stuff
	 */
	create_new_screen();
	new_window(main_screen);
	update_all_windows();

	term_move_cursor(0, 0);
	return 0;
}

/*
 * put_echo: a display routine for echo that doesnt require an sprintf,
 * so it doesnt have that overhead, and it also doesnt hvae any size 
 * limitations.  The sky's the limit!
 */
void	put_echo (const unsigned char *str)
{
	add_to_log(irclog_fp, -1, str);
	add_to_screen(str);
}

/*
 * put_it: the primary irc display routine.  This routine can be used to
 * display output to a user window.  Its ok to have newlines or tabs in
 * the output, but you should be careful not to overflow the 10k buffer
 * used to hold the output (use put_echo if you just want to output an
 * unbounded string).
 *
 * Some systems (notorously Ultrix) cannot gracefully convert float
 * variables through "..." into va_list, and attempting to do so will 
 * cause a crash on dereference (in vsprintf()), so do do that.  You'll
 * have to sprintf() the floats into strings and then pass the string 
 * in with %s.  Ugh.  This is not a bug on our part.
 */
void	put_it (const char *format, ...)
{
	if (window_display && format)
	{
		va_list args;
		va_start (args, format);
		vsnprintf(putbuf, OBNOXIOUS_BUFFER_SIZE, format, args);
		va_end(args);
		put_echo(putbuf);
	}
}

/* 
 * This is an alternative form of put_it which writes three asterisks
 * before actually putting things out.
 */
void 	vsay (const char *format, va_list args)
{
	if (window_display && format)
	{
		char *str;

		*putbuf = 0;
		if ((str = get_string_var(BANNER_VAR)))
		{
			if (get_int_var(BANNER_EXPAND_VAR))
			{
			    int af;
			    char *foo;

			    foo = expand_alias(str, empty_string, &af, NULL);
			    strlcpy(putbuf, foo, OBNOXIOUS_BUFFER_SIZE);
			    new_free(&foo);
			}
			else
			    strlcpy(putbuf, str, OBNOXIOUS_BUFFER_SIZE);

			strlcat(putbuf, " ", OBNOXIOUS_BUFFER_SIZE);
		}

		vsnprintf(putbuf + strlen(putbuf), 
			OBNOXIOUS_BUFFER_SIZE - strlen(putbuf) - 1, 
			format, args);

		put_echo(putbuf);
	}
}

void	say (const char *format, ...)
{
	va_list args;
	va_start(args, format);
	vsay(format, args);
	va_end(args);
}

void	yell (const char *format, ...)
{
	if (format)
	{
		va_list args;
		va_start (args, format);
		vsnprintf(putbuf, OBNOXIOUS_BUFFER_SIZE, format, args);
		va_end(args);
		if (do_hook(YELL_LIST, "%s", putbuf))
			put_echo(putbuf);
	}
}

/*
 * Error is exactly like yell, except that if the error occured while
 * you were loading a script, it tells you where it happened.
 */
void 	error (const char *format, ...)
{
	dump_load_stack(0);
	if (format)
	{
		va_list args;
		va_start (args, format);
		vsnprintf(putbuf, OBNOXIOUS_BUFFER_SIZE, format, args);
		va_end(args);
		do_hook(YELL_LIST, "%s", putbuf);
		put_echo(putbuf);
	}
}
