/*
 * log.c: handles the irc session logging functions 
 *
 * Written By Michael Sandrof
 * Copyright(c) 1990 
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
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
