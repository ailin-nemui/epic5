/*
 * debuglog.c -- parallel debug logging for epic5
 *
 * Copyright 2013 EPIC Software Labs
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
 * So here's the plan...
 * Sometimes it is hard to debug the screen output of epic, because you
 * have to output *something* and if the output routines are screwed up,
 * then how can you output something to say something is wrong?
 *
 * Anyways, this is a general purpose extensible debug logging system 
 * that I will start using to log things that I can't put to the screen.
 * Then I can just tail -f the logfile in another window.
 *
 * For better or for worse, this file is included from commands.c
 */

static	FILE *	debuglogf = NULL;

BUILT_IN_COMMAND(debuglogcmd)
{
	char *	arg;

	if (!(arg = next_arg(args, &args)))
	{
		if (debuglogf == NULL)
			say("Debug log is OFF");
		else
			say("Debug log is ON");
	}
	else if (!my_stricmp(arg, "ON"))
	{
		if (debuglogf == NULL)
		{
			if (!(debuglogf = fopen("debug.log", "a")))
			{
				yell("Cannot open debug.log. help!");
				return;
			}
			debuglog("Log file opened");
		}
		say("Debug log is ON (debug.log)");
	}
	else if (!my_stricmp(arg, "OFF"))
	{
		debuglog("Log file closed");
		if (debuglogf != NULL)
		{
			fclose(debuglogf);
			debuglogf = NULL;
		}
		say("Debug log is OFF");
	}
}

int	debuglog (const char *format, ...)
{
	char	timebuf[10240];
	struct timeval xnow;
	time_t sec;
	struct tm *tm;

        if (debuglogf && format)
        {
                va_list args;
                va_start (args, format);

		get_time(&xnow);
		sec = xnow.tv_sec;
		tm = localtime(&sec);
		strftime(timebuf, 10240, "%F %T", tm);
		fprintf(debuglogf, "[%s.%04ld] ", timebuf, (long)xnow.tv_usec / 1000);
                vfprintf(debuglogf, format, args);
                va_end(args);
		fprintf(debuglogf, "\n");
		fflush(debuglogf);
		return 0;
	}	
	return -1;
}


