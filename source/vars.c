/* $EPIC: vars.c,v 1.42 2003/10/10 06:09:01 jnelson Exp $ */
/*
 * vars.c: All the dealing of the irc variables are handled here. 
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright © 1993, 2003 EPIC Software Labs.
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
#include "alist.h"
#include "status.h"
#include "window.h"
#include "lastlog.h"
#include "log.h"
#include "hook.h"
#include "sedcrypt.h"
#include "history.h"
#include "notify.h"
#include "vars.h"
#include "input.h"
#include "ircaux.h"
#include "term.h"
#include "output.h"
#include "stack.h"
#include "dcc.h"
#include "keys.h"
#include "translat.h"
#include "timer.h"
#include "clock.h"
#include "mail.h"

/* IrcVariable: structure for each variable in the variable table */
typedef struct
{
	const char *	name;		/* what the user types */
	int		type;		/* variable types, see below */
	int		integer;	/* int value of variable */
	double		number;		/* decimal value of variable */
	char *		string;		/* string value of variable */
	void		(*func) (const void *); /* func called when var is set */
	char		int_flags;	/* internal flags to the variable */
	unsigned short	flags;		/* flags for this variable */
}	IrcVariable;

/*
 * The VIF_* macros stand for "(V)ariable.(i)nt_(f)lags", and have been
 * used for the various possible values of the int_flags data member.
 * The first two are, the third one is not.  The third one is used in
 * the 'flags' data member, but its based on the same idea.
 */
#define VIF_CHANGED	0x01	/* /set has been changed by user */
#define VIF_GLOBAL	0x02	/* /set was changed only by global */
#define VIF_PENDING	0x04	/* A /set is pending for this variable */

/* the types of IrcVariables */
#define BOOL_TYPE_VAR 0
#define CHAR_TYPE_VAR 1
#define INT_TYPE_VAR 2
#define STR_TYPE_VAR 3
#define FLOAT_TYPE_VAR 4

const char	*var_settings[] =
{
	"OFF", "ON", "TOGGLE"
};

static	void	eight_bit_characters 	(const void *);
static	void	set_realname 		(const void *);
static 	void 	set_display_pc_characters (const void *);
static 	void	set_dcc_timeout 	(const void *);
static	void	set_mangle_inbound 	(const void *);
static	void	set_mangle_outbound 	(const void *);
static	void	set_mangle_logfiles 	(const void *);
static	void	set_scroll 		(const void *);
static	void	update_all_status_wrapper (const void *);
static	void	set_highlight_char	(const void *);

/*
 * irc_variable: all the irc variables used.  Note that the integer and
 * boolean defaults are set here, which the string default value are set in
 * the init_variables() procedure 
 */
static	IrcVariable irc_variable[] =
{
	{ "ALLOW_C1_CHARS",		BOOL_TYPE_VAR,	DEFAULT_ALLOW_C1_CHARS, 0, NULL, NULL, 0, 0 },
	{ "ALT_CHARSET",		BOOL_TYPE_VAR,	DEFAULT_ALT_CHARSET, 0, NULL, NULL, 0, 0 },
	{ "ALWAYS_SPLIT_BIGGEST",	BOOL_TYPE_VAR,	DEFAULT_ALWAYS_SPLIT_BIGGEST, 0, NULL, NULL, 0, 0 },
	{ "AUTO_NEW_NICK",		BOOL_TYPE_VAR,	DEFAULT_AUTO_NEW_NICK, 0, NULL, NULL, 0, 0 },
        { "AUTO_RECONNECT",             BOOL_TYPE_VAR,  DEFAULT_AUTO_RECONNECT, 0, NULL, NULL, 0, 0 },
	{ "AUTO_RECONNECT_DELAY",	INT_TYPE_VAR,	DEFAULT_AUTO_RECONNECT_DELAY, 0, NULL, NULL, 0, 0 },
        { "AUTO_REJOIN",                BOOL_TYPE_VAR,  DEFAULT_AUTO_REJOIN, 0, NULL, NULL, 0, 0 },
	{ "AUTO_REJOIN_CONNECT",	BOOL_TYPE_VAR,	DEFAULT_AUTO_REJOIN_CONNECT, 0, NULL, NULL, 0, 0 },
	{ "AUTO_REJOIN_DELAY",		INT_TYPE_VAR,	DEFAULT_AUTO_REJOIN_DELAY, 0, NULL, NULL, 0, 0 },
	{ "AUTO_UNMARK_AWAY",		BOOL_TYPE_VAR,	DEFAULT_AUTO_UNMARK_AWAY, 0, NULL, NULL, 0, 0 },
	{ "AUTO_WHOWAS",		BOOL_TYPE_VAR,	DEFAULT_AUTO_WHOWAS, 0, NULL, NULL, 0, 0 },
	{ "BAD_STYLE",			BOOL_TYPE_VAR,	DEFAULT_BAD_STYLE, 0, NULL, NULL, 0, 0 },
	{ "BANNER",			STR_TYPE_VAR,	0, 0, NULL, NULL, 0, 0 },
	{ "BANNER_EXPAND",		BOOL_TYPE_VAR,	0, 0, NULL, NULL, 0, 0 },
	{ "BEEP",			BOOL_TYPE_VAR,	DEFAULT_BEEP, 0, NULL, NULL, 0, 0 },
	{ "BEEP_MAX",			INT_TYPE_VAR,	DEFAULT_BEEP_MAX, 0, NULL, NULL, 0, 0 },
	{ "BEEP_ON_MSG",		STR_TYPE_VAR,	0, 0, NULL, set_beep_on_msg, 0, 0 },
	{ "BEEP_WHEN_AWAY",		INT_TYPE_VAR,	DEFAULT_BEEP_WHEN_AWAY, 0, NULL, NULL, 0, 0 },
	{ "BLINK_VIDEO",		BOOL_TYPE_VAR,	DEFAULT_BLINK_VIDEO, 0, NULL, NULL, 0, 0 },
	{ "BOLD_VIDEO",			BOOL_TYPE_VAR,	DEFAULT_BOLD_VIDEO, 0, NULL, NULL, 0, 0 },
	{ "CHANNEL_NAME_WIDTH",		INT_TYPE_VAR,	DEFAULT_CHANNEL_NAME_WIDTH, 0, NULL, update_all_status_wrapper, 0, 0 },
	{ "CLIENT_INFORMATION",		STR_TYPE_VAR,	0, 0, NULL, NULL, 0, 0 },
	{ "CLOCK",			BOOL_TYPE_VAR,	DEFAULT_CLOCK, 0, NULL, set_clock, 0, 0 },
	{ "CLOCK_24HOUR",		BOOL_TYPE_VAR,	DEFAULT_CLOCK_24HOUR, 0, NULL, reset_clock, 0, 0 },
	{ "CLOCK_FORMAT",		STR_TYPE_VAR,	0, 0, NULL, set_clock_format, 0, 0 },
	{ "CLOCK_INTERVAL",		INT_TYPE_VAR,	DEFAULT_CLOCK_INTERVAL, 0, NULL, set_clock_interval, 0, 0 },
	{ "CMDCHARS",			STR_TYPE_VAR,	0, 0, NULL, NULL, 0, 0 },
	{ "COLOR",			BOOL_TYPE_VAR,	DEFAULT_COLOR, 0, NULL, NULL, 0, 0 },
	{ "COMMAND_MODE",		BOOL_TYPE_VAR,	DEFAULT_COMMAND_MODE, 0, NULL, NULL, 0, 0 },
	{ "COMMENT_HACK",		BOOL_TYPE_VAR,	DEFAULT_COMMENT_HACK, 0, NULL, NULL, 0, 0 },
	{ "CONNECT_TIMEOUT",		INT_TYPE_VAR,	DEFAULT_CONNECT_TIMEOUT, 0, NULL, NULL, 0, 0 },
	{ "CONTINUED_LINE",		STR_TYPE_VAR,	0, 0, NULL, set_continued_line, 0, 0 },
	{ "CPU_SAVER_AFTER",		INT_TYPE_VAR,	DEFAULT_CPU_SAVER_AFTER, 0, NULL, set_cpu_saver_after, 0, 0 },
	{ "CPU_SAVER_EVERY",		INT_TYPE_VAR,	DEFAULT_CPU_SAVER_EVERY, 0, NULL, set_cpu_saver_every, 0, 0 },
	{ "CURRENT_WINDOW_LEVEL",	STR_TYPE_VAR,	0, 0, NULL, set_current_window_level, 0, 0 },
	{ "DCC_AUTO_SEND_REJECTS",	BOOL_TYPE_VAR,	DEFAULT_DCC_AUTO_SEND_REJECTS, 0, NULL, NULL, 0, 0 },
	{ "DCC_LONG_PATHNAMES",		BOOL_TYPE_VAR,	DEFAULT_DCC_LONG_PATHNAMES, 0, NULL, NULL, 0, 0 },
	{ "DCC_SLIDING_WINDOW",		INT_TYPE_VAR,	DEFAULT_DCC_SLIDING_WINDOW, 0, NULL, NULL, 0, 0 },
	{ "DCC_STORE_PATH",		STR_TYPE_VAR,	0, 0, NULL, NULL, 0, 0 },
	{ "DCC_TIMEOUT",		INT_TYPE_VAR,	DEFAULT_DCC_TIMEOUT, 0, NULL, set_dcc_timeout, 0, 0 },
	{ "DCC_USE_GATEWAY_ADDR",	BOOL_TYPE_VAR,	0, 0, NULL, NULL, 0, 0 },
	{ "DEBUG",			INT_TYPE_VAR,	0, 0, NULL, NULL, 0, 0 },
	{ "DISPATCH_UNKNOWN_COMMANDS",	BOOL_TYPE_VAR,	DEFAULT_DISPATCH_UNKNOWN_COMMANDS, 0, NULL, NULL, 0, 0 },
	{ "DISPLAY",			BOOL_TYPE_VAR,	DEFAULT_DISPLAY, 0, NULL, NULL, 0, 0 },
	{ "DISPLAY_ANSI",		BOOL_TYPE_VAR,	DEFAULT_DISPLAY_ANSI, 0, NULL, NULL, 0, 0 },
	{ "DISPLAY_PC_CHARACTERS",	INT_TYPE_VAR,	DEFAULT_DISPLAY_PC_CHARACTERS, 0, NULL, set_display_pc_characters, 0, 0 },
	{ "DO_NOTIFY_IMMEDIATELY",	BOOL_TYPE_VAR,	DEFAULT_DO_NOTIFY_IMMEDIATELY, 0, NULL, NULL, 0, 0 },
	{ "EIGHT_BIT_CHARACTERS",	BOOL_TYPE_VAR,	DEFAULT_EIGHT_BIT_CHARACTERS, 0, NULL, eight_bit_characters, 0, 0 },
	{ "FLOATING_POINT_MATH",	BOOL_TYPE_VAR,	DEFAULT_FLOATING_POINT_MATH, 0, NULL, NULL, 0, 0 },
	{ "FLOATING_POINT_PRECISION",	INT_TYPE_VAR,	DEFAULT_FLOATING_POINT_PRECISION, 0, NULL, NULL, 0, 0 },
	{ "FLOOD_AFTER",		INT_TYPE_VAR,	DEFAULT_FLOOD_AFTER, 0, NULL, NULL, 0, 0 },
	{ "FLOOD_IGNORE",		BOOL_TYPE_VAR,	DEFAULT_FLOOD_IGNORE, 0, NULL, NULL, 0, 0 },
	{ "FLOOD_MASKUSER",		INT_TYPE_VAR,	DEFAULT_FLOOD_MASKUSER, 0, NULL, NULL, 0, 0 },
	{ "FLOOD_RATE",			INT_TYPE_VAR,	DEFAULT_FLOOD_RATE, 0, NULL, NULL, 0, 0 },
	{ "FLOOD_RATE_PER",		INT_TYPE_VAR,	DEFAULT_FLOOD_RATE_PER, 0, NULL, NULL, 0, 0 },
	{ "FLOOD_USERS",		INT_TYPE_VAR,	DEFAULT_FLOOD_USERS, 0, NULL, NULL, 0, 0 },
	{ "FLOOD_WARNING",		BOOL_TYPE_VAR,	DEFAULT_FLOOD_WARNING, 0, NULL, NULL, 0, 0 },
	{ "FULL_STATUS_LINE",		BOOL_TYPE_VAR,	DEFAULT_FULL_STATUS_LINE, 0, NULL, update_all_status_wrapper, 0, 0 },
	{ "HELP_PAGER",			BOOL_TYPE_VAR,	DEFAULT_HELP_PAGER, 0, NULL, NULL, 0, 0 },
	{ "HELP_PATH",			STR_TYPE_VAR,	0, 0, NULL, NULL, 0, 0 },
	{ "HELP_PROMPT",		BOOL_TYPE_VAR,	DEFAULT_HELP_PROMPT, 0, NULL, NULL, 0, 0 },
	{ "HELP_WINDOW",		BOOL_TYPE_VAR,	DEFAULT_HELP_WINDOW, 0, NULL, NULL, 0, 0 },
	{ "HIDE_PRIVATE_CHANNELS",	BOOL_TYPE_VAR,	DEFAULT_HIDE_PRIVATE_CHANNELS, 0, NULL, update_all_status_wrapper, 0, 0 },
	{ "HIGHLIGHT_CHAR",		STR_TYPE_VAR,	0, 0, NULL, set_highlight_char, 0, 0 },
	{ "HIGH_BIT_ESCAPE",		INT_TYPE_VAR,	DEFAULT_HIGH_BIT_ESCAPE, 0, NULL, set_meta_8bit, 0, 0 },
	{ "HISTORY",			INT_TYPE_VAR,	DEFAULT_HISTORY, 0, NULL, set_history_size, 0, 0 },
	{ "HISTORY_CIRCLEQ",		BOOL_TYPE_VAR,	DEFAULT_HISTORY_CIRCLEQ, 0, NULL, NULL, 0, 0 },
	{ "HOLD_SLIDER",		INT_TYPE_VAR,	DEFAULT_HOLD_SLIDER, 0, NULL, NULL, 0, 0 },
	{ "INDENT",			BOOL_TYPE_VAR,	DEFAULT_INDENT, 0, NULL, NULL, 0, 0 },
	{ "INPUT_ALIASES",		BOOL_TYPE_VAR,	DEFAULT_INPUT_ALIASES, 0, NULL, NULL, 0, 0 },
	{ "INPUT_PROMPT",		STR_TYPE_VAR,	0, 0, NULL, set_input_prompt, 0, 0 },
	{ "INSERT_MODE",		BOOL_TYPE_VAR,	DEFAULT_INSERT_MODE, 0, NULL, update_all_status_wrapper, 0, 0 },
	{ "INVERSE_VIDEO",		BOOL_TYPE_VAR,	DEFAULT_INVERSE_VIDEO, 0, NULL, NULL, 0, 0 },
	{ "KEY_INTERVAL",		INT_TYPE_VAR,	DEFAULT_KEY_INTERVAL, 0, NULL, set_key_interval, 0, 0 },
	{ "LASTLOG",			INT_TYPE_VAR,	DEFAULT_LASTLOG, 0, NULL, set_lastlog_size, 0, 0 },
	{ "LASTLOG_LEVEL",		STR_TYPE_VAR,	0, 0, NULL, set_lastlog_level, 0, 0 },
	{ "LOAD_PATH",			STR_TYPE_VAR,	0, 0, NULL, NULL, 0, 0 },
	{ "LOG",			BOOL_TYPE_VAR,	DEFAULT_LOG, 0, NULL, logger, 0, 0 },
	{ "LOGFILE",			STR_TYPE_VAR,	0, 0, NULL, NULL, 0, 0 },
	{ "LOG_REWRITE",		STR_TYPE_VAR,	0, 0, NULL, NULL, 0, 0 },
	{ "MAIL",			INT_TYPE_VAR,	DEFAULT_MAIL, 0, NULL, set_mail, 0, 0 },
	{ "MAIL_INTERVAL",		INT_TYPE_VAR,	DEFAULT_MAIL_INTERVAL, 0, NULL, set_mail_interval, 0, 0 },
	{ "MANGLE_INBOUND",		STR_TYPE_VAR,	0, 0, NULL, set_mangle_inbound, 0, 0 },
	{ "MANGLE_LOGFILES",		STR_TYPE_VAR,	0, 0, NULL, set_mangle_logfiles, 0, 0 },
	{ "MANGLE_OUTBOUND",		STR_TYPE_VAR,	0, 0, NULL, set_mangle_outbound, 0, 0 },
	{ "MAX_RECONNECTS",		INT_TYPE_VAR,	DEFAULT_MAX_RECONNECTS, 0, NULL, NULL, 0, 0 },
	{ "METRIC_TIME",		BOOL_TYPE_VAR,	DEFAULT_METRIC_TIME, 0, NULL, reset_clock, 0, 0 },
	{ "MIRC_BROKEN_DCC_RESUME",	BOOL_TYPE_VAR,	0, 0, NULL, NULL, 0, 0 },
        { "MODE_STRIPPER",              BOOL_TYPE_VAR,  DEFAULT_MODE_STRIPPER, 0, NULL, NULL, 0, 0 },
	{ "ND_SPACE_MAX",		INT_TYPE_VAR,	DEFAULT_ND_SPACE_MAX, 0, NULL, NULL, 0, 0 },
	{ "NEW_SERVER_LASTLOG_LEVEL",	STR_TYPE_VAR,	0, 0, NULL, set_new_server_lastlog_level, 0, 0 },
	{ "NOTIFY",			BOOL_TYPE_VAR,	DEFAULT_NOTIFY, 0, NULL, set_notify, 0, 0 },
	{ "NOTIFY_INTERVAL",		INT_TYPE_VAR,	DEFAULT_NOTIFY_INTERVAL, 0, NULL, set_notify_interval, 0, 0 },
	{ "NOTIFY_LEVEL",		STR_TYPE_VAR,	0, 0, NULL, set_notify_level, 0, 0 },
	{ "NOTIFY_ON_TERMINATION",	BOOL_TYPE_VAR,	DEFAULT_NOTIFY_ON_TERMINATION, 0, NULL, NULL, 0, 0 },
	{ "NOTIFY_USERHOST_AUTOMATIC",	BOOL_TYPE_VAR,	DEFAULT_NOTIFY_USERHOST_AUTOMATIC, 0, NULL, NULL, 0, 0 },
	{ "NO_CONTROL_LOG",		BOOL_TYPE_VAR,	DEFAULT_NO_CONTROL_LOG, 0, NULL, NULL, 0, 0 },
	{ "NO_CTCP_FLOOD",		BOOL_TYPE_VAR,	DEFAULT_NO_CTCP_FLOOD, 0, NULL, NULL, 0, 0 },
	{ "NO_FAIL_DISCONNECT",		BOOL_TYPE_VAR,	DEFAULT_NO_FAIL_DISCONNECT, 0, NULL, NULL, 0, 0 },
	{ "NUM_OF_WHOWAS",		INT_TYPE_VAR,	DEFAULT_NUM_OF_WHOWAS, 0, NULL, NULL, 0, 0 },
	{ "OUTPUT_REWRITE",		STR_TYPE_VAR,	0, 0, NULL, NULL, 0, 0 },
	{ "PAD_CHAR",			CHAR_TYPE_VAR,	DEFAULT_PAD_CHAR, 0, NULL, NULL, 0, 0 },
	{ "QUIT_MESSAGE",		STR_TYPE_VAR,   0, 0, NULL, NULL, 0, 0 },
	{ "RANDOM_SOURCE",		INT_TYPE_VAR,	DEFAULT_RANDOM_SOURCE, 0, NULL, NULL, 0, 0 },
	{ "REALNAME",			STR_TYPE_VAR,	0, 0, NULL, set_realname, 0, 0 },
	{ "REVERSE_STATUS_LINE",	BOOL_TYPE_VAR,	DEFAULT_REVERSE_STATUS_LINE, 0, NULL, update_all_status_wrapper, 0, 0 },
	{ "SCREEN_OPTIONS",             STR_TYPE_VAR,   0, 0, NULL, NULL, 0, 0 },
	{ "SCROLL",			BOOL_TYPE_VAR,	1, 0, NULL, set_scroll, 0, 0 },
	{ "SCROLLBACK",			INT_TYPE_VAR,	DEFAULT_SCROLLBACK, 0, NULL, set_scrollback_size, 0, 0 },
	{ "SCROLLBACK_RATIO",		INT_TYPE_VAR,	DEFAULT_SCROLLBACK_RATIO, 0, NULL, NULL, 0, 0 },
	{ "SCROLL_LINES",		INT_TYPE_VAR,	DEFAULT_SCROLL_LINES, 0, NULL, set_scroll_lines, 0, 0 },
	{ "SECURITY",			INT_TYPE_VAR,	DEFAULT_SECURITY, 0, NULL, NULL, 0, 0 },
	{ "SHELL",			STR_TYPE_VAR,	0, 0, NULL, NULL, 0, 0 },
	{ "SHELL_FLAGS",		STR_TYPE_VAR,	0, 0, NULL, NULL, 0, 0 },
	{ "SHELL_LIMIT",		INT_TYPE_VAR,	DEFAULT_SHELL_LIMIT, 0, NULL, NULL, 0, 0 },
	{ "SHOW_CHANNEL_NAMES",		BOOL_TYPE_VAR,	DEFAULT_SHOW_CHANNEL_NAMES, 0, NULL, NULL, 0, 0 },
	{ "SHOW_END_OF_MSGS",		BOOL_TYPE_VAR,	DEFAULT_SHOW_END_OF_MSGS, 0, NULL, NULL, 0, 0 },
	{ "SHOW_NUMERICS",		BOOL_TYPE_VAR,	DEFAULT_SHOW_NUMERICS, 0, NULL, NULL, 0, 0 },
	{ "SHOW_STATUS_ALL",		BOOL_TYPE_VAR,	DEFAULT_SHOW_STATUS_ALL, 0, NULL, update_all_status_wrapper, 0, 0 },
	{ "SHOW_WHO_HOPCOUNT", 		BOOL_TYPE_VAR,	DEFAULT_SHOW_WHO_HOPCOUNT, 0, NULL, NULL, 0, 0 },
	{ "SSL_CERTFILE",		STR_TYPE_VAR,	0, 0, NULL, NULL, 0, 0 },
	{ "SSL_KEYFILE",		STR_TYPE_VAR,	0, 0, NULL, NULL, 0, 0 },
	{ "SSL_PATH",			STR_TYPE_VAR,	0, 0, NULL, NULL, 0, 0 },
	{ "STATUS_AWAY",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_CHANNEL",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_CHANOP",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_CLOCK",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_CPU_SAVER",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_DOES_EXPANDOS",	BOOL_TYPE_VAR,	0, 0, NULL, NULL, 0, 0 },
	{ "STATUS_FORMAT",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_FORMAT1",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_FORMAT2",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_HALFOP",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_HOLD",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_HOLD_LINES",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_INSERT",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_MAIL",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_MODE",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_NICKNAME",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_NOTIFY",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
        { "STATUS_NO_REPEAT",           BOOL_TYPE_VAR,  DEFAULT_STATUS_NO_REPEAT, 0, NULL, build_status, 0, 0 },
	{ "STATUS_OPER",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_OVERWRITE",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_QUERY",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_SCROLLBACK",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_SERVER",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_SSL_OFF",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_SSL_ON",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_TRUNCATE_RHS",	BOOL_TYPE_VAR,	DEFAULT_STATUS_TRUNCATE_RHS, 0, NULL, build_status, 0, 0 },
	{ "STATUS_UMODE",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_USER",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_USER1",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_USER10",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_USER11",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_USER12",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_USER13",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_USER14",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_USER15",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_USER16",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_USER17",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_USER18",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_USER19",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },	
	{ "STATUS_USER2",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_USER20",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_USER21",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_USER22",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_USER23",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_USER24",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_USER25",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_USER26",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_USER27",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_USER28",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_USER29",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },	
	{ "STATUS_USER3",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_USER30",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_USER31",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_USER32",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_USER33",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_USER34",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_USER35",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_USER36",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_USER37",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_USER38",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_USER39",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },	
	{ "STATUS_USER4",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_USER5",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_USER6",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_USER7",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_USER8",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_USER9",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },	
	{ "STATUS_VOICE",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
	{ "STATUS_WINDOW",		STR_TYPE_VAR,	0, 0, NULL, build_status, 0, 0 },
        { "SUPPRESS_FROM_REMOTE_SERVER",BOOL_TYPE_VAR,  DEFAULT_SUPPRESS_FROM_REMOTE_SERVER, 0, NULL, NULL, 0, 0},
	{ "SWITCH_CHANNELS_BETWEEN_WINDOWS",	BOOL_TYPE_VAR,	DEFAULT_SWITCH_CHANNELS_BETWEEN_WINDOWS, 0, NULL, NULL, 0, 0 },
	{ "SWITCH_CHANNEL_ON_PART",	BOOL_TYPE_VAR,	DEFAULT_SWITCH_CHANNEL_ON_PART, 0, NULL, NULL, 0, 0 },
	{ "TAB",			BOOL_TYPE_VAR,	DEFAULT_TAB, 0, NULL, NULL, 0, 0 },
	{ "TAB_MAX",			INT_TYPE_VAR,	DEFAULT_TAB_MAX, 0, NULL, NULL, 0, 0 },
	{ "TERM_DOES_BRIGHT_BLINK",	BOOL_TYPE_VAR,	DEFAULT_TERM_DOES_BRIGHT_BLINK, 0, NULL, NULL, 0, 0 },
	{ "TRANSLATION",		STR_TYPE_VAR,	0, 0, NULL, set_translation, 0, 0 },
	{ "TRANSLATION_PATH",		STR_TYPE_VAR,	0, 0, NULL, NULL, 0, 0 },
	{ "UNDERLINE_VIDEO",		BOOL_TYPE_VAR,	DEFAULT_UNDERLINE_VIDEO, 0, NULL, NULL, 0, 0 },
	{ "USER_INFORMATION", 		STR_TYPE_VAR,	0, 0, NULL, NULL, 0, 0 },
	{ "VERBOSE_CTCP",		BOOL_TYPE_VAR,	DEFAULT_VERBOSE_CTCP, 0, NULL, NULL, 0, 0 },
	{ "WORD_BREAK",			STR_TYPE_VAR,	0, 0, NULL, NULL, 0, 0 },
	{ "WSERV_PATH",			STR_TYPE_VAR,	0, 0, NULL, NULL, 0, 0 },
	{ "XTERM",			STR_TYPE_VAR,	0, 0, NULL, NULL, 0, 0 },
	{ "XTERM_OPTIONS", 		STR_TYPE_VAR,	0, 0, NULL, NULL, 0, 0 },
	{ (char *) 0, 0, 0, 0, 0, 0, 0, 0 }
};

/*
 * init_variables: initializes the string variables that can't really be
 * initialized properly above 
 */
void 	init_variables (void)
{
	char 	*s;
	int 	i;

	for (i = 1; i < NUMBER_OF_VARIABLES - 1; i++)
		if (strcmp(irc_variable[i-1].name, irc_variable[i].name) >= 0)
			panic("Variable [%d] (%s) is out of order.", i, irc_variable[i].name);

	set_string_var(BANNER_VAR, DEFAULT_BANNER);
	set_string_var(BEEP_ON_MSG_VAR, DEFAULT_BEEP_ON_MSG);
	set_string_var(CMDCHARS_VAR, DEFAULT_CMDCHARS);
	set_string_var(CURRENT_WINDOW_LEVEL_VAR, DEFAULT_CURRENT_WINDOW_LEVEL);
	set_string_var(LOGFILE_VAR, DEFAULT_LOGFILE);
	set_string_var(SHELL_VAR, DEFAULT_SHELL);
	set_string_var(SHELL_FLAGS_VAR, DEFAULT_SHELL_FLAGS);
	set_string_var(CONTINUED_LINE_VAR, DEFAULT_CONTINUED_LINE);
	set_string_var(INPUT_PROMPT_VAR, DEFAULT_INPUT_PROMPT);
	set_string_var(HIGHLIGHT_CHAR_VAR, DEFAULT_HIGHLIGHT_CHAR);
	set_string_var(LASTLOG_LEVEL_VAR, DEFAULT_LASTLOG_LEVEL);
	set_string_var(LOG_REWRITE_VAR, NULL);
	set_string_var(MANGLE_INBOUND_VAR, NULL);
	set_string_var(MANGLE_LOGFILES_VAR, NULL);
	set_string_var(MANGLE_OUTBOUND_VAR, NULL);
	set_string_var(NEW_SERVER_LASTLOG_LEVEL_VAR, 
			DEFAULT_NEW_SERVER_LASTLOG_LEVEL);
	set_string_var(NOTIFY_LEVEL_VAR, DEFAULT_NOTIFY_LEVEL);
	set_string_var(OUTPUT_REWRITE_VAR, NULL);
	set_string_var(QUIT_MESSAGE_VAR, DEFAULT_QUIT_MESSAGE);
	set_string_var(REALNAME_VAR, realname);
	set_string_var(SSL_CERTFILE_VAR, NULL);
	set_string_var(SSL_KEYFILE_VAR, NULL);
	set_string_var(SSL_PATH_VAR, NULL);
	set_string_var(STATUS_FORMAT_VAR, DEFAULT_STATUS_FORMAT);
	set_string_var(STATUS_FORMAT1_VAR, DEFAULT_STATUS_FORMAT1);
	set_string_var(STATUS_FORMAT2_VAR, DEFAULT_STATUS_FORMAT2);
	set_string_var(STATUS_AWAY_VAR, DEFAULT_STATUS_AWAY);
	set_string_var(STATUS_CHANNEL_VAR, DEFAULT_STATUS_CHANNEL);
	set_string_var(STATUS_CHANOP_VAR, DEFAULT_STATUS_CHANOP);
	set_string_var(STATUS_HALFOP_VAR, DEFAULT_STATUS_HALFOP);
	set_string_var(STATUS_SSL_ON_VAR, DEFAULT_STATUS_SSL_ON);
	set_string_var(STATUS_SSL_OFF_VAR, DEFAULT_STATUS_SSL_OFF);
	set_string_var(STATUS_CLOCK_VAR, DEFAULT_STATUS_CLOCK);
	set_string_var(STATUS_CPU_SAVER_VAR, DEFAULT_STATUS_CPU_SAVER);
	set_string_var(STATUS_HOLD_VAR, DEFAULT_STATUS_HOLD);
	set_string_var(STATUS_HOLD_LINES_VAR, DEFAULT_STATUS_HOLD_LINES);
	set_string_var(STATUS_INSERT_VAR, DEFAULT_STATUS_INSERT);
	set_string_var(STATUS_MAIL_VAR, DEFAULT_STATUS_MAIL);
	set_string_var(STATUS_MODE_VAR, DEFAULT_STATUS_MODE);
	set_string_var(STATUS_NICK_VAR, DEFAULT_STATUS_NICK);
	set_string_var(STATUS_OPER_VAR, DEFAULT_STATUS_OPER);
	set_string_var(STATUS_OVERWRITE_VAR, DEFAULT_STATUS_OVERWRITE);
	set_string_var(STATUS_QUERY_VAR, DEFAULT_STATUS_QUERY);
	set_string_var(STATUS_SCROLLBACK_VAR, DEFAULT_STATUS_SCROLLBACK);
	set_string_var(STATUS_SERVER_VAR, DEFAULT_STATUS_SERVER);
	set_string_var(STATUS_UMODE_VAR, DEFAULT_STATUS_UMODE);
	set_string_var(STATUS_USER0_VAR, DEFAULT_STATUS_USER);
	set_string_var(STATUS_USER1_VAR, DEFAULT_STATUS_USER1);
	set_string_var(STATUS_USER2_VAR, DEFAULT_STATUS_USER2);
	set_string_var(STATUS_USER3_VAR, DEFAULT_STATUS_USER3);
	set_string_var(STATUS_USER4_VAR, DEFAULT_STATUS_USER4);
	set_string_var(STATUS_USER5_VAR, DEFAULT_STATUS_USER5);
	set_string_var(STATUS_USER6_VAR, DEFAULT_STATUS_USER6);
	set_string_var(STATUS_USER7_VAR, DEFAULT_STATUS_USER7);
	set_string_var(STATUS_USER8_VAR, DEFAULT_STATUS_USER8);
	set_string_var(STATUS_USER9_VAR, DEFAULT_STATUS_USER9);
	set_string_var(STATUS_USER10_VAR, DEFAULT_STATUS_USER10);
	set_string_var(STATUS_USER11_VAR, DEFAULT_STATUS_USER11);
	set_string_var(STATUS_USER12_VAR, DEFAULT_STATUS_USER12);
	set_string_var(STATUS_USER13_VAR, DEFAULT_STATUS_USER13);
	set_string_var(STATUS_USER14_VAR, DEFAULT_STATUS_USER14);
	set_string_var(STATUS_USER15_VAR, DEFAULT_STATUS_USER15);
	set_string_var(STATUS_USER16_VAR, DEFAULT_STATUS_USER16);
	set_string_var(STATUS_USER17_VAR, DEFAULT_STATUS_USER17);
	set_string_var(STATUS_USER18_VAR, DEFAULT_STATUS_USER18);
	set_string_var(STATUS_USER19_VAR, DEFAULT_STATUS_USER19);
	set_string_var(STATUS_USER20_VAR, DEFAULT_STATUS_USER20);
	set_string_var(STATUS_USER21_VAR, DEFAULT_STATUS_USER21);
	set_string_var(STATUS_USER22_VAR, DEFAULT_STATUS_USER22);
	set_string_var(STATUS_USER23_VAR, DEFAULT_STATUS_USER23);
	set_string_var(STATUS_USER24_VAR, DEFAULT_STATUS_USER24);
	set_string_var(STATUS_USER25_VAR, DEFAULT_STATUS_USER25);
	set_string_var(STATUS_USER26_VAR, DEFAULT_STATUS_USER26);
	set_string_var(STATUS_USER27_VAR, DEFAULT_STATUS_USER27);
	set_string_var(STATUS_USER28_VAR, DEFAULT_STATUS_USER28);
	set_string_var(STATUS_USER29_VAR, DEFAULT_STATUS_USER29);
	set_string_var(STATUS_USER30_VAR, DEFAULT_STATUS_USER30);
	set_string_var(STATUS_USER31_VAR, DEFAULT_STATUS_USER31);
	set_string_var(STATUS_USER32_VAR, DEFAULT_STATUS_USER32);
	set_string_var(STATUS_USER33_VAR, DEFAULT_STATUS_USER33);
	set_string_var(STATUS_USER34_VAR, DEFAULT_STATUS_USER34);
	set_string_var(STATUS_USER35_VAR, DEFAULT_STATUS_USER35);
	set_string_var(STATUS_USER36_VAR, DEFAULT_STATUS_USER36);
	set_string_var(STATUS_USER37_VAR, DEFAULT_STATUS_USER37);
	set_string_var(STATUS_USER38_VAR, DEFAULT_STATUS_USER38);
	set_string_var(STATUS_USER39_VAR, DEFAULT_STATUS_USER39);
	set_string_var(STATUS_VOICE_VAR, DEFAULT_STATUS_VOICE);
	set_string_var(STATUS_WINDOW_VAR, DEFAULT_STATUS_WINDOW);
	set_string_var(TRANSLATION_VAR, NULL);
	set_string_var(USERINFO_VAR, DEFAULT_USERINFO);
	set_string_var(XTERM_VAR, DEFAULT_XTERM);
	set_string_var(XTERM_OPTIONS_VAR, DEFAULT_XTERM_OPTIONS);
	set_string_var(STATUS_NOTIFY_VAR, DEFAULT_STATUS_NOTIFY);
	set_string_var(CLIENTINFO_VAR, IRCII_COMMENT);
	set_string_var(WORD_BREAK_VAR, DEFAULT_WORD_BREAK);
	set_string_var(WSERV_PATH_VAR, WSERV_PATH);

	/*
	 * Construct the default help path
	 */
	s = malloc_strdup(irc_lib);
	malloc_strcat(&s, "/help");
	set_string_var(HELP_PATH_VAR, s);
	new_free(&s);

	/*
	 * Forcibly init all the variables
	 */
	for (i = 0; i < NUMBER_OF_VARIABLES; i++)
	{
		IrcVariable *var = &irc_variable[i];

		if (var->func)
		{
			if (var->func == build_status)
				continue;
			if (var->func == update_all_status_wrapper)
				continue;

			var->flags |= VIF_PENDING;
			switch (var->type)
			{
				case (BOOL_TYPE_VAR):
				case (INT_TYPE_VAR):
				case (CHAR_TYPE_VAR):
					var->func(&var->integer);
					break;
				case (FLOAT_TYPE_VAR):
					var->func(&var->number);
					break;
				case (STR_TYPE_VAR):
					var->func(var->string);
					break;
			}
			var->flags &= ~VIF_PENDING;
		}
	}
}

/*
 * do_boolean: just a handy thing.  Returns 1 if the str is not ON, OFF, or
 * TOGGLE 
 */
int 	do_boolean (char *str, int *value)
{
	upper(str);
	if (strcmp(str, var_settings[ON]) == 0)
		*value = 1;
	else if (strcmp(str, var_settings[OFF]) == 0)
		*value = 0;
	else if (strcmp(str, "TOGGLE") == 0)
	{
		if (*value)
			*value = 0;
		else
			*value = 1;
	}
	else
		return (1);
	return (0);
}

/*
 * get_variable_index: converts a string into an offset into the set table.
 * Returns NUMBER_OF_VARIABLES if varname doesn't exist.
 */
static enum VAR_TYPES get_variable_index (const char *varname)
{
	enum VAR_TYPES	retval;
	int	cnt;

	find_fixed_array_item(irc_variable, sizeof(IrcVariable), 
				NUMBER_OF_VARIABLES, varname, &cnt, 
				(int *)&retval);

	if (cnt < 0)
		return retval;

	return NUMBER_OF_VARIABLES;
}

/*
 * set_var_value: Given the variable structure and the string representation
 * of the value, this sets the value in the most verbose and error checking
 * of manors.  It displays the results of the set and executes the function
 * defined in the var structure 
 */
void 	set_var_value (int svv_index, char *value)
{
	char	*rest;
	IrcVariable *var;
	int	old;

	var = &(irc_variable[svv_index]);
	switch (var->type)
	{
	case BOOL_TYPE_VAR:
	{
		if (value && *value && (value = next_arg(value, &rest)))
		{
			old = var->integer;
			if (do_boolean(value, &(var->integer)))
			{
				say("Value must be either ON, OFF, or TOGGLE");
				break;
			}
			if (!(var->int_flags & VIF_CHANGED))
			{
				if (old != var->integer)
					var->int_flags |= VIF_CHANGED;
			}
			if (loading_global)
				var->int_flags |= VIF_GLOBAL;
			if (var->func)
				(var->func) (&var->integer);
			say("Value of %s set to %s", var->name,
				var->integer ? var_settings[ON]
					     : var_settings[OFF]);
		}
		else
			say("Current value of %s is %s", var->name,
				(var->integer) ?
				var_settings[ON] : var_settings[OFF]);
		break;
	}
	case CHAR_TYPE_VAR:
	{
		if (!value)
		{
			if (!(var->int_flags & VIF_CHANGED))
			{
				if (var->integer)
					var->int_flags |= VIF_CHANGED;
			}
			if (loading_global)
				var->int_flags |= VIF_GLOBAL;
			var->integer = ' ';
			if (var->func)
				(var->func) (&var->integer);
			say("Value of %s set to '%c'", var->name, var->integer);
		}


		else if (value && *value && (value = next_arg(value, &rest)))
		{
			if (strlen(value) > 1)
				say("Value of %s must be a single character",
					var->name);
			else
			{
				if (!(var->int_flags & VIF_CHANGED))
				{
					if (var->integer != *value)
						var->int_flags |= VIF_CHANGED;
				}
				if (loading_global)
					var->int_flags |= VIF_GLOBAL;
				var->integer = *value;
				if (var->func)
					(var->func) (&var->integer);
				say("Value of %s set to '%c'", var->name,
					var->integer);
			}
		}
		else
			say("Current value of %s is '%c'", var->name,
				var->integer);
		break;
	}
	case INT_TYPE_VAR:
	{
		if (value && *value && (value = next_arg(value, &rest)))
		{
			int	val;

			if (!is_number(value))
			{
				say("Value of %s must be numeric!", var->name);
				break;
			}
			if ((val = my_atol(value)) < 0)
			{
				say("Value of %s must be a non-negative number", var->name);
				break;
			}
			if (!(var->int_flags & VIF_CHANGED))
			{
				if (var->integer != val)
					var->int_flags |= VIF_CHANGED;
			}
			if (loading_global)
				var->int_flags |= VIF_GLOBAL;
			var->integer = val;
			if (var->func)
				(var->func) (&var->integer);
			say("Value of %s set to %d", var->name, var->integer);
		}
		else
			say("Current value of %s is %d", var->name, var->integer);
		break;
	}
	case FLOAT_TYPE_VAR:
	{
		if (value && *value && (value = next_arg(value, &rest)))
		{
			int	val;

			if (!is_real_number(value))
			{
				say("Value of %s must be numeric!", var->name);
				break;
			}
			val = atof(value);
			if (!(var->int_flags & VIF_CHANGED))
			{
				if (var->number != val)
					var->int_flags |= VIF_CHANGED;
			}
			if (loading_global)
				var->int_flags |= VIF_GLOBAL;
			var->number = val;
			if (var->func)
				(var->func) (&var->number);
			say("Value of %s set to %f", var->name, var->number);
		}
		else
			say("Current value of %s is %f", var->name, var->number);
		break;
	}
	case STR_TYPE_VAR:
	{
		if (value)
		{
			if (*value)
			{
				if ((!var->int_flags & VIF_CHANGED))
				{
					if ((var->string && !value) ||
					    (!var->string && value) ||
					    my_stricmp(var->string, value))
						var->int_flags |= VIF_CHANGED;
				}
				if (loading_global)
					var->int_flags |= VIF_GLOBAL;
				malloc_strcpy(&(var->string), value);
			}
			else
			{
				if (var->string)
					say("Current value of %s is %s",
						var->name, var->string);
				else
					say("No value for %s has been set",
						var->name);
				return;
			}
		}
		else
			new_free(&(var->string));

		if (var->func && !(var->int_flags & VIF_PENDING))
		{
			var->int_flags |= VIF_PENDING;
			(var->func) (var->string);
			var->int_flags &= ~VIF_PENDING;
		}

		say("Value of %s set to %s", var->name, var->string ?
			var->string : "<EMPTY>");
		break;
	}
	}
}

/*
 * set_variable: The SET command sets one of the irc variables.  The args
 * should consist of "variable-name setting", where variable name can be
 * partial, but non-ambbiguous, and setting depends on the variable being set 
 */
BUILT_IN_COMMAND(setcmd)
{
	char	*var = NULL;
	int	cnt;
enum VAR_TYPES	sv_index;
	int	hook = 0;

	/*
	 * XXX Ugh.  This is a hideous offense of good taste which is
	 * necessary to support set's abominable syntax, particularly
	 * acute with /set continued_line<space><space>
	 */
	while (args && *args && isspace(*args))
		args++;
	var = args;
	while (args && *args && !isspace(*args))
		args++;
	if (args && *args)
		*args++ = 0;

	if (var && *var)
	{
		if (*var == '-')
		{
			var++;
			args = (char *) 0;
		}

		/* Exact match? */
		upper(var);
		find_fixed_array_item (irc_variable, sizeof(IrcVariable), NUMBER_OF_VARIABLES, var, &cnt, (int *)&sv_index);

		if (cnt == 1)
			cnt = -1;

		if ((cnt >= 0) || !(irc_variable[sv_index].int_flags & VIF_PENDING))
			hook = 1;

		if (cnt < 0)
			irc_variable[sv_index].int_flags |= VIF_PENDING;

		if (hook)
		{
			hook = do_hook(SET_LIST, "%s %s", 
				var, args ? args : "<unset>");

			if (hook && (cnt < 0))
			{
				hook = do_hook(SET_LIST, "%s %s",
					irc_variable[sv_index].name, 
					args ? args : "<unset>");
			}
		}

		if (cnt < 0)
			irc_variable[sv_index].int_flags &= ~VIF_PENDING;

		if (hook)
		{
			if (cnt < 0)
				set_var_value(sv_index, args);
			else if (cnt == 0)
			{
				if (do_hook(SET_LIST, "set-error No such variable \"%s\"", var))
					say("No such variable \"%s\"", var);
			}
			else
			{
				if (do_hook(SET_LIST, "set-error %s is ambiguous", var))
				{
					say("%s is ambiguous", var);
					for (cnt += sv_index; (int)sv_index < cnt; sv_index = (enum VAR_TYPES)(sv_index + 1))
						set_var_value(sv_index, empty_string);
				}
			}
		}
	}
	else
        {
		int var_index;
		for (var_index = 0; var_index < NUMBER_OF_VARIABLES; var_index++)
			set_var_value(var_index, empty_string);
        }
}

/*
 * get_string_var: returns the value of the string variable given as an index
 * into the variable table.  Does no checking of variable types, etc 
 */
char *	get_string_var (enum VAR_TYPES var)
{
	return (irc_variable[var].string);
}

/*
 * get_int_var: returns the value of the integer string given as an index
 * into the variable table.  Does no checking of variable types, etc 
 */
int 	get_int_var (enum VAR_TYPES var)
{
	return (irc_variable[var].integer);
}

/*
 * get_int_var: returns the value of the integer string given as an index
 * into the variable table.  Does no checking of variable types, etc 
 */
double 	get_float_var (enum VAR_TYPES var)
{
	return (irc_variable[var].number);
}

/*
 * set_string_var: sets the string variable given as an index into the
 * variable table to the given string.  If string is null, the current value
 * of the string variable is freed and set to null 
 */
void 	set_string_var (enum VAR_TYPES var, const char *string)
{
	if (string)
		malloc_strcpy(&(irc_variable[var].string), string);
	else
		new_free(&(irc_variable[var].string));
}

/* Same story, second verse. */
void 	set_int_var (enum VAR_TYPES var, int value)
{
	irc_variable[var].integer = value;
}

/* Same story, second verse. */
void 	set_float_var (enum VAR_TYPES var, double value)
{
	irc_variable[var].number = value;
}

/*
 * save_variables: this writes all of the IRCII variables to the given FILE
 * pointer in such a way that they can be loaded in using LOAD or the -l switch 
 */
void 	save_variables (FILE *fp, int do_all)
{
	IrcVariable *var;

	for (var = irc_variable; var->name; var++)
	{
		if (!(var->int_flags & VIF_CHANGED))
			continue;
		if (do_all || !(var->int_flags & VIF_GLOBAL))
		{
			if (strcmp(var->name, "DISPLAY") == 0 || strcmp(var->name, "CLIENT_INFORMATION") == 0)
				continue;
			fprintf(fp, "SET ");
			switch (var->type)
			{
			case BOOL_TYPE_VAR:
				fprintf(fp, "%s %s\n", var->name, var->integer ?
					var_settings[ON] : var_settings[OFF]);
				break;
			case CHAR_TYPE_VAR:
				fprintf(fp, "%s %c\n", var->name, var->integer);
				break;
			case INT_TYPE_VAR:
				fprintf(fp, "%s %u\n", var->name, var->integer);
				break;
			case FLOAT_TYPE_VAR:
				fprintf(fp, "%s %f\n", var->name, var->number);
				break;
			case STR_TYPE_VAR:
				if (var->string)
					fprintf(fp, "%s %s\n", var->name,
						var->string);
				else
					fprintf(fp, "-%s\n", var->name);
				break;
			}
		}
	}
}

char 	*make_string_var (const char *var_name)
{
	enum VAR_TYPES	msv_index;
	char	*ret = (char *) 0;
	char	*copy;

	copy = LOCAL_COPY(var_name);
	upper(copy);

	msv_index = get_variable_index(copy);
	if (msv_index == NUMBER_OF_VARIABLES)
		return NULL;

	switch (irc_variable[msv_index].type)
	{
		case STR_TYPE_VAR:
		        if (irc_variable[msv_index].string)
			    ret = malloc_strdup(irc_variable[msv_index].string);
			break;
		case INT_TYPE_VAR:
			ret = malloc_strdup(ltoa(irc_variable[msv_index].integer));
			break;
		case FLOAT_TYPE_VAR:
			ret = malloc_strdup(ftoa(irc_variable[msv_index].number));
			break;
		case BOOL_TYPE_VAR:
			ret = malloc_strdup(var_settings[irc_variable[msv_index].integer]);
			break;
		case CHAR_TYPE_VAR:
			ret = malloc_dupchar(irc_variable[msv_index].integer);
			break;
	}
	return (ret);

}

GET_FIXED_ARRAY_NAMES_FUNCTION(get_set, irc_variable)

/* returns the size of the character set */
int 	charset_size (void)
{
	return get_int_var(EIGHT_BIT_CHARACTERS_VAR) ? 256 : 128;
}

static void 	eight_bit_characters (const void *stuff)
{
	int	value = *(const int *)stuff;

	if (value == ON && !term_eight_bit())
		say("Warning!  Your terminal says it does not support eight bit characters");
	set_term_eight_bit(value);
}

static void 	set_realname (const void *stuff)
{
	const char *value = (const char *)stuff;

	if (!value)
	{
		say("Unsetting your realname will do you no good.  So there.");
		value = empty_string;
	}
	strlcpy(realname, value, sizeof realname);
}

static void 	set_display_pc_characters (const void *stuff)
{
	int	value = *(const int *)stuff;

	if (value < 0 || value > 5)
	{
		say("The value of DISPLAY_PC_CHARACTERS must be between 0 and 5 inclusive");
		set_int_var(DISPLAY_PC_CHARACTERS_VAR, 0);
	}
}

static void	set_dcc_timeout (const void *stuff)
{
	int	value = *(const int *)stuff;

	if (value == 0)
		dcc_timeout = (time_t) -1;
	else
		dcc_timeout = value;
}

int	parse_mangle (const char *value, int nvalue, char **rv)
{
	char	*str1, *str2;
	char	*copy;
	char	*nv = NULL;

	if (rv)
		*rv = NULL;

	if (!value)
		return 0;

	copy = LOCAL_COPY(value);

	while ((str1 = new_next_arg(copy, &copy)))
	{
		while (*str1 && (str2 = next_in_comma_list(str1, &str1)))
		{
			     if (!my_strnicmp(str2, "ALL_OFF", 4))
				nvalue |= STRIP_ALL_OFF;
			else if (!my_strnicmp(str2, "-ALL_OFF", 5))
				nvalue &= ~(STRIP_ALL_OFF);
			else if (!my_strnicmp(str2, "ALL", 3))
				nvalue = (0x7FFFFFFF ^ (MANGLE_ESCAPES) ^ (STRIP_OTHER));
			else if (!my_strnicmp(str2, "-ALL", 4))
				nvalue = 0;
			else if (!my_strnicmp(str2, "ANSI", 2))
				nvalue |= MANGLE_ANSI_CODES;
			else if (!my_strnicmp(str2, "-ANSI", 3))
				nvalue &= ~(MANGLE_ANSI_CODES);
			else if (!my_strnicmp(str2, "BLINK", 2))
				nvalue |= STRIP_BLINK;
			else if (!my_strnicmp(str2, "-BLINK", 3))
				nvalue &= ~(STRIP_BLINK);
			else if (!my_strnicmp(str2, "BOLD", 2))
				nvalue |= STRIP_BOLD;
			else if (!my_strnicmp(str2, "-BOLD", 3))
				nvalue &= ~(STRIP_BOLD);
			else if (!my_strnicmp(str2, "COLOR", 1))
				nvalue |= STRIP_COLOR;
			else if (!my_strnicmp(str2, "-COLOR", 2))
				nvalue &= ~(STRIP_COLOR);
			else if (!my_strnicmp(str2, "ESCAPE", 1))
				nvalue |= MANGLE_ESCAPES;
			else if (!my_strnicmp(str2, "-ESCAPE", 2))
				nvalue &= ~(MANGLE_ESCAPES);
			else if (!my_strnicmp(str2, "ND_SPACE", 2))
				nvalue |= STRIP_ND_SPACE;
			else if (!my_strnicmp(str2, "-ND_SPACE", 3))
				nvalue &= ~(STRIP_ND_SPACE);
			else if (!my_strnicmp(str2, "NONE", 2))
				nvalue = 0;
			else if (!my_strnicmp(str2, "OTHER", 2))
				nvalue |= STRIP_OTHER;
			else if (!my_strnicmp(str2, "-OTHER", 3))
				nvalue &= ~(STRIP_OTHER);
			else if (!my_strnicmp(str2, "REVERSE", 2))
				nvalue |= STRIP_REVERSE;
			else if (!my_strnicmp(str2, "-REVERSE", 3))
				nvalue &= ~(STRIP_REVERSE);
			else if (!my_strnicmp(str2, "ROM_CHAR", 2))
				nvalue |= STRIP_ROM_CHAR;
			else if (!my_strnicmp(str2, "-ROM_CHAR", 3))
				nvalue &= ~(STRIP_ROM_CHAR);
			else if (!my_strnicmp(str2, "UNDERLINE", 1))
				nvalue |= STRIP_UNDERLINE;
			else if (!my_strnicmp(str2, "-UNDERLINE", 2))
				nvalue &= ~(STRIP_UNDERLINE);
		}
	}

	if (rv)
	{
		if (nvalue & MANGLE_ESCAPES)
			malloc_strcat_wordlist(&nv, comma, "ESCAPE");
		if (nvalue & MANGLE_ANSI_CODES)
			malloc_strcat_wordlist(&nv, comma, "ANSI");
		if (nvalue & STRIP_COLOR)
			malloc_strcat_wordlist(&nv, comma, "COLOR");
		if (nvalue & STRIP_REVERSE)
			malloc_strcat_wordlist(&nv, comma, "REVERSE");
		if (nvalue & STRIP_UNDERLINE)
			malloc_strcat_wordlist(&nv, comma, "UNDERLINE");
		if (nvalue & STRIP_BOLD)
			malloc_strcat_wordlist(&nv, comma, "BOLD");
		if (nvalue & STRIP_BLINK)
			malloc_strcat_wordlist(&nv, comma, "BLINK");
		if (nvalue & STRIP_ROM_CHAR)
			malloc_strcat_wordlist(&nv, comma, "ROM_CHAR");
		if (nvalue & STRIP_ND_SPACE)
			malloc_strcat_wordlist(&nv, comma, "ND_SPACE");
		if (nvalue & STRIP_ALL_OFF)
			malloc_strcat_wordlist(&nv, comma, "ALL_OFF");
		if (nvalue & STRIP_OTHER)
			malloc_strcat_wordlist(&nv, comma, "OTHER");

		*rv = nv;
	}

	return nvalue;
}

static	void	set_mangle_inbound (const void *stuff)
{
	const char *value = (const char *)stuff;
	char *nv = NULL;
	inbound_line_mangler = parse_mangle(value, inbound_line_mangler, &nv);
	set_string_var(MANGLE_INBOUND_VAR, nv);
	new_free(&nv);
}

static	void	set_mangle_outbound (const void *stuff)
{
	const char *value = (const char *)stuff;
	char *nv = NULL;
	outbound_line_mangler = parse_mangle(value, outbound_line_mangler, &nv);
	set_string_var(MANGLE_OUTBOUND_VAR, nv);
	new_free(&nv);
}

static	void	set_mangle_logfiles (const void *stuff)
{
	const char *value = (const char *)stuff;
	char *nv = NULL;
	logfile_line_mangler = parse_mangle(value, logfile_line_mangler, &nv);
	set_string_var(MANGLE_LOGFILES_VAR, nv);
	new_free(&nv);
}

static	void	set_scroll (const void *stuff)
{
	int	value = *(const int *)stuff;
	char *	whatever;
	int	owd = window_display;

	window_display = 0;
	if (value)
	{
		whatever = LOCAL_COPY("ONE");
		window_scroll(current_window, &whatever);
	}
	else
	{
		whatever = LOCAL_COPY("ZERO");
		window_scroll(current_window, &whatever);
	}
	window_display = owd;
}

static void	update_all_status_wrapper (const void *stuff)
{
	update_all_status();
}

static void    set_highlight_char (const void *stuff)
{
	const char *s = (const char *)stuff;
        int     len;

        if (!s)
                s = empty_string;
        len = strlen(s);

        if (!my_strnicmp(s, "BOLD", len))
                malloc_strcpy(&highlight_char, BOLD_TOG_STR);
        else if (!my_strnicmp(s, "INVERSE", len))
                malloc_strcpy(&highlight_char, REV_TOG_STR); 
        else if (!my_strnicmp(s, "UNDERLINE", len))
                malloc_strcpy(&highlight_char, UND_TOG_STR);
        else
                malloc_strcpy(&highlight_char, s);
}


/*******/
typedef struct	varstacklist
{
	char *	varname;
	char *	value;
	struct varstacklist *next;
}	VarStack;

VarStack *set_stack = NULL;

void	do_stack_set (int type, char *args)
{
	VarStack *item;
	char *varname = NULL;

	if (set_stack == NULL && (type == STACK_POP || type == STACK_LIST))
	{
		say("Set stack is empty!");
		return;
	}

	if (STACK_PUSH == type)
	{
		varname = next_arg(args, &args);
		if (!varname)
		{
			say("Must specify a variable name to stack");
			return;
		}
		upper(varname);

		item = (VarStack *)new_malloc(sizeof(VarStack));
		item->varname = malloc_strdup(varname);
		item->value = make_string_var(varname);

		item->next = set_stack;
		set_stack = item;
		return;
	}

	else if (STACK_POP == type)
	{
	    VarStack *prev = NULL;
	    enum VAR_TYPES var_index;
	    int	owd = window_display;

	    varname = next_arg(args, &args);
	    if (!varname)
	    {
		say("Must specify a variable name to stack");
		return;
	    }
	    upper(varname);

	    for (item = set_stack; item; prev = item, item = item->next)
	    {
		/* If this is not it, go to the next one */
		if (my_stricmp(varname, item->varname))
			continue;

		/* remove it from the list */
		if (prev == NULL)
			set_stack = item->next;
		else
			prev->next = item->next;

		window_display = 0; 
		var_index = get_variable_index(item->varname);
		if (var_index == NUMBER_OF_VARIABLES)
			return;		/* Do nothing */
		set_var_value(var_index, item->value);
		window_display = owd; 

		new_free(&item->varname);
		new_free(&item->value);
		new_free(&item);
		return;
	    }

	    say("%s is not on the Set stack!", varname);
	    return;
	}

	else if (STACK_LIST == type)
	{
	    VarStack *prev = NULL;

	    for (item = set_stack; item; prev = item, item = item->next)
		say("Variable [%s] = %s", item->varname, item->value ? item->value : "<EMPTY>");

	    return;
	}

	else
		say("Unknown STACK type ??");
}

