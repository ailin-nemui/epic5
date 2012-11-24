/* $EPIC: output.c,v 1.25 2012/11/24 01:42:51 jnelson Exp $ */
/*
 * output.c: handles a variety of tasks dealing with the output from the irc
 * program 
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright © 1997, 2002 EPIC Software Labs.
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
#include "output.h"
#include "vars.h"
#include "input.h"
#include "termx.h"
#include "lastlog.h"
#include "window.h"
#include "screen.h"
#include "hook.h"
#include "ctcp.h"
#include "log.h"
#include "ircaux.h"
#include "alias.h"
#include "commands.h"
#include "server.h"
#include "levels.h"

/* make this buffer *much* bigger than needed */
#define OBNOXIOUS_BUFFER_SIZE BIG_BUFFER_SIZE * 10
static	char	putbuf[OBNOXIOUS_BUFFER_SIZE + 1];

/* 
 * unflash: sends a ^[c to the screen
 * Must be defined to be useful, cause some vt100s really *do* reset when
 * sent this command. >;-)
 * Now that you can send ansi sequences, this is much less inportant.. 
 */
static void	unflash (void)
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
}

/*
 * refresh_screen: Whenever the REFRESH_SCREEN function is activated, this
 * swoops into effect 
 */
BUILT_IN_KEYBINDING(refresh_screen)
{
	need_redraw = 1;
}

void	redraw_all_screens (void)
{
	Screen *s, *old_os;

	old_os = output_screen;
	for (s = screen_list; s; s = s->next)
	{
		output_screen = s;
		unflash();
		term_clear_screen();
		if (s == main_screen && term_resize())
			recalculate_windows(current_window->screen);
	}

	/* Logically mark all windows as needing a redraw */
	redraw_all_windows();

	/* Physically redraw all windows and status bars */
	update_all_windows();

	/* Physically redraw all input lines */
	update_input(NULL, UPDATE_ALL);

	output_screen = old_os;
	need_redraw = 0;
}

/* extern_write -- controls whether others may write to our terminal or not. */
/* This is basically stolen from bsd -- so its under the bsd copyright */
BUILT_IN_COMMAND(extern_write)
{
	char *tty;
	Stat sbuf;
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

/*
 * init_screen() sets up a full screen display for normal display mode.
 * It will fail if your TERM setting doesn't have all of the necessary
 * capabilities to run in full screen mode.  The client is expected to 
 * fail-over to dumb mode in this case.
 * This may only be called once, at initial startup (by main()).
 */
int	init_screen (void)
{
	/* Investigate TERM and put the console in full-screen mode */
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
 * put_echo: a display routine for echo that doesnt require an snprintf,
 * so it doesnt have that overhead, and it also doesnt have any size 
 * limitations.  The sky's the limit!
 */
void	put_echo (const unsigned char *str)
{
	add_to_log(0, irclog_fp, -1, str, 0, NULL);
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
 * cause a crash on dereference (in vsnprintf()), so don't do that.  You'll
 * have to snprintf() the floats into strings and then pass the string 
 * in with %s.  Ugh.  This is not a bug on our part.
 */
void	put_it (const char *format, ...)
{
	if (window_display && format)
	{
		va_list args;
		va_start (args, format);
		vsnprintf(putbuf, sizeof putbuf, format, args);
		va_end(args);
		put_echo(putbuf);
	}
}

void	file_put_it (FILE *fp, const char *format, ...)
{
	if (format)
	{
		va_list args;
		va_start (args, format);
		vsnprintf(putbuf, sizeof putbuf, format, args);
		va_end(args);
		if (fp)
		{
			fputs(putbuf, fp);
			fputs("\n", fp);
		}
		else if (window_display)
			put_echo(putbuf);
	}
}

/* 
 * This is an alternative form of put_it which writes three asterisks
 * before actually putting things out.
 */
static void 	vsay (const char *format, va_list args)
{
	if (window_display && format)
	{
		char *str;

		*putbuf = 0;
		if ((str = get_string_var(BANNER_VAR)))
		{
			if (get_int_var(BANNER_EXPAND_VAR))
			{
			    char *foo;

			    foo = expand_alias(str, empty_string);
			    strlcpy(putbuf, foo, sizeof putbuf);
			    new_free(&foo);
			}
			else
			    strlcpy(putbuf, str, sizeof putbuf);

			strlcat(putbuf, " ", sizeof putbuf);
		}

		vsnprintf(putbuf + strlen(putbuf), 
			sizeof(putbuf) - strlen(putbuf) - 1, 
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
		vsnprintf(putbuf, sizeof putbuf, format, args);
		va_end(args);
		if (do_hook(YELL_LIST, "%s", putbuf))
			put_echo(putbuf);
	}
}

void	privileged_yell (const char *format, ...)
{
	if (format)
	{
		va_list args;
		va_start (args, format);
		vsnprintf(putbuf, sizeof putbuf, format, args);
		va_end(args);

		privileged_output++;
		put_echo(putbuf);
		privileged_output--;
	}
}


/*
 * Error is exactly like yell, except that if the error occured while
 * you were loading a script, it tells you where it happened.
 */
void 	my_error (const char *format, ...)
{
	dump_load_stack(0);
	if (format)
	{
		va_list args;
		va_start (args, format);
		vsnprintf(putbuf, sizeof putbuf, format, args);
		va_end(args);
		do_hook(YELL_LIST, "%s", putbuf);
		put_echo(putbuf);
	}
}

/******************************************************************/
/*
 * syserr is exactly like say, except that if the error occured while
 * you were loading a script, it tells you where it happened.
 */
static void     vsyserr (int server, const char *format, va_list args)
{
	char *  str;
	int     l, old_from_server = from_server;
	int	i_set_from_server = 0;

        if (!window_display || !format)
		return;

	*putbuf = 0;
	if ((str = get_string_var(BANNER_VAR)))
	{
		if (get_int_var(BANNER_EXPAND_VAR))
		{
		    char *foo;

		    foo = expand_alias(str, empty_string);
		    strlcpy(putbuf, foo, sizeof putbuf);
		    new_free(&foo);
		}
		else
		    strlcpy(putbuf, str, sizeof putbuf);

		strlcat(putbuf, " INFO -- ", sizeof putbuf);
	}

	vsnprintf(putbuf + strlen(putbuf),
		sizeof(putbuf) - strlen(putbuf) - 1,
		format, args);

	if (is_server_valid(server))
	{
		old_from_server = from_server;
		from_server = server;
		i_set_from_server = 1;
	}

	l = message_from(NULL, LEVEL_SYSERR);
	if (do_hook(YELL_LIST, "%s", putbuf))
		put_echo(putbuf);
	pop_message_from(l);

	if (i_set_from_server)
		from_server = old_from_server;
}

void    syserr (int server, const char *format, ...)
{
        va_list args;
        va_start(args, format);
        vsyserr(server, format, args);
        va_end(args);
}

