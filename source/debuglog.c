/*
 * debuglog.c -- parallel debug logging for epic5
 * Copyright 2013 EPIC Software Labs
 *
 * [include the copyright body here]
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
static	int	counter = 0;

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
	struct timeval now;
	time_t sec;
	struct tm *tm;

        if (debuglogf && format)
        {
                va_list args;
                va_start (args, format);

		get_time(&now);
		sec = now.tv_sec;
		tm = localtime(&sec);
		strftime(timebuf, 10240, "%F %T", tm);
		fprintf(debuglogf, "[%s.%04ld] ", timebuf, now.tv_usec / 1000);
                vfprintf(debuglogf, format, args);
                va_end(args);
		fprintf(debuglogf, "\n");
		fflush(debuglogf);
		return 0;
	}	
	return -1;
}


