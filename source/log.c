/*
 * log.c: handles the irc session logging functions 
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright 1995, 2009 EPIC Software Labs.
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
#include "levels.h"
#include "log.h"
#include "vars.h"
#include "output.h"
#include "ircaux.h"
#include "alias.h"
#include "screen.h"

	FILE	*irclog_fp;
	int	logfile_line_mangler;
	int	current_log_refnum = -1;

/*
 * XXX This should return an int, and act as a front end
 * 	to open_file_for_write().
 */
static FILE *	open_log (const char *logfile, FILE **fp)
{
	char *		tempname;
	size_t		templen;
	Filename 	fullname;

	/* If the user hasn't specified a logfile, do nothing */
	if (!logfile)
		return NULL;

	/* If the user is already logging here, tell them. */
	if (*fp) 
	{
		say("Logging is already on");
		return *fp;
	}

	/* If necessary, remove "s on the outside of the filename */
	/* I do this way too much on accident; it's annoying not to have it */
	tempname = LOCAL_COPY(logfile);
	templen = strlen(tempname);
	if (templen > 2 && tempname[0] == '"' 
			&& tempname[templen - 1] == '"')
	{
		tempname[templen - 1] = 0;
		tempname++;
	}

	/* If the result is an empty filename then just punt */
	if (!tempname || !*tempname)
	{
		yell("Cannot log to the filename [%s] because the result "
			"was an empty filename", logfile);
		return NULL;
	}

	if (normalize_filename(tempname, fullname))
	{
		yell("Warning: I could not normalize the filename [%s] "
			"(the result was [%s] -- watch out", 
			logfile, fullname);
		(void)0;		/* Do nothing... */
	}

	if ((*fp = fopen(fullname, "a")) != NULL)
	{
		time_t		t;
		struct	tm *	ltime;
		char		timestr[256];

		/* Convert the time to a string to insert in the file */
		time(&t);
		ltime = localtime(&t);		/* Not gmtime, m'kay? */
		strftime(timestr, 255, "%a %b %d %H:%M:%S %Y", ltime);

		chmod(fullname, S_IRUSR | S_IWUSR);
		say("Starting logfile %s", fullname);
		fprintf(*fp, "IRC log started %s\n", timestr);
		fflush(*fp);
	}
	else
	{
		yell("Couldn't open logfile %s: %s", fullname, strerror(errno));
		*fp = NULL;
	}

	return (*fp);
}

static FILE *	close_log (FILE **fp)
{
	time_t	t;
	char	my_buffer[256];
struct	tm	*ugh;

	time(&t);
	ugh = localtime(&t);		/* Not gmtime, m'kay? */
	/* Ugh.  Solaris. */
	strftime(my_buffer, 255, "%a %b %d %H:%M:%S %Y", ugh);

	if (*fp)
	{
		fprintf(*fp, "IRC log ended %s\n", my_buffer);
		fflush(*fp);
		fclose(*fp);
		*fp = (FILE *) 0;
		say("Logfile ended");
	}

	return (*fp);
}

/*
 * do_log: open or close a logfile
 *	flag 	- 1 if the logfile should be opened
 *		  0 if the logfile should be closed
 *	logfile - The name of the logfile (only needed if flag == 1)
 *	fp	- A pointer to a (FILE *) which gets the result of fopen().
 *		  Will be set to NULL if the file is not open (due to 
 *		  error or flag == 0)
 */
FILE *	do_log (int flag, const char *logfile, FILE **fp)
{
	if (flag)
		return open_log(logfile, fp);
	else
		return close_log(fp);
}

/* logger: if flag is 0, logging is turned off, else it's turned on */
void	logger (void *stuff)
{
	VARIABLE *v;
	int	flag;
	char *	logfile = NULL;

	v = (VARIABLE *)stuff;
	flag = v->integer;

	/* Always allow /SET LOG OFF, regardless of /SET LOGFILE */
	if (flag == 0)
		do_log(flag, NULL, &irclog_fp);
	else
	{
		/* You cannot /SET LOG ON if you cleared /SET LOGFILE */
		/* (This is rare - /SET LOGFILE has a default value) */
		if ((logfile = get_string_var(LOGFILE_VAR)) == (char *) 0)
		{
			say("You must set the LOGFILE variable first!");
			v->integer = 0;
			return;
		}

		do_log(flag, logfile, &irclog_fp);

		/* If opening the logfile failed, set log off */
		if (!irclog_fp && flag)
			v->integer = 0;
	}
}

/*
 * set_logfile 	- Callback for /SET LOGFILE
 *
 * Arguments:
 *  stuff	- A (VARIABLE *) object
 *
 * Notes:
 *  + In EPIC5-1.8, we changed this so if you change the logfile name when 
 *    SET LOG ON, it will close the old filename and open the new filename.
 *  + Unsetting the logfile name while logging is on will stop logging.
 *  + Changing the logfile name to itself will result in a close+reopen,
 */
void	set_logfile (void *stuff)
{
	VARIABLE *v;
	char *logfile;

	/* If SET LOG OFF, then we are done here! */
	if (!irclog_fp)
		return;

	v = (VARIABLE *)stuff;

	if (!(logfile = v->string))
	{
		say("Unsetting LOGFILE turns off logging");
		set_var_value(LOG_VAR, zero, 1);
	}
	else
	{
		do_log(0, NULL, &irclog_fp);
		do_log(1, logfile, &irclog_fp);
	}
}

/*
 * add_to_log: add the given line to the log file.  If no log file is open
 * this function does nothing. 
 * (Note:  "logref" should be -1 unless we are logging from a /log log, ie,
 * any place that is not inside logfiles.c)
 */
void 	add_to_log (int logref, FILE *fp, long winref, const unsigned char *line, int mangler, const char *rewriter)
{
	char	*local_line = NULL;
	int	old_logref;
static	int	recursive = 0;

	/*
	 * I added "recursive" because this function should not
	 * generate any output.  But it might generate output if
	 * the string expand was bogus below.  I chose to "fix" this
	 * by refusing to log anything recursively.  The downside
	 * is any errors won't get logged, which may or may not be
	 * a problem, I haven't decided yet.
	 */
	if (recursive > 0)
		return;
	
	if (!fp || inhibit_logging)
		return;

	recursive++;
	old_logref = current_log_refnum;
	current_log_refnum = logref;

	/* Do this first */
	/* Either do mangling or /set no_control_log, but never both! */
	if (mangler == 0)
		mangler = logfile_line_mangler;
	else if (get_int_var(NO_CONTROL_LOG_VAR))
		mangler |= STRIP_UNPRINTABLE;
	if (mangler)
		local_line = new_normalize_string(line, 1, mangler);
	else
		local_line = malloc_strdup(line);

	if (rewriter == NULL)
		rewriter = get_string_var(LOG_REWRITE_VAR);
	if (rewriter)
	{
		char    *prepend_exp;
		char    argstuff[10240];

		/* First, create the $* list for the expando */
		snprintf(argstuff, 10240, "%ld %s", winref, local_line);
		new_free(&local_line);

		/* Now expand the expando with the above $* */
		prepend_exp = expand_alias(rewriter, argstuff);
		local_line = prepend_exp;
	}

	fprintf(fp, "%s\n", local_line); /* XXX UTF8 XXX */
	fflush(fp);

	new_free(&local_line);
	current_log_refnum = old_logref;
	recursive--;
}

