/* $EPIC: vars.c,v 1.61 2004/08/12 16:59:29 jnelson Exp $ */
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

#define VARS_C

#include "irc.h"
#include "alist.h"
#include "alias.h"
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
#include "commands.h"
#include "if.h"

static void 	set_variable (const char *, IrcVariable *, const char *, int);

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

	Bucket *var_bucket = NULL;

static	void	eight_bit_characters 	(void *);
static 	void 	set_display_pc_characters (void *);
static 	void	set_dcc_timeout 	(void *);
static	void	set_mangle_inbound 	(void *);
static	void	set_mangle_outbound 	(void *);
static	void	set_mangle_logfiles 	(void *);
static	void	update_all_status_wrapper (void *);
static	void	set_highlight_char	(void *);
static	void	set_wserv_type		(void *);


/* BIV stands for "built in variable" */
static int	add_biv (const char *name, int bucket, int type, void (*func) (void *), const char *script, ...)
{
	IrcVariable *var;
	va_list va;
	int numval;
	const char *strval;

	var = (IrcVariable *)new_malloc(sizeof(IrcVariable));
	var->type = type;
	if (script)
		var->script = malloc_strdup(script);
	else
		var->script = NULL;
	var->func = func;

	var->data = new_malloc(sizeof(union builtin_variable));
	var->flags = 0;

	va_start(va, script);
	switch (var->type) {
	    case BOOL_VAR:
	    case CHAR_VAR:
	    case INT_VAR:
		numval = va_arg(va, int);
		var->data->integer = numval;
		break;
	    case STR_VAR:
		strval = va_arg(va, char *);
		if (strval)
			var->data->string = malloc_strdup(strval);
		else
			var->data->string = NULL;
		break;
	}
	va_end(va);

	add_builtin_variable_alias(name, var);
	if (bucket)
	{
		add_to_bucket(var_bucket, name, var);
		return (var_bucket->numitems - 1);
	}
	else
		return -1;
}

#define VAR(x, y, z) x ## _VAR = add_biv( #x, 1, y ## _VAR, z,NULL, DEFAULT_ ## x);

/*
 * init_variables: initializes the string variables that can't really be
 * initialized properly above 
 */
void 	init_variables_stage1 (void)
{
	char 	*s;

	var_bucket = new_bucket();

	VAR(ALLOW_C1_CHARS, 		BOOL, NULL)
	VAR(ALT_CHARSET, 		BOOL, NULL)
	VAR(ALWAYS_SPLIT_BIGGEST, 	BOOL, NULL)
	VAR(AUTO_NEW_NICK,		BOOL, NULL)
	VAR(AUTO_RECONNECT, 		BOOL, NULL)
	VAR(AUTO_RECONNECT_DELAY, 	INT,  NULL)
	VAR(AUTO_REJOIN, 		BOOL, NULL)
	VAR(AUTO_REJOIN_CONNECT, 	BOOL, NULL)
	VAR(AUTO_REJOIN_DELAY, 		INT,  NULL)
	VAR(AUTO_UNMARK_AWAY, 		BOOL, NULL)
	VAR(AUTO_WHOWAS, 		BOOL, NULL)
	VAR(BAD_STYLE, 			BOOL, NULL)
	VAR(BANNER, 			STR,  NULL)
#define DEFAULT_BANNER_EXPAND 0
	VAR(BANNER_EXPAND, 		BOOL, NULL)
	VAR(BEEP, 			BOOL, NULL)
	VAR(BEEP_MAX, 			INT,  NULL)
	VAR(BLINK_VIDEO, 		BOOL, NULL)
	VAR(BOLD_VIDEO, 		BOOL, NULL)
	VAR(CHANNEL_NAME_WIDTH, 	INT,  update_all_status_wrapper)
#define DEFAULT_CLIENT_INFORMATION IRCII_COMMENT
	VAR(CLIENT_INFORMATION, 	STR,  NULL)
	VAR(CLOCK, 			BOOL, set_clock);
	VAR(CLOCK_24HOUR, 		BOOL, reset_clock);
	VAR(CLOCK_FORMAT, 		STR,  set_clock_format);
	VAR(CLOCK_INTERVAL, 		INT,  set_clock_interval);
	VAR(CMDCHARS, 			STR,  NULL);
	VAR(COLOR, BOOL, NULL);
	VAR(COMMAND_MODE, BOOL, NULL);
	VAR(COMMENT_HACK, BOOL, NULL);
	VAR(CONNECT_TIMEOUT, INT,  NULL);
	VAR(CONTINUED_LINE, STR,  NULL);
	VAR(CPU_SAVER_AFTER, INT,  set_cpu_saver_after);
	VAR(CPU_SAVER_EVERY, INT,  set_cpu_saver_every);
	VAR(CURRENT_WINDOW_LEVEL, STR,  set_current_window_mask);
	VAR(DCC_AUTO_SEND_REJECTS, BOOL, NULL);
	VAR(DCC_DEQUOTE_FILENAMES, BOOL, NULL);
	VAR(DCC_LONG_PATHNAMES, BOOL, NULL);
	VAR(DCC_SLIDING_WINDOW, INT,  NULL);
#define DEFAULT_DCC_STORE_PATH NULL
	VAR(DCC_STORE_PATH, STR,  NULL);
	VAR(DCC_TIMEOUT, INT,  set_dcc_timeout);
#define DEFAULT_DCC_USE_GATEWAY_ADDR 0
	VAR(DCC_USE_GATEWAY_ADDR, BOOL, NULL)
#define DEFAULT_DEBUG 0
	VAR(DEBUG, INT,  NULL);
	VAR(DISPATCH_UNKNOWN_COMMANDS, BOOL, NULL);
	VAR(DISPLAY, BOOL, NULL);
	VAR(DISPLAY_ANSI, BOOL, NULL);
	VAR(DISPLAY_PC_CHARACTERS, INT,  set_display_pc_characters);
	VAR(DO_NOTIFY_IMMEDIATELY, BOOL, NULL);
	VAR(EIGHT_BIT_CHARACTERS, BOOL, eight_bit_characters);
	VAR(FLOATING_POINT_MATH, BOOL, NULL);
	VAR(FLOATING_POINT_PRECISION, INT,  NULL);
	VAR(FLOOD_AFTER, INT,  NULL);
	VAR(FLOOD_IGNORE, BOOL, NULL);
	VAR(FLOOD_MASKUSER, INT,  NULL);
	VAR(FLOOD_RATE, INT,  NULL);
	VAR(FLOOD_RATE_PER, INT,  NULL);
	VAR(FLOOD_USERS, INT,  NULL);
	VAR(FLOOD_WARNING, BOOL, NULL);
	VAR(FULL_STATUS_LINE, BOOL, update_all_status_wrapper);
	VAR(HELP_PAGER, BOOL, NULL);
	/*
	 * Construct the default help path
	 */
	s = malloc_strdup(irc_lib);
	malloc_strcat(&s, "/help");
#define DEFAULT_HELP_PATH s
	VAR(HELP_PATH, STR,  NULL);
	VAR(HELP_PROMPT, BOOL, NULL);
	VAR(HELP_WINDOW, BOOL, NULL);
	VAR(HIDE_PRIVATE_CHANNELS, BOOL, update_all_status_wrapper);
	VAR(HIGHLIGHT_CHAR, STR,  set_highlight_char);
	VAR(HIGH_BIT_ESCAPE, INT,  set_meta_8bit);
	VAR(HISTORY, INT,  set_history_size);
	VAR(HISTORY_CIRCLEQ, BOOL, NULL);
	VAR(HOLD_SLIDER, INT,  NULL);
	VAR(INDENT, BOOL, NULL);
	VAR(INPUT_ALIASES, BOOL, NULL);
	VAR(INPUT_PROMPT, STR,  set_input_prompt);
	VAR(INSERT_MODE, BOOL, update_all_status_wrapper);
	VAR(INVERSE_VIDEO, BOOL, NULL);
	VAR(KEY_INTERVAL, INT,  set_key_interval);
	VAR(LASTLOG, INT,  set_lastlog_size);
	VAR(LASTLOG_LEVEL, STR,  set_lastlog_mask);
#define DEFAULT_LOAD_PATH NULL
	VAR(LOAD_PATH, STR,  NULL);
	VAR(LOG, BOOL, logger);
	VAR(LOGFILE, STR,  NULL);
#define DEFAULT_LOG_REWRITE NULL
	VAR(LOG_REWRITE, STR,  NULL);
	VAR(MAIL, INT,  set_mail);
	VAR(MAIL_INTERVAL, INT,  set_mail_interval);
#define DEFAULT_MANGLE_INBOUND NULL
	VAR(MANGLE_INBOUND, STR,  set_mangle_inbound);
#define DEFAULT_MANGLE_LOGFILES NULL
	VAR(MANGLE_LOGFILES, STR,  set_mangle_logfiles);
#define DEFAULT_MANGLE_OUTBOUND NULL
	VAR(MANGLE_OUTBOUND, STR,  set_mangle_outbound);
	VAR(MAX_RECONNECTS, INT,  NULL);
	VAR(METRIC_TIME, BOOL, reset_clock);
#define DEFAULT_MIRC_BROKEN_DCC_RESUME 0
	VAR(MIRC_BROKEN_DCC_RESUME, BOOL, NULL);
	VAR(MODE_STRIPPER, BOOL, NULL);
	VAR(ND_SPACE_MAX, INT,  NULL);
	VAR(NEW_SERVER_LASTLOG_LEVEL, STR,  set_new_server_lastlog_mask);
	VAR(NOTIFY, BOOL, set_notify);
	VAR(NOTIFY_INTERVAL, INT,  set_notify_interval);
	VAR(NOTIFY_LEVEL, STR,  set_notify_mask);
	VAR(NOTIFY_ON_TERMINATION, BOOL, NULL);
	VAR(NOTIFY_USERHOST_AUTOMATIC, BOOL, NULL);
	VAR(NO_CONTROL_LOG, BOOL, NULL);
	VAR(NO_CTCP_FLOOD, BOOL, NULL);
	VAR(NO_FAIL_DISCONNECT, BOOL, NULL);
	VAR(NUM_OF_WHOWAS, INT,  NULL);
	VAR(OLD_SERVER_LASTLOG_LEVEL, STR,  set_old_server_lastlog_mask);
#define DEFAULT_OUTPUT_REWRITE NULL
	VAR(OUTPUT_REWRITE, STR,  NULL);
	VAR(PAD_CHAR, CHAR, NULL);
	VAR(QUIT_MESSAGE, STR,  NULL);
	VAR(RANDOM_SOURCE, INT,  NULL);
#define DEFAULT_REALNAME NULL
	VAR(REALNAME, STR,  NULL);
	VAR(REVERSE_STATUS_LINE, BOOL, update_all_status_wrapper);
#define DEFAULT_SCREEN_OPTIONS NULL
	VAR(SCREEN_OPTIONS, STR,  NULL);
	VAR(SCROLLBACK, INT,  set_scrollback_size);
	VAR(SCROLLBACK_RATIO, INT,  NULL);
	VAR(SCROLL_LINES, INT,  set_scroll_lines);
	VAR(SECURITY, INT,  NULL);
	VAR(SHELL, STR,  NULL);
	VAR(SHELL_FLAGS, STR,  NULL);
	VAR(SHELL_LIMIT, INT,  NULL);
	VAR(SHOW_CHANNEL_NAMES, BOOL, NULL);
	VAR(SHOW_END_OF_MSGS, BOOL, NULL);
	VAR(SHOW_NUMERICS, BOOL, NULL);
	VAR(SHOW_STATUS_ALL, BOOL, update_all_status_wrapper);
	VAR(SHOW_WHO_HOPCOUNT, BOOL, NULL);
#define DEFAULT_SSL_CERTFILE NULL
	VAR(SSL_CERTFILE, STR,  NULL);
#define DEFAULT_SSL_KEYFILE NULL
	VAR(SSL_KEYFILE, STR,  NULL);
#define DEFAULT_SSL_PATH NULL
	VAR(SSL_PATH, STR,  NULL);
	VAR(STATUS_AWAY, STR,  build_status);
	VAR(STATUS_CHANNEL, STR,  build_status);
	VAR(STATUS_CHANOP, STR,  build_status);
	VAR(STATUS_CLOCK, STR,  build_status);
	VAR(STATUS_CPU_SAVER, STR,  build_status);
#define DEFAULT_STATUS_DOES_EXPANDOS 0
	VAR(STATUS_DOES_EXPANDOS, BOOL, NULL);
	VAR(STATUS_FORMAT, STR,  build_status);
	VAR(STATUS_FORMAT1, STR,  build_status);
	VAR(STATUS_FORMAT2, STR,  build_status);
	VAR(STATUS_HALFOP, STR,  build_status);
	VAR(STATUS_HOLD, STR,  build_status);
	VAR(STATUS_HOLD_LINES, STR,  build_status);
	VAR(STATUS_INSERT, STR,  build_status);
	VAR(STATUS_MAIL, STR,  build_status);
	VAR(STATUS_MODE, STR,  build_status);
#define DEFAULT_STATUS_NICKNAME DEFAULT_STATUS_NICK
	VAR(STATUS_NICKNAME, STR,  build_status);
	VAR(STATUS_NOSWAP, STR,  build_status);
	VAR(STATUS_NOTIFY, STR,  build_status);
	VAR(STATUS_NO_REPEAT, BOOL, build_status);
	VAR(STATUS_OPER, STR,  build_status);
	VAR(STATUS_OVERWRITE, STR,  build_status);
	VAR(STATUS_QUERY, STR,  build_status);
	VAR(STATUS_SCROLLBACK, STR,  build_status);
	VAR(STATUS_SERVER, STR,  build_status);
	VAR(STATUS_SSL_OFF, STR,  build_status);
	VAR(STATUS_SSL_ON, STR,  build_status);
	VAR(STATUS_TRUNCATE_RHS, BOOL, build_status);
	VAR(STATUS_UMODE, STR,  build_status);
#define DEFAULT_STATUS_USER0 DEFAULT_STATUS_USER
	VAR(STATUS_USER0, STR,  build_status);
	VAR(STATUS_USER1, STR,  build_status);
	VAR(STATUS_USER10, STR,  build_status);
	VAR(STATUS_USER11, STR,  build_status);
	VAR(STATUS_USER12, STR,  build_status);
	VAR(STATUS_USER13, STR,  build_status);
	VAR(STATUS_USER14, STR,  build_status);
	VAR(STATUS_USER15, STR,  build_status);
	VAR(STATUS_USER16, STR,  build_status);
	VAR(STATUS_USER17, STR,  build_status);
	VAR(STATUS_USER18, STR,  build_status);
	VAR(STATUS_USER19, STR,  build_status);
	VAR(STATUS_USER2, STR,  build_status);
	VAR(STATUS_USER20, STR,  build_status);
	VAR(STATUS_USER21, STR,  build_status);
	VAR(STATUS_USER22, STR,  build_status);
	VAR(STATUS_USER23, STR,  build_status);
	VAR(STATUS_USER24, STR,  build_status);
	VAR(STATUS_USER25, STR,  build_status);
	VAR(STATUS_USER26, STR,  build_status);
	VAR(STATUS_USER27, STR,  build_status);
	VAR(STATUS_USER28, STR,  build_status);
	VAR(STATUS_USER29, STR,  build_status);
	VAR(STATUS_USER3, STR,  build_status);
	VAR(STATUS_USER30, STR,  build_status);
	VAR(STATUS_USER31, STR,  build_status);
	VAR(STATUS_USER32, STR,  build_status);
	VAR(STATUS_USER33, STR,  build_status);
	VAR(STATUS_USER34, STR,  build_status);
	VAR(STATUS_USER35, STR,  build_status);
	VAR(STATUS_USER36, STR,  build_status);
	VAR(STATUS_USER37, STR,  build_status);
	VAR(STATUS_USER38, STR,  build_status);
	VAR(STATUS_USER39, STR,  build_status);
	VAR(STATUS_USER4, STR,  build_status);
	VAR(STATUS_USER5, STR,  build_status);
	VAR(STATUS_USER6, STR,  build_status);
	VAR(STATUS_USER7, STR,  build_status);
	VAR(STATUS_USER8, STR,  build_status);
	VAR(STATUS_USER9, STR,  build_status);
	VAR(STATUS_VOICE, STR,  build_status);
	VAR(STATUS_WINDOW, STR,  build_status);
	VAR(SUPPRESS_FROM_REMOTE_SERVER, BOOL, NULL);
	VAR(SWITCH_CHANNELS_BETWEEN_WINDOWS, BOOL, NULL);
	VAR(SWITCH_CHANNEL_ON_PART, BOOL, NULL);
	VAR(TAB, BOOL, NULL);
	VAR(TAB_MAX, INT,  NULL);
	VAR(TERM_DOES_BRIGHT_BLINK, BOOL, NULL);
#define DEFAULT_TRANSLATION NULL
	VAR(TRANSLATION, STR,  set_translation);
#define DEFAULT_TRANSLATION_PATH NULL
	VAR(TRANSLATION_PATH, STR,  NULL);
	VAR(UNDERLINE_VIDEO, BOOL, NULL);
	VAR(USER_INFORMATION, STR,  NULL);
	VAR(VERBOSE_CTCP, BOOL, NULL);
	VAR(WORD_BREAK, STR,  NULL);
#define DEFAULT_WSERV_PATH WSERV_PATH
	VAR(WSERV_PATH, STR,  NULL);
	VAR(WSERV_TYPE, STR,  set_wserv_type);
	VAR(XTERM, STR,  NULL);
	VAR(XTERM_OPTIONS, STR,  NULL);

	new_free(&s);
}

void 	init_variables_stage2 (void)
{
	int 	i;

	/*
	 * Forcibly init all the variables
	 */
	for (i = 0; i < var_bucket->numitems; i++)
	{
		IrcVariable *var = (IrcVariable *)var_bucket->list[i].stuff;

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

static void	show_var_value (const char *name, IrcVariable *var, int newval)
{
	char *value;

	value = make_string_var_bydata(var->type, (void *)var->data);

	if (!value)
		value = malloc_strdup("<EMPTY>");

	say("%s value of %s is %s", newval ? "New" : "Current", 
					name, value);
	new_free(&value);
}

/*
 * set_var_value: Given the variable structure and the string representation
 * of the value, this sets the value in the most verbose and error checking
 * of manors.  It displays the results of the set and executes the function
 * defined in the var structure 
 */
void 	set_var_value (int svv_index, const char *value, int noisy)
{
	IrcVariable *var;

	var = (IrcVariable *)var_bucket->list[svv_index].stuff;
	set_variable(var_bucket->list[svv_index].name, var, value, noisy);
}

/*
 * set_var_value: Given the variable structure and the string representation
 * of the value, this sets the value in the most verbose and error checking
 * of manors.  It displays the results of the set and executes the function
 * defined in the var structure 
 */
static void 	set_variable (const char *name, IrcVariable *var, const char *orig_value, int noisy)
{
	char	*rest;
	int	old;
	int	changed = 0;
	char	*value;

	if (orig_value)
		value = LOCAL_COPY(orig_value);
	else
		value = NULL;

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
			    say("Value of %s must be a single character", name);
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
			    say("Value of %s must be numeric!", name);
			else if ((val = my_atol(value)) < 0)
			    say("Value of %s must be a non-negative number", 
					name);
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
	    if ((var->func || var->script) && !(var->flags & VIF_PENDING))
	    {
		var->flags |= VIF_PENDING;
		if (var->func)
		    (var->func)(var->data);
		if (var->script)
		{
		    char *s;
		    s = make_string_var_bydata(var->type, (void *)var->data);
		    parse_line("SET", var->script, s, 0);
		    new_free(&s);
		}
		var->flags &= ~VIF_PENDING;
	    }
	}

	if (noisy)
	    show_var_value(name, var, changed);
}

static void	create_user_set (char *args)
{
	char *expr = NULL;
	char *typestr;
	int   type;
	char *varname;
	char *(*unused)(void);
	IrcVariable *var;

	varname = next_arg(args, &args);
	if (!varname || !*varname)
	{
		say("Usage: /SET -CREATE varname <TYPE> [{<code>}]");
		return;
	}

	while (args && *args && isspace(*args))
		args++;
	typestr = next_arg(args, &args);
	upper(typestr);

	if (typestr && !strcmp(typestr, "BOOL"))
		type = BOOL_VAR;
	else if (typestr && !strcmp(typestr, "STR"))
		type = STR_VAR;
	else if (typestr && !strcmp(typestr, "INT"))
		type = INT_VAR;
	else if (typestr && !strcmp(typestr, "CHAR"))
		type = CHAR_VAR;
	else
	{
		say("Usage: /SET -CREATE varname <TYPE> [{<code>}]");
		return;
	}

	while (args && *args && isspace(*args))
		args++;
	if (*args == '{')
	{
		expr = next_expr(&args, '{');
		if (!expr || !*expr)
		{
			say("Usage: /SET -CREATE varname <TYPE> [{<code>}]");
			return;
		}
	}

	upper(varname);

	get_var_alias(varname, &unused, &var);
	if (var != NULL && var->func)
	{
		say("Cannot replace builtin var %s with created var", varname);
		return;
	}
	else if (var != NULL && var->func == NULL)
	{
		delete_builtin_variable(varname);

		if (var->type == STR_VAR)
			new_free(&var->data->string);
		new_free(&var->data);
		if (var->script)
			new_free(&var->script);
		new_free(&var);
	}

	if (type == STR_VAR)
		add_biv(varname, 0, type, NULL, expr, (char *)NULL);
	else
		add_biv(varname, 0, type, NULL, expr, 0);

	say("Created new SET named \"%s\" of type %s ", varname, typestr);
}


/*
 * set_variable: The SET command sets one of the irc variables.  The args
 * should consist of "variable-name setting", where variable name can be
 * partial, but non-ambbiguous, and setting depends on the variable being set 
 */
BUILT_IN_COMMAND(setcmd)
{
	char	*var = NULL;
	IrcVariable *thevar;
	const char *name;
	int	i;
	Bucket	*b;

	/*
	 * XXX Ugh.  This is a hideous offense of good taste which is
	 * necessary to support set's abominable syntax, particularly
	 * acute with /set continued_line<space><space>
	 */
	while (args && *args && isspace(*args))
		args++;

	if (!my_strnicmp(args, "-create", 7))
	{
		next_arg(args, &args);
		create_user_set(args);
		return;
	}

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
		b = new_bucket();
		bucket_builtin_variables(b, var);

		if (b->numitems == 1 ||
		      (b->numitems > 1 && !my_stricmp(var, b->list[0].name)))
		{
			thevar = (IrcVariable *)b->list[0].stuff;
			name = b->list[0].name;
		}
		else
		{
			thevar = NULL;
			name = NULL;
		}

		if (!thevar || !(thevar->flags & VIF_PENDING))
		{
			if (thevar)
				thevar->flags |= VIF_PENDING;

			if (!do_hook(SET_LIST, "%s %s", 
					var, args ? args : "<unset>"))
				return;		/* Grabed -- stop. */

			if (name)
			{
			    if (!do_hook(SET_LIST, "%s %s",
					name, args ? args : "<unset>"))
				return;		/* Grabed -- stop. */
			}

			if (thevar)
				thevar->flags &= ~VIF_PENDING;
		}

		/* User didn't offer at it -- do the default thing. */
		if (thevar)
		{
			if (args && !*args)
				show_var_value(name, thevar, 0);
			else
				set_variable(name, thevar, args, 1);
			return;
		}

		if (b->numitems == 0)
		{
		    if (do_hook(SET_LIST, "set-error No such variable \"%s\"", 
					var))
			say("No such variable \"%s\"", var);
		    return;
		}

		else if (b->numitems > 1)
		{
		    if (do_hook(SET_LIST, "set-error %s is ambiguous", var))
		    {
			say("%s is ambiguous", var);
			for (i = 0; i < b->numitems; i++)
			    show_var_value(b->list[i].name, 
					(IrcVariable *)b->list[i].stuff, 0);
		    }
		}

		free_bucket(&b);
	}
	else
        {
		b = new_bucket();
		bucket_builtin_variables(b, empty_string);

		for (i = 0; i < b->numitems; i++)
		    show_var_value(b->list[i].name, 
				(IrcVariable *)b->list[i].stuff, 0);

		free_bucket(&b);
        }
}

/*
 * get_string_var: returns the value of the string variable given as an index
 * into the variable table.  Does no checking of variable types, etc 
 */
char *	get_string_var (int var)
{
	return ((IrcVariable *)var_bucket->list[var].stuff)->data->string;
}

/*
 * get_int_var: returns the value of the integer string given as an index
 * into the variable table.  Does no checking of variable types, etc 
 */
int 	get_int_var (int var)
{
	return ((IrcVariable *)var_bucket->list[var].stuff)->data->integer;
}

char 	*make_string_var (const char *var_name)
{
	char *	(*dummy) (void);
	IrcVariable *thevar = NULL;
	char	*copy;

	copy = LOCAL_COPY(var_name);
	upper(copy);

	get_var_alias(var_name, &dummy, &thevar);
	if (thevar == NULL)
		return NULL;

	return make_string_var_bydata(thevar->type, thevar->data);
}

char 	*make_string_var_bydata (int type, void *vp)
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


/***************************************************************************/
/* returns the size of the character set */
int 	charset_size (void)
{
	return get_int_var(EIGHT_BIT_CHARACTERS_VAR) ? 256 : 128;
}

static void 	eight_bit_characters (void *stuff)
{
	VARIABLE *v;
	int	value;

	v = (VARIABLE *)stuff;
	value = v->integer;
	if (value == ON && !term_eight_bit())
		say("Warning!  Your terminal says it does not support eight bit characters");
	set_term_eight_bit(value);
}

static void 	set_display_pc_characters (void *stuff)
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

static void	set_dcc_timeout (void *stuff)
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
			else if (!my_strnicmp(str2, "ALT_CHAR", 3))
				nvalue |= STRIP_ALT_CHAR;
			else if (!my_strnicmp(str2, "ALT_CHAR", 4))
				nvalue &= ~(STRIP_ALT_CHAR);
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

static	void	set_mangle_inbound (void *stuff)
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

static	void	set_mangle_outbound (void *stuff)
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

static	void	set_mangle_logfiles (void *stuff)
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

static void	update_all_status_wrapper (void *stuff)
{
	update_all_status();
}

static void    set_highlight_char (void *stuff)
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

static void    set_wserv_type (void *stuff)
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


/***************************************************************************/
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
	char *(*dummy) (void) = NULL;
	IrcVariable *var;

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
		get_var_alias(item->varname, &dummy, &var);
		set_variable(item->varname, var, item->value, 1);
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

