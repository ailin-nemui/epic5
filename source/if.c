/*
 * if.c: the IF, WHILE, FOREACH, DO, FE, FEC, and FOR commands for IRCII 
 *
 * WHILE, FOREACH commands written by Troy Rollo
 * Copyright 1992, 1994 Troy Rollo
 *
 * IF, FE, FEC, FOR, DO, SWITCH, REPEAT commands written by Jeremy Nelson
 * Copyright 1995, 1997 Jeremy Nelson
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 */

#include "irc.h"
#include "alias.h"
#include "if.h"
#include "ircaux.h"
#include "output.h"
#include "parse.h"
#include "vars.h"
#include "commands.h"
#include "window.h"


/*
 * next_expr finds the next expression delimited by brackets. The type
 * of bracket expected is passed as a parameter. Returns NULL on error.
 */
static __inline__
char *	my_next_expr (char **args, char type, int whine, int wantchar)
{
	char	*expr_start,
		*expr_end;

	if (!*args || !**args || **args != type)
	{
		if (whine)
			say("Expression syntax");
		return NULL;
	}

	/* Find the end of the expression and terminate it. */
	expr_start = *args;
	if (!(expr_end = MatchingBracket(expr_start + 1, type, 
					(type == '(') ? ')' : '}')))
	{
		say("Unmatched '%c'", type);
		return NULL;
	}
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

char *	next_expr_with_type (char **args, char type)
{
	return my_next_expr (args, type, 0, 1);
}

char *	next_expr_failok (char **args, char type)
{
	return my_next_expr (args, type, 0, 0);
}

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
	char *current_line = NULL;
	int flag = 0;

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
			error("IF: Missing expression");
			return;
		}
		current_expr_val = parse_inline(current_expr, subargs, &flag);
		if (get_int_var(DEBUG_VAR) & DEBUG_EXPANSIONS)
			yell("%s expression expands to: (%s)", command, current_expr_val);
		result = check_val(current_expr_val);
		new_free(&current_expr_val);

		if (*args == '{')
			current_line = next_expr(&args, '{');
		else
			current_line = args, args = NULL;

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
					current_line = next_expr(&args, '{');
				else
					current_line = args, args = NULL;

			}
			else
				current_line = NULL;
		}

		if (current_line)
			parse_line(NULL, current_line, subargs, 0, 0);

		break;
	}
}

BUILT_IN_COMMAND(docmd)
{
	char *body, *expr, *cmd, *ptr;
	char *newexp = (char *) 0;
	int args_used = 0;
	int result;

	if (*args == '{')
	{
		body = next_expr(&args, '{');
		if (body == (char *) 0)
		{
			error("DO: unbalanced {");
			return;
		}	
		if (args && *args && (cmd = next_arg(args, &args)) && 
		     !my_stricmp (cmd, "while"))
		{
			if (!(expr = next_expr(&args, '(')))
			{
				error("DO: unbalanced (");
				return;
			}

			will_catch_break_exceptions++;
			will_catch_continue_exceptions++;
			while (1)
			{
				parse_line ((char *) 0, body, subargs ? subargs : empty_string, 0, 0);
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

				/* Alas, too bad the malloc is neccesary */
				malloc_strcpy(&newexp, expr);
				ptr = parse_inline(newexp, subargs ? subargs : empty_string,
					&args_used);
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
		parse_line ((char *) 0, body, subargs ? subargs : empty_string, 0, 0);
	}
	/* falls through to here if it its /do ... */
	parse_line ((char *) 0, args, subargs ? subargs : empty_string, 0, 0);
}

BUILT_IN_COMMAND(whilecmd)
{
	char	*exp = (char *) 0,
		*ptr,
		*body = (char *) 0,
		*newexp = (char *) 0;
	int	args_used;	/* this isn't used here, but is passed
				 * to expand_alias() */
	int 	whileval = !strcmp(command, "WHILE");

	if ((ptr = next_expr(&args, '(')) == (char *) 0)
	{
		error("WHILE: missing boolean expression");
		return;
	}
	exp = LOCAL_COPY(ptr);

	if ((ptr = next_expr_failok(&args, '{')) == (char *) 0)
		ptr = args;
	body = LOCAL_COPY(ptr);

	will_catch_break_exceptions++;
	will_catch_continue_exceptions++;
	while (1)
	{
		newexp = LOCAL_COPY(exp);
		ptr = parse_inline(newexp, subargs ? subargs : empty_string, &args_used);
		if (check_val(ptr) != whileval)
			break;

		new_free(&ptr);

		parse_line((char *) 0, body, subargs ?  subargs : empty_string, 0, 0);
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
	}
	will_catch_break_exceptions--;
	will_catch_continue_exceptions--;
	new_free(&ptr);
}

BUILT_IN_COMMAND(foreach)
{
	char	*struc = (char *) 0,
		*ptr,
		*body = (char *) 0,
		*var = (char *) 0;
	char	**sublist;
	int	total;
	int	i;
	int	slen;
	int	old_display;
	int	list = VAR_ALIAS;
	int	af;

	while (args && my_isspace(*args))
		args++;

	if (*args == '-')
		args++, list = COMMAND_ALIAS;

	if ((ptr = new_next_arg(args, &args)) == (char *) 0)
	{
		error("FOREACH: missing structure expression");
		return;
	}

	struc = upper(remove_brackets(ptr, subargs, &af));

	if ((var = next_arg(args, &args)) == (char *) 0)
	{
		new_free(&struc);
		error("FOREACH: missing variable");
		return;
	}
	while (my_isspace(*args))
		args++;		/* why was this (*args)++? */

	if ((body = next_expr(&args, '{')) == (char *) 0)
	{
		new_free(&struc);
		error("FOREACH: missing statement");
		return;
	}

	if ((sublist = get_subarray_elements(struc, &total, list)) == NULL)
	{
		new_free(&struc);
		return;		/* Nothing there. */
	}

	slen = strlen(struc);
	old_display = window_display;

	will_catch_break_exceptions++;
	will_catch_continue_exceptions++;
	for (i = 0; i < total; i++)
	{
		add_local_alias(var, sublist[i] + slen + 1, 0);
		new_free(&sublist[i]);

		parse_line(NULL, body, subargs ? subargs : empty_string, 0, 0);
	
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
	}
	while (++i < total)
		new_free(&sublist[i]);
	will_catch_break_exceptions--;
	will_catch_continue_exceptions--;

	new_free((char **)&sublist);
	new_free(&struc);
}

/*
 * FE:  Written by Jeremy Nelson (jnelson@acronet.net)
 * When you need to iteratively loop rather than recursively loop.
 */
BUILT_IN_COMMAND(fe)
{
	char    *list = (char *) 0,
		*templist = (char *) 0,
		*placeholder,
		*vars,
		*var[255],
		*word = (char *) 0,
		*todo = (char *) 0,
		fec_buffer[2];
	int     ind, x, y, blah,args_flag;
	int     old_display;
	int	doing_fe = !strcmp(command, "FE");
	char	*mapvar = (char *) 0;
	char	*mapsep = doing_fe ? space : empty_string;
	char	*map = (char *) 0;
	size_t	mapclue = 0;

#if 0
	for (x = 0; x <= (sizeof(var) / sizeof(*var)); var[x++] = (char *) 0)
		;
#endif

	if (!subargs)
		subargs = empty_string;

	if (*args == '(' && (list = next_expr(&args, '('))) {
		templist = expand_alias(list, subargs, &args_flag, NULL);
	} else if ((mapvar = next_arg(args, &args))) {
		templist = get_variable(mapvar);
	} else {
		error("%s: Missing List for /%s", command, command);
		return;
	}    

	if (!templist || !*templist) {
		new_free(&templist);
		return;
	}

	vars = args;
	if (!(args = strchr(args, '{'))) {
		error("%s: Missing commands", command);
		new_free(&templist);
		return;
	}
	if (vars == args) {
		error("%s: You did not specify any variables", command);
		new_free(&templist);
		return;
	}

	args[-1] = '\0';
	ind = 0;

	while ((var[ind++] = next_arg(vars, &vars)))
	{
		if (ind > (sizeof(var) / sizeof(*var)))
		{
			error("%s: Too many variables", command);
			new_free(&templist);
			return;
		}
	}
	ind = ind ? ind - 1: 0;

	if (!(todo = next_expr(&args, '{')))
	{
		error("%s: Missing }", command);
		new_free(&templist);
		return;
	}

	old_display = window_display;

	if (!doing_fe)
		{ word = fec_buffer; word[1] = 0; }

	blah = ((doing_fe) ? (word_count(templist)) : (strlen(templist)));
	placeholder = templist;

	will_catch_break_exceptions++;
	will_catch_continue_exceptions++;
	for ( x = 0 ; x < blah ; )
	{
		for ( y = 0 ; y < ind ; y++ )
		{
			if (doing_fe)
				word = ((x+y) < blah)
				    ? new_next_arg(templist, &templist)
				    : empty_string;
			else
				word[0] = ((x+y) < blah)
				    ? templist[x+y] : 0;

			/* Something is really hosed if this happens */
			if (!word)
				word = empty_string;

			add_local_alias(var[y], word, 0);
		}
		x += ind;
		parse_line((char *) 0, todo, 
		    subargs?subargs:empty_string, 0, 0);

		if (mapvar)
			for ( y = 0 ; y < ind ; y++ ) {
				char *foo = get_variable(var[y]);
				m_sc3cat(&map, mapsep, foo, &mapclue);
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
	}
	will_catch_break_exceptions--;
	will_catch_continue_exceptions--;

	add_var_alias(mapvar, map, 0);
	new_free(&map);

	window_display = 0;
	window_display = old_display;
	new_free(&placeholder);
}


void	for_next_cmd (int argc, char **argv, const char *subargs)
{
	char 	*var, *cmds;
	char	istr[256];
	int	start, end, step = 1, i;

	if ((my_stricmp(argv[1], "from") && my_stricmp(argv[1], "=")) ||
		(argc != 6 && argc != 8))
	{
		error("Usage: /FOR var FROM start TO end {commands}");
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
	for (i = start; i <= end; i += step)
	{
		snprintf(istr, 255, "%d", i);
		add_local_alias(var, istr, 0);
		parse_line(NULL, cmds, subargs, 0, 0);

		if (break_exception)
		{
			break_exception = 0;
			break;
		}
		if (continue_exception)
			continue_exception = 0;	/* Dont continue here! */
		if (return_exception)
			break;
	}
	will_catch_break_exceptions--;
	will_catch_continue_exceptions--;
}

void	for_fe_cmd (int argc, char **argv, const char *subargs)
{
	char 	*var, *list, *cmds;
	char	*next, *real_list, *x;
	int	args_flag = 0;

	if ((my_stricmp(argv[1], "in")) || (argc != 4)) {
		error("Usage: /FOR var IN (list) {commands}");
		return;
	}

	var = argv[0];
	list = argv[2];
	cmds = argv[3];

	if (*cmds == '{')
		cmds++;
	if (*list == '(')
		list++;
	x = real_list = expand_alias(list, subargs, &args_flag, NULL);
	will_catch_break_exceptions++;
	will_catch_continue_exceptions++;
	while (real_list && *real_list)
	{
		next = new_next_arg(real_list, &real_list);
		add_local_alias(var, next, 0);
		parse_line(NULL, cmds, subargs, 0, 0);

		if (break_exception) {
			break_exception = 0;
			break;
		}
		if (continue_exception)
			continue_exception = 0;	/* Dont continue here! */
		if (return_exception)
			break;
	}
	will_catch_break_exceptions--;
	will_catch_continue_exceptions--;
	new_free(&x);
}

void	for_pattern_cmd (int argc, char **argv, const char *subargs)
{
	error("/FOR var IN <pattern> {commands} reserved for future use");
}

BUILT_IN_COMMAND(loopcmd)
{
	int	argc;
	char	*argv[9];

	if (!subargs)
		subargs = empty_string;

	argc = split_args(args, argv, 9);

	if (argc < 2)
		error("%s: syntax error", command);
	else if (!my_stricmp(argv[1], "from") || !my_stricmp(argv[1], "="))
		for_next_cmd(argc, argv, subargs);
	else if (!my_stricmp(argv[1], "in"))
	{
		if (argc < 4)
			error("%s: syntax error", command);
		else if (*argv[2] == '(')
			for_fe_cmd(argc, argv, subargs);
		else
			for_pattern_cmd(argc, argv, subargs);
	}
	else
		error("%s: syntax error", command);
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
	char        *working        = (char *) 0;
	char        *commence       = (char *) 0;
	char        *evaluation     = (char *) 0;
	char        *lameeval       = (char *) 0;
	char        *iteration      = (char *) 0;
	const char  *sa             = (char *) 0;
	int         argsused        = 0;
	char        *blah           = (char *) 0;
	char        *commands       = (char *) 0;

	/* Get the whole () thing */
	if ((working = next_expr_failok(&args, '(')) == NULL)	/* ) */
	{
		loopcmd("FOR", args, subargs);
		return;
	}

	commence = LOCAL_COPY(working);

	/* Find the beginning of the second expression */
	evaluation = strchr(commence, ',');
	if (!evaluation)
	{
		error("FOR: no components!");
		return;
	}
	do 
		*evaluation++ = '\0';
	while (my_isspace(*evaluation));

	/* Find the beginning of the third expression */
	iteration = strchr(evaluation, ',');
	if (!iteration)
	{
		error("FOR: Only two components!");
		return;
	}
	do 
	{
		*iteration++ = '\0';
	}
	while (my_isspace(*iteration));

	working = args;
	while (my_isspace(*working))
		*working++ = '\0';

	if ((working = next_expr(&working, '{')) == (char *) 0)		/* } */
	{
		error("FOR: badly formed commands");
		return;
	}
	commands = LOCAL_COPY(working);

	sa = subargs ? subargs : empty_string;
	parse_line((char *) 0, commence, sa, 0, 0);

	will_catch_break_exceptions++;
	will_catch_continue_exceptions++;
	while (1)
	{
		lameeval = LOCAL_COPY(evaluation);

		blah = parse_inline(lameeval, sa, &argsused);
		if (!check_val(blah))
		{
			new_free(&blah);
			break;
		}

		new_free(&blah);
		parse_line((char *) 0, commands, sa, 0, 0);
		if (break_exception)
		{
			break_exception = 0;
			break;
		}
		if (continue_exception)
			continue_exception = 0;	/* Dont continue here! */
		if (return_exception)
			break;

		parse_line((char *) 0, iteration, sa, 0, 0);
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
	int 	af;

	if (!(control = next_expr(&args, '(')))
	{
		say("SWITCH: String to be matched not found where expected");
		return;
	}

	control = expand_alias(control, subargs, &af, NULL);
	if (get_int_var(DEBUG_VAR) & DEBUG_EXPANSIONS)
		yell("%s expression expands to: (%s)", command, control);

	if (!(body = next_expr(&args, '{')))
		say("SWITCH: Execution body not found where expected");

#if 0
	/* Strip out the "" magic */
	if (*body == '"' && body[strlen(body)-1] == '"')
		body[strlen(body)-1] = 0, body++;
#endif

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
			header = expand_alias(header, subargs, &af, NULL);
			if (get_int_var(DEBUG_VAR) & DEBUG_EXPANSIONS)
				yell("%s expression expands to: (%s)", command, header);
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
			parse_line(NULL, commands, subargs, 0, 0);
			if (break_exception)
				break_exception = 0;
			will_catch_break_exceptions--;
			break;
		}

		if (*body == ';')
			body++;		/* grumble */
	}

	new_free(&control);
}

BUILT_IN_COMMAND(repeatcmd)
{
	char *	num_expr = NULL;
	int 	value;

	while (isspace(*args))
		args++;

	if (*args == '(')
	{
		char *		tmp_val;
		char *		dumb_copy;
		int 		argsused;
		const char *	sa = subargs ? subargs : empty_string;

		num_expr = next_expr(&args, '(');
		dumb_copy = LOCAL_COPY(num_expr);

		tmp_val = parse_inline(dumb_copy,sa,&argsused);
		value = my_atol(tmp_val);
		new_free(&tmp_val);
	}
	else
	{
		char *		tmp_val;
		int 		af;

		num_expr = new_next_arg(args, &args);
		tmp_val = expand_alias(num_expr, subargs, &af, NULL);
		value = my_atol(tmp_val);
		new_free(&tmp_val);
	}

	if (value <= 0)
		return;

	/* Probably want to catch break and continue here */
	while (value--)
		parse_line(NULL, args, subargs ? subargs : empty_string, 0, 0);

	return;
}
