/* $EPIC: vars.c,v 1.54 2004/07/24 00:26:46 jnelson Exp $ */
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
#include "reg.h"

/* IrcVariable: structure for each variable in the variable table */
typedef struct
{
	const char *	name;		/* what the user types */
	int		type;		/* variable types, see below */
	VARIABLE *	data;		/* The value of the variable */
	void	(*func) (const void *); /* func called when var is set */
	u_short		flags;		/* flags for this variable */
}	IrcVariable;

/*
 * The VIF_* macros stand for "(V)ariable.(i)nt_(f)lags", and have been
 * used for the various possible values of the flags data member.
 * The first two are, the third one is not.  The third one is used in
 * the 'flags' data member, but its based on the same idea.
 */
#define VIF_PENDING	0x04	/* A /set is pending for this variable */

const char	*var_settings[] =
{
	"OFF", "ON", "TOGGLE"
};

static void 	set_string_var (enum VAR_TYPES var, const char *string);
static void 	set_int_var (enum VAR_TYPES var, int value);
static char 	*make_string_var_bydata (int type, void *);

static	void	eight_bit_characters 	(const void *);
static	void	set_realname 		(const void *);
static 	void 	set_display_pc_characters (const void *);
static 	void	set_dcc_timeout 	(const void *);
static	void	set_mangle_inbound 	(const void *);
static	void	set_mangle_outbound 	(const void *);
static	void	set_mangle_logfiles 	(const void *);
static	void	update_all_status_wrapper (const void *);
static	void	set_highlight_char	(const void *);
static	void	set_wserv_type		(const void *);

#define VARPROTO(x, y, z) { x, y, NULL, z, 0 },

/*
 * irc_variable: all the irc variables used.  Note that the integer and
 * boolean defaults are set here, which the string default value are set in
 * the init_variables() procedure 
 */
static	IrcVariable irc_variable[] =
{
VARPROTO("ALLOW_C1_CHARS", 		BOOL_VAR, NULL)
VARPROTO("ALT_CHARSET", 		BOOL_VAR, NULL)
VARPROTO("ALWAYS_SPLIT_BIGGEST",	BOOL_VAR, NULL)
VARPROTO("AUTO_NEW_NICK", 		BOOL_VAR, NULL)
VARPROTO("AUTO_RECONNECT",		BOOL_VAR, NULL)
VARPROTO("AUTO_RECONNECT_DELAY",	INT_VAR,  NULL)
VARPROTO("AUTO_REJOIN",			BOOL_VAR, NULL)
VARPROTO("AUTO_REJOIN_CONNECT",		BOOL_VAR, NULL)
VARPROTO("AUTO_REJOIN_DELAY",		INT_VAR,  NULL)
VARPROTO("AUTO_UNMARK_AWAY",		BOOL_VAR, NULL)
VARPROTO("AUTO_WHOWAS",			BOOL_VAR, NULL)
VARPROTO("BAD_STYLE",			BOOL_VAR, NULL)
VARPROTO("BANNER",			STR_VAR,  NULL)
VARPROTO("BANNER_EXPAND",		BOOL_VAR, NULL)
VARPROTO("BEEP",			BOOL_VAR, NULL)
VARPROTO("BEEP_MAX",			INT_VAR,  NULL)
VARPROTO("BLINK_VIDEO",			BOOL_VAR, NULL)
VARPROTO("BOLD_VIDEO",			BOOL_VAR, NULL)
VARPROTO("CHANNEL_NAME_WIDTH",		INT_VAR,  update_all_status_wrapper)
VARPROTO("CLIENT_INFORMATION",		STR_VAR,  NULL)
VARPROTO("CLOCK",			BOOL_VAR, set_clock)
VARPROTO("CLOCK_24HOUR",		BOOL_VAR, reset_clock)
VARPROTO("CLOCK_FORMAT",		STR_VAR,  set_clock_format)
VARPROTO("CLOCK_INTERVAL",		INT_VAR,  set_clock_interval)
VARPROTO("CMDCHARS",			STR_VAR,  NULL)
VARPROTO("COLOR",			BOOL_VAR, NULL)
VARPROTO("COMMAND_MODE",		BOOL_VAR, NULL)
VARPROTO("COMMENT_HACK",		BOOL_VAR, NULL)
VARPROTO("CONNECT_TIMEOUT",		INT_VAR,  NULL)
VARPROTO("CONTINUED_LINE",		STR_VAR,  NULL)
VARPROTO("CPU_SAVER_AFTER",		INT_VAR,  set_cpu_saver_after)
VARPROTO("CPU_SAVER_EVERY",		INT_VAR,  set_cpu_saver_every)
VARPROTO("CURRENT_WINDOW_LEVEL", 	STR_VAR,  set_current_window_mask)
VARPROTO("DCC_AUTO_SEND_REJECTS", 	BOOL_VAR, NULL)
VARPROTO("DCC_DEQUOTE_FILENAMES", 	BOOL_VAR, NULL)
VARPROTO("DCC_LONG_PATHNAMES",	 	BOOL_VAR, NULL)
VARPROTO("DCC_SLIDING_WINDOW",	 	INT_VAR,  NULL)
VARPROTO("DCC_STORE_PATH",	 	STR_VAR,  NULL)
VARPROTO("DCC_TIMEOUT",		 	INT_VAR,  set_dcc_timeout)
VARPROTO("DCC_USE_GATEWAY_ADDR", 	BOOL_VAR, NULL)
VARPROTO("DEBUG",		 	INT_VAR,  NULL)
VARPROTO("DISPATCH_UNKNOWN_COMMANDS", 	BOOL_VAR, NULL)
VARPROTO("DISPLAY",		  	BOOL_VAR, NULL)
VARPROTO("DISPLAY_ANSI",	  	BOOL_VAR, NULL)
VARPROTO("DISPLAY_PC_CHARACTERS", 	INT_VAR,  set_display_pc_characters)
VARPROTO("DO_NOTIFY_IMMEDIATELY",	BOOL_VAR, NULL)
VARPROTO("EIGHT_BIT_CHARACTERS",  	BOOL_VAR, eight_bit_characters)
VARPROTO("FLOATING_POINT_MATH",	  	BOOL_VAR, NULL)
VARPROTO("FLOATING_POINT_PRECISION",	INT_VAR,  NULL)
VARPROTO("FLOOD_AFTER",			INT_VAR,  NULL)
VARPROTO("FLOOD_IGNORE",		BOOL_VAR, NULL)
VARPROTO("FLOOD_MASKUSER",		INT_VAR,  NULL)
VARPROTO("FLOOD_RATE",			INT_VAR,  NULL)
VARPROTO("FLOOD_RATE_PER",		INT_VAR,  NULL)
VARPROTO("FLOOD_USERS",			INT_VAR,  NULL)
VARPROTO("FLOOD_WARNING",		BOOL_VAR, NULL)
VARPROTO("FULL_STATUS_LINE",		BOOL_VAR, update_all_status_wrapper)
VARPROTO("HELP_PAGER",			BOOL_VAR, NULL)
VARPROTO("HELP_PATH",			STR_VAR,  NULL)
VARPROTO("HELP_PROMPT",			BOOL_VAR, NULL)
VARPROTO("HELP_WINDOW",			BOOL_VAR, NULL)
VARPROTO("HIDE_PRIVATE_CHANNELS",	BOOL_VAR, update_all_status_wrapper)
VARPROTO("HIGHLIGHT_CHAR",		STR_VAR,  set_highlight_char)
VARPROTO("HIGH_BIT_ESCAPE",		INT_VAR,  set_meta_8bit)
VARPROTO("HISTORY",			INT_VAR,  set_history_size)
VARPROTO("HISTORY_CIRCLEQ",		BOOL_VAR, NULL)
VARPROTO("HOLD_SLIDER",			INT_VAR,  NULL)
VARPROTO("INDENT",			BOOL_VAR, NULL)
VARPROTO("INPUT_ALIASES",		BOOL_VAR, NULL)
VARPROTO("INPUT_PROMPT",		STR_VAR,  set_input_prompt)
VARPROTO("INSERT_MODE",			BOOL_VAR, update_all_status_wrapper)
VARPROTO("INVERSE_VIDEO",		BOOL_VAR, NULL)
VARPROTO("KEY_INTERVAL",		INT_VAR,  set_key_interval)
VARPROTO("LASTLOG",			INT_VAR,  set_lastlog_size)
VARPROTO("LASTLOG_LEVEL",		STR_VAR,  set_lastlog_mask)
VARPROTO("LOAD_PATH",			STR_VAR,  NULL)
VARPROTO("LOG",				BOOL_VAR, logger)
VARPROTO("LOGFILE",			STR_VAR,  NULL)
VARPROTO("LOG_REWRITE",			STR_VAR,  NULL)
VARPROTO("MAIL",			INT_VAR,  set_mail)
VARPROTO("MAIL_INTERVAL",		INT_VAR,  set_mail_interval)
VARPROTO("MANGLE_INBOUND",		STR_VAR,  set_mangle_inbound)
VARPROTO("MANGLE_LOGFILES",		STR_VAR,  set_mangle_logfiles)
VARPROTO("MANGLE_OUTBOUND",		STR_VAR,  set_mangle_outbound)
VARPROTO("MAX_RECONNECTS",		INT_VAR,  NULL)
VARPROTO("METRIC_TIME",			BOOL_VAR, reset_clock)
VARPROTO("MIRC_BROKEN_DCC_RESUME",	BOOL_VAR, NULL)
VARPROTO("MODE_STRIPPER",             	BOOL_VAR, NULL)
VARPROTO("ND_SPACE_MAX",		INT_VAR,  NULL)
VARPROTO("NEW_SERVER_LASTLOG_LEVEL",	STR_VAR,  set_new_server_lastlog_mask)
VARPROTO("NOTIFY",			BOOL_VAR, set_notify)
VARPROTO("NOTIFY_INTERVAL",		INT_VAR,  set_notify_interval)
VARPROTO("NOTIFY_LEVEL",		STR_VAR,  set_notify_mask)
VARPROTO("NOTIFY_ON_TERMINATION",	BOOL_VAR, NULL)
VARPROTO("NOTIFY_USERHOST_AUTOMATIC",	BOOL_VAR, NULL)
VARPROTO("NO_CONTROL_LOG",		BOOL_VAR, NULL)
VARPROTO("NO_CTCP_FLOOD",		BOOL_VAR, NULL)
VARPROTO("NO_FAIL_DISCONNECT",		BOOL_VAR, NULL)
VARPROTO("NUM_OF_WHOWAS",		INT_VAR,  NULL)
VARPROTO("OLD_SERVER_LASTLOG_LEVEL",	STR_VAR,  set_old_server_lastlog_mask)
VARPROTO("OUTPUT_REWRITE",		STR_VAR,  NULL)
VARPROTO("PAD_CHAR",			CHAR_VAR, NULL)
VARPROTO("QUIT_MESSAGE",		STR_VAR,  NULL)
VARPROTO("RANDOM_SOURCE",		INT_VAR,  NULL)
VARPROTO("REALNAME",			STR_VAR,  set_realname)
VARPROTO("REVERSE_STATUS_LINE",		BOOL_VAR, update_all_status_wrapper)
VARPROTO("SCREEN_OPTIONS",		STR_VAR,  NULL)
VARPROTO("SCROLLBACK",			INT_VAR,  set_scrollback_size)
VARPROTO("SCROLLBACK_RATIO",		INT_VAR,  NULL)
VARPROTO("SCROLL_LINES",		INT_VAR,  set_scroll_lines)
VARPROTO("SECURITY",			INT_VAR,  NULL)
VARPROTO("SHELL",			STR_VAR,  NULL)
VARPROTO("SHELL_FLAGS",			STR_VAR,  NULL)
VARPROTO("SHELL_LIMIT",			INT_VAR,  NULL)
VARPROTO("SHOW_CHANNEL_NAMES",		BOOL_VAR, NULL)
VARPROTO("SHOW_END_OF_MSGS",		BOOL_VAR, NULL)
VARPROTO("SHOW_NUMERICS",		BOOL_VAR, NULL)
VARPROTO("SHOW_STATUS_ALL",		BOOL_VAR, update_all_status_wrapper)
VARPROTO("SHOW_WHO_HOPCOUNT", 		BOOL_VAR, NULL)
VARPROTO("SSL_CERTFILE",		STR_VAR,  NULL)
VARPROTO("SSL_KEYFILE",			STR_VAR,  NULL)
VARPROTO("SSL_PATH",			STR_VAR,  NULL)
VARPROTO("STATUS_AWAY",			STR_VAR,  build_status)
VARPROTO("STATUS_CHANNEL",		STR_VAR,  build_status)
VARPROTO("STATUS_CHANOP",		STR_VAR,  build_status)
VARPROTO("STATUS_CLOCK",		STR_VAR,  build_status)
VARPROTO("STATUS_CPU_SAVER",		STR_VAR,  build_status)
VARPROTO("STATUS_DOES_EXPANDOS",	BOOL_VAR, NULL)
VARPROTO("STATUS_FORMAT",		STR_VAR,  build_status)
VARPROTO("STATUS_FORMAT1",		STR_VAR,  build_status)
VARPROTO("STATUS_FORMAT2",		STR_VAR,  build_status)
VARPROTO("STATUS_HALFOP",		STR_VAR,  build_status)
VARPROTO("STATUS_HOLD",			STR_VAR,  build_status)
VARPROTO("STATUS_HOLD_LINES",		STR_VAR,  build_status)
VARPROTO("STATUS_INSERT",		STR_VAR,  build_status)
VARPROTO("STATUS_MAIL",			STR_VAR,  build_status)
VARPROTO("STATUS_MODE",			STR_VAR,  build_status)
VARPROTO("STATUS_NICKNAME",		STR_VAR,  build_status)
VARPROTO("STATUS_NOSWAP",		STR_VAR,  build_status)
VARPROTO("STATUS_NOTIFY",		STR_VAR,  build_status)
VARPROTO("STATUS_NO_REPEAT",		BOOL_VAR, build_status)
VARPROTO("STATUS_OPER",			STR_VAR,  build_status)
VARPROTO("STATUS_OVERWRITE",		STR_VAR,  build_status)
VARPROTO("STATUS_QUERY",		STR_VAR,  build_status)
VARPROTO("STATUS_SCROLLBACK",		STR_VAR,  build_status)
VARPROTO("STATUS_SERVER",		STR_VAR,  build_status)
VARPROTO("STATUS_SSL_OFF",		STR_VAR,  build_status)
VARPROTO("STATUS_SSL_ON",		STR_VAR,  build_status)
VARPROTO("STATUS_TRUNCATE_RHS",		BOOL_VAR, build_status)
VARPROTO("STATUS_UMODE",		STR_VAR,  build_status)
VARPROTO("STATUS_USER",			STR_VAR,  build_status)
VARPROTO("STATUS_USER1",		STR_VAR,  build_status)
VARPROTO("STATUS_USER10",		STR_VAR,  build_status)
VARPROTO("STATUS_USER11",		STR_VAR,  build_status)
VARPROTO("STATUS_USER12",		STR_VAR,  build_status)
VARPROTO("STATUS_USER13",		STR_VAR,  build_status)
VARPROTO("STATUS_USER14",		STR_VAR,  build_status)
VARPROTO("STATUS_USER15",		STR_VAR,  build_status)
VARPROTO("STATUS_USER16",		STR_VAR,  build_status)
VARPROTO("STATUS_USER17",		STR_VAR,  build_status)
VARPROTO("STATUS_USER18",		STR_VAR,  build_status)
VARPROTO("STATUS_USER19",		STR_VAR,  build_status)
VARPROTO("STATUS_USER2",		STR_VAR,  build_status)
VARPROTO("STATUS_USER20",		STR_VAR,  build_status)
VARPROTO("STATUS_USER21",		STR_VAR,  build_status)
VARPROTO("STATUS_USER22",		STR_VAR,  build_status)
VARPROTO("STATUS_USER23",		STR_VAR,  build_status)
VARPROTO("STATUS_USER24",		STR_VAR,  build_status)
VARPROTO("STATUS_USER25",		STR_VAR,  build_status)
VARPROTO("STATUS_USER26",		STR_VAR,  build_status)
VARPROTO("STATUS_USER27",		STR_VAR,  build_status)
VARPROTO("STATUS_USER28",		STR_VAR,  build_status)
VARPROTO("STATUS_USER29",		STR_VAR,  build_status)
VARPROTO("STATUS_USER3",		STR_VAR,  build_status)
VARPROTO("STATUS_USER30",		STR_VAR,  build_status)
VARPROTO("STATUS_USER31",		STR_VAR,  build_status)
VARPROTO("STATUS_USER32",		STR_VAR,  build_status)
VARPROTO("STATUS_USER33",		STR_VAR,  build_status)
VARPROTO("STATUS_USER34",		STR_VAR,  build_status)
VARPROTO("STATUS_USER35",		STR_VAR,  build_status)
VARPROTO("STATUS_USER36",		STR_VAR,  build_status)
VARPROTO("STATUS_USER37",		STR_VAR,  build_status)
VARPROTO("STATUS_USER38",		STR_VAR,  build_status)
VARPROTO("STATUS_USER39",		STR_VAR,  build_status)
VARPROTO("STATUS_USER4",		STR_VAR,  build_status)
VARPROTO("STATUS_USER5",		STR_VAR,  build_status)
VARPROTO("STATUS_USER6",		STR_VAR,  build_status)
VARPROTO("STATUS_USER7",		STR_VAR,  build_status)
VARPROTO("STATUS_USER8",		STR_VAR,  build_status)
VARPROTO("STATUS_USER9",		STR_VAR,  build_status)
VARPROTO("STATUS_VOICE",		STR_VAR,  build_status)
VARPROTO("STATUS_WINDOW",		STR_VAR,  build_status)
VARPROTO("SUPPRESS_FROM_REMOTE_SERVER",	BOOL_VAR, NULL)
VARPROTO("SWITCH_CHANNELS_BETWEEN_WINDOWS", BOOL_VAR, NULL)
VARPROTO("SWITCH_CHANNEL_ON_PART",	BOOL_VAR, NULL)
VARPROTO("TAB",				BOOL_VAR, NULL)
VARPROTO("TAB_MAX",			INT_VAR,  NULL)
VARPROTO("TERM_DOES_BRIGHT_BLINK",	BOOL_VAR, NULL)
VARPROTO("TRANSLATION",			STR_VAR,  set_translation)
VARPROTO("TRANSLATION_PATH",		STR_VAR,  NULL)
VARPROTO("UNDERLINE_VIDEO",		BOOL_VAR, NULL)
VARPROTO("USER_INFORMATION", 		STR_VAR,  NULL)
VARPROTO("VERBOSE_CTCP",		BOOL_VAR, NULL)
VARPROTO("WORD_BREAK",			STR_VAR,  NULL)
VARPROTO("WSERV_PATH",			STR_VAR,  NULL)
VARPROTO("WSERV_TYPE",			STR_VAR,  set_wserv_type)
VARPROTO("XTERM",			STR_VAR,  NULL)
VARPROTO("XTERM_OPTIONS", 		STR_VAR,  NULL)
VARPROTO(NULL,				0,	  0)
};

/*
 * init_variables: initializes the string variables that can't really be
 * initialized properly above 
 */
void 	init_variables_stage1 (void)
{
	int 	i;

	for (i = 1; i < NUMBER_OF_VARIABLES - 1; i++)
		if (strcmp(irc_variable[i-1].name, irc_variable[i].name) >= 0)
			panic("Variable [%d] (%s) is out of order.", i, irc_variable[i].name);

	for (i = 0; i < NUMBER_OF_VARIABLES; i++)
	{
	    irc_variable[i].data = new_malloc(sizeof(union builtin_variable));
	    switch (irc_variable[i].type) {
		case BOOL_VAR:
		case CHAR_VAR:
		case INT_VAR:
			irc_variable[i].data->integer = 0;
			break;
		case STR_VAR:
			irc_variable[i].data->string = NULL;
			break;
	    }
	}

}

/*
 * set_string_var: sets the string variable given as an index into the
 * variable table to the given string.  If string is null, the current value
 * of the string variable is freed and set to null 
 */
__inline
static void 	set_string_var (enum VAR_TYPES var, const char *string)
{
	if (string)
		malloc_strcpy(&(irc_variable[var].data->string), string);
	else
		new_free(&(irc_variable[var].data->string));
}

/* Same story, second verse. */
__inline
static void 	set_int_var (enum VAR_TYPES var, int value)
{
	irc_variable[var].data->integer = value;
}


void 	init_variables_stage2 (void)
{
	char 	*s;
	int 	i;

	set_int_var(ALLOW_C1_CHARS_VAR,		DEFAULT_ALLOW_C1_CHARS);
	set_int_var(ALT_CHARSET_VAR, 		DEFAULT_ALT_CHARSET);
	set_int_var(ALWAYS_SPLIT_BIGGEST_VAR, 	DEFAULT_ALWAYS_SPLIT_BIGGEST);
	set_int_var(AUTO_NEW_NICK_VAR,		DEFAULT_AUTO_NEW_NICK);
	set_int_var(AUTO_RECONNECT_VAR,         DEFAULT_AUTO_RECONNECT);
	set_int_var(AUTO_RECONNECT_DELAY_VAR,	DEFAULT_AUTO_RECONNECT_DELAY);
	set_int_var(AUTO_REJOIN_VAR,            DEFAULT_AUTO_REJOIN);
	set_int_var(AUTO_REJOIN_CONNECT_VAR,	DEFAULT_AUTO_REJOIN_CONNECT);
	set_int_var(AUTO_REJOIN_DELAY_VAR,	DEFAULT_AUTO_REJOIN_DELAY);
	set_int_var(AUTO_UNMARK_AWAY_VAR,	DEFAULT_AUTO_UNMARK_AWAY);
	set_int_var(AUTO_WHOWAS_VAR,		DEFAULT_AUTO_WHOWAS);
	set_int_var(BAD_STYLE_VAR,		DEFAULT_BAD_STYLE);
	set_int_var(BEEP_VAR,			DEFAULT_BEEP);
	set_int_var(BEEP_MAX_VAR,		DEFAULT_BEEP_MAX);
	set_int_var(BLINK_VIDEO_VAR,		DEFAULT_BLINK_VIDEO);
	set_int_var(BOLD_VIDEO_VAR,		DEFAULT_BOLD_VIDEO);
	set_int_var(CHANNEL_NAME_WIDTH_VAR,	DEFAULT_CHANNEL_NAME_WIDTH);
	set_int_var(CLOCK_VAR,			DEFAULT_CLOCK);
	set_int_var(CLOCK_24HOUR_VAR,		DEFAULT_CLOCK_24HOUR);
	set_int_var(CLOCK_INTERVAL_VAR,		DEFAULT_CLOCK_INTERVAL);
	set_int_var(COLOR_VAR,			DEFAULT_COLOR);
	set_int_var(COMMAND_MODE_VAR,		DEFAULT_COMMAND_MODE);
	set_int_var(COMMENT_HACK_VAR,		DEFAULT_COMMENT_HACK);
	set_int_var(CONNECT_TIMEOUT_VAR,	DEFAULT_CONNECT_TIMEOUT);
	set_int_var(CPU_SAVER_AFTER_VAR,	DEFAULT_CPU_SAVER_AFTER);
	set_int_var(CPU_SAVER_EVERY_VAR,	DEFAULT_CPU_SAVER_EVERY);
	set_int_var(DCC_AUTO_SEND_REJECTS_VAR,	DEFAULT_DCC_AUTO_SEND_REJECTS);
	set_int_var(DCC_DEQUOTE_FILENAMES_VAR,	DEFAULT_DCC_DEQUOTE_FILENAMES);
	set_int_var(DCC_LONG_PATHNAMES_VAR,	DEFAULT_DCC_LONG_PATHNAMES);
	set_int_var(DCC_SLIDING_WINDOW_VAR,	DEFAULT_DCC_SLIDING_WINDOW);
	set_int_var(DCC_TIMEOUT_VAR,		DEFAULT_DCC_TIMEOUT);
	set_int_var(DISPATCH_UNKNOWN_COMMANDS_VAR,	DEFAULT_DISPATCH_UNKNOWN_COMMANDS);
	set_int_var(DISPLAY_VAR,		DEFAULT_DISPLAY);
	set_int_var(DISPLAY_ANSI_VAR,		DEFAULT_DISPLAY_ANSI);
	set_int_var(DISPLAY_PC_CHARACTERS_VAR,	DEFAULT_DISPLAY_PC_CHARACTERS);
	set_int_var(DO_NOTIFY_IMMEDIATELY_VAR,	DEFAULT_DO_NOTIFY_IMMEDIATELY);
	set_int_var(EIGHT_BIT_CHARACTERS_VAR,	DEFAULT_EIGHT_BIT_CHARACTERS);
	set_int_var(FLOATING_POINT_MATH_VAR,	DEFAULT_FLOATING_POINT_MATH);
	set_int_var(FLOATING_POINT_PRECISION_VAR, DEFAULT_FLOATING_POINT_PRECISION);
	set_int_var(FLOOD_AFTER_VAR,		DEFAULT_FLOOD_AFTER);
	set_int_var(FLOOD_IGNORE_VAR,		DEFAULT_FLOOD_IGNORE);
	set_int_var(FLOOD_MASKUSER_VAR,		DEFAULT_FLOOD_MASKUSER);
	set_int_var(FLOOD_RATE_VAR,		DEFAULT_FLOOD_RATE);
	set_int_var(FLOOD_RATE_PER_VAR,		DEFAULT_FLOOD_RATE_PER);
	set_int_var(FLOOD_USERS_VAR,		DEFAULT_FLOOD_USERS);
	set_int_var(FLOOD_WARNING_VAR,		DEFAULT_FLOOD_WARNING);
	set_int_var(FULL_STATUS_LINE_VAR,	DEFAULT_FULL_STATUS_LINE);
	set_int_var(HELP_PAGER_VAR,		DEFAULT_HELP_PAGER);
	set_int_var(HELP_PROMPT_VAR,		DEFAULT_HELP_PROMPT);
	set_int_var(HELP_WINDOW_VAR,		DEFAULT_HELP_WINDOW);
	set_int_var(HIDE_PRIVATE_CHANNELS_VAR,	DEFAULT_HIDE_PRIVATE_CHANNELS);
	set_int_var(HIGH_BIT_ESCAPE_VAR,	DEFAULT_HIGH_BIT_ESCAPE);
	set_int_var(HISTORY_VAR,		DEFAULT_HISTORY);
	set_int_var(HISTORY_CIRCLEQ_VAR,	DEFAULT_HISTORY_CIRCLEQ);
	set_int_var(HOLD_SLIDER_VAR,		DEFAULT_HOLD_SLIDER);
	set_int_var(INDENT_VAR,			DEFAULT_INDENT);
	set_int_var(INPUT_ALIASES_VAR,		DEFAULT_INPUT_ALIASES);
	set_int_var(INSERT_MODE_VAR,		DEFAULT_INSERT_MODE);
	set_int_var(INVERSE_VIDEO_VAR,		DEFAULT_INVERSE_VIDEO);
	set_int_var(KEY_INTERVAL_VAR,		DEFAULT_KEY_INTERVAL);
	set_int_var(LASTLOG_VAR,		DEFAULT_LASTLOG);
	set_int_var(LOG_VAR,			DEFAULT_LOG);
	set_int_var(MAIL_VAR,			DEFAULT_MAIL);
	set_int_var(MAIL_INTERVAL_VAR,		DEFAULT_MAIL_INTERVAL);
	set_int_var(MAX_RECONNECTS_VAR,		DEFAULT_MAX_RECONNECTS);
	set_int_var(METRIC_TIME_VAR,		DEFAULT_METRIC_TIME);
	set_int_var(MODE_STRIPPER_VAR,          DEFAULT_MODE_STRIPPER);
	set_int_var(ND_SPACE_MAX_VAR,		DEFAULT_ND_SPACE_MAX);
	set_int_var(NOTIFY_VAR,			DEFAULT_NOTIFY);
	set_int_var(NOTIFY_INTERVAL_VAR,	DEFAULT_NOTIFY_INTERVAL);
	set_int_var(NOTIFY_ON_TERMINATION_VAR,	DEFAULT_NOTIFY_ON_TERMINATION);
	set_int_var(NOTIFY_USERHOST_AUTOMATIC_VAR,	DEFAULT_NOTIFY_USERHOST_AUTOMATIC);
	set_int_var(NO_CONTROL_LOG_VAR,		DEFAULT_NO_CONTROL_LOG);
	set_int_var(NO_CTCP_FLOOD_VAR,		DEFAULT_NO_CTCP_FLOOD);
	set_int_var(NO_FAIL_DISCONNECT_VAR,	DEFAULT_NO_FAIL_DISCONNECT);
	set_int_var(NUM_OF_WHOWAS_VAR,		DEFAULT_NUM_OF_WHOWAS);
	set_int_var(PAD_CHAR_VAR,		DEFAULT_PAD_CHAR);
	set_int_var(RANDOM_SOURCE_VAR,		DEFAULT_RANDOM_SOURCE);
	set_int_var(REVERSE_STATUS_LINE_VAR,	DEFAULT_REVERSE_STATUS_LINE);
	set_int_var(SCROLLBACK_VAR,		DEFAULT_SCROLLBACK);
	set_int_var(SCROLLBACK_RATIO_VAR,	DEFAULT_SCROLLBACK_RATIO);
	set_int_var(SCROLL_LINES_VAR,		DEFAULT_SCROLL_LINES);
	set_int_var(SECURITY_VAR,		DEFAULT_SECURITY);
	set_int_var(SHELL_LIMIT_VAR,		DEFAULT_SHELL_LIMIT);
	set_int_var(SHOW_CHANNEL_NAMES_VAR,	DEFAULT_SHOW_CHANNEL_NAMES);
	set_int_var(SHOW_END_OF_MSGS_VAR,	DEFAULT_SHOW_END_OF_MSGS);
	set_int_var(SHOW_NUMERICS_VAR,		DEFAULT_SHOW_NUMERICS);
	set_int_var(SHOW_STATUS_ALL_VAR,	DEFAULT_SHOW_STATUS_ALL);
	set_int_var(SHOW_WHO_HOPCOUNT_VAR, 	DEFAULT_SHOW_WHO_HOPCOUNT);
	set_int_var(STATUS_NO_REPEAT_VAR,       DEFAULT_STATUS_NO_REPEAT);
	set_int_var(STATUS_TRUNCATE_RHS_VAR,	DEFAULT_STATUS_TRUNCATE_RHS);
	set_int_var(SUPPRESS_FROM_REMOTE_SERVER_VAR, 	DEFAULT_SUPPRESS_FROM_REMOTE_SERVER);
	set_int_var(SWITCH_CHANNELS_BETWEEN_WINDOWS_VAR,	DEFAULT_SWITCH_CHANNELS_BETWEEN_WINDOWS);
	set_int_var(SWITCH_CHANNEL_ON_PART_VAR,	DEFAULT_SWITCH_CHANNEL_ON_PART);
	set_int_var(TAB_VAR,			DEFAULT_TAB);
	set_int_var(TAB_MAX_VAR,		DEFAULT_TAB_MAX);
	set_int_var(TERM_DOES_BRIGHT_BLINK_VAR,	DEFAULT_TERM_DOES_BRIGHT_BLINK);
	set_int_var(UNDERLINE_VIDEO_VAR,	DEFAULT_UNDERLINE_VIDEO);
	set_int_var(VERBOSE_CTCP_VAR,		DEFAULT_VERBOSE_CTCP);


	set_string_var(BANNER_VAR, DEFAULT_BANNER);
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
	set_string_var(OLD_SERVER_LASTLOG_LEVEL_VAR, 
			DEFAULT_OLD_SERVER_LASTLOG_LEVEL);
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
	set_string_var(STATUS_NOSWAP_VAR, DEFAULT_STATUS_NOSWAP);
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
	set_string_var(WSERV_TYPE_VAR, DEFAULT_WSERV_TYPE);

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
			var->func(var->data);
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

void	show_var_value (enum VAR_TYPES svv_index, int newval)
{
	IrcVariable *var;
	char *value;

	var = &irc_variable[svv_index];
	value = make_string_var_bydata(var->type, (void *)var->data);

	if (!value)
		value = malloc_strdup("<EMPTY>");

	say("%s value of %s is %s", newval ? "New" : "Current", 
					var->name, value);
	new_free(&value);
}

/*
 * set_var_value: Given the variable structure and the string representation
 * of the value, this sets the value in the most verbose and error checking
 * of manors.  It displays the results of the set and executes the function
 * defined in the var structure 
 */
void 	set_var_value (enum VAR_TYPES svv_index, char *value, int noisy)
{
	char	*rest;
	IrcVariable *var;
	int	old;
	int	changed = 0;

	var = &(irc_variable[svv_index]);
	switch (var->type)
	{
	    case BOOL_VAR:
	    {
		if (value && *value && (value = next_arg(value, &rest)))
		{
			old = var->data->integer;
			if (do_boolean(value, &(var->data->integer)))
			    say("Value must be either ON, OFF, or TOGGLE");
			else
			    changed = 1;
		}
		break;
	    }

	    case CHAR_VAR:
	    {
		if (!value)
		{
			var->data->integer = ' ';
			changed = 1;
		}
		else if (value && *value && (value = next_arg(value, &rest)))
		{
			if (strlen(value) > 1)
			    say("Value of %s must be a single character",
					var->name);
			else
			{
				var->data->integer = *value;
				changed = 1;
			}
		}
		break;
	    }

	    case INT_VAR:
	    {
		if (value && *value && (value = next_arg(value, &rest)))
		{
			int	val;

			if (!is_number(value))
			    say("Value of %s must be numeric!", var->name);
			else if ((val = my_atol(value)) < 0)
			    say("Value of %s must be a non-negative number", 
					var->name);
			else
			{
				var->data->integer = val;
				changed = 1;
			}
		}
		break;
	    }

	    case STR_VAR:
	    {
		if (!value)
		{
			new_free(&(var->data->string));
			changed = 1;
		}
		else if (*value)
		{
			malloc_strcpy(&(var->data->string), value);
			changed = 1;
		}
	    }
	}

	if (changed)
	{
	    if (var->func && !(var->flags & VIF_PENDING))
	    {
		var->flags |= VIF_PENDING;
		(var->func)(var->data);
		var->flags &= ~VIF_PENDING;
	    }
	}

	if (noisy)
	    show_var_value(svv_index, changed);
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
		find_fixed_array_item(irc_variable, sizeof(IrcVariable), 
					NUMBER_OF_VARIABLES, var, &cnt, 
					(int *)&sv_index);

		if (cnt == 1)
			cnt = -1;

		if ((cnt >= 0) || !(irc_variable[sv_index].flags & VIF_PENDING))
			hook = 1;

		if (cnt < 0)
			irc_variable[sv_index].flags |= VIF_PENDING;

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
			irc_variable[sv_index].flags &= ~VIF_PENDING;

		/* If the user hooked it, we're all done! */
		if (!hook)
			return;

		/* User didn't offer at it -- do the default thing. */
		if (cnt < 0)
		{
			set_var_value(sv_index, args, 1);
			return;
		}

		/* User didn't offer at it, and it isn't valid */
		if (cnt == 0)
		{
			if (do_hook(SET_LIST, "set-error No such variable \"%s\"", var))
				say("No such variable \"%s\"", var);
			return;
		}

		/* User didn't offer at it, and it's ambiguous */
		if (do_hook(SET_LIST, "set-error %s is ambiguous", var))
		{
			say("%s is ambiguous", var);
			for (cnt += sv_index; (int)sv_index < cnt; 
				sv_index = (enum VAR_TYPES)(sv_index + 1))
			{
				char es[1];
				es[0] = 0;
				set_var_value(sv_index, es, 1);
			}
		}
	}
	else
        {
		int var_index;
		for (var_index = 0; var_index < NUMBER_OF_VARIABLES; var_index++)
		{
			char es[1];
			es[0] = 0;
			set_var_value(var_index, es, 1);
		}
        }
}

/*
 * get_string_var: returns the value of the string variable given as an index
 * into the variable table.  Does no checking of variable types, etc 
 */
char *	get_string_var (enum VAR_TYPES var)
{
	return (irc_variable[var].data->string);
}

/*
 * get_int_var: returns the value of the integer string given as an index
 * into the variable table.  Does no checking of variable types, etc 
 */
int 	get_int_var (enum VAR_TYPES var)
{
	return (irc_variable[var].data->integer);
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
		if (strcmp(var->name, "DISPLAY") == 0 || strcmp(var->name, "CLIENT_INFORMATION") == 0)
			continue;
		fprintf(fp, "SET ");
		switch (var->type)
		{
		case BOOL_VAR:
			fprintf(fp, "%s %s\n", var->name, var->data->integer ?
				var_settings[ON] : var_settings[OFF]);
			break;
		case CHAR_VAR:
			fprintf(fp, "%s %c\n", var->name, var->data->integer);
			break;
		case INT_VAR:
			fprintf(fp, "%s %u\n", var->name, var->data->integer);
			break;
		case STR_VAR:
			if (var->data->string)
				fprintf(fp, "%s %s\n", var->name,
					var->data->string);
			else
				fprintf(fp, "-%s\n", var->name);
			break;
		}
	}
}

static char 	*make_string_var_bydata (int type, void *vp)
{
	char	*ret = (char *) 0;
	VARIABLE *data = (VARIABLE *)vp;

	switch (type)
	{
		case STR_VAR:
		        if (data->string)
			    ret = malloc_strdup(data->string);
			break;
		case INT_VAR:
			ret = malloc_strdup(ltoa(data->integer));
			break;
		case BOOL_VAR:
			ret = malloc_strdup(var_settings[data->integer]);
			break;
		case CHAR_VAR:
			ret = malloc_dupchar(data->integer);
			break;
		default:
			panic("make_string_var_bydata: unrecognized type [%d]", type);
	}
	return (ret);

}

char 	*make_string_var (const char *var_name)
{
	enum VAR_TYPES	msv_index;
	char	*copy;

	copy = LOCAL_COPY(var_name);
	upper(copy);

	msv_index = get_variable_index(copy);
	if (msv_index == NUMBER_OF_VARIABLES)
		return NULL;

	return make_string_var_bydata(irc_variable[msv_index].type, irc_variable[msv_index].data);
}


GET_FIXED_ARRAY_NAMES_FUNCTION(get_set, irc_variable)

/* returns the size of the character set */
int 	charset_size (void)
{
	return get_int_var(EIGHT_BIT_CHARACTERS_VAR) ? 256 : 128;
}

static void 	eight_bit_characters (const void *stuff)
{
	VARIABLE *v;
	int	value;

	v = (VARIABLE *)stuff;
	value = v->integer;
	if (value == ON && !term_eight_bit())
		say("Warning!  Your terminal says it does not support eight bit characters");
	set_term_eight_bit(value);
}

static void 	set_realname (const void *stuff)
{
	VARIABLE *v;
	const char *value;

	v = (VARIABLE *)stuff;
	value = v->string;

	if (!value)
	{
		say("Unsetting your realname will do you no good.  So there.");
		value = empty_string;
	}
	strlcpy(realname, value, sizeof realname);
}

static void 	set_display_pc_characters (const void *stuff)
{
	VARIABLE *v;
	int	value;

	v = (VARIABLE *)stuff;
	value = v->integer;

	if (value < 0 || value > 5)
	{
		say("The value of DISPLAY_PC_CHARACTERS must be between 0 and 5 inclusive");
		v->integer = 0;
	}
}

static void	set_dcc_timeout (const void *stuff)
{
	VARIABLE *v;
	int	value;

	v = (VARIABLE *)stuff;
	value = v->integer;

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
				nvalue = (0x7FFFFFFF ^ (MANGLE_ESCAPES) ^ (STRIP_OTHER) ^ (STRIP_ALL_OFF));
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
	VARIABLE *v;
	const char *value;
	char *nv = NULL;

	v = (VARIABLE *)stuff;
	value = v->string;

	inbound_line_mangler = parse_mangle(value, inbound_line_mangler, &nv);
	malloc_strcpy(&v->string, nv);
	new_free(&nv);
}

static	void	set_mangle_outbound (const void *stuff)
{
	VARIABLE *v;
	const char *value;
	char *nv = NULL;

	v = (VARIABLE *)stuff;
	value = v->string;

	outbound_line_mangler = parse_mangle(value, outbound_line_mangler, &nv);
	malloc_strcpy(&v->string, nv);
	new_free(&nv);
}

static	void	set_mangle_logfiles (const void *stuff)
{
	VARIABLE *v;
	const char *value;
	char *nv = NULL;

	v = (VARIABLE *)stuff;
	value = v->string;

	logfile_line_mangler = parse_mangle(value, logfile_line_mangler, &nv);
	malloc_strcpy(&v->string, nv);
	new_free(&nv);
}

static void	update_all_status_wrapper (const void *stuff)
{
	update_all_status();
}

static void    set_highlight_char (const void *stuff)
{
	VARIABLE *v;
	const char *s;
        int     len;

	v = (VARIABLE *)stuff;
	s = v->string;

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

static void    set_wserv_type (const void *stuff)
{
	VARIABLE *v;
	const char *s;

	v = (VARIABLE *)stuff;
	s = v->string;

        if (!s)
		return;		/* It's ok */
	if (!my_stricmp(s, "SCREEN"))
		return;		/* It's ok */
	if (!my_stricmp(s, "XTERM"))
		return;		/* It's ok */

	say("SET WSERV_TYPE must be either SCREEN or XTERM");
	new_free(&v->string);
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
		set_var_value(var_index, item->value, 1);
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

