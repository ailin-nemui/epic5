/* $EPIC: log.c,v 1.4 2002/07/17 22:52:52 jnelson Exp $ */
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
#include "log.h"
#include "vars.h"
#include "output.h"
#include "ircaux.h"
#include "alias.h"
#include <sys/stat.h>

	FILE	*irclog_fp;
	int	logfile_line_mangler;

FILE *do_log (int flag, char *logfile, FILE **fp)
{
	time_t	t;
	char	my_buffer[256];
struct	tm	*ugh;

	time(&t);
	ugh = localtime(&t);		/* Not gmtime, m'kay? */
	/* Ugh.  Solaris. */
	strftime(my_buffer, 255, "%a %b %d %H:%M:%S %Y", ugh);

	if (flag)
	{
		if (*fp)
			say("Logging is already on");
		else
		{
			if (!logfile)
				return NULL;
				
			if (!(logfile = expand_twiddle(logfile)))
			{
				say("SET LOGFILE: No such user");
				return NULL;
			}

			if ((*fp = fopen(logfile, "a")) != NULL)
			{
				chmod(logfile, S_IREAD | S_IWRITE);
				say("Starting logfile %s", logfile);

				fprintf(*fp, "IRC log started %s\n", my_buffer);
				fflush(*fp);
			}
			else
			{
				say("Couldn't open logfile %s: %s", logfile, strerror(errno));
				*fp = (FILE *) 0;
			}
			new_free(&logfile);
		}
	}
	else
	{
		if (*fp)
		{
			fprintf(*fp, "IRC log ended %s\n", my_buffer);
			fflush(*fp);
			fclose(*fp);
			*fp = (FILE *) 0;
			say("Logfile ended");
		}
	}

	return (*fp);
}

/* logger: if flag is 0, logging is turned off, else it's turned on */
void	logger (int flag)
{
	char	*logfile;

	if ((logfile = get_string_var(LOGFILE_VAR)) == (char *) 0)
	{
		say("You must set the LOGFILE variable first!");
		set_int_var(LOG_VAR, 0);
		return;
	}
	do_log(flag, logfile, &irclog_fp);
	if (!irclog_fp && flag)
		set_int_var(LOG_VAR, 0);
}

/*
 * set_log_file: sets the log file name.  If logging is on already, this
 * closes the last log file and reopens it with the new name.  This is called
 * automatically when you SET LOGFILE. 
 */
void set_log_file (char *filename)
{
	char	*expand;

	if (filename)
	{
		if (strcmp(filename, get_string_var(LOGFILE_VAR)))
			expand = expand_twiddle(filename);
		else
			expand = expand_twiddle(get_string_var(LOGFILE_VAR));

		if (!expand)
		{
			say("SET LOGFILE: No such user");
			return;
		}

		set_string_var(LOGFILE_VAR, expand);
		new_free(&expand);
		if (irclog_fp)
		{
			logger(0);
			logger(1);
		}
	}
}

/*
 * add_to_log: add the given line to the log file.  If no log file is open
 * this function does nothing. 
 */
void 	add_to_log (FILE *fp, unsigned winref, const unsigned char *line)
{
	char	*local_line;
	size_t	size;
	int	must_free = 0;
	char *	pend = NULL;

	if (fp && !inhibit_logging)
	{
		/*
		 * We need to make a local copy because 'mangle_line' 
		 * diddles around with the source, and so we can't subject
		 * line to that, it is 'const'.
		 *
		 * 'mangle_line' can expand the input string, so it is 
		 * neccesary to allocate more than we need.
		 */
		size = strlen(line) * 11;
		local_line = alloca(size + 1);
		strcpy(local_line, line);

		/* Do this first */
		if (logfile_line_mangler)
		   if (mangle_line(local_line, logfile_line_mangler, size)
					> size)
			; /* Whimper -- what to do, what to do? */

		if (get_int_var(NO_CONTROL_LOG_VAR))
		{
			char *tmp = alloca(strlen(local_line) + 1);
			strip_control(local_line, tmp);
			strlcpy(local_line, tmp, size);
		}

                if ((pend = get_string_var(LOG_REWRITE_VAR)))
                {
                        char    *prepend_exp;
                        char    argstuff[10240];
                        int     args_flag;

                        /* First, create the $* list for the expando */
                        snprintf(argstuff, 10240, "%u %s", winref, local_line);

                        /* Now expand the expando with the above $* */
                        prepend_exp = expand_alias(pend, argstuff,
                                                   &args_flag, NULL);

                        local_line = prepend_exp;
                        must_free = 1;
                }

		fprintf(fp, "%s\n", local_line);
		fflush(fp);

		if (must_free)
			new_free(&local_line);
	}
}
