/*
 * ircII: a new irc client.  I like it.  I hope you will too!
 *
 * Written By Michael Sandrof
 * Copyright(c) 1990 
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 *
 * Reformatted by Matthew Green, November 1992.
 */
#include "irc.h"


/*
 * irc_version is what $J returns, its the common-name for the version.
 */
const char irc_version[] = "EPIC4-1.1.1";
const char useful_info[] = "epic4 1 1 1";

/*
 * internal_version is what $V returns, its the integer-id for the
 * version, and corresponds to the date of release, YYYYMMDD.
 */ 
const char internal_version[] = "20011111";

/*
 * In theory, this number is incremented for every commit.
 */
const unsigned long	commit_id = 126;

/*
 * As a way to poke fun at the current rage of naming releases after
 * english words, which often have little or no correlation with outside
 * reality, I have decided to start doing that with EPIC.  These names
 * are intentionally and maliciously silly.  Complaints will be ignored.
 */
const char ridiculous_version_name[] = "Ponderous";

#define __need_putchar_x__
#include "status.h"
#include "dcc.h"
#include "names.h"
#include "vars.h"
#include "input.h"
#include "alias.h"
#include "output.h"
#include "term.h"
#include "exec.h"
#include "screen.h"
#include "log.h"
#include "server.h"
#include "hook.h"
#include "keys.h"
#include "ircaux.h"
#include "commands.h"
#include "window.h"
#include "history.h"
#include "exec.h"
#include "notify.h"
#include "mail.h"
#include "timer.h"
#include "newio.h"
#include "parse.h"
#include "notice.h"
#include <pwd.h>


/*
 * Global variables
 */

/* The ``DEFAULT'' port used for irc server connections. */
int		irc_port = IRC_PORT;

/* Set if ircII should usurp flow control, unset if not.  Probably bogus. */
int		use_flow_control = 1;

/* 
 * When a numeric is being processed, this holds the negative value
 * of the numeric.  Its negative so if we pass it to do_hook, it can
 * tell its a numeric and not a named ON event.
 */
int		current_numeric;

/* Set if the client is not using a termios interface */
int		dumb_mode = 0;

/* Set if the client is supposed to fork(). (a bot.)  Probably bogus. */
int		background = 0;

/* Set if the client is checking fd 0 for input. (usu. op of "background") */
int		use_input = 1;

/* The number of WAIT tokens sent out so far */
int		waiting_out = 0;

/* The number of WAIT tokens returned so far */
int		waiting_in = 0;

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
time_t		idle_time = 0;

/* Set to the time the client booted up */
time_t		start_time;

/* The number of child processes still unreaped. */
int		child_dead = 0;

/* Set if the current output is from a trusted source */
int		trusted_output = 1;

/* Whether or not we're in CPU saver mode */
int		cpu_saver = 0;

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

/* This is 0 until your ~/.ircrc is loaded */
int		ircrc_loaded = 0;

/* This is 0 unless you specify the -B command line flag */
int		load_ircrc_right_away = 0;

/* This is 1 if you want all logging to be inhibited. Dont leave this on! */
int		inhibit_logging = 0;

/* This is 1 if we're currently loading the "global" script, 0 afterwards */
int		loading_global = 0;

/* This is reset every time io() is called.  Use this to save calls to time */
time_t		now = 0;

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
char		*LocalHostName = NULL;
struct	in_addr	LocalHostAddr;

int		inbound_line_mangler = 0,
		outbound_line_mangler = 0;

char		*invite_channel = (char *) 0,	/* last channel of an INVITE */
		*ircrc_file = (char *) 0,	/* full path .ircrc file */
		*my_path = (char *) 0,		/* path to users home dir */
		*irc_lib = (char *) 0,		/* path to the ircII library */
		*default_channel = NULL,	/* Channel to join on connect */
		nickname[NICKNAME_LEN + 1],	/* users nickname */
		hostname[NAME_LEN + 1],		/* name of current host */
		realname[REALNAME_LEN + 1],	/* real name of user */
		username[NAME_LEN + 1],		/* usernameof user */
		userhost[NAME_LEN + 1],		/* userhost of user */
		*send_umode = NULL,		/* sent umode */
		*last_notify_nick = (char *) 0,	/* last detected nickname */
		empty_string[] = "",		/* just an empty string */
		space[] = " ",			/* just a lonely space */
		on[] = "ON",
		off[] = "OFF",
		zero[] = "0",
		one[] = "1",
		star[] = "*",
		dot[] = ".",
		comma[] = ",",
		*cut_buffer = (char *) 0;	/* global cut_buffer */

fd_set		readables, writables;


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
      -f\tThe program wont mess with your flow control                \n\
      -F\tThe program will mess with your flow control                \n\
      -h\tPrint this help message                                     \n\
      -q\tThe program will not load your .ircrc file                  \n\
      -s\tThe program will not connect to a server upon startup       \n\
      -v\tPrint the version of this irc client and exit               \n\
      -x\tRun the client in full X_DEBUG mode                         \n\
      -c <chan>\tJoin <chan> after first connection to a server       \n\
      -H <host>\tUse a virtual host instead of default hostname	      \n\
      -l <file>\tLoads <file> instead of your .ircrc file             \n\
      -L <file>\tLoads <file> instead of your .ircrc file             \n\
      -n <nick>\tThe program will use <nick> as your default nickname \n\
      -p <port>\tThe program will use <port> as the default portnum   \n\
      -z <user>\tThe program will use <user> as your default username \n";



SIGNAL_HANDLER(sig_irc_exit)
{
	irc_exit (1, NULL);
}

/* irc_exit: cleans up and leaves */
void	irc_exit (int really_quit, char *format, ...)
{
	char 	buffer[BIG_BUFFER_SIZE];
	char *	sub_format;
	int	old_window_display = window_display;
#ifdef PERL
	extern void perlstartstop(int);
#endif

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
		vsnprintf(buffer, BIG_BUFFER_SIZE - 1, format, arglist);
		va_end(arglist);
	}
	else
	{
		if (!(format = get_string_var(QUIT_MESSAGE_VAR)))
			format = "%s";

		sub_format = convert_sub_format(format, 's');
		snprintf(buffer, BIG_BUFFER_SIZE - 1, sub_format, irc_version);
		new_free(&sub_format);
	}


	/* Do some clean up */
	do_hook(EXIT_LIST, "%s", buffer);
#ifdef PERL
	perlstartstop(0);  /* In case there's perl code in the exit hook. */
#endif
	close_all_servers(buffer);
	logger(0);
	clean_up_processes();

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
	dumpcmd(NULL, NULL, NULL);
	set_lastlog_size(0);
	set_history_size(0);
	remove_channel(NULL, 0);
	destroy_call_stack();
	remove_bindings();
	delete_all_windows();
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
SIGNAL_HANDLER(child_reap)
{
	dead_children_processes++;
}

volatile int	segv_recurse = 0;

/* sigsegv: something to handle segfaults in a nice way */
SIGNAL_HANDLER(coredump)
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
IRC-II has trapped a critical protection error.				\n\
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
This version of IRC II is  --->[%s]					\n\
The date of release is     --->[%s]					\n\
									\n\
* * * * * * * * * * * * * * * * * * * * * * * *				\n\
The program will now terminate.						\n", irc_version, internal_version);

		fflush(stdout);
		panic_dump_call_stack();
	}

        if (x_debug & DEBUG_CRASH)
                irc_exit(0, "Hmmm. %s has another bug.  Go figure...",
			irc_version);
        else
                irc_exit(1, "Hmmm. %s has another bug.  Go figure...",
			irc_version);
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
void irc_quit (char unused, char *not_used)
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
SIGNAL_HANDLER(cntl_c)
{
	/* after 5 hits, we stop whatever were doing */
	if (cntl_c_hit++ >= 4)
		irc_exit(1, "User pressed ^C five times.");
	else if (cntl_c_hit > 1)
		kill(getpid(), SIGALRM);
}

SIGNAL_HANDLER(nothing)
{
	/* nothing to do! */
}

SIGNAL_HANDLER(sig_user1)
{
	say("Got SIGUSR1, closing DCC connections and EXECed processes");
	close_all_dcc();
	clean_up_processes();
}

static	void	show_version (void)
{
	printf("ircII %s (Date of release: %s)\n\r",irc_version,internal_version);
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
	int add_servers = 0;
	struct passwd *entry;
	struct hostent *hp;
	char *ptr = (char *) 0;
	char *tmp_hostname = NULL;
	char *irc_path = NULL;
	char *translation_path = NULL;

	extern char *optarg;
	extern int optind;

	*nickname = 0;
	*realname = 0;
	*username = 0;

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
			strmcpy(realname, entry->pw_gecos, REALNAME_LEN);
		}

		if (entry->pw_name && *(entry->pw_name))
			strmcpy(username, entry->pw_name, NAME_LEN);

		if (entry->pw_dir && *(entry->pw_dir))
			malloc_strcpy(&my_path, entry->pw_dir);
	}


	if ((ptr = getenv("IRCNICK")))
		strmcpy(nickname, ptr, NICKNAME_LEN);

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
	if (!*username)
		if ((ptr = getenv("LOGNAME")) && *ptr)
			strmcpy(username, ptr, NAME_LEN);

#ifndef ALLOW_USER_SPECIFIED_LOGIN
	if (!*username)
#endif
		if ((ptr = getenv("IRCUSER")) && *ptr) 
			strmcpy(username, ptr, NAME_LEN);
#ifdef ALLOW_USER_SPECIFIED_LOGIN
		else if (*username)
			;
#endif
		else if ((ptr = getenv("USER")) && *ptr) 
			strmcpy(username, ptr, NAME_LEN);
		else if ((ptr = getenv("HOME")) && *ptr)
		{
			char *ptr2 = strrchr(ptr, '/');
			if (ptr2)
				strmcpy(username, ptr2, NAME_LEN);
			else
				strmcpy(username, ptr, NAME_LEN);
		}
		else
		{
			fprintf(stderr, "I dont know what your user name is.\n");
			fprintf(stderr, "Set your LOGNAME environment variable\n");
			fprintf(stderr, "and restart IRC II.\n");
			exit(1);
		}

	if ((ptr = getenv("IRCNAME")))
		strmcpy(realname, ptr, REALNAME_LEN);
	else if ((ptr = getenv("NAME")))
		strmcpy(realname, ptr, REALNAME_LEN);
	else if (!*realname)
		strmcpy(realname, "*Unknown*", REALNAME_LEN);

	if ((ptr = getenv("HOME")))
		malloc_strcpy(&my_path, ptr);
	else if (!my_path)
		malloc_strcpy(&my_path, "/");



	if ((ptr = getenv("IRCPORT")))
		irc_port = my_atol(ptr);

	if ((ptr = getenv("IRCRC")))
		ircrc_file = m_strdup(ptr);
	else
		ircrc_file = m_2dup(my_path, IRCRC_NAME);

	if ((ptr = getenv("IRCLIB")))
		irc_lib = m_2dup(ptr, "/");
	else
		irc_lib = m_strdup(IRCLIB);

	if ((ptr = getenv("IRCUMODE")))
		send_umode = m_strdup(ptr);

	if ((ptr = getenv("IRCPATH")))
		irc_path = m_strdup(ptr);
	else
		irc_path = m_sprintf(DEFAULT_IRCPATH, irc_lib);

	set_string_var(LOAD_PATH_VAR, irc_path);
	new_free(&irc_path);

	if ((ptr = getenv("IRCHOST")) && *ptr)
		tmp_hostname = ptr;

	if ((ptr = getenv("IRCTRANSLATIONPATH")))
		translation_path = m_strdup(ptr);
	else
		translation_path = m_2dup(IRCLIB, "/translation/");

	set_string_var(TRANSLATION_PATH_VAR, translation_path);
	new_free(&translation_path);

	/*
	 * Parse the command line arguments.
	 */
	while ((ch = getopt(argc, argv, "aBbc:dfFhH:l:L:n:p:qsvxz:")) != EOF)
	{
		switch (ch)
		{
			case 'v':	/* Output ircII version */
				show_version();
				/* NOTREACHED */

			case 'p': /* Default port to use */
				irc_port = my_atol(optarg);
				break;

			case 'f': /* Use flow control */
				use_flow_control = 1;
				break;

			case 'F': /* dont use flow control */
				use_flow_control = 0;
				break;

			case 'd': /* use dumb mode */
				dumb_mode = 1;
				break;

			case 'l': /* Load some file instead of ~/.ircrc */
			case 'L': /* Same as above. Doesnt work like before */
				malloc_strcpy(&ircrc_file, optarg);
				break;

			case 'a': /* add server, not replace */
				add_servers = 1;
				break;

			case 'q': /* quick startup -- no .ircrc */
				quick_startup = 1;
				break;

			case 's': /* dont connect - let user choose server */
				dont_connect = 1;
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
				strmcpy(nickname, optarg, NICKNAME_LEN);
				break;

			case 'x': /* x_debug flag */
				x_debug = (unsigned long)0x0fffffff;
				break;

			case 'z':
#ifdef ALLOW_USER_SPECIFIED_LOGIN
				strmcpy(username, optarg, NAME_LEN);
#endif
				break;

			case 'B':
				load_ircrc_right_away = 1;
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
		strmcpy(nickname, *argv++, NICKNAME_LEN), argc--;

	/*
 	 * "nickname" needs to be valid before we call build_server_list,
	 * so do a final check on whatever nickname we're going to use.
	 */
	if (!*nickname)
		strmcpy(nickname, username, NICKNAME_LEN);

	for (; *argv; argc--, argv++)
		if (**argv)
			build_server_list(*argv, NULL);



	if (!check_nickname(nickname, 1))
	{
		fprintf(stderr, "Invalid nickname: [%s]\n", nickname);
		fprintf(stderr, "Please restart IRC II with a valid nickname\n");
		exit(1);
	}

	/*
	 * Find and build the server lists...
	 */
	if ((ptr = getenv("IRCSERVER")))
		build_server_list(ptr, NULL);

	if (!server_list_size() || add_servers)
	{
		read_server_file();
		if (!server_list_size())
		{
			ptr = m_strdup(DEFAULT_SERVER);
			build_server_list(ptr, NULL);
			new_free(&ptr);
		}
	}

	/*
	 * Figure out our virtual hostname, if any.
	 */
	if (tmp_hostname)
	{
		fprintf(stderr, "You specified a hostname of [%s]\n", tmp_hostname);
		memset(&LocalHostAddr, 0, sizeof(LocalHostAddr));
		if ((hp = gethostbyname(tmp_hostname)))
		{
			memmove((void *)&LocalHostAddr, hp->h_addr, sizeof(LocalHostAddr));
			fprintf(stderr, "Ok.  I'll buy that.\n");
			LocalHostName = m_strdup(tmp_hostname);
		}
		else
			fprintf(stderr, "I can't configure for that address! (Trying normal hostname...)\n");
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

	
	return;
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
	static	int	first_time = 1,
			level = 0;
static	struct	timeval	clock_timeout,
			right_away,
			timer,
			*timeptr = NULL;
		int	hold_over;
		int	dccs;
		fd_set	rd;

	static	int	old_level = 0;
	static	int	last_warn = 0;

	level++;
	now = time(NULL);

	/* Don't let this accumulate behind the user's back. */
	cntl_c_hit = 0;

	if (x_debug & DEBUG_WAITS)
	{
		if (level != old_level)
		{
			yell("Moving from io level [%d] to level [%d] from [%s]", old_level, level, what);
			old_level = level;
		}
	}

	if (level && (level - last_warn == 5))
	{
		last_warn = level;
		yell("io's nesting level is [%d],  [%s]<-[%s]<-[%s]<-[%s]<-[%s]", level, what, caller[level-1], caller[level-2], caller[level-3], caller[level-4]);
		if (level % 50 == 0)
			panic("Ahoy there matey!  Abandon ship!");
	}
	else if (level && (last_warn - level == 5))
		last_warn -= 5;


	caller[level] = what;

	/* first time we run this function, set up the timeouts */
	if (first_time)
	{
		first_time = 0;

		/*
		 * time delay for updating of internal clock
		 *
		 * Instead of looking every 15 seconds and seeing if
		 * the clock has changed, we now figure out how much
		 * time there is to the next clock change and then wait
		 * until then.  There is a small performance penalty 
		 * in actually calculating when the next minute will tick, 
		 * but that will be offset by the fact that we will only
		 * call select() once a minute instead of 4 times.
		 * Plus you get the benefit of the clock actually changing
		 * on time rather than up to 15 seconds later.
		 */
		clock_timeout.tv_usec = 0L;

		right_away.tv_usec = 0L;
		right_away.tv_sec = 0L;

		timer.tv_usec = 0L;
	}

	/* CHECK FOR CPU SAVER MODE */
	if (!cpu_saver && get_int_var(CPU_SAVER_AFTER_VAR))
		if (now - idle_time > get_int_var(CPU_SAVER_AFTER_VAR) * 60)
			cpu_saver_on(0, NULL);

	/* SET UP FD SETS */
	rd = readables;

	clock_timeout.tv_sec = 60 - now % 60;
	if (cpu_saver && get_int_var(CPU_SAVER_EVERY_VAR))
		clock_timeout.tv_sec += (get_int_var(CPU_SAVER_EVERY_VAR) - 1) * 60;

	if (!timeptr)
		timeptr = &clock_timeout;
	timer.tv_sec = TimerTimeout();
	if (timer.tv_sec <= timeptr->tv_sec)
		timeptr = &timer;

	if ((hold_over = unhold_windows()))
		timeptr = &right_away;
	if ((dccs = dcc_dead()))		/* XXX HACK! XXX */
		timeptr = &right_away;

	/* GO AHEAD AND WAIT FOR SOME DATA TO COME IN */
	switch (new_select(&rd, NULL, timeptr))
	{
		/* Timeout -- nothing interesting. */
		case 0:
			break;

		/* Interrupted system call -- check for SIGINT */
		case -1:
		{
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
			make_window_current(NULL);
			dcc_check(&rd);
			do_server(&rd);
			do_processes(&rd);
			do_screens(&rd);
			break;
		} 
	}

	if (dccs)			/* XXX HACK XXX */
		dcc_check(&rd);		/* XXX HACK XXX */

	ExecuteTimers();
	get_child_exit(-1);
	if (level == 1 && need_defered_commands)
		do_defered_commands();

	cursor_to_input();
	timeptr = &clock_timeout;

	if (update_clock(RESET_TIME))
	{
		/* 
		 * Do notify first so the server is working on it
		 * while we do the other crap.
		 */
		do_notify();

		if (get_int_var(CLOCK_VAR) || check_mail_status(NULL))
		{
			update_all_status();
			cursor_to_input();
		}
	}

	/* (set in term.c) -- we need to redraw the screen */
	if (need_redraw)
		refresh_screen('\0', NULL);

	window_check_channels();
	alloca(0);
	caller[level] = NULL;
	level--;

#ifdef DELAYED_FREES
	if (level == 0 && need_delayed_free)
		do_delayed_frees();
#endif
	return;
}

static void check_password (void)
{
#if defined(PASSWORD) && (defined(HARD_SECURE) || defined(SOFT_SECURE))
#define INPUT_PASSWD_LEN 15
	char 	input_passwd[INPUT_PASSWD_LEN];
#ifdef HAVE_GETPASS
	strlcpy(input_passwd, getpass("Passwd: "), INPUT_PASSWD_LEN);
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

/*
 * I moved this here, because it didnt really belong in status.c
 */
/* 
 * XXX This is bogus -- it gets set when you do /set clock_format.
 * The reason we use a global variable is to cut down on the number of
 * calls to get_string_var, which is truly bogus, but neccesary for any
 * semblance of efficiency.
 */
	char	*time_format = (char *) 0;	/* XXX Bogus XXX */
static	char	*strftime_24hour = "%R";
static	char	*strftime_12hour = "%I:%M%p";
	int	broken_clock = 0;

/* update_clock: figures out the current time and returns it in a nice format */
char	*update_clock (int flag)
{
static		char	time_str[61];
static		int	min = -1;
static		int	hour = -1;

	struct	timeval	tv;
static 	struct 	tm	time_val;
static		time_t	last_minute = -1;
		time_t	hideous;
		int	new_minute = 0;

	if (x_debug & DEBUG_BROKEN_CLOCK)
	{
		snprintf(time_str, 60, "%c12%c:%c00AM%c",
			BLINK_TOG, BLINK_TOG, BLINK_TOG, BLINK_TOG);

		if (flag == GET_TIME)
			return(time_str);
		else
			return NULL;
	}

	/*
	 * This is cheating because we only call localtime() once per minute.
	 * This implicitly assumes that you only want to get the localtime
	 * info once every minute, which we do.  If you wanted to get it every
	 * second (which we dont), you DONT WANT TO DO THIS!
	 */
	get_time(&tv);
	hideous = tv.tv_sec;
#ifndef NO_CHEATING
	if (hideous / 60 != last_minute)
	{
		last_minute = hideous / 60;
		time_val = *localtime(&hideous);
	}
#else
	time_val = *localtime(&hideous);
#endif

	if (flag == RESET_TIME || time_val.tm_min != min || time_val.tm_hour != hour)
	{
		if (time_format)	/* XXX Bogus XXX */
			strftime(time_str, 60, time_format, &time_val);
		else if (get_int_var(CLOCK_24HOUR_VAR))
			strftime(time_str, 60, strftime_24hour, &time_val);
		else
			strftime(time_str, 60, strftime_12hour, &time_val);

		if ((time_val.tm_min != min) || (time_val.tm_hour != hour))
		{
			int	old_server = from_server;

			new_minute = 1;
			hour = time_val.tm_hour;
			min = time_val.tm_min;

			from_server = primary_server;
			do_hook(TIMER_LIST, "%02d:%02d", hour, min);
			do_hook(IDLE_LIST, "%ld", (tv.tv_sec - idle_time) / 60);
			from_server = old_server;
		}

		if (flag != RESET_TIME || new_minute)
			return time_str;
		else
			return NULL;
	}

	if (flag == GET_TIME)
		return time_str;
	else
		return NULL;
}

void	reset_clock (char *unused)
{
	update_clock(RESET_TIME);
	update_all_status();
}


int 	main (int argc, char *argv[])
{
#ifdef SOCKS
	SOCKSinit(argv[0]);
#endif
        start_time = time(NULL);
	check_password();
	check_valid_user();
	check_invalid_host();
	parse_args(argc, argv);
	init_keys();

	fprintf(stderr, "EPIC Version 4 -- %s\n", ridiculous_version_name);
	fprintf(stderr, "EPIC Software Labs (2000)\n");
	fprintf(stderr, "Version (%s) -- Date (%s)\n", irc_version, internal_version);
	fprintf(stderr, "%s\n", compile_info);
	fprintf(stderr, "Process [%d]", getpid());
	if (isatty(0))
		fprintf(stderr, " connected to tty [%s]", ttyname(0));
	else
		dumb_mode = 1;
	fprintf(stderr, "\n");

	FD_ZERO(&readables);
	FD_ZERO(&writables);

	/* If we're a bot, do the bot thing. */
	if (!use_input && fork())
		_exit(0);

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

	if ((dumb_mode == 0) && (init_screen() == 0))
	{
		my_signal(SIGCONT, term_cont);
		my_signal(SIGWINCH, sig_refresh_screen);
		init_variables();
		build_status(NULL);
		update_input(UPDATE_ALL);
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
		init_variables();
		build_status(NULL);
	}

	/* Get the terminal-specific keybindings now */
	init_keys2();

	/* The all-collecting stack frame */
	make_local_stack("TOP");

	/* XXXX Move this somewhere else eventually XXXX */
	if (load_ircrc_right_away)
	{
		char	buffer[7];
		strcpy(buffer, "global");

		loading_global = 1;
		load("LOAD", buffer, empty_string);
		loading_global = 0;

		/* read the .ircrc file */
		if (access(ircrc_file, R_OK) == 0 && !quick_startup)
			load("LOAD", ircrc_file, empty_string);

		ircrc_loaded = 1;
	}

	if (dont_connect)
		display_server_list();		/* Let user choose server */
	else
		reconnect(-1, 0);		/* Connect to default server */

	time(&idle_time);
	set_input(empty_string);
	set_input_prompt(get_string_var(INPUT_PROMPT_VAR));
	for (;;)
		io("main");
	/* NOTREACHED */
}
