/* $EPIC: log.c,v 1.23 2005/10/13 01:11:58 jnelson Exp $ */
/*
 * log.c: handles the irc session logging functions 
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

static FILE *open_log (const char *logfile, FILE **fp)
{
	time_t	t;
	char	my_buffer[256];
	struct	tm	*ugh;
	Filename fullname;

	time(&t);
	ugh = localtime(&t);		/* Not gmtime, m'kay? */
	/* Ugh.  Solaris. */
	strftime(my_buffer, 255, "%a %b %d %H:%M:%S %Y", ugh);

	if (*fp) 
	{
		say("Logging is already on");
		return *fp;
	}

	if (!logfile)
		return NULL;
			
	if (normalize_filename(logfile, fullname))
		(void)0;		/* Do nothing... */

	if ((*fp = fopen(fullname, "a")) != NULL)
	{
		chmod(fullname, S_IREAD | S_IWRITE);
		say("Starting logfile %s", fullname);

		fprintf(*fp, "IRC log started %s\n", my_buffer);
		fflush(*fp);
	}
	else
	{
		say("Couldn't open logfile %s: %s", fullname, strerror(errno));
		*fp = NULL;
	}

	return (*fp);
}

static FILE *close_log (FILE **fp)
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

FILE *do_log (int flag, const char *logfile, FILE **fp)
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
	char *	logfile;

	v = (VARIABLE *)stuff;
	flag = v->integer;

	if ((logfile = get_string_var(LOGFILE_VAR)) == (char *) 0)
	{
		say("You must set the LOGFILE variable first!");
		v->integer = 0;
		return;
	}
	do_log(flag, logfile, &irclog_fp);
	if (!irclog_fp && flag)
		v->integer = 0;
}

/*
 * add_to_log: add the given line to the log file.  If no log file is open
 * this function does nothing. 
 * (Note:  "logref" should be -1 unless we are logging from a /log log, ie,
 * any place that is not inside logfiles.c)
 */
void 	add_to_log (int logref, FILE *fp, long winref, const unsigned char *line, int mangler, const char *rewriter)
{
	char	*local_line;
	int	must_free = 0;
	int	old_logref;

	if (!fp || inhibit_logging)
		return;

	old_logref = current_log_refnum;
	current_log_refnum = logref;

	/* Do this first */
	if (mangler == 0)
		mangler = logfile_line_mangler;
	if (get_int_var(NO_CONTROL_LOG_VAR))
		mangler |= STRIP_UNPRINTABLE;
	if (mangler)
		local_line = new_normalize_string(line, 1, mangler);

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
		prepend_exp = expand_alias(rewriter, argstuff,
					   NULL);

		local_line = prepend_exp;
		must_free = 1;
	}

	fprintf(fp, "%s\n", local_line);
	fflush(fp);

	current_log_refnum = old_logref;
}

