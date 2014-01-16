/* $EPIC: irc.c,v 1.1374 2014/01/16 19:31:24 jnelson Exp $ */
/*
 * ircII: a new irc client.  I like it.  I hope you will too!
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright © 1994 Jake Khuon.
 * Copyright © 1993, 2010 EPIC Software Labs.
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


/*
 * irc_version is what $J returns, its the common-name for the version.
 */
const char irc_version[] = "EPIC5-1.1.7";
const char useful_info[] = "epic5 1 1 7";

/*
 * internal_version is what $V returns, its the integer-id for the
 * version, and corresponds to the date of release, YYYYMMDD.
 */ 
const char internal_version[] = "20140116";

/*
 * In theory, this number is incremented for every commit.
 */
const unsigned long	commit_id = 1705;

/*
 * As a way to poke fun at the current rage of naming releases after
 * english words, which often have little or no correlation with outside
 * reality, I have decided to start doing that with EPIC.  These names
 * are intentionally and maliciously silly.  Complaints will be ignored.
 */
const char ridiculous_version_name[] = "Kainotophobia";

#define __need_putchar_x__
#include "status.h"
#include "clock.h"
#include "dcc.h"
#include "names.h"
#include "vars.h"
#include "input.h"
#include "alias.h"
#include "output.h"
#include "termx.h"
#include "exec.h"
#include "screen.h"
#include "log.h"
#include "server.h"
#include "hook.h"
#include "keys.h"
#include "ircaux.h"
#include "commands.h"
#include "window.h"
#include "exec.h"
#include "notify.h"
#include "mail.h"
#include "timer.h"
#include "newio.h"
#include "parse.h"
#include "levels.h"
#include "extlang.h"
#include "files.h"
#include <pwd.h>


/*
 * Global variables
 */

/* The ``DEFAULT'' port used for irc server connections. */
int		irc_port = IRC_PORT;

/* 
 * When a numeric is being processed, this holds the negative value
 * of the numeric.  Its negative so if we pass it to do_hook, it can
 * tell its a numeric and not a named ON event.
 */
int		current_numeric = -1;

/* Set if the client is not using a termios interface */
int		dumb_mode = 0;

/* Set if the client is supposed to fork(). (a bot.)  Probably bogus. */
int		background = 0;

/* Set if the client is checking fd 0 for input. (usu. op of "background") */
int		use_input = 1;

/*
 * Set when an OPER command is sent out, reset when umode +o or 464 reply
 * comes back.  This is *seriously* bogus.
 */
int		oper_command = 0;

/* Set if your IRCRC file is NOT to be loaded on startup. */
int		quick_startup = 0;

/* Set if user does not want to auto-connect to a server upon startup */
int		dont_connect = 0;

/* Set to the current time, each time you press a key. */
Timeval		idle_time = { 0, 0 };

/* Set to the time the client booted up */
Timeval		start_time;

/* Set to 0 when you want to suppress all beeps (such as window repaints) */
int		global_beep_ok = 1;

/* The unknown userhost value.  Bogus, but DONT CHANGE THIS!  */
const char	*unknown_userhost = "<UNKNOWN>@<UNKNOWN>";

/* Whether or not the client is dying. */
int		dead = 0;

/* The number of pending SIGINTs (^C) still unprocessed. */
volatile int	cntl_c_hit = 0;

/* This is 1 if we are in the foreground process group */
int		foreground = 1;

/* This is 1 if you want all logging to be inhibited. Dont leave this on! */
int		inhibit_logging = 0;

/* This is reset every time io() is called.  Use this to save calls to time */
Timeval		now = {0, 0};

/* Output which is displayed without modification by the user */
int		privileged_output = 0;

/* Output which should not trigger %F in hidden windows */
int		do_window_notifies = 1;

/*
 * If set, outbound connections will be bind()ed to the address
 * specified.  if unset, the default address for your host will
 * be used.  LocalHostName can be set by the /HOSTNAME command 
 * or via the IRCHOST environment variable.  These variables should
 * be considered read-only.  Dont ever change them.
 *
 * Its important (from a user's point of view) that these never be
 * set to addresses that do not belong to the current hostname.
 * If that happens, outbound connections will fail, and its not my fault.
 */
char *		LocalIPv4HostName = NULL;
char *		LocalIPv6HostName = NULL;
char *		LocalHostName = NULL;

int		inbound_line_mangler = 0,
		outbound_line_mangler = 0;

static char	*epicrc_file = NULL,		/* full path .epicrc file */
		*ircrc_file = NULL;		/* full path .ircrc file */
char		*startup_file = NULL,		/* Set when epicrc loaded */
		*my_path = (char *) 0,		/* path to users home dir */
		*irc_lib = (char *) 0,		/* path to the ircII library */
		*default_channel = NULL,	/* Channel to join on connect */
		nickname[NICKNAME_LEN + 1],	/* users nickname */
		hostname[NAME_LEN + 1],		/* name of current host */
#if 0
		username[NAME_LEN + 1],		/* usernameof user */
		userhost[NAME_LEN + 1],		/* userhost of user */
#endif
		*send_umode = NULL,		/* sent umode */
		*last_notify_nick = (char *) 0;	/* last detected nickname */
const char	empty_string[] = "",		/* just an empty string */
		space[] = " ",			/* just a lonely space */
		on[] = "ON",
/*		my_off[] = "OFF", */
		zero[] = "0",
		one[] = "1",
		star[] = "*",
		dot[] = ".",
		comma[] = ",";
unsigned char	*cut_buffer = (unsigned char *) 0;	/* global cut_buffer */

static		char	switch_help[] =
"Usage: epic [switches] [nickname] [server list]                      \n\
  The [nickname] can be up to 30 characters long                      \n\
  The [server list] are one or more server descriptions               \n\
  The [switches] are zero or more of the following:                   \n\
      -a\tThe [server list] adds to default server list               \n"
#ifndef NO_BOTS
"      -b\tThe program should run in the background ``bot mode''       \n"
#endif
"      -B\tLoads your .ircrc file before you connect to a server.      \n\
      -d\tThe program should run in ``dumb mode'' (no fancy screen)   \n\
      -h\tPrint this help message                                     \n\
      -q\tThe program will not load your .ircrc file                  \n\
      -s\tThe program will not connect to a server upon startup       \n\
      -S\tEach argument will be tokenised by whitechar                \n\
      -v\tPrint the version of this irc client and exit               \n\
      -x\tRun the client in full X_DEBUG mode                         \n\
      -c <chan>\tJoin <chan> after first connection to a server       \n\
      -H <host>\tUse a virtual host instead of default hostname	      \n\
      -l <file>\tLoads <file> instead of your .ircrc file             \n\
      -L <file>\tLoads <file> instead of your .ircrc file             \n\
      -n <nick>\tThe program will use <nick> as your default nickname \n\
      -p <port>\tThe program will use <port> as the default portnum   \n\
      -z <user>\tThe program will use <user> as your default username \n";



static SIGNAL_HANDLER(sig_irc_exit)
{
	irc_exit (1, NULL);
}

/* irc_exit: cleans up and leaves */
void	irc_exit (int really_quit, const char *format, ...)
{
	char 	buffer[BIG_BUFFER_SIZE];
	char *	quit_message = NULL;
	int	old_window_display = window_display;
	int	value;

	/*
	 * If we get called recursively, something is hosed.
	 * Each recursion we get more insistant.
	 */
	if (dead == 1)
		exit(1);			/* Take that! */
	if (dead == 2)
		_exit(1);			/* Try harder */
	if (dead >= 3)
		kill(getpid(), SIGKILL);	/* DIE DIE DIE! */

	/* Faults in the following code are just silently punted */
	dead++;	

	if (really_quit == 0)	/* Don't clean up if we're crashing */
		goto die_now;

	close_all_dcc(); /* Need to do this before we close the server */

	if (format)
	{
		va_list arglist;
		va_start(arglist, format);
		vsnprintf(buffer, sizeof(buffer), format, arglist);
		va_end(arglist);
		quit_message = buffer;
	}
	else
	{
		strlcpy(buffer, "Default", sizeof(buffer));
		quit_message = NULL;
	}

	/* Do some clean up */
	do_hook(EXIT_LIST, "%s", buffer);
#ifdef HAVE_TCL
	tclstartstop(0);
#endif
#ifdef HAVE_PERL
	perlstartstop(0);  /* In case there's perl code in the exit hook. */
#endif
#ifdef HAVE_RUBY
	ruby_startstop(0);
#endif

	close_all_servers(quit_message);
	value = 0;
	logger(&value);
	get_child_exit(-1);  /* In case some children died in the exit hook. */
	clean_up_processes();
	close_all_dbms();

	/* Arrange to have the cursor on the input line after exit */
	if (!dumb_mode)
	{
		cursor_to_input();
		term_cr();
		term_clear_to_eol();
		term_reset();
	}
	
	/* Try to free as much memory as possible */
	window_display = 0;
/*	dumpcmd(NULL, NULL, NULL); */
	remove_channel(NULL, 0);
	set_lastlog_size(&value);
	delete_all_windows();
	destroy_server_list();

	destroy_call_stack();
	remove_bindings();
	flush_on_hooks();
	flush_all_symbols();
	window_display = old_window_display;
	fprintf(stdout, "\r");
	fflush(stdout);

	if (really_quit)
		exit(0);

die_now:
	my_signal(SIGABRT, SIG_DFL);
	kill(getpid(), SIGABRT);
	kill(getpid(), SIGQUIT);
	exit(1);
}

volatile int	dead_children_processes;

/* 
 * This is needed so that the fork()s we do to read compressed files dont
 * sit out there as zombies and chew up our fd's while we read more.
 */
static SIGNAL_HANDLER(child_reap)
{
	dead_children_processes = 1;
}

volatile int	segv_recurse = 0;

/* sigsegv: something to handle segfaults in a nice way */
static SIGNAL_HANDLER(coredump)
{
	if (segv_recurse++)
		exit(1);

	if (!dead)
	{
		term_reset();
		fprintf(stderr, "\
									\n\
									\n\
									\n\
* * * * * * * * * * * * * * * * * * * * * * * *				\n\
EPIC has trapped a critical protection error.				\n\
This is probably due to a bug in the program.				\n\
									\n\
If you have access to the 'BUG_FORM' in the ircII source distribution,	\n\
we would appreciate your filling it out if you feel doing so would	\n\
be helpful in finding the cause of your problem.			\n\
									\n\
If you do not know what the 'BUG_FORM' is or you do not have access	\n\
to it, please dont worry about filling it out.  You might try talking	\n\
to the person who is in charge of IRC at your site and see if you can	\n\
get them to help you.							\n\
									\n\
This version of EPIC is --->[%s (%lu)]					\n\
The date of release is  --->[%s]					\n\
									\n\
* * * * * * * * * * * * * * * * * * * * * * * *				\n\
The program will now terminate.						\n", irc_version, commit_id, internal_version);

		fflush(stdout);
		panic_dump_call_stack();

		while ((x_debug & DEBUG_CRASH) && !sleep(1)) {};
	}

        if (x_debug & DEBUG_CRASH)
                irc_exit(0, "Hmmm. %s (%lu) has another bug.  Go figure...",
			irc_version, commit_id);
        else
                irc_exit(1, "Hmmm. %s (%lu) has another bug.  Go figure...",
			irc_version, commit_id);
}

/*
 * quit_response: Used by irc_io when called from irc_quit to see if we got
 * the right response to our question.  If the response was affirmative, the
 * user gets booted from irc.  Otherwise, life goes on. 
 */
static void quit_response (char *dummy, char *ptr)
{
	int	len;

	if ((len = strlen(ptr)) != 0)
		if (!my_strnicmp(ptr, "yes", len))
			irc_exit(1, NULL);
}

/* irc_quit: prompts the user if they wish to exit, then does the right thing */
BUILT_IN_KEYBINDING(irc_quit)
{
	static	int in_it = 0;

	if (in_it)
		return;
	in_it = 1;
	add_wait_prompt("Do you really want to quit? ", 
			quit_response, empty_string, WAIT_PROMPT_LINE, 1);
	in_it = 0;
}

/*
 * cntl_c: emergency exit.... if somehow everything else freezes up, hitting
 * ^C five times should kill the program.   Note that this only works when
 * the program *is* frozen -- if it doesnt die when you do this, then the
 * program is functioning correctly (ie, something else is wrong)
 */
static SIGNAL_HANDLER(cntl_c)
{
	/* after 5 hits, we stop whatever were doing */
	if (cntl_c_hit++ >= 4)
		irc_exit(1, "User pressed ^C five times.");
	else if (cntl_c_hit > 1)
		kill(getpid(), SIGUSR2);
}

static SIGNAL_HANDLER(nothing)
{
	/* nothing to do! */
}

static SIGNAL_HANDLER(sig_user1)
{
	say("Got SIGUSR1, closing DCC connections and EXECed processes");
	close_all_dcc();
	clean_up_processes();
}

static SIGNAL_HANDLER(sig_user2)
{
	system_exception++;
}

static	void	show_version (void)
{
	printf("ircII %s (Commit id: %lu) (Date of release: %s)\n\r", 
			irc_version, commit_id, internal_version);
	exit (0);
}

/*
 * parse_args: parse command line arguments for irc, and sets all initial
 * flags, etc. 
 *
 * major rewrite 12/22/94 -jfn
 * major rewrite 02/18/97 -jfn
 *
 * Sanity check:
 *   Supported flags: -a, -b, -B, -d, -f, -F, -h, -q, -s -v, -x
 *   Flags that take args: -c, -l, -L, -n, -p, -z
 *
 * We use getopt() so that your local argument passing convension
 * will prevail.  The first argument that occurs after all of the normal 
 * arguments have been parsed will be taken as a default nickname.
 * All the rest of the args will be taken as servers to be added to your
 * default server list.
 */
static	void	parse_args (int argc, char **argv)
{
	int ch;
	int append_servers = 0;
	struct passwd *entry;
	char *ptr = (char *) 0;
	char *tmp_hostname = NULL;
	char *the_path = NULL;
	char *translation_path = NULL;
	
	int altargc = 0;
	char **altargv;

	extern char *optarg;
	extern int optind;

	*nickname = 0;
#if 0
	*username = 0;
#endif

	/* 
	 * Its probably better to parse the environment variables
	 * first -- that way they can be used as defaults, but can 
	 * still be overriden on the command line.
	 */
	if ((entry = getpwuid(getuid())))
	{
		if (entry->pw_gecos && *(entry->pw_gecos))
		{
			if ((ptr = strchr(entry->pw_gecos, ',')))
				*ptr = 0;
			set_var_value(DEFAULT_REALNAME_VAR, entry->pw_gecos, 0);
		}

		if (entry->pw_name && *(entry->pw_name))
			set_var_value(DEFAULT_USERNAME_VAR, entry->pw_name, 0);

		if (entry->pw_dir && *(entry->pw_dir))
			malloc_strcpy(&my_path, entry->pw_dir);
	}


	if ((ptr = getenv("IRCNICK")))
		strlcpy(nickname, ptr, sizeof nickname);

	/*
	 * We now allow users to use IRCUSER or USER if we couldnt get the
	 * username from the password entries.  For those systems that use
	 * NIS and getpwuid() fails (boo, hiss), we make a last ditch effort
	 * to see what LOGNAME is (defined by POSIX.2 to be the canonical 
	 * username under which the person logged in as), and if that fails,
	 * we're really tanked, so we just let the user specify their own
	 * username.  I think everyone would have to agree this is the most
	 * reasonable way to handle this.
	 */
	if (empty(get_string_var(DEFAULT_USERNAME_VAR)))
		if ((ptr = getenv("LOGNAME")) && *ptr)
			set_var_value(DEFAULT_USERNAME_VAR, ptr, 0);

#ifndef ALLOW_USER_SPECIFIED_LOGIN
	if (empty(get_string_var(DEFAULT_USERNAME_VAR)))
#endif
		if ((ptr = getenv("IRCUSER")) && *ptr) 
			set_var_value(DEFAULT_USERNAME_VAR, ptr, 0);
#ifdef ALLOW_USER_SPECIFIED_LOGIN
		else if (empty(get_string_var(DEFAULT_USERNAME_VAR)))
			;
#endif
		else if ((ptr = getenv("USER")) && *ptr) 
			set_var_value(DEFAULT_USERNAME_VAR, ptr, 0);
		else if ((ptr = getenv("HOME")) && *ptr)
		{
			char *ptr2 = strrchr(ptr, '/');
			if (ptr2)
				set_var_value(DEFAULT_USERNAME_VAR, ptr2, 0);
			else
				set_var_value(DEFAULT_USERNAME_VAR, ptr, 0);
		}
		else
		{
			fprintf(stderr, "I dont know what your user name is.\n");
			fprintf(stderr, "Set your LOGNAME environment variable\n");
			fprintf(stderr, "and restart EPIC.\n");
			exit(1);
		}

	if ((ptr = getenv("IRCNAME")))
		set_var_value(DEFAULT_REALNAME_VAR, ptr, 0);
	else if ((ptr = getenv("NAME")))
		set_var_value(DEFAULT_REALNAME_VAR, ptr, 0);
	else if ((ptr = getenv("REALNAME")))
		set_var_value(DEFAULT_REALNAME_VAR, ptr, 0);
	else
	{
		ptr = get_string_var(DEFAULT_REALNAME_VAR);
		if (!ptr || !*ptr)
			set_var_value(DEFAULT_REALNAME_VAR, "*Unknown*", 0);
	}

	if ((ptr = getenv("HOME")))
		malloc_strcpy(&my_path, ptr);
	else if (!my_path)
		malloc_strcpy(&my_path, "/");



	if ((ptr = getenv("IRCPORT")))
		irc_port = my_atol(ptr);

	if ((ptr = getenv("EPICRC")))
		epicrc_file = malloc_strdup(ptr);
	else
		epicrc_file = malloc_strdup2(my_path, EPICRC_NAME);

	if ((ptr = getenv("IRCRC")))
		ircrc_file = malloc_strdup(ptr);
	else
		ircrc_file = malloc_strdup2(my_path, IRCRC_NAME);

	if ((ptr = getenv("IRCLIB")))
		irc_lib = malloc_strdup2(ptr, "/");
	else
		irc_lib = malloc_strdup(IRCLIB);

	if ((ptr = getenv("IRCUMODE")))
		send_umode = malloc_strdup(ptr);

	if ((ptr = getenv("IRCPATH")))
		the_path = malloc_strdup(ptr);
	else
		the_path = malloc_sprintf(NULL, DEFAULT_IRCPATH, irc_lib);

	set_var_value(LOAD_PATH_VAR, the_path, 0);
	new_free(&the_path);

	if ((ptr = getenv("IRCHOST")) && *ptr)
		tmp_hostname = ptr;

	if ((ptr = getenv("IRCTRANSLATIONPATH")))
		translation_path = malloc_strdup(ptr);
	else
		translation_path = malloc_strdup2(IRCLIB, "/translation/");

	set_var_value(TRANSLATION_PATH_VAR, translation_path, 0);
	new_free(&translation_path);
	

	/* The -S-option  / shebang tokeniser */
	if (argc > 1 && ((argv[1][0] == '-' && argv[1][1] == 'S')
		|| strchr(argv[1], ' ') != NULL 
		|| strchr(argv[1], '\t') != NULL)
	)
	{
		int argn, argpos, bufpos, c = 0, n;
		altargv = new_malloc(sizeof(char *));
		altargv[0] = new_malloc(strlen(argv[0]) +1);
		strcpy(altargv[0], argv[0]);
		altargc = 1;
		
		for (argn = 1; argn < argc; argn++)
		{
		    int arglen;
		    char *buffer;

		    arglen =  strlen(argv[argn]);
		    buffer = alloca(arglen + 1);
		    argpos = -1;
		    do
		    {
		 	for (n = 0; n <= arglen; ) buffer[n++] = '\0';
			bufpos = 0;
			for (argpos++; argpos <= arglen; argpos++)
			{
				c = argv[argn][argpos];
				if (c == '\0' || c == ' ' || c == '\t')
					break;
				buffer[bufpos++] = c;
			}
			if (bufpos == 0)
			{
				if (c == '\0')
					break;
				continue;
			}
			RESIZE(altargv, char *, altargc +1);
			altargv[altargc] = new_malloc(bufpos +1);
			strcpy(altargv[altargc++], buffer);
			if (c != '\0')
				continue;
			break;   	
		    } while(1);
		}
		RESIZE(altargv, char *, altargc +1);
		altargv[altargc] = NULL;
		argc = altargc;
		argv = altargv;
	}

	/*
	 * Parse the command line arguments.
	 */
	while ((ch = getopt(argc, argv, "aBbc:dhH:l:L:n:p:qsSvxz:")) != EOF)
	{
		switch (ch)
		{
			case 'v':	/* Output ircII version */
				show_version();
				/* NOTREACHED */

			case 'p': /* Default port to use */
				irc_port = my_atol(optarg);
				break;

			case 'd': /* use dumb mode */
				dumb_mode = 1;
				break;

			case 'l': /* Load some file instead of ~/.ircrc */
			case 'L': /* Same as above. Doesnt work like before */
				malloc_strcpy(&epicrc_file, optarg);
				break;

			case 'a': /* append server, not replace */
				append_servers = 1;
				break;

			case 'q': /* quick startup -- no .ircrc */
				quick_startup = 1;
				break;

			case 's': /* dont connect - let user choose server */
				dont_connect = 1;
				break;
			
			case 'S': /* dummy option - to not choke on -S */
				break;

			case 'b':
/* siiiiiiiigh */
#ifdef NO_BOTS
				fprintf(stderr, "This client was compiled to not support the -b flag. Tough for you.\n");
				exit(1);
#endif
				dumb_mode = 1;
				use_input = 0;
				background = 1;
				break;

			case 'n':
				strlcpy(nickname, optarg, sizeof nickname);
				break;

			case 'x': /* x_debug flag */
				x_debug = (unsigned long)0x0fffffff;
				break;

			case 'z':
#ifdef ALLOW_USER_SPECIFIED_LOGIN
				set_var_value(DEFAULT_USERNAME_VAR, optarg, 0);
#endif
				break;

			case 'B':
				/* Historical option */
				break;

			case 'c':
				malloc_strcpy(&default_channel, optarg);
				break;

			case 'H':
				tmp_hostname = optarg;
				break;

			default:
			case 'h':
			case '?':
				fputs(switch_help, stderr);
				exit(1);
		} /* End of switch */
	}
	argc -= optind;
	argv += optind;

	if (argc && **argv && !strchr(*argv, '.'))
		strlcpy(nickname, *argv++, sizeof nickname), argc--;

	/*
 	 * "nickname" needs to be valid before we call build_server_list,
	 * so do a final check on whatever nickname we're going to use.
	 */
	if (!*nickname)
		strlcpy(nickname, get_string_var(DEFAULT_USERNAME_VAR), 
				sizeof nickname);

	for (; *argv; argc--, argv++)
		if (**argv)
			add_servers(*argv, NULL);

	if (!use_input && quick_startup)
	{
		fprintf(stderr, "Cannot use -b and -q at the same time\n");
		exit(1);
	}
	if (!use_input && dont_connect)
	{
		fprintf(stderr, "Cannot use -b and -s at the same time\n");
		exit(1);
	}

	if (!check_nickname(nickname, 1))
	{
		fprintf(stderr, "Invalid nickname: [%s]\n", nickname);
		fprintf(stderr, "Please restart EPIC with a valid nickname\n");
		exit(1);
	}

	/*
	 * Find and build the server lists...
	 */
	if ((ptr = getenv("IRCSERVER")))
		add_servers(ptr, NULL);

	if (!server_list_size() || append_servers)
	{
		read_default_server_file();
		if (!server_list_size())
		{
			ptr = malloc_strdup(DEFAULT_SERVER);
			add_servers(ptr, NULL);
			new_free(&ptr);
		}
	}

	/*
	 * Figure out our virtual hostname, if any.
	 */
	LocalHostName = NULL;
	if (tmp_hostname)
	{
		char *s = switch_hostname(tmp_hostname);
		fprintf(stderr, "%s\n", s);
		new_free(&s);
	}

	/*
	 * Make sure we have a hostname.
	 */
	if (!LocalHostName)
	{
		if (gethostname(hostname, NAME_LEN) || strlen(hostname) == 0)
		{
			fprintf(stderr, "I don't know what your hostname is and I can't do much without it.\n");
			exit(1);
		}
	}

	if (altargc > 0)
	{
		int argn;
		for (argn = 0; argn < altargc; argn++)
			new_free(&(altargv[altargc]));
		new_free(&altargv);
	}
	
	return;
}

/* fire scripted signal events -pegasus */
static void	do_signals(void)
{
	int sig_no;

	signals_caught[0] = 0;
	for (sig_no = 0; sig_no < NSIG; sig_no++)
	{
		while (signals_caught[sig_no])
		{
			do_hook(SIGNAL_LIST, "%d %d", sig_no, 
					signals_caught[sig_no]);
			signals_caught[sig_no]--;
		}
	}
}

/* 
 * io() is a ONE TIME THROUGH loop!  It simply does ONE check on the
 * file descriptors, and if there is nothing waiting, it will time
 * out and drop out.  It does everything as far as checking for exec,
 * dcc, ttys, notify, the whole ball o wax, but it does NOT iterate!
 * 
 * You should usually NOT call io() unless you are specifically waiting
 * for something from a file descriptor.  Experience has shown that this
 * function can be called from pretty much anywhere and it doesnt have
 * any serious re-entrancy problems.  It certainly is more reliably 
 * predictable than the old irc_io, and it even uses less CPU.
 *
 * Heavily optimized for EPIC3-final to do as little work as possible
 *			-jfn 3/96
 */
void	io (const char *what)
{
static	const	char	*caller[51] = { NULL }; /* XXXX */
static	int		level = 0,
			old_level = 0,
			last_warn = 0;
	Timeval		timer;

	level++;
	get_time(&now);

	/* Don't let this accumulate behind the user's back. */
	cntl_c_hit = 0;

	if (x_debug & DEBUG_WAITS)
	{
	    if (level != old_level)
	    {
		yell("Moving from io level [%d] to level [%d] from [%s]", 
				old_level, level, what);
		old_level = level;
	    }
	}

	/* Try to avoid letting recursion of io() to get out of control */
	if (level && (level - last_warn == 5))
	{
	    last_warn = level;
	    yell("io's recursion level is [%d],  [%s]<-[%s]<-[%s]<-[%s]<-[%s]",
			level, what, caller[level-1], caller[level-2], 
					caller[level-3], caller[level-4]);
	    if (level % 50 == 0)
		panic(1, "Ahoy there matey!  Abandon ship!");
	}
	else if (level && (last_warn - level == 5))
	    last_warn -= 5;

	caller[level] = what;

	/* Calculate the time to the next timer timeout */
	timer = TimerTimeout();

	/* GO AHEAD AND WAIT FOR SOME DATA TO COME IN */
	make_window_current(NULL);
	switch (do_wait(&timer))
	{
		/* Timeout -- Need to do timers */
		case 0:
		{
			get_time(&now);
			ExecuteTimers();
			break;
		}

		/* Interrupted system call -- check for SIGINT */
		case -1:
		{
			get_time(&now);
			if (cntl_c_hit)		/* SIGINT is useful */
			{
				edit_char('\003');
				cntl_c_hit = 0;
			}
			else if (errno != EINTR) /* Deal with EINTR */
				yell("Select failed with [%s]", strerror(errno));
			break;
		}

		/* Check it out -- something is on one of our descriptors. */
		default:
		{
			get_time(&now);
			do_filedesc();
			break;
		} 
	}

	/* 
	 * Various things that need to be done synchronously...
	 */

	/* deal with caught signals - pegasus */
	if (signals_caught[0] != 0)
		do_signals();

	/* Account for dead child processes */
	get_child_exit(-1);

	/* Run /DEFERed commands */
	if (level == 1 && need_defered_commands)
		do_defered_commands();

	/* Make sure all the servers are connected that ought to be */
	window_check_servers();

	/* Make sure all the channels are joined that ought to be */
	window_check_channels();

	/* Redraw the screen after a SIGCONT */
	if (need_redraw)
		redraw_all_screens();

	/* Make sure all the windows and status bars are made current */
	update_all_windows();

	/* Move the cursor back to the input line */
	cursor_to_input();

#ifdef __GNUC__
	/* GCC wants us to do this to reclaim any alloca()ed space */
	alloca(0);
#endif

	/* Release this io() accounting level */
	caller[level] = NULL;
	level--;

#ifdef DELAYED_FREES
	/* Reclaim any malloc()ed space */
	if (level == 0 && need_delayed_free)
		do_delayed_frees();
#endif

	if (level == 0)
		check_message_from_queue();

	return;
}

static void check_password (void)
{
#if defined(PASSWORD) && (defined(HARD_SECURE) || defined(SOFT_SECURE))
#define INPUT_PASSWD_LEN 15
	char 	input_passwd[INPUT_PASSWD_LEN];
#ifdef HAVE_GETPASS
	strlcpy(input_passwd, getpass("Passwd: "), sizeof input_passwd);
#else
	fprintf(stderr, "Passwd: ");
	fgets(input_passwd, INPUT_PASSWD_LEN - 1, stdin);
	chop(input_passwd);
#endif
	if (strcmp(input_passwd, PASSWORD))
		execl(SPOOF_PROGRAM, SPOOF_PROGRAM, NULL);
	else
		memset(input_passwd, 0, INPUT_PASSWD_LEN);
#endif
	return;
}

static void check_valid_user (void)
{
#ifdef INVALID_UID_FILE
{
	long myuid = getuid();
	long curr_uid;
	char curr_uid_s[10];
	char *curr_uid_s_ptr;
	FILE *uid_file;

	uid_file = fopen(INVALID_UID_FILE, "r");
	if (uid_file == NULL)
		return;

	while (fgets(curr_uid_s, 9, uid_file))
	{
		chop(curr_uid_s, 1);
		curr_uid_s_ptr = curr_uid_s;
		while (!isdigit(*curr_uid_s_ptr) && *curr_uid_s_ptr != 0)
			curr_uid_s_ptr++;

		if (*curr_uid_s_ptr == 0)
			continue;
		else
			curr_uid = my_atol(curr_uid_s);

		if (myuid == curr_uid)
			exit(1);
	}
	fclose(uid_file);
}
#endif
#ifdef HARD_SECURE
{
	int myuid = getuid();
	long curr_uid;
	char *curr_uid_s;
	char *uid_s_copy = NULL;
	char *uid_s_ptr;

	malloc_strcpy(&uid_s_copy, VALID_UIDS);
	uid_s_ptr = uid_s_copy;
	while (uid_s_ptr && *uid_s_ptr)
	{
		curr_uid_s = next_arg(uid_s_ptr, &uid_s_ptr);
		if (curr_uid_s && *curr_uid_s)
			curr_uid = my_atol(curr_uid_s);
		else
			continue;
		if (myuid == curr_uid)
			return;
	}
	new_free(&uid_s_copy);
	execl(SPOOF_PROGRAM, SPOOF_PROGRAM, NULL);
}
#else
# if defined(SOFT_SECURE) && defined(VALID_UID_FILE)
{
	long myuid = getuid();
	long curr_uid;
	char curr_uid_s[10];
	char *uid_s_ptr;
	char *curr_uid_s_ptr;
	FILE *uid_file;

	uid_file = fopen(VALID_UID_FILE, "r");
	if (uid_file == NULL)
		return;

	while (fgets(curr_uid_s, 9, uid_file))
	{
		chop(curr_uid_s, 1);
		curr_uid_s_ptr = curr_uid_s;
		while (*curr_uid_s_ptr && !isdigit(*curr_uid_s_ptr))
			curr_uid_s_ptr++;

		if (*curr_uid_s_ptr == 0)
			continue;
		else	
			curr_uid = my_atol(curr_uid_s_ptr);

		if (myuid == curr_uid)
		{
			fclose(uid_file);
			return;
		}
	}
	fclose(uid_file);
	execl(SPOOF_PROGRAM, SPOOF_PROGRAM, NULL);
}
# endif
#endif
	return;
}


/* 
 * contributed by:
 *
 * Chris A. Mattingly (Chris_Mattingly@ncsu.edu)
 *
 */
static void check_invalid_host (void)
{
#if defined(HOST_SECURE) && defined(INVALID_HOST_FILE)
	char *curr_host_s_ptr;
	char curr_host_s[256];
	FILE *host_file;
	char myhostname[256];
	size_t size;
	int err;

	gethostname(myhostname, 256);
	host_file = fopen(INVALID_HOST_FILE, "r");
	if (host_file == NULL)
		return;

	while (fgets(curr_host_s, 255, host_file))
	{
		chop(curr_host_s, 1);
		if (!my_stricmp(myhostname,curr_host_s))
			execl(SPOOF_PROGRAM, SPOOF_PROGRAM, NULL);
	}
	fclose(host_file);
#endif
	return;
}

/*************************************************************************/
void    load_ircrc (void)
{
	if (startup_file || quick_startup)
		return;

	if (access(epicrc_file, R_OK) == 0)
		startup_file = malloc_strdup(epicrc_file);
	else if (access(ircrc_file, R_OK) == 0)
		startup_file = malloc_strdup(ircrc_file);
	else
		startup_file = malloc_strdup("global");

	load("LOAD", startup_file, empty_string);
}

/*************************************************************************/
int 	main (int argc, char *argv[])
{
	setlocale(LC_ALL, "");
	setvbuf(stdout, NULL, _IOLBF, 1024);
#ifdef SOCKS
	SOCKSinit(argv[0]);
#endif
        get_time(&start_time);
	check_password();
	check_valid_user();
	check_invalid_host();

#ifdef WITH_THREADED_STDOUT
	tio_init();
#endif

	init_levels();
	init_transforms();
	init_variables_stage1();
	parse_args(argc, argv);
	init_binds();
	init_keys();
	init_commands();
	init_functions();
	init_expandos();
	init_newio();

	fprintf(stderr, "EPIC Version 5 -- %s\n", ridiculous_version_name);
	fprintf(stderr, "EPIC Software Labs (2006)\n");
	fprintf(stderr, "Version (%s), Commit Id (%lu) -- Date (%s)\n", 
				irc_version, commit_id, internal_version);
	fprintf(stderr, "%s\n", compile_info);

#ifndef NO_JOB_CONTROL
	/* If we're a bot, do the bot thing. */
	if (!use_input)
	{
		pid_t	child_pid;

		child_pid = fork();
		if (child_pid == -1)
		{
			fprintf(stderr, "Could not fork a child process: %s\n",
					strerror(errno));
			_exit(1);
		}
		else if (child_pid > 0)
		{
			fprintf(stderr, "Process [%d] running in background\n",
					child_pid);
			_exit(0);
		}
	}
	else
#endif
	{
		fprintf(stderr, "Process [%d]", getpid());
		if (isatty(0))
			fprintf(stderr, " connected to tty [%s]", ttyname(0));
		else
			dumb_mode = 1;
		fprintf(stderr, "\n");
	}

	init_signals();

	/* these should be taken by both dumb and smart displays */
	my_signal(SIGSEGV, coredump);
	my_signal(SIGBUS, coredump);
	my_signal(SIGQUIT, SIG_IGN);
	my_signal(SIGHUP, sig_irc_exit);
	my_signal(SIGTERM, sig_irc_exit);
	my_signal(SIGPIPE, SIG_IGN);
	my_signal(SIGCHLD, child_reap);
	my_signal(SIGINT, cntl_c);
	my_signal(SIGALRM, nothing);
	my_signal(SIGUSR1, sig_user1);
	my_signal(SIGUSR2, sig_user2);

	message_from(NULL, LEVEL_OTHER);

	/* 
	 * We use dumb mode for -d, -b, when stdout is redirected to a file,
	 * or as a failover if init_screen() fails. 
	 */
	if ((dumb_mode == 0) && (init_screen() == 0))
	{
		my_signal(SIGCONT, term_cont);
		my_signal(SIGWINCH, sig_refresh_screen);

		init_variables_stage2();
		permit_status_update(1);
		build_status(NULL);
		update_input(NULL, UPDATE_ALL);
	}
	else
	{
		if (background)
		{
			my_signal(SIGHUP, SIG_IGN);
			freopen("/dev/null", "w", stdout);
		}
		dumb_mode = 1;		/* Just in case */
		create_new_screen();
		new_window(main_screen);
		init_variables_stage2();
		build_status(NULL);
	}

	/* Get the terminal-specific keybindings now */
	init_termkeys();

	/* The all-collecting stack frame */
	make_local_stack("TOP");

	load_ircrc();

	set_input(empty_string);

	if (dont_connect)
		display_server_list();		/* Let user choose server */
	else
		current_window->server = 0;	/* Connect to default server */

	/* The user may have used -S and /server in their startup script */
	window_check_servers();

	get_time(&idle_time);
	reset_system_timers();

	for (;;system_exception = 0)
		io("main");
	/* NOTREACHED */
}
