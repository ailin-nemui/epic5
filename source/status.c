/*
 * status.c: handles the status line updating, etc for IRCII 
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright 1994 Jake Khuon.
 * Copyright 1995, 2003 EPIC Software Labs.
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

#define __need_putchar_x__
#include "irc.h"
#include "dcc.h"
#include "termx.h"
#include "status.h"
#include "server.h"
#include "vars.h"
#include "hook.h"
#include "input.h"
#include "levels.h"
#include "lastlog.h"
#include "commands.h"
#include "window.h"
#include "screen.h"
#include "mail.h"
#include "output.h"
#include "names.h"
#include "ircaux.h"
#include "alias.h"
#include "clock.h"
#include "functions.h"

#ifdef Char
#undef Char
#endif
#define Char const char

/*
 * Maximum number of "%" expressions in a status line format.  If you change
 * this number, you must manually change the snprintf() in make_status 
 */
#define STATUS_FUNCTION(x) Char * x (int window_, short map, char key)
#define MAX_FUNCTIONS 40
#define MAX_STATUS_USER 39

STATUS_FUNCTION(status_nickname);
STATUS_FUNCTION(status_query_nick);
STATUS_FUNCTION(status_right_justify);
STATUS_FUNCTION(status_chanop);
STATUS_FUNCTION(status_ssl);
STATUS_FUNCTION(status_channel);
STATUS_FUNCTION(status_server);
STATUS_FUNCTION(status_network);
STATUS_FUNCTION(status_mode);
STATUS_FUNCTION(status_umode);
STATUS_FUNCTION(status_insert_mode);
STATUS_FUNCTION(status_overwrite_mode);
STATUS_FUNCTION(status_away);
STATUS_FUNCTION(status_oper);
STATUS_FUNCTION(status_user);
STATUS_FUNCTION(status_dcc);
STATUS_FUNCTION(status_dcc_all);
STATUS_FUNCTION(status_hold);
STATUS_FUNCTION(status_holdmode);
STATUS_FUNCTION(status_version);
STATUS_FUNCTION(status_clock);
STATUS_FUNCTION(status_hold_lines);
STATUS_FUNCTION(status_window);
STATUS_FUNCTION(status_mail);
STATUS_FUNCTION(status_refnum);
STATUS_FUNCTION(status_refnum_real);
STATUS_FUNCTION(status_null_function);
STATUS_FUNCTION(status_notify_windows);
STATUS_FUNCTION(status_voice);
STATUS_FUNCTION(status_cpu_saver_mode);
STATUS_FUNCTION(status_position);
STATUS_FUNCTION(status_scrollback);
STATUS_FUNCTION(status_scroll_info);
STATUS_FUNCTION(status_windowspec);
STATUS_FUNCTION(status_percent);
STATUS_FUNCTION(status_test);
STATUS_FUNCTION(status_swappable);
STATUS_FUNCTION(status_activity);
STATUS_FUNCTION(status_window_prefix);
STATUS_FUNCTION(status_server_status);
STATUS_FUNCTION(status_sequence_point);

/* These are used as placeholders for some expandos */
static	char	*mode_format 		= (char *) 0;
static	char	*umode_format 		= (char *) 0;
static	char	*query_format 		= (char *) 0;
static	char	*clock_format 		= (char *) 0;
static	char	*hold_lines_format 	= (char *) 0;
static	char	*channel_format 	= (char *) 0;
static	char	*cpu_saver_format 	= (char *) 0;
static	char	*mail_format 		= (char *) 0;
static	char	*nick_format		= (char *) 0;
static	char	*server_format 		= (char *) 0;
static	char	*sp_format 		= (char *) 0;
static	char	*notify_format 		= (char *) 0;

	Status	main_status;
	int	main_status_init = 0;
static void	init_status	(void);

/*
 * Status updates are not permitted while we are doing some sensitive
 * operations (like creating a window) to avoid null derefs
 */
	int     status_updates_permitted = 0;
static	int	defered_status_updates = 0;

/*
 * This is the list of possible expandos.  Note that you should not use
 * the '{' character, as it would be confusing.  It is already used for 
 * specifying the map.
 */
struct status_formats {
	short	map;
	char 	key;
	Char	*(*callback_function)(int, short, char);
	char	**format_var;
	int	*format_set;
};
struct status_formats status_expandos[] = {
{ 0, 'A', status_away,          NULL, 			NULL },
{ 0, 'B', status_hold_lines,    &hold_lines_format,	&STATUS_HOLD_LINES_VAR},
{ 0, 'C', status_channel,       &channel_format,	&STATUS_CHANNEL_VAR },
{ 0, 'D', status_dcc, 	        NULL, 			NULL },
{ 0, 'E', status_activity,	NULL,			NULL },
{ 0, 'F', status_notify_windows,&notify_format,		&STATUS_NOTIFY_VAR },
{ 0, 'G', status_network,	NULL,			NULL },
{ 0, 'H', status_hold,		NULL,			NULL },
{ 0, 'I', status_insert_mode,   NULL,			NULL },
{ 0, 'K', status_scrollback,	NULL,			NULL },
{ 0, 'L', status_cpu_saver_mode,&cpu_saver_format,	&STATUS_CPU_SAVER_VAR },
{ 0, 'M', status_mail,		&mail_format,		&STATUS_MAIL_VAR },
{ 0, 'N', status_nickname,	&nick_format,		&STATUS_NICKNAME_VAR },
{ 0, 'O', status_overwrite_mode,NULL,			NULL },
{ 0, 'P', status_position,      NULL,			NULL },
{ 0, 'Q', status_query_nick,    &query_format,		&STATUS_QUERY_VAR },
{ 0, 'R', status_refnum,        NULL, 			NULL },
{ 0, 'S', status_server,        &server_format,     	&STATUS_SERVER_VAR },
{ 0, 'T', status_clock,         &clock_format,      	&STATUS_CLOCK_VAR },
{ 0, 'U', status_user,		NULL, 			NULL },
{ 0, 'V', status_version,	NULL, 			NULL },
{ 0, 'W', status_window,	NULL, 			NULL },
{ 0, 'X', status_user,		NULL, 			NULL },
{ 0, 'Y', status_user,		NULL, 			NULL },
{ 0, 'Z', status_user,		NULL, 			NULL },
{ 0, '#', status_umode,		&umode_format,	     	&STATUS_UMODE_VAR },
{ 0, '%', status_percent,	NULL, 			NULL },
{ 0, '*', status_oper,		NULL, 			NULL },
{ 0, '+', status_mode,		&mode_format,       	&STATUS_MODE_VAR },
{ 0, '.', status_windowspec,	NULL, 			NULL },
{ 0, '=', status_voice,		NULL, 			NULL },
{ 0, '>', status_right_justify,	NULL, 			NULL },
{ 0, '@', status_chanop,	NULL, 			NULL },
{ 0, '|', status_ssl,		NULL,			NULL },
{ 0, '0', status_user,		NULL, 			NULL },
{ 0, '1', status_user,		NULL, 			NULL },
{ 0, '2', status_user,		NULL, 			NULL },
{ 0, '3', status_user,		NULL, 			NULL },
{ 0, '4', status_user,		NULL, 			NULL },
{ 0, '5', status_user,		NULL, 			NULL },
{ 0, '6', status_user,		NULL, 			NULL },
{ 0, '7', status_user,		NULL, 			NULL },
{ 0, '8', status_user,		NULL, 			NULL },
{ 0, '9', status_user,		NULL, 			NULL },
{ 1, '0', status_user,		NULL, 			NULL },
{ 1, '1', status_user,		NULL, 			NULL },
{ 1, '2', status_user,		NULL, 			NULL },
{ 1, '3', status_user,		NULL, 			NULL },
{ 1, '4', status_user,		NULL, 			NULL },
{ 1, '5', status_user,		NULL, 			NULL },
{ 1, '6', status_user,		NULL, 			NULL },
{ 1, '7', status_user,		NULL, 			NULL },
{ 1, '8', status_user,		NULL, 			NULL },
{ 1, '9', status_user,		NULL, 			NULL },
{ 1, 'D', status_dcc_all,	NULL, 			NULL },
{ 1, 'F', status_notify_windows,&notify_format,		&STATUS_NOTIFY_VAR },
{ 1, 'H', status_holdmode,	NULL,			NULL },
{ 1, 'K', status_scroll_info,	NULL,			NULL },
{ 1, 'P', status_window_prefix, NULL,			NULL },
{ 1, 'R', status_refnum_real,   NULL, 			NULL },
{ 1, 'S', status_server,        &server_format,     	&STATUS_SERVER_VAR },
{ 1, 'T', status_test,		NULL,			NULL },
{ 1, 'W', status_swappable,	NULL,			NULL },
{ 1, '+', status_mode,		&mode_format,       	&STATUS_MODE_VAR },
{ 2, '0', status_user,	 	NULL, 			NULL },
{ 2, '1', status_user,	 	NULL, 			NULL },
{ 2, '2', status_user,	 	NULL, 			NULL },
{ 2, '3', status_user,	 	NULL, 			NULL },
{ 2, '4', status_user,		NULL, 			NULL },
{ 2, '5', status_user,	 	NULL, 			NULL },
{ 2, '6', status_user,	 	NULL, 			NULL },
{ 2, '7', status_user,	 	NULL,			NULL },
{ 2, '8', status_user,	 	NULL, 			NULL },
{ 2, '9', status_user,	 	NULL, 			NULL },
{ 2, 'P', status_sequence_point, &sp_format,		&STATUS_SEQUENCE_POINT_VAR },
{ 2, 'S', status_server,        &server_format,     	&STATUS_SERVER_VAR },
{ 2, 'W', status_window,	NULL, 			NULL },
{ 2, '+', status_mode,		&mode_format,       	&STATUS_MODE_VAR },
{ 3, '0', status_user,	 	NULL, 			NULL },
{ 3, '1', status_user,	 	NULL, 			NULL },
{ 3, '2', status_user,	 	NULL, 			NULL },
{ 3, '3', status_user,	 	NULL, 			NULL },
{ 3, '4', status_user,	 	NULL, 			NULL },
{ 3, '5', status_user,	 	NULL, 			NULL },
{ 3, '6', status_user,	 	NULL, 			NULL },
{ 3, '7', status_user,	 	NULL, 			NULL },
{ 3, '8', status_user,	 	NULL, 			NULL },
{ 3, '9', status_user,	 	NULL, 			NULL },
{ 3, 'S', status_server,        &server_format,     	&STATUS_SERVER_VAR },
{ 3, 'W', status_window,	NULL, 			NULL },
{ 3, '+', status_mode,		&mode_format,       	&STATUS_MODE_VAR },
{ 4, 'S', status_server,        &server_format,     	&STATUS_SERVER_VAR },
{ 5, 'S', status_server_status,	NULL,			NULL }
};
#define NUMBER_OF_EXPANDOS (sizeof(status_expandos) / sizeof(struct status_formats))

/*
 * convert_sub_format: This is used to convert the formats of the
 * sub-portions of the status line to a format statement specially designed
 * for that sub-portions.  convert_sub_format looks for a single occurence of
 * %c (where c is passed to the function). When found, it is replaced by "%s"
 * for use in a snprintf.  All other occurences of % followed by any other
 * character are left unchanged.  Only the first occurence of %c is
 * converted, all subsequence occurences are left unchanged.  This routine
 * mallocs the returned string. 
 */
char	*convert_sub_format (const char *format, char c)
{
	char	buffer[BIG_BUFFER_SIZE + 1];
	int	pos = 0;
	int	dont_got_it = 1;

	if (!format)
		return NULL;		/* NULL in, NULL out */

	while (*format && pos < BIG_BUFFER_SIZE - 4)
	{
		if (*format != '%')
		{
			buffer[pos++] = *format++;
			continue;
		}

		format++;
		if (*format == c && dont_got_it)
		{
			dont_got_it = 0;
			buffer[pos++] = '%';
			buffer[pos++] = 's';
			format++;
		}
		else if (*format != '%')
		{
			buffer[pos++] = '%';
			buffer[pos++] = '%';
			buffer[pos++] = *format;
			format++;
		}
		else
		{
			buffer[pos++] = '%';
			buffer[pos++] = '%';
		}
	}

	buffer[pos] = 0;
	return malloc_strdup(buffer);
}


/*
 * This walks a raw format string and parses out any expandos that it finds.
 * An expando is handled by pre-fetching any string variable that is used
 * by the callback function, the callback function is registered, and a
 * %s format is put in the snprintf()-able return value (stored in buffer).
 * All other characters are copied as-is to the return value.
 */
static void	build_status_format (Status *s, int k)
{
	char	buffer[BIG_BUFFER_SIZE + 1];
	int	cp;
	short	map;
	char	key;
	unsigned	i;
	Char	*raw = s->line[k].raw;
	char	*format = buffer;

	debuglog("build_status_format for status line %d", k);

	cp = s->line[k].count = 0;
	while (raw && *raw && (format - buffer < BIG_BUFFER_SIZE - 4))
	{
		if (*raw != '%')
		{
			*format++ = *raw++;
			continue;
		}

		/* It is a % */
		map = 0;

		/* Find the map, if neccesary */
		if (*++raw == '{')
		{
			char	*endptr;

			raw++;
			map = strtoul(raw, &endptr, 10);
			if (*endptr != '}')
			{
				/* Unrecognized */
				continue;
			}
			raw = endptr + 1;
		}

		key = *raw++;

		/* Choke once we get to the maximum number of expandos */
		if (cp >= MAX_FUNCTIONS)
			continue;

		for (i = 0; i < NUMBER_OF_EXPANDOS; i++)
		{
			if (status_expandos[i].map != map ||
			    status_expandos[i].key != key)
				continue;

			if (status_expandos[i].format_var)
				new_free(status_expandos[i].format_var);
			if (status_expandos[i].format_set != NULL)
				*(status_expandos[i].format_var) = 
					convert_sub_format(get_string_var(*status_expandos[i].format_set), key);

			*format++ = '%';
			*format++ = 's';

			s->line[k].func[cp] = status_expandos[i].callback_function;
			s->line[k].map[cp] = map;
			s->line[k].key[cp] = key;
			cp++;
			break;
		}
	}

	s->line[k].count = cp;
	*format = 0;
	malloc_strcpy(&(s->line[k].format), buffer);
	while (cp < MAX_FUNCTIONS)
	{
		s->line[k].func[cp] = status_null_function;
		s->line[k].map[cp] = 0;
		s->line[k].key[cp] = 0;
		cp++;
	}
}

/*
 * STEP ONE	- Compile the Status
 *
 * compile_status:  Compile a status object so it can be passed later
 *			to make_status().  You should always use the same
 *			window for a status to any function.
 *
 * Arguments:
 *	w	- A window whose status is being complied.
 *		  This should be NULL when 's' is &main_status.
 *	s	- A window's Status object that needs to be compiled.
 *		  The Status object can have a 'raw' value which will be
 *		  compiled -- otherwise, this Status will use the global
 *		  default status format (main_status)
 *
 * Return value:
 *	None -- but the results are stored in 's'
 *
 * Notes:
 *   You may be asking "why do you pass in 's' separately from 'w'?
 *    1. 'w' is permitted to be NULL, but 's' is never NULL
 *    2. $status_oneoff() will pass in its own 's' 
 */
void	compile_status (int window_, Status *s)
{
	int 	i,
		k;

	if (window_ > 0)
	{
		if (!window_is_valid(window_))
			return;
		debuglog("compile_status for window %d", get_window_user_refnum(window_));
	}
	else
		debuglog("compile_status for global");

	for (k = 0; k < 3; k++)
	{
		new_free((char **)&s->line[k].format);
		s->line[k].count = 0;

		/*
		 * If we have an overriding status_format, then we parse
		 * that out.
		 */
		if (window_ > 0 && s->line[k].raw)
			build_status_format(s, k);

		/*
		 * Otherwise, If this is for a window, just copy the essential
		 * information over from the main status lines.
		 */
		else if (window_ > 0)
		{
			s->line[k].format = malloc_strdup(main_status.line[k].format);
			for (i = 0; i < MAX_FUNCTIONS; i++)
			{
			    s->line[k].func[i] = main_status.line[k].func[i];
			    s->line[k].map[i] = main_status.line[k].map[i];
			    s->line[k].key[i] = main_status.line[k].key[i];
			}
			s->line[k].count = main_status.line[k].count;
		}

		/*
		 * Otherwise, this *is* the main status lines we are generating
		 * and we need to do all the normal shenanigans.
		 */
		else
		{
			if (k == 0)
			   malloc_strcpy(&s->line[k].raw, get_string_var(STATUS_FORMAT_VAR));
			else if (k == 1)
			   malloc_strcpy(&s->line[k].raw, get_string_var(STATUS_FORMAT1_VAR));
			else  /* (k == 2) */
			   malloc_strcpy(&s->line[k].raw, get_string_var(STATUS_FORMAT2_VAR));

			build_status_format(s, k);
		}
	}
}

/*
 * STEP TWO 	- Convert a complied Status into a string
 *
 * make_status: Convert a pre-compiled Status bar into a text string which is
 *		suitable for outputting.  
 *
 * Arguments:
 *	window - The window whose status is being generated
 *	status - A precompiled Status bar (ie, it's been run through 
 *		 compile_status()) template
 *
 * Return value:
 *	-1 	Something bad happened:
 *		* Status updates are forbidden (status_updates_permitted == 0)
 *		* window was 0 or Status was NULL (what do you want me to do?)
 * 	0	The status bar was updated, but nothing changed
 *	>= 1	The status bar was updated -- you should call redraw_status()
 */
int	make_status (int window_, Status *status)
{
	int		status_line;
	char	buffer	    [BIG_BUFFER_SIZE + 1];
	char	lhs_buffer  [BIG_BUFFER_SIZE + 1];
	char	rhs_buffer  [BIG_BUFFER_SIZE + 1];
	const char	*func_value [MAX_FUNCTIONS];
	size_t		save_size;
	int		anything_changed = 0;
	int		user_refnum;
	int		screen_columns;

	/* Should this be a panic? */
	if (window_ == 0 || status == NULL)
	{
		debuglog("make_status -- window is null or status is null");
		return -1;
	}

	user_refnum = get_window_user_refnum(window_);

	if (!status_updates_permitted)
	{
		debuglog("make_status(%d) -- no status updates right now", user_refnum);
		defered_status_updates++;
		return -1;
	}

	/* For hidden windows, we just pretend they're on the main screen */
	if (get_window_screennum(window_) < 0)
	{
		debuglog("make_status(%d) -- updating hidden window", user_refnum);
		screen_columns = get_screen_columns(main_screen);
	}
	else
		screen_columns = get_screen_columns(get_window_screennum(window_));

	for (status_line = 0; status_line < status->number; status_line++)
	{
		int	fillchar;
		char	*lhp = lhs_buffer,
			*rhp = rhs_buffer;
		char 	*cp,
			*str = NULL;
	const char *	start_rhs = 0;
		int	pr_lhs = 0,
			pr_rhs = 0,
			line = 0,	/* XXX gcc4 lameness */
			*prc = &pr_lhs, 
			i;
	const char *	s;
		int	code_point;
		int	cols;
		ptrdiff_t	offset;

		fillchar = 0;

		/*
		 * Figure out which of the three status bars we're creating.
		 */
		if (status->number == 1 && status_line == 0)
			line = 0;
		else if (status->number == 2 && status_line == 0)
			line = 1;
		else if (status->number == 2 && status_line == 1)
			line = 2;
		else
			panic(1, "make_status: for window [%d], status->number is [%d] and status_line "
				"is [%d] and that makes no sense!", 
				user_refnum, status->number, status_line);

		debuglog("make_status(%d): status line %d, number %d, line %d",
			user_refnum, status_line, status->number, line);

		/*
		 * Sanity check:  If the status format doesnt exist, dont do
		 * anything for it.
		 */
		if (!status->line[line].format)
		{
			debuglog("make_status(%d/%d): no status format", user_refnum, line);
			continue;
		}

		/*
		 * Run each of the status-generating functions from the the
		 * status list.  Note that the retval of the functions is no
		 * longer malloc()ed.  This saves 40-some odd malloc/free sets
		 * each time the status bar is updated, which is non-trivial.
		 */
		for (i = 0; i < MAX_FUNCTIONS; i++)
		{
			if (status->line[line].func[i] == NULL)
			{
				debuglog("make_status(%d/%d/%d): Not set up", line, i);
				return -1;	/* Not set up yet */
/* 				panic(1, "status callback null.  window [%d], line [%d], function [%d]", user_refnum, line, i); */
			}
			func_value[i] = status->line[line].func[i]
				(window_, status->line[line].map[i], status->line[line].key[i]);
		}

		/*
		 * Now press the status line into "buffer".  The magic about
		 * setting status_format is handled elsewhere.
		 */
		str = buffer;
		snprintf(str, BIG_BUFFER_SIZE - 1, status->line[line].format,
		    func_value[0], func_value[1], func_value[2], 
		    func_value[3], func_value[4], func_value[5],
		    func_value[6], func_value[7], func_value[8], func_value[9],
		    func_value[10], func_value[11], func_value[12],
		    func_value[13], func_value[14], func_value[15],
		    func_value[16], func_value[17], func_value[18],
		    func_value[19], func_value[20], func_value[21],
		    func_value[22], func_value[23], func_value[24],
		    func_value[25], func_value[26], func_value[27],
		    func_value[28], func_value[29], func_value[30],
		    func_value[31], func_value[32], func_value[33],
		    func_value[34], func_value[35], func_value[36], 
		    func_value[37], func_value[38], func_value[39]); 

		/*
		 * If the user wants us to, pass the status bar through the
		 * expander to pick any variables/function calls.
		 * This is horribly expensive, but what do i care if you
		 * want to waste cpu cycles? ;-)
		 */
		if (get_int_var(STATUS_DOES_EXPANDOS_VAR))
		{
			int  	old_fs = from_server;
			int	ocw = get_window_refnum(0);
			int	owd;

			debuglog("make_status(%d/%d): expanding expandos", user_refnum, line);
			make_window_current_informally(window_);
			from_server = get_window_server(0);
			owd = swap_window_display(0);

			str = expand_alias(buffer, empty_string);

			swap_window_display(owd);
			from_server = old_fs;
			make_window_current_informally(ocw);
			strlcpy(buffer, str, sizeof buffer);
			new_free(&str);
		}


		/*
		 * This converts away any ansi codes in the status line
		 * in accordance with the currenet settings.  This leaves us
		 * with nothing but logical characters, which are then easy
		 * to count. :-)
		 */
		str = new_normalize_string((const char *)buffer, 3, display_line_mangler);

		/*
		 * Count out the characters.
		 * Walk the entire string, looking for nonprintable
		 * characters.  We count all the printable characters
		 * on both sides of the %> tag.
		 */
		s = (char *)str;
		cp = lhp;
		lhs_buffer[0] = rhs_buffer[0] = 0;

		while ((code_point = next_code_point2(s, &offset, 1)))
		{
			if (code_point < 0)
				s++;
			else
				s += offset;

			/*
			 * The FIRST such %> tag is used.
			 * Using multiple %>s is bogus.
			 */
			if (code_point == '\f' && start_rhs == NULL)
			{
				start_rhs = s;
				*cp = 0;

				cp = rhp;
				prc = &pr_rhs;
			}

                        /*
                         * Skip over attribute changes, not useful.
                         */
                        else if (code_point == 6)
                        {
				size_t numbytes = 0;

				*cp++ = 6;

				/* XXX BIG_BUFFER_SIZE here is wrong. */
				copy_internal_attribute(s, cp, BIG_BUFFER_SIZE, &numbytes);
				s += numbytes;
				cp += numbytes;
                        }

			/*
			 * XXXXX This is a bletcherous hack.
			 * If i knew what was good for me id not do this.
			 */
			else if (code_point == 9)	/* TAB */
			{
				fillchar = 32;	/* Fill becomes space */

				do
					*cp++ = ' ';
				while (++(*prc) % 8);
			}

			/*
			 * So it is a printable character.
			 */
			else
			{
				char	utf8str[16];
				const char *x;
	
				if (!start_rhs)
					fillchar = code_point;

				cols = codepoint_numcolumns(code_point);
				if (cols == -1)
					cols = 0;
				*prc += cols;

				ucs_to_utf8(code_point, utf8str, sizeof(utf8str));
				for (x = utf8str; *x; x++)
					*cp++ = *x;
			}

			/*
			 * Dont allow more than CO printable characters
			 */
			if (pr_lhs + pr_rhs >= screen_columns)
			{
				*cp = 0;
				break;
			}
		}
		*cp = 0;

		/* What will we be filling with? */
		if (get_int_var(STATUS_NO_REPEAT_VAR))
			fillchar = 32;	/* Fill becomes space */

		/*
		 * Now if we have a rhs, then we have to adjust it.
		 */
		/* Not attached, so don't "fix" it */
		{
			char	utf8str[16];
			int 		numf = 0;

			if ((cols = codepoint_numcolumns(fillchar)) < 1)
			{
				/* Use space as fillchar if necessary */
				fillchar = 32;
				cols = 1;
			}
			ucs_to_utf8(fillchar, utf8str, sizeof(utf8str));

			numf = screen_columns - pr_lhs - 1;
			if (start_rhs)
				numf -= pr_rhs;

			while (numf >= 0)
			{
				strlcat(lhs_buffer, utf8str, sizeof lhs_buffer);
				numf -= cols;
			}
		}

		save_size = strlen(all_off());
		strlcpy(buffer, lhs_buffer, sizeof buffer - save_size);
		strlcat(buffer, rhs_buffer, sizeof buffer - save_size);
		strlcat(buffer, all_off(), sizeof buffer);
		new_free(&str);

		if (!status->line[line].result ||
			strcmp(buffer, status->line[line].result))
		{
		    /*
		     * Roll the new back onto the old
		     */
		    debuglog("Make_status(%d/%d/%d): changed", user_refnum, status_line, line);
		    malloc_strcpy(&status->line[line].result, buffer);
		    anything_changed++;

		    /*
		     * Ends up that BitchX always throws this hook and
		     * people seem to like having this thrown in standard
		     * mode, so i'll go along with that.
		     *
		     * We now do this unconditionally, rather than 
		     * waiting until the window to be redrawn.  This was
		     * because people want this thrown for invisible
		     * windows (although they might change their minds)
		     */
		    do_hook(STATUS_UPDATE_LIST, "%d %d %s", user_refnum, status_line, buffer);
		}
	}

	debuglog("make_status: made %d changes", anything_changed);
	return anything_changed;
}

/*
 * STEP THREE 	- Output a status
 *
 * redraw_status: Output a generated status bar after you've decided the
 *		  screen needs to be updated (because make_status returned 1
 *		  or because the user redrew the screen, whatever)
 *
 * Arguments:
 *	window - The window whose status is being generated
 *	status - A generated Status bar (ie, it's been run through 
 *		 make_status()) template
 *
 * Return value:
 *	-1 	Something bad happened:
 *		* Status updates are forbidden (status_updates_permitted == 0)
 *		* window was 0 or Status was NULL (what do you want me to do?)
 *	0	The status bars were updated
 */
int	redraw_status (int window_, Status *status)
{
	int	status_line, line;
	char *	status_str;
	int	user_refnum;

	/* Should this be a panic? */
	if (window_ < 1 || status == NULL)
	{
		debuglog("redraw_status: window or status is null!");
		return -1;
	}

	user_refnum = get_window_user_refnum(window_);

	if (!status_updates_permitted)
	{
		debuglog("redraw_status: no status updates right now, try later");
		defered_status_updates++;
		return -1;
	}
	debuglog("redraw_status(%d): redrawing", user_refnum);

	for (status_line = 0; status_line < status->number; status_line++)
	{
		/*
		 * Figure out which of the three status bars we're creating.
		 */
		if (status->number == 1 && status_line == 0)
			line = 0;
		else if (status->number == 2 && status_line == 0)
			line = 1;
		else if (status->number == 2 && status_line == 1)
			line = 2;
		else
			return -1;

		if (!(status_str = status->line[line].result))
		{
			debuglog("redraw_status(%d/%d/%d): no status bar",
				user_refnum, status_line, line);
			continue;
		}

		if (dumb_mode || !foreground || get_window_screennum(window_) < 0)
		{
			debuglog("redraw_status(%d/%d/%d): dumb/bg/hidden",
				user_refnum, status_line, line);
			continue;
		}

		/*
		 * Output the status line to the screen
		 */
		output_screen = get_window_screennum(window_);
		term_move_cursor(0, get_window_bottom(window_) + status_line);
		output_with_count(status_str, 1, 1);
		debuglog("redraw_status(%d/%d/%d): status redrawn",
			user_refnum, status_line, line);
	}
	return 0;
}

static	void	initialize_status (Status *s)
{
	int	i, k;

	s->number = 1;
	s->special = NULL;
	s->prefix_when_current = NULL;
	s->prefix_when_not_current = NULL;
	for (i = 0; i < 3; i++)
	{
		s->line[i].raw = NULL;
		s->line[i].format = NULL;
		s->line[i].count = 0;
		s->line[i].result = NULL;
		for (k = 0; k < MAX_FUNCTIONS; k++)
		{
			s->line[i].func[k] = NULL;
			s->line[i].map[k] = 0;
			s->line[i].key[k] = 0;
		}
	}
}

static Status *	new_status (void)
{
	Status *s;
	s = new_malloc(sizeof(Status));
	initialize_status(s);
	return s;
}

static void	init_status	(void)
{
	initialize_status(&main_status);
	main_status_init = 1;
}

static void	destroy_status (Status **s)
{
	int	i, k;

	(*s)->number = -1;
	new_free(&((*s)->special));
	new_free(&((*s)->prefix_when_current));
	new_free(&((*s)->prefix_when_not_current));
	for (i = 0; i < 3; i++)
	{
		new_free(&((*s)->line[i].raw));
		new_free(&((*s)->line[i].format));
		(*s)->line[i].count = 0;
		new_free(&((*s)->line[i].result));
		for (k = 0; k < MAX_FUNCTIONS; k++)
		{
			(*s)->line[i].func[k] = NULL;
			(*s)->line[i].map[k] = 0;
			(*s)->line[i].key[k] = 0;
		}
	}
	new_free(s);
}


/*
 * This function is called whenever you change a global status format or 
 * one of the "subexpandos" for the status format (ie, /set status_away) 
 * so that means every status in the system needs to be recompiled.
 *
 * This function can be quite expensive.
 *
 * This function is a /SET callback, so it must always take a (void *) as an
 * argument even though we don't care about it.
 */
void	build_status	(void *stuff)
{
	int	window = 0;

	if (!main_status_init)
		init_status();

	/* Recompile every status */
	compile_status(0, &main_status);
	while (traverse_all_windows2(&window))
		compile_status(window, get_window_status(window));

	/* This forces make_status() and redraw_status() */
	update_all_status();
}


/*
 * permit_status_update: sets the status_update_flag to whatever flag is.
 */
int     permit_status_update (int flag)
{
        int 	old_flag;

	old_flag = status_updates_permitted;
        status_updates_permitted = flag;
	debuglog("permit_status_update: %d -> %d", old_flag, flag);

	/* XXX I hate this, but I just want this problem to go away. */
	/* This is caused by doing a /window command within /on window_create */
	if (flag && defered_status_updates)
	{
		debuglog("permit_status_update: forcing update_all_*");
		update_all_status();
		update_all_windows();
		defered_status_updates = 0;
	}

        return old_flag;
}


/******************** STATUS FORMAT EXPANDOS ***************************/
/* Some useful macros */
/*
 * This is used to get the current window on a window's screen
 */
#if 0
#define CURRENT_WINDOW window->screen->input_window
#endif

/*
 * This tests to see if the window IS the current window on its screen
 */
#define IS_CURRENT_WINDOW (get_window_screennum(window_) >= 0 && get_screen_input_window(get_window_screennum(window_)) == window_)

/*
 * This tests to see if all expandoes are to appear in all status bars
 */
#define SHOW_ALL_WINDOWS (get_int_var(SHOW_STATUS_ALL_VAR))

/*
 * "Current-type" window expandoes occur only on the current window for a 
 * screen.  However, if /set show_status_all is on, then ALL windows act as
 * "Current-type" windows.
 */
#define DISPLAY_ON_WINDOW (IS_CURRENT_WINDOW || SHOW_ALL_WINDOWS)


/*
 * "Ack!  What are these macros?" you may be asking.
 *
 * Traditionally, when ircII redraws your status bar, it calls a function
 * for each status expando you have in your status bar (ie, %T calls 
 * status_time()).  Each function returns a malloced string, which is then
 * sprintf()d into a bigger string, and the malloced string is freed.
 * This is inefficient, because status redraws happen pretty frqeuently
 * and malloc()ing is very expensive.
 *
 * Historically, when epic redraws your status bar, it calls a function
 * for each status expando, just like ircII, but each function has its
 * own private 'static' variable used for return values.  This requires
 * less cpu since there isn't a lot of extra malloc+free stuff going on.
 * The downside is it's possible to fill/overrun these buffers, which 
 * would lead to the values being truncated.
 *
 * As a compromise, we now use a two-cycle system.  We continue to use the
 * static variable as we always have, but we malloc() it off only the first
 * time we use it, and any time it needs to grow in size.  This permits us
 * to avoid malloc+free() every single time for every single status bar, and
 * avoid possible truncation/overrun of predefined buffer sizes.
 *
 * These macros are only necessary for those status expandos that might
 * return values of indeterminite length.  It's not necessary for those 
 * functions where the return value is hardcoded or has known limited size.
 *
 * So what we do is do a snprintf() into the return value, and snprintf()
 * tells us if the result was truncated.  If it was truncated, we resize
 * the return value buffer and then do the snprintf() again, and this time
 * it won't be truncated. 
 */

/* 
 * This macro declares the expandable return value variables.
 *	my_bufferx	Is the actual resizable return value.  It is malloced
 *			of when first used, and then resized any time a return
 *			value would overflow it.
 *	my_bufferxsize	The actual size we have for the return value. This is 
 *			the "having" space.
 *	actual_size	The actual size of what we want to return.  This is the
 *			"needed" space.
 *
 */
#define STATUS_VARS \
	static char *	my_bufferx = NULL; \
	static size_t 	my_bufferxsize = 64; \
	       ssize_t	actual_size; \
		char *	expanded_arg = NULL;

/* After you change 'my_bufferxspace', call this to update 'my_bufferx' */
#define CHECK \
	RESIZE(my_bufferx, char, my_bufferxsize);

/* 
 * After you change 'actual_size', call this to check if the results
 * overflowed the return value buffer.  If it did overflow, it resizes
 * the return value buffer so it won't overflow again.
 */
#define RECHECK \
	if (actual_size < 0) 	/* Die Die Die */ \
		my_bufferxsize += 16;	\
	else if (actual_size < (ssize_t)my_bufferxsize) \
		break; \
	else					\
		my_bufferxsize = actual_size + 1; \
	CHECK

/*
 * Do a sprintf() using the printf format "fmt", which must contain only
 * one "%s" and no other %-formats, using the 'arg' string.  If the result
 * overflows the return buffer, the return buffer is resized and we do it
 * a second time.
 */
#define PRESS(fmt, arg) \
	if (! fmt ) return empty_string; \
\
	if (get_int_var(STATUS_DOES_EXPANDOS_VAR)) { 	\
		size_t	siz;  				\
		siz = strlen(arg) * 2 + 6;		\
		expanded_arg = alloca(siz); 		\
		*expanded_arg = 0; 			\
		escape_chars(arg, "$\\", expanded_arg, siz); \
	} else { \
		expanded_arg = LOCAL_COPY(arg); \
	} \
\
	CHECK \
	do { \
		actual_size = (ssize_t)snprintf(my_bufferx, my_bufferxsize, fmt, expanded_arg); \
		RECHECK \
	} while (1);  \

/*
 * Return the return buffer.  The reason this is not part of 'PRESS' is 
 * because sometimes you may want to do something between the PRESS but 
 * before the RETURN, based on the result.
 */
#define RETURN return my_bufferx;

/***********************************************************************/
/*
 * These are the functions that all of the status expandoes invoke
 */

/*
 * This is your current nickname in the window.
 */
STATUS_FUNCTION(status_nickname)
{
	STATUS_VARS

	PRESS(nick_format, get_server_nickname(get_window_server(window_)))
	RETURN
}

/*
 * This displays the server that the window is connected to.
 */
STATUS_FUNCTION(status_server)
{
	STATUS_VARS
const	char	*n = NULL;

	/*
	 * If there is only one server, dont bother telling the user
	 * what it is.
	 */
	if (map == 0 && connected_to_server == 1)
		return empty_string;

	/*
	 * If this window isnt connected to a server, say so.
	 */
	if (get_window_server(window_) == NOSERV)
		return "No Server";

	/* Map 0 uses the shortname, shown when multi-connected */
	/* Map 1 uses the shortname, shown at all times */
	/* Map 2 uses the full name, shown at all times */
	/* Map 3 uses the groupname, shown at all times */
	/* Map 4 uses the full itsname, shown at all times */
	if (map == 0 || map == 1) 
		n = get_server_altname(get_window_server(window_), 0);
	else if (map == 3)
		n = get_server_group(get_window_server(window_));
	else if (map == 4)
		n = get_server_itsname(get_window_server(window_));

	if (!n)
		n = get_server_name(get_window_server(window_));
	if (!n)
		return "Unknown";

	PRESS(server_format, n)
	RETURN
}

STATUS_FUNCTION(status_server_status)
{
	const char *	state = NULL;

	state = get_server_state_str(get_window_server(window_));
	return state ? state : "Unknown";
}

/*
 * Displays the 005 "NETWORK" value for the current server for the window.
 */
STATUS_FUNCTION(status_network)
{
	const char *text = NULL;

	if (get_window_server(window_) != NOSERV)
		text = get_server_005(get_window_server(window_), "NETWORK");

	return text ? text : "Unknown";
}

/*
 * This displays any nicks that you may have as your current query in the
 * given window.
 */
STATUS_FUNCTION(status_query_nick)
{
	STATUS_VARS
	const char *q;

	if (!(q = get_window_equery(window_)))
		return empty_string;

	PRESS(query_format, q)
	RETURN
}

/*
 * This forces anything to the right of this point to be right-justified
 * (if possible) on the status bar.  Note that the right hand side is always
 * cropped, and not the left hand side.  That is to say, this is a hint that
 * if the left hand side is too short, that the "filler" should be put here
 * at this point rather than at the end of the status line.
 */
STATUS_FUNCTION(status_right_justify)
{
	static char my_buffer[] = "\f";
	return my_buffer;
}

/*
 * Displays whatever windows are notifying, and have notified
 * (usually when output happens on hidden windows that you marked with 
 * /window notify on)
 */
STATUS_FUNCTION(status_notify_windows)
{
	STATUS_VARS
	int	doneone = 0;
	char	buf2[BIG_BUFFER_SIZE];
	int	w = 0;

	/*
	 * This only goes to a current-type window.
	 */
	if (!DISPLAY_ON_WINDOW)
		return empty_string;

	/*
	 * Look for any notifying windows that have had some output since 
	 * they have been hidden and collect their refnums.
	 */
	*buf2 = 0;
	while (traverse_all_windows2(&w))
	{
		if (get_window_notified(w))
		{
			const char *s = NULL;

			if (!(s = get_window_notify_name(w)))
				s = get_window_name(w);

			if (doneone++)
				strlcat(buf2, ",", sizeof buf2);
			strlcat(buf2, (map == 1 && s) ? s :
					ltoa(get_window_user_refnum(w)), sizeof buf2);
		}
	}

	/*
	 * Only do the snprintf if there are windows to process.
	 */
	if (!doneone)
		return empty_string;

	PRESS(notify_format, buf2)
	RETURN
}

/*
 * Displays what time it is on the current-type window
 */
STATUS_FUNCTION(status_clock)
{
	STATUS_VARS

	if (!get_int_var(CLOCK_VAR) || !DISPLAY_ON_WINDOW)
		return empty_string;

	PRESS(clock_format, get_clock())
	RETURN
}

/*
 * The current channel mode for the current channel (if any)
 */
STATUS_FUNCTION(status_mode)
{
	STATUS_VARS
	const char *	mode = NULL;
	const char *	chan = NULL;

	/* If the user has no mode format, or we're not connected, punt. */
        if (get_window_server(window_) == NOSERV)
		return empty_string;

	/* If there is a current channel, get it's mode */
	if ((chan = get_window_echannel(window_)))
		mode = get_channel_mode(chan, get_window_server(window_));
	if (!mode)
		mode = empty_string;

	/* If this is %+, and there is not a mode, punt. */
	if ((map == 0 || map == 2) && !*mode)
		return empty_string;

	if (map == 0 || map == 1)
	{
	    /* This now gets handled by PRESS() for everything */
	}
	else if (map == 2 || map == 3)
	{
		char *x;
		x = LOCAL_COPY(mode);
		if (!(mode = next_arg(x, &x)))
			mode = empty_string;
	}

	/* Press the mode into the status format. */
	PRESS(mode_format, mode)
	RETURN
}


/*
 * Your user mode for the server in this window.
 */
STATUS_FUNCTION(status_umode)
{
	STATUS_VARS
	char	localbuf[20];

	/*
	 * If we are only on one server and this isnt the current-type 
	 * window, then dont display it here.
	 */
	if (connected_to_server == 1 && !DISPLAY_ON_WINDOW)
		return empty_string;

	/*
	 * Punt if the window isnt connected to a server.
	 */
	if (get_window_server(window_) < 0)
		return empty_string;

	strlcpy(localbuf, get_umode(get_window_server(window_)), sizeof localbuf);
	if (!*localbuf)
		return empty_string;

	PRESS(umode_format, localbuf)
	RETURN
}

/*
 * Figures out whether or not youre a channel operator on the current
 * channel for this window.
 */
STATUS_FUNCTION(status_chanop)
{
	const char	*text;
	const char *chan;

	if (get_window_server(window_) == NOSERV ||
           (!(chan = get_window_echannel(window_))))
		return empty_string;
	
	if (get_channel_oper(chan, get_window_server(window_)) &&
		(text = get_string_var(STATUS_CHANOP_VAR)))
			return text;

	if (get_channel_halfop(chan, get_window_server(window_)) &&
		(text = get_string_var(STATUS_HALFOP_VAR)))
			return text;

	return empty_string;
}

/*
 * are we using SSL conenction?
 */
STATUS_FUNCTION(status_ssl)
{
	const char *text;

	if (get_window_server(window_) != NOSERV && 
		get_server_ssl_enabled(get_window_server(window_)) &&
		(text = get_string_var(STATUS_SSL_ON_VAR)))
			return text;
	else if ((text = get_string_var(STATUS_SSL_OFF_VAR)))
			return text;
	return empty_string;
}


/*
 * Figures out how many lines are being "held" (never been displayed, usually
 * because of hold_mode or scrollback being on) for this window.
 */
STATUS_FUNCTION(status_hold_lines)
{
	STATUS_VARS
	int	num;
	int	interval = get_window_hold_interval(window_);
	int	lines_held;

	if (interval == 0)
		interval = 1;		/* XXX WHAT-ever */

	if (get_window_holding_distance_from_display_ip(window_) > get_window_scrollback_distance_from_display_ip(window_))
		lines_held = get_window_holding_distance_from_display_ip(window_) - get_window_display_lines(window_);
	else
		lines_held = get_window_scrollback_distance_from_display_ip(window_) - get_window_display_lines(window_);

	if (lines_held <= 0)
		return empty_string;

	if ((num = (lines_held / interval) * interval) == 0)
		return empty_string;

	PRESS(hold_lines_format, ltoa(num))
	RETURN
}

/*
 * Figures out what the current channel is for the window.
 */
STATUS_FUNCTION(status_channel)
{
	STATUS_VARS
	const char *chan;
	char 	channel[IRCD_BUFFER_SIZE + 1];
	int	num;

	if (get_window_server(window_) == NOSERV || !channel_format)
		return empty_string;

	if (!(chan = get_window_echannel(window_)))
		return empty_string;

	if (get_int_var(HIDE_PRIVATE_CHANNELS_VAR) && 
	    is_channel_private(chan, get_window_server(window_)))
		strlcpy(channel, "*private*", sizeof channel);
	else
		strlcpy(channel, chan, sizeof channel);

	num = get_int_var(CHANNEL_NAME_WIDTH_VAR);
	if (num > 0 && (int)strlen(channel) > num)
		channel[num] = 0;

	PRESS(channel_format, check_channel_type(channel))
	RETURN
}

/*
 * Figures out if you are a channel voice for the current channel in this
 * window.  For whatever reason, this wont display if youre also a channel
 * operator on the current channel in this window.
 */
STATUS_FUNCTION(status_voice)
{
	const char *text;
	const char *chan;

	if (get_window_server(window_) == NOSERV ||
           (chan = get_window_echannel(window_)) == NULL)
		return empty_string;

	if (get_channel_voice(chan, get_window_server(window_)) &&
	    !get_channel_oper(chan, get_window_server(window_)) &&
	    (text = get_string_var(STATUS_VOICE_VAR)))
		return text;

	return empty_string;
}

/*
 * Displays how much mail we think you have.
 */
STATUS_FUNCTION(status_mail)
{
	STATUS_VARS
	const char *	number;

	/*
	 * The order is important here.  We check to see whether or not
	 * this is a current-type window *FIRST* because most of the time
	 * that will be false, and check_mail() is very expensive; we dont
	 * want to do it if we're going to ignore the result.
	 */
	if (!get_int_var(MAIL_VAR) || !DISPLAY_ON_WINDOW || 
				((number = check_mail()) == 0))
		return empty_string;

	PRESS(mail_format, number)
	RETURN
}

/*
 * Display if "insert mode" is ON for the input line.
 */
STATUS_FUNCTION(status_insert_mode)
{
	const char	*text;

	if (get_int_var(INSERT_MODE_VAR) && DISPLAY_ON_WINDOW &&
	    (text = get_string_var(STATUS_INSERT_VAR)))
		return text;

	return empty_string;
}

/*
 * Displays if "insert mode" is OFF for the input line.
 */
STATUS_FUNCTION(status_overwrite_mode)
{
	const char	*text;

	if (!get_int_var(INSERT_MODE_VAR) && DISPLAY_ON_WINDOW &&
	    (text = get_string_var(STATUS_OVERWRITE_VAR)))
		return text;

	return empty_string;
}

/*
 * Displays if you are AWAY (protocol away) on the current server for
 * the window.
 */
STATUS_FUNCTION(status_away)
{
	const char	*text;

	/*
	 * If we're only on one server, only do this for the
	 * current-type window.
	 */
	if (connected_to_server == 1 && !DISPLAY_ON_WINDOW)
		return empty_string;

	if (get_window_server(window_) != NOSERV && 
	    get_server_away_message(get_window_server(window_)) && 
	    (text = get_string_var(STATUS_AWAY_VAR)))
		return text;

	return empty_string;
}


/*
 * This is a generic status_userX variable.
 */
STATUS_FUNCTION(status_user)
{
	const char *	text;
	int	i;

	/* XXX Ick.  Oh well. */
	struct dummystruct {
		short		map;
		char		key;
		int *		var;
	} lookup[] = {
	{ 0, 'U', &STATUS_USER_VAR }, { 0, 'X', &STATUS_USER1_VAR },
	{ 0, 'Y', &STATUS_USER2_VAR }, { 0, 'Z', &STATUS_USER3_VAR },
	{ 0, '0', &STATUS_USER_VAR }, { 0, '1', &STATUS_USER1_VAR },
	{ 0, '2', &STATUS_USER2_VAR }, { 0, '3', &STATUS_USER3_VAR },
	{ 0, '4', &STATUS_USER4_VAR }, { 0, '5', &STATUS_USER5_VAR },
	{ 0, '6', &STATUS_USER6_VAR }, { 0, '7', &STATUS_USER7_VAR },
	{ 0, '8', &STATUS_USER8_VAR }, { 0, '9', &STATUS_USER9_VAR },
	{ 1, '0', &STATUS_USER10_VAR }, { 1, '1', &STATUS_USER11_VAR },
	{ 1, '2', &STATUS_USER12_VAR }, { 1, '3', &STATUS_USER13_VAR },
	{ 1, '4', &STATUS_USER14_VAR }, { 1, '5', &STATUS_USER15_VAR },
	{ 1, '6', &STATUS_USER16_VAR }, { 1, '7', &STATUS_USER17_VAR },
	{ 1, '8', &STATUS_USER18_VAR }, { 1, '9', &STATUS_USER19_VAR },
	{ 2, '0', &STATUS_USER20_VAR }, { 2, '1', &STATUS_USER21_VAR },
	{ 2, '2', &STATUS_USER22_VAR }, { 2, '3', &STATUS_USER23_VAR },
	{ 2, '4', &STATUS_USER24_VAR }, { 2, '5', &STATUS_USER25_VAR },
	{ 2, '6', &STATUS_USER26_VAR }, { 2, '7', &STATUS_USER27_VAR },
	{ 2, '8', &STATUS_USER28_VAR }, { 2, '9', &STATUS_USER29_VAR },
	{ 3, '0', &STATUS_USER30_VAR }, { 3, '1', &STATUS_USER31_VAR },
	{ 3, '2', &STATUS_USER32_VAR }, { 3, '3', &STATUS_USER33_VAR },
	{ 3, '4', &STATUS_USER34_VAR }, { 3, '5', &STATUS_USER35_VAR },
	{ 3, '6', &STATUS_USER36_VAR }, { 3, '7', &STATUS_USER37_VAR },
	{ 3, '8', &STATUS_USER38_VAR }, { 3, '9', &STATUS_USER39_VAR },
	{ 0, 0, 0 },
	};

	text = NULL;
	for (i = 0; lookup[i].var; i++)
	{
		if (map == lookup[i].map && key == lookup[i].key)
		{
			text = get_string_var(*lookup[i].var);
			break;
		}
	}

	if (text && (DISPLAY_ON_WINDOW || map > 1))
		return text;

	return empty_string;
}

STATUS_FUNCTION(status_hold)
{
	const char *text;

	if (get_window_holding_distance_from_display_ip(window_) > get_window_display_lines(window_) ||
	    get_window_scrollback_distance_from_display_ip(window_) > get_window_display_lines(window_))
		if ((text = get_string_var(STATUS_HOLD_VAR)))
			return text;
	return empty_string;
}

STATUS_FUNCTION(status_holdmode)
{
	const char *text;

	/* If hold mode is on... */
	if (get_window_hold_mode(window_))
	{
	    /* ... and we're not holding anything */
	    if (get_window_holding_distance_from_display_ip(window_) < get_window_display_lines(window_) &&
	        get_window_scrollback_distance_from_display_ip(window_) < get_window_display_lines(window_))
	    {
		if ((text = get_string_var(STATUS_HOLDMODE_VAR)))
			return text;
	    }
	}
	return empty_string;
}

STATUS_FUNCTION(status_oper)
{
	const char *text;

	if (get_window_server(window_) != NOSERV && get_server_operator(get_window_server(window_)) &&
	    (text = get_string_var(STATUS_OPER_VAR)) &&
	    (connected_to_server != 1 || DISPLAY_ON_WINDOW))
		return text;
	else
		return empty_string;
}

STATUS_FUNCTION(status_window)
{
	const char *text;

	if (!(text = get_string_var(STATUS_WINDOW_VAR)))
		text = empty_string;

	switch (map)
	{
		case 0:
			if (get_window_screennum(window_) < 0)
				break;
			if (get_screen_visible_windows(get_window_screennum(window_)) <= 1)
				break;
			/* FALLTHROUGH */
		case 3:
			if (!IS_CURRENT_WINDOW)
				break;
			/* FALLTHROUGH */
		case 2:
			return text;
	}

	return empty_string;
}

STATUS_FUNCTION(status_refnum)
{
	STATUS_VARS
	const char *value;

	if (!(value = get_window_name(window_)))
		value = ltoa(get_window_user_refnum(window_));

	PRESS("%s", value)
	RETURN
}

STATUS_FUNCTION(status_refnum_real)
{
	STATUS_VARS

	PRESS("%s", ltoa(get_window_user_refnum(window_)));
	RETURN
}

STATUS_FUNCTION(status_version)
{
	if (DISPLAY_ON_WINDOW)
		return irc_version; /* XXXX */

	return empty_string;
}


STATUS_FUNCTION(status_null_function)
{
	return empty_string;
}

/*
 * This displays the DCC "Progress Meter", which goes to the window that
 * has level DCC.  OR, if "current_window_level" includes DCC, then this
 * goes to the current window.
 */
STATUS_FUNCTION(status_dcc)
{
	Mask	mask;

	get_window_mask(window_, &mask);

	if ((mask_isset(&current_window_mask, LEVEL_DCC) && 
				IS_CURRENT_WINDOW) ||
	    (mask_isset(&mask, LEVEL_DCC)))
		return DCC_get_current_transfer();

	return empty_string;
}

/*
 * As above, but always return the indicator.
 */
STATUS_FUNCTION(status_dcc_all)
{
	return DCC_get_current_transfer();
}

/*
 * This displays something if the client is in 'cpu saver' mode.
 * A discussion of that mode should be found in irc.c, so i wont get
 * into it here.
 */
STATUS_FUNCTION(status_cpu_saver_mode)
{
	STATUS_VARS

	if (!cpu_saver)
		return empty_string;

	PRESS(cpu_saver_format, "CPU")
	RETURN
}

/*
 * This is a private expando that i use for testing.  But if you want to
 * use it, more power to you!  I reserve the right to change this expando
 * at my whims.  You should not rely on anything specific happening here.
 */
STATUS_FUNCTION(status_position)
{
	static char my_buffer[81];

	snprintf(my_buffer, sizeof my_buffer, "(%d/%d/%d-%d-%d)", 
			get_window_scrolling_distance_from_display_ip(window_),
			get_window_holding_distance_from_display_ip(window_),
			get_window_scrollback_distance_from_display_ip(window_),
			get_window_display_lines(window_),
			get_window_cursor(window_));
	return my_buffer;
}

/*
 * This returns something if this window is currently in scrollback mode.
 * Useful if you sometimes forget!
 */
STATUS_FUNCTION(status_scrollback)
{
	const char *stuff;

	if (get_window_scrollback_top_of_display_exists(window_) &&
	    (stuff = get_string_var(STATUS_SCROLLBACK_VAR)))
		return stuff;
	else
		return empty_string;
}

STATUS_FUNCTION(status_scroll_info)
{
	static char my_buffer[81];

	if (!get_window_scrollback_top_of_display_exists(window_))
		return empty_string;

	snprintf(my_buffer, sizeof my_buffer, " (Scroll: %d of %d)", 
			get_window_scrollback_distance_from_display_ip(window_),
			get_window_display_buffer_size(window_) - 1);
	return my_buffer;
}


STATUS_FUNCTION(status_windowspec)
{
	if (get_window_status(window_)->special)
		return get_window_status(window_)->special;
	else
		return empty_string;
}

STATUS_FUNCTION(status_window_prefix)
{
	if (IS_CURRENT_WINDOW)
	{
		if (get_window_status(window_)->prefix_when_current)
			return get_window_status(window_)->prefix_when_current;
		else if (get_string_var(STATUS_PREFIX_WHEN_CURRENT_VAR))
			return get_string_var(STATUS_PREFIX_WHEN_CURRENT_VAR);
		else
			return empty_string;
	}
	else
	{
		if (get_window_status(window_)->prefix_when_not_current)
			return get_window_status(window_)->prefix_when_not_current;
		else if (get_string_var(STATUS_PREFIX_WHEN_NOT_CURRENT_VAR))
			return get_string_var(STATUS_PREFIX_WHEN_NOT_CURRENT_VAR);
		else
			return empty_string;
	}
}

STATUS_FUNCTION(status_percent)
{
	static	char	percent[] = "%";
	return	percent;
}

STATUS_FUNCTION(status_test)
{
	static	char	retval[] = "TEST";
	return retval;
}

/*
 * This returns something if this window is currently in scrollback mode.
 * Useful if you sometimes forget!
 */
STATUS_FUNCTION(status_swappable)
{
	const char *stuff;

	if (!get_window_swappable(window_) &&
	    (stuff = get_string_var(STATUS_NOSWAP_VAR)))
		return stuff;
	else
		return empty_string;
}

STATUS_FUNCTION(status_activity)
{
	const char *format, *data;
	char *result;
	static char retval[80];

	if (!get_window_current_activity(window_))
		return empty_string;

	format = get_window_current_activity_format(window_);
	data = get_window_current_activity_data(window_);

	result = expand_alias(format, data);
	strlcpy(retval, result, sizeof(retval));
	new_free(&result);
	return retval;
}

BUILT_IN_FUNCTION(function_status_oneoff, input)
{
	Status *s;
	char *	windesc;
	char	*retval;
	int	window;

	GET_FUNC_ARG(windesc, input);

	if ((window = lookup_window(windesc)) < 1)
		RETURN_EMPTY;

	s = new_status();
	malloc_strcpy(&s->line[0].raw, input);

	compile_status(window, s);
	make_status(window, s);

	retval = denormalize_string(s->line[0].result);
	destroy_status(&s);

	RETURN_MSTR(retval);
}

STATUS_FUNCTION(status_sequence_point)
{
	STATUS_VARS
	char resultstr[100];

	if (x_debug & DEBUG_SEQUENCE_POINTS)
	{
		snprintf(resultstr, sizeof(resultstr), UINTMAX_FORMAT, sequence_point);
		PRESS(sp_format, resultstr);
		RETURN
	}
	else
		return empty_string;
}


