/*
 * commands.c -- Stuff needed to execute commands in ircII.
 *		 Includes the bulk of the built in commands for ircII.
 *
 * Written By Michael Sandrof
 * Reorganized with ruthless abandon for EPIC3pre2 (summer, 1995)
 *   by Jeremy Nelson (jnelson@acronet.net)
 *
 * Copyright(c) 1990, 1995 Michael Sandroff and others
 * Copyright 1996, 1997 EPIC Software Labs
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 */

#define __need_putchar_x__
#define __need_term_flush__
#include "irc.h"
#include "alias.h"
#include "alist.h"
#include "crypt.h"
#include "ctcp.h"
#include "dcc.h"
#include "commands.h"
#include "exec.h"
#include "files.h"
#include "funny.h"
#include "help.h"
#include "history.h"
#include "hook.h"
#include "server.h"
#include "if.h"
#include "ignore.h"
#include "input.h"
#include "ircaux.h"
#include "keys.h"
#include "lastlog.h"
#include "names.h"
#include "notify.h"
#include "notice.h"
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

/* recv_nick: the nickname of the last person to send you a privmsg */
	char	*recv_nick = NULL;

/* sent_nick: the nickname of the last person to whom you sent a privmsg */
	char	*sent_nick = NULL;
	char	*sent_body = NULL;

/* Used to handle and catch breaks and continues */
	int	will_catch_break_exceptions = 0;
	int	will_catch_continue_exceptions = 0;
	int	will_catch_return_exceptions = 0;
	int	break_exception = 0;
	int	continue_exception = 0;
	int	return_exception = 0;

typedef	struct	WaitCmdstru
{
	char	*stuff;
	struct	WaitCmdstru	*next;
}	WaitCmd;

static	WaitCmd	*start_wait_list = NULL,
		*end_wait_list = NULL;

	char	lame_wait_nick[] = "***LW***";
	char	wait_nick[] = "***W***";

/* commands and whatnot */
static  void    abortcmd 	(const char *, char *, const char *);
static	void	away 		(const char *, char *, const char *);
static	void	beepcmd 	(const char *, char *, const char *);
static	void	blesscmd	(const char *, char *, const char *);
static	void	breakcmd	(const char *, char *, const char *);
static	void	comment 	(const char *, char *, const char *);
static	void	continuecmd	(const char *, char *, const char *);
static	void	ctcp 		(const char *, char *, const char *);
static	void	dcc 		(const char *, char *, const char *);
static	void	deop 		(const char *, char *, const char *);
static	void	do_send_text 	(const char *, char *, const char *);
static	void	sendlinecmd 	(const char *, char *, const char *);
static	void	echocmd		(const char *, char *, const char *);
static	void	funny_stuff 	(const char *, char *, const char *);
static	void	cd 		(const char *, char *, const char *);
static	void	defercmd	(const char *, char *, const char *);
static	void	describe 	(const char *, char *, const char *);
static	void	e_call		(const char *, char *, const char *);
static	void	e_clear		(const char *, char *, const char *);
#if 0
static	void	e_channel 	(const char *, char *, const char *);
#endif
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
static  void    realname_cmd 	(const char *, char *, const char *);
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
static	void	usleepcmd	(const char *, char *, const char *);
static  void	shift_cmd 	(const char *, char *, const char *);
static	void	sleepcmd 	(const char *, char *, const char *);
static	void	stackcmd	(const char *, char *, const char *);
static  void	unshift_cmd 	(const char *, char *, const char *);
static	void	version 	(const char *, char *, const char *);
static 	void	waitcmd 	(const char *, char *, const char *);
static	void	whois 		(const char *, char *, const char *);
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
	char *		name;		/* what the user types */
	char *		server_func;	/* what gets sent to the server */
	CmdFunc 	func;		/* function that is the command */
	unsigned	flags;
}	IrcCommand;

/*
 * irc_command: all the availble irc commands:  Note that the first entry has
 * a zero length string name and a null server command... this little trick
 * makes "/ blah blah blah" to always send the arguments to a channel, 
 * bypassing queries, etc.  Neato.  This list MUST be sorted.
 */
static	IrcCommand irc_command[] =
{
	{ "",		empty_string,	do_send_text,		0 },
	{ "#",		NULL,		comment, 		0 },
	{ ":",		NULL,		comment, 		0 },
        { "ABORT",      NULL,           abortcmd,               0 },
	{ "ADMIN",	"ADMIN",	send_comm, 		0 },
	{ "ALIAS",	zero,		aliascmd,		0 }, /* alias.c */
	{ "ALLOCDUMP",	NULL,		allocdumpcmd,		0 },
	{ "ASSIGN",	one,		assigncmd,		0 }, /* alias.c */
	{ "AWAY",	"AWAY",		away,			0 },
	{ "BEEP",	0,		beepcmd,		0 },
	{ "BIND",	NULL,		bindcmd,		0 }, /* keys.c */
	{ "BLESS",	"BLESS",	blesscmd,		0 },
	{ "BREAK",	NULL,		breakcmd,		0 },
	{ "BYE",	"QUIT",		e_quit,			0 },
	{ "CALL",	NULL,		e_call,			0 },
	{ "CD",		NULL,		cd,			0 },
	{ "CHANNEL",	"JOIN",		e_channel,		0 },
	{ "CLEAR",	"CLEAR",	e_clear,		0 },
	{ "COMMENT",	NULL,		comment,		0 },
	{ "CONNECT",	"CONNECT",	send_comm,		0 },
	{ "CONTINUE",	NULL,		continuecmd,		0 },
	{ "CTCP",	NULL,		ctcp,			0 },
	{ "DATE",	"TIME",		send_comm,		0 },
	{ "DCC",	NULL,		dcc,			0 },
	{ "DEFER",	NULL,		defercmd,		0 },
	{ "DEOP",	NULL,		deop,			0 },
	{ "DESCRIBE",	NULL,		describe,		0 },
	{ "DIE",	"DIE",		send_comm,		0 },
	{ "DISCONNECT",	NULL,		disconnectcmd,		0 }, /* server.c */
        { "DO",         NULL,           docmd,                  0 }, /* if.c */
        { "DUMP",       NULL,           dumpcmd,		0 }, /* alias.c */
	{ "ECHO",	NULL,		echocmd,		0 },
	{ "ENCRYPT",	NULL,		encrypt_cmd,		0 }, /* crypt.c */
	{ "EVAL",	"EVAL",		evalcmd,		0 },
#ifdef EXEC_COMMAND
	{ "EXEC",	NULL,		execcmd,		0 }, /* exec.c */
#else
	{ "EXEC",	NULL,		NULL,			0 },
#endif
	{ "EXIT",	"QUIT",		e_quit,			0 },
        { "FE",         "FE",           fe,		        0 }, /* if.c */
        { "FEC",        "FEC",          fe,                     0 }, /* if.c */
	{ "FLUSH",	NULL,		flush,			0 },
        { "FOR",        NULL,           forcmd,		        0 }, /* if.c */
	{ "FOREACH",	NULL,		foreach,		0 }, /* if.c */
	{ "HELP",	NULL,		help,			0 }, /* help.c */
	{ "HISTORY",	NULL,		history,		0 }, /* history.c */
	{ "HOOK",	NULL,		hookcmd,		0 },
	{ "HOST",	"USERHOST",	userhostcmd,		0 },
	{ "HOSTNAME",	"HOSTNAME",	e_hostname,		0 },
	{ "IF",		"IF",		ifcmd,			0 }, /* if.c */
	{ "IGNORE",	NULL,		ignore,			0 }, /* ignore.c */
	{ "INFO",	"INFO",		info,			0 },
	{ "INPUT",	"INPUT",	inputcmd,		0 },
	{ "INPUT_CHAR",	"INPUT_CHAR",	inputcmd,		0 },
	{ "INVITE",	"INVITE",	send_comm,		0 },
	{ "IRCHOST",	"HOSTNAME",	e_hostname,		0 },
	{ "IRCNAME",	NULL,		realname_cmd,		0 },
	{ "IRCUSER",	NULL,		set_username,		0 },
	{ "ISON",	"ISON",		isoncmd,		0 },
	{ "JOIN",	"JOIN",		e_channel,		0 },
	{ "KICK",	"KICK",		send_kick,		0 },
	{ "KILL",	"KILL",		send_2comm,		0 },
	{ "KNOCK",	"KNOCK",	send_channel_com,	0 },
	{ "LASTLOG",	NULL,		lastlog,		0 }, /* lastlog.c */
	{ "LEAVE",	"PART",		send_2comm,		0 },
	{ "LICENSE",	NULL,		license,		0 },
	{ "LINKS",	"LINKS",	send_comm,		0 },
	{ "LIST",	"LIST",		funny_stuff,		0 },
	{ "LOAD",	"LOAD",		load,			0 },
	{ "LOCAL",	"2",		localcmd,		0 }, /* alias.c */
	{ "LUSERS",	"LUSERS",	send_comm,		0 },
	{ "MAP",	"MAP",		send_comm,		0 },
	{ "ME",		NULL,		mecmd,			0 },
	{ "MESG",	"MESG",		extern_write,		0 },
	{ "MODE",	"MODE",		send_channel_com,	0 },
	{ "MOTD",	"MOTD",		send_comm,		0 },
	{ "MSG",	"PRIVMSG",	e_privmsg,		0 },
	{ "NAMES",	"NAMES",	funny_stuff,		0 },
	{ "NICK",	"NICK",		e_nick,			0 },
	{ "NOTE",	"NOTE",		send_comm,		0 },
	{ "NOTICE",	"NOTICE",	e_privmsg,		0 },
	{ "NOTIFY",	NULL,		notify,			0 }, /* notify.c */
	{ "ON",		NULL,		oncmd,			0 }, /* hook.c */
	{ "OPER",	"OPER",		oper,			0 },
	{ "PACKAGE",	NULL,		packagecmd,		0 },
	{ "PARSEKEY",	NULL,		parsekeycmd,		0 },
	{ "PART",	"PART",		send_2comm,		0 },
	{ "PAUSE",	NULL,		e_pause,		0 },
	{ "PING",	NULL, 		pingcmd,		0 },
	{ "POP",	NULL,		pop_cmd,		0 },
	{ "PRETEND",	NULL,		pretend_cmd,		0 },
	{ "PUSH",	NULL,		push_cmd,		0 },
	{ "QUERY",	NULL,		query,			0 },
        { "QUEUE",      NULL,           queuecmd,               0 }, /* queue.c */
	{ "QUIT",	"QUIT",		e_quit,			0 },
	{ "QUOTE",	"QUOTE",	quotecmd,		0 },
	{ "RBIND",	zero,		rbindcmd,		0 }, /* keys.c */
        { "REALNAME",   NULL,           realname_cmd,           0 },
	{ "RECONNECT",  NULL,           reconnect_cmd,          0 },
	{ "REDIRECT",	NULL,		redirect,		0 },
	{ "REHASH",	"REHASH",	send_comm,		0 },
	{ "REPEAT",	"REPEAT",	repeatcmd,		0 },
	{ "RESTART",	"RESTART",	send_comm,		0 },
	{ "RETURN",	NULL,		returncmd,		0 },
	{ "RPING",	"RPING",	send_comm,		0 },
	{ "SAVE",	NULL,		save_settings,		0 },
	{ "SAY",	empty_string,	do_send_text,		0 },
	{ "SEND",	NULL,		do_send_text,		0 },
	{ "SENDLINE",	empty_string,	sendlinecmd,		0 },
	{ "SERVER",	NULL,		servercmd,		0 }, /* server.c */
	{ "SERVLIST",	"SERVLIST",	send_comm,		0 },
	{ "SET",	NULL,		setcmd,			0 }, /* vars.c */
	{ "SETENV",	NULL,		setenvcmd,		0 },
	{ "SHIFT",	NULL,		shift_cmd,		0 },
	{ "SHOOK",	NULL,		shookcmd,		0 },
	{ "SIGNOFF",	"QUIT",		e_quit,			0 },
	{ "SILENCE",	"SILENCE",	send_comm,		0 },
	{ "SLEEP",	NULL,		sleepcmd,		0 },
	{ "SQUERY",	"SQUERY",	send_2comm,		0 },
	{ "SQUIT",	"SQUIT",	send_2comm,		0 },
	{ "STACK",	NULL,		stackcmd,		0 }, /* stack.c */
	{ "STATS",	"STATS",	send_comm,		0 },
	{ "STUB",	"STUB",		stubcmd,		0 }, /* alias.c */
	{ "SWITCH",	"SWITCH",	switchcmd,		0 }, /* if.c */
	{ "TIME",	"TIME",		send_comm,		0 },
	{ "TIMER",	"TIMER",	timercmd,		0 },
	{ "TOPIC",	"TOPIC",	e_topic,		0 },
	{ "TRACE",	"TRACE",	send_comm,		0 },
	{ "TYPE",	NULL,		type,			0 }, /* keys.c */
	{ "UNCLEAR",	"UNCLEAR",	e_clear,		0 },
	{ "UNLESS",	"UNLESS",	ifcmd,			0 }, /* if.c */
	{ "UNLOAD",	NULL,		unloadcmd,		0 }, /* alias.c */
	{ "UNSHIFT",	NULL,		unshift_cmd,		0 },
	{ "UNTIL",	"UNTIL",	whilecmd,		0 },
	{ "UPING",	"UPING",	send_comm,		0 },
	{ "USERHOST",	NULL,		userhostcmd,		0 },
	{ "USERIP",	"USERIP",	useripcmd,		0 },
	{ "USLEEP",	NULL,		usleepcmd,		0 },
	{ "USRIP",	"USRIP",	usripcmd,		0 },
	{ "VERSION",	"VERSION",	version,		0 },
	{ "WAIT",	NULL,		waitcmd,		0 },
	{ "WALLCHOPS",	"WALLCHOPS",	send_2comm,		0 },
	{ "WALLOPS",	"WALLOPS",	e_wallop,		0 },
	{ "WHICH",	"WHICH",	load,			0 },
	{ "WHILE",	"WHILE",	whilecmd,		0 }, /* if.c */
	{ "WHO",	"WHO",		whocmd,			0 }, /* who.c */
	{ "WHOIS",	"WHOIS",	whois,			0 },
	{ "WHOWAS",	"WHOWAS",	whois,			0 },
	{ "WINDOW",	NULL,		windowcmd,		0 }, /* window.c */
	{ "XDEBUG",	NULL,		xdebugcmd,		0 }, /* debug.c */
	{ "XECHO",	"XECHO",	echocmd,		0 },
	{ "XEVAL",	"XEVAL",	xevalcmd,		0 },
	{ "XQUOTE",	"XQUOTE",	quotecmd,		0 },
	{ "XTYPE",	NULL,		xtypecmd,		0 },
	{ NULL,		NULL,		comment,		0 }
};

/* number of entries in irc_command array */
#define	NUMBER_OF_COMMANDS (sizeof(irc_command) / sizeof(IrcCommand)) - 2

/* 
 * Full scale abort.  Does a "save" into the filename in line, and
 * then does a coredump
 */
static void 	really_save (char *, int, int, int);
BUILT_IN_COMMAND(abortcmd)
{
	char	*filename = next_arg(args, &args);
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
			arg = strchr(args, ' ');
			if (arg)
				*arg++ = '\0';
			else
				arg = empty_string;

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
			else
			{
				say("%s: %s unknown flag", command, args);
				return;
			}
		}
	}

	if (flag == AWAY_ALL)
	{
		for (i = 0; i < number_of_servers; i++)
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
	char	*arg,
		*expand;
	char buffer[BIG_BUFFER_SIZE + 1];

	/* Hrm.  Is it worse to do new_next_arg than next_arg? */
	if ((arg = new_next_arg(args, &args)) != NULL)
	{
		if ((expand = expand_twiddle(arg)) != NULL)
		{
			if (chdir(expand))
				say("CD: %s", strerror(errno));
			new_free(&expand);
		}
		else
			say("CD: No such user");
	}
	getcwd(buffer, BIG_BUFFER_SIZE+1);
	say("Current directory: %s", buffer);
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
			unhold = 1;

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
			clear_all_windows(unhold, visible, hidden);
		else
			unclear_all_windows(unhold, visible, hidden);
	}
	else
	{
		if (unhold)
			hold_mode(NULL, OFF, 1);

		if (clear)
			clear_window_by_refnum(0);
		else
			unclear_window_by_refnum(0);
	}
	update_input(UPDATE_JUST_CURSOR);
}

/* comment: does the /COMMENT command, useful in .ircrc */
BUILT_IN_COMMAND(comment)
{
	/* nothing to do... */
}

BUILT_IN_COMMAND(ctcp)
{
	char	*to;
	char	*stag;
	int	tag;
	int	type;

	if ((to = next_arg(args, &args)) != NULL)
	{
		if (!strcmp(to, "*"))
			if ((to = get_channel_by_refnum(0)) == NULL)
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

BUILT_IN_COMMAND(dcc)
{
	if (*args)
		process_dcc(args);
	else
		dcc_list((char *) NULL);
}

static	char ** defer_list = NULL;
static	int	defer_list_size = -1;
	int	need_defered_commands = 0;

void	do_defered_commands (void)
{
	int	i;

	if (defer_list)
	{
		for (i = 0; defer_list[i]; i++)
		{
			parse_line("deferred", defer_list[i], NULL, 0, 0);
			new_free(&defer_list[i]);
		}
	}

	defer_list_size = 1;
	RESIZE(defer_list, char *, defer_list_size);
	defer_list[0] = NULL;
	need_defered_commands = 0;
}

BUILT_IN_COMMAND(defercmd)
{
	/* Bootstrap the defer list */
	if (defer_list_size <= 0)
	{
		defer_list_size = 1;
		RESIZE(defer_list, char *, defer_list_size);
		defer_list[0] = NULL;
	}

	defer_list_size++;
	RESIZE(defer_list, char *, defer_list_size);
	defer_list[defer_list_size - 2] = m_strdup(args);
	defer_list[defer_list_size - 1] = NULL;
	need_defered_commands++;
}


BUILT_IN_COMMAND(deop)
{
	send_to_server("MODE %s -o", get_server_nickname(from_server));
}

BUILT_IN_COMMAND(describe)
{
	char	*target;

	target = next_arg(args, &args);
	if (target && args && *args)
	{
		char	*message;

		if (!strcmp(target, "*"))
			if ((target = get_channel_by_refnum(0)) == NULL)
				target = zero;

		message = args;

		message_from(target, LOG_ACTION);
		send_ctcp(CTCP_PRIVMSG, target, CTCP_ACTION, "%s", message);
		if (do_hook(SEND_ACTION_LIST, "%s %s", target, message))
			put_it("* -> %s: %s %s", target, get_server_nickname(from_server), message);
		message_from(NULL, LOG_CRAP);
	}
	else
		say("Usage: /DESCRIBE <[=]nick|channel|*> <action description>");
}

BUILT_IN_COMMAND(do_send_text)
{
	char	*tmp;

	if (command)
		tmp = get_channel_by_refnum(0);
	else
		tmp = get_target_by_refnum(0);

	send_text(tmp, args, NULL, 1);
}

/*
 * e_channel: does the channel command.  I just added displaying your current
 * channel if none is given 
 */
BUILT_IN_COMMAND(e_channel)
{
	if (args && *args)
	{
		window_rejoin(current_window, &args);
		update_all_status();
	}
	else
		list_channels();

	message_from(NULL, LOG_CRAP);
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

	if (from_server == -1)
	{
		say("You may not change nicknames when not connected to a server");
		return;
	}

	nick_command_is_pending(from_server, 1);
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
	struct timeval 	start;

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
	add_timer(0, "", seconds, 1, (int (*)(void *))comment, NULL, NULL, current_window);
	while (time_diff(get_time(NULL), start) > 0)
		io("e_pause");
}

/*
 * e_privmsg: The MSG command, displaying a message on the screen indicating
 * the message was sent.  Also, this works for the NOTICE command. 
 */
BUILT_IN_COMMAND(e_privmsg)
{
	char	*nick;

	if ((nick = new_next_arg(args, &args)) != NULL)
	{
		if (!strcmp(nick, "."))
		{
			if (!(nick = sent_nick))
			{
				say("You have not sent a message to anyone yet.");
				return;
			}
		}
		else if (!strcmp(nick, ",")) 
		{
			if (!(nick = recv_nick))
			{
				say("You have not received a message from anyone yet.");
				return;
			}
		}
		else if (!strcmp(nick, "*") && (!(nick = get_channel_by_refnum(0))))
			nick = zero;
		send_text(nick, args, command, window_display);
		malloc_strcpy(&sent_body, args);
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
	char *	reason;

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
	char 	*arg;
	char 	*channel = get_channel_by_refnum(0);
	int	clear_topic = 0;
	char	*args_copy;

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
			send_to_server("%s %s :%s", command, arg, args);
		else
			send_to_server("%s %s", command, arg);
	}
	else if (channel)
		send_to_server("%s %s :%s", command, channel, args_copy);
	else
		say("You are not on a channel in this window.");
}


/* e_wallop: used for WALLOPS (undernet only command) */
BUILT_IN_COMMAND(e_wallop)
{
	message_from(NULL, LOG_WALLOP);
	send_to_server("%s :%s", command, args);
	message_from(NULL, LOG_CRAP);
}

/*
 * echo: simply displays the args to the screen, or, if it's XECHO,
 * processes the flags first, then displays the text on the screen
 * XECHO	<- dont delete this, i search for it. ;-)
 */
BUILT_IN_COMMAND(echocmd)
{
	unsigned display;
	int	lastlog_level = 0;
	int	from_level = 0;
	char	*flag_arg;
	int	temp;
	Window *old_to_window;
	int	all_windows = 0;
	int	banner = 0;
	char	*stuff = NULL;
	int	nolog = 0;
	int	more = 1;
	int	old_und = 0, old_rev = 0, old_bold = 0, 
		old_color = 0, old_blink = 0, old_ansi = 0;
	int	xtended = 0;

	old_to_window = to_window;
	if (command && *command == 'X')
	{
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
					yell("%s: -LINE only works if -WIN is specified first", command);
					to_window = old_to_window;
					return;
				}
				else if (to_window->scratch_line == -1)
				{
					yell("%s: -LINE only works on scratch windows", command);
					to_window = old_to_window;
					return;
				}

				to_line = my_atol(next_arg(args, &args));
				if (to_line < 0 || 
					to_line >= to_window->display_size)
				{
					yell("%s: -LINE %d is out of range for window (max %d)", 
						command, to_line, 
						to_window->display_size - 1);
					to_window = old_to_window;
					return;
				}
				to_window->scratch_line = to_line;
			}

			/* LEVEL (use specified lastlog level) */
			else	
			{
				if (!(flag_arg = next_arg(args, &args)))
					break;
				if ((temp = parse_lastlog_level(flag_arg)))
				{
					lastlog_level = 
						set_lastlog_msg_level(temp);
					from_level = message_from_level(temp);
				}
			}
			break;
		    }

		    case 'V':	/* VISUAL (output to a visible window) */
		    {
			/* There is always at least one visible window! */
			Window *win = NULL;
			while ((traverse_all_windows(&win)))
			{
				if (win->visible)
				{
					to_window = win;
					break;
				}
			}
			break;
		    }

		    case 'W':	/* WINDOW (output to specified window) */
		    {
			flag_arg = next_arg(args, &args);
			if (!(flag_arg = next_arg(args, &args)))
				break;
			if (isdigit(*flag_arg))
				to_window = get_window_by_refnum(my_atol(flag_arg));
			else
				to_window = get_window_by_name(flag_arg);
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
			banner = 1;
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
			next_arg(args, &args);

			old_und = get_int_var(UNDERLINE_VIDEO_VAR);
			old_rev = get_int_var(INVERSE_VIDEO_VAR);
			old_bold = get_int_var(BOLD_VIDEO_VAR);
			old_color = get_int_var(COLOR_VAR);
			old_blink = get_int_var(BLINK_VIDEO_VAR);
			old_ansi = get_int_var(DISPLAY_ANSI_VAR);

			set_int_var(UNDERLINE_VIDEO_VAR, 1);
			set_int_var(INVERSE_VIDEO_VAR, 1);
			set_int_var(BOLD_VIDEO_VAR, 1);
			set_int_var(COLOR_VAR, 1);
			set_int_var(BLINK_VIDEO_VAR, 1);
			set_int_var(DISPLAY_ANSI_VAR, 1);

			xtended = 1;
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
		    args = empty_string;
	    }
	}

	display = window_display;
	window_display = 1;
	if (nolog)
		inhibit_logging = 1;

	if (banner == 1)
	{
		malloc_strcpy(&stuff, numeric_banner());
		if (*stuff)
		{
			m_3cat(&stuff, space, args);
			args = stuff;
		}
	}
	else if (banner != 0)
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
		set_int_var(UNDERLINE_VIDEO_VAR, old_und);
		set_int_var(INVERSE_VIDEO_VAR, old_rev);
		set_int_var(BOLD_VIDEO_VAR, old_bold);
		set_int_var(COLOR_VAR, old_color);
		set_int_var(BLINK_VIDEO_VAR, old_blink);
		set_int_var(DISPLAY_ANSI_VAR, old_ansi);
	}

	if (lastlog_level)
	{
		set_lastlog_msg_level(lastlog_level);
		message_from_level(from_level);
	}
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
		if (!my_strnicmp(flag + 1, "S", 1)) /* SERVER */
		{
			int val = my_atol(next_arg(args, &args));
			if (is_server_connected(val))
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

	parse_line(NULL, args, subargs ? subargs : empty_string, 0, 0);

	to_window = old_to_window;
	make_window_current_by_refnum(old_refnum);
	from_server = old_from_server;
}

BUILT_IN_COMMAND(evalcmd)
{
	parse_line(NULL, args, subargs ? subargs : empty_string, 0, 0);
}

/* flush: flushes all pending stuff coming from the server */
BUILT_IN_COMMAND(flush)
{
	if (get_int_var(HOLD_MODE_VAR))
		flush_everything_being_held(NULL);

	say("Standby, Flushing server output...");
	flush_server(from_server);
	say("Done");
}

BUILT_IN_COMMAND(funny_stuff)
{
	char	*arg,
		*stuff;
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
			else if (my_strnicmp(arg+1, "W", 1) == 0)	/* WIDE */
				flags |= FUNNY_WIDE;
			else if (my_strnicmp(arg+1, "U", 1) == 0)	/* USERS */
				flags |= FUNNY_USERS;
			else if (my_strnicmp(arg+1, "N", 1) == 0)	/* NAME */
				flags |= FUNNY_NAME;
			else
				stuff = arg;
		}
		else stuff = arg;
	}

	set_funny_flags(min, max, flags);
	if (strcmp(stuff, "*") == 0)
		if (!(stuff = get_channel_by_refnum(0)))
			stuff = empty_string;

	/* Channel names can contain stars! */
	if (strchr(stuff, '*') && !im_on_channel(stuff, from_server))
	{
		funny_match(stuff);
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
		funny_match(NULL);
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
		const char *s = switch_hostname(args);
		say("%s\n", s);
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
		say("\tCopyright 1993-2002 EPIC Software Labs");
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
		say("	    \tJake Khuon           <khuon@epicsol.org>");
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
		say("\t\thttp://www.epicsol.org/credits.phtml");
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
	send_to_server("%s %s", command, args?args:empty_string);
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
	yell("Coypright © 1995, 2000 Jeremy Nelson and others ");
	yell("                       (\"EPIC Software Labs\").");
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
	int	package_set_here;
	int	line;
	int	start_line;
} load_level[MAX_LOAD_DEPTH] = { { NULL, NULL, 0, 0, 0 } };

int 	load_depth = -1;

void dump_load_stack (int onelevel)
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

int	current_line (void)
{ 
	if (load_depth == -1)
		return -1;
	else
		return load_level[load_depth].line;
}

char *current_package (void)
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

/*
 * load: the /LOAD command.  Reads the named file, parsing each line as
 * though it were typed in (passes each line to parse_line). 
 * ---loadcmd---
 */
BUILT_IN_COMMAND(load)
{
	FILE	*fp;
	char	*filename,
		*expanded = NULL;
	int	flag = 0;
        int     paste_level = 0;
	char	*start,
		*current_row = NULL,
#define MAX_LINE_SIZE BIG_BUFFER_SIZE * 5
		buffer[MAX_LINE_SIZE * 2 + 1];
	int	no_semicolon = 1;
	char	*irc_path;
	int	display;

	if (!(irc_path = get_string_var(LOAD_PATH_VAR)))
	{
		say("LOAD_PATH has not been set");
		return;
	}

	if (++load_depth == MAX_LOAD_DEPTH)
	{
		load_depth--;
		dump_load_stack(0);
		say("No more than %d levels of LOADs allowed", MAX_LOAD_DEPTH);
		return;
	}

	display = window_display;
	window_display = 0;
	status_update(0);	/* No updates to the status bar! */

	/* 
	 * We iterate over the whole list -- if we use the -args flag, the
	 * we will make a note to exit the loop at the bottom after we've
	 * gone through it once...
	 */
	while (args && *args && (filename = next_arg(args, &args)))
	{
		/* 
		 * If we use the args flag, then we will get the next
		 * filename (via continue) but at the bottom of the loop
		 * we will exit the loop 
		 */
		if (my_strnicmp(filename, "-args", strlen(filename)) == 0)
		{
			flag = 1;
			continue;
		}

		if (!(expanded = expand_twiddle(filename)))
		{
			error("Unknown user for file %s", filename);
			continue;
		}

		if (!(fp = uzfopen(&expanded, irc_path, 1)))
		{
			/*
			 * uzfopen emits an error if the file
			 * is not found, so we dont have to.
			 */
			new_free(&expanded);
			continue;
		}

		/*
		 * Is it the "WHICH" command?
		 */
		if (command && *command == 'W')
		{
			yell("%s", expanded);	/* window_display is 0 here. */
			if (fp)
				fclose (fp);
			new_free(&expanded);
			continue;
		}

		/*
		 * No, it is the "LOAD" command. so load it.
		 */
		{
		int	in_comment 	=  0;
		int	comment_line 	= -1;
		int 	paste_line	= -1;
		char *	real_start	= NULL;

		current_row = NULL;
		load_level[load_depth].filename = expanded;
		load_level[load_depth].line = 1;
		if (load_depth > 0 && load_level[load_depth - 1].package)
			malloc_strcpy(&load_level[load_depth].package,
					load_level[load_depth-1].package);

		for (;;load_level[load_depth].line++)
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
				load_level[load_depth].line++;
			}

			if (start[len - 1] == '\n')
				start[--len] = '\0';


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

				else if ((!ptr || 
					(ptr != start || *ptr == '/')) && 
					current_row)
				{
				    if (!paste_level)
				    {
					parse_line(NULL, current_row, 
					   flag 
					   ? args 
					   : get_int_var(INPUT_ALIASES_VAR) 
					     ? empty_string 
					     : NULL,
					   0, 0);
					new_free(&current_row);
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

/* switch statement tabbed back */
case '/' :
{
	no_semicolon = 1;

	/* 
	 * If we're in a comment, any slashes that arent preceeded by
	 * a star is just ignored (cause its in a comment, after all >;) 
	 */
	if (in_comment)
	{
		/* ooops! cant do ptr[-1] if ptr == optr... doh! */
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
	 * textbox, etc) may use slash-star in some ascii graphics.
	 */
	if ((ptr[1] == '*')
		&& (!get_int_var(COMMENT_HACK_VAR) || ptr == real_start))
	{
		/* Yep. its the start of a comment. */
		in_comment = 1;
		comment_line = load_level[load_depth].line;
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
			/* Send the line off to parse_line */
			parse_line(NULL, current_row, flag 
				 ? args 
				 : get_int_var(INPUT_ALIASES_VAR) 
				   ? empty_string 
				   : NULL, 0, 0);

			new_free(&current_row);
		    }

		    /* Just add semicolon and keep going */
		    else
			no_semicolon = 0;
		}
	}
	break;
}
case '{' :
{
	if (in_comment)
		break;

#ifdef BRACE_LOAD_HACK
	/*
	 * left-brace is probably only introducing a new brace-set if it
	 *	*) Leads off a line
	 *	*) Is preceeded by whitespace
	 *	*) Is not immediately following an rparen or rbrace
	 * In any other such case, the { is taken as a literal character and
	 * is not used for bracing.
	 */
	if (ptr > start && !isspace(ptr[-1]) && ptr[-1] != ')' &&
		ptr[-1] != '}' && ptr[-1] != '{' && ptr[-1] != '(' &&
		ptr[-1] != '$' && ptr[-1] != '%')
	{
		malloc_strcat(&current_row, "{");
		if (paste_level)
			no_semicolon = 0;
		else
			no_semicolon = 1;
	}
	else
	{
#endif
		/* If we are opening a brand new {} pair, remember the line */
		if (!paste_level)
			paste_line = load_level[load_depth].line;

		paste_level++;
		if (ptr == start)
			malloc_strcat(&current_row, " {");
		else
			malloc_strcat(&current_row, "{");
		no_semicolon = 1;
#ifdef BRACE_LOAD_HACK
	}
#endif
	break;
}
case '}' :
{
	if (in_comment)
		break;

#ifdef BRACE_LOAD_HACK
	/*
	 * Same as above, only in reverse.  An rbrace is only taken as a
	 * special character if it
	 *	*) Ends a line
	 *	*) Is followed by whitespace
	 *	*) Is followed by a rparen or rbrace.
	 * Otherwise, it is just a normal character.
	 */
	if (ptr == start || isspace(ptr[1]) || ptr[1] != '(' || ptr[1] != ')'
		|| ptr[1] != '{' || ptr[1] != '}')
	{
#endif
		if (!paste_level)
		{
			error("Unexpected } in %s, line %d",
					expanded, load_level[load_depth].line);
			break;
		}

		paste_level--;
		/* If we're back to "level 0", then reset the paste line */
		if (!paste_level)
			paste_line = -1;
		malloc_strcat(&current_row, "}");
		no_semicolon = ptr[1] ? 1 : 0;
		break;
#ifdef BRACE_LOAD_HACK
	}
	else
	{	
		malloc_strcat(&current_row, "}");
		if (paste_level)
			no_semicolon = 0;
		else
			no_semicolon = 1;
	}
	break;
#endif
}
case ';' :
{
	if (in_comment)
		break;

	malloc_strcat(&current_row, ";");
	/*
	 * I guess it was totaly wrong how ircII handled semicolons and
	 * we wont attempt to out-guess the user.  Any semicolons are
	 * left as-is.
	 */
	if (ptr[1] == 0 && !paste_level)
	{
		parse_line(NULL, current_row, flag ? args : 
			get_int_var(INPUT_ALIASES_VAR) ? empty_string : NULL,
			0, 0);
		new_free(&current_row);
	}
	else if (ptr[1] == 0 && paste_level)
		no_semicolon = 0;
	else
		no_semicolon = 1;

	break;
}
/* End of reformatting */

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
			expanded, comment_line);

		if (current_row)
		{
		    if (paste_level)
		    error("Unexpected EOF in %s trying to match '{' at line %d",
			expanded, paste_line);
		    else
		        parse_line(NULL, current_row, flag ? args : 
			   get_int_var(INPUT_ALIASES_VAR) ? empty_string : NULL,
			   0, 0);

		    new_free(&current_row);
		}
		} /* End of direct /LOAD command */

		new_free(&expanded);
		fclose(fp);

	} /* End of while (args && *args) */

	/*
	 * Restore some sanity
	 */
	if (get_int_var(DISPLAY_VAR))
	       window_display = display;
	status_update(1);

	new_free(&load_level[load_depth].package);
	load_depth--;
}


/*
 * The /me command.  Does CTCP ACTION.  Dont ask me why this isnt the
 * same as /describe...
 */
BUILT_IN_COMMAND(mecmd)
{
	if (args && *args)
	{
		char	*target;

		if ((target = get_target_by_refnum(0)) != NULL)
		{
			int 	old;

			send_ctcp(CTCP_PRIVMSG, target, CTCP_ACTION, 
					"%s", args);

			message_from(target, LOG_ACTION);
			old = set_lastlog_msg_level(LOG_ACTION);
			if (do_hook(SEND_ACTION_LIST, "%s %s", target, args))
				put_it("* %s %s", get_server_nickname(from_server), args);
			set_lastlog_msg_level(old);
			message_from(NULL, LOG_CRAP);
		}
		else
			say("No target, neither channel nor query");
	}
	else
		say("Usage: /ME <action description>");
}

/*
 * This isnt a command, its used by the wait command.  Since its extern,
 * and it doesnt use anything static in this file, im sure it doesnt
 * belong here.
 */
static void oh_my_wait (int servnum)
{
	int w_server;

	if ((w_server = servnum) == -1)
		w_server = primary_server;

	if (is_server_connected(w_server))
	{
		int old_doing_privmsg = doing_privmsg;
		int old_doing_notice = doing_notice;
		int old_in_ctcp_flag = in_ctcp_flag;
		int old_from_server = from_server;

		waiting_out++;
		lock_stack_frame();
		send_to_aserver(w_server, "%s", lame_wait_nick);
		while (waiting_in < waiting_out)
			io("oh_my_wait");

		doing_privmsg = old_doing_privmsg;
		doing_notice = old_doing_notice;
		in_ctcp_flag = old_in_ctcp_flag;
		from_server = old_from_server;
	}
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
	struct timeval t;
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
	int	s = from_server;
	char	buffer[IO_BUFFER_SIZE + 1];

	strlcpy(buffer, args, IRCD_BUFFER_SIZE);	/* Sanity Check */
	parse_server(buffer);
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
	update_window_status(current_window, 0);
}

/*
 * quote: handles the QUOTE command.  args are a direct server command which
 * is simply send directly to the server 
 */
BUILT_IN_COMMAND(quotecmd)
{
	int	old_from_server = from_server;
	int	urlencoded = 0;
	size_t	length;

	if (*command == 'X')
	{
		while (args && (*args == '-' || *args == '/'))
		{
			char *flag = next_arg(args, &args);
			if (!my_strnicmp(flag + 1, "S", 1)) /* SERVER */
			{
				int sval = my_atol(next_arg(args, &args));
				if (is_server_connected(sval))
					from_server = sval;
			}
			else if (!my_strnicmp(flag + 1, "U", 1)) /* URL quoting */
			{
				urlencoded++;
			}
			else if (!my_strnicmp(flag + 1, "A", 1)) /* ALL */
			{
				int	i;
				if (args && *args)
				{
					for (i = 0; i < number_of_servers; i++)
					{
						if (is_server_connected(i))
						{
						    from_server = i;
						    send_to_server("%s", args);
						}
					}
				}
				goto end;
			}
			else
			{
				/* End option processing on unknown arg. */
				break;
			}
		}
	}

	if (urlencoded)
	{
		length = strlen(args);
		urldecode(args, &length);
		send_to_server_raw(length, args);
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
			goto end;
		}

		/*
		 * If we know its going to cause a problem in the 
		 * future, whine about it.
		 */
		if (cnt < 0 && (rfc1459[loc].flags & PROTO_DEPREC))
			yell("Doing /QUOTE %s is discouraged because it will destablize the client.  Use the client's built in command instead.", comm);

		send_to_server("%s %s", comm, args);
	}

end:
	from_server = old_from_server;
}

/* This code is courtesy of Richie B. (richie@morra.et.tudelft.nl) */
/*
 * REALNAME command. Changes the current realname. This will only be parsed
 * to the server when the client is connected again.
 */
BUILT_IN_COMMAND(realname_cmd)
{
        if (*args)
	{
                strmcpy(realname, args, REALNAME_LEN);
		say("Realname at next server connnection: %s", realname);
	}
	else
		say("Usage: /REALNAME [text of realname]");
}

/*
 * RECONNECT command. Closes the server, and then reconnects again.
 * Works also while connected to multiple servers. It only reconnects to the
 * current server number (which is stored in from_server). 
 * This command puts the REALNAME command in effect.
 */
BUILT_IN_COMMAND(reconnect_cmd)
{
	if (from_server == -1)
	{
		say("Reconnect to what server? (You're not connected)");
		return;
	}
	if (!args || !*args)
		args = "Reconnecting";

        say("Reconnecting to server %d", from_server);
	set_server_quit_message(from_server, args);
	server_reconnects_to(from_server, from_server);
	reconnect(from_server, 1);
}
/* End of contributed code */

BUILT_IN_COMMAND(redirect)
{
	char	*who;

	if ((who = next_arg(args, &args)) == NULL)
	{
		say("%s", "Usage: /REDIRECT <nick|channel|%process> <cmd>");
		return;
	}

	if (from_server == -1)
	{
		say("You may not use /REDIRECT here.");
		return;
	}

	if (!strcmp(who, "*") && !(who = get_channel_by_refnum(0)))
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
	if ((*who == '=') && !dcc_chat_active(who + 1)) 
	{
		say("You don't have an active DCC CHAT to %s",who + 1);
		return;
	}

	/*
	 * Turn on redirect, and do the thing.
	 */
	set_server_redirect(from_server, who);
	clear_sent_to_server(from_server);
	parse_line(NULL, args, NULL, 0, 0);

	/*
	 * If we've queried the server, then we wait for it to
	 * reply, otherwise we're done.
	 */
	if (sent_to_server(from_server))
		send_to_server("***%s", who);
	else
		set_server_redirect(from_server, NULL);
}

/* This generates a file of your ircII setup */
static void really_save (char *file, int flags, int save_all, int append)
{
	FILE	*fp;
static	char *	mode[] = {"w", "a"};

	if (!(fp = fopen(file, mode[append]))) 
	{
		say("Error opening %s: %s", file, strerror(errno));
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
	say("Settings %s to %s", append ? "appended" : "saved", file);
}

/* save_settings: saves the current state of IRCII to a file */
BUILT_IN_COMMAND(save_settings)
{
	char *	arg;
	char *	fn;
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

	if (!arg || !*arg || !(fn = expand_twiddle(arg)))
	{
		say("Invalid filename or path");
		return;
	}

	really_save(fn, save_flags, save_global, save_append);
	new_free(&fn);
}

BUILT_IN_COMMAND(send_2comm)
{
	char	*reason = NULL;

	if (!(args = next_arg(args, &reason)))
		args = empty_string;
	if (!reason || !*reason)
		reason = empty_string;

	if (!args || !*args || !strcmp(args, "*"))
	{
		args = get_channel_by_refnum(0);
		if (!args || !*args)
			args = "*";	/* what-EVER */
	}

	if (reason && *reason)
		send_to_server("%s %s :%s", command, args, reason);
	else
		send_to_server("%s %s", command, args);
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
	char	*kickee,
		*comment;
	char	*channel;

	char usage[] = "Usage: %s <channel|*> <nickname> [comment]";

	if (!(channel = next_arg(args, &args)))
	{
		yell(usage, command);
		return;
	}

	if (!(kickee = next_arg(args, &args)))
	{
		yell(usage, command);
                return;
	}

	comment = args?args:empty_string;
	if (!strcmp(channel, "*"))
		channel = get_channel_by_refnum(0);

	send_to_server("%s %s %s :%s", command, channel, kickee, comment);
}

/*
 * send_channel_com: does the same as send_com for command where the first
 * argument is a channel name.  If the first argument is *, it is expanded
 * to the current channel (a la /WHO).
 */
BUILT_IN_COMMAND(send_channel_com)
{
	char	*ptr,
		*s;

	char usage[] = "Usage: %s <*|#channel> [arguments]";

        ptr = next_arg(args, &args);

	if (ptr && !strcmp(ptr, "*"))
	{
		if ((s = get_channel_by_refnum(0)) != NULL)
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
	parse_line(NULL, args, get_int_var(INPUT_ALIASES_VAR) ? empty_string : NULL, 1, 0);
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
			strmcpy(username, empty_string, NAME_LEN);
		else 
			strmcpy(username, blah, NAME_LEN);
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

	if ((arg = next_arg(args, &args)) != NULL)
		sleep(my_atol(arg));
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
		len = strlen(arg);
		if (!my_strnicmp(arg, "ON", len))
			do_stack_on(type, args);
		else if (!my_strnicmp(arg, "ALIAS", len))
			do_stack_alias(type, args, STACK_DO_ALIAS);
		else if (!my_strnicmp(arg, "ASSIGN", len))
			do_stack_alias(type, args, STACK_DO_ASSIGN);
		else if (!my_strnicmp(arg, "SET", len))
			do_stack_set(type, args);
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
	char 		*arg;
	struct timeval 	pause;
	time_t		nms;

	if ((arg = next_arg(args, &args)))
	{
		nms = (time_t)my_atol(arg);
		pause.tv_sec = nms / 1000000;
		pause.tv_usec = nms % 1000000;
		select(0, NULL, NULL, NULL, &pause);
	}
	else
		say("Usage: USLEEP <usec>");
}


/* version: does the /VERSION command with some IRCII version stuff */
BUILT_IN_COMMAND(version)
{
	char	*host;

	if ((host = next_arg(args, &args)) != NULL)
		send_to_server("%s %s", command, host);
	else
	{ 
		say ("Client: ircII %s (Internal Version: %s)", irc_version, internal_version);
		send_to_server("%s", command);
	}
}

BUILT_IN_COMMAND(waitcmd)
{
	char	*ctl_arg = next_arg(args, &args);

	if (ctl_arg && !my_strnicmp(ctl_arg, "-c", 2))
	{
		WaitCmd	*new_wait;

		new_wait = (WaitCmd *) new_malloc(sizeof(WaitCmd));
		new_wait->stuff = m_strdup(args);
		new_wait->next = NULL;

		if (end_wait_list)
			end_wait_list->next = new_wait;
		end_wait_list = new_wait;
		if (!start_wait_list)
			start_wait_list = new_wait;
		send_to_server("%s", wait_nick);
	}

	else if (ctl_arg && !my_strnicmp(ctl_arg, "for", 3))
	{
		clear_sent_to_server(from_server);
		lock_stack_frame();
		parse_line(NULL, args, subargs, 0, 0);
		unlock_stack_frame();
		if (sent_to_server(from_server))
			oh_my_wait(from_server);
		clear_sent_to_server(from_server);	/* reset it again */
	}

	else if (ctl_arg && *ctl_arg == '%')
	{
		int	w_index = is_valid_process(ctl_arg);

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
#if 0	/* Bogus */
				set_input(empty_string);
#endif
				lock_stack_frame();
				while (process_is_running(ctl_arg))
					io("wait %proc");
				unlock_stack_frame();
			}
		}
	}
	else if (ctl_arg)
		yell("Unknown argument to /WAIT");
	else
	{
		oh_my_wait(from_server);
		clear_sent_to_server(from_server);
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

	if (*args == '-' || *args == '/')
	{
		char saved = *args;
		args++;
		if ((arg = next_arg(args, &args)) != NULL)
		{
			if (!my_strnicmp(arg, "L", 1))
			{
				for (; *args; args++)
					input_add_character(*args, empty_string);
			}
			else
				say ("Unknown flag -%s to XTYPE", arg);
			return;
		}
		input_add_character(saved, empty_string);
	}
	else
		type(command, args, empty_string);
	return;
}

/*
 * 
 * The rest of this file is the stuff that is not a command but is used
 * by the commands in this file.  These are considered to be "overhead".
 *
 */

/*
 * find_command: looks for the given name in the command list, returning a
 * pointer to the first match and the number of matches in cnt.  If no
 * matches are found, null is returned (as well as cnt being 0). The command
 * list is sorted, so we do a binary search.  The returned commands always
 * points to the first match in the list.  If the match is exact, it is
 * returned and cnt is set to the number of matches * -1.  Thus is 4 commands
 * matched, but the first was as exact match, cnt is -4.
 */
static IrcCommand *find_command (char *com, int *cnt)
{
	IrcCommand *retval;
	int loc;

	/*
	 * As a special case, the empty or NULL command is send_text.
	 */
	if (!com || !*com)
	{
		*cnt = -1;
		return irc_command;
	}

	retval = (IrcCommand *)find_fixed_array_item ((void *)irc_command, 
			sizeof(IrcCommand), NUMBER_OF_COMMANDS + 1, 
			com, cnt, &loc);
	return retval;
}

/*
 * Returns 1 if the given built in command exists,
 * Returns 0 if not.
 */
int	command_exist (char *command)
{
	int num;
	char *buf;

	if (!command || !*command)
		return 0;

	buf = LOCAL_COPY(command);
	upper(buf);

	if (find_command(buf, &num))
		return (num < 0) ? 1 : 0;

	return 0;
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
	char *command;
	char *format;
	int  level;
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
		old_server,
		lastlog_level;
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
	{NULL, NULL, SEND_MSG_LIST,     "PRIVMSG", "*%s*> %s" , LOG_MSG }, 
	{NULL, NULL, SEND_PUBLIC_LIST,  "PRIVMSG", "%s> %s"   , LOG_PUBLIC },
	{NULL, NULL, SEND_NOTICE_LIST,  "NOTICE",  "-%s-> %s" , LOG_NOTICE }, 
	{NULL, NULL, SEND_NOTICE_LIST,  "NOTICE",  "-%s-> %s" , LOG_NOTICE }
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
	    else if (*current_nick == '@')
		target_file_write(current_nick + 1, text);

	    else if (*current_nick == '"')
		send_to_server("%s", text);

	    else if (*current_nick == '/')
	    {
		line = m_opendup(current_nick, " ", text, NULL);
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
			line = m_strdup(text);

		old_server = from_server;
		from_server = -1;
		dcc_chat_transmit(current_nick + 1, line, text, command, hook);
		from_server = old_server;
		malloc_strcpy(&sent_nick, current_nick);
		new_free(&line);
	    }
	    else
	    {
		char *	copy = NULL;

		if (doing_notice)
		{
			say("You cannot send a message from within ON NOTICE");
			continue;
		}

		i = is_channel(current_nick);
		if (doing_privmsg || (command && !strcmp(command, "NOTICE")))
			i += 2;

		if ((key = is_crypted(current_nick)))
		{
			copy = LOCAL_COPY(text);
			lastlog_level = set_lastlog_msg_level(target[i].level);
			message_from(current_nick, target[i].level);

			if (hook && do_hook(target[i].hook_type, "%s %s", 
						current_nick, copy))
				put_it(target[i].format, current_nick, copy);
			line = crypt_msg(copy, key);

			send_to_server("%s %s :%s", 
					target[i].command, current_nick, line);
			malloc_strcpy(&sent_nick, current_nick);

			new_free(&line);
			set_lastlog_msg_level(lastlog_level);
		}

		else
		{
			if (i == 0)
				malloc_strcpy(&sent_nick, current_nick);
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
		const char	*old_mf;
		int		old_ml;

		if (!target[i].message)
			continue;

		save_message_from(&old_mf, &old_ml);

		message_from(target[i].nick_list, target[i].level);
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

		restore_message_from(old_mf, old_ml);
	}

	/*
	 * If the user didnt explicitly send the text (hook == 1), then 
	 * it makes no sense to presume that theyre not still /AWAY.
	 * This also makes sure that the "no longer away" message doesnt
	 * get munched if window_display is 0.
	 */
	if (hook && get_server_away(from_server) && get_int_var(AUTO_UNMARK_AWAY_VAR))
		parse_line(NULL, "AWAY", empty_string, 0, 0);

	window_display = old_window_display;
	recursion--;
}



/*
 * eval_inputlist:  Cute little wrapper that calls parse_line() when we
 * get an input prompt ..
 */
static void	eval_inputlist (char *args, char *line)
{
	parse_line(NULL, args, line ? line : empty_string, 0, 0);
}

GET_FIXED_ARRAY_NAMES_FUNCTION(get_command, irc_command);

/* 
 * This is a key binding and has to be here because it looks at the
 * command array which is static to this file.
 */
/*
 * command_completion: builds lists of commands and aliases that match the
 * given command and displays them for the user's lookseeing 
 */
void 	command_completion (char unused, char *not_used)
{
	int		do_aliases = 1;
	int		cmd_cnt,
			alias_cnt,
			i,
			c,
			len;
	char		**aliases = NULL;
	char		*line = NULL,
			*com,
			*cmdchars,
			*rest,
			firstcmdchar[2] = "/";
	IrcCommand	*command;
	char		buffer[BIG_BUFFER_SIZE + 1];

	malloc_strcpy(&line, get_input());
	if (((com = next_arg(line, &rest)) != NULL) && *com)
	{
		if (!(cmdchars = get_string_var(CMDCHARS_VAR)))
			cmdchars = DEFAULT_CMDCHARS;

		if (*com == '/' || strchr(cmdchars, *com))
		{
			com++;
			*firstcmdchar = *cmdchars;
			if (*com && strchr(cmdchars, *com))
			{
				do_aliases = 0;
				alias_cnt = 0;
				com++;
			}
			else
				do_aliases = 1;

			upper(com);
			if (do_aliases)
				aliases = glob_cmd_alias(com, &alias_cnt);

			if ((command = find_command(com, &cmd_cnt)) != NULL)
			{
				if (cmd_cnt < 0)
					cmd_cnt *= -1;

				/* special case for the empty string */
				if (*(command->name) == (char) 0)
				{
					command++;
					cmd_cnt = NUMBER_OF_COMMANDS;
				}
			}

			if ((alias_cnt == 1) && (cmd_cnt == 0))
			{
				snprintf(buffer, BIG_BUFFER_SIZE, "%s%s %s", firstcmdchar, *aliases, rest);
				set_input(buffer);
				new_free((char **)aliases);
				new_free((char **)&aliases);
				update_input(UPDATE_ALL);
			}
			else if (((cmd_cnt == 1) && (alias_cnt == 0)) ||
			    ((cmd_cnt == 1) && (alias_cnt == 1) &&
			     (!strcmp(aliases[0], command->name))))
			{
				snprintf(buffer, BIG_BUFFER_SIZE, "%s%s%s %s", firstcmdchar,
					do_aliases ? empty_string : firstcmdchar,
					command->name, rest);
				set_input(buffer);
				update_input(UPDATE_ALL);
			}
			else
			{
				*buffer = (char) 0;
				if (command)
				{
					say("Commands:");
					strmcpy(buffer, "\t", BIG_BUFFER_SIZE);
					c = 0;
					for (i = 0; i < cmd_cnt; i++)
					{
						strmcat(buffer, command[i].name,
							BIG_BUFFER_SIZE);
						for (len = strlen(command[i].name); len < 15; len++)
							strmcat(buffer, " ", BIG_BUFFER_SIZE);
						if (++c == 4)
						{
							say("%s", buffer);
							strmcpy(buffer, "\t", BIG_BUFFER_SIZE);
							c = 0;
						}
					}
					if (c)
						say("%s", buffer);
				}
				if (aliases)
				{
					say("Aliases:");
					strmcpy(buffer, "\t", BIG_BUFFER_SIZE);
					c = 0;
					for (i = 0; i < alias_cnt; i++)
					{
						strmcat(buffer, aliases[i], BIG_BUFFER_SIZE);
						for (len = strlen(aliases[i]); len < 15; len++)
							strmcat(buffer, " ", BIG_BUFFER_SIZE);
						if (++c == 4)
						{
							say("%s", buffer);
							strmcpy(buffer, "\t", BIG_BUFFER_SIZE);
							c = 0;
						}
						new_free(&(aliases[i]));
					}
					if (strlen(buffer) > 1)
						say("%s", buffer);
					new_free((char **)&aliases);
				}
				if (!*buffer)
					term_beep();
			}
		}
		else
			term_beep();
	}
	else
		term_beep();

	new_free(&line);
}

BUILT_IN_COMMAND(e_call)
{
	dump_call_stack();
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
void	parse_line (const char *name, const char *org_line, const char *args, int hist_flag, int append_flag)
{
	char	*line = NULL,
		*stuff,
		*s,
		*t;
	int	args_flag = 0;
	int	die = 0;

	/*
	 * If this is an atomic scope, then we create a new local variable
	 * stack.  Otherwise, this command will use someone else's stack.
	 */
	if (name)
		make_local_stack(name);

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
		    parse_line(NULL, stuff, args, hist_flag, append_flag);

		    /*
		     * Check to see if an exception has been thrown.  If it
		     * has, then we stop parsing, but we dont catch the
		     * exception, someone upstream will catch it.
		     */
		    if ((will_catch_break_exceptions && break_exception) ||
			(will_catch_return_exceptions && return_exception) ||
			(will_catch_continue_exceptions && continue_exception))
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
		stuff = expand_alias(line, args, &args_flag, &line);
		if (!line && append_flag && !args_flag && args && *args)
			m_3cat(&stuff, " ", args);

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
		    (will_catch_continue_exceptions && continue_exception))
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
		    (will_catch_continue_exceptions && continue_exception))
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
	char *		cmdchars;
	const char *	com;
	int		args_flag,
			add_to_hist,
			cmdchar_used = 0;
	int		noisy = 1;
	char *		this_cmd = NULL;

	if (!line || !*line) 
		return 0;

	if (get_int_var(DEBUG_VAR) & DEBUG_COMMANDS)
		yell("Executing [%d] %s", level, line);
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
		char *		l_ptr;

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
			if ((l_ptr = MatchingBracket(my_line + 1, '(', ')')))
				*l_ptr++ = 0;
		}

		if ((tmp = parse_inline(my_line + 1, sub_args, &args_flag)))
			new_free(&tmp);

		if (hist_flag && add_to_hist)
			add_to_history(this_cmd);
	}
	else
	{
		char		*rest,
				*alias = NULL,
				*alias_name = NULL;
		char		*cline;
		int		cmd_cnt,
				alias_cnt = 0;
		IrcCommand	*command;
		void		*arglist = NULL;

		if ((rest = strchr(line, ' ')))
		{
			cline = alloca((rest - line) + 1);
			strmcpy(cline, line, rest - line);
			rest++;
		}
		else
		{
			cline = LOCAL_COPY(line);
			rest = empty_string;
		}

		upper(cline);

		if (cmdchar_used < 2)
			alias = get_cmd_alias(cline, &alias_cnt, 
						&alias_name, &arglist);

		if (alias && alias_cnt < 0)
		{
			if (hist_flag && add_to_hist)
				add_to_history(this_cmd);
			call_user_alias(alias_name, alias, rest, arglist);
			new_free(&alias_name);
		}
		else
		{
			/* History */
			if (*cline == '!')
			{
				if ((cline = do_history(cline + 1, rest)) != NULL)
				{
					if (level == 1)
					{
						set_input(cline);
						update_input(UPDATE_ALL);
					}
					else
						parse_command(cline, 0, sub_args);

					new_free(&cline);
				}
				else
					set_input(empty_string);
			}
			else
			{
				if (hist_flag && add_to_hist)
					add_to_history(this_cmd);
				command = find_command(cline, &cmd_cnt);

				/*
				 * At this point we know that there are
				 * no exact matches for the alias.
				 * What we do have to do is check to see
				 * if there is a completed match for any
				 * aliases (alias_cnt == 1), or completed
				 * match for any commands (cmd_cnt == 1)
				 * or any exact matches for commands
				 * (cmd_cnt == -1)
				 */

				/*
				 * First we see if there is an exact match
				 * for commands, or if there was a completion
				 * with no alias completions:
				 */
				if (cmd_cnt < 0 ||
				   (alias_cnt == 0 && cmd_cnt == 1))
				{
				    /* I should make a function to do this */
					if (!strcmp(command->name, "EXEC") && get_int_var(SECURITY_VAR) & SECURITY_NO_NONINTERACTIVE_EXEC)
						yell("Warning: the command '%s %s' was not executed due to a security violation", command->name, rest);
					else if (!strcmp(command->name, "SET") && get_int_var(SECURITY_VAR) & SECURITY_NO_NONINTERACTIVE_SET)
						yell("Warning: the command '%s %s' was not executed due to a security violation", command->name, rest);
					else if (command->func)
						command->func(command->server_func, rest, sub_args);
					else
						say("%s: command disabled", command->name);
				}

				/*
				 * If there is no built in command, or the
				 * built in command is overriden by the alias,
				 * then run the alias.
				 */
				else if ((alias_cnt == 1 && 
					  cmd_cnt == 1 && 
					  !strcmp(alias_name, command->name)) 
				      || (alias_cnt == 1 && 
					  cmd_cnt == 0))
				{
					call_user_alias(alias_name, alias,
							rest, arglist);
				}

				/*
				 * Otherwise, if the command is your nickname
				 * fake a /me command.
				 */
				else if (is_me(from_server, cline))
					mecmd(NULL, rest, empty_string);

				/*
				 * Kasi has asked me at least 6 times for this.
				 * If i just go ahead and add it, he will
				 * stop asking me.... :P
				 */
				else if (get_int_var(DISPATCH_UNKNOWN_COMMANDS_VAR))
					send_to_server("%s %s", cline, rest);

				/*
				 * If its not ambiguous,
				 */
				else if (alias_cnt + cmd_cnt > 1)
					say("Ambiguous command: %s", cline);

				/*
				 * Nothing to do but whine at the user.
				 */
				else
					say("Unknown command: %s", cline);
			}
			if (alias)
				new_free(&alias_name);
		}
	}
	if (old_display_var != get_int_var(DISPLAY_VAR))
		window_display = get_int_var(DISPLAY_VAR);
	else
		window_display = display;

	level--;
	unset_current_command();
        return 0;
}

/* 
 * How does this work?  Well, when we issue the /wait command
 * it increments a variable "waiting" which is the number of 
 * times wait has been called so far.  If we get a wait
 * token, we reduce the wait level by one, and if the wait
 * level is zero, then we are free to clear irc_io_loop which
 * will cause the waits to just fall out.
 */
int 	check_wait_command (char *nick)
{
	if ((waiting_out > waiting_in) && !strcmp(nick, lame_wait_nick))
	{
		waiting_in++;
		unlock_stack_frame();
	        return 1;
	}
	if (start_wait_list && !strcmp(nick, wait_nick))
	{
		WaitCmd *old = start_wait_list;

		start_wait_list = old->next;
		if (old->stuff)
		{
			parse_line("WAIT", old->stuff, empty_string, 0, 0);
			new_free(&old->stuff);
		}
		if (end_wait_list == old)
			end_wait_list = NULL;
		new_free((char **)&old);
		return 1;
	}
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

