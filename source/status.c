/*
 * status.c: handles the status line updating, etc for IRCII 
 *
 * Written By Michael Sandrof
 * Extensive modifications by Jake Khuon and others (epic software labs)
 *
 * Copyright(c) 1990  Michael Sandrof
 * Copyright 1997, 1998 EPIC Software Labs
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 */

#define __need_putchar_x__
#include "irc.h"
#include "dcc.h"
#include "term.h"
#include "status.h"
#include "server.h"
#include "vars.h"
#include "hook.h"
#include "input.h"
#include "commands.h"
#include "window.h"
#include "screen.h"
#include "mail.h"
#include "output.h"
#include "names.h"
#include "ircaux.h"
#include "alias.h"
#ifdef HAVE_SSL
#include "ssl.h"
#endif

#ifdef Char
#undef Char
#endif
#define Char const char

/*
 * Maximum number of "%" expressions in a status line format.  If you change
 * this number, you must manually change the sprintf() in make_status 
 */
#define STATUS_FUNCTION(x) static Char * x (Window *window, int map, int key)
#define MAX_FUNCTIONS 40
#define MAX_STATUS_USER 39

STATUS_FUNCTION(status_nickname);
STATUS_FUNCTION(status_query_nick);
STATUS_FUNCTION(status_right_justify);
STATUS_FUNCTION(status_chanop);
STATUS_FUNCTION(status_ssl);
STATUS_FUNCTION(status_channel);
STATUS_FUNCTION(status_server);
STATUS_FUNCTION(status_mode);
STATUS_FUNCTION(status_umode);
STATUS_FUNCTION(status_insert_mode);
STATUS_FUNCTION(status_overwrite_mode);
STATUS_FUNCTION(status_away);
STATUS_FUNCTION(status_oper);
STATUS_FUNCTION(status_user);
STATUS_FUNCTION(status_dcc);
STATUS_FUNCTION(status_hold);
STATUS_FUNCTION(status_version);
STATUS_FUNCTION(status_clock);
STATUS_FUNCTION(status_hold_lines);
STATUS_FUNCTION(status_window);
STATUS_FUNCTION(status_mail);
STATUS_FUNCTION(status_refnum);
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
static	char	*notify_format 		= (char *) 0;

Status	main_status;
int	main_status_init = 0;

/*
 * This is the list of possible expandos.  Note that you should not use
 * the '{' character, as it would be confusing.  It is already used for 
 * specifying the map.
 */
struct status_formats {
	int	map;
	char 	key;
	Char	*(*callback_function)(Window *, int, int);
	char	**format_var;
	int	format_set;
};
struct status_formats status_expandos[] = {
{ 0, 'A', status_away,          NULL, 			-1 },
{ 0, 'B', status_hold_lines,    &hold_lines_format,	STATUS_HOLD_LINES_VAR },
{ 0, 'C', status_channel,       &channel_format,	STATUS_CHANNEL_VAR },
{ 0, 'D', status_dcc, 	        NULL, 			-1 },
{ 0, 'F', status_notify_windows,&notify_format,		STATUS_NOTIFY_VAR },
{ 0, 'H', status_hold,		NULL,			-1 },
{ 0, 'I', status_insert_mode,   NULL,			-1 },
{ 0, 'K', status_scrollback,	NULL,			-1 },
{ 0, 'L', status_cpu_saver_mode,&cpu_saver_format,	STATUS_CPU_SAVER_VAR },
{ 0, 'M', status_mail,		&mail_format,		STATUS_MAIL_VAR },
{ 0, 'N', status_nickname,	&nick_format,		STATUS_NICK_VAR },
{ 0, 'O', status_overwrite_mode,NULL,			-1 },
{ 0, 'P', status_position,      NULL,			-1 },
{ 0, 'Q', status_query_nick,    &query_format,		STATUS_QUERY_VAR },
{ 0, 'R', status_refnum,        NULL, 			-1 },
{ 0, 'S', status_server,        &server_format,     	STATUS_SERVER_VAR },
{ 0, 'T', status_clock,         &clock_format,      	STATUS_CLOCK_VAR },
{ 0, 'U', status_user,		NULL, 			-1 },
{ 0, 'V', status_version,	NULL, 			-1 },
{ 0, 'W', status_window,	NULL, 			-1 },
{ 0, 'X', status_user,		NULL, 			-1 },
{ 0, 'Y', status_user,		NULL, 			-1 },
{ 0, 'Z', status_user,		NULL, 			-1 },
{ 0, '#', status_umode,		&umode_format,	     	STATUS_UMODE_VAR },
{ 0, '%', status_percent,	NULL, 			-1 },
{ 0, '*', status_oper,		NULL, 			-1 },
{ 0, '+', status_mode,		&mode_format,       	STATUS_MODE_VAR },
{ 0, '.', status_windowspec,	NULL, 			-1 },
{ 0, '=', status_voice,		NULL, 			-1 },
{ 0, '>', status_right_justify,	NULL, 			-1 },
{ 0, '@', status_chanop,	NULL, 			-1 },
{ 0, '|', status_ssl,		NULL,			-1 },
{ 0, '0', status_user,		NULL, 			-1 },
{ 0, '1', status_user,		NULL, 			-1 },
{ 0, '2', status_user,		NULL, 			-1 },
{ 0, '3', status_user,		NULL, 			-1 },
{ 0, '4', status_user,		NULL, 			-1 },
{ 0, '5', status_user,		NULL, 			-1 },
{ 0, '6', status_user,		NULL, 			-1 },
{ 0, '7', status_user,		NULL, 			-1 },
{ 0, '8', status_user,		NULL, 			-1 },
{ 0, '9', status_user,		NULL, 			-1 },
{ 1, '0', status_user,		NULL, 			-1 },
{ 1, '1', status_user,		NULL, 			-1 },
{ 1, '2', status_user,		NULL, 			-1 },
{ 1, '3', status_user,		NULL, 			-1 },
{ 1, '4', status_user,		NULL, 			-1 },
{ 1, '5', status_user,		NULL, 			-1 },
{ 1, '6', status_user,		NULL, 			-1 },
{ 1, '7', status_user,		NULL, 			-1 },
{ 1, '8', status_user,		NULL, 			-1 },
{ 1, '9', status_user,		NULL, 			-1 },
{ 1, 'F', status_notify_windows,&notify_format,		STATUS_NOTIFY_VAR },
{ 1, 'K', status_scroll_info,	NULL,			-1 },
{ 1, 'S', status_server,        &server_format,     	STATUS_SERVER_VAR },
{ 1, 'T', status_test,		NULL,			-1 },
{ 2, '0', status_user,	 	NULL, 			-1 },
{ 2, '1', status_user,	 	NULL, 			-1 },
{ 2, '2', status_user,	 	NULL, 			-1 },
{ 2, '3', status_user,	 	NULL, 			-1 },
{ 2, '4', status_user,		NULL, 			-1 },
{ 2, '5', status_user,	 	NULL, 			-1 },
{ 2, '6', status_user,	 	NULL, 			-1 },
{ 2, '7', status_user,	 	NULL,			-1 },
{ 2, '8', status_user,	 	NULL, 			-1 },
{ 2, '9', status_user,	 	NULL, 			-1 },
{ 2, 'S', status_server,        &server_format,     	STATUS_SERVER_VAR },
{ 3, '0', status_user,	 	NULL, 			-1 },
{ 3, '1', status_user,	 	NULL, 			-1 },
{ 3, '2', status_user,	 	NULL, 			-1 },
{ 3, '3', status_user,	 	NULL, 			-1 },
{ 3, '4', status_user,	 	NULL, 			-1 },
{ 3, '5', status_user,	 	NULL, 			-1 },
{ 3, '6', status_user,	 	NULL, 			-1 },
{ 3, '7', status_user,	 	NULL, 			-1 },
{ 3, '8', status_user,	 	NULL, 			-1 },
{ 3, '9', status_user,	 	NULL, 			-1 }
};
#define NUMBER_OF_EXPANDOS (sizeof(status_expandos) / sizeof(struct status_formats))

/*
 * convert_sub_format: This is used to convert the formats of the
 * sub-portions of the status line to a format statement specially designed
 * for that sub-portions.  convert_sub_format looks for a single occurence of
 * %c (where c is passed to the function). When found, it is replaced by "%s"
 * for use is a sprintf.  All other occurences of % followed by any other
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
	return m_strdup(buffer);
}


/*
 * This walks a raw format string and parses out any expandos that it finds.
 * An expando is handled by pre-fetching any string variable that is used
 * by the callback function, the callback function is registered, and a
 * %s format is put in the sprintf()-able return value (stored in buffer).
 * All other characters are copied as-is to the return value.
 */
void	build_status_format (Status *s, int k)
{
	char	buffer[BIG_BUFFER_SIZE + 1];
	int	cp;
	int	map;
	char	key;
	int	i;
	Char	*raw = s->line[k].raw;
	char	*format = buffer;

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
			if (status_expandos[i].format_set != -1)
				*(status_expandos[i].format_var) = 
					convert_sub_format(get_string_var(status_expandos[i].format_set), key);

			*format++ = '%';
			*format++ = 's';

			s->line[k].func[cp] = 
				status_expandos[i].callback_function;
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
 * This function does two things:
 * 1) Rebuilds the status_func[] tables for each of the three possible
 *    status_format's (single, lower double, upper double)
 * 2) Causes the status bars to be redrawn immediately.
 */
void	rebuild_a_status (Window *w)
{
	int 	i,
		k;
	Status	*s;

	if (w)
		s = &w->status;
	else
		s = &main_status;

	for (k = 0; k < 3; k++)
	{
		new_free((char **)&s->line[k].format);
		s->line[k].count = 0;

		/*
		 * If we have an overriding status_format, then we parse
		 * that out.
		 */
		if (w && s->line[k].raw)
			build_status_format(s, k);

		/*
		 * Otherwise, If this is for a window, just copy the essential
		 * information over from the main status lines.
		 */
		else if (w)
		{
			s->line[k].format = m_strdup(main_status.line[k].format);
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
			   s->line[k].raw = get_string_var(STATUS_FORMAT_VAR);
			else if (k == 1)
			   s->line[k].raw = get_string_var(STATUS_FORMAT1_VAR);
			else  /* (k == 2) */
			   s->line[k].raw = get_string_var(STATUS_FORMAT2_VAR);

			build_status_format(s, k);
		}
	}
}

void	init_status	(void)
{
	int	i, k;

	main_status.double_status = 0;
	main_status.special = 0;
	for (i = 0; i < 3; i++)
	{
		main_status.line[i].raw = NULL;
		main_status.line[i].format = NULL;
		main_status.line[i].count = 0;
		main_status.line[i].result = NULL;
		for (k = 0; k < MAX_FUNCTIONS; k++)
		{
			main_status.line[i].func[k] = NULL;
			main_status.line[i].map[k] = 0;
			main_status.line[i].key[k] = 0;
		}
	}
	main_status_init = 1;
}

void	build_status	(char *unused)
{
	Window 	*w = NULL;

	if (!unused && !main_status_init)
		init_status();

	rebuild_a_status(w);
	while (traverse_all_windows(&w))
		rebuild_a_status(w);

	update_all_status();
}


/*
 * This just sucked beyond words.  I was always planning on rewriting this,
 * but the crecendo of complaints with regards to this just got to be too 
 * irritating, so i fixed it early.
 */
void	make_status (Window *window)
{
	int	status_line;
	u_char	buffer	    [BIG_BUFFER_SIZE + 1];
	u_char	lhs_buffer  [BIG_BUFFER_SIZE + 1];
	u_char	rhs_buffer  [BIG_BUFFER_SIZE + 1];
	Char	*func_value [MAX_FUNCTIONS];
	u_char	*ptr;

	for (status_line = 0; status_line < window->status.double_status + 1; status_line++)
	{
		u_char	lhs_fillchar[6],
			rhs_fillchar[6],
			*fillchar = lhs_fillchar,
			*lhp = lhs_buffer,
			*rhp = rhs_buffer,
			*cp,
			*start_rhs = 0,
			*str;
		int	in_rhs = 0,
			pr_lhs = 0,
			pr_rhs = 0,
			line,
			*prc = &pr_lhs, 
			i;

		fillchar[0] = fillchar[1] = 0;

		/*
		 * If status line gets to one, then that means that
		 * window->double_status is not zero.  That means that
		 * the status line we're working on is STATUS2.
		 */
		if (status_line)
			line = 2;

		/*
		 * If status_line is zero, and window->double_status is
		 * not zero (double status line is on) then we're working
		 * on STATUS1.
		 */
		else if (window->status.double_status)
			line = 1;

		/*
		 * So status_line is zero and window->double_status is zero.
		 * So we're working on STATUS (0).
		 */
		else
			line = 0;


		/*
		 * Sanity check:  If the status format doesnt exist, dont do
		 * anything for it.
		 */
		if (!window->status.line[line].format)
			continue;

		/*
		 * Run each of the status-generating functions from the the
		 * status list.  Note that the retval of the functions is no
		 * longer malloc()ed.  This saves 40-some odd malloc/free sets
		 * each time the status bar is updated, which is non-trivial.
		 */
		for (i = 0; i < MAX_FUNCTIONS; i++)
		{
			if (window->status.line[line].func[i] == NULL)
				panic("status callback null.  Window [%d], line [%d], function [%d]", window->refnum, line, i);
			func_value[i] = window->status.line[line].func[i]
				(window, window->status.line[line].map[i],
				 window->status.line[line].key[i]);
		}

		/*
		 * If the REVERSE_STATUS_LINE var is on, then put a reverse
		 * character in the first position (itll get translated to
		 * the tcap code in the output code.
		 */
		if (get_int_var(REVERSE_STATUS_LINE_VAR))
			*buffer = REV_TOG , str = buffer + 1;
		else
			str = buffer;

		/*
		 * Now press the status line into "buffer".  The magic about
		 * setting status_format is handled elsewhere.
		 */
		snprintf(str, BIG_BUFFER_SIZE - 1, window->status.line[line].format,
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
			int  af = 0;
			Window *old = current_window;

			current_window = window;
			str = expand_alias(buffer, empty_string, &af, NULL);
			current_window = old;
			strmcpy(buffer, str, BIG_BUFFER_SIZE);
			new_free(&str);
		}


		/*
		 * This converts away any ansi codes in the status line
		 * in accordance with the currenet settings.  This leaves us
		 * with nothing but logical characters, which are then easy
		 * to count. :-)
		 */
		str = normalize_string(buffer, 3);

		/*
		 * Count out the characters.
		 * Walk the entire string, looking for nonprintable
		 * characters.  We count all the printable characters
		 * on both sides of the %> tag.
		 */
		ptr = str;
		cp = lhp;
		lhs_buffer[0] = rhs_buffer[0] = 0;

		while (*ptr)
		{
			/*
			 * The FIRST such %> tag is used.
			 * Using multiple %>s is bogus.
			 */
			if (*ptr == '\f' && start_rhs == NULL)
			{
				ptr++;
				start_rhs = ptr;
				fillchar = rhs_fillchar;
				in_rhs = 1;
				*cp = 0;
				cp = rhp;
				prc = &pr_rhs;
			}

                        /*
                         * Skip over attribute changes, not useful.
                         */
                        else if (*ptr == '\006')
                        {
                                /* Copy the next 5 values */
                                *cp++ = *ptr++;
                                *cp++ = *ptr++;
                                *cp++ = *ptr++;
                                *cp++ = *ptr++;
                                *cp++ = *ptr++;
                        }

			/*
			 * XXXXX This is a bletcherous hack.
			 * If i knew what was good for me id not do this.
			 */
			else if (*ptr == 9)	/* TAB */
			{
				fillchar[0] = ' ';
				fillchar[1] = 0;
				do
					*cp++ = ' ';
				while (++(*prc) % 8);
				ptr++;
			}

			/*
			 * So it is a printable character.
			 */
			else
			{
				*prc += 1;
				fillchar[0] = *cp++ = *ptr++;
				fillchar[1] = 0;
			}

			/*
			 * Dont allow more than CO printable characters
			 */
			if (pr_lhs + pr_rhs >= window->screen->co)
			{
				*cp = 0;
				break;
			}
		}
		*cp = 0;

		/* What will we be filling with? */
		if (get_int_var(STATUS_NO_REPEAT_VAR))
		{
			lhs_fillchar[0] = ' ';
			lhs_fillchar[1] = 0;
			rhs_fillchar[0] = ' ';
			rhs_fillchar[1] = 0;
		}

		/*
		 * Now if we have a rhs, then we have to adjust it.
		 */
		if (start_rhs)
		{
			int numf = 0;

			numf = window->screen->co - pr_lhs - pr_rhs -1;
			while (numf-- >= 0)
				strmcat(lhs_buffer, lhs_fillchar, 
						BIG_BUFFER_SIZE);
		}

		/*
		 * No rhs?  If the user wants us to pad it out, do so.
		 */
		else if (get_int_var(FULL_STATUS_LINE_VAR))
		{
			int chars = window->screen->co - pr_lhs - 1;

			while (chars-- >= 0)
				strmcat(lhs_buffer, lhs_fillchar, 
						BIG_BUFFER_SIZE);
		}

		strlcpy(buffer, lhs_buffer, BIG_BUFFER_SIZE -6);
		strlcat(buffer, rhs_buffer, BIG_BUFFER_SIZE -6);
		strlcat(buffer, all_off(), BIG_BUFFER_SIZE);
		new_free(&str);

		/*
		 * Ends up that BitchX always throws this hook and
		 * people seem to like having this thrown in standard
		 * mode, so i'll go along with that.
		 */
		do_hook(STATUS_UPDATE_LIST, "%d %d %s", 
			window->refnum, 
			status_line, 
			buffer);

		if (dumb_mode)
			continue;

		/*
		 * Update the status line on the screen.
		 * First check to see if it has changed
		 */
		if (!window->status.line[status_line].result ||
			strcmp(buffer, window->status.line[status_line].result))
		{
			/*
			 * Roll the new back onto the old
			 */
			malloc_strcpy(&window->status.line[status_line].result,
					buffer);

			/*
			 * Output the status line to the screen
			 */
			if (!dumb_mode)
			{
				output_screen = window->screen;
				term_move_cursor(0, window->bottom + status_line);
				output_with_count(buffer, 1, 1);
				cursor_in_display(window);
			}
		}
	}

	cursor_to_input();
}


/* Some useful macros */
/*
 * This is used to get the current window on a window's screen
 */
#define CURRENT_WINDOW window->screen->current_window

/*
 * This tests to see if the window IS the current window on its screen
 */
#define IS_CURRENT_WINDOW (window->screen->current_window == window)

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
 * These are the functions that all of the status expandoes invoke
 */


/*
 * This is your current nickname in the window.
 */
STATUS_FUNCTION(status_nickname)
{
static	char	my_buffer[64];

	snprintf(my_buffer, 63, nick_format, 
			get_server_nickname(window->server));
	return my_buffer;
}

/*
 * This displays the server that the window is connected to.
 */
STATUS_FUNCTION(status_server)
{
	char	*rest;
const	char	*n;
	char	*name;
	char	*next;
static	char	my_buffer[64];
	size_t	len;

#ifdef OLD_STATUS_S_EXPANDO_BEHAVIOR
	/*
	 * If there is only one server, dont bother telling the user
	 * what it is.
	 */
	if (connected_to_server == 1 && map == 0)
		return empty_string;
#endif

	/*
	 * If this window isnt connected to a server, say so.
	 */
	if (window->server == -1)
		return "No Server";

	/*
	 * If the user doesnt want this expando, dont force it.
	 */
	if (!server_format)
		return empty_string;

	/* Figure out what server this window is on */
	n = get_server_name(window->server);
	if (map == 2)
	{
		snprintf(my_buffer, 63, server_format, n);
		return my_buffer;
	}

	name = LOCAL_COPY(n);

	/*
	 * If the first segment before the first dot is a number,
	 * then its an ip address, and use the whole thing.
	 */
	if (strtoul(name, &next, 10) && *next == '.')
	{
		snprintf(my_buffer, 63, server_format, name);
		return my_buffer;
	}

	/*
	 * Get the stuff to the left of the first dot.
	 */
	if (!(rest = strchr(name, '.')))
	{
		snprintf(my_buffer, 63, server_format, name);
		return my_buffer;
	}

	/*
	 * If the first segment is 'irc', thats not terribly
	 * helpful, so get the next segment.
	 */
	if (!strncmp(name, "irc", 3))
	{
		name = rest + 1;
		if (!(rest = strchr(name + 1, '.')))
			rest = name + strlen(name);
	}

	/*
	 * If the name of the server is > 60 chars, crop it back to 60.
	 */
	if ((len = rest - name) > 60)
		len = 60;

	/*
	 * Plop the server into the server_format and return it.
	 */
	name[len] = 0;
	snprintf(my_buffer, 63, server_format, name);
	return my_buffer;
}

/*
 * This displays any nicks that you may have as your current query in the
 * given window.
 */
STATUS_FUNCTION(status_query_nick)
{
	static char my_buffer[BIG_BUFFER_SIZE + 1];

	if (window->query_nick && query_format)
	{
		snprintf(my_buffer, BIG_BUFFER_SIZE, 
				query_format, window->query_nick);
		return my_buffer;
	}

	return empty_string;
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
	int	doneone = 0;
	char	buf2[81];
static	char	my_buffer[81];

	/*
	 * This only goes to a current-type window.
	 */
	if (!DISPLAY_ON_WINDOW)
		return empty_string;

	*my_buffer = 0;
	*buf2 = 0;

	/*
	 * Look for any notifying windows that have had some output since 
	 * they have been hidden and collect their refnums.
	 */
	window = NULL;
	while (traverse_all_windows(&window))
	{
		if (window->miscflags & WINDOW_NOTIFIED)
		{
			if (doneone++)
				strmcat(buf2, ",", 80);
			strmcat(buf2, (map == 1 && window->name) ? 
					window->name :
					ltoa(window->refnum), 80);
		}
	}

	/*
	 * Only do the sprintf if there are windows to process.
	 */
	if (doneone && notify_format)
		snprintf(my_buffer, 80, notify_format, buf2);

	return my_buffer;
}

/*
 * Displays what time it is on the current-type window
 */
STATUS_FUNCTION(status_clock)
{
	static	char	my_buffer[81];

	if (get_int_var(CLOCK_VAR) && clock_format && DISPLAY_ON_WINDOW)
		snprintf(my_buffer, 80, clock_format, update_clock(GET_TIME));
	else
		*my_buffer = 0;

	return my_buffer;
}

/*
 * The current channel mode for the current channel (if any)
 */
STATUS_FUNCTION(status_mode)
{
	char	*mode = (char *) 0;
static  char    my_buffer[81];

	*my_buffer = 0;
	if (window->current_channel && window->server != -1)
	{
                mode = get_channel_mode(window->current_channel,window->server);
		if (mode && *mode && mode_format)
		{
			/*
			 * This gross hack is required to make sure that the 
			 * channel key doesnt accidentally contain anything 
			 * dangerous...
			 */
			if (get_int_var(STATUS_DOES_EXPANDOS_VAR))
			{
				char *mode2 = alloca(strlen(mode) * 2 + 1);
				double_quote(mode, "$", mode2);
				mode = mode2;
			}

			snprintf(my_buffer, 80, mode_format, mode);
		}
	}
	return my_buffer;
}


/*
 * Your user mode for the server in this window.
 */
STATUS_FUNCTION(status_umode)
{
	char	localbuf[20];
static	char	my_buffer[81];

	/*
	 * If we are only on one server and this isnt the current-type 
	 * window, then dont display it here.
	 */
	if (connected_to_server == 1 && !DISPLAY_ON_WINDOW)
		return empty_string;

	/*
	 * Punt if the window isnt connected to a server.
	 */
	if (window->server < 0)
		return empty_string;

	strmcpy(localbuf, get_umode(window->server), 19);
	if (!*localbuf)
		return empty_string;

	snprintf(my_buffer, 80, umode_format, localbuf);
	return my_buffer;
}

/*
 * Figures out whether or not youre a channel operator on the current
 * channel for this window.
 */
STATUS_FUNCTION(status_chanop)
{
	char	*text;

	if (!window->current_channel && window->server == -1)
		return empty_string;
	
	if (get_channel_oper(window->current_channel, window->server) &&
		(text = get_string_var(STATUS_CHANOP_VAR)))
			return text;

	if (get_channel_halfop(window->current_channel, window->server) &&
		(text = get_string_var(STATUS_HALFOP_VAR)))
			return text;

	return empty_string;
}

/*
 * are we using SSL conenction?
 */
STATUS_FUNCTION(status_ssl)
{
#ifdef HAVE_SSL
	char *text;

	if (window->server != -1 && get_server_isssl(window->server) &&
		(text = get_string_var(STATUS_SSL_ON_VAR)))
			return text;
	else if (text = get_string_var(STATUS_SSL_ON_VAR))
			return text;
#endif
	return empty_string;
}


/*
 * Figures out how many lines are being "held" (never been displayed, usually
 * because of hold_mode or scrollback being on) for this window.
 */
STATUS_FUNCTION(status_hold_lines)
{
	int	num;
static	char	my_buffer[81];
	int	interval = window->hold_interval;

	if (interval == 0)
		interval = 1;		/* XXX WHAT-ever */

	if ((num = (window->lines_held / interval) * interval))
	{
		snprintf(my_buffer, 80, hold_lines_format, ltoa(num));
		return my_buffer;
	}

	return empty_string;
}

/*
 * Figures out what the current channel is for the window.
 */
STATUS_FUNCTION(status_channel)
{
	char 	channel[IRCD_BUFFER_SIZE + 1];
static	char	my_buffer[IRCD_BUFFER_SIZE + 1];
	int	num;

	if (!window->current_channel || window->server == -1 || !channel_format)
		return empty_string;

	if (get_int_var(HIDE_PRIVATE_CHANNELS_VAR) && 
	    is_channel_private(window->current_channel, window->server))
		strmcpy(channel, "*private*", IRCD_BUFFER_SIZE);
	else
		strmcpy(channel, window->current_channel, IRCD_BUFFER_SIZE);

	num = get_int_var(CHANNEL_NAME_WIDTH_VAR);
	if (num > 0 && strlen(channel) > num)
		channel[num] = 0;

	snprintf(my_buffer, IRCD_BUFFER_SIZE, 
			channel_format, check_channel_type(channel));
	return my_buffer;
}

/*
 * Figures out if you are a channel voice for the current channel in this
 * window.  For whatever reason, this wont display if youre also a channel
 * operator on the current channel in this window.
 */
STATUS_FUNCTION(status_voice)
{
	char *text;

	if (window->current_channel && window->server != -1 &&
	    get_channel_voice(window->current_channel, window->server) &&
	    !get_channel_oper(window->current_channel, window->server) &&
	    (text = get_string_var(STATUS_VOICE_VAR)))
		return text;

	return empty_string;
}

/*
 * Displays how much mail we think you have.
 */
STATUS_FUNCTION(status_mail)
{
	char	*number;
static	char	my_buffer[81];

	/*
	 * The order is important here.  We check to see whether or not
	 * this is a current-type window *FIRST* because most of the time
	 * that will be false, and check_mail() is very expensive; we dont
	 * want to do it if we're going to ignore the result.
	 */
	if (get_int_var(MAIL_VAR) && mail_format && 
	    DISPLAY_ON_WINDOW && (number = check_mail()))
	{
		snprintf(my_buffer, 80, mail_format, number);
		return my_buffer;
	}

	return empty_string;
}

/*
 * Display if "insert mode" is ON for the input line.
 */
STATUS_FUNCTION(status_insert_mode)
{
	char	*text;

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
	char	*text;

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
	char	*text;

	/*
	 * If we're only on one server, only do this for the
	 * current-type window.
	 */
	if (connected_to_server == 1 && !DISPLAY_ON_WINDOW)
		return empty_string;

	if (window->server != -1 && 
	    get_server_away(window->server) && 
	    (text = get_string_var(STATUS_AWAY_VAR)))
		return text;

	return empty_string;
}


/*
 * This is a generic status_userX variable.  
 */
STATUS_FUNCTION(status_user)
{
	char *	text;
	int	i;

	/* XXX Ick.  Oh well. */
	struct dummystruct {
		int		map;
		char		key;
		enum VAR_TYPES	var;
	} lookup[] = {
	{ 0, 'U', STATUS_USER0_VAR }, { 0, 'X', STATUS_USER1_VAR },
	{ 0, 'Y', STATUS_USER2_VAR }, { 0, 'Z', STATUS_USER3_VAR },
	{ 0, '0', STATUS_USER0_VAR }, { 0, '1', STATUS_USER1_VAR },
	{ 0, '2', STATUS_USER2_VAR }, { 0, '3', STATUS_USER3_VAR },
	{ 0, '4', STATUS_USER4_VAR }, { 0, '5', STATUS_USER5_VAR },
	{ 0, '6', STATUS_USER6_VAR }, { 0, '7', STATUS_USER7_VAR },
	{ 0, '8', STATUS_USER8_VAR }, { 0, '9', STATUS_USER9_VAR },
	{ 1, '0', STATUS_USER10_VAR }, { 1, '1', STATUS_USER11_VAR },
	{ 1, '2', STATUS_USER12_VAR }, { 1, '3', STATUS_USER13_VAR },
	{ 1, '4', STATUS_USER14_VAR }, { 1, '5', STATUS_USER15_VAR },
	{ 1, '6', STATUS_USER16_VAR }, { 1, '7', STATUS_USER17_VAR },
	{ 1, '8', STATUS_USER18_VAR }, { 1, '9', STATUS_USER19_VAR },
	{ 2, '0', STATUS_USER20_VAR }, { 2, '1', STATUS_USER21_VAR },
	{ 2, '2', STATUS_USER22_VAR }, { 2, '3', STATUS_USER23_VAR },
	{ 2, '4', STATUS_USER24_VAR }, { 2, '5', STATUS_USER25_VAR },
	{ 2, '6', STATUS_USER26_VAR }, { 2, '7', STATUS_USER27_VAR },
	{ 2, '8', STATUS_USER28_VAR }, { 2, '9', STATUS_USER29_VAR },
	{ 3, '0', STATUS_USER30_VAR }, { 3, '1', STATUS_USER31_VAR },
	{ 3, '2', STATUS_USER32_VAR }, { 3, '3', STATUS_USER33_VAR },
	{ 3, '4', STATUS_USER34_VAR }, { 3, '5', STATUS_USER35_VAR },
	{ 3, '6', STATUS_USER36_VAR }, { 3, '7', STATUS_USER37_VAR },
	{ 3, '8', STATUS_USER38_VAR }, { 3, '9', STATUS_USER39_VAR },
	{ 0, 0, 0 },
	};

	text = NULL;
	for (i = 0; lookup[i].var; i++)
	{
		if (map == lookup[i].map && key == lookup[i].key)
		{
			text = get_string_var(lookup[i].var);
			break;
		}
	}

	if (text && (DISPLAY_ON_WINDOW || map > 1))
		return text;

	return empty_string;
}

STATUS_FUNCTION(status_hold)
{
	char *text;

	if (window->holding_something && 
	    (text = get_string_var(STATUS_HOLD_VAR)))
		return text;
	else
		return empty_string;
}

STATUS_FUNCTION(status_oper)
{
	char *text;

	if (window->server != -1 && get_server_operator(window->server) &&
	    (text = get_string_var(STATUS_OPER_VAR)) &&
	    (connected_to_server != 1 || DISPLAY_ON_WINDOW))
		return text;
	else
		return empty_string;
}

STATUS_FUNCTION(status_window)
{
	char *text;

	if ((number_of_windows_on_screen(window) > 1) && 
	    IS_CURRENT_WINDOW && (text = get_string_var(STATUS_WINDOW_VAR)))
		return text;
	else
		return empty_string;
}

STATUS_FUNCTION(status_refnum)
{
	static char my_buffer[81];

	strlcpy(my_buffer, window->name ? window->name 
					: ltoa(window->refnum), 80);
	return my_buffer;
}

STATUS_FUNCTION(status_version)
{
	if (DISPLAY_ON_WINDOW)
		return (char *)irc_version; /* XXXX */

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
	if ((current_window_level & LOG_DCC && IS_CURRENT_WINDOW) ||
			(window->window_level & LOG_DCC))
		return DCC_get_current_transfer();

	return empty_string;
}

/*
 * This displays something if the client is in 'cpu saver' mode.
 * A discussion of that mode should be found in irc.c, so i wont get
 * into it here.
 */
STATUS_FUNCTION(status_cpu_saver_mode)
{
	static char my_buffer[81];

	if (cpu_saver && cpu_saver_format)
	{
		snprintf(my_buffer, 80, cpu_saver_format, "CPU");
		return my_buffer;
	}

	return empty_string;
}

/*
 * This is a private expando that i use for testing.  But if you want to
 * use it, more power to you!  I reserve the right to change this expando
 * at my whims.  You should not rely on anything specific happening here.
 */
STATUS_FUNCTION(status_position)
{
	static char my_buffer[81];

	snprintf(my_buffer, 80, "(%d-%d)", 
			window->screen->input_line,
			window->screen->input_cursor);
#if 0
			window->lines_scrolled_back,
			window->distance_from_display);
#endif
	return my_buffer;
}

/*
 * This returns something if this window is currently in scrollback mode.
 * Useful if you sometimes forget!
 */
STATUS_FUNCTION(status_scrollback)
{
	char *stuff;

	if (window->scrollback_point && 
	    (stuff = get_string_var(STATUS_SCROLLBACK_VAR)))
		return stuff;
	else
		return empty_string;
}

STATUS_FUNCTION(status_scroll_info)
{
	static char my_buffer[81];

	if (window->scrollback_point)
	{
		snprintf(my_buffer, 80, " (Scroll: %d of %d)", 
				window->distance_from_display,
				window->display_buffer_size - 1);
	}
	else
		*my_buffer = 0;

	return my_buffer;
}


STATUS_FUNCTION(status_windowspec)
{
	static char my_buffer[81];

	if (window->status.special)
		strmcpy(my_buffer, window->status.special, 80);
	else
		*my_buffer = 0;

	return my_buffer;
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

