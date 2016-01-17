/*
 * if.c: the IF, WHILE, FOREACH, DO, FE, FEC, and FOR commands for IRCII 
 *
 * Copyright (c) 1991, 1994 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew R. Green.
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

#include "irc.h"
#include "alias.h"
#include "ifcmd.h"
#include "ircaux.h"
#include "output.h"
#include "parse.h"
#include "vars.h"
#include "commands.h"
#include "window.h"
#include "reg.h"

/*
 * next_expr finds the next expression delimited by brackets. The type
 * of bracket expected is passed as a parameter. Returns NULL on error.
 *
 * YOU MUST CHECK FOR AND PROPERLY HANDLE A NULL ON RETURN -- 
 * PASSING NULL TO parse_line() or parse_arglist() OR SIMILAR FUNCTIONS
 * WILL RETURN IN A PANIC.  DO NOT IGNORE THIS!
 */
static __inline__
char *	my_next_expr (char **args, char type, int whine, int wantchar)
{
	char	*expr_start,
		*expr_end;
	ssize_t	span;

	if (!*args || !**args || **args != type)
	{
		if (whine)
			say("Expression syntax");
		return NULL;
	}

	/* Find the end of the expression and terminate it. */
	expr_start = *args;
	if ((span = MatchingBracket(expr_start + 1, type, 
					(type == '(') ? ')' : '}')) < 0)
	{
		if (whine)
			say("Unmatched '%c' around [%-.20s]", 
				type, expr_start + 1);
		return NULL;
	}
	else
		expr_end = expr_start + 1 + span;

	*expr_end = 0;

	/* 
	 * Reset the input string to non-whitespace after the expression.
	 * "expr_end + 1" is safe here -- "expr_end" points at where the
	 * old } or ) was, and there is at the very least a nul character
	 * after that.
	 */
	*args = skip_spaces(expr_end + 1);

	/* Remove any extraneous whitespace in the expression */
	expr_start = skip_spaces(expr_start + 1);
	remove_trailing_spaces(expr_start, 0);

	/*
	 * It is guaranteed that (ptr2[-1] >= *args) and so it is further
	 * guaranteed that assigning to ptr2[-1] is valid.  So we will stick
	 * the type of expression there just in case anyone wants it.
	 */
	if (wantchar)
		*--expr_start = type;

	return expr_start;
}

/*
 * YOU MUST CHECK FOR AND PROPERLY HANDLE A NULL ON RETURN -- 
 * PASSING NULL TO parse_line() or parse_arglist() OR SIMILAR FUNCTIONS
 * WILL RETURN IN A PANIC.  DO NOT IGNORE THIS!
 */
char *	next_expr_with_type (char **args, char type)
{
	return my_next_expr (args, type, 0, 1);
}

/*
 * YOU MUST CHECK FOR AND PROPERLY HANDLE A NULL ON RETURN -- 
 * PASSING NULL TO parse_line() or parse_arglist() OR SIMILAR FUNCTIONS
 * WILL RETURN IN A PANIC.  DO NOT IGNORE THIS!
 */
char *	next_expr_failok (char **args, char type)
{
	return my_next_expr (args, type, 0, 0);
}

/*
 * YOU MUST CHECK FOR AND PROPERLY HANDLE A NULL ON RETURN -- 
 * PASSING NULL TO parse_line() or parse_arglist() OR SIMILAR FUNCTIONS 
 * WILL RETURN IN A PANIC.  DO NOT IGNORE THIS!
 */
char *	next_expr (char **args, char type)
{
	return my_next_expr (args, type, 1, 0);
}


/*
 * All new /if command -- (jfn, 1997)
 *
 * Here's the plan:
 *
 *		if (expr) ......
 *		if (expr) {......}
 *		if (expr) {......} {......}
 *		if (expr) {......} else {......}
 *		if (expr) {......} elif (expr2) {......}
 *		if (expr) {......} elsif (expr2) {......}
 *		if (expr) {......} else if (expr2) {......}
 * etc.
 */

BUILT_IN_COMMAND(ifcmd)
{
	int unless_cmd;
	char *current_expr;
	char *current_expr_val;
	int result;
	char *stuff = NULL;

	unless_cmd = (*command == 'U');
	if (!subargs)
		subargs = empty_string;

	if (!args)
		yell("Usage: %s (expr) {true} [ELSIF (expr) {true}] [ELSE] {false}", command);

	while (args && *args)
	{
		while (my_isspace(*args))
			args++;

		current_expr = next_expr(&args, '(');
		if (!current_expr)
		{
			my_error("IF: Missing expression");
			return;
		}
		current_expr_val = parse_inline(current_expr, subargs);
		if (get_int_var(DEBUG_VAR) & DEBUG_EXPANSIONS)
			privileged_yell("%s expression expands to: (%s)", command, current_expr_val);

		result = check_val(current_expr_val);
		new_free(&current_expr_val);

		if (*args == '{')
			stuff = next_expr(&args, '{');
		else
			stuff = args, args = NULL;

		/* If the expression was FALSE for IF, and TRUE for UNLESS */
		if (unless_cmd == result)
		{
			if (args)
			{
				if (!my_strnicmp(args, "elif ", 5))
				{
					args += 5;
					continue;
				}
				else if (!my_strnicmp(args, "else if ", 8))
				{
					args += 8;
					continue;
				}
				else if (!my_strnicmp(args, "elsif ", 6))
				{
					args += 6;
					continue;
				}
				else if (!my_strnicmp(args, "else ", 5))
					args += 5;

				while (my_isspace(*args))
					args++;

				if (*args == '{')
					stuff = next_expr(&args, '{');
				else
					stuff = args, args = NULL;

			}
			else
				stuff = NULL;
		}

		if (stuff)
			runcmds(stuff, subargs);

		break;
	}
}

BUILT_IN_COMMAND(docmd)
{
	char *body, *expr, *cmd, *ptr;
	char *newexp = NULL;
	int result;

	if (!subargs)
		subargs = empty_string;

	if (*args == '{')
	{
		if (!(body = next_expr(&args, '{')))
		{
			my_error("DO: unbalanced {");
			return;
		}	
		if (args && *args && (cmd = next_arg(args, &args)) && 
		     !my_stricmp (cmd, "while"))
		{
			if (!(expr = next_expr(&args, '(')))
			{
				my_error("DO: unbalanced (");
				return;
			}

			will_catch_break_exceptions++;
			will_catch_continue_exceptions++;
			while (1)
			{
				runcmds(body, subargs);
				if (break_exception)
				{
					break_exception = 0;
					break;
				}
				if (continue_exception)
				{
					continue_exception = 0;
					continue;
				}
				if (return_exception)
					break;
				if (system_exception)
					break;

				/* Alas, too bad the malloc is neccesary */
				malloc_strcpy(&newexp, expr);
				ptr = parse_inline(newexp, subargs);
				result = check_val(ptr);
				new_free(&ptr);
				if (!result)
					break;
			}	
			new_free(&newexp);
			will_catch_break_exceptions--;
			will_catch_continue_exceptions--;
			return;
		}
		/* falls through to here if its /do {...} */
		runcmds(body, subargs);
	}
	/* falls through to here if it its /do ... */
	runcmds(args, subargs);
}

BUILT_IN_COMMAND(whilecmd)
{
	char	*exp = NULL,
		*ptr,
		*body = NULL,
		*newexp = NULL;
	int 	whileval = !strcmp(command, "WHILE");
	size_t	sigh;

	if (!args)
		return;

	if (!subargs)
		subargs = empty_string;

	if (!(ptr = next_expr(&args, '(')))
	{
		my_error("WHILE: missing boolean expression");
		return;
	}
	exp = LOCAL_COPY(ptr);

	if (!(ptr = next_expr_failok(&args, '{')))
	{
		if (!(ptr = args))
		{
			my_error("WHILE: Missing body");
			return;
		}
	}

	body = LOCAL_COPY(ptr);

	will_catch_break_exceptions++;
	will_catch_continue_exceptions++;
	sigh = strlen(exp) + 1;		/* Just to make clang shut up */
	newexp = alloca(sigh + 1);
	while (1)
	{
		/* 
		 * We do this to get a fresh copy of "exp"
		 * every time thorugh the loop.
		 * parse_inline() will mangle our string
		 * The use of strlen(exp)+1 is intentional.
		 */
		strlcpy(newexp, exp, sigh);
		ptr = parse_inline(newexp, subargs);
		if (check_val(ptr) != whileval)
			break;

		new_free(&ptr);

		runcmds(body, subargs);
		if (continue_exception)
		{
			continue_exception = 0;
			continue;
		}
		if (break_exception)
		{
			break_exception = 0;
			break;
		}
		if (return_exception)
			break;
		if (system_exception)
			break;
	}
	will_catch_break_exceptions--;
	will_catch_continue_exceptions--;
	new_free(&ptr);
}

BUILT_IN_COMMAND(foreach)
{
	char	*struc = NULL,
		*ptr,
		*body = NULL,
		*var = NULL;
	char	**sublist;
	int	total;
	int	i;
	int	slen;
	int	list = VAR_ALIAS;

	if (!subargs)
		subargs = empty_string;

	while (args && my_isspace(*args))
		args++;

	if (!args || !*args)
	{
		my_error("Usage: FOREACH structure varname {block}");
		return;
	}

	if (*args == '-')
		args++, list = COMMAND_ALIAS;

	if (!(ptr = new_next_arg(args, &args)))
	{
		my_error("FOREACH: missing structure expression");
		return;
	}

	struc = upper(remove_brackets(ptr, subargs));

	if (!(var = next_arg(args, &args)))
	{
		new_free(&struc);
		my_error("FOREACH: missing variable");
		return;
	}
	while (my_isspace(*args))
		args++;		/* why was this (*args)++? */

	if (!(body = next_expr(&args, '{')))
	{
		new_free(&struc);
		my_error("FOREACH: missing statement");
		return;
	}

	if ((sublist = get_subarray_elements(struc, &total, list)) == NULL)
	{
		new_free(&struc);
		return;		/* Nothing there. */
	}

	slen = strlen(struc);

	will_catch_break_exceptions++;
	will_catch_continue_exceptions++;
	for (i = 0; i < total; i++)
	{
		add_local_alias(var, sublist[i] + slen + 1, 0);
		new_free(&sublist[i]);

		runcmds(body, subargs);
	
		if (continue_exception)
		{
			continue_exception = 0;
			continue;
		}
		if (break_exception)
		{
			break_exception = 0;
			break;
		}
		if (return_exception)
			break;
		if (system_exception)
			break;
	}
	while (++i < total)
		new_free(&sublist[i]);
	will_catch_break_exceptions--;
	will_catch_continue_exceptions--;

	new_free((char **)&sublist);
	new_free(&struc);
}

/*
 * FE:  When you need to iteratively loop rather than recursively loop.
 */
BUILT_IN_COMMAND(fe)
{
	char    *list = NULL,
		*templist = NULL,
		*placeholder,
		*vars,
		*var[255],
		*word = NULL,
		*todo = NULL,
		fec_buffer[16];
	unsigned	ind, x, y;
	int     old_display;
	int	doing_fe = !strcmp(command, "FE");
	char	*mapvar = NULL;
	const char	*mapsep = doing_fe ? space : empty_string;
	char	*map = NULL;
	size_t	mapclue = 0;

	if (!subargs)
		subargs = empty_string;

	if (*args == ':' || isalnum(*args)) {
		mapvar = next_arg(args, &args);
		templist = get_variable(mapvar);
	} else if ((list = next_expr(&args, '('))) {
		templist = expand_alias(list, subargs);
	} else {
		my_error("%s: Missing List for /%s", command, command);
		return;
	}    

	if (!templist || !*templist) {
		new_free(&templist);
		return;
	}

	vars = args;
	if (!(args = strchr(args, '{'))) {
		my_error("%s: Missing commands", command);
		new_free(&templist);
		return;
	}
	if (vars == args) {
		my_error("%s: You did not specify any variables", command);
		new_free(&templist);
		return;
	}

	args[-1] = '\0';
	ind = 0;

	while ((var[ind++] = next_arg(vars, &vars)))
	{
		if (ind > (sizeof(var) / sizeof(*var)))
		{
			my_error("%s: Too many variables", command);
			new_free(&templist);
			return;
		}
	}
	ind = ind ? ind - 1: 0;

	if (!(todo = next_expr(&args, '{')))
	{
		my_error("%s: Missing }", command);
		new_free(&templist);
		return;
	}

	old_display = window_display;

	if (!doing_fe)
		{ word = fec_buffer; }

	placeholder = templist;

	will_catch_break_exceptions++;
	will_catch_continue_exceptions++;
	for ( x = 0 ; templist && *templist; )
	{
		for ( y = 0 ; y < ind ; y++ )
		{
			if (doing_fe)
				word = next_func_arg(templist, &templist);
			else
			{
				int	codepoint;
				codepoint = next_code_point((const unsigned char **)&templist, 0);
				if (codepoint == -1)
				{
					templist++;
					continue /* What to do? */;
				}
				ucs_to_utf8(codepoint, word, 16);
			}

			/* Something is really hosed if this happens */
			if (!word)
				word = endstr(templist);

			add_local_alias(var[y], word, 0);
		}
		x += ind;
		runcmds(todo, subargs);

		if (mapvar)
			for ( y = 0 ; y < ind ; y++ ) {
				char *foo = get_variable(var[y]);
				malloc_strcat_wordlist_c(&map, mapsep, foo, &mapclue);
				new_free(&foo);
			}

		if (continue_exception)
		{
			continue_exception = 0;
			continue;
		}
		if (break_exception)
		{
			break_exception = 0;
			break;
		}
		if (return_exception)
			break;
		if (system_exception)
			break;
	}
	will_catch_break_exceptions--;
	will_catch_continue_exceptions--;

	add_var_alias(mapvar, map, 0);
	new_free(&map);

	window_display = 0;
	window_display = old_display;
	new_free(&placeholder);
}


static void	for_next_cmd (int argc, char **argv, const char *subargs)
{
	char 	*var, *cmds;
	char	istr[256];
	int	start, end, step = 1, i;

	if (!subargs)
		subargs = empty_string;

	if ((my_stricmp(argv[1], "from") && my_stricmp(argv[1], "=")) ||
		(argc != 6 && argc != 8))
	{
		my_error("Usage: /FOR var FROM start TO end {commands}");
		return;
	}

	var = argv[0];
	start = atoi(argv[2]);
	end = atoi(argv[4]);
	if (argc == 8)
	{
		step = atoi(argv[6]);
		cmds = argv[7];
	}
	else
	{
		step = 1;
		cmds = argv[5];
	}

	if (*cmds == '{')
		cmds++;
	will_catch_break_exceptions++;
	will_catch_continue_exceptions++;
	for (i = start; step > 0 ? i <= end : i >= end; i += step)
	{
		snprintf(istr, 255, "%d", i);
		add_local_alias(var, istr, 0);
		runcmds(cmds, subargs);

		if (break_exception)
		{
			break_exception = 0;
			break;
		}
		if (continue_exception)
			continue_exception = 0;	/* Dont continue here! */
		if (return_exception)
			break;
		if (system_exception)
			break;
	}
	will_catch_break_exceptions--;
	will_catch_continue_exceptions--;
}

static void	for_fe_cmd (int argc, char **argv, const char *subargs)
{
	char 	*var, *list, *cmds;
	char	*next, *real_list, *x;

	if (!subargs)
		subargs = empty_string;

	if ((my_stricmp(argv[1], "in")) || (argc != 4)) {
		my_error("Usage: /FOR var IN (list) {commands}");
		return;
	}

	var = argv[0];
	list = argv[2];
	cmds = argv[3];

	if (*cmds == '{')
		cmds++;
	if (*list == '(')
		list++;
	x = real_list = expand_alias(list, subargs);
	will_catch_break_exceptions++;
	will_catch_continue_exceptions++;
	while (real_list && *real_list)
	{
		next = next_func_arg(real_list, &real_list);
		add_local_alias(var, next, 0);
		runcmds(cmds, subargs);

		if (break_exception) {
			break_exception = 0;
			break;
		}
		if (continue_exception)
			continue_exception = 0;	/* Dont continue here! */
		if (return_exception)
			break;
		if (system_exception)
			break;
	}
	will_catch_break_exceptions--;
	will_catch_continue_exceptions--;
	new_free(&x);
}

static void	for_pattern_cmd (int argc, char **argv, const char *subargs)
{
	my_error("/FOR var IN <pattern> {commands} reserved for future use");
}

static BUILT_IN_COMMAND(loopcmd)
{
	int	argc;
	char	*argv[10];

	if (!subargs)
		subargs = empty_string;

	argc = split_args(args, argv, 9);

	if (argc < 2)
		my_error("%s: syntax error", command);
	else if (!my_stricmp(argv[1], "from") || !my_stricmp(argv[1], "="))
		for_next_cmd(argc, argv, subargs);
	else if (!my_stricmp(argv[1], "in"))
	{
		if (argc < 4)
			my_error("%s: syntax error", command);
		else if (*argv[2] == '(')
			for_fe_cmd(argc, argv, subargs);
		else
			for_pattern_cmd(argc, argv, subargs);
	}
	else
		my_error("%s: syntax error", command);
}

/* 
 * FOR command..... prototype: 
 *  for (commence,evaluation,iteration) {commands}
 * in the same style of C's for, the for loop is just a specific
 * type of WHILE loop.
 *
 * IMPORTANT: Since ircII uses ; as a delimeter between commands,
 * commas were chosen to be the delimiter between expressions,
 * so that semicolons may be used in the expressions (think of this
 * as the reverse as C, where commas seperate commands in expressions,
 * and semicolons end expressions.
 */
BUILT_IN_COMMAND(forcmd)
{
	char *	working    = NULL;
	char *	commence   = NULL;
	char *	evaluation = NULL;
	char *	lameeval   = NULL;
	char *	iteration  = NULL;
	char *	blah       = NULL;
	char *	commands   = NULL;
	size_t	sigh;

	if (!subargs)
		subargs = empty_string;

	/* Get the whole () thing */
	if ((working = next_expr_failok(&args, '(')) == NULL)	/* ) */
	{
		loopcmd("FOR", args, subargs);
		return;
	}

	commence = LOCAL_COPY(working);

	/* Find the beginning of the second expression */
	if (!(evaluation = strchr(commence, ',')))
	{
		my_error("FOR: no components!");
		return;
	}
	do 
		*evaluation++ = 0;
	while (my_isspace(*evaluation));

	/* Find the beginning of the third expression */
	if (!(iteration = strchr(evaluation, ',')))
	{
		my_error("FOR: Only two components!");
		return;
	}
	do 
		*iteration++ = 0;
	while (my_isspace(*iteration));

	working = args;
	while (my_isspace(*working))
		*working++ = 0;

	if (!(working = next_expr(&working, '{')))		/* } */
	{
		my_error("FOR: badly formed commands");
		return;
	}
	commands = LOCAL_COPY(working);

	runcmds(commence, subargs);

	will_catch_break_exceptions++;
	will_catch_continue_exceptions++;
	sigh = strlen(evaluation) + 1;
	lameeval = alloca(sigh + 1);
	while (1)
	{
		/* 
		 * This is very intentional.
		 * "lameeval" gets mangled every time through, so we need
		 * to take a fresh copy from scratch each time.
		 */
		strlcpy(lameeval, evaluation, sigh);
		blah = parse_inline(lameeval, subargs);
		if (!check_val(blah))
		{
			new_free(&blah);
			break;
		}

		new_free(&blah);
		runcmds(commands, subargs);
		if (break_exception)
		{
			break_exception = 0;
			break;
		}
		if (continue_exception)
			continue_exception = 0;	/* Dont continue here! */
		if (return_exception)
			break;
		if (system_exception)
			break;

		runcmds(iteration, subargs);
	}
	will_catch_break_exceptions--;
	will_catch_continue_exceptions--;

	new_free(&blah);
}

/*

  Need to support something like this:

	switch (text to be matched)
	{
		(sample text)
		{
			...
		}
		(sample text2)
		(sample text3)
		{
			...
		}
		...
	}

How it works:

	The command is technically made up a single (...) expression and
	a single {...} expression.  The (...) expression is taken to be
	regular expando-text (much like the (...) body of /fe.

	The {...} body is taken to be a series of [(...)] {...} pairs.
	The [(...)] expression is taken to be one or more consecutive
	(...) structures, which are taken to be text expressions to match
	against the header text.  If any of the (...) expressions are found
	to match, then the commands in the {...} body are executed.

	There may be as many such [(...)] {...} pairs as you need.  However,
	only the *first* pair found to be matching is actually executed,
	and the others are ignored, so placement of your switches are
	rather important:  Put your most general ones last.

*/
BUILT_IN_COMMAND(switchcmd)
{
	char 	*control, 
		*body, 
		*header, 
		*commands;

	if (!subargs)
		subargs = empty_string;

	if (!(control = next_expr(&args, '(')))
	{
		say("SWITCH: String to be matched not found where expected");
		return;
	}

	control = expand_alias(control, subargs);
	if (get_int_var(DEBUG_VAR) & DEBUG_EXPANSIONS)
		privileged_yell("%s expression expands to: (%s)", command, control);

	if (!(body = next_expr(&args, '{')))
		say("SWITCH: Execution body not found where expected");

	while (body && *body)
	{
		int hooked = 0;

		while (*body == '(')
		{
			if (!(header = next_expr(&body, '(')))
			{
				say("SWITCH: Case label not found where expected");
				new_free(&control);
				return;
			}
			header = expand_alias(header, subargs);
			if (get_int_var(DEBUG_VAR) & DEBUG_EXPANSIONS)
				privileged_yell("%s expression expands to: (%s)", command, header);
			if (wild_match(header, control))
				hooked = 1;
			new_free(&header);
			if (*body == ';')
				body++;		/* ugh. */
		}

		if (!(commands = next_expr(&body, '{')))
		{
			say("SWITCH: case body not found where expected");
			break;
		}

		if (hooked)
		{
			will_catch_break_exceptions++;
			runcmds(commands, subargs);
			if (break_exception)
				break_exception = 0;
			will_catch_break_exceptions--;
			break;
		}

		while (*body && (*body == ';' || isspace(*body)))
			body++;		/* grumble */
	}

	new_free(&control);
}

BUILT_IN_COMMAND(repeatcmd)
{
	char *	num_expr = NULL;
	int 	value;
	char *	tmp_val;

	if (!subargs)
		subargs = empty_string;

	while (isspace(*args))
		args++;

	if (*args == '(')
	{
		char *		dumb_copy;

		if (!(num_expr = next_expr(&args, '(')))
		{
			say("REPEAT: Could not find expression");
			return;
		}
		dumb_copy = LOCAL_COPY(num_expr);
		tmp_val = parse_inline(dumb_copy, subargs);
	}
	else
	{
		num_expr = new_next_arg(args, &args);
		tmp_val = malloc_strdup(num_expr);
	}

	value = my_atol(tmp_val);
	new_free(&tmp_val);

	if (value <= 0)
		return;

	/* Probably want to catch break and continue here */
	while (value--)
		runcmds(args, subargs);

	return;
}
