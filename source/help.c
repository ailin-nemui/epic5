/* $EPIC: help.c,v 1.6 2003/01/26 03:25:38 jnelson Exp $ */
/*
 * help.c: handles the help stuff for irc 
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright © 1995, 2002 EPIC Software Labs.
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
 * This has been replaced almost entirely from the original by Michael
 * Sandrof in order to fit in with the multiple screen code.
 *
 * ugh, this wasn't easy to do, but I got there, after working out what
 * had been changed and why, by myself - phone, October 1992.
 *
 * And when I started getting /window create working, I discovered new
 * bugs, and there has been a few more major changes in here again.
 * It is invalid to call help from more than one screen, at the moment,
 * because there is to much to keep track of - phone, jan 1993.
 */

#if 0
static char rcsid[] = "@(#)$Id: help.c,v 1.6 2003/01/26 03:25:38 jnelson Exp $";
#endif

#include "irc.h"
#include "help.h"
#include "input.h"
#include "ircaux.h"
#include "hook.h"
#include "output.h"
#include "screen.h"
#include "server.h"
#include "term.h"
#include "vars.h"
#include "window.h"
#include "glob.h"

/* Forward declarations */

static	void	help_me 		(char *, char *);
static	void	help_show_paused_topic 	(char *, char *);
static	void	create_help_window 	(void);
static	void	set_help_screen 	(Screen *);
static	void	help_put_it	(const char *topic, const char *format, ...);

/*
 * A few variables here - A lot added to get help working with
 * non - recursive calls to irc_io, and also have it still 
 * reading things from the server(s), so not to ping timeout.
 */
static	int	dont_pause_topic = 0;
static	int	entry_size;
static	int	finished_help_paging = 0;
static	FILE *	help_fp;
#define HELP_PAUSED_LINES_MAX 500
static	int	help_paused_lines = 0;
static	char *	help_paused_topic[HELP_PAUSED_LINES_MAX]; /* Should be enough */
static	Screen *help_screen = (Screen *) 0;
static	int	help_show_directory = 0;
static	char	help_topic_list[BIG_BUFFER_SIZE + 1];
static	Window *help_window = (Window *) 0;
static	char	no_help[] = "NOHELP";
static	char	paused_topic[128];
static	char *	this_arg;
static	int	use_help_window = 0;


/* 
 * show_help:  show's either a page of text from a help_fp, or the whole
 * thing, depending on the value of HELP_PAGER_VAR.  If it gets to the end,
 * (in either case it will eventally), it closes the file, and returns 0
 * to indicate this.
 */ 
static	int	show_help (Window *window, char *name)
{
	Window	*old_to_window;
	int	rows = -1;
	char	line[256];

	if (!help_fp)
		return 0;

	old_to_window = to_window;
	to_window = window ? window : current_window;

	if (get_int_var(HELP_PAGER_VAR))
		rows = to_window->display_size;

	while (rows)
	{
 		if (!fgets(line, 255, help_fp))
		{
			fclose(help_fp);
			help_fp = NULL;
			to_window = old_to_window;
			return 0;
		}

		if (*(line + strlen(line) - 1) == '\n')
			*(line + strlen(line) - 1) = (char) 0;
			
		/*
		 * This is for compatability with ircII-4.4
		 */
		if (*line == '!' || *line == '#')
			continue;

		help_put_it(name, "%s", line);
		rows--;
	}

	to_window = old_to_window;
	return (1);
}

/*
 * help_prompt: The main procedure called to display the help file
 * currently being accessed.  Using add_wait_prompt(), it sets it
 * self up to be recalled when the next page is asked for.   If
 * called when we have finished paging the help file, we exit, as
 * there is nothing left to show.  If line is 'q' or 'Q', exit the
 * help pager, clean up, etc..  If all is cool for now, we call
 * show_help, and either if its finished, exit, or prompt for the
 * next page.   From here, if we've finished the help page, and
 * doing help prompts, prompt for the help..
 */
static	void	help_prompt (char *name, char *line)
{
	if (finished_help_paging)
	{
		if (*paused_topic)
			help_show_paused_topic(paused_topic, empty_string);
		return;
	}

	if (line && toupper(*line) == 'Q')
	{
		finished_help_paging = 1;
		fclose(help_fp);
		help_fp = NULL;
	}

	if (show_help(help_window, name))
	{
		if (dumb_mode)
			help_prompt(name, NULL);
		else
			add_wait_prompt("*** Hit any key for more, 'q' to quit ***",
				help_prompt, name, WAIT_PROMPT_KEY, 1);
	}
	else
	{
		finished_help_paging = 1;
		if (help_fp)
			fclose(help_fp);
		help_fp = NULL;

		if (help_show_directory)
		{
			if (get_int_var(HELP_PAGER_VAR))
			{
			    if (dumb_mode)
				help_show_paused_topic(name, empty_string);
			    else
				add_wait_prompt("*** Hit any key to end ***", 
					help_show_paused_topic, paused_topic,
					WAIT_PROMPT_KEY, 1);
			}
			else
			{
			    help_show_paused_topic(paused_topic, empty_string);
			    set_help_screen((Screen *) 0);
			}
			help_show_directory = 0;
			return;
		}
	}

	if (finished_help_paging)
	{
		if (get_int_var(HELP_PROMPT_VAR))
		{
			char	tmp[BIG_BUFFER_SIZE + 1];

			sprintf(tmp, "%s%sHelp? ", help_topic_list,
				*help_topic_list ? " " : empty_string);
			if (!dumb_mode)
				add_wait_prompt(tmp, help_me, help_topic_list,
					WAIT_PROMPT_LINE, 1);
		}
		else
		{
			if (*paused_topic)
				help_show_paused_topic(paused_topic, empty_string);
#if 0
			set_help_screen((Screen *) 0);
#endif
		}
	}
}

/*
 * help_topic:  Given a topic, we search the help directory, and try to
 * find the right file, if all is cool, and we can open it, or zcat it,
 * then we call help_prompt to get the actually displaying of the file
 * on the road.
 */
static	void	help_topic (char *path, char *name)
{
	char	*filename = (char *) 0;

	if (!name)
		return;

	/* what is the base name? */
	filename = m_sprintf("%s/%s", path, name);
	if (filename[strlen(filename)-1] == '/')
		chop(filename, 1);

	/* let uzfopen have all the fun */
	if ((help_fp = uzfopen(&filename, path, 1)))
	{
		/* Isnt this a heck of a lot better then the kludge you were using? */
		help_put_it(name, "*** Help on %s", name);
		help_prompt(name, NULL);
	}
	else
		help_put_it (name, "*** No help available on %s: Use ? for list of topics", name);

	new_free(&filename);
	return;
}

/*
 * help_pause_add_line: this procedure does a help_put_it() call, but
 * puts off the calling, until help_show_paused_topic() is called.
 * I do this because I need to create the list of help topics, but
 * not show them, until we've seen the whole file, so we called
 * help_show_paused_topic() when we've seen the file, if it is needed.
 */
static 	void 	help_pause_add_line (char *format, ...)
{
	char	buf[BIG_BUFFER_SIZE];
	va_list args;

	va_start (args, format);
	vsnprintf(buf, BIG_BUFFER_SIZE - 1, format, args);
	va_end (args);
	if (help_paused_lines >= HELP_PAUSED_LINES_MAX)
		panic("help_pause_add_line: would overflow the buffer");
	malloc_strcpy(&help_paused_topic[help_paused_lines], buf);
	help_paused_lines++;
}

/*
 * help_show_paused_topic:  see above.  Called when we've seen the
 * whole help file, and we have a list of topics to display.
 */
static	void	help_show_paused_topic (char *name, char *line)
{
	static int i = 0;
	int j = 0;
	int rows;

	if (!help_paused_lines)
		return;

	if (toupper(*line) == 'Q')
		i = help_paused_lines + 1;	/* just big enough */

	if (get_int_var(HELP_PAGER_VAR))
		rows = help_window->display_size;
	else
		rows = help_paused_lines;	/* Suitably big enough */

	if (i < help_paused_lines)
	{
		for (j = 0; j < rows; j++)
		{
			help_put_it(name, "%s", help_paused_topic[i]);
			new_free(&help_paused_topic[i]);

			/* if we're done, the recurse to break loop */
			if (++i >= help_paused_lines)
				break;
		}
		if (!dumb_mode)
		{
			if ((i < help_paused_lines) && get_int_var(HELP_PAGER_VAR))
				add_wait_prompt("[MORE]", help_show_paused_topic, name, WAIT_PROMPT_KEY, 1);
		}
		else
			help_show_paused_topic(name, line);
	}

	/* 
	 * This cant be an else of the previous if because 'i' can 
	 * change in the previous if and we need to test it again
	 */
	if (i >= help_paused_lines)
	{
		if (get_int_var(HELP_PROMPT_VAR))
		{
			char	buf[BIG_BUFFER_SIZE];

			sprintf(buf, "%s%sHelp? ", name, (name && *name) ? " " : empty_string);
			if (!dumb_mode)
				add_wait_prompt(buf, help_me, name, WAIT_PROMPT_LINE, 1);
		}
		else
			set_help_screen((Screen *) 0);

		dont_pause_topic = 0;
		help_paused_lines = 0;	/* Probably should reset this ;-) */
		i = 0;
	}
}

/*
 * help_me:  The big one.  The help procedure that handles working out
 * what was actually requested, sets up the paused topic list if it is
 * needed, does pretty much all the hard work.
 */
static	void	help_me (char *topics, char *args)
{
	char *	ptr;
	glob_t	g;
	int	entries = 0,
		cnt,
		i,
		cols;
	Stat	stat_buf;
	char	path[BIG_BUFFER_SIZE+1];
	int	help_paused_first_call = 0;
	char *	help_paused_path = (char *) 0;
	char *	help_paused_name = (char *) 0;
	char *	temp;
	char	tmp[BIG_BUFFER_SIZE+1];
	char	buffer[BIG_BUFFER_SIZE+1];
	char *	pattern = NULL;

	strcpy(help_topic_list, topics);
	ptr = get_string_var(HELP_PATH_VAR);

	sprintf(path, "%s/%s", ptr, topics);
	for (ptr = path; (ptr = strchr(ptr, ' '));)
		*ptr = '/';

	/*
	 * first we check access to the help dir, whinge if we can't, then
	 * work out we need to ask them for more help, else we check the
	 * args list, and do the stuff 
	 */
	if (help_show_directory)
	{
		help_show_paused_topic(paused_topic, empty_string);
		help_show_directory = 0;
	}
		
	finished_help_paging = 0;
	if (access(path, R_OK|X_OK))
	{
		help_put_it(no_help, "*** Cannot access help directory!");
		set_help_screen((Screen *) 0);
		return;
	}

	this_arg = next_arg(args, &args);
	if (!this_arg && *help_topic_list && get_int_var(HELP_PROMPT_VAR))
	{
		if ((temp = strrchr(help_topic_list, ' ')) != NULL)
			*temp = '\0';
		else
			*help_topic_list = '\0';

		sprintf(tmp, "%s%sHelp? ", help_topic_list, *help_topic_list ? " " : empty_string);

		if (!dumb_mode)
			add_wait_prompt(tmp, help_me, help_topic_list, WAIT_PROMPT_LINE, 1);
		return;
	}

	if (!this_arg)
	{
		set_help_screen((Screen *) 0);
		return;
	}

	create_help_window();

	if (!help_window)
		return;

	/*
	 * This is just a bogus while loop which is intended to allow
	 * the user to do '/help alias expressions' without having to
	 * include a slash inbetween the topic and subtopic.
	 *
	 * If all goes well, we 'break' at the bottom of the loop.
	 */
	while (this_arg)
	{
		entries = 0;
		message_from((char *) 0, LOG_CURRENT);

		if (!*this_arg)
			help_topic(path, NULL);

		if (strcmp(this_arg, "?") == 0)
		{
			this_arg = empty_string;
			if (!dont_pause_topic)
				dont_pause_topic = 1;
		}

		/*
		 * entry_size is set to the width of the longest help topic
		 * (adjusted for compression extensions, of course.)
		 */
		entry_size = 0;

		/*
		 * Gather up the names of the files in the help directory.
		 */
		{
#ifndef HAVE_FCHDIR
			char 	opath[MAXPATHLEN + 1];
			getcwd(opath, MAXPATHLEN);
#else
			int 	cwd = open(".", O_RDONLY);
#endif

			chdir(path);
			pattern = alloca(strlen(path) + 2 + 
					 strlen(this_arg) + 3);
			strcpy(pattern, this_arg);
			strcat(pattern, "*");
#ifdef GLOB_INSENSITIVE
			bsd_glob(pattern, GLOB_INSENSITIVE, NULL, &g);
#else
			bsd_glob(pattern, 0, NULL, &g);
#endif

#ifndef HAVE_FCHDIR
			chdir(opath);
#else
			fchdir(cwd);
			close(cwd);
#endif
		}

		for (i = 0; i < g.gl_matchc; i++)
		{
			char	*tmp = g.gl_pathv[i];
			int 	len = strlen(tmp);

			if (!end_strcmp(tmp, ".gz", 3))
				len -= 3;
			entry_size = (len > entry_size) ? len : entry_size;
		}

		/*
		 * Right here we need to check for an 'exact match'.
		 * An 'exact match' would be sitting in gl_pathv[0],
		 * and it is 'exact' if it is identical to what we are
		 * looking for, or if it is the same except that it has
		 * a compression extension on it
		 */
		if (g.gl_matchc > 1)
		{
			char *str1 = g.gl_pathv[0];
			char *str2 = this_arg;
			int len1 = strlen(str1);
			int len2 = strlen(str2);


			     if (len1 == len2 && !my_stricmp(str1, str2))
				entries = 1;
			else if (len1 - 3 == len2 && !my_strnicmp(str1, str2, len2) && !end_strcmp(str1, ".gz", 3))
				entries = 1;
			else if (len1 - 2 == len2 && !my_strnicmp(str1, str2, len2) && !end_strcmp(str1, ".Z", 2))
				entries = 1;
			else if (len1 - 2 == len2 && !my_strnicmp(str1, str2, len2) && !end_strcmp(str1, ".z", 2))
				entries = 1;
		}

		if (!*help_topic_list)
			dont_pause_topic = 1;

/* reformatted */
/*
 * entries: -1 means something really died, 0 means there
 * was no help, 1, means it wasn't a directory, and so to
 * show the help file, and the default means to add the
 * stuff to the paused topic list..
 */
if (!entries)
	entries = g.gl_matchc;

switch (entries)
{
	case -1:
	{
		help_put_it(no_help, "*** Error during help function: %s", strerror(errno));
		set_help_screen(NULL);
		if (help_paused_first_call)
		{
			help_topic(help_paused_path, help_paused_name);
			help_paused_first_call = 0;
			new_free(&help_paused_path);
			new_free(&help_paused_name);
		}
		return;
	}
	case 0:
	{
		help_put_it(this_arg, "*** No help available on %s: Use ? for list of topics", this_arg);
		if (!get_int_var(HELP_PROMPT_VAR))
		{
			set_help_screen(NULL);
			break;
		}
		sprintf(tmp, "%s%sHelp? ", help_topic_list, *help_topic_list ? " " : empty_string);
		if (!dumb_mode)
			add_wait_prompt(tmp, help_me, help_topic_list, WAIT_PROMPT_LINE, 1);

		if (help_paused_first_call)
		{
			help_topic(help_paused_path, help_paused_name);
			help_paused_first_call = 0;
			new_free(&help_paused_path);
			new_free(&help_paused_name);
		}
	
		break;
	}
	case 1:
	{
		sprintf(tmp, "%s/%s", path, g.gl_pathv[0]);
		stat(tmp, &stat_buf);
		if (stat_buf.st_mode & S_IFDIR)
		{
			strcpy(path, tmp);
			if (*help_topic_list)
				strcat(help_topic_list, " ");

			strcat(help_topic_list, g.gl_pathv[0]);

			if (!(this_arg = next_arg(args, &args)))
			{
				help_paused_first_call = 1;
				malloc_strcpy(&help_paused_path, path);
				malloc_strcpy(&help_paused_name, g.gl_pathv[0]);
				dont_pause_topic = -1;
				this_arg = "?";
			}
			bsd_globfree(&g);
			continue;
		}
		else
		{
			help_topic(path, g.gl_pathv[0]);
			finished_help_paging = 0;
			break;
		}
	}
	default:
	{
		help_show_directory = 1;
		strcpy(paused_topic, help_topic_list);
		help_pause_add_line("*** %s choices:", help_topic_list);
		entry_size += 2;
		cols = (current_term->TI_cols - 10) / entry_size;

		strcpy(buffer, empty_string);
		cnt = 0;

		for (i = 0; i < entries; i++)
		{
			if (!end_strcmp(g.gl_pathv[i], ".gz", 3))
				chop(g.gl_pathv[i], 3);
			strcat(buffer, g.gl_pathv[i]);

			/*
			 * Since we already know how many columns each
			 * line will contain, we check to see if we have
			 * accumulated that many entries.  If we have, we
			 * output the line to the screen.
			 */
			if (++cnt == cols)
			{
				help_pause_add_line("%s", buffer);
				strcpy(buffer, empty_string);
				cnt = 0;
			}

			/*
			 * If we have not finished this line, then we have
			 * to pad the name length out to the expected width.
			 * 'entry_size' is the column width.  We also have
			 * do adjust for compression extension.
			 */
			else
				strextend(buffer, ' ', entry_size - strlen(g.gl_pathv[i]));
		}

		help_pause_add_line("%s", buffer);
		if (help_paused_first_call)
		{
			help_topic(help_paused_path, help_paused_name);
			help_paused_first_call = 0;
			new_free(&help_paused_path);
			new_free(&help_paused_name);
		}
		if (dont_pause_topic == 1)
		{
			help_show_paused_topic(paused_topic, empty_string);
			help_show_directory = 0;
		}
		break;
	}
}
/* end of reformatting */


		bsd_globfree(&g);
		break;
	}

	/*
	 * This one is for when there was never a topic and the prompt
	 * never got a topic..  and help_screen was never reset..
	 * phone, jan 1993.
	 */
	if (!*help_topic_list && finished_help_paging)
		set_help_screen((Screen *) 0);
}

/*
 * help: the HELP command, gives help listings for any and all topics out
 * there 
 */
BUILT_IN_COMMAND(help)
{
	char	*help_path;

	finished_help_paging = 0;
	help_show_directory = 0;
	dont_pause_topic = 0;
	use_help_window = 0;

	/*
	 * The idea here is to work out what sort of help we are using - 
	 * either the installed help files, or some help service, what
	 * ever it maybe.  Once we have worked this out, if we are using
	 * a help window, set it up properly.
	 */
	help_path = get_string_var(HELP_PATH_VAR);

	if (!help_path || !*help_path || access(help_path, R_OK | X_OK))
	{
		help_put_it(no_help, "*** HELP_PATH variable not set or set to an invalid path");
		return;
	}

	/* Allow us to wait until help is finished */
	if (!my_strnicmp(args, "-wait", 2))
	{
		while (help_screen)
			io("help");
		return;
	}

	if (help_path && help_screen && help_screen != current_window->screen)
	{
		say("You may not run help in two screens");
		return;
	}

	help_screen = current_window->screen;
	help_window = (Window *) 0;
	help_me(empty_string, (args && *args) ? args : "?");
}




static	void create_help_window (void)
{
	if (help_window)
		return;

	if (!dumb_mode && get_int_var(HELP_WINDOW_VAR))
	{
		if (!(help_window = new_window(current_window->screen)))
		{
			say("Could not create a new window, maybe you have "
				"all your windows fixed?");
			return;
		}

		use_help_window = 1;
		help_window->hold_mode = OFF;
		help_window->window_level = LOG_HELP;
		update_all_windows();
	}
	else
		help_window = current_window;
}



static	void	set_help_screen (Screen *screen)
{
	help_screen = screen;
	if (!help_screen && help_window)
	{
		if (use_help_window)
		{
			int display = window_display;

			window_display = 0;
			delete_window(help_window);
			window_display = display;
		}
		help_window = (Window *) 0;
		update_all_windows();
	}
}

static	void	help_put_it	(const char *topic, const char *format, ...)
{
	char putbuf[BIG_BUFFER_SIZE * 3 + 1];

	if (format)
	{
		va_list args;
		va_start (args, format);
		vsnprintf(putbuf, BIG_BUFFER_SIZE * 3, format, args);
		va_end(args);

		if (do_hook(HELP_LIST, "%s %s", topic, putbuf))
		{
			int old_level = who_level;
			Window *old_to_window = to_window;

			/*
			 * LOG_HELP is a completely bogus mode.  We use
			 * it only to make sure that the current level is
			 * not LOG_CURRENT, so that the to_window will stick.
			 */
			who_level = LOG_HELP;
			if (help_window)
				to_window = help_window;
			add_to_screen(putbuf);
			to_window = old_to_window;
			who_level = old_level;
		}
	}
}

