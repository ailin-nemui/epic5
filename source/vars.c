/*
 * vars.c: All the dealing of the irc variables are handled here. 
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright 1993, 2003 EPIC Software Labs.
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
#include "notify.h"
#include "vars.h"
#include "input.h"
#include "ircaux.h"
#include "termx.h"
#include "output.h"
#include "stack.h"
#include "dcc.h"
#include "keys.h"
#include "timer.h"
#include "clock.h"
#include "mail.h"
#include "reg.h"
#include "commands.h"
#include "ifcmd.h"
#include "ssl.h"


/*
 * About /SETs and BIVs
 * 
 * A /SET, is a persistent global variable.
 * A /SET has a type 
 *	Boolean -- ON or OFF
 *	String
 * 	Integer
 * 	Character
 * Much of the behavior of the client is controlled/configured by /SETs.
 *
 * Formally, /SETs are known as Built-In Variables ("BIVs") and you'll see that in 
 * alias.c in the unified symbol table.
 *
 * But in this file here, BIVs refer to those /SETs that are hardcoded in the client,
 * and not /SETs that are created by the user at runtime.   BIVs are created when the
 * client boots.
 *
 * Because symbol lookups (by string) are expensive, and because /SETs are used so 
 * extensively, BIVs have an optimization abstraction that lets C code get a /SET in 
 * O(1) time.
 * 
 * Most of the things in this file are related to BIV /SETs.  There is also the /SET
 * command, which applies to all SETs, builtin or hardcoded.
 *
 * You can always do a full name-to-object lookup with bucket_builtin_variables(),
 * but you can only do a O(1) lookup on BIVs.
 */

/*
 * This bucket contains ONLY THE HARDCODED /SETs
 * This is duplicative of the master /SET list maintained by the symbol table.
 * The routines that do O(1) mapping of /set's to (IrcVariable) objects
 * will use this bucket for their lookups.
 */
	Bucket *var_bucket = NULL;


static	void	set_mangle_inbound 	(void *);
static	void	set_mangle_outbound 	(void *);
static	void	set_mangle_logfiles 	(void *);
static	void	set_mangle_display	(void *);
static	void	update_all_status_wrapper (void *);
static	void	set_wserv_type		(void *);
static	void	set_indent		(void *);


/*
 * add_biv -- Create a Hard-coded /SET at client-startup.
 *
 * Arguments:
 *	name 	- The name of the /SET
 *	bucket 	- 1 to save the /SET in the optimization bucket, 0 if not
 *	type	- STR_VAR - the /SET can be set to a string
 *		  INT_VAR - The /SET can be set to an integer
 *		  CHAR_VAR - The /SET can be set to a single character
 *		  BOOL_VAR - The /SET can be set to OFF or ON
 *	func	- A C function to call back when the /SET is changed
 *		  This is used when you need to do something when the
 *		  value is changed, or you need to constrain/QA the value
 *	script	- Only NULL is supported.
 *	...	- The initial value of the /SET (usually a #define DEFAULT_ in config.h)
 *
 * Return value:
 *	> 0	If bucket == 1, the integer offset in the BIV bucket for this /SET
 *	-1	If bucket == 0, the new /SET was not saved in the bucket.
 *
 * It is clear this function was originally intended to support creating user sets,
 * but that should be done using $symbolctl() instead.
 * 
 * THIS IS AN INTERNAL FUNCTION - NOBODY SHOULD CALL IT EXCEPT THE BOOTSTRAP CODE
 * (init_variables_stage1())
 */
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
	var->pending = 0;

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

/* 
 * clone_biv -- Create a deep copy of a /SET's object
 * 
 * Arguments:
 * 	old	- Ptr to an existing/live (IrcVariable)
 *
 * Return value:
 *	A ptr to a new (IrcVariable) that is a deep copy of 'old'
 *	You can pass the return value to unclone_bov() later.
 *
 * This is used by /STACK PUSH SET <x> to create the copy of <x>
 * that gets pushed onto the stack for restoration later.
 *
 * This can be called on any /SET
 */
IrcVariable *	clone_biv (IrcVariable *old)
{
	IrcVariable *var;

	if (!old)
		return NULL;		/* Take THAT! */

	var = (IrcVariable *)new_malloc(sizeof(IrcVariable));
	var->type = old->type;
	if (old->script)
		var->script = malloc_strdup(old->script);
	else
		var->script = NULL;
	var->func = old->func;

	var->data = new_malloc(sizeof(union builtin_variable));
	var->pending = 0;

	switch (old->type) {
	    case BOOL_VAR:
	    case CHAR_VAR:
	    case INT_VAR:
		var->data->integer = old->data->integer;
		break;
	    case STR_VAR:
		if (old->data->string)
			var->data->string = malloc_strdup(old->data->string);
		else
			var->data->string = NULL;
		break;
	}

	return var;
}

/* 
 * unclone_biv -- Restore a deep copy of an (IrcVariable) object
 * 
 * Arguments:
 *	name	- The name of a /SET to be restored
 *	clone	- Ptr to (IrcVariable) previously returned by clone_biv()
 *
 * This is used by /STACK POP SET <x> to restore the copy of <x>
 * that was previously created by /STACK PUSH SET <x>.
 *
 * When this value gets reset, if the /SET has a callback, it is invoked
 *
 * This can be called on any /SET
 */
void	unclone_biv (const char *name, IrcVariable *clone)
{
	IrcVariable *var;
	int	i;

	for (i = 0; i < var_bucket->numitems; i++)
	{
	    if (!strcmp(name, var_bucket->list[i].name))
	    {
		var = (IrcVariable *)var_bucket->list[i].stuff;
		var->type = clone->type;
		if (clone->script)
		{
			malloc_strcpy(&var->script, clone->script);
			new_free(&clone->script);
		}
		else
			new_free(&var->script);

		var->pending = clone->pending;

		/*
		 * XXX This should be unified with set_variable() somehow.
		 */
		switch (clone->type) {
		    case BOOL_VAR:
		    case CHAR_VAR:
		    case INT_VAR:
			var->data->integer = clone->data->integer;
			break;
		    case STR_VAR:
			if (clone->data->string)
			    malloc_strcpy(&var->data->string, clone->data->string);
			else
			    new_free(&var->data->string);
			new_free(&clone->data->string);
			break;
		}

		/* 
		 * XXX I copied this from set_variable(), but this
		 * should be refactored and shared with that function.
		 */
		if ((var->func || var->script) && !var->pending)
		{
			var->pending++;
			if (var->func)
			    (var->func)(var->data);
			if (var->script)
			{
			    char *s;
			    int owd = window_display;

			    s = make_string_var_bydata((const void *)var);
			    window_display = 0;
			    call_lambda_command("SET", var->script, s);
			    window_display = owd;
			    new_free(&s);
			}
			var->pending--;
		}


		new_free(&clone->data);
		new_free(&clone);
		return;
	     }
	}
}

/*
 * is_var_builtin -- Is a /SET a BIV, or is it user-created?
 *
 * Arguments:
 *	varname	- The name of a /SET
 *
 * Return value:
 *	0 - The named /SET is not a BIV, either:
 *		- because it does not exist, or
 *		- because it is a user-created /SET
 *	1 - The named /SET is a BIV and must not be deleted
 *
 * This is used by $symbolctl() to determine if a /SET can be deleted.
 * XXX Perhaps this info should be stored in the (IrcVariable)
 *
 * This can be called on any /SET.
 */
int	is_var_builtin (const char *varname)
{
	int	i;

	for (i = 0; i < var_bucket->numitems; i++)
	{
		if (!my_stricmp(var_bucket->list[i].name, varname))
			return 1;
	}
	return 0;
}

#define VAR(x, y, z) x ## _VAR = add_biv( #x, 1, y ## _VAR, z,NULL, DEFAULT_ ## x);

/*
 * init_variables -- Bootstrap the Built In Variables (BIV) /SETs during client bootup
 *
 * When the client is bootstraping itself, it needs to create all the built-in-variable
 * /SETs so things can start using them.   This is done before parsing command line 
 * arguments, since you can change /SETs that way
 *
 * The default values for /SETs are #defined in include/config.h -- mostly.
 * For those things not defined in config.h, a hardcoded default value is found here!
 *
 * This can only be called once, at client boostrap time.
 */
void 	init_variables_stage1 (void)
{
	var_bucket = new_bucket();

	VAR(ACCEPT_INVALID_SSL_CERT,	BOOL, NULL)
	VAR(ALLOW_C1_CHARS, 		BOOL, NULL)
	VAR(ALWAYS_SPLIT_BIGGEST, 	BOOL, NULL)
	VAR(BANNER, 			STR,  NULL)
	VAR(BANNER_EXPAND, 		BOOL, NULL)
	VAR(BEEP, 			BOOL, NULL)
	VAR(CHANNEL_NAME_WIDTH, 	INT,  update_all_status_wrapper)
#define DEFAULT_CLIENT_INFORMATION IRCII_COMMENT
	VAR(CLIENT_INFORMATION, 	STR,  NULL)
	VAR(CLOCK, 			BOOL, my_set_clock);
	VAR(CLOCK_24HOUR, 		BOOL, reset_clock);
	VAR(CLOCK_FORMAT, 		STR,  set_clock_format);
	VAR(CLOCK_INTERVAL, 		INT,  set_clock_interval);
	VAR(CMDCHARS, 			STR,  NULL);
	VAR(COMMENT_HACK, 		BOOL, NULL);
	VAR(CONTINUED_LINE, 		STR,  NULL);
	VAR(CPU_SAVER_AFTER, 		INT,  set_cpu_saver_after);
	VAR(CPU_SAVER_EVERY, 		INT,  set_cpu_saver_every);
	VAR(CURRENT_WINDOW_LEVEL, 	STR,  set_current_window_mask);
	VAR(DCC_AUTO_SEND_REJECTS, 	BOOL, NULL);
	VAR(DCC_CONNECT_TIMEOUT,	INT,  NULL);
	VAR(DCC_DEQUOTE_FILENAMES, 	BOOL, NULL);
	VAR(DCC_LONG_PATHNAMES, 	BOOL, NULL);
	VAR(DCC_SLIDING_WINDOW, 	INT,  NULL);
	VAR(DCC_STORE_PATH, 		STR,  NULL);
	VAR(DCC_USE_GATEWAY_ADDR, 	BOOL, NULL)
#define DEFAULT_DEBUG 0
	VAR(DEBUG, 			INT,  NULL);
#define DEFAULT_DEFAULT_REALNAME NULL
	VAR(DEFAULT_REALNAME, 		STR,  NULL);
#define DEFAULT_DEFAULT_USERNAME NULL
	VAR(DEFAULT_USERNAME, 		STR,  NULL);
	VAR(DISPATCH_UNKNOWN_COMMANDS,	BOOL, NULL);
	VAR(DISPLAY, 			BOOL, NULL);
	VAR(DO_NOTIFY_IMMEDIATELY, 	BOOL, NULL);
	VAR(FIRST_LINE,			STR,  NULL);
	VAR(FLOATING_POINT_MATH, 	BOOL, NULL);
	VAR(FLOATING_POINT_PRECISION,	INT,  NULL);
	VAR(FLOOD_AFTER, 		INT,  NULL);
	VAR(FLOOD_IGNORE, 		BOOL, NULL);
	VAR(FLOOD_MASKUSER,		INT,  NULL);
	VAR(FLOOD_RATE,			INT,  NULL);
	VAR(FLOOD_RATE_PER,		INT,  NULL);
	VAR(FLOOD_USERS,		INT,  NULL);
	VAR(FLOOD_WARNING,		BOOL, NULL);
	VAR(HIDE_PRIVATE_CHANNELS,	BOOL, update_all_status_wrapper);
	VAR(HOLD_SLIDER,		INT,  NULL);
	VAR(INDENT,			BOOL, set_indent);
        VAR(INPUT_INDICATOR_LEFT,	STR,  NULL);
        VAR(INPUT_INDICATOR_RIGHT,	STR,  NULL);
        VAR(INPUT_PROMPT,		STR,  set_input_prompt);
	VAR(INSERT_MODE,		BOOL, update_all_status_wrapper);
	VAR(KEY_INTERVAL,		INT,  set_key_interval);
	VAR(LASTLOG, 			INT,  set_lastlog_size);
	VAR(LASTLOG_LEVEL,		STR,  set_lastlog_mask);
	VAR(LASTLOG_REWRITE,		STR,  NULL);
#define DEFAULT_LOAD_PATH NULL
	VAR(LOAD_PATH,			STR,  NULL);
	VAR(LOG,			BOOL, logger);
	VAR(LOGFILE,			STR,  set_logfile);
#define DEFAULT_LOG_REWRITE NULL
	VAR(LOG_REWRITE,		STR,  NULL);
	VAR(MAIL,			INT,  set_mail);
	VAR(MAIL_INTERVAL,		INT,  set_mail_interval);
	VAR(MAIL_TYPE,			STR,  set_mail_type);
#define DEFAULT_MANGLE_DISPLAY "NORMALIZE"
	VAR(MANGLE_DISPLAY,		STR,  set_mangle_display);
#define DEFAULT_MANGLE_INBOUND NULL
	VAR(MANGLE_INBOUND,		STR,  set_mangle_inbound);
#define DEFAULT_MANGLE_LOGFILES NULL
	VAR(MANGLE_LOGFILES,		STR,  set_mangle_logfiles);
#define DEFAULT_MANGLE_OUTBOUND NULL
	VAR(MANGLE_OUTBOUND,		STR,  set_mangle_outbound);
	VAR(METRIC_TIME,		BOOL, reset_clock);
	VAR(MIRC_BROKEN_DCC_RESUME,	BOOL, NULL);
	VAR(MODE_STRIPPER,		BOOL, NULL);
	VAR(NEW_SERVER_LASTLOG_LEVEL,	STR,  set_new_server_lastlog_mask);
	VAR(NOTIFY,			BOOL, set_notify);
	VAR(NOTIFY_INTERVAL,		INT,  set_notify_interval);
	VAR(NOTIFY_LEVEL,		STR,  set_notify_mask);
	VAR(NOTIFY_ON_TERMINATION,	BOOL, NULL);
	VAR(NOTIFY_USERHOST_AUTOMATIC,	BOOL, NULL);
	VAR(NO_CONTROL_LOG,		BOOL, NULL);	/* XXX /set mangle_logfile */
	VAR(NO_CTCP_FLOOD,              BOOL, NULL);
	VAR(NO_FAIL_DISCONNECT,         BOOL, NULL);
	VAR(OLD_MATH_PARSER,            BOOL, NULL);
	VAR(OLD_SERVER_LASTLOG_LEVEL,   STR,  set_old_server_lastlog_mask);
#define DEFAULT_OUTPUT_REWRITE NULL
	VAR(OUTPUT_REWRITE,             STR,  NULL);
	VAR(PAD_CHAR,                   CHAR, NULL);
	VAR(QUIT_MESSAGE,               STR,  NULL);
	VAR(RANDOM_SOURCE,              INT,  NULL);
	VAR(SCREEN_OPTIONS,             STR,  NULL);
	VAR(SCROLLBACK,                 INT,  set_scrollback_size);
	VAR(SCROLLBACK_RATIO,           INT,  NULL);
	VAR(SCROLL_LINES,               INT,  set_scroll_lines);
	VAR(SHELL,                      STR,  NULL);
	VAR(SHELL_FLAGS,                STR,  NULL);
	VAR(SHELL_LIMIT,                INT,  NULL);
	VAR(SHOW_CHANNEL_NAMES,         BOOL, NULL);
	VAR(SHOW_NUMERICS,              BOOL, NULL);
	VAR(SHOW_STATUS_ALL,            BOOL, update_all_status_wrapper);
#ifdef HAVE_SSL
	VAR(SSL_ROOT_CERTS_LOCATION,    STR, set_ssl_root_certs_location);
#endif
	VAR(STATUS_AWAY,                STR,  build_status);
	VAR(STATUS_CHANNEL,             STR,  build_status);
	VAR(STATUS_CHANOP,              STR,  build_status);
	VAR(STATUS_CLOCK,               STR,  build_status);
	VAR(STATUS_CPU_SAVER,           STR,  build_status);
#define DEFAULT_STATUS_DOES_EXPANDOS 0
	VAR(STATUS_DOES_EXPANDOS,       BOOL, NULL);
	VAR(STATUS_FORMAT,              STR,  build_status);
	VAR(STATUS_FORMAT1,             STR,  build_status);
	VAR(STATUS_FORMAT2,             STR,  build_status);
	VAR(STATUS_HALFOP,              STR,  build_status);
	VAR(STATUS_HOLD,                STR,  build_status);
	VAR(STATUS_HOLD_LINES,          STR,  build_status);
	VAR(STATUS_HOLDMODE,            STR,  build_status);
	VAR(STATUS_INSERT,              STR,  build_status);
	VAR(STATUS_MAIL,                STR,  build_status);
	VAR(STATUS_MODE,                STR,  build_status);
	VAR(STATUS_NICKNAME,            STR,  build_status);
	VAR(STATUS_NOSWAP,              STR,  build_status);
	VAR(STATUS_NOTIFY,              STR,  build_status);
	VAR(STATUS_NO_REPEAT,           BOOL, build_status);
	VAR(STATUS_OPER,                STR,  build_status);
	VAR(STATUS_OVERWRITE,           STR,  build_status);
	VAR(STATUS_PREFIX_WHEN_CURRENT, STR,  build_status);
	VAR(STATUS_PREFIX_WHEN_NOT_CURRENT, STR,  build_status);
	VAR(STATUS_QUERY,               STR,  build_status);
	VAR(STATUS_SCROLLBACK,          STR,  build_status);
	VAR(STATUS_SERVER,              STR,  build_status);
	VAR(STATUS_SSL_OFF,             STR,  build_status);
	VAR(STATUS_SSL_ON,              STR,  build_status);
	VAR(STATUS_UMODE,               STR,  build_status);
	VAR(STATUS_USER,                STR,  build_status);
	VAR(STATUS_USER1,               STR,  build_status);
	VAR(STATUS_USER10,              STR,  build_status);
	VAR(STATUS_USER11,              STR,  build_status);
	VAR(STATUS_USER12,              STR,  build_status);
	VAR(STATUS_USER13,              STR,  build_status);
	VAR(STATUS_USER14,              STR,  build_status);
	VAR(STATUS_USER15,              STR,  build_status);
	VAR(STATUS_USER16,              STR,  build_status);
	VAR(STATUS_USER17,              STR,  build_status);
	VAR(STATUS_USER18,              STR,  build_status);
	VAR(STATUS_USER19,              STR,  build_status);
	VAR(STATUS_USER2,               STR,  build_status);
	VAR(STATUS_USER20,              STR,  build_status);
	VAR(STATUS_USER21,              STR,  build_status);
	VAR(STATUS_USER22,              STR,  build_status);
	VAR(STATUS_USER23,              STR,  build_status);
	VAR(STATUS_USER24,              STR,  build_status);
	VAR(STATUS_USER25,              STR,  build_status);
	VAR(STATUS_USER26,              STR,  build_status);
	VAR(STATUS_USER27,              STR,  build_status);
	VAR(STATUS_USER28,              STR,  build_status);
	VAR(STATUS_USER29,              STR,  build_status);
	VAR(STATUS_USER3,               STR,  build_status);
	VAR(STATUS_USER30,              STR,  build_status);
	VAR(STATUS_USER31,              STR,  build_status);
	VAR(STATUS_USER32,              STR,  build_status);
	VAR(STATUS_USER33,              STR,  build_status);
	VAR(STATUS_USER34,              STR,  build_status);
	VAR(STATUS_USER35,              STR,  build_status);
	VAR(STATUS_USER36,              STR,  build_status);
	VAR(STATUS_USER37,              STR,  build_status);
	VAR(STATUS_USER38,              STR,  build_status);
	VAR(STATUS_USER39,              STR,  build_status);
	VAR(STATUS_USER4,               STR,  build_status);
	VAR(STATUS_USER5,               STR,  build_status);
	VAR(STATUS_USER6,               STR,  build_status);
	VAR(STATUS_USER7,               STR,  build_status);
	VAR(STATUS_USER8,               STR,  build_status);
	VAR(STATUS_USER9,               STR,  build_status);
	VAR(STATUS_VOICE,               STR,  build_status);
	VAR(STATUS_WINDOW,              STR,  build_status);
	VAR(SUPPRESS_FROM_REMOTE_SERVER, BOOL, NULL);
	VAR(SWITCH_CHANNELS_BETWEEN_WINDOWS,  BOOL, NULL);
	VAR(TERM_DOES_BRIGHT_BLINK,     BOOL, NULL);
	VAR(TMUX_OPTIONS,               STR,  NULL);
	VAR(USER_INFORMATION,           STR,  NULL);
	VAR(WORD_BREAK,                 STR,  NULL);
#define DEFAULT_WSERV_PATH WSERV_PATH
	VAR(WSERV_PATH,                 STR,  NULL);
	VAR(WSERV_TYPE,                 STR,  set_wserv_type);
	VAR(XTERM,                      STR,  NULL);
	VAR(XTERM_OPTIONS,              STR,  NULL);
}

/*
 * init_variables_stage2 -- Complete the initialization of all BIV /SETs
 *
 * During the early part of the client bootstrap process, we created 
 * the BIV /SETs.  But that's all we did, we just created them.  
 * Many /SETs bootstrap themselves through a callback function, which 
 * we haven't called yet.   This function goes through and calls the
 * callback function for every /SET with its initial value.
 *
 * This can only be called once, at client boostrap time.
 */
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
			/*
			 * We don't call build_status() or update_all_status() here, 
			 * because there is still more bootstrap that needs to be 
			 * done before the status bar is ready to go.  So the bootstrap 
			 * code calls build_status() for the first time after we return.
			 */
			if (var->func == build_status)
				continue;
			if (var->func == update_all_status_wrapper)
				continue;

			var->pending++;
			var->func(var->data);
			var->pending--;
		}
	}
}

/*
 * do_boolean -- determine whether a string contains a boolean value and set it
 *
 * Arguments:
 *	str	- A string that should contain "ON", "OFF", or "TOGGLE"
 *	value	- A pointer to an existing boolean value.  It should be 0 or 1.
 *
 * Return value:
 *	0 - The operation completed successfully
 * 		If str is "ON", then *value is set to 1.
 * 		If str is "OFF", then *value is set to 0.
 * 		If str is "TOGGLE", then *value is set to the opposte of whatever it is 
 *	1 - The operation failed (str did not contain a value boolean value)
 *		*value is unchanged.
 *
 * XXX This is not /SET specific; it belongs somewhere else.
 */
int 	do_boolean (char *str, int *value)
{
	upper(str);
	if (strcmp(str, "ON") == 0)
		*value = 1;
	else if (strcmp(str, "OFF") == 0)
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
 * show_var_value -- Convert a /SET's value to something human-readable
 *
 * Arguments:
 *	name	- The name of a /SET 
 *	var	- Ptr to the /SET's (IrcVariable) object
 *	newval	- Whether this /SET is being changed or just read out
 *
 * This can be used for any /SET
 */
static void	show_var_value (const char *name, IrcVariable *var, int newval)
{
	char *value;

	value = make_string_var_bydata((const void *)var);

	if (!value)
		value = malloc_strdup("<EMPTY>");

	say("%s value of %s is %s", newval ? "New" : "Current", 
					name, value);
	new_free(&value);
}

/*
 * set_var_value - A wrapper for changing a BIV /SET
 *
 * Arguments:
 *	svv_index - The index of the BIV 
 *	value	  - The value the /SET should be changed to
 *	noisy	  - 1 if the user should be told, 0 if not.
 *
 * You can only call this for a BIV /SET.
 */
void 	set_var_value (int svv_index, const char *value, int noisy)
{
	IrcVariable *var;

	var = (IrcVariable *)var_bucket->list[svv_index].stuff;
	set_variable(var_bucket->list[svv_index].name, var, value, noisy);
}

/*
 * set_variable -- Change a /SET's value
 *
 * Arguments:
 *	name	  - The name of the /SET being changed
 *	var	  - The (IrcVariable) for that /SET
 *	new_value - The value the /SET should be changed to
 *	noisy	  - 1 if the user should be told, 0 if not.
 *
 * Return value:
 *	 0	- The new value was set successfully.
 *	-1	- Something went wrong; the /SET was not changed.
 *
 * You can call this for any /SET.
 * However, you have already looked up the 'var' yourself.   
 *    I recommend bucket_builtin_variables().
 */
int 	set_variable (const char *name, IrcVariable *var, const char *new_value, int noisy)
{
	char	*rest;
	int	changed = 0;
	unsigned char	*value;
	int	retval = 0;

	if (new_value)
		value = LOCAL_COPY(new_value);
	else
		value = NULL;

	switch (var->type)
	{
	    case BOOL_VAR:
	    {
		if (value && *value && (value = next_arg(value, &rest)))
		{
			if (do_boolean(value, &(var->data->integer))) {
			    say("Value must be either ON, OFF, or TOGGLE");
			    retval = -1;
			}
			else
			    changed = 1;
		}
		break;
	    }

	    case CHAR_VAR:
	    {
		int	codepoint;

		if (!value || !*value)
		{
			var->data->integer = ' ';
			changed = 1;
			break;
		}

		if ((codepoint = next_code_point((const unsigned char **)&value, 0)) == -1)
		{
			say("New value of %s could not be determined", name);
			retval = -1;
			break;
		}

		if (codepoint_numcolumns(codepoint) != 1)
		{
			say("New value of %s must be exactly 1 column wide", name);
			retval = -1;
			break;
		}

		var->data->integer = codepoint;
		changed = 1;
		break;
	    }

	    case INT_VAR:
	    {
		if (value && *value && (value = next_arg(value, &rest)))
		{
			int	val;

			if (!is_number(value)) {
			    say("Value of %s must be numeric!", name);
			    retval = -1;
			} else if ((val = my_atol(value)) < 0) {
			    say("Value of %s must be a non-negative number", name);
			    retval = -1;
			} else {
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
	    if ((var->func || var->script) && !var->pending)
	    {
		var->pending++;
		if (var->func)
		    (var->func)(var->data);
		if (var->script)
		{
		    char *s;
		    int owd = window_display;

		    s = make_string_var_bydata((const void *)var);
		    window_display = 0;
		    call_lambda_command("SET", var->script, s);
		    window_display = owd;
		    new_free(&s);
		}
		var->pending--;
	    }
	}

	if (noisy)
	    show_var_value(name, var, changed);

	return retval;
}

/*
 * setcmd: The SET command sets one of the irc variables.  The args
 * should consist of "variable-name setting", where variable name can be
 * partial, but non-ambbiguous, and setting depends on the variable being set 
 */
BUILT_IN_COMMAND(setcmd)
{
	char	*var = NULL;
	IrcVariable *thevar;
	const char *name;
	int	i;
	Bucket	*b = NULL;

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

	/*
	 * Show all of the /SET values if no argument was given.
	 */
	if (!var || !*var)
        {
		b = new_bucket();
		bucket_builtin_variables(b, empty_string);

		for (i = 0; i < b->numitems; i++)
		    show_var_value(b->list[i].name, 
				(IrcVariable *)b->list[i].stuff, 0);

		free_bucket(&b);
		return;
        }

	/*
	 * We're committed to changing a /SET value here.
	 */

	/*
	 * If the name is prefixed with a hyphen, it unsets the value.
	 */
	if (*var == '-')
	{
		var++;
		args = NULL;
	}

	/*
	 * How many /SET VARiables are matched by what the user typed? 
	 */
	upper(var);
	b = new_bucket();
	bucket_builtin_variables(b, var);

	/*
	 * Decide if this is an "exact" match or an "uknonwn" match.
	 */

	/* Either exactly correct, or only one possible match */
	if (b->numitems == 1 ||
	      (b->numitems > 1 && !my_stricmp(var, b->list[0].name)))
	{
		thevar = (IrcVariable *)b->list[0].stuff;
		name = b->list[0].name;
	}
	/* Either zero or two-or-more matches */
	else
	{
		thevar = NULL;
		name = NULL;
	}

	/*
	 * STEP 1 -- We would Allow the user to overrule the /SET via /ON SET.
	 * You can /SET a new value in the /ON SET!
	 */
	if (!thevar || !thevar->pending)
	{
		if (thevar)
			thevar->pending++;

		/*
		 * Offer exactly what the user asked for
		 */
		if (!do_hook(SET_LIST, "%s %s", 
				var, args ? args : "<unset>"))
		{
			free_bucket(&b);
			if (thevar)
				thevar->pending--;
			return;		/* Grabbed -- stop. */
		}

		/*
		 * Offer the fully complete name, but only
		 * if that's not what the user asked for!
		 */
		if (name && my_stricmp(name, var))
		{
		    if (!do_hook(SET_LIST, "%s %s",
				name, args ? args : "<unset>"))
		    {
			free_bucket(&b);
			if (thevar)
				thevar->pending--;
			return;		/* Grabbed -- stop. */
		    }
		}

		if (thevar)
			thevar->pending--;
	}

	/*
	 * STEP 2 -- The user /SET a valid variable, but the user didn't grab it
	 * via an /ON SET above.
	 */
	if (thevar)
	{
		/* Either set it, or display it, depending on the args */
		if (args && !*args)
			show_var_value(name, thevar, 0);
		else
			set_variable(name, thevar, args, 1);

		free_bucket(&b);
		return;
	}

	/*
	 * If there was no match at all (as opposed to two-or-more), then 
	 * also throw /on unknown_set.
	 */
	if (b->numitems == 0)
	{
	    if (do_hook(UNKNOWN_SET_LIST, "%s %s", 
				var, args ? args : "<unset>"))
	      if (do_hook(SET_LIST, "set-error No such variable \"%s\"",
				var))
		say("No such variable \"%s\"", var);
	    free_bucket(&b);
	    return;
	}

	/*
	 * If there was two-or-more matches (ambiguous), then also throw
	 * /on set to tell the user it was ambiguous.
	 */
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

/*
 * get_string_var -- Get the value of a BIV using its index
 *
 * Arguments:
 *	var - An integer representing the offset of a BIV
 *	      It must be a string typed /SET!
 *
 * Return value:
 *	The value of that /SET as an string
 *
 * IMPORTANT: This O(1) function only works for /SETs that are hardcoded.
 * You need to call make_string_var2() to look up user-created /SETs
 *
 * IMPORTANT:  This does not do any type checking.  If you call get_string_var()
 * against a /SET that is not an integer /SET, you will get garbage!
 * 
 * IMPORTANT: The caller DOES NOT OWN the return value.  YOU MUST NOT 
 * new_free() IT.  YOU MUST NOT CHANGE IT.  
 */
const char *	get_string_var (int var)
{
	return ((IrcVariable *)var_bucket->list[var].stuff)->data->string;
}

/*
 * get_int_var -- Get the value of a BIV using its index
 *
 * Arguments:
 *	var - An integer representing the offset of a BIV
 *	      It must be an integer typed /SET!
 *
 * Return value:
 *	The value of that /SET as an integer
 *
 * IMPORTANT: This O(1) function only works for /SETs that are hardcoded.
 * You need to call make_string_var2() to look up user-created /SETs
 *
 * IMPORTANT:  This does not do any type checking.  If you call get_int_var()
 * against a /SET that is not an integer /SET, you will get garbage!
 */
int 	get_int_var (int var)
{
	return ((IrcVariable *)var_bucket->list[var].stuff)->data->integer;
}

/*
 * make_string_var -- Get the current value of any /SET by name
 *
 * Arguments:
 *	var_name - The name of a /SET -- Must be in canonical format 
 *
 * Return Value:
 *	NULL 	- The requested /SET does not exist,
 *		  OR, the /SET does not have a value
 *	_	- A new string containing a human-readable version of the /SET
 *		  THE RETURN VALUE IS MALLOC()ed -- THE CALLER MUST NEW_FREE() it.
 *
 *
 * You can call this against any /SET
 *
 * Because 'var_name' is looked up literally as a symbol, it must be in
 * canonical format (dots, not []s) and uppercase.  This function will 
 * uppercase what you pass it as a courtesy.
 */
char *	make_string_var (const char *var_name)
{
	char *	(*dummy) (void);
	IrcVariable *thevar = NULL;
	char	*copy;

	copy = LOCAL_COPY(var_name);
	upper(copy);

	get_var_alias(copy, &dummy, &thevar);
	if (thevar == NULL)
		return NULL;

	return make_string_var_bydata((const void *)thevar);
}

/*
 * make_string_var2 -- Get the current value of any /SET by name with error code
 *
 * Arguments:
 *	var_name - The name of a /SET -- Must be in canonical format 
 *	retval	 - A ptr to a (char *) where we will return the string
 *
 * Return Value:
 *	-1	- The requested /SET does not exist
 *	 0	- A new string containing a human-readable version of the /SET
 *		  has been placed in *retval.  YOU MUST NEW_FREE() THE STRING
 *		  PLACED IN *retval.
 *
 * You can call this against any /SET
 *
 * Because 'var_name' is looked up literally as a symbol, it must be in
 * canonical format (dots, not []s) and uppercase.  This function will 
 * uppercase what you pass it as a courtesy.
 */
int	make_string_var2 (const char *var_name, char **retval)
{
	char *	(*dummy) (void);
	IrcVariable *thevar = NULL;
	char	*copy;

	copy = LOCAL_COPY(var_name);
	upper(copy);

	get_var_alias(copy, &dummy, &thevar);
	if (thevar == NULL)
		return -1;

	*retval = make_string_var_bydata((const void *)thevar);
	return 0;
}

/*
 * make_string_var_bydata -- Make a new string representing the value of an (IrcVariable)
 *
 * Arguments:
 *	irc_variable	- A ptr to an (IrcVariable) object
 *
 * Return value:
 *	A new string containing a human-readable version of the value in irc_variable.
 *	THE RETURN VALUE IS MALLOC()ed -- THE CALLER MUST NEW_FREE() it.
 *
 * You can call this against any /SET
 */
char *	make_string_var_bydata (const void *irc_variable)
{
	int	type;
const 	VARIABLE *data;
	char	*ret = (char *) 0;

	type = ((const IrcVariable *)irc_variable)->type;
	data = (const VARIABLE *)(((const IrcVariable *)irc_variable)->data);

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
			if (data->integer)
				ret = malloc_strdup("ON");
			else
				ret = malloc_strdup("OFF");
			break;
		case CHAR_VAR:
		{
			char utf8str[16];

			ucs_to_utf8(data->integer, utf8str, sizeof(utf8str));
			ret = malloc_strdup(utf8str);
			break;
		}
		default:
			panic(1, "make_string_var_bydata: unrecognized type [%d]", type);
	}
	return (ret);

}


/***************************************************************************/
/*
 * set_mangle_inbound -- update 'inbound_line_mangler' from /SET MANGLE_INBOUND
 */
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

/*
 * set_mangle_outbound -- update 'outbound_line_mangler' from /SET MANGLE_OUTBOUND
 */
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

/*
 * set_mangle_logfiles -- update 'logfile_line_mangler' from /SET MANGLE_LOGFILES
 */
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

/*
 * set_mangle_display - update 'display_line_mangler' from /SET MANGLE_DISPLAY
 */
static	void	set_mangle_display (void *stuff)
{
	VARIABLE *v;
	const char *value;
	char *nv = NULL;

	v = (VARIABLE *)stuff;
	value = v->string;

	display_line_mangler = parse_mangle(value, display_line_mangler, &nv);
	malloc_strcpy(&v->string, nv);
	new_free(&nv);
}

/*
 * update_all_status_wrapper - call update_all_status() from a /SET callback
 * 
 * Several /SETs change what appears on the status bar, so update_all_status()
 * recalculates the status bar and updates it
 */
static void	update_all_status_wrapper (void *stuff)
{
	update_all_status();
}

/*
 * set_wserv_type -- callback for /SET WSERV_TYPE
 *
 * This constrains the value of /SET WSERV_TYPE to the supported values
 */
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
	if (!my_stricmp(s, "TMUX"))
		return;		/* It's ok */

	say("SET WSERV_TYPE must be either SCREEN, XTERM or TMUX");
	new_free(&v->string);
}

/*
 * set_indent -- callback for /SET INDENT
 *
 * /SET INDENT is a wrapper around /WINDOW INDENT on every window.
 *
 * XXX Normally, these kinds of /SETs use the /WINDOW value if the user
 *     set one, and the /SET as a fallback.  WHy is this different?
 */
void    set_indent (void *stuff)
{
        VARIABLE *v;
        int     indent;
        Window  *window = NULL;
 
        v = (VARIABLE *)stuff;
        indent = v->integer;

        while (traverse_all_windows(&window))
                window->indent = indent;
}


/***************************************************************************/
#if 0
void    help_topics_set (FILE *f)
{                                                                               
        int     i;

	for (i = 0; i < var_bucket->numitems; i++)
                fprintf(f, "set %s\n", var_bucket->list[i].name);
}
#endif

