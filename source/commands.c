/*
 * commands.c -- Stuff needed to execute commands in ircII.
 *		 Includes the bulk of the built in commands for ircII.
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright 1995, 2015 EPIC Software Labs
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
#define __need_term_h__
#define __need_putchar_x__
#define __need_term_flush__
#include "irc.h"
#define __need_ArgList_t__
#include "alias.h"
#include "alist.h"
#include "sedcrypt.h"
#include "ctcp.h"
#include "dcc.h"
#include "commands.h"
#include "exec.h"
#include "files.h"
#include "hook.h"
#include "server.h"
#include "ifcmd.h"
#include "ignore.h"
#include "input.h"
#include "ircaux.h"
#include "keys.h"
#include "lastlog.h"
#include "log.h"
#include "names.h"
#include "notify.h"
#include "numbers.h"
#include "output.h"
#include "parse.h"
#include "queue.h"
#include "screen.h"
#include "status.h"
#include "stack.h"
#include "termx.h"
#include "timer.h"
#include "vars.h"
#include "window.h"
#include "who.h"
#include "newio.h"
#include "words.h"
#include "reg.h"
#include "extlang.h"
#include "elf.h"

/* used with input_move_cursor */
#define RIGHT 1
#define LEFT 0

/* The maximum number of recursive LOAD levels allowed */
#define MAX_LOAD_DEPTH 10

/* flags used by e_away */
#define AWAY_ONE                        0
#define AWAY_ALL                        1

/* Used to handle and catch breaks and continues */
	int	will_catch_break_exceptions = 0;
	int	will_catch_continue_exceptions = 0;
	int	will_catch_return_exceptions = 0;
	int	break_exception = 0;
	int	continue_exception = 0;
	int	return_exception = 0;
	int	system_exception = 0;

/* commands and whatnot */
static  void    abortcmd 	(const char *, char *, const char *);
static	void	away 		(const char *, char *, const char *);
static	void	beepcmd 	(const char *, char *, const char *);
static	void	blesscmd	(const char *, char *, const char *);
static	void	botmodecmd	(const char *, char *, const char *);
static	void	breakcmd	(const char *, char *, const char *);
static	void	commentcmd 	(const char *, char *, const char *);
static	void	continuecmd	(const char *, char *, const char *);
static	void	ctcp 		(const char *, char *, const char *);
	void	debuglogcmd	(const char *, char *, const char *);
static	void	deop 		(const char *, char *, const char *);
static	void	send_to_channel_first	(const char *, char *, const char *);
static	void	send_to_query_first	(const char *, char *, const char *);
static	void	sendlinecmd 	(const char *, char *, const char *);
static	void	echocmd		(const char *, char *, const char *);
static	void	funny_stuff 	(const char *, char *, const char *);
static	void	cd 		(const char *, char *, const char *);
static	void	defercmd	(const char *, char *, const char *);
static	void	describe 	(const char *, char *, const char *);
static	void	e_call		(const char *, char *, const char *);
static	void	e_clear		(const char *, char *, const char *);
static	void	e_hostname	(const char *, char *, const char *);
static	void	e_nick 		(const char *, char *, const char *);
static	void	e_privmsg 	(const char *, char *, const char *);
static	void	e_pause		(const char *, char *, const char *);
static	void	e_quit 		(const char *, char *, const char *);
static	void	e_topic 	(const char *, char *, const char *);
static	void	e_wallop 	(const char *, char *, const char *);
static	void	evalcmd 	(const char *, char *, const char *);
static	void	flush 		(const char *, char *, const char *);
static	void	hookcmd		(const char *, char *, const char *);
static	void	info 		(const char *, char *, const char *);
static	void	inputcmd 	(const char *, char *, const char *);
static	void	license		(const char *, char *, const char *);
static	void	mecmd 		(const char *, char *, const char *);
static	void	oper 		(const char *, char *, const char *);
static	void	packagecmd	(const char *, char *, const char *);
static	void	pingcmd 	(const char *, char *, const char *);
static  void    pop_cmd 	(const char *, char *, const char *);
static	void	pretend_cmd	(const char *, char *, const char *);
static  void    push_cmd 	(const char *, char *, const char *);
static	void	query		(const char *, char *, const char *);
static	void	quotecmd	(const char *, char *, const char *);
static	void	redirect 	(const char *, char *, const char *);
static	void	returncmd	(const char *, char *, const char *);
extern	void	rubycmd		(const char *, char *, const char *);
static	void	send_2comm 	(const char *, char *, const char *);
static	void	send_comm 	(const char *, char *, const char *);
static	void	send_invite 	(const char *, char *, const char *);
static	void	send_kick 	(const char *, char *, const char *);
static	void	send_channel_com (const char *, char *, const char *);
static	void	setenvcmd	(const char *, char *, const char *);
static	void	squitcmd	(const char *, char *, const char *);
static	void	subpackagecmd	(const char *, char *, const char *);
static	void	typecmd 	(const char *, char *, const char *);
static	void	usleepcmd	(const char *, char *, const char *);
static  void	shift_cmd 	(const char *, char *, const char *);
static	void	sleepcmd 	(const char *, char *, const char *);
static	void	stackcmd	(const char *, char *, const char *);
static  void	unshift_cmd 	(const char *, char *, const char *);
static	void	version 	(const char *, char *, const char *);
static 	void	waitcmd 	(const char *, char *, const char *);
static	void	whois 		(const char *, char *, const char *);
static	void	xechocmd 	(const char *, char *, const char *);
static	void	xevalcmd 	(const char *, char *, const char *);
static	void	xtypecmd 	(const char *, char *, const char *);
static	void	allocdumpcmd	(const char *, char *, const char *);

/* other */
static	void	eval_inputlist 	(char *, const char *);
static void	parse_block (const char *, const char *, int interactive);

/* I hate typedefs, but they sure can be useful.. */
typedef void (*CmdFunc) (const char *, char *, const char *);

/* IrcCommand: structure for each command in the command table */
typedef	struct
{
	const char *	name;		/* what the user types */
	CmdFunc 	func;		/* function that is the command */
}	IrcCommand;

/* Current command name, needed for $curcmd() */

const char *current_command = NULL;

/*
 * irc_command: all the availble irc commands:  Note that the first entry has
 * a zero length string name and a null server command... this little trick
 * makes "/ blah blah blah" to always send the arguments to a channel, 
 * bypassing queries, etc.  Neato.  This list MUST be sorted.
 */
static	IrcCommand irc_command[] =
{
	{ "",		send_to_channel_first	},
	{ "#",		commentcmd	},
	{ ":",		commentcmd	},
        { "ABORT",      abortcmd	},
	{ "ADMIN",	send_comm	},
	{ "ALIAS",	aliascmd	}, /* alias.c */
	{ "ALLOCDUMP",	allocdumpcmd	},
	{ "ASSIGN",	assigncmd	}, /* alias.c */
	{ "AWAY",	away		},
	{ "BEEP",	beepcmd		},
	{ "BIND",	bindcmd		}, /* keys.c */
	{ "BLESS",	blesscmd	},
	{ "BOTMODE",	botmodecmd	},
	{ "BREAK",	breakcmd	},
	{ "CALL",	e_call		},
	{ "CD",		cd		},
	{ "CHANNEL",	e_channel	},
	{ "CLEAR",	e_clear		},
	{ "COMMENT",	commentcmd	},
	{ "CONNECT",	send_comm	},
	{ "CONTINUE",	continuecmd	},
	{ "CTCP",	ctcp		},
	{ "DCC",	dcc_cmd		}, /* dcc.c */
	{ "DEBUGLOG",	debuglogcmd	}, /* debuglog.c */
	{ "DEFER",	defercmd	},
	{ "DEOP",	deop		},
	{ "DESCRIBE",	describe	},
	{ "DIE",	send_comm	},
	{ "DISCONNECT",	disconnectcmd	}, /* server.c */
        { "DO",         docmd           }, /* if.c */
        { "DUMP",       dumpcmd		}, /* alias.c */
	{ "ECHO",	echocmd		},
	{ "ENCODING",	encoding	}, /* recode.c */
	{ "ENCRYPT",	encrypt_cmd	}, /* crypt.c */
	{ "EVAL",	evalcmd		},
	{ "EXEC",	execcmd		}, /* exec.c */
	{ "EXIT",	e_quit		},
        { "FE",         fe		}, /* if.c */
        { "FEC",        fe              }, /* if.c */
	{ "FLUSH",	flush		},
        { "FOR",        forcmd		}, /* if.c */
	{ "FOREACH",	foreach		}, /* if.c */
	{ "HOOK",	hookcmd		},
	{ "HOSTNAME",	e_hostname	},
	{ "IF",		ifcmd		}, /* if.c */
	{ "IGNORE",	ignore		}, /* ignore.c */
	{ "INFO",	info		},
	{ "INPUT",	inputcmd	},
	{ "INPUT_CHAR",	inputcmd	},
	{ "INVITE",	send_invite	},
	{ "ISON",	isoncmd		},
	{ "JOIN",	e_channel	},
	{ "KICK",	send_kick	},
	{ "KILL",	send_2comm	},
	{ "KNOCK",	send_channel_com},
	{ "LASTLOG",	lastlog		}, /* lastlog.c */
	{ "LICENSE",	license		},
	{ "LINKS",	send_comm	},
	{ "LIST",	funny_stuff	},
	{ "LOAD",	load		},
	{ "LOCAL",	localcmd	}, /* alias.c */
	{ "LOG",	logcmd		}, /* logfiles.c */
	{ "LUSERS",	send_comm	},
	{ "MAP",	send_comm	},
	{ "ME",		mecmd		},
	{ "MESG",	extern_write	},
	{ "MODE",	send_channel_com},
	{ "MOTD",	send_comm	},
	{ "MSG",	e_privmsg	},
	{ "NAMES",	funny_stuff	},
	{ "NICK",	e_nick		},
	{ "NOTE",	send_comm	},
	{ "NOTICE",	e_privmsg	},
	{ "NOTIFY",	notify		}, /* notify.c */
	{ "ON",		oncmd		}, /* hook.c */
	{ "OPER",	oper		},
	{ "PACKAGE",	packagecmd	},
	{ "PARSEKEY",	parsekeycmd	},
	{ "PART",	send_2comm	},
	{ "PAUSE",	e_pause		},
#ifdef HAVE_PERL
	{ "PERL",	perlcmd		}, /* perl.c */
#endif
	{ "PING",	pingcmd		},
	{ "POP",	pop_cmd		},
	{ "PRETEND",	pretend_cmd	},
	{ "PUSH",	push_cmd	},
#ifdef HAVE_PYTHON
	{ "PYDIRECT",	pydirect_cmd	}, /* python.c */
	{ "PYTHON",	pythoncmd	}, /* python.c */
#endif
	{ "QUERY",	query		},
        { "QUEUE",      queuecmd        }, /* queue.c */
	{ "QUIT",	e_quit		},
	{ "QUOTE",	quotecmd	},
	{ "RBIND",	rbindcmd	}, /* keys.c */
	{ "RECONNECT",  reconnectcmd    }, /* server.c */
	{ "REDIRECT",	redirect	},
	{ "REHASH",	send_comm	},
	{ "REPEAT",	repeatcmd	},
	{ "RESTART",	send_comm	},
	{ "RETURN",	returncmd	},
	{ "RPING",	send_comm	},
#ifdef HAVE_RUBY
	{ "RUBY",	rubycmd		}, /* ruby.c */
#endif
	{ "SAY",	send_to_channel_first	},
	{ "SEND",	send_to_query_first	},
	{ "SENDLINE",	sendlinecmd	},
	{ "SERVER",	servercmd	}, /* server.c */
	{ "SERVLIST",	send_comm	},
	{ "SET",	setcmd		}, /* vars.c */
	{ "SETENV",	setenvcmd	},
	{ "SHIFT",	shift_cmd	},
	{ "SHOOK",	shookcmd	},
	{ "SILENCE",	send_comm	},
	{ "SLEEP",	sleepcmd	},
	{ "SQUERY",	send_2comm	},
	{ "SQUIT",	squitcmd	},
	{ "STACK",	stackcmd	}, /* stack.c */
	{ "STATS",	send_comm	},
	{ "STUB",	stubcmd		}, /* alias.c */
	{ "SUBPACKAGE",	subpackagecmd	},
	{ "SWITCH",	switchcmd	}, /* if.c */
	{ "TIME",	send_comm	},
	{ "TIMER",	timercmd	},
	{ "TOPIC",	e_topic		},
	{ "TRACE",	send_comm	},
	{ "TYPE",	typecmd		}, /* keys.c */
	{ "UNCLEAR",	e_clear		},
	{ "UNLESS",	ifcmd		}, /* if.c */
	{ "UNLOAD",	unloadcmd	}, /* alias.c */
	{ "UNSHIFT",	unshift_cmd	},
	{ "UNTIL",	whilecmd	},
	{ "UPING",	send_comm	},
	{ "USERHOST",	userhostcmd	},
	{ "USERIP",	useripcmd	},
	{ "USLEEP",	usleepcmd	},
	{ "USRIP",	usripcmd	},
	{ "VERSION",	version		},
	{ "WAIT",	waitcmd		},
	{ "WALLCHOPS",	send_2comm	},
	{ "WALLOPS",	e_wallop	},
	{ "WHICH",	load		},
	{ "WHILE",	whilecmd	}, /* if.c */
	{ "WHO",	whocmd		}, /* who.c */
	{ "WHOIS",	whois		},
	{ "WHOWAS",	whois		},
	{ "WINDOW",	windowcmd	}, /* window.c */
	{ "XDEBUG",	xdebugcmd	}, /* debug.c */
	{ "XECHO",	xechocmd	},
	{ "XEVAL",	xevalcmd	},
	{ "XQUOTE",	quotecmd	},
	{ "XTYPE",	xtypecmd	},
	{ NULL,		commentcmd 	}
};

void	init_commands (void)
{
	int	i;

	for (i = 0; irc_command[i].name; i++)
	    add_builtin_cmd_alias(irc_command[i].name, irc_command[i].func);
}


/* 
 * Full scale abort.  
 */
BUILT_IN_COMMAND(abortcmd)
{
        abort();
}
 
/*
 * away: the /AWAY command.  Keeps track of the away message locally, and
 * sends the command on to the server.
 */
BUILT_IN_COMMAND(away)
{
	char	*arg = NULL;
	int	flag = AWAY_ONE;
	int	i;

	if (*args)
	{
		if ((*args == '-') || (*args == '/'))
		{
			arg = args;
			while (*arg && !isspace(*arg))
				arg++;
			if (*arg)
				*arg++ = 0;

			if (0 == my_strnicmp(args+1, "ALL", 1))	/* all */
			{
				flag = AWAY_ALL;
				args = arg;
			}
			else if (0 == my_strnicmp(args+1, "ONE", 1)) /* one */
			{
				flag = AWAY_ONE;
				args = arg;
			}
			else if (0 == my_strnicmp(args+1, "-", 1)) /* stop */
				args = arg;
			else
			{
				say("AWAY: %s unknown flag", args);
				return;
			}
		}
	}

	if (flag == AWAY_ALL)
	{
		for (i = 0; i < server_list_size(); i++)
		    if (is_server_valid(i))
			set_server_away(i, args);
	}
	else
		set_server_away(from_server, args);

	update_all_status();
}

BUILT_IN_COMMAND(blesscmd)
{
	bless_local_stack();
}

BUILT_IN_COMMAND(beepcmd)
{
	term_beep();
}

BUILT_IN_COMMAND(cd)
{
	char	*arg;
	Filename dir;

	/* Hrm.  Is it worse to do new_next_arg than next_arg? */
	if ((arg = new_next_arg(args, &args)) != NULL)
	{
		if (normalize_filename(arg, dir))
			say("CD: %s contains an invalid directory", dir);
		else if (chdir(dir))
			say("CD: %s", strerror(errno));
	}

	if ((getcwd(dir, sizeof(dir))))
		say("Current directory: %s", dir);
	else
		say("The current directory is invalid.");
}

BUILT_IN_COMMAND(e_call)
{
	dump_call_stack();
}

/* clear: the CLEAR command.  Figure it out */
BUILT_IN_COMMAND(e_clear)
{
	char	*arg;
	int	all = 0,
		visible = 0,
		hidden = 0,
		unhold = 0;
	int	clear = !strcmp(command, "CLEAR");

	while ((arg = next_arg(args, &args)) != NULL)
	{
		/* -ALL and ALL here becuase the help files used to be wrong */
		if (!my_strnicmp(arg, "ALL", 1) || !my_strnicmp(arg+1, "ALL", 1))
			visible = 0, hidden = 0, all = 1;

		/* UNHOLD */
		else if (!my_strnicmp(arg+1, "UNHOLD", 1))
			unhold = 1;		/* Obsolete */

		else if (!my_strnicmp(arg+1, "VISIBLE", 1))
			visible = 1, hidden = 0, all = 1;

		else if (!my_strnicmp(arg+1, "HIDDEN", 1))
			visible = 0, hidden = 1, all = 1;

		else
			say("Unknown flag: %s", arg);
	}

	if (all)
	{
		if (clear)
			clear_all_windows(visible, hidden, unhold);
		else
			unclear_all_windows(visible, hidden, unhold);
	}
	else
	{
		if (clear)
			clear_window_by_refnum(0, unhold);
		else
			unclear_window_by_refnum(0, unhold);
	}

	update_all_windows();
}

/* comment: does the /COMMENT command, useful in .ircrc */
BUILT_IN_COMMAND(commentcmd)
{
	/* nothing to do... */
}

BUILT_IN_COMMAND(ctcp)
{
	const char	*to;
	char *	stag;
	int	i;
	int	request;

	if ((to = next_arg(args, &args)) != NULL)
	{
		if (!strcmp(to, "*"))
			if ((to = get_window_echannel(0)) == NULL)
				to = zero;

		if ((stag = next_arg(args, &args)) != NULL)
			upper(stag);
		else
			stag = LOCAL_COPY("VERSION");

		if (get_server_doing_notice(from_server) > 0)
		{
			say("You may not use the CTCP command from a NOTICE!");
			return;
		}
		else if (get_server_doing_privmsg(from_server))
			request = 0;		/* XXX What about dcc chat? */
		else
			request = 1;

		if (args && *args)
			send_ctcp(request, to, stag, "%s", args);
		else
			send_ctcp(request, to, stag, NULL);
	}
	else
		say("Usage: /CTCP <[=]nick|channel|*> [<request>]");
}

#include "debuglog.c"

struct defer {
	char *	cmds;
	int	servref;
	char *	subargs;
};
typedef struct defer Defer;

static	Defer *	defer_list = NULL;
static	int	defer_list_size = -1;
	int	need_defered_commands = 0;

void	do_defered_commands (void)
{
	int	i;

	if (defer_list)
	{
	    int old_from_server = from_server;
	    int old_window = get_server_current_window(from_server);

	    for (i = 0; defer_list[i].cmds; i++)
	    {
		from_server = defer_list[i].servref;
		make_window_current_by_refnum(get_server_current_window(from_server));

		call_lambda_command("deferred", defer_list[i].cmds,
						defer_list[i].subargs);
		new_free(&defer_list[i].cmds);
		new_free(&defer_list[i].subargs);
	    }

	    from_server = old_from_server;
	    make_window_current_by_refnum(old_window);
	}

	defer_list_size = 1;
	RESIZE(defer_list, Defer, defer_list_size);
	defer_list[0].cmds = NULL;
	defer_list[0].subargs = NULL;
	need_defered_commands = 0;
}

BUILT_IN_COMMAND(defercmd)
{
	/* Bootstrap the defer list */
	if (defer_list_size <= 0)
	{
		defer_list_size = 1;
		RESIZE(defer_list, Defer, defer_list_size);
		defer_list[0].cmds = NULL;
		defer_list[0].subargs = NULL;
	}

	defer_list_size++;
	RESIZE(defer_list, Defer, defer_list_size);
	defer_list[defer_list_size - 2].cmds = malloc_strdup(args);
	defer_list[defer_list_size - 2].subargs = malloc_strdup(subargs);
	defer_list[defer_list_size - 2].servref = from_server;

	defer_list[defer_list_size - 1].cmds = NULL;
	defer_list[defer_list_size - 1].subargs = NULL;
	need_defered_commands++;
}


BUILT_IN_COMMAND(deop)
{
	send_to_server("MODE %s -o", get_server_nickname(from_server));
}

BUILT_IN_COMMAND(describe)
{
	const char	*target;

	target = next_arg(args, &args);
	if (target && args && *args)
	{
		char	*message;
		int	l;

		if (!strcmp(target, "*"))
			if ((target = get_window_echannel(0)) == NULL)
				target = zero;

		message = args;

		l = message_from(target, LEVEL_ACTION);
		send_ctcp(1, target, "ACTION", "%s", message);
		if (do_hook(SEND_ACTION_LIST, "%s %s", target, message))
			put_it("* -> %s: %s %s", target, get_server_nickname(from_server), message);
		pop_message_from(l);
	}
	else
		say("Usage: /DESCRIBE <[=]nick|channel|*> <action description>");
}

BUILT_IN_COMMAND(send_to_query_first)
{
	const char	*tmp;

	tmp = get_window_target(0);
	send_text(from_server, tmp, args, NULL, 1, 0);
}

BUILT_IN_COMMAND(send_to_channel_first)
{
	const char	*tmp;

	if ((tmp = get_window_echannel(0)))
	    send_text(from_server, tmp, args, NULL, 1, 0);
	else if ((tmp = get_window_target(0)))
	    send_text(from_server, tmp, args, NULL, 1, 0);
}

/*
 * e_channel: does the channel command.  I just added displaying your current
 * channel if none is given 
 */
BUILT_IN_COMMAND(e_channel)
{
	int	l;

	l = message_from(NULL, LEVEL_OTHER);
	if (args && *args)
		windowcmd_rejoin(0, &args);
	else
		list_channels();
	pop_message_from(l);
}

/*
 * e_nick: does the /NICK command.  Records the users current nickname and
 * sends the command on to the server 
 */
BUILT_IN_COMMAND(e_nick)
{
	char	*nick;

	if (from_server == NOSERV)
	{
	    say("I can't figure out what server to change your nickname for");
	    return;
	}

	if (!(nick = next_arg(args, &args)))
	{
		say("Your nickname on server %d is %s", 
				from_server, 
				get_server_nickname(from_server));
		if (get_pending_nickname(from_server))
			say("A nickname change on server %d to %s is pending.", 
				from_server,
				get_pending_nickname(from_server));
		return;
	}

	change_server_nickname(from_server, nick);
}

/*
 * This is a quick and dirty hack (emphasis on dirty) that i whipped up
 * just for the heck of it.  I feel really dirty about using the add_timer
 * call (bletch!) to fake a timeout for io().  The better answer would be
 * for io() to take an argument specifying the maximum threshold for a
 * timeout, but i didnt want to deal with that here.  So i just add a
 * dummy timer event that does nothing (wasting two function calls and
 * about 20 bytes of memory), and call io() until the whole thing blows
 * over.  Nice and painless.  You might want to try this instead of /sleep,
 * since this is (obviously) non-blocking.  This also calls time() for every
 * io event, so that might also start adding up.  Oh well, TIOLI.
 *
 * Without an argument, it waits for the user to press a key.  Any key.
 * and the key is accepted.  Thats probably not right, ill work on that.
 */
static	int	e_pause_cb_throw = 0;
static	void	e_pause_cb (char *u1, const char *u2) { e_pause_cb_throw--; }
static int e_pause_callback (void *ignored) { return 0; }
BUILT_IN_COMMAND(e_pause)
{
	char *	sec;
	double 	seconds;
	Timeval	start;

	if (!(sec = next_arg(args, &args)))
	{
		int	c_level = e_pause_cb_throw;

		add_wait_prompt(empty_string, e_pause_cb, 
				NULL, WAIT_PROMPT_DUMMY, 0);
		e_pause_cb_throw++;
		while (e_pause_cb_throw > c_level)
			io("pause");
		return;
	}

	seconds = atof(sec);
	get_time(&start);
	start = time_add(start, double_to_timeval(seconds));

	/* 
	 * I use comment here simply becuase its not going to mess
	 * with the arguments.
	 */
	add_timer(0, empty_string, seconds, 1, 
			e_pause_callback,
			NULL, NULL, GENERAL_TIMER, -1, 0, 0);
	while (time_diff(get_time(NULL), start) > 0)
		io("e_pause");
}

/*
 * e_privmsg: The MSG command, displaying a message on the screen indicating
 * the message was sent.  Also, this works for the NOTICE command. 
 */
BUILT_IN_COMMAND(e_privmsg)
{
	const char	*nick;

	if (!strcmp(command, "MSG"))
		command = "PRIVMSG";		/* *cough* */

	if ((nick = next_arg(args, &args)) != NULL)
	{
		if (!strcmp(nick, "."))
		{
			if (!(nick = get_server_sent_nick(from_server)))
			{
				say("You have not sent a message to anyone yet.");
				return;
			}
		}
		else if (!strcmp(nick, ",")) 
		{
			if (!(nick = get_server_recv_nick(from_server)))
			{
				say("You have not received a message from anyone yet.");
				return;
			}
		}
		else if (!strcmp(nick, "*") && (!(nick = get_window_echannel(0))))
			nick = zero;
		send_text(from_server, nick, args, command, get_window_display(), 0);
		set_server_sent_body(from_server, args);
	}
	else 
	{
		if (!strcmp(command, "PRIVMSG"))
			command = "MSG";	/* *cough* */
		say("Usage: /%s <[=]nickname|channel|*> <text>", command);
	}
}

/* e_quit: The /QUIT, /EXIT, etc command */
BUILT_IN_COMMAND(e_quit)
{
	if (args && *args)
		irc_exit(1, "%s", args);
	else
		irc_exit(1, NULL);
}

/*
 * The TOPIC command.
 */
BUILT_IN_COMMAND(e_topic)
{
	int	clear_topic = 0;
	const char *channel = get_window_echannel(0);
	const char *arg;
	char *	args_copy;
	const char *recode_text;
	char *	extra = NULL;

	if (!args)
		return;

	if (*args == '-')
		clear_topic = 1, args++;

	/*
	 * You can do
	 *   	/TOPIC #chan blahblah blah
	 * or	
	 * 	/TOPIC blahblah blah
	 * So we keep a copy just in case it's the latter.
	 */
	args_copy = LOCAL_COPY(args);

	if (!(arg = next_arg(args, &args)))
		arg = channel;
	else if (!strcmp(arg, "*"))
		arg = channel;

	if (!arg)
	{
		say("You are not on a channel in this window.");
		return;
	}

	if (is_channel(arg))
	{
		if ((args && *args) || clear_topic)
		{
			recode_text = outbound_recode(arg, from_server, args, &extra);
			send_to_server_with_payload(recode_text, "TOPIC %s", arg);
			new_free(&extra);
		}
		else
			send_to_server("TOPIC %s", arg);
	}
	else if (channel)
	{
		recode_text = outbound_recode(channel, from_server, args_copy, &extra);
		send_to_server_with_payload(recode_text, "TOPIC %s", channel);
		new_free(&extra);
	}
	else
		say("You are not on a channel in this window.");
}


/* e_wallop: used for WALLOPS (undernet only command) */
BUILT_IN_COMMAND(e_wallop)
{
	int l;

	l = message_from(NULL, LEVEL_WALLOP);
	send_to_server("WALLOPS :%s", args);
	pop_message_from(l);
}

/* Super simple, fast /ECHO */
BUILT_IN_COMMAND(echocmd)
{
	int	old;

	old = swap_window_display(1);
        put_echo(args);
	swap_window_display(old);
}

/*
 * xecho: simply displays the args to the screen, with some flags.
 * XECHO	<- dont delete this, i search for it. ;-)
 */
BUILT_IN_COMMAND(xechocmd)
{
	unsigned display;
	char	*flag_arg;
	int	temp = 0;
	int	all_windows = 0;
	int	all_windows_for_server = 0;
	int	want_banner = 0;
	char	*stuff = NULL;
	int	nolog = 0;
	int	more = 1;
	int	xtended = 0;
	int	old_window_notify = do_window_notifies;
	int	old_mangler = display_line_mangler;
	int	to_window_refnum = get_to_window();
	int	old_inhibit_logging = inhibit_logging;
	int	old_output_expires_after = output_expires_after;
	int	to_level = get_who_level();
	const char *	to_from = get_who_from();

	while (more && args && *args == '-')
	{
	    switch (args[1])
	    {
		case 'c':
		case 'C':	/* CURRENT (output to user's current window) */
		{
			next_arg(args, &args);
			to_window_refnum = 0;
			break;
		}

		case 'l':
		case 'L':
		{
		    int w;

		    flag_arg = next_arg(args, &args);

		    /* LINE (output to scratch window) */
		    if (flag_arg[2] == 'i' || flag_arg[2] == 'I')
		    {
			int to_line = 0;
			int display_lines;

			if (to_window_refnum == -1)
			{
			    yell("XECHO: -LINE only works if -WIN is specified first");
			    return;
			}

			/* This is checked below, anyways */
			if (get_window_refnum(to_window_refnum) < 1)
				break;

			display_lines = get_window_display_lines(to_window_refnum);
			to_line = my_atol(next_arg(args, &args));
			if (to_line < 0 || to_line >= display_lines)
			{
				yell("XECHO: -LINE %d is out of range "
					"for window (max %d)", to_line, 
					display_lines - 1);
				return;
			}
			set_window_change_line(to_window_refnum, to_line);
		     }

		     /* LEVEL (use specified lastlog level) */
		     else	
		     {
			if (!(flag_arg = next_arg(args, &args)))
				break;

			/* XECHO -L overrules /query and channels! */
			if ((temp = str_to_level(flag_arg)) > -1)
			{
				to_level = temp;
				to_from = NULL;
			}
		     }
		     break;
		}

		case 't':
		case 'T':
		{
			/* Chew up the argument. */
			next_arg(args, &args);

			if (!(flag_arg = next_arg(args, &args)))
				break;

			to_from = flag_arg;
			break;
		}

		case 'v':
		case 'V':	/* VISUAL (output to a visible window) */
		{
			/* Chew up the argument. */
			/*flag_arg =*/ next_arg(args, &args);

			if (get_window_screen(0))
			    to_window_refnum = get_window_refnum(0);
			else if (last_input_screen && 
				 last_input_screen->input_window)
			    to_window_refnum = last_input_screen->input_window;
			else
			    to_window_refnum = lookup_any_visible_window();
			break;
		}

		case 'w':
		case 'W':	/* WINDOW (output to specified window) */
		{
			int	w;
			next_arg(args, &args);

			if (!(flag_arg = next_arg(args, &args)))
				break;

			if (((w = lookup_window(flag_arg)) < 1) && 
			    ((w = get_channel_window(flag_arg, from_server)) < 1))
			{
			    /* 
			     * This is a special favor to Blackjac for
			     * backwards compatability with epic4 where
			     * /xecho -w $winchan() would output to the 
			     * current window when $winchan() returned -1.
			     */
			    if (!my_stricmp(flag_arg, "-1"))
				w = 0;
			    else
				return;	/* No such window */
			}

			to_window_refnum = w;
			break;
		}

		case 'e':
		case 'E':
		{
			double	timeout = 0;

			next_arg(args, &args);
			if (!(flag_arg = next_arg(args, &args)))
				break;

			/* Outputting here is bad.  So just ignore it */
			if (is_number(flag_arg))
			{
				timeout = atof(flag_arg);
				output_expires_after = timeout;
			}

			break;
		}
		case 'a':
		case 'A':	/* ALL (output to all windows) */
		case '*':
		{
			flag_arg = next_arg(args, &args);

			if (flag_arg[2] == 's' || flag_arg[2] == 'S')
				all_windows_for_server = 1;
			else
				all_windows = 1;

			break;
		}

		case 'b':
		case 'B':	/* WITH BANNER */
		{
			next_arg(args, &args);
			want_banner = 1;
			break;
		}

		case 'r':
		case 'R':   /* RAW OUTPUT TO TERMINAL */
		{
			int	wx;

			next_arg(args, &args);
			/*
			 * Nuke reminded me of this.  Just because to_window
			 * is set does not mean that tputs_x is going to
			 * put the string out to the screen on that window.
			 * So we have to make sure that output_screen is
			 * to_window->screen.
			 */
			if (to_window_refnum != -1 && 
					get_window_screen(to_window_refnum))
				output_screen = get_window_screen(to_window_refnum);
			else
				output_screen = get_window_screen(0);
			tputs_x(args);
			term_flush();
			return;
		}

		case 'n':
		case 'N': /* NOLOG (dont add to lastlog) */
		{
			next_arg(args, &args);
			nolog = 1;
			break;
		}

		case 's':
		case 'S': /* SAY (dont output if suppressing output) */
		{
			next_arg(args, &args);
			if (!get_window_display())
				return;
			break;
		}

		case 'x':
		case 'X': /* X -- allow all attributes to be outputted */
		{
			next_arg(args, &args);
			display_line_mangler = NORMALIZE;
			xtended = 1;
			break;
		}

		case 'f':
		case 'F': /* DO not notify for hidden windows (%F) */
		{
			next_arg(args, &args);
			do_window_notifies = 0;
			break;
		}

		case '-': /* End of arg list */
		{
			next_arg(args, &args);
			more = 0;
			break;
		}

		default: /* Unknown */
		{
			/*
			 * Unknown flags are just spit out
			 * like normal.  This is done so that
			 * people can do /xecho -> blah blah
			 * and not get throttled on the '->'
			 */
			more = 0;
			break;
		}
	    }

	    if (!args)
		args = LOCAL_COPY(empty_string);
	}

	display = swap_window_display(1);
	if (nolog)
		inhibit_logging = 1;

	if (want_banner == 1)
	{
		malloc_strcpy(&stuff, banner());
		if (*stuff)
		{
			malloc_strcat2_c(&stuff, space, args, NULL);
			args = stuff;
		}
	}
	else if (want_banner != 0)
		panic(1, "xecho: want_banner is %d", want_banner);

	if (all_windows == 1 || all_windows_for_server == 1)
	{
		int	win = 0;

		while ((traverse_all_windows2(&win)))
		{
			int	l;

			if (all_windows == 0 && get_window_server(win) != from_server)
				continue;

			l = message_setall(win, to_from, to_level);
			put_echo(args);
			pop_message_from(l);
		}
	}
	else if (all_windows != 0)
		panic(1, "xecho: all_windows is %d", all_windows);
	else if (all_windows_for_server != 0)
		panic(1, "xecho: all_windows_for_server is %d", 
			all_windows_for_server);
	else
	{
		int l = message_setall(to_window_refnum, to_from, to_level);
		put_echo(args);
		pop_message_from(l);
	}

	if (stuff)
		new_free(&stuff);

	if (xtended)
		display_line_mangler = old_mangler;

	do_window_notifies = old_window_notify;
	output_expires_after = old_output_expires_after;

	if (nolog)
		inhibit_logging = old_inhibit_logging;

	swap_window_display(display);
}

/*
 * /xeval only shares one line of code with /eval, and so doing all of the
 * overhead stuff is ridiculous when /eval is used.  So i split them up.
 * Sue me.
 */
BUILT_IN_COMMAND(xevalcmd)
{
	char *	flag;
	int	old_from_server = from_server;
	int	old_refnum = get_window_refnum(0);
	int	l = -1;
	int	old_window_display = get_window_display();
	int	old_inhibit_logging = inhibit_logging;
	int	nolog = 0;

	while (args && (*args == '-' || *args == '/'))
	{
		flag = next_arg(args, &args);
		if (!my_stricmp(flag, "--")) 	/* End of options */
			break;

		if (!my_strnicmp(flag + 1, "SERVER", 1)) /* SERVER */
		{
			char *s;
			int val;

			/* No argument means no commands means ignore it */
			if (!(s = next_arg(args, &args)))
				return;

			val = str_to_servref(s);
			if (is_server_registered(val))
				from_server = val;
		}
		else if (!my_strnicmp(flag + 1, "WINDOW", 1)) /* WINDOW */
		{
			int	win;

			if ((win = lookup_window(next_arg(args, &args))) > 0)
			{
				l = message_setall(win, get_who_from(), get_who_level());
				make_window_current_by_refnum(win);
			}
		}
		/* This does the reverse of ^ */
		else if (!my_strnicmp(flag + 1, "NOISY", 1)) /* NOISY */
			set_window_display(1);

		else if (!my_strnicmp(flag + 1, "NOLOG", 2)) /* NOLOG */
			nolog = 1;
	}

	if (nolog)
		inhibit_logging = 1;

	runcmds(args, subargs);

	if (l != -1)
		pop_message_from(l);

	make_window_current_by_refnum(old_refnum);
	from_server = old_from_server;
	set_window_display(old_window_display);
	if (nolog)
		inhibit_logging = old_inhibit_logging;
}

BUILT_IN_COMMAND(evalcmd)
{
	runcmds(args, subargs);
}

/* flush: flushes all pending stuff coming from the server */
BUILT_IN_COMMAND(flush)
{
	say("Standby, Flushing server output...");
	flush_server(from_server);
	say("Done");
}

BUILT_IN_COMMAND(funny_stuff)
{
	char	*arg;
	const char	*stuff;
	int	min = 0,
		max = 0,
		flags = 0,
		ircu = 0;

	stuff = empty_string;
	while ((arg = next_arg(args, &args)) != NULL)
	{
		if (*arg == '/' || *arg == '-')
		{
			if (my_strnicmp(arg+1, "IRCU", 1) == 0) 	/* IRCU */
				ircu = 1;
			else if (my_strnicmp(arg+1, "MAX", 2) == 0)	/* MAX */
			{
				if ((arg = next_arg(args, &args)) != NULL)
					max = my_atol(arg);
			}
			else if (my_strnicmp(arg+1, "MIN", 2) == 0) /* MIN */
			{
				if ((arg = next_arg(args, &args)) != NULL)
					min = my_atol(arg);
			}
			else if (my_strnicmp(arg+1, "ALL", 1) == 0) /* ALL */
				flags &= ~(FUNNY_PUBLIC | FUNNY_PRIVATE);
			else if (my_strnicmp(arg+1, "PUBLIC", 2) == 0) /* PUBLIC */
			{
				flags |= FUNNY_PUBLIC;
				flags &= ~FUNNY_PRIVATE;
			}
			else if (my_strnicmp(arg+1, "PRIVATE", 2) == 0) /* PRIVATE */
			{
				flags |= FUNNY_PRIVATE;
				flags &= ~FUNNY_PUBLIC;
			}
			else if (my_strnicmp(arg+1, "TOPIC", 1) == 0)	/* TOPIC */
				flags |= FUNNY_TOPIC;
			else if (my_strnicmp(arg+1, "USERS", 1) == 0)	/* USERS */
				flags |= FUNNY_USERS;
			else if (my_strnicmp(arg+1, "NAME", 1) == 0)	/* NAME */
				flags |= FUNNY_NAME;
			else
				stuff = arg;
		}
		else stuff = arg;
	}

	if (strcmp(stuff, "*") == 0)
		if (!(stuff = get_window_echannel(0)))
			stuff = empty_string;

	/* Channel names can contain stars! */
	if (strchr(stuff, '*') && !im_on_channel(stuff, from_server))
	{
		set_server_funny_stuff(from_server, min, max, flags, stuff);

		if (min && ircu)
		{
			if (max)
				send_to_server("%s >%d,<%d", command, min - 1, max + 1);
			else
				send_to_server("%s >%d", command, min - 1);
		}
		else if (max && ircu)
			send_to_server("%s <%d", command, max + 1);
		else
			send_to_server("%s %s", command, empty_string);
	}
	else
	{
		set_server_funny_stuff(from_server, min, max, flags, NULL);

		if (min && ircu)
		{
			if (max)
				send_to_server("%s >%d,<%d", command, min - 1, max + 1);
			else
				send_to_server("%s >%d", command, min - 1);
		}
		else if (max && ircu)
			send_to_server("%s <%d", command, max + 1);
		else
			send_to_server("%s %s", command, stuff);
	}
}

BUILT_IN_COMMAND(hookcmd)
{
	if (*args)
		do_hook(HOOK_LIST, "%s", args);
	else
		say("Usage: /HOOK [text]");
}

/*
 * Modified /hostname by Thomas Morgan (tmorgan@pobox.com)
 */
BUILT_IN_COMMAND(e_hostname)
{
	if (args && *args)
	{
		char *s;

		if (!strcmp(args, "-"))
			args = NULL;
		s = switch_hostname(args);
		say("%s", s);
		new_free(&s);
	}
	else
		say("Local Host name is (IPv4: %s, IPv6: %s)",
			LocalIPv4HostName ? LocalIPv4HostName : "<default>",
			LocalIPv6HostName ? LocalIPv6HostName : "<default>");
}


/*
 * info: does the /INFO command.  I just added some credits
 * I updated most of the text -phone, feb 1993.
 */
BUILT_IN_COMMAND(info)
{
	if (!args || !*args)
	{
		say("IRC II: Originally written by Michael Sandrof");
		say("\tCopyright 1990-1991 Michael Sandrof");
		say("Versions 2.1 to 2.2pre7 by Troy Rollo");
		say("\tCopyright 1991-1992 Troy Rollo");
		say("Versions 2.2pre8 through 2.8.2 by Matthew Green");
		say("\tCopyright 1992-1995 Matthew Green");
		say("All EPIC versions by EPIC Software Labs");
		say("\tCopyright 1993-2013 EPIC Software Labs");
		say(" ");
		say("	    Contact the EPIC project (%s)", EMAIL_CONTACT);
		say("	    for problems with this or any other EPIC client");
		say(" ");
		say("EPIC Software Labs (in alphabetical order):");
		say("       \tBrian Hauber         <bhauber@epicsol.org>");
		say("       \tChip Norkus          <wd@epicsol.org>");
		say("       \tCrazyEddy            <crazyed@epicsol.org>");
		say("	    \tDennis Moore         <nimh@epicsol.org>");
                say("       \tErlend B. Mikkelsen  <howl@epicsol.org>");
		say("       \tJason Brand          <kitambi@epicsol.org>");
		say("	    \tJeremy Nelson        <jnelson@epicsol.org>");
		say("       \tStanislaw Halik      <sthalik@epicsol.org>");
		say("       \tWilliam Rockwood     <wjr@epicsol.org>");
                say("       \tXavier               <jak@epicsol.org>");
		say(" ");
		say("The EPIC Project:");
		say("There are far too many people in the EPIC project to ");
		say("thank them properly here, so we set up a web page:");
		say("\t\thttp://www.epicsol.org/?page=credits");
		say(" ");
		say("A special thank you to all who rabidly use and support");
		say("the EPIC client and what it stands for");
		say(" ");
		say("        In memory of Jeffrey Zabek, 1973 - 2000        ");
		say("        In memory of Matthew Lumberto, 2013            ");
		say(" ");
		say("ircii contributors");
		say(" ");
		say("       \tMichael Sandrof       Mark T. Dameu");
		say("       \tStellan Klebom        Carl v. Loesch");
		say("       \tTroy Rollo            Martin  Friedrich");
		say("       \tMichael Weber         Bill Wisner");
		say("       \tRiccardo Facchetti    Stephen van den Berg");
		say("       \tVolker Paulsen        Kare Pettersson");
		say("       \tIan Frechette         Charles Hannum");
		say("       \tMatthew Green         Christopher Williams");
		say("       \tJonathan Lemon        Brian Koehmstedt");
		say("       \tNicolas Pioch         Brian Fehdrau");
		say("       \tDarren Reed           Jeff Grills");
		say("	    \tChris Williams");
	}
	send_to_server("INFO %s", args?args:empty_string);
}

/*
 * eval_inputlist:  Cute little wrapper that calls runcmds() when we
 * get an input prompt ..
 */
static void	eval_inputlist (char *args, const char *line)
{
        char *tmp;
	if (args[0]=='(') {
                if (!(tmp = next_expr(&args, '('))) {
			yell("INPUT: syntax error with arglist");
			return;
		}
                runcmds_with_arglist(args, tmp, line);
	} else {
                /* traditional behavior */
		runcmds(args, line);
	}
}


/*
 * inputcmd:  the INPUT command.   Takes a couple of arguements...
 * the first surrounded in double quotes, and the rest makes up
 * a normal ircII command.  The command is evalutated, with $*
 * being the line that you input.  Used add_wait_prompt() to prompt
 * the user...  -phone, jan 1993.
 */
BUILT_IN_COMMAND(inputcmd)
{
	char	*prompt;
	int	wait_type;
	char	*argument;
	int	echo = 1;

	while (*args == '-')
	{
		argument = next_arg(args, &args);
		if (!my_stricmp(argument, "-noecho"))
			echo = 0;
		if (!my_stricmp(argument, "--"))
			break;
	}

	if (!(prompt = new_next_arg(args, &args)))
	{
		say("Usage: %s \"prompt\" { commands }", command);
		return;
	}

	if (!strcmp(command, "INPUT"))
		wait_type = WAIT_PROMPT_LINE;
	else
		wait_type = WAIT_PROMPT_KEY;

	while (my_isspace(*args))
		args++;

	add_wait_prompt(prompt, eval_inputlist, args, wait_type, echo);
}

/*
 * I put this here for legal/logistical reasons.  The problem is that each
 * file in the EPIC source distribution does not have the actual license
 * information, but instead a pointer to where the license is at.  This is
 * confusing as all heck and doesnt result in the license information
 * actually making its way into any compiled binaries.  Because there are
 * people who are distributing EPIC as a binary package, this command
 * allows the resulting binary to have the copyright/license information
 * in it, which (IMO) is probably a good idea.
 */
BUILT_IN_COMMAND(license)
{
	yell("This is the original license for this software.");
	yell(" ");
	yell("Copyright (c) 1990 Michael Sandroff.");
	yell("Copyright (c) 1991, 1992 Troy Rollo.");
 	yell("Copyright (c) 1992-1996 Matthew Green.");
	yell("Copyright 1994 Jake Khuon.");
	yell("Coypright 1993, 2013 EPIC Software Labs.");
	yell("All rights reserved");
	yell(" ");
	yell("Redistribution and use in source and binary forms, with or");
	yell("modification, are permitted provided that the following");
	yell("conditions are met:");
	yell("1. Redistributions of source code must retain the above");
	yell("   copyright notice, this list of conditions and the following");
	yell("   disclaimer.");
	yell("2. Redistributions in binary form must reproduce the above");
	yell("   copyright notice, the above paragraph (the one permitting");
	yell("   redistribution), this list of conditions, and the following");
	yell("   disclaimer in the documentation and/or other materials ");
	yell("   provided with the distribution.");
	yell("3. The names of the author(s) may not be used to endorse or ");
	yell("   promote products derived from this software without specific");
	yell("   prior written permission");
	yell(" ");
	yell("THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY");
	yell("EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,");
	yell("THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A ");
	yell("PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE ");
	yell("AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL");
	yell("EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING BUT NOT LIMITED");
	yell("TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,");
	yell("DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED");
	yell("AND ON ANY THEORY OF LIABIILTY, WHETHER IN CONTRACT, STRICT");
	yell("LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING");
	yell("IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF");
	yell("THE POSSIBILITY OF SUCH DAMAGE.");
}

struct load_info
{
	char 	*filename;
	char	*package;
	const char	*loader;
	int	package_set_here;
	int	line;
	int	start_line;
	struct stat sb;
} load_level[MAX_LOAD_DEPTH];

int 	load_depth = -1;

void	dump_load_stack (int onelevel)
{
	int i = load_depth;

	if (i == -1)
		return;

	yell("Right before line [%d] of file [%s]", load_level[i].line,
					  load_level[i].filename);

	if (!onelevel)
	{
		while (--i >= 0)
		{
			yell("Loaded right before line [%d] of file [%s]", 
				load_level[i].line, load_level[i].filename);
		}
	}
	return;
}

const char *current_filename (void) 
{ 
	if (load_depth == -1)
		return empty_string;
	else if (load_level[load_depth].filename)
		return load_level[load_depth].filename;
	else
		return empty_string;
}

const char *current_loader (void) 
{ 
	if (load_depth == -1)
		return empty_string;
	else if (load_level[load_depth].loader)
		return load_level[load_depth].loader;
	else
		return empty_string;
}

int	current_line (void)
{ 
	if (load_depth == -1)
		return -1;
	else
		return load_level[load_depth].line;
}

const char *current_package (void)
{
	if (load_depth == -1)
		return NULL;
	else if (load_level[load_depth].package)
		return load_level[load_depth].package;
	else
		return empty_string;
}

BUILT_IN_COMMAND(packagecmd)
{
	if (load_depth == -1)
		return;
	else
		malloc_strcpy(&load_level[load_depth].package, args);
}

BUILT_IN_COMMAND(subpackagecmd)
{
	if (load_depth == -1)
		return;
	else if (load_depth > 0)
	{
		malloc_strcpy(&load_level[load_depth].package, 
				load_level[load_depth-1].package);
		malloc_strcat(&load_level[load_depth].package, "::");
	}
	else
		new_free(&load_level[load_depth].package);

	malloc_strcat(&load_level[load_depth].package, args);
}

static void	loader_which (const char *file_contents, off_t file_contents_size, const char *filename, const char *args, struct load_info *);
static void	loader_std (const char *file_contents, off_t file_contents_size, const char *filename, const char *args, struct load_info *);
static void	loader_pf  (const char *file_contents, off_t file_contents_size, const char *filename, const char *args, struct load_info *);

/*
 * load: the /LOAD command.  Reads the named file, parsing each line as
 * though it were typed in (passes each line to parse_statement). 
 * ---loadcmd---
 */
BUILT_IN_COMMAND(load)
{
	char *	filename;
	char *	sargs;
	const char *	use_path;
	char *	expanded;
        struct epic_loadfile * elf;
	int	display;
	int	do_one_more = 0;
	void	(*loader) (const char *, off_t, const char *, const char *, struct load_info *);
	char *	file_contents = NULL;
	off_t	file_contents_size = 0;
	/* This should default to /SET DEFAULT_SCRIPT_ENCODING */
	const char *	declared_encoding = NULL;

	if (++load_depth == MAX_LOAD_DEPTH)
	{
		load_depth--;
		dump_load_stack(0);
		say("No more than %d levels of LOADs allowed", MAX_LOAD_DEPTH);
		return;
	}

	/* Make sure no old (bogus) values are lying around. */
	load_level[load_depth].filename = NULL;
	load_level[load_depth].package = NULL;
	load_level[load_depth].loader = NULL;
	load_level[load_depth].package_set_here = 0;
	load_level[load_depth].line = 0;
	load_level[load_depth].start_line = 0;
	/* What to do with load_level[load_depth].sb? */

	display = swap_window_display(0);
	permit_status_update(0);	/* No updates to the status bar! */

	/*
	 * Default loader: "std" for /load, "which" for /which
	 */
	if (command && *command == 'W')
	    loader = loader_which;
	else
	    loader = loader_std;

	declared_encoding = find_recoding("scripts", NULL, NULL);

	/* 
	 * We iterate over the whole list -- if we use the -args flag, the
	 * we will make a note to exit the loop at the bottom after we've
	 * gone through it once...
	 */
	while (args && *args && (filename = next_arg(args, &args)))
	{
	    if (do_one_more)
	    {
		sargs = args;
		args = NULL;
	    }
	    else if (my_strnicmp(filename, "-pf", strlen(filename)) == 0)
	    {
		loader = loader_pf;
		continue;
	    }
	    else if (my_strnicmp(filename, "-std", strlen(filename)) == 0)
	    {
		loader = loader_std;
		continue;
	    }
	    /* 
	     * If we use the args flag, then we will get the next
	     * filename (via continue) but at the bottom of the loop
	     * we will exit the loop 
	     */
	    else if (my_strnicmp(filename, "-args", strlen(filename)) == 0)
	    {
		do_one_more = 1;
		continue;		/* Pick up the filename */
	    }
	    else if (my_strnicmp(filename, "-encoding", strlen(filename)) == 0)
	    {
		declared_encoding = next_arg(args, &args);
		continue;
	    }
	    else
		sargs = NULL;

	    /* Locate the file */
	    if (!(use_path = get_string_var(LOAD_PATH_VAR)))
	    {
		say("LOAD_PATH has not been set");
		continue;
	    }

	    /*
	     * uzfopen emits an error if the file is not found, so we dont.
	     * uzfopen() also frees 'expanded' for us on error.
	     */
	    expanded = malloc_strdup(filename);

	    /* Read the file into a string */
            if (!(elf = uzfopen(&expanded, use_path, 1, 
				&load_level[load_depth].sb)))
                continue;

	    if (slurp_elf_file(elf, &file_contents, &file_contents_size) > 0)
	    {
		if (invalid_utf8str(file_contents))
		{
		    size_t	really;

		    really = file_contents_size;
		    if ((off_t)really != file_contents_size)
			privileged_yell("Loading a non-utf8 file whose size is greater than size_t will probably have problems");

		    say("Recoding %s using encoding %s", 
				expanded, declared_encoding);
		    recode_with_iconv(declared_encoding, NULL, 
				&file_contents, &really);

		    file_contents_size = (off_t)really;
	        }
	    }
	    epic_fclose(elf);
	    new_free(&elf);

	    /* If no file resulted, then we're done. */
	    if (!file_contents || !*file_contents)
		continue;

	    /* Now process the file */
            load_level[load_depth].filename = expanded;
	    load_level[load_depth].line = 1;
	    if (load_depth > 0 && load_level[load_depth - 1].package)
	        malloc_strcpy(&load_level[load_depth].package,
				load_level[load_depth-1].package);

	    will_catch_return_exceptions++;
	    loader(file_contents, file_contents_size, expanded, 
			sargs, &load_level[load_depth]);
	    will_catch_return_exceptions--;
	    return_exception = 0;

	    new_free(&load_level[load_depth].filename);
	    new_free(&load_level[load_depth].package);
	    new_free(&file_contents);
	}

	/*
	 * Restore some sanity
	 */
	/*
	 * XXX This looks "clever", but it's not.
	 * In other places in the client, we would just unconditionally
	 * restore the value of 'set_window_display(display)', but there
	 * is a historical practice where users expect to be able to do 
	 * /SET DISPLAY OFF in their scripts, and expect it to stay off.
	 * Because we turned the display off (above), honoring 
	 * /set display off is as simple as not restoring the original 
	 * display value.
	 */
	if (get_int_var(DISPLAY_VAR))
	       set_window_display(display);
	permit_status_update(1);
	update_all_status();

	load_depth--;
}

/* The "WHICH" loader */
static void	loader_which (const char *file_contents, off_t file_contents_size, const char *filename, const char *subargs, struct load_info *loadinfo)
{
	loadinfo->loader = "which";
	yell("%s", filename);
}

/* The "Standard" (legacy) loader */
static void	loader_std (const char *file_contents, off_t file_contents_size, const char *filename, const char *subargs, struct load_info *loadinfo)
{
	int	in_comment, comment_line, no_semicolon;
	int	paste_level, paste_line;
	char 	*start, *real_start, *current_row;
#define MAX_LINE_SIZE BIG_BUFFER_SIZE * 5
	char	buffer[MAX_LINE_SIZE * 2 + 1];

        loadinfo->loader = "std";

	in_comment = 0;
	comment_line = -1;
	paste_level = 0;
	paste_line = -1;
	no_semicolon = 1;
	real_start = NULL;
	current_row = NULL;

	for (;;loadinfo->line++)
	{
	    int     len;
	    char    *ptr;

            if (!string_fgets(buffer, MAX_LINE_SIZE, &file_contents, &file_contents_size))
                    break;

            if (loadinfo->line == 1 && loadinfo->sb.st_mode & 0111 &&
	    	(buffer[0] != '#' || buffer[1] != '!'))
	    {
	    	yell("Caution -- %s is marked as an executable; "
		     "loading binaries results in undefined behavior.", 
		     loadinfo->filename);
	    }

	    for (start = buffer; my_isspace(*start); start++)
		;

	    if (!*start || *start == '#')
		continue;

	    len = strlen(start);

	    /*
	     * Original idea to allow \'s in scripts for continued
	     * lines by Stargazer <spz@specklec.mpifr-bonn.mpg.de>
	     * 
	     * If we have \\ at the end of the line, that
	     * should indicate that we DONT want the slash to 
	     * escape the newline
	     *
	     * We cant just do start[len-2] because we cant say
	     * what will happen if len = 1... (a blank line)
	     *
	     * SO.... 
	     * If the line ends in a newline, and
	     * If there are at least 2 characters in the line
	     *	and the 2nd to the last one is a \ and,
	     * If there are EITHER 2 characters on the line or
	     *	the 3rd to the last character is NOT a \ and,
	     * If the line isnt too big yet and,
	     * If we can read more from the file,
	     * THEN -- adjust the length of the string
	     */
	    while ( (start[len-1] == '\n') && 
			(len >= 2 && start[len-2] == '\\') &&
			(len < 3 || start[len-3] != '\\') && 
		        (len < MAX_LINE_SIZE) && 
                        (string_fgets(&(start[len-2]), MAX_LINE_SIZE - len, &file_contents, &file_contents_size)))
	    {
		len = strlen(start);
		loadinfo->line++;
	    }

	    if (start[len-1] == '\n')
		start[--len] = 0;

	    real_start = start;
	    while (start && *start)
	    {
		char    *optr = start;

		/* Skip slashed brackets */
		while ((ptr = strpbrk(optr, "{};/")) && 
			ptr != optr && ptr[-1] == '\\')
		    optr = ptr + 1;

		/* 
		 * if no_semicolon is set, we will not attempt
		 * to parse this line, but will continue
		 * grabbing text
		 */
		if (no_semicolon)
		    no_semicolon = 0;

		else if ((!ptr || (ptr != start || *ptr == '/')) && 
							current_row)
		{
		    if (!paste_level)
		    {
			parse_statement(current_row, 0, NULL);
			new_free(&current_row);

			if (return_exception)
				return;
		    }
		    else if (!in_comment)
			malloc_strcat(&current_row, ";");
		}

		if (ptr)
		{
		    char    c = *ptr;

		    *ptr = 0;
		    if (!in_comment)
			malloc_strcat(&current_row, start);
		    *ptr = c;

		    switch (c)
		    {

		    case '/' :
		    {
			no_semicolon = 1;
			
			/* 
			 * If we're in a comment, any slashes that arent 
			 * preceeded by a star is just ignored (cause its 
			 * in a comment, after all >;) 
			 */
			if (in_comment)
			{
			    /* oops! cant do ptr[-1] if ptr == optr. doh! */
			    if ((ptr > start) && (ptr[-1] == '*'))
			    {
				in_comment = 0;
				comment_line = -1;
			    }
			    break;
			}
			
			/* We're not in a comment... should we start one? */
			/*
			 * COMMENT_HACK (at the request of Kanan) determines 
			 * whether C-like comments will be honored only at the
			 * beginning of a line (if ON) or anywhere (if OFF).
			 * This is needed because some older scripts (phoenix,
			 * textbox, etc) may use slash-star in some ascii 
			 * graphics.
			 */
			if ((ptr[1] == '*') && 
			    (!get_int_var(COMMENT_HACK_VAR) || 
			     ptr == real_start))
			{
			    /* Yep. its the start of a comment. */
			    in_comment = 1;
			    comment_line = loadinfo->line;
			}
			else
			{
			    /* Its NOT a comment. Tack it on the end */
			    malloc_strcat(&current_row, "/");

			    /* Is the / is at the EOL? */
			    if (!ptr[1])
			    {
				/* If we are NOT in a block alias, */
				if (paste_level == 0)
				{
				    parse_statement(current_row, 0, NULL);
				    new_free(&current_row);
				    if (return_exception)
					return;
				}

				/* Just add semicolon and keep going */
				else
				    no_semicolon = 0;
			     }
			}
			break;
		    }

		    /* switch statement tabbed back */
		    case '{' :
		    {
			if (in_comment)
			    break;

			/* 
			 * If we are opening a brand new {} pair, 
			 * remember the line
			 */
			if (!paste_level)
				paste_line = loadinfo->line;

			paste_level++;
			if (ptr == start)
				malloc_strcat(&current_row, " {");
			else
				malloc_strcat(&current_row, "{");
			no_semicolon = 1;
			break;
		    }

		    case '}' :
		    {
			if (in_comment)
			    break;

			if (!paste_level)
			{
				my_error("Unexpected } in %s, line %d",
					filename, loadinfo->line);
				break;
			}

			paste_level--;
			/* 
			 * If we're back to "level 0", then reset 
			 * the paste line
			 */
			if (!paste_level)
				paste_line = -1;
			malloc_strcat(&current_row, "}");
			no_semicolon = ptr[1] ? 1 : 0;
			break;
		    }

		    case ';':
		    {
			if (in_comment)
			    break;
			
			malloc_strcat(&current_row, ";");

			/* Semicolon at the end of line, not within {}s */
			if (ptr[1] == 0 && !paste_level)
			{
			    parse_statement(current_row, 0, NULL);
			    new_free(&current_row);
			    if (return_exception)
				return;
			}

			/* Semicolon at end of line, within {}s */
			else if (ptr[1] == 0 && paste_level)
			    no_semicolon = 0;

			/* Semicolon in the middle of a line */
			else
			    no_semicolon = 1;

			break;
		    }

		    } /* End of switch */
		    start = ptr+1;
		}
		else /* if (!ptr) -- eg, no special chars*/
		{
		    if (!in_comment)
			malloc_strcat(&current_row, start);
		    start = NULL;
		}
	    } /* End of while (start && *start) */
	} /* End of for(;;line++) */

	if (in_comment)
	    my_error("File %s ended with an unterminated comment in line %d",
			filename, comment_line);

	if (current_row)
	{
	    if (paste_level)
	    {
		my_error("Unexpected EOF in %s trying to match '{' at line %d",
				filename, paste_line);
	        new_free(&current_row);
	    }
	    else
	    {
		parse_statement(current_row, 0, NULL);
	        new_free(&current_row);
	        if (return_exception)
			return;
	    }
	}
}

/* The "Pre-Formatted" (new) loader */
/*
 * The idea behind the pre-formatted loader was two-fold:
 * 1) Eliminate the irregular syntax rules that applied only to /load'ed 
 *    scripts and were not used anywhere else in ircII
 * 2) Make loading scripts faster by reducing the processing needed to get
 *    them ready for /eval'ing.
 *
 * Normal loader scripts are taken to be a collection of newline-separated 
 * blocks of statements.  By custom, each block contained one statement.
 * When people first wanted to break statements over multiple lines, the
 * \ continued-line marker was added.  Later, when the perl-esque syntax
 * was introduced, a fancy {-and-} counting algorithm was added that allowed
 * the automatic continuation of a statement across lines without having to
 * use \'s.  Further, semicolons had to be added between statements that 
 * were within {..}'s.  The result is a file that bears little resemblance 
 * to what ircII code actually looks like, and requires a heavyweight parser 
 * to convert a script into something that can be executed.
 *
 * The rules regarding the placement and matching of {'s and }'s, and the 
 * insertion of semicolons are, to be kind, irregular.  This irregularity
 * inspired the PF loader.
 * 
 * The PF stands for "Pre-Formatted" and this means that your script is
 * expected to be well-formed ircII code.  The PF loader does not do any
 * processing on your code, except to remove comments and leading spaces.
 * The whole of the file should be a block, that is, a set of semicolon-
 * separated statements.
 *
 * This creates several advantages.  Because you are required to put in the
 * semicolons yourself, the PF loader does not inexplicably insert semicolons
 * in places you don't want them.  The PF loader does not treat each line 
 * as a separate statement so you can break up your statements however it 
 * pleases you. 
 *
 * Because of the lack of processing overhead, the pf loader is lightweight
 * and is "signficantly" faster than the standard loader.  This can be a 
 * big win for large scripts.
 */
static void	loader_pf (const char *file_contents, off_t file_contents_size, const char *filename, const char *subargs, struct load_info *loadinfo)
{
	char *	buffer;
	int	bufsize, pos;
	int	this_char, my_newline, comment, shebang;

	loadinfo->loader = "pf";

	bufsize = 8192;
	buffer = new_malloc(bufsize);
	pos = 0;
	my_newline = 1;
	comment = 0;
	shebang = 0;

	this_char = string_fgetc(&file_contents, &file_contents_size);

	/* 
	 * If the file is +x, and starts with #!, it's ok.
	 * Otherwise, refuse to load it.
	 */
	if (loadinfo->sb.st_mode & 0111)
	{
	    /* Special code to support #! scripts. */
	    if (this_char == '#' && !string_feof(file_contents, file_contents_size))
	    {
		this_char = string_fgetc(&file_contents, &file_contents_size);
		if (this_char == '!')
		   shebang = 1;
	    }

	    if (shebang == 0)
	    {
		yell("Cannot open %s -- executable file", 
			loadinfo->filename); 
		new_free(&buffer);
		return;
	    }

	    comment = 1;
	}

	while (!string_feof(file_contents, file_contents_size))
	{
	    do
	    {
		/* At a newline, turn on eol handling, turn off comment. */
		if (this_char == '\n') {
		    my_newline = 1;
		    comment = 0;
		    break;
		}

		/* If we are in a comment, ignore this character. */
		if (comment)
		    break;

		/* If we last saw an eol, ignore any following spaces */
		if (my_newline && isspace(this_char))
		    break;

		/* If we last saw an eol, a # starts a one-line comment. */
		if (my_newline && this_char == '#') {
		    comment = 1;
		    break;
		}

		/* If the last thing we saw was a newline, put a space here */
		if (my_newline && pos > 0)
		    buffer[pos++] = ' ';

		/* We are no longer at a newline */
		my_newline = 0;

		/* Append this character to the buffer */
		buffer[pos++] = this_char;

	    } while (0);

	    if (pos >= bufsize - 20) {
		bufsize *= 2;
		new_realloc((void **)&buffer, bufsize);
	    }

	    this_char = string_fgetc(&file_contents, &file_contents_size);
	}

	buffer[pos] = 0;
	call_lambda_command("LOAD", buffer, subargs);
	new_free(&buffer);
}

/*
 * The /me command.  Does CTCP ACTION.  Dont ask me why this isnt the
 * same as /describe...
 */
BUILT_IN_COMMAND(mecmd)
{
	if (args && *args)
	{
		const char	*target;
		int	l;

		if ((target = get_window_target(0)) != NULL)
		{
			send_ctcp(1, target, "ACTION", "%s", args);

			l = message_from(target, LEVEL_ACTION);
			if (do_hook(SEND_ACTION_LIST, "%s %s", target, args))
				put_it("* %s %s", get_server_nickname(from_server), args);
			pop_message_from(l);
		}
		else
			say("No target, neither channel nor query");
	}
	else
		say("Usage: /ME <action description>");
}

static 	void	oper_password_received (char *data, const char *line)
{
	send_to_server("OPER %s %s", data, line);
}

/* oper: the OPER command.  */
BUILT_IN_COMMAND(oper)
{
	char	*password;
	char	*nick;

	oper_command = 1;
	if (!(nick = next_arg(args, &args)))
		nick = nickname;
	if (!(password = next_arg(args, &args)))
	{
		add_wait_prompt("Operator Password:",
			oper_password_received, nick, WAIT_PROMPT_LINE, 0);
		return;
	}
	send_to_server("OPER %s %s", nick, password);
}

/* pingcmd: ctcp ping, duh - phone, jan 1993. */
BUILT_IN_COMMAND(pingcmd)
{
	Timeval t;
	char buffer[64];

	get_time(&t);
	snprintf(buffer, 63, "%s PING %ld %ld", args, 
				(long)t.tv_sec, (long)t.tv_usec);
	ctcp(NULL, buffer, empty_string);
}

BUILT_IN_COMMAND(pop_cmd)
{
	extern char *function_pop(char *);
        char *blah = function_pop(args);
        new_free(&blah);
}

BUILT_IN_COMMAND(pretend_cmd)
{
	char *args_copy;
	int	s = from_server;

	if (!is_server_open(s))
	{
		say("Server %d is not connected", s);
		return;
	}
	
	if (!(args_copy = alloca(IO_BUFFER_SIZE + 1)))
		return;

	strlcpy(args_copy, args, IO_BUFFER_SIZE);
	parse_server(args_copy, IO_BUFFER_SIZE);
	from_server = s;
}


BUILT_IN_COMMAND(push_cmd)
{
	extern char *function_push(char *);
        char *blah = function_push(args);
        new_free(&blah);
}

/*
 * query: the /QUERY command.  Works much like the /MSG, I'll let you figure
 * it out.
 */
BUILT_IN_COMMAND(query)
{
	windowcmd_query(0, &args);
}

/*
 * quote: handles the QUOTE command.  args are a direct server command which
 * is simply send directly to the server 
 */
BUILT_IN_COMMAND(quotecmd)
{
	int	refnum = from_server;
	int	urlencoded = 0;

	if (*command == 'X')
	{
	    while (args && (*args == '-' || *args == '/'))
	    {
		char *flag = next_arg(args, &args);

		if (!my_strnicmp(flag + 1, "SERVER", 1)) /* SERVER */
		{
			char *s;
			int sval;

			/* No argument means no commands means ignore it. */
			if (!(s = next_arg(args, &args)))
				return;

			sval = str_to_servref(s);
			if (!is_server_open(sval))
			{
			   say("XQUOTE: Server %d is not connected", sval);
			   return;
			}
			refnum = sval;
		}
		else if (!my_strnicmp(flag + 1, "URL", 1)) /* URL quoting */
			urlencoded++;
		else if (!my_strnicmp(flag + 1, "ALL", 1)) /* ALL */
		{
			int	i;

			if (args && *args)
			{
				for (i = 0; i < server_list_size(); i++)
				{
					if (is_server_registered(i))
					    send_to_aserver(i, "%s", args);
				}
			}
			return;
		}
		else		/* End option processing on unknown arg. */
			break;
	    }
	}

	if (urlencoded)
	{
		char *	dest;
		size_t  destlen;

		if (!(dest = transform_string_dyn("-URL", args, 0, &destlen)))
		{
			yell("XQUOTE -U: Could not urldecode [%s]", args);
			return;		/* It failed. bail. */
		}

		send_to_aserver_raw(refnum, destlen, dest);
		new_free(&dest);
	}
	else if (args && *args)
		send_to_aserver(refnum, "%s", args);
}

BUILT_IN_COMMAND(redirect)
{
	const char	*who;

	if ((who = next_arg(args, &args)) == NULL)
	{
		say("%s", "Usage: /REDIRECT <nick|channel|=dcc|%process|/command|@filedescriptor|\"|0> <cmd>");
		return;
	}

	if (from_server == NOSERV)
	{
		say("You may not use /REDIRECT here.");
		return;
	}

	if (!strcmp(who, "*") && !(who = get_window_echannel(0)))
	{
		say("Must be on a channel to redirect to '*'");
		return;
	}

	if (is_me(from_server, who))
	{
		say("You may not redirect output to yourself");
		return;
	}

	/*
	 * Added by Chaos: Fixes the problem with /redirect when 
	 * redirecting to a dcc chat sessions that isn't active or 
	 * doesn't exist.
	 */
	if ((*who == '=') && !is_number(who + 1) && !dcc_chat_active(who + 1)) 
	{
		say("You don't have an active DCC CHAT to %s",who + 1);
		return;
	}

	/*
	 * Turn on redirect, and do the thing.
	 */
	set_server_redirect(from_server, who);
	set_server_sent(from_server, 0);
	runcmds(args, subargs);

	/*
	 * If we've queried the server, then we wait for it to
	 * reply, otherwise we're done.
	 */
	if (get_server_sent(from_server))
		send_to_server("***%s", who);
	else
		set_server_redirect(from_server, NULL);
}

BUILT_IN_COMMAND(squitcmd)
{
	char *server = NULL;
	char *reason = NULL;

	if (!(server = new_next_arg(args, &reason)))
	{
		say("Usage: /SQUIT <server> [<reason>]");
		return;
	}
	if (reason && *reason)
		send_to_server("SQUIT %s :%s", server, reason);
	else
		send_to_server("SQUIT %s", server);
}

BUILT_IN_COMMAND(send_2comm)
{
	const char *target;
	char *reason = NULL;

	if (!(target = next_arg(args, &reason)))
		target = empty_string;
	if (!reason || !*reason)
		reason = LOCAL_COPY(empty_string);

	if (!target || !*target || !strcmp(target, "*"))
	{
		target = get_window_echannel(0);
		if (!target || !*target)
			target = "*";	/* what-EVER */
	}

	if (reason && *reason)
	{
		const char *recode_text;
		char *extra = NULL;

		recode_text = outbound_recode(target, from_server, reason, &extra);
		send_to_server_with_payload(recode_text, "%s %s", command, target);
		new_free(&extra);
	}
	else
		send_to_server("%s %s", command, target);
}

/*
 * send_comm: the generic command function.  Uses the full command name found
 * in 'command', combines it with the 'args', and sends it to the server 
 */
BUILT_IN_COMMAND(send_comm)
{
	if (args && *args)
		send_to_server("%s %s", command, args);
	else
		send_to_server("%s", command);
}

/*
 * Usage: INVITE #channel nickname1 nickname2 nickname3
 *        INVITE nickname #channel1 #channel2 #channel3
 *	  INVITE nickname
 */
BUILT_IN_COMMAND(send_invite)
{
	const char *arg;
	const char *nick = NULL;
	const char *channel = NULL;
	const char *currchan;
	int	invites = 0;

	currchan = get_window_echannel(0);
	if (!currchan || !*currchan)
		currchan = "*";		/* what-EVER */

	while ((arg = next_arg(args, &args)))
	{
	    if (!strcmp(arg, "*"))
		channel = currchan;
	    else if (is_channel(arg))
		channel = arg;
	    else
		nick = arg;

	    if (channel && nick)
	    {
		send_to_server("%s %s %s", command, nick, channel);
		invites++;
	    }
	}

	if (!invites && nick)
	{
	    if (strcmp(currchan, "*"))
	    {
		send_to_server("%s %s %s", command, nick, currchan);
		invites++;
	    }
	    else
		say("You are not on a channel");
	}

	if (!invites)
	    say("Usage: %s [#channel] nickname", command);
}


/*
 * send_kick: sends a kick message to the server.  Works properly with
 * kick comments.
 */
BUILT_IN_COMMAND(send_kick)
{
	char	*kickee;
	const char	*comment;
	const char	*channel;
	const char *	recode_text;
	char *	extra = NULL;

	char usage[] = "Usage: KICK <channel|*> <nickname> [comment]";

	if (!(channel = next_arg(args, &args)))
	{
		yell("%s", usage);
		return;
	}

	if (!(kickee = next_arg(args, &args)))
	{
		yell("%s", usage);
                return;
	}

	comment = args?args:empty_string;
	if (!strcmp(channel, "*"))
		channel = get_window_echannel(0);

	recode_text = outbound_recode(channel, from_server, comment, &extra);
	send_to_server_with_payload(recode_text, "KICK %s %s", channel, kickee);
	new_free(&extra);
}

/*
 * send_channel_com: does the same as send_com for command where the first
 * argument is a channel name.  If the first argument is *, it is expanded
 * to the current channel (a la /WHO).
 */
BUILT_IN_COMMAND(send_channel_com)
{
	char	*ptr;
	const char	*s;

	char usage[] = "Usage: %s <*|#channel> [arguments]";

        ptr = next_arg(args, &args);

	if (ptr && !strcmp(ptr, "*"))
	{
		if ((s = get_window_echannel(0)) != NULL)
			send_to_server("%s %s %s", command, s, args?args:empty_string);
		else
			say("%s * is not valid since you are not on a channel", command);
	}
	else if (ptr)
		send_to_server("%s %s %s", command, ptr, args?args:empty_string);
	else
		yell(usage, command);
}

/* The SENDLINE command.. */
BUILT_IN_COMMAND(sendlinecmd)
{
	int	server;
	int	display;

	server = from_server;
	display = swap_window_display(1);
	parse_statement(args, 1, NULL);
	update_input(NULL, UPDATE_ALL);
	swap_window_display(display);
	from_server = server;
}

BUILT_IN_COMMAND(setenvcmd)
{
	char *env_var;

	if ((env_var = next_arg(args, &args)) != NULL)
	{
		if (*env_var == '-' && empty(args))
			unsetenv(env_var + 1);
		else 
		{
			unsetenv(env_var);
			setenv(env_var, args, 1);
		}
	}
	else
		say("Usage: SETENV [-]<var-name> [<value>]");
}

BUILT_IN_COMMAND(shift_cmd)
{
	extern char *function_shift(char *);
        char *blah = function_shift(args);
        new_free(&blah);
}

BUILT_IN_COMMAND(sleepcmd)
{
	char	*arg;
	float	nms;

	if ((arg = next_arg(args, &args)) != NULL)
	{
		nms = atof(arg);
		my_sleep(nms);
	}
	else
		say("Usage: SLEEP <seconds>");
}


BUILT_IN_COMMAND(stackcmd)
{
	char	*arg;
	int	len, type;

	if ((arg = next_arg(args, &args)) != NULL)
	{
		len = strlen(arg);
		if (!my_strnicmp(arg, "PUSH", len))
			type = STACK_PUSH;
		else if (!my_strnicmp(arg, "POP", len))
			type = STACK_POP;
		else if (!my_strnicmp(arg, "LIST", len))
			type = STACK_LIST;
		else
		{
			say("%s is unknown stack verb", arg);
			return;
		}
	}
	else
	{
		say("Need operation for STACK");
		return;
	}
	if ((arg = next_arg(args, &args)) != NULL)
	{
		char *n;

		len = strlen(arg);
		if (!my_strnicmp(arg, "ON", len))
			do_stack_on(type, args);
		else if (!my_strnicmp(arg, "ALIAS", len))
		{
		    n = remove_brackets(args, subargs);
		    if (type == STACK_PUSH)
		    {
			if (stack_push_cmd_alias(n))
			    say("Can't push ALIAS %s", n);
		    }
		    else if (type == STACK_POP)
		    {
			if (stack_pop_cmd_alias(n))
			    say("Can't pop ALIAS %s", n);
		    }
		    else
		    {
			if (stack_list_cmd_alias(n))
			    say("Can't list ALIAS %s", n);
		    }
		    new_free(&n);
		}
		else if (!my_strnicmp(arg, "ASSIGN", len))
		{
		    n = remove_brackets(args, subargs);
		    if (type == STACK_PUSH)
		    {
			if (stack_push_var_alias(n))
			    say("Can't push ASSIGN %s", n);
		    }
		    else if (type == STACK_POP)
		    {
			if (stack_pop_var_alias(n))
			    say("Can't pop ASSIGN %s", n);
		    }
		    else
		    {
			if (stack_list_var_alias(n))
			    say("Can't list ASSIGN %s", n);
		    }
		    new_free(&n);
		}
		else if (!my_strnicmp(arg, "SET", len))
		{
		    n = remove_brackets(args, subargs);
		    if (type == STACK_PUSH)
		    {
			if (stack_push_builtin_var_alias(n))
			    say("Can't push SET %s", n);
		    }
		    else if (type == STACK_POP)
		    {
			if (stack_pop_builtin_var_alias(n))
			    say("Can't pop SET %s", n);
		    }
		    else
		    {
			if (stack_list_builtin_var_alias(n))
			    say("Can't list SET %s", n);
		    }
		    new_free(&n);
		}
		else if (!my_strnicmp(arg, "BIND", len))
			do_stack_bind(type, args);
		else
		{
			say("%s is not a valid STACK type", arg);
			return;
		}
	}
	else
	{
		say("Need stack type for STACK");
		return;
	}
}

/* timercmd moved to 'timer.c' */

BUILT_IN_COMMAND(unshift_cmd)
{
	extern char *function_unshift(char *);
        char *blah = function_unshift(args);
        new_free(&blah);
}

BUILT_IN_COMMAND(usleepcmd)
{
	char *	arg;
	double	interval;

	if ((arg = next_arg(args, &args)))
	{
		interval = my_atol(arg) / 1000000.0;
		my_sleep(interval);
	}
	else
		say("Usage: USLEEP <usec>");
}


/* version: does the /VERSION command with some IRCII version stuff */
BUILT_IN_COMMAND(version)
{
	char	*host;

	if ((host = next_arg(args, &args)) != NULL)
		send_to_server("VERSION %s", host);
	else
	{ 
		say ("Client: ircII %s (Commit Id: %ld) (Internal Version: %s)", irc_version, commit_id, internal_version);
		send_to_server("VERSION");
	}
}

BUILT_IN_COMMAND(waitcmd)
{
	char	*ctl_arg = next_arg(args, &args);

	if (ctl_arg && !my_strnicmp(ctl_arg, "-cmd", 2))
		server_passive_wait(from_server, args);

	else if (ctl_arg && !my_strnicmp(ctl_arg, "for", 3))
	{
		set_server_sent(from_server, 0);
		lock_stack_frame();
		runcmds(args, subargs);
		unlock_stack_frame();
		if (get_server_sent(from_server))
			server_hard_wait(from_server);
		set_server_sent(from_server, 0);	/* Reset it again */
	}

	else if (ctl_arg && *ctl_arg == '%')
	{
		if (is_valid_process(ctl_arg))
		{
			if (args && *args)
			{
				if (!my_strnicmp(args, "-cmd ", 4))
					next_arg(args, &args);
				add_process_wait(ctl_arg, args);
			}
			else
			{
				char	reason[1024];

				snprintf(reason, 1024, "WAIT on EXEC %s", 
						ctl_arg);
				lock_stack_frame();
				while (is_valid_process(ctl_arg))
					io(reason);
				unlock_stack_frame();
			}
		}
	}
	else if (ctl_arg && *ctl_arg == '=')
	{
		ctl_arg++;
		wait_for_dcc(ctl_arg);
	}
	else if (ctl_arg)
		yell("Unknown argument to /WAIT");
	else
	{
		server_hard_wait(from_server);
		set_server_sent(from_server, 0);
	}
}

/*
 * whois: the WHOIS and WHOWAS commands. 
 */
BUILT_IN_COMMAND(whois)
{
	char *stuff = NULL;

	if (!strcmp(command, "WHOWAS"))
	{
		char *word_one = next_arg(args, &args);
		if (!is_string_empty(args))
			malloc_sprintf(&stuff, "%s %s", word_one, args);
		else if (!is_string_empty(word_one))
			malloc_sprintf(&stuff, "%s", word_one);
		else
			malloc_sprintf(&stuff, "%s", get_server_nickname(from_server));

		send_to_server("WHOWAS %s", stuff);
		new_free(&stuff);
	}
	else /* whois command */
		send_to_server("WHOIS %s", is_string_empty(args) ? 
					get_server_nickname(from_server) : 
					args);
}

/*
 * type: The TYPE command.  This parses the given string and treats each
 * character as though it were typed in by the user.  Thus key bindings
 * are used for each character parsed.  Special case characters are control
 * character sequences, specified by a ^ follow by a legal control key.
 * Thus doing "/TYPE ^B" will be as tho ^B were hit at the keyboard,
 * probably moving the cursor backward one character.
 *
 * This was moved from keys.c, because it certainly does not belong there,
 * and this seemed a reasonable place for it to go for now.
 */
BUILT_IN_COMMAND(typecmd)
{
        int     c;
        char    key;

        for (; *args; args++)
        {
                if (*args == '^')
                {
                        args++;
                        if (*args == '?')
                                key = '\177';
                        else if (*args)
                        {
                                c = *args;
                                if (islower(c))
                                        c = toupper(c);
                                if (c < 64)
                                {
                                        say("Invalid key sequence: ^%c", c);
                                        return;
                                }
                                key = c - 64;
                        }
                        else
                                break;
                }
                else
                {
                        if (*args == '\\')
                                args++;
                        if (*args)
                                key = *args;
                        else
                                break;
                }

		translate_user_input(key);
        }
}

BUILT_IN_COMMAND(xtypecmd)
{
	char	*arg;
	int	code_point;

	if (*args == '-' || *args == '/')
	{
		int saved = (int)*args;

		args++;
		if ((arg = next_arg(args, &args)) != NULL)
		{
			if (!my_strnicmp(arg, "LITERAL", 1))
			{
				while ((code_point = next_code_point((const unsigned char **)&args, 1)))
				{
					input_add_character(code_point, NULL);
				}
			}
			else
				say("Unknown flag -%s to XTYPE", arg);
			return;
		}

		input_add_character(saved, NULL);
	}
	else
		typecmd(command, args, empty_string);
	return;
}

/*
 * 
 * The rest of this file is the stuff that is not a command but is used
 * by the commands in this file.  These are considered to be "overhead".
 *
 */

/*
 * Returns 1 if the given built in command exists,
 * Returns 0 if not.
 */
int	command_exist (char *command)
{
	char *name;
	void *	args = NULL;
	void 	(*func) (const char *, char *, const char *) = NULL;

	if (!command || !*command)
		return 0;
	name = LOCAL_COPY(command);
	upper(name);

	get_cmd_alias(name, &args, &func);
	if (func == NULL)
		return 0;
	return 1;
}

int	redirect_text (int to_server, const char *nick_list, const char *text, char *command, int hook)
{
static	int 	recursion = 0;
	int 	old_from_server = from_server;
	int	allow = 0;
	int	retval;

	from_server = to_server;
	if (recursion++ == 0)
		allow = do_hook(REDIRECT_LIST, "%s %s", nick_list, text);

	/* Suppress output */
	if (strcmp(nick_list, "0") == 0 || *nick_list == '@') 
		retval = 1;
	else
		retval = 0;

	/* 
	 * Dont hook /ON REDIRECT if we're being called recursively
	 */
	if (allow)
		send_text(from_server, nick_list, text, command, hook, 0);

	recursion--;
	from_server = old_from_server;
	return retval;
}


struct target_type
{
	char *nick_list;
	const char *message;
	int  hook_type;
	const char *command;
	const char *format;
	int  mask;
};


/*
 * The whole shebang.
 *
 * The message targets are parsed and collected into one of 4 buckets.
 * This is not too dissimilar to what was done before, except now i 
 * feel more comfortable about how the code works.
 *
 * Bucket 0 -- Unencrypted PRIVMSGs to nicknames
 * Bucket 1 -- Unencrypted PRIVMSGs to channels
 * Bucket 2 -- Unencrypted NOTICEs to nicknames
 * Bucket 3 -- Unencrypted NOTICEs to channels
 *
 * All other messages (encrypted, and DCC CHATs) are dispatched 
 * immediately, and seperately from all others.  All messages that
 * end up in one of the above mentioned buckets get sent out all
 * at once.
 *
 * XXXX --- Its super super important that you *never* call yell(),
 * put_it(), or anything like that which would end up calling add_to_window().
 * add_to_window() can get in an infinite cycle with send_text() if the
 * user is /redirect'ing.  send_text() tries to prevent the user from being
 * able to send stuff to the window by never hooking /ONs if we're already
 * recursing -- so whoever is hacking on this code, its also up to you to 
 * make sure that YOU dont send anything to the screen without checking first!
 */
/* SENDTEXT -- Don't delete this, I search for it! */
void 	send_text (int server, const char *nick_list, const char *text, const char *command, int hook, int already_encoded)
{
	int 	i, 
		old_server;
	char 	*current_nick,
		*next_nick,
		*line;
	Crypt	*key;
	int	old_window_display;
	int	old_from_server;
static	int	recursion = 0;
	const char *target_encoding;
	char	*buf2;
	size_t	buf2len;
	char *	extra = NULL;
	const char	*recode_text;

	/*
	 * XXXX - Heaven help us.
	 */
struct target_type target[4] = 
{	
	{NULL, NULL, SEND_MSG_LIST,     "PRIVMSG", "*%s*> %s" , LEVEL_MSG }, 
	{NULL, NULL, SEND_PUBLIC_LIST,  "PRIVMSG", "%s> %s"   , LEVEL_PUBLIC },
	{NULL, NULL, SEND_NOTICE_LIST,  "NOTICE",  "-%s-> %s" , LEVEL_NOTICE },
	{NULL, NULL, SEND_NOTICE_LIST,  "NOTICE",  "-%s-> %s" , LEVEL_NOTICE }
};

	if (!nick_list || !text)
		return;

	/*
	 * If we are called recursively, it is because the user has 
	 * /redirect'ed the output, or the user is sending something from
	 * their /on send_*.  In both of these cases, an infinite loop
	 * could occur.  To defeat the /redirect, we do not output to the
	 * screen.  To defeat the /on send_*, we do not offer /on events.
	 *
	 * XXX - The 'hook == -1' hack is there until I write something 
	 * more decent than recursion to handle /msg -<server>/<target>.
	 *
	 * I've gotten into too many scrapes with infinite recursion here.
	 * So, if you recurse twice, then something is seriously wrong
	 * and we will just flat out refuse to send the message.
	 */
	if (hook == -1)
		hook = 1;
	else if (recursion == 1)
		hook = 0;
	else if (recursion >= 2)
		return;
	recursion++;

	old_from_server = from_server;
	from_server = server;
	old_window_display = swap_window_display(hook);
	next_nick = LOCAL_COPY(nick_list);

	if (command && !strcmp(command, "MSG"))
		command = "PRIVMSG";		/* XXX */

	while ((current_nick = next_nick))
	{
	    if ((next_nick = strchr(current_nick, ',')))
		*next_nick++ = 0;

	    if (!*current_nick)
		continue;

	    /* 
	     * The OUTBOUND text is ALWAYS recoded.
	     * In theory, 'text' should always be in utf8..
	     * But CTCPs are pre-encoded, so don't double recode those.
	     */
	    if (!already_encoded)
		recode_text = outbound_recode(current_nick, from_server, text, &extra);
	    else
		recode_text = text;

	    if (*current_nick == '%')
		text_to_process(current_nick, recode_text, 1);

	    /*
	     * Blank lines may be sent to /exec'd processes, but
	     * not to any other targets.  Once we know that the target
	     * is not an exec'd process, we make sure the line is not
	     * blank.
	     */
	    else if (!text || !*text)
		;

	    /*
	     * Target 0 (an invalid irc target) is the "sink": messages
	     * go in but they don't go out.
	     */
	    else if (!strcmp(current_nick, "0"))
		;

	    /*
	     * Targets that start with @ are numbers that refer to 
	     * $open() files.  This used to be used for DCC TALK but
	     * we haven't supported that for 5 years.
	     */
	    else if (*current_nick == '@' && is_number(current_nick + 1))
		target_file_write(current_nick, text);
	    else if (*current_nick == '@' 
			&& (current_nick[1] == 'W' || current_nick[1] == 'w')
			&& is_number(current_nick + 2))
		target_file_write(current_nick, text);
	    else if (*current_nick == '@' 
			&& (current_nick[1] == 'L' || current_nick[1] == 'l')
			&& is_number(current_nick + 2))
		target_file_write(current_nick, text);
	    else if (*current_nick == '@' 
			&& (current_nick[1] == 'E' || current_nick[1] == 'e'))
	    {
		/* XXX this is probably cheating. */
		char *ptr = NULL;
		malloc_sprintf(&ptr, "-W %s %s",
				current_nick + 2, text);
		xechocmd("XECHO", ptr, NULL);
		new_free(&ptr);
	    }
	    else if (*current_nick == '"')
		send_to_server("%s", text);

	    else if (*current_nick == '/')
	    {
		line = malloc_strdup3(current_nick, space, text);
		parse_statement(line, 0, empty_string);
		new_free(&line);
	    }
	    else if (*current_nick == '=')
	    {
		if (!is_number(current_nick + 1) &&
			!dcc_chat_active(current_nick + 1))
		{
			yell("No DCC CHAT connection open to %s", 
				current_nick + 1);
			new_free(&extra);
			continue;
		}

		if ((key = is_crypted(current_nick, -1, NULL)) != 0)
		{
			char *breakage = LOCAL_COPY(recode_text);
			line = crypt_msg(breakage, key);
		}
		else
			line = malloc_strdup(recode_text);

		old_server = from_server;
		from_server = NOSERV;
		dcc_chat_transmit(current_nick + 1, line, text, command, hook);
		from_server = old_server;
		set_server_sent_nick(from_server, current_nick);
		new_free(&line);
	    }
	    else if (*current_nick == '-' && strchr(current_nick + 1, '/'))
	    {
		int	servref;
		char *	servername;
		char *	msgtarget;

		servername = current_nick + 1;
		msgtarget = strchr(servername, '/');
		*msgtarget++ = 0;

		if ((servref = str_to_servref(servername)) == NOSERV)
		{
			yell("Unknown server [%s] in target", servername);
			new_free(&extra);
			continue;
		}

		/* XXX -- Recursion is a hack. (but it works) */
		send_text(servref, msgtarget, text, command, hook ? -1 : 0, already_encoded);
		new_free(&extra);
		continue;
	    }
	    else
	    {
		char *	copy = NULL;

		/* XXX Should we check for invalid from_server here? */

		if (get_server_doing_notice(from_server) > 0)
		{
			say("You cannot send a message from within ON NOTICE");
			new_free(&extra);
			continue;
		}

		i = is_channel(current_nick);
		if (get_server_doing_privmsg(from_server) || 
				(command && !strcmp(command, "NOTICE")))
			i += 2;

		if ((key = is_crypted(current_nick, from_server, NULL)))
		{
			int	l;

			l = message_from(current_nick, target[i].mask);
			if (hook && do_hook(target[i].hook_type, "%s %s", 
						current_nick, text))
				put_it(target[i].format, current_nick, text);

			copy = LOCAL_COPY(recode_text);
			line = crypt_msg(copy, key);
			send_to_server_with_payload(line, "%s %s", 
					target[i].command, current_nick);

			set_server_sent_nick(from_server, current_nick);
			new_free(&line);
			pop_message_from(l);
		}
		else if (extra)		/* Message was transcoded */
		{
			int	l;

			l = message_from(current_nick, target[i].mask);

			/*
			 * TEXT is the original UTF8 string.
			 * Recode_text is converted FOR IRC USE ONLY.
			 */
			if (hook && do_hook(target[i].hook_type, "%s %s", 
						current_nick, text))
				put_it(target[i].format, current_nick, text);

			send_to_server_with_payload(recode_text, "%s %s", 
					target[i].command, current_nick);
			set_server_sent_nick(from_server, current_nick);

			pop_message_from(l);
		}
		else
		{
			if (i == 0)
				set_server_sent_nick(from_server, current_nick);
			if (target[i].nick_list)
				malloc_strcat(&target[i].nick_list, ",");
			malloc_strcat(&target[i].nick_list, current_nick);
			if (!target[i].message)
				target[i].message = text;
		}
	    }

	    new_free(&extra);
	}

	for (i = 0; i < 4; i++)
	{
		int	l;

		if (!target[i].message)
			continue;

		l = message_from(target[i].nick_list, target[i].mask);
		if (hook && do_hook(target[i].hook_type, "%s %s", 
				    target[i].nick_list, target[i].message))
		    put_it(target[i].format, target[i].nick_list, 
				target[i].message);

		send_to_server_with_payload(target[i].message,
				"%s %s", 
				target[i].command, 
				target[i].nick_list);

		new_free(&target[i].nick_list);
		target[i].message = NULL;

		pop_message_from(l);
	}

	swap_window_display(old_window_display);
	from_server = old_from_server;
	recursion--;
}

/***************************************************************************/
static char *	parse_line_alias_special (const char *name, const char *what, const char *args, void *arglist, int function);

/* 
 * Execute a block of ircII code (``what'') as a lambda function.
 * The block will be run in its own private local name space, and will be
 * given its own $FUNCTION_RETURN variable, which it will return.
 * 
 *      name - If name is not NULL, this lambda function will be treated as
 *              an atomic scope, and will not be able to change the enclosing
 *              scope's local variables.  If name is NULL, it will have access
 *              to the enclosing local variables.
 *      what - The lambda function itself; a block of ircII code.
 *      args - The value of $* for the lambda function
 */   
char *  call_lambda_function (const char *name, const char *what, const char *args)
{
	/* 
	 * Explanation for why 'args' is (const char *) and not (char *).
	 * 'args' is $*, and is passed in from above.  $* might be changed
	 * if we were executing an alias that had a parameter list, and so
	 * parse_line_alias_special must take a (char *).  But the only time
	 * that ``args'' would be changed is if the ``arglist'' param was 
	 * not NULL.  Since we hardcode ``arglist == NULL'' it is absolutely
	 * guaranteed that ``args'' will not be touched, and so, it can safely
	 * be treated as (const char *) by whoever called us.  Unfortunately,
	 * C does not allow you to treat a pointer as conditionally const, so
	 * we just use the cast to hide that.  This is absolutely safe.
	 */
	return parse_line_alias_special(name, what, (char *)
#ifdef HAVE_INTPTR_T
	 					    (intptr_t)
#endif
							args, NULL, 1);
}

/*
 * Execute a block of ircII code as a lambda command, but do not set up a 
 * new FUNCTION_RETURN value and do not return that value.
 */
void	call_lambda_command (const char *name, const char *what, const char *args)
{
	parse_line_alias_special(name, what, (char *)
#ifdef HAVE_INTPTR_T
	 					    (intptr_t)
#endif
							args, NULL, 0);
}

/************************************************************************/
/*
 * Execute a named user alias block of ircII code as a function.  Set up a
 * new FUNCTION_RETURN value and return it.
 */
char 	*call_user_function	(const char *alias_name, const char *alias_stuff, char *args, void *arglist)
{
	return parse_line_alias_special(alias_name, alias_stuff, args, 
						arglist, 1);
}

/*
 * Execute a named user alias block of ircII code as a command.  Do not set
 * up a new FUNCTION_RETURN and do not return that value.
 */
void	call_user_command (const char *alias_name, const char *alias_stuff, char *args, void *arglist)
{
	parse_line_alias_special(alias_name, alias_stuff, args, 
					arglist, 0);
}

/*
 * parse_line_alias_special -- Run a generalized block of code
 * Arguments:
 *	name	 - If non-NULL, the name of a new atomic scope.
 *		   If NULL, this is not a new atomic scope.
 *	what	 - The block of code to execute
 *	args	 - The value of $*
 *	arglist	 - Local variables to auto-assign, shifting off of $*
 *		   If NULL, do not change $*
 *	function - If 1, this is a function call.  Set up new $function_return
 *			and return that value.  CALLER MUST FREE THIS VALUE.
 *		   If 0, this is not a function call.  Return value is NULL.
 *
 * Special note -- This function is really only suitable for running new atomic
 *		scopes, because it set up a new /RETURNable block.  If you 
 *		just want to run a block of code inside an existing scope
 *		then use the runcmds() function.
 */
static char *	parse_line_alias_special (const char *name, const char *what, const char *orig_subargs, void *arglist, int function)
{
	int	old_last_function_call_level = last_function_call_level;
	char *	result = NULL;
	int	localvars = name ? 1 : 0;	/* 'name' could be free()d */
	char *	subargs;

	if (!orig_subargs)
		subargs = LOCAL_COPY(empty_string);
	else
		subargs = LOCAL_COPY(orig_subargs);

	if (localvars && !make_local_stack(name))
	{
		yell("Could not run (%s) [%s]; too much recursion", 
				name ? name : "<unnamed>", what);
		if (function)
			return malloc_strdup(empty_string);
		else
			return NULL;
	}
	if (arglist)
		prepare_alias_call(arglist, &subargs);
	if (function)
	{
		last_function_call_level = wind_index;
		add_local_alias("FUNCTION_RETURN", empty_string, 0);
	}

	will_catch_return_exceptions++;
	parse_block(what, subargs, 0);
	will_catch_return_exceptions--;
	return_exception = 0;

	if (function)
	{
		result = get_variable("FUNCTION_RETURN");
		last_function_call_level = old_last_function_call_level;
	}
	if (localvars)
		destroy_local_stack();

	return result;
}

void	runcmds (const char *what, const char *subargs)
{
	if (!subargs)
		subargs = empty_string;
	parse_block(what, subargs, 0);
}

void    runcmds_with_arglist (const char *what, char *args, const char *subargs)
{
	ArgList *arglist = NULL;
	if (!subargs)
		subargs = LOCAL_COPY(empty_string);
	arglist = parse_arglist(args);
	parse_line_alias_special(NULL, what, subargs, arglist, 0);

	destroy_arglist(&arglist);
}

/*
 * parse_block: execute a block of ircII statements (in a C string)
 *
 * Arguments:
 *	org_line  - A block of ircII statements.  They will be run in sequence.
 *	args	  - The value of $* to use for this block.
 *		  - (DEPRECATED) May be NULL if 'orig_line' is a single
 *		    statement that must not be subject to $-expansion
 *		    (old /load, /sendline, /on input, dumb mode input)
 *		    *** IF args IS NULL, name MUST ALSO BE NULL! ***
 *	interactive - (DEPRECATED) 1 if this block came from user input.
 *		    0 if this block came from anywhere else.
 *		    (This is used in parse_statement to decide how to handle /s)
 *
 * If args is NULL, the 'org_line' argument is passed straight through to 
 * parse_statement().
 *
 * If args is not NULL, the 'orig_line' argument is parsed, statement by 
 * statement, and each statement is passed through to parse_statement().
 */
static void	parse_block (const char *org_line, const char *args, int interactive)
{
	char	*line = NULL;
	ssize_t	span;

	/* 
	 * Explicit statements (from /load or /on input or /sendline)
	 * are passed straight through to parse_statement without mangling.
	 */
	if (args == NULL)
	{
		parse_statement(org_line, interactive, args);
		return;
	}

	/*
	 * We will be mangling 'org_line', so we make a copy to work with.
	 */
	if (!org_line)
		panic(1, "org_line is NULL and it shouldn't be.");
	line = LOCAL_COPY(org_line);

	/*
	 * Otherwise, if the command has arguments, then:
	 *	* It is being run as part of an alias/on/function
	 *	* It is being /LOADed while /set input_aliases ON
	 *	* It is typed at the input prompt while /set input_aliases ON
	 */
    while (line && *line)
    {
	/*
	 * Now we expand the first command in this set, so as to
	 * include any variables or argument-expandos.  The "line"
	 * pointer is set to the first character of the next command
	 * (if any).  
	 */
	if ((span = next_statement(line)) < 0)
		break;

	if (line[span] == ';')
		line[span++] = 0;

	/*
	 * Now we run the command.
	 */
	parse_statement(line, interactive, args);

	/*
	 * If an exception was thrown by this command, then we just
	 * stop processing and let the exception be caught by whoever
	 * is catching it above us.
	 */
	if ((will_catch_break_exceptions && break_exception) ||
	    (will_catch_return_exceptions && return_exception) ||
	    (will_catch_continue_exceptions && continue_exception) ||
	     system_exception)
		break;

	/* Willfully ignore spaces after semicolons. */
	line += span;
	while (line && *line && isspace(*line))
		line++;

	/*
	 * We repeat this as long as we have more commands to parse.
	 */
    }

	return;
}


/*
 * parse_statement:  Execute a single statement.  Each statement is either a
 * block statement (surrounded by {}s), an expression statement (surrounded by
 * ()s or starts with a @), or a command statement (anything else). 
 *
 * Args:
 *	line	- A single ircII statement
 *	interactive - 1 if the statement is run because of user input
 *		      0 if the statement is run for any other reason
 *	subargs	- The value of $* to use for this statement
 *		  (DEPRECATED) May be NULL if $-expansion is suppressed.
 *
 * You usually don't want to run this function unless you *know for sure*
 * that what you have in hand is a statement and nothing else.  If you are
 * not sure, better to call runcmds() and let it figure it out.
 *
 * Known legitimate callers:
 *	parse_block() (which breaks a block into statements)
 *	/LOAD -STD (which breaks a file into statements)
 *	SEND_LINE key binding (which processes the input line as a statement)
 *	Input in dumb mode (ditto)
 *	/SENDLINE  (which simulates processing of the input line)
 */
int	parse_statement (const char *stmt, int interactive, const char *subargs)
{
static	unsigned 	level = 0;
	unsigned 	old_window_display;
	int		old_display_var;
	int		cmdchar_used = 0;
	int		quiet = 0;
	char *		this_stmt;

	if (!stmt || !*stmt)
		return 0;

	this_stmt = LOCAL_COPY(stmt);
	set_current_command(this_stmt);

	old_window_display = get_window_display();
	old_display_var = get_int_var(DISPLAY_VAR);

	if (get_int_var(DEBUG_VAR) & DEBUG_COMMANDS)
		privileged_yell("Executing [%d] %s", level, stmt);
	level++;

	/* 
	 * Once and for all i hope i fixed this.  What does this do?
	 * well, at the beginning of your input line, it looks to see
	 * if youve used any ^s or /s.  You can use up to one ^ and up
	 * to two /s.  When any character is found that is not one of
	 * these characters, it stops looking.
	 */
	for (; *stmt; stmt++)
	{
	    /* 
	     * ^ turns off window_display for this statement.
	     * The user must do /^ at the input line for this.
	     */
	    if (*stmt == '^' && (!interactive || cmdchar_used))
	    {
		if (quiet++ > 1)
			break;
	    }
	    /* / is the command char.  1 or 2 are allowed */
	    else if (*stmt == '/')
	    {
		if (cmdchar_used++ > 2)
			break;	
	    }
	    else
		break;	
	}

	if (quiet)
		set_window_display(0);

	/* 
	 * Statement in interactive mode w/o command chars sends the
	 * statement to the current target.
	 */
	if (interactive && cmdchar_used == 0)
		send_text(from_server, get_window_target(0), stmt, NULL, 1, 0);

	/*
	 * Statement that looks like () {} is an block-with-arglist statement
	 */
	else if (*stmt == '(')
	{
		char *arglist;
		char *block;
		char *copy;

		/*
		 * If any syntax errors are encountered, fall down to
		 * the expression handler (which handles it in a backwards
		 * compatable way
		 */
		copy = LOCAL_COPY(stmt);
		if (!(arglist = next_expr(&copy, '(')))
			goto expression_statement;
		while (*copy && my_isspace(*copy))
			copy++;
		if (*copy != '{')
			goto expression_statement;
		if (!(block = next_expr(&copy, '{')))
			goto expression_statement;

		runcmds_with_arglist(block, arglist, subargs);
	}

	/* 
	 * Statement that starts with a { is a block statement.
	 */
	else if (*stmt == '{')
	{
	    char *stuff;
	    char *copy;

	    copy = LOCAL_COPY(stmt);
	    if (!(stuff = next_expr(&copy, '{'))) 
	    {
		privileged_yell("Unmatched { around [%-.20s]", copy); 
		stuff = copy + 1;
	    }

	    parse_block(stuff, subargs, interactive);
	}

	/*
	 * Statement that starts with @ or surrounded by ()s is an
 	 * expression statement.
	 */
	else if ((*stmt == '@') || (*stmt == '('))
	{
		/*
		 * The expression parser wants to mangle the string it's given.
		 * Normally we would copy the statement as part of the 
		 * expand_alias() step, but since we don't expand expressions,
		 * we just make a local copy.
		 */
		char	*my_stmt, *tmp;

expression_statement:
		my_stmt = LOCAL_COPY(stmt);

		/* Expressions can start with @ or be surrounded by ()s */
		if (*my_stmt == '(')
		{
		    ssize_t	span;

		    if ((span = MatchingBracket(my_stmt + 1, '(', ')')) >= 0)
			my_stmt[1 + span] = 0;
		}

		if ((tmp = parse_inline(my_stmt + 1, subargs)))
			new_free(&tmp);
	}

	/*
	 * Everything else whatsoever is a command statement.
	 */
	else
	{
		char	*cmd, *args;
		const char *alias = NULL;
		void	*arglist = NULL;
		void	(*builtin) (const char *, char *, const char *) = NULL;
		const char *prevcmd = NULL;

		if (subargs != NULL)
			cmd = expand_alias(stmt, subargs); 
		else
			cmd = malloc_strdup(stmt);

		args = cmd;
		while (*args && !isspace(*args))
			args++;
		if (*args)
			*args++ = 0;

		upper(cmd);
		alias = get_cmd_alias(cmd, &arglist, &builtin);

		if (cmdchar_used >= 2)
			alias = NULL;		/* Unconditionally */

		if (alias || builtin) {
			prevcmd = current_command;
			current_command = cmd;
		}
		if (alias) {
			call_user_command(cmd, alias, args, arglist);
		}
		else if (builtin)
			builtin(cmd, args, subargs);
		else if (get_int_var(DISPATCH_UNKNOWN_COMMANDS_VAR))
			send_to_server("%s %s", cmd, args);
		else if (do_hook(UNKNOWN_COMMAND_LIST, "%s%s %s", cmdchar_used >= 2 ? "//" : "", cmd, args))
			say("Unknown command: %s", cmd);

		if (alias || builtin) {
			current_command = prevcmd;
		}

		new_free(&cmd);
	}

	/* 
	 * Just as in /LOAD -- if the user just did /set display,
	 * honor their new value; otherwise, put everything back
	 * the way we found it.
	 */
	if (old_display_var != get_int_var(DISPLAY_VAR))
		set_window_display(get_int_var(DISPLAY_VAR));
	else
		set_window_display(old_window_display);

	level--;
	unset_current_command();
        return 0;
}

/***********************************************************************/
BUILT_IN_COMMAND(breakcmd)
{
	if (!will_catch_break_exceptions)
		say("Cannot BREAK here.");
	else
		break_exception++;
}

BUILT_IN_COMMAND(continuecmd)
{
	if (!will_catch_continue_exceptions)
		say("Cannot CONTINUE here.");
	else
		continue_exception++;
}

BUILT_IN_COMMAND(returncmd)
{
	if (!will_catch_return_exceptions)
		say("Cannot RETURN here.");
	else
	{
		if (args && *args)
			add_local_alias("FUNCTION_RETURN", args, 0);
		return_exception++;
	}
}

BUILT_IN_COMMAND(allocdumpcmd)
{
	malloc_dump(args);
}

BUILT_IN_COMMAND(botmodecmd)
{
#if defined(NO_BOTS) || defined(NO_JOB_CONTROL)
	yell("This client was configured not to allow bots.  Bummer.");
#else
	if (dumb_mode) {
		use_input = 0;
		background = 1;
		my_signal(SIGHUP, SIG_IGN);
		if (!freopen("/dev/null", "w", stdout))
		{
		     yell("Could not redirect stdout to /dev/null.  Can't become a bot.");
		     return;
		}
		new_close(0);
		if (fork())
			_exit(0);
	} else {
		say("Bot mode can only be entered from Dumb mode.");
	}
#endif
}

void	help_topics_commands (FILE *f)
{
	int	x;

	for (x = 0; irc_command[x].name; x++)
		fprintf(f, "command %s\n", irc_command[x].name);
}


