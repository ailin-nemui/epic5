/* $EPIC: commands.c,v 1.101 2004/07/23 00:49:46 jnelson Exp $ */
/*
 * commands.c -- Stuff needed to execute commands in ircII.
 *		 Includes the bulk of the built in commands for ircII.
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright © 1995, 2003 EPIC Software Labs
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
 * Reorganized with ruthless abandon for EPIC3pre2 (summer, 1995)
 *   by Jeremy Nelson (jnelson@acronet.net)
 */

#define __need_putchar_x__
#define __need_term_flush__
#include "irc.h"
#include "alias.h"
#include "alist.h"
#include "sedcrypt.h"
#include "ctcp.h"
#include "dcc.h"
#include "commands.h"
#include "exec.h"
#include "files.h"
#include "history.h"
#include "hook.h"
#include "server.h"
#include "if.h"
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
#include "term.h"
#include "timer.h"
#include "vars.h"
#include "window.h"
#include "who.h"
#include "newio.h"
#include "words.h"
#include "reg.h"

/* 
 * defined to 1 if we are parsing something interactive from the user,
 * 0 if it is not being done interactive (within an alias)
 */
	int interactive = 0;

/* used with input_move_cursor */
#define RIGHT 1
#define LEFT 0

/* used with /save */
#define	SFLAG_ALIAS	0x0001
#define SFLAG_ASSIGN	0x0002
#define	SFLAG_BIND	0x0004
#define SFLAG_NOTIFY	0x0008
#define	SFLAG_ON	0x0010
#define SFLAG_SERVER	0x0020
#define	SFLAG_SET	0x0040
#define SFLAG_ALL	0x00ff

/* The maximum number of recursive LOAD levels allowed */
#define MAX_LOAD_DEPTH 10

/* flags used by e_away */
#define AWAY_ONE                        0
#define AWAY_ALL                        1

/* flags used by parse_line */
#define SECURITY_NO_VARIABLE_COMMAND    1
#define SECURITY_NO_NONINTERACTIVE_EXEC 2
#define SECURITY_NO_NONINTERACTIVE_SET  4

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
static	void	breakcmd	(const char *, char *, const char *);
static	void	commentcmd 	(const char *, char *, const char *);
static	void	continuecmd	(const char *, char *, const char *);
static	void	ctcp 		(const char *, char *, const char *);
static	void	deop 		(const char *, char *, const char *);
static	void	send_to_channel	(const char *, char *, const char *);
static	void	send_to_target	(const char *, char *, const char *);
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
static  void    reconnect_cmd   (const char *, char *, const char *);
static	void	redirect 	(const char *, char *, const char *);
static	void	returncmd	(const char *, char *, const char *);
static	void	save_settings 	(const char *, char *, const char *);
static	void	send_2comm 	(const char *, char *, const char *);
static	void	send_comm 	(const char *, char *, const char *);
static	void	send_kick 	(const char *, char *, const char *);
static	void	send_channel_com (const char *, char *, const char *);
static  void    set_username 	(const char *, char *, const char *);
static	void	setenvcmd	(const char *, char *, const char *);
static	void	squitcmd	(const char *, char *, const char *);
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
static	void	eval_inputlist 	(char *, char *);
static	int	parse_command	(const char *, int, const char *);

/* I hate typedefs, but they sure can be useful.. */
typedef void (*CmdFunc) (const char *, char *, const char *);

/* IrcCommand: structure for each command in the command table */
typedef	struct
{
	const char *	name;		/* what the user types */
	CmdFunc 	func;		/* function that is the command */
}	IrcCommand;

/*
 * irc_command: all the availble irc commands:  Note that the first entry has
 * a zero length string name and a null server command... this little trick
 * makes "/ blah blah blah" to always send the arguments to a channel, 
 * bypassing queries, etc.  Neato.  This list MUST be sorted.
 */
static	IrcCommand irc_command[] =
{
	{ "",		send_to_channel	},
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
	{ "DEFER",	defercmd	},
	{ "DEOP",	deop		},
	{ "DESCRIBE",	describe	},
	{ "DIE",	send_comm	},
	{ "DISCONNECT",	disconnectcmd	}, /* server.c */
        { "DO",         docmd           }, /* if.c */
        { "DUMP",       dumpcmd		}, /* alias.c */
	{ "ECHO",	echocmd		},
	{ "ENCRYPT",	encrypt_cmd	}, /* crypt.c */
	{ "EVAL",	evalcmd		},
	{ "EXEC",	execcmd		}, /* exec.c */
	{ "EXIT",	e_quit		},
        { "FE",         fe		}, /* if.c */
        { "FEC",        fe              }, /* if.c */
	{ "FLUSH",	flush		},
        { "FOR",        forcmd		}, /* if.c */
	{ "FOREACH",	foreach		}, /* if.c */
	{ "HISTORY",	history		}, /* history.c */
	{ "HOOK",	hookcmd		},
	{ "HOSTNAME",	e_hostname	},
	{ "IF",		ifcmd		}, /* if.c */
	{ "IGNORE",	ignore		}, /* ignore.c */
	{ "INFO",	info		},
	{ "INPUT",	inputcmd	},
	{ "INPUT_CHAR",	inputcmd	},
	{ "INVITE",	send_comm	},
	{ "IRCUSER",	set_username	},
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
	{ "PING",	pingcmd		},
	{ "POP",	pop_cmd		},
	{ "PRETEND",	pretend_cmd	},
	{ "PUSH",	push_cmd	},
	{ "QUERY",	query		},
        { "QUEUE",      queuecmd        }, /* queue.c */
	{ "QUIT",	e_quit		},
	{ "QUOTE",	quotecmd	},
	{ "RBIND",	rbindcmd	}, /* keys.c */
	{ "RECONNECT",  reconnect_cmd   },
	{ "REDIRECT",	redirect	},
	{ "REHASH",	send_comm	},
	{ "REPEAT",	repeatcmd	},
	{ "RESTART",	send_comm	},
	{ "RETURN",	returncmd	},
	{ "RPING",	send_comm	},
	{ "SAVE",	save_settings	},
	{ "SAY",	send_to_channel	},
	{ "SEND",	send_to_target	},
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

/* number of entries in irc_command array */
#define	NUMBER_OF_COMMANDS (sizeof(irc_command) / sizeof(IrcCommand)) - 2

void	init_commands (void)
{
	int	i;

	for (i = 0; irc_command[i].name; i++)
	    add_builtin_cmd_alias(irc_command[i].name, irc_command[i].func);
}


/* 
 * Full scale abort.  Does a "save" into the filename in line, and
 * then does a coredump
 */
static void 	really_save (const char *, int, int, int);
BUILT_IN_COMMAND(abortcmd)
{
	const char *filename = next_arg(args, &args);
	int	flags = SFLAG_ALL;

        filename = filename ? filename : "irc.aborted";
	really_save(filename, flags, 0, 0);
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
			if ((arg = strchr(args, ' ')))
				*arg++ = 0;
			else
				arg = endstr(args);

			if (0 == my_strnicmp(args+1, "A", 1))	/* all */
			{
				flag = AWAY_ALL;
				args = arg;
			}
			else if (0 == my_strnicmp(args+1, "O", 1)) /* one */
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

	getcwd(dir, sizeof(dir));
	say("Current directory: %s", dir);
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
		if (!my_strnicmp(arg, "A", 1) || !my_strnicmp(arg+1, "A", 1))
			visible = 0, hidden = 0, all = 1;

		/* UNHOLD */
		else if (!my_strnicmp(arg+1, "U", 1))
			unhold = 1;		/* Obsolete */

		else if (!my_strnicmp(arg+1, "V", 1))
			visible = 1, hidden = 0, all = 1;

		else if (!my_strnicmp(arg+1, "H", 1))
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
	char	*stag;
	int	tag;
	int	type;

	if ((to = next_arg(args, &args)) != NULL)
	{
		if (!strcmp(to, "*"))
			if ((to = get_echannel_by_refnum(0)) == NULL)
				to = zero;

		if ((stag = next_arg(args, &args)) != NULL)
			tag = get_ctcp_val(upper(stag));
		else
			tag = CTCP_VERSION;

		if ((type = in_ctcp()) == -1)
			say("You may not use the CTCP command from an ON CTCP_REPLY!");
		else
		{
			if (args && *args)
				send_ctcp(type, to, tag, "%s", args);
			else
				send_ctcp(type, to, tag, NULL);
		}
	}
	else
		say("Usage: /CTCP <[=]nick|channel|*> [<request>]");
}

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
	    int old_winref = get_winref_by_servref(from_server);

	    for (i = 0; defer_list[i].cmds; i++)
	    {
		from_server = defer_list[i].servref;
		make_window_current_by_refnum(get_winref_by_servref(from_server));

		call_lambda_command("deferred", defer_list[i].cmds,
						defer_list[i].subargs);
		new_free(&defer_list[i].cmds);
		new_free(&defer_list[i].subargs);
	    }

	    from_server = old_from_server;
	    make_window_current_by_refnum(old_winref);
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
			if ((target = get_echannel_by_refnum(0)) == NULL)
				target = zero;

		message = args;

		l = message_from(target, LEVEL_ACTION);
		send_ctcp(CTCP_PRIVMSG, target, CTCP_ACTION, "%s", message);
		if (do_hook(SEND_ACTION_LIST, "%s %s", target, message))
			put_it("* -> %s: %s %s", target, get_server_nickname(from_server), message);
		pop_message_from(l);
	}
	else
		say("Usage: /DESCRIBE <[=]nick|channel|*> <action description>");
}

BUILT_IN_COMMAND(send_to_target)
{
	const char	*tmp;

	tmp = get_target_by_refnum(0);
	send_text(tmp, args, NULL, 1);
}

BUILT_IN_COMMAND(send_to_channel)
{
	const char	*tmp;

	tmp = get_echannel_by_refnum(0);
	send_text(tmp, args, NULL, 1);
}

/*
 * e_channel: does the channel command.  I just added displaying your current
 * channel if none is given 
 */
BUILT_IN_COMMAND(e_channel)
{
	int	l;

	l = message_from(NULL, LEVEL_CRAP);
	if (args && *args)
		window_rejoin(current_window, &args);
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

	if (!(nick = next_arg(args, &args)))
	{
		say("Your nickname is %s", get_server_nickname(get_window_server(0)));
		if (get_pending_nickname(get_window_server(0)))
			say("A nickname change to %s is pending.", get_pending_nickname(get_window_server(0)));
		return;
	}

	if (!(nick = check_nickname(nick, 1)))
	{
		say("The nickname you specified is not a legal nickname.");
		return;
	}

	if (from_server == NOSERV)
	{
		say("You may not change nicknames when not connected to a server");
		return;
	}

	set_server_nickname_pending(from_server, 1);
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
static	void	e_pause_cb (char *u1, char *u2) { e_pause_cb_throw--; }
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
	add_timer(0, "", seconds, 1, (int (*)(void *))commentcmd, NULL, NULL, current_window->refnum);
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
		else if (!strcmp(nick, "*") && (!(nick = get_echannel_by_refnum(0))))
			nick = zero;
		send_text(nick, args, command, window_display);
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
/* Flaming paranoia added 07/07/2000 at request of Liandrin. */
BUILT_IN_COMMAND(e_quit)
{
	char *	sub_format;
	const char *	reason;

	if (args && *args)
		reason = args;
	else if (!(reason = get_string_var(QUIT_MESSAGE_VAR)))
		reason = "%s";

	sub_format = convert_sub_format(reason, 's');
	irc_exit(1, sub_format, irc_version);
}

/*
 * The TOPIC command.
 */
BUILT_IN_COMMAND(e_topic)
{
	int	clear_topic = 0;
	char	*args_copy;
	const char *channel = get_echannel_by_refnum(0);
	const char *arg;

	if (args && *args == '-')
		clear_topic = 1, args++;

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
			send_to_server("TOPIC %s :%s", arg, args);
		else
			send_to_server("TOPIC %s", arg);
	}
	else if (channel)
		send_to_server("TOPIC %s :%s", channel, args_copy);
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
        int owd = window_display;
        window_display = 1;
        put_echo(args);
        window_display = owd;
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
	Window *old_to_window;
	int	all_windows = 0;
	int	want_banner = 0;
	char	*stuff = NULL;
	int	nolog = 0;
	int	more = 1;
	char 	*old_und = 0, *old_rev = 0, *old_bold = 0, 
		*old_color = 0, *old_blink = 0, *old_ansi = 0;
	int	xtended = 0;
	int	l = -1;
	int	old_window_notify = do_window_notifies;

	old_to_window = to_window;

	while (more && args && (*args == '-' || *args == '/'))
	{
	    switch (toupper(args[1]))
	    {
		case 'C':	/* CURRENT (output to user's current window) */
		{
			next_arg(args, &args);
			to_window = current_window;
			break;
		}

		case 'L':
		{
			flag_arg = next_arg(args, &args);

			/* LINE (output to scratch window) */
			if (toupper(flag_arg[2]) == 'I') 
			{
				int to_line = 0;

				if (!to_window)
				{
					yell("XECHO: -LINE only works if -WIN is specified first");
					to_window = old_to_window;
					return;
				}
				to_line = my_atol(next_arg(args, &args));
				if (to_line < 0 || 
					to_line >= to_window->display_size)
				{
					yell("XECHO: -LINE %d is out of range for window (max %d)", 
						to_line, 
						to_window->display_size - 1);
					to_window = old_to_window;
					return;
				}
				to_window->change_line = to_line;
			}

			/* LEVEL (use specified lastlog level) */
			else	
			{
				if (!(flag_arg = next_arg(args, &args)))
					break;
				if ((temp = str_to_level(flag_arg)) > -1)
					l = message_from(NULL, temp);
			}
			break;
		}

		case 'V':	/* VISUAL (output to a visible window) */
		{
			/* There is always at least one visible window! */
			Window *win = NULL;

			/* Chew up the argument. */
			flag_arg = next_arg(args, &args);

			while ((traverse_all_windows(&win)))
			{
				if (win->screen)
				{
					to_window = win;
					break;
				}
			}
			break;
		}

		case 'W':	/* WINDOW (output to specified window) */
		{
			next_arg(args, &args);

			if (!(flag_arg = next_arg(args, &args)))
				break;

			if (!(to_window = get_window_by_desc(flag_arg)))
				to_window = get_window_by_refnum(get_channel_winref(flag_arg, from_server));
			break;
		}

		case 'A':	/* ALL (output to all windows) */
		case '*':
		{
			next_arg(args, &args);
			all_windows = 1;
			break;
		}

		case 'B':	/* WITH BANNER */
		{
			next_arg(args, &args);
			want_banner = 1;
			break;
		}

		case 'R':   /* RAW OUTPUT TO TERMINAL */
		{
			next_arg(args, &args);
			/*
			 * Nuke reminded me of this.  Just because to_window
			 * is set does not mean that tputs_x is going to
			 * put the string out to the screen on that window.
			 * So we have to make sure that output_screen is
			 * to_window->screen.
			 */
			if (to_window)
				output_screen = to_window->screen;
			else
				output_screen = current_window->screen;
			tputs_x(args);
			term_flush();
			to_window = old_to_window;
			return;
		}

		case 'N': /* NOLOG (dont add to lastlog) */
		{
			next_arg(args, &args);
			nolog = 1;
			break;
		}

		case 'S': /* SAY (dont output if suppressing output) */
		{
			next_arg(args, &args);
			if (!window_display)
			{
				to_window = old_to_window;
				return;
			}
			break;
		}

		case 'X': /* X -- allow all attributes to be outputted */
		{
			char one_copy[4];

			next_arg(args, &args);

			/* 
			 * XXX - Never mind how hideous this is, because
			 * it's going away very soon.
			 */
			old_und = make_string_var("UNDERLINE_VIDEO");
			old_rev = make_string_var("INVERSE_VIDEO");
			old_bold = make_string_var("BOLD_VIDEO");
			old_color = make_string_var("COLOR");
			old_blink = make_string_var("BLINK_VIDEO");
			old_ansi = make_string_var("DISPLAY_ANSI");
			strlcpy(one_copy, one, 4);

			set_var_value(UNDERLINE_VIDEO_VAR, one_copy, 0);
			set_var_value(INVERSE_VIDEO_VAR, one_copy, 0);
			set_var_value(BOLD_VIDEO_VAR, one_copy, 0);
			set_var_value(COLOR_VAR, one_copy, 0);
			set_var_value(BLINK_VIDEO_VAR, one_copy, 0);
			set_var_value(DISPLAY_ANSI_VAR, one_copy, 0);

			xtended = 1;
			break;
		}

		case 'F': /* DO not notify for hidden windwos (%F) */
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

	display = window_display;
	window_display = 1;
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
		abort();

	if (all_windows == 1)
	{
		Window *win = NULL;
		while ((traverse_all_windows(&win)))
		{
			to_window = win;
			put_echo(args);
		}
	}
	else if (all_windows != 0)
		abort();
	else
		put_echo(args);

	if (stuff)
		new_free(&stuff);

	if (xtended)
	{
		set_var_value(UNDERLINE_VIDEO_VAR, old_und, 0);
		set_var_value(INVERSE_VIDEO_VAR, old_rev, 0);
		set_var_value(BOLD_VIDEO_VAR, old_bold, 0);
		set_var_value(COLOR_VAR, old_color, 0);
		set_var_value(BLINK_VIDEO_VAR, old_blink, 0);
		set_var_value(DISPLAY_ANSI_VAR, old_ansi, 0);
		new_free(&old_und);
		new_free(&old_rev);
		new_free(&old_bold);
		new_free(&old_color);
		new_free(&old_blink);
		new_free(&old_ansi);
	}

	if (l > -1)
		pop_message_from(l);

	do_window_notifies = old_window_notify;
	if (nolog)
		inhibit_logging = 0;
	window_display = display;
	to_window = old_to_window;
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
	Window *old_to_window = to_window;
	int	old_refnum = current_window->refnum;

	while (args && (*args == '-' || *args == '/'))
	{
		flag = next_arg(args, &args);
		if (!my_stricmp(flag, "--")) 	/* End of options */
			break;

		if (!my_strnicmp(flag + 1, "S", 1)) /* SERVER */
		{
			int val = parse_server_index(next_arg(args, &args), 1);
			if (is_server_registered(val))
				from_server = val;
		}
		else if (!my_strnicmp(flag + 1, "W", 1)) /* WINDOW */
		{
			Window *win = get_window_by_desc(next_arg(args, &args));
			if (win)
			{
				current_window = win;
				to_window = win;
			}
		}
	}

	runcmds(args, subargs);

	to_window = old_to_window;
	make_window_current_by_refnum(old_refnum);
	from_server = old_from_server;
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
			if (my_strnicmp(arg+1, "I", 1) == 0) 	/* IRCU */
				ircu = 1;
			else if (my_strnicmp(arg+1, "MA", 2) == 0)	/* MAX */
			{
				if ((arg = next_arg(args, &args)) != NULL)
					max = my_atol(arg);
			}
			else if (my_strnicmp(arg+1, "MI", 2) == 0) /* MIN */
			{
				if ((arg = next_arg(args, &args)) != NULL)
					min = my_atol(arg);
			}
			else if (my_strnicmp(arg+1, "A", 1) == 0) /* ALL */
				flags &= ~(FUNNY_PUBLIC | FUNNY_PRIVATE);
			else if (my_strnicmp(arg+1, "PU", 2) == 0) /* PUBLIC */
			{
				flags |= FUNNY_PUBLIC;
				flags &= ~FUNNY_PRIVATE;
			}
			else if (my_strnicmp(arg+1, "PR", 2) == 0) /* PRIVATE */
			{
				flags |= FUNNY_PRIVATE;
				flags &= ~FUNNY_PUBLIC;
			}
			else if (my_strnicmp(arg+1, "T", 1) == 0)	/* TOPIC */
				flags |= FUNNY_TOPIC;
			else if (my_strnicmp(arg+1, "U", 1) == 0)	/* USERS */
				flags |= FUNNY_USERS;
			else if (my_strnicmp(arg+1, "N", 1) == 0)	/* NAME */
				flags |= FUNNY_NAME;
			else
				stuff = arg;
		}
		else stuff = arg;
	}

	if (strcmp(stuff, "*") == 0)
		if (!(stuff = get_echannel_by_refnum(0)))
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
		say("Local Host name is %s",
			LocalHostName ? LocalHostName : hostname);
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
		say("All EPIC versions by Jeremy Nelson and Others");
		say("\tCopyright 1993-2004 EPIC Software Labs");
		say(" ");
		say("	    Contact the EPIC project (%s)", EMAIL_CONTACT);
		say("	    for problems with this or any other EPIC client");
		say(" ");
		say("EPIC Software Labs (in alphabetical order):");
		say("       \tB. Thomas Frazier    <ay-ar@epicsol.org>");
		say("       \tBrian Hauber         <bhauber@epicsol.org>");
		say("       \tChip Norkus          <wd@epicsol.org>");
		say("       \tCrazyEddy            <crazyed@epicsol.org>");
		say("	    \tDennis Moore         <nimh@epicsol.org>");
                say("       \tErlend B. Mikkelsen  <howl@epicsol.org>");
		say("	    \tJake Khuon           <khuon@epicsol.org>");
		say("       \tJason Brand          <kitambi@epicsol.org>");
		say("	    \tJeremy Nelson        <jnelson@epicsol.org>");
		say("       \tRobert Chady         <chady@epicsol.org>");
		say("       \tRobohak              <robohak@epicsol.org>");
		say("       \tSrfRoG               <cag@epicsol.org>");
		say("       \tTerry Warner         <keerf@epicsol.org>");
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
 	yell("Copyright © 1993, 1997 Jeremy Nelson.");
	yell("Copyright © 1994 Jake Khuon.");
	yell("Coypright © 1995, 2004 EPIC Software Labs.");
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
} load_level[MAX_LOAD_DEPTH] = { { NULL, NULL, 0, 0, 0, 0 } };

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
		return empty_string;
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

static void	loader_which (FILE *fp, const char *filename, const char *args, struct load_info *);
static void	loader_std (FILE *fp, const char *filename, const char *args, struct load_info *);
static void	loader_pf  (FILE *fp, const char *filename, const char *args, struct load_info *);
/*
 * load: the /LOAD command.  Reads the named file, parsing each line as
 * though it were typed in (passes each line to parse_line). 
 * ---loadcmd---
 */
BUILT_IN_COMMAND(load)
{
	char *	filename;
	char *	sargs;
	char *	use_path;
	char *	expanded;
	FILE *	fp;
	int	display;
	int	do_one_more = 0;
	void	(*loader) (FILE *, const char *, const char *, struct load_info *);

	if (++load_depth == MAX_LOAD_DEPTH)
	{
		load_depth--;
		dump_load_stack(0);
		say("No more than %d levels of LOADs allowed", MAX_LOAD_DEPTH);
		return;
	}

	display = window_display;
	window_display = 0;
	permit_status_update(0);	/* No updates to the status bar! */

	/*
	 * Defult loader: "std" for /load, "which" for /which
	 */
	if (command && *command == 'W')
	    loader = loader_which;
	else
	    loader = loader_std;

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
	    if (!(fp = uzfopen(&expanded, use_path, 1)))
		continue;

	    load_level[load_depth].filename = expanded;
	    load_level[load_depth].line = 1;
	    if (load_depth > 0 && load_level[load_depth - 1].package)
	        malloc_strcpy(&load_level[load_depth].package,
				load_level[load_depth-1].package);

	    will_catch_return_exceptions++;
	    loader(fp, expanded, sargs, &load_level[load_depth]);
	    will_catch_return_exceptions--;
	    return_exception = 0;

	    new_free(&load_level[load_depth].filename);
	    new_free(&load_level[load_depth].package);
	    fclose(fp);
	}

	/*
	 * Restore some sanity
	 */
	if (get_int_var(DISPLAY_VAR))
	       window_display = display;
	permit_status_update(1);
	update_all_status();

	load_depth--;
}

/* The "WHICH" loader */
static void	loader_which (FILE *fp, const char *filename, const char *subargs, struct load_info *loadinfo)
{
	loadinfo->loader = "which";
	yell("%s", filename);
}

/* The "Standard" (legacy) loader */
static void	loader_std (FILE *fp, const char *filename, const char *subargs, struct load_info *loadinfo)
{
	int	in_comment, comment_line, no_semicolon;
	int	paste_level, paste_line;
	char 	*start, *real_start, *current_row;
#define MAX_LINE_SIZE BIG_BUFFER_SIZE * 5
	char	buffer[MAX_LINE_SIZE * 2 + 1];
const	char	*defargs;

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

	    if (!fgets(buffer, MAX_LINE_SIZE, fp))
		break;

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
			(fgets(&(start[len-2]), MAX_LINE_SIZE - len, fp)))
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
		while ((ptr = sindex(optr, "{};/")) && 
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
			if (subargs)
			    defargs = subargs;
			else if (get_int_var(INPUT_ALIASES_VAR))
			    defargs = empty_string;
			else
			    defargs = NULL;

			parse_line(NULL, current_row, defargs, 0);
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
				    if (subargs)
					defargs = subargs;
				    else if (get_int_var(INPUT_ALIASES_VAR))
					defargs = empty_string;
				    else
					defargs = NULL;

				    parse_line(NULL, current_row, defargs, 0);
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

#ifdef BRACE_LOAD_HACK
			/*
			 * left-brace is probably only introducing a new 
			 * brace-set if it
			 *	*) Leads off a line
			 * 	*) Is preceeded by whitespace
			 *	*) Is not immediately following an rparen 
			 * 	   or rbrace
			 * In any other such case, the { is taken as a 
			 * literal character and is not used for bracing. }
			 */
			if (ptr > start && 
			    !isspace(ptr[-1]) && 
			    ptr[-1] != ')' &&
			    ptr[-1] != '{' && 
			    ptr[-1] != '}' && 
			    ptr[-1] != '(' &&
			    ptr[-1] != '$' && 
			    ptr[-1] != '%')
			{
			    malloc_strcat(&current_row, "{");
			    if (paste_level)
				no_semicolon = 0;
			    else
				no_semicolon = 1;
			}
			else
#endif
			{
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
			}
			break;
		    }
		    case '}' :
		    {
			if (in_comment)
			    break;

#ifdef BRACE_LOAD_HACK
			/*
			 * Same as above, only in reverse.  An rbrace is 
			 * only taken as a special character if it
			 *	*) Ends a line
			 *	*) Is followed by whitespace
			 *	*) Is followed by a rparen or rbrace.
			 * Otherwise, it is just a normal character.
			 */
			if (ptr == start || 
			    isspace(ptr[1]) || 
			    ptr[1] != '(' || 
			    ptr[1] != ')' || 
			    ptr[1] != '{' || 
			    ptr[1] != '}')
#endif
			{
			    if (!paste_level)
			    {
				error("Unexpected } in %s, line %d",
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
			}
#ifdef BRACE_LOAD_HACK
			else
			{	
			    malloc_strcat(&current_row, "}");
			    if (paste_level)
				no_semicolon = 0;
			    else
				no_semicolon = 1;
			}
#endif
			break;
		    }
		    case ';':
		    {
			if (in_comment)
			    break;
			
			malloc_strcat(&current_row, ";");
			/*
			 * I guess it was totaly wrong how ircII 
			 * handled semicolons and we wont attempt to 
			 * out-guess the user.  Any semicolons are
			 * left as-is.
			 */
			if (ptr[1] == 0 && !paste_level)
			{
			    if (subargs)
				defargs = subargs;
			    else if (get_int_var(INPUT_ALIASES_VAR))
				defargs = empty_string;
			    else
				defargs = NULL;
			
			    parse_line(NULL, current_row, defargs, 0);
			    new_free(&current_row);
			    if (return_exception)
				return;
			}
			else if (ptr[1] == 0 && paste_level)
			    no_semicolon = 0;
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
	    error("File %s ended with an unterminated comment in line %d",
			filename, comment_line);

	if (current_row)
	{
	    if (paste_level)
	    {
		error("Unexpected EOF in %s trying to match '{' at line %d",
				filename, paste_line);
	        new_free(&current_row);
	    }
	    else
	    {
		if (subargs)
		    defargs = subargs;
		else if (get_int_var(INPUT_ALIASES_VAR))
		    defargs = empty_string;
		else
		    defargs = NULL;

		parse_line(NULL, current_row, defargs, 0);
	        new_free(&current_row);

	        if (return_exception)
			return;
	    }
	}
}

static void	loader_pf (FILE *fp, const char *filename, const char *subargs, struct load_info *loadinfo)
{
	char *	buffer;
	int	bufsize, pos;
	int	this_char, newline, comment;

	loadinfo->loader = "pf";

	bufsize = 8192;
	buffer = new_malloc(bufsize);
	pos = 0;
	newline = 1;
	comment = 0;

	this_char = fgetc(fp);
	while (!feof(fp))
	{
	    do
	    {
		/* At a newline, turn on eol handling, turn off comment. */
		if (this_char == '\n') {
		    newline = 1;
		    comment = 0;
		    break;
		}

		/* If we are in a comment, ignore this character. */
		if (comment)
		    break;

		/* If we last saw an eol, ignore any following spaces */
		if (newline && isspace(this_char))
		    break;

		/* If we last saw an eol, a # starts a one-line comment. */
		if (newline && this_char == '#') {
		    comment = 1;
		    break;
		}

		/* If the last thing we saw was a newline, put a space here */
		if (newline && pos > 0)
		    buffer[pos++] = ' ';

		/* We are no longer at a newline */
		newline = 0;

		/* Append this character to the buffer */
		buffer[pos++] = this_char;

	    } while (0);

	    if (pos >= bufsize - 20) {
		bufsize *= 2;
		new_realloc((void **)&buffer, bufsize);
	    }

	    this_char = fgetc(fp);
	}

	buffer[pos] = 0;
	runcmds(buffer, subargs);
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

		if ((target = get_target_by_refnum(0)) != NULL)
		{
			send_ctcp(CTCP_PRIVMSG, target, CTCP_ACTION, 
					"%s", args);

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

static 	void	oper_password_received (char *data, char *line)
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
	snprintf(buffer, 63, "%s PING %ld %ld", args, t.tv_sec, t.tv_usec);
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

	args_copy = alloca(IO_BUFFER_SIZE + 1);
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
	window_query(current_window, &args);
}

/*
 * quote: handles the QUOTE command.  args are a direct server command which
 * is simply send directly to the server 
 */
BUILT_IN_COMMAND(quotecmd)
{
	int	refnum = from_server;
	int	urlencoded = 0;
	size_t	length;

	if (*command == 'X')
	{
	    while (args && (*args == '-' || *args == '/'))
	    {
		char *flag = next_arg(args, &args);

		if (!my_strnicmp(flag + 1, "S", 1)) /* SERVER */
		{
			int sval;

			sval = parse_server_index(next_arg(args, &args), 1);
			if (!is_server_open(sval))
			{
			   say("XQUOTE: Server %d is not connected", sval);
			   return;
			}
			refnum = sval;
		}
		else if (!my_strnicmp(flag + 1, "U", 1)) /* URL quoting */
			urlencoded++;
		else if (!my_strnicmp(flag + 1, "A", 1)) /* ALL */
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
		length = strlen(args);
		urldecode(args, &length);
		send_to_aserver_raw(refnum, length, args);
	}
	else if (args && *args)
	{
		char	*comm = new_next_arg(args, &args);
		protocol_command *p;
		int	cnt;
		int	loc;

		upper(comm);
		p = (protocol_command *)find_fixed_array_item(
			(void *)rfc1459, sizeof(protocol_command),
			num_protocol_cmds + 1, comm, &cnt, &loc);

		/*
		 * If theyre dispatching some protocol commands we
		 * dont know about, then let them, without complaint.
		 */
		if (cnt < 0 && (rfc1459[loc].flags & PROTO_NOQUOTE))
		{
			yell("Doing /QUOTE %s is not permitted.  Use the client's built in command instead.", comm);
			return;
		}

		/*
		 * If we know its going to cause a problem in the 
		 * future, whine about it.
		 */
		if (cnt < 0 && (rfc1459[loc].flags & PROTO_DEPREC))
			yell("Doing /QUOTE %s is discouraged because it will destablize the client.  Use the client's built in command instead.", comm);

		send_to_aserver(refnum, "%s %s", comm, args);
	}
}

/*
 * RECONNECT command.  Reset a server's state to RECONNECT if necessary.
 * if necesasry.  This is dangerous.  Better to use /SERVER.
 * current server number (which is stored in from_server). 
 * This command puts the REALNAME command in effect.
 */
BUILT_IN_COMMAND(reconnect_cmd)
{
	int	server;

	if ((server = from_server) == NOSERV)
	{
	    say("Reconnect to what server?  (You're not connected)");
	    return;
	}

	if (!args || !*args)
		args = LOCAL_COPY("Reconnecting");

        say("Reconnecting to server %d", server);
	if (is_server_open(server))
	{
		set_server_quit_message(server, args);
		close_server(server, NULL);
	}
	set_server_status(server, SERVER_RECONNECT);
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

	if (!strcmp(who, "*") && !(who = get_echannel_by_refnum(0)))
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

/* This generates a file of your ircII setup */
static void really_save (const char *file, int flags, int save_all, int append)
{
	FILE	*fp;
static	const char *	mode[] = {"w", "a"};
	Filename realfile;

	if (normalize_filename(file, realfile))
	{
		say("%s contains an invalid directory", realfile);
		return;
	}

	if (!(fp = fopen(realfile, mode[append]))) 
	{
		say("Error opening %s: %s", realfile, strerror(errno));
		return;
	}

	if (flags & SFLAG_ALIAS)
		save_aliases(fp, save_all);
	if (flags & SFLAG_ASSIGN)
		save_assigns(fp, save_all);
	if (flags & SFLAG_BIND)
		save_bindings(fp, save_all);
	if (flags & SFLAG_NOTIFY)
		save_notify(fp);
	if (flags & SFLAG_ON)
		save_hooks(fp, save_all);
	if (flags & SFLAG_SERVER)
		save_servers(fp);
	if (flags & SFLAG_SET)
		save_variables(fp, save_all);

	fclose(fp);
	say("Settings %s to %s", append ? "appended" : "saved", realfile);
}

/* save_settings: saves the current state of IRCII to a file */
BUILT_IN_COMMAND(save_settings)
{
	char *	arg;
	int	save_flags;
	int	save_global;
	int	save_append;

	save_flags = save_global = save_append = 0;

	while ((arg = next_arg(args, &args)) != NULL)
	{
		if (*arg != '-')
			break;	/* Must be the filename */

		upper(++arg);

		     if (!strncmp("ALIAS", arg, 3))
			save_flags |= SFLAG_ALIAS;
		else if (!strncmp("ALL", arg, 3))
			save_flags = SFLAG_ALL;
		else if (!strncmp("APPEND", arg, 3))
			save_append = 1;
		else if (!strncmp("ASSIGN", arg, 2))
			save_flags |= SFLAG_ASSIGN;
		else if (!strncmp("BIND", arg, 1))
			save_flags |= SFLAG_BIND;
		else if (!strncmp("GLOBAL", arg, 1))
			save_global = 1;
		else if (!strncmp("NOTIFY", arg, 1))
			save_flags |= SFLAG_NOTIFY;
		else if (!strncmp("ON", arg, 1))
			save_flags |= SFLAG_ON;
		else if (!strncmp("SET", arg, 3))
			save_flags |= SFLAG_SET;
		else if (!strncmp("SERVER", arg, 3))
			save_flags |= SFLAG_SERVER;
		else
			break;	/* Odd filename */
	}

	if (save_flags == 0)
	{
		say("Usage: SAVE [-ALIAS|-ASSIGN|-BIND|-NOTIFY|-ON|-SET|-SERVER|-ALL|-GLOBAL|-APPEND] [path]");
		return;
	}

	if (!arg || !*arg)
	{
		say("You must specify a filename.");
		return;
	}

	really_save(arg, save_flags, save_global, save_append);
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
		target = get_echannel_by_refnum(0);
		if (!target || !*target)
			target = "*";	/* what-EVER */
	}

	if (reason && *reason)
		send_to_server("%s %s :%s", command, target, reason);
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
 * send_kick: sends a kick message to the server.  Works properly with
 * kick comments.
 */
BUILT_IN_COMMAND(send_kick)
{
	char	*kickee;
	const char	*comment;
	const char	*channel;

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
		channel = get_echannel_by_refnum(0);

	send_to_server("KICK %s %s :%s", channel, kickee, comment);
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
		if ((s = get_echannel_by_refnum(0)) != NULL)
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
	display = window_display;
	window_display = 1;
	parse_line(NULL, args, get_int_var(INPUT_ALIASES_VAR) ? empty_string : NULL, 1);
	update_input(UPDATE_ALL);
	window_display = display;
	from_server = server;
}

/* 
 * IRCUSER command. Changes your userhost on the fly.  Takes effect
 * the next time you connect to a server 
 */
BUILT_IN_COMMAND(set_username)
{
#ifdef ALLOW_USER_SPECIFIED_LOGIN
        char *blah = next_arg(args, &args);
	if (blah && *blah)
	{
		if (!strcmp(blah, "-"))
			strlcpy(username, empty_string, sizeof username);
		else 
			strlcpy(username, blah, sizeof username);
		say("Username has been changed to '%s'",username);
	}
	else
		say ("Usage: /IRCUSER <text>");
#endif
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
	Timeval	interval;
	float	nms;
	time_t	sec;

	if ((arg = next_arg(args, &args)) != NULL)
	{
		nms = atof(arg);
		interval.tv_sec = sec = (int)nms;
		interval.tv_usec = (nms-sec) * 1000000;
		select(0, NULL, NULL, NULL, &interval);
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
			do_stack_set(type, args);
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
	Timeval	interval;
	time_t	nms;

	if ((arg = next_arg(args, &args)))
	{
		nms = (time_t)my_atol(arg);
		interval.tv_sec = nms / 1000000;
		interval.tv_usec = nms % 1000000;
		select(0, NULL, NULL, NULL, &interval);
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

	if (ctl_arg && !my_strnicmp(ctl_arg, "-c", 2))
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
		int	w_index = is_valid_process(ctl_arg);
		char	reason[1024];

		if (w_index != -1)
		{
			if (args && *args)
			{
				if (!my_strnicmp(args, "-cmd ", 4))
					next_arg(args, &args);
				add_process_wait(w_index, args);
			}
			else
			{
				snprintf(reason, 1024, "WAIT on EXEC %s", 
						ctl_arg);
				lock_stack_frame();
				while (process_is_running(ctl_arg))
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
		char *word_one = next_arg (args, &args);
		if (args && *args)
			malloc_sprintf(&stuff, "%s %s", word_one, args);
		else if (word_one && *word_one)
			malloc_sprintf(&stuff, "%s %d", word_one, get_int_var(NUM_OF_WHOWAS_VAR));
		else
			malloc_sprintf(&stuff, "%s %d", get_server_nickname(from_server), get_int_var(NUM_OF_WHOWAS_VAR));

		send_to_server("WHOWAS %s", stuff);
		new_free(&stuff);
	}
	else /* whois command */
		send_to_server("WHOIS %s", args && *args ? args : get_server_nickname(from_server));
}

BUILT_IN_COMMAND(xtypecmd)
{
	char	*arg;
	char	es[1];

	if (*args == '-' || *args == '/')
	{
		char saved = *args;
		args++;
		if ((arg = next_arg(args, &args)) != NULL)
		{
			if (!my_strnicmp(arg, "L", 1))
			{
				for (; *args; args++)
				{
					es[0] = 0;
					input_add_character(*args, es);
				}
			}
			else
				say("Unknown flag -%s to XTYPE", arg);
			return;
		}
		es[0] = 0;
		input_add_character(saved, es);
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
		send_text(nick_list, text, command, hook);

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
void 	send_text (const char *nick_list, const char *text, const char *command, int hook)
{
	int 	i, 
		old_server;
	char 	*current_nick,
		*next_nick,
		*line;
	Crypt	*key;
	int	old_window_display = window_display;
static	int	recursion = 0;

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
	 */
	if (recursion)
		hook = 0;

	window_display = hook;
	recursion++;
	next_nick = LOCAL_COPY(nick_list);

	if (command && !strcmp(command, "MSG"))
		command = "PRIVMSG";		/* XXX */

	while ((current_nick = next_nick))
	{
	    if ((next_nick = strchr(current_nick, ',')))
		*next_nick++ = 0;

	    if (!*current_nick)
		continue;

	    if (*current_nick == '%')
	    {
		if ((i = get_process_index(&current_nick)) == -1)
			say("Invalid process specification");
		else
			text_to_process(i, text, 1);
	    }
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
		target_file_write(current_nick + 1, text);
	    else if (*current_nick == '@' && toupper(current_nick[1]) == 'W' 
			&& is_number(current_nick + 1))
		target_file_write(current_nick + 1, text);

	    else if (*current_nick == '"')
		send_to_server("%s", text);

	    else if (*current_nick == '/')
	    {
		line = malloc_strdup3(current_nick, space, text);
		parse_command(line, 0, empty_string);
		new_free(&line);
	    }
	    else if (*current_nick == '=')
	    {
		if (!is_number(current_nick + 1) &&
			!dcc_chat_active(current_nick + 1))
		{
			yell("No DCC CHAT connection open to %s", 
				current_nick + 1);
			continue;
		}

		if ((key = is_crypted(current_nick)) != 0)
		{
			char *breakage = LOCAL_COPY(text);
			line = crypt_msg(breakage, key);
		}
		else
			line = malloc_strdup(text);

		old_server = from_server;
		from_server = NOSERV;
		dcc_chat_transmit(current_nick + 1, line, text, command, hook);
		from_server = old_server;
		set_server_sent_nick(from_server, current_nick);
		new_free(&line);
	    }
	    else
	    {
		char *	copy = NULL;

		if (get_server_doing_notice(from_server))
		{
			say("You cannot send a message from within ON NOTICE");
			continue;
		}

		i = is_channel(current_nick);
		if (get_server_doing_privmsg(from_server) || (command && !strcmp(command, "NOTICE")))
			i += 2;

		if ((key = is_crypted(current_nick)))
		{
			int	l;

			copy = LOCAL_COPY(text);
			l = message_from(current_nick, target[i].mask);

			if (hook && do_hook(target[i].hook_type, "%s %s", 
						current_nick, copy))
				put_it(target[i].format, current_nick, copy);
			line = crypt_msg(copy, key);

			send_to_server("%s %s :%s", 
					target[i].command, current_nick, line);
			set_server_sent_nick(from_server, current_nick);

			new_free(&line);
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

		send_to_server("%s %s :%s", 
				target[i].command, 
				target[i].nick_list, 
				target[i].message);

		new_free(&target[i].nick_list);
		target[i].message = NULL;

		pop_message_from(l);
	}

	/*
	 * If the user didnt explicitly send the text (hook == 1), then 
	 * it makes no sense to presume that theyre not still /AWAY.
	 * This also makes sure that the "no longer away" message doesnt
	 * get munched if window_display is 0.
	 */
	if (hook && get_server_away(from_server) && get_int_var(AUTO_UNMARK_AWAY_VAR))
		runcmds("AWAY", empty_string);

	window_display = old_window_display;
	recursion--;
}



/*
 * eval_inputlist:  Cute little wrapper that calls parse_line() when we
 * get an input prompt ..
 */
static void	eval_inputlist (char *args, char *line)
{
	runcmds(args, line);
}

GET_FIXED_ARRAY_NAMES_FUNCTION(get_command, irc_command)

BUILT_IN_COMMAND(e_call)
{
	dump_call_stack();
}

/***************************************************************************/
static char *	parse_line_alias_special (const char *name, const char *what, char *args, void *arglist, int function);

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

static char *	parse_line_alias_special (const char *name, const char *what, char *args, void *arglist, int function)
{
	int	old_window_display = window_display;
	int	old_last_function_call_level = last_function_call_level;
	char	*result = NULL;
	int	localvars = name ? 1 : 0;

	window_display = 0;
	if (!args)
		args = LOCAL_COPY(empty_string);
	if (localvars && !make_local_stack(name))
	{
		yell("Could not run (%s) [%s]; too much recursion", 
				name ? name : "<unnamed>", what);
		return malloc_strdup(empty_string);
	}
	if (arglist)
		prepare_alias_call(arglist, &args);
	if (function)
	{
		last_function_call_level = wind_index;
		add_local_alias("FUNCTION_RETURN", empty_string, 0);
	}
	window_display = old_window_display;

	will_catch_return_exceptions++;
	runcmds(what, args);
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
	parse_line(NULL, what, subargs ? subargs : empty_string, 0);
}

/* 
 * parse_line: This is the main parsing routine.  It should be called in
 * almost all circumstances over parse_command().
 *
 * parse_line breaks up the line into commands separated by unescaped
 * semicolons if we are in non interactive mode. Otherwise it tries to leave
 * the line untouched.
 *
 * Currently, a carriage return or newline breaks the line into multiple
 * commands too. This is expected to stop at some point when parse_command
 * will check for such things and escape them using the ^P convention.
 * We'll also try to check before we get to this stage and escape them before
 * they become a problem.
 *
 * Other than these two conventions the line is left basically untouched.
 *
 * Ideas on parsing: Why should the calling function be responsible
 *  for removing {} blocks?  Why cant this parser cope with and {}s
 *  that come up?
 *
 * I whacked in 'return', 'continue', and 'break' support in one afternoon.
 * Im sure that i did a hideously bletcherous job that needs to be cleaned up.
 * So just be forewarned about the damage.
 */
void	parse_line (const char *name, const char *org_line, const char *args, int hist_flag)
{
	char	*line = NULL;
	char 	*stuff,
		*s,
		*t;
	int	die = 0;
	ssize_t	span;

	/*
	 * If this is an atomic scope, then we create a new local variable
	 * stack.  Otherwise, this command will use someone else's stack.
	 */
	if (name)
	{
	    if (!make_local_stack(name))
	    {
                yell("Could not run (%s) [%s]; too much recursion", name ? name : "<unnamed>", org_line);
                return;
	    }
	}

	if (!org_line)
		panic("org_line is NULL and it shouldn't be.");

	/*
	 * We will be mangling 'org_line', so we make a copy to work with.
	 */
	line = LOCAL_COPY(org_line);

	/*
	 * If the user outputs the "empty command", then output a blank
	 * line to their current target.  This is useful for pastes.
	 */
	if (!*org_line)
		send_text(get_target_by_refnum(0), empty_string, NULL, 1);

	/*
	 * Otherwise, if the command has arguments, then:
	 *	* It is being run as part of an alias/on/function
	 *	* It is being /LOADed while /set input_aliases ON
	 *	* It is typed at the input prompt while /set input_aliases ON
	 */
	else if (args) 
	{
	    do 
            {
		/*
		 * At any point where a command is expected, the user is
		 * allowed to have { ... } set, which will be evaluated as
		 * a sub-context.  We rip off the { ... } stuff and then
		 * pass the insides recursively off to parse_line.  Note
		 * that this applies recursively inside that set as well,
		 * so arbitrary nesting is permitted by this.  If there is
		 * a missing closing brace, we dont try to recover.
		 *
		 * If the sub-context throws a break, return, or continue,
		 * then we drop out of our parsing and allow the exception
		 * to be thrown up the stack.
		 */
                while (*line == '{') 
                {
		    if (!(stuff = next_expr(&line, '{'))) 
		    {
			error("Unmatched {"); 
			if (name)
				destroy_local_stack();
			return;
		    }

		    /* I'm fairly sure the first arg ought not be 'name' */
		    parse_line(NULL, stuff, args, hist_flag);

		    /*
		     * Check to see if an exception has been thrown.  If it
		     * has, then we stop parsing, but we dont catch the
		     * exception, someone upstream will catch it.
		     */
		    if ((will_catch_break_exceptions && break_exception) ||
			(will_catch_return_exceptions && return_exception) ||
			(will_catch_continue_exceptions && continue_exception) ||
			system_exception)
		    {
			die = 1;
			break;
		    }

		    /*
		     * Munch up all the semicolons or spaces after the 
		     * { ... } set.
		     */
		    while (line && *line && ((*line == ';') || 
						(my_isspace(*line))))
			*line++ = '\0';

		    /*
		     * Now we go back to the top and repeat as long as 
		     * neccesary.
		     */
		}

		/*
		 * At this point we're just parsing a normal command.
		 * If we saw an exception go by or there is nothing to
		 * parse, stop processing.
		 */
		if (!line || !*line || die)
		     break;

		/*
		 * Don't allow the user to execute a command that starts with
		 * an expando (a possible security hole) if they have
		 * /set security to block it.
		 */
		if (*line == '$' && (get_int_var(SECURITY_VAR) & 
					SECURITY_NO_VARIABLE_COMMAND))
		{
			yell("WARNING: The command '%s' (%s) was not executed due to a security violation", line, args);
			break;
		}

		/*
		 * Now we expand the first command in this set, so as to
		 * include any variables or argument-expandos.  The "line"
		 * pointer is set to the first character of the next command
		 * (if any).  If the line is only *one* command, and if our
		 * caller wants us to add $* as neccesary, and if the command
		 * did not reference the argument list, and there is an
		 * argument list, then we tack the argument list on to the
		 * end of the command.  This is what allows stuff like
		 *	/alias m msg
		 * to work as expected.
		 */
		stuff = expand_alias(line, args, &span);

		if (span < 0)
			line = NULL;
		else
		{
			line += span;

			/* Willfully ignore spaces after semicolons. */
			while (line && *line && isspace(*line))
				line++;
		}

		/*
		 * Now we run the command.
		 */
		parse_command(stuff, hist_flag, args);

		/*
		 * And clean up after ourselves
		 */
                new_free(&stuff);

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

		/*
		 * We repeat this as long as we have more commands to parse.
		 */
	    }
	    while (line && *line);
	}

	/*
	 * Otherwise, If it is being /LOADed, just parse it directly.
	 */
        else if (load_depth != -1)
		parse_command(line, hist_flag, args);

	/*
	 * Otherwise its a command being run by the user in direct mode
	 * and /SET INPUT_ALIASES is OFF.
	 */
	else
	{
	    /*
	     * This handling is strictly for backwards compatability.
	     * It used to be possible to insert literal newlines in
	     * the text in the input prompt via some tomfoolery, but
	     * this does not really happen much to speak of any more.
	     * Nevertheless, I'm sure there is someone out there who 
	     * depends on this working, so don't take it out. ;-)
	     */
	    while ((s = line))
	    {
		/*
		 * Find a newline, if any
		 */
		if ((t = sindex(line, "\r\n")))
		{
			*t++ = 0;
			line = t;
		}
		else
			line = NULL;

		/*
		 * Run the command (args is NULL, remember)
		 */
		parse_command(s, hist_flag, args);

		if ((will_catch_break_exceptions && break_exception) ||
		    (will_catch_return_exceptions && return_exception) ||
		    (will_catch_continue_exceptions && continue_exception) ||
		    system_exception)
			break;
	    }
	}

	/*
	 * If we're an atomic scope, blow away our local variable stack.
	 */
	if (name)
		destroy_local_stack();

	return;
}


/*
 * parse_command: parses a line of input from the user.  If the first
 * character of the line is equal to irc_variable[CMDCHAR_VAR].value, the
 * line is used as an irc command and parsed appropriately.  If the first
 * character is anything else, the line is sent to the current channel or to
 * the current query user.  If hist_flag is true, commands will be added to
 * the command history as appropriate.  Otherwise, parsed commands will not
 * be added. 
 *
 * Parse_command() only parses a single command.In general, you will want to 
 * use parse_line() to execute things.Parse command recognizes no quoted
 * characters or anything (beyond those specific for a given command being
 * executed). 
 *
 * Only Three functions have any business calling us:
 *	- call_function (in alias.c)
 *	- parse_line	(in edit.c)
 *	- parse_command (right here)
 *
 * Everyone else must call parse_line.  No exceptions.
 */
int	parse_command (const char *line, int hist_flag, const char *sub_args)
{
static	unsigned 	level = 0;
	unsigned 	display;
	int		old_display_var;
	const char *	cmdchars;
	const char *	com;
	int		add_to_hist,
			cmdchar_used = 0;
	int		noisy = 1;
	char *		this_cmd = NULL;

	if (!line || !*line) 
		return 0;

	if (get_int_var(DEBUG_VAR) & DEBUG_COMMANDS)
		privileged_yell("Executing [%d] %s", level, line);
	level++;
	if (!(cmdchars = get_string_var(CMDCHARS_VAR)))
		cmdchars = DEFAULT_CMDCHARS;

	this_cmd = LOCAL_COPY(line);
	set_current_command(this_cmd);
	add_to_hist = 1;
	display = window_display;
	old_display_var = get_int_var(DISPLAY_VAR);

	/* 
	 * Once and for all i hope i fixed this.  What does this do?
	 * well, at the beginning of your input line, it looks to see
	 * if youve used any ^s or /s.  You can use up to one ^ and up
	 * to two /s.  When any character is found that is not one of
	 * these characters, it stops looking.
	 */
	for (; *line; line++)
	{
		/* Fix to allow you to do ^foo at the input line. */
		if (*line == '^' && (!hist_flag || cmdchar_used))
		{
			if (!noisy)
				break;
			noisy = 0;
		}
		else if ((!hist_flag && *line == '/') || strchr(cmdchars, *line))
		{
			cmdchar_used++;
			if (cmdchar_used > 2)
				break;
		}
		else
			break;
	}

	if (!noisy)
		window_display = 0;
	com = line;

	/*
	 * always consider input a command unless we are in interactive mode
	 * and command_mode is off.   -lynx
	 */
	if (hist_flag && !cmdchar_used && !get_int_var(COMMAND_MODE_VAR))
	{
		send_text(get_target_by_refnum(0), line, NULL, 1);
		if (hist_flag && add_to_hist)
			add_to_history(this_cmd);
		/* Special handling for ' and : */
	}
	else if (*com == '\'' && get_int_var(COMMAND_MODE_VAR))
	{
		send_text(get_target_by_refnum(0), line + 1, NULL, 1);
		if (hist_flag && add_to_hist)
			add_to_history(this_cmd);
	}
	else if ((*com == '@') || (*com == '('))
	{
		/* This kludge fixes a memory leak */
		char *		tmp;
		char *		my_line = LOCAL_COPY(line);

		/*
		 * This new "feature" masks a weakness in the underlying
		 * grammar that allowed variable names to begin with an
		 * lparen, which inhibited the expansion of $s inside its
		 * name, which caused icky messes with array subscripts.
		 *
		 * Anyhow, we now define any commands that start with an
		 * lparen as being "expressions", same as @ lines.
		 */
		if (*com == '(')
		{
		    ssize_t	span;

		    if ((span = MatchingBracket(my_line + 1, '(', ')')) >= 0)
			my_line[1 + span] = 0;
		}

		if ((tmp = parse_inline(my_line + 1, sub_args)))
			new_free(&tmp);

		if (hist_flag && add_to_hist)
			add_to_history(this_cmd);
	}
	else do
	{
		char	*cline, *rest;
		char 	*alias = NULL;
		void	*arglist = NULL;
		void	(*cmd) (const char *, char *, const char *) = NULL;

		if ((rest = strchr(line, ' ')))
		{
			size_t size;

			size = (rest - line) + 1;
			cline = alloca(size);
			strlcpy(cline, line, size);
			rest++;
		}
		else
		{
			cline = LOCAL_COPY(line);
			rest = LOCAL_COPY(empty_string);
		}

		upper(cline);
		alias = get_cmd_alias(cline, &arglist, &cmd);

		if (cmdchar_used >= 2)
			alias = NULL;		/* Unconditionally */

		if (alias == NULL && cmd == NULL && *cline == '!')
		{
			if ((cline = do_history(cline + 1, rest)) != NULL)
			{
				if (level == 1)
					set_input(cline);
				else
					parse_command(cline, 0, sub_args);

				new_free(&cline);
			}
			else
				set_input(empty_string);

			break;
		}

		if (hist_flag && add_to_hist)
			add_to_history(this_cmd);

		if (alias)
			call_user_command(cline, alias, rest, arglist);
		else if (cmd)
			cmd(cline, rest, sub_args);
		else if (get_int_var(DISPATCH_UNKNOWN_COMMANDS_VAR))
			send_to_server("%s %s", cline, rest);
		else
			say("Unknown command: %s", cline);
	}
	while (0);

	if (old_display_var != get_int_var(DISPLAY_VAR))
		window_display = get_int_var(DISPLAY_VAR);
	else
		window_display = display;

	level--;
	unset_current_command();
        return 0;
}


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

