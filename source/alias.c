/*
 * alias.c -- Handles the whole kit and caboodle for aliases.
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright © 1997, 1998 Jeremy Nelson and others ("EPIC Software Labs").
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
/* Almost totaly rewritten by Jeremy Nelson (01/97) */

#define __need_cs_alist_hash__
#include "irc.h"
#include "alias.h"
#include "alist.h"
#include "array.h"
#include "commands.h"
#include "files.h"
#include "history.h"
#include "hook.h"
#include "input.h"
#include "ircaux.h"
#include "output.h"
#include "screen.h"
#include "stack.h"
#include "status.h"
#include "vars.h"
#include "window.h"
#include "keys.h"

#define LEFT_BRACE '{'
#define RIGHT_BRACE '}'
#define LEFT_BRACKET '['
#define RIGHT_BRACKET ']'
#define LEFT_PAREN '('
#define RIGHT_PAREN ')'
#define DOUBLE_QUOTE '"'

/*
 * after_expando: This replaces some much more complicated logic strewn
 * here and there that attempted to figure out just how long an expando 
 * name was supposed to be.  Well, now this changes that.  This will slurp
 * up everything in 'start' that could possibly be put after a $ that could
 * result in a syntactically valid expando.  All you need to do is tell it
 * if the expando is an rvalue or an lvalue (it *does* make a difference)
 */
static 	char *lval[] = { "rvalue", "lvalue" };
static  char *after_expando (char *start, int lvalue, int *call)
{
	char	*rest;
	char	*str;

	if (!*start)
		return start;

	/*
	 * One or two leading colons are allowed
	 */
	str = start;
	if (*str == ':')
		if (*++str == ':')
			++str;

	/*
	 * This handles 99.99% of the cases
	 */
	while (*str && (isalpha(*str) || isdigit(*str) || 
				*str == '_' || *str == '.'))
		str++;

	/*
	 * This handles any places where a var[var] could sneak in on
	 * us.  Supposedly this should never happen, but who can tell?
	 */
	while (*str == '[')
	{
		if (!(rest = MatchingBracket(str + 1, '[', ']')))
		{
			if (!(rest = strchr(str, ']')))
			{
				yell("Unmatched bracket in %s (%s)", 
						lval[lvalue], start);
				return empty_string;
			}
		}
		str = rest + 1;
	}

	/*
	 * Rvalues may include a function call, slurp up the argument list.
	 */
	if (!lvalue && *str == '(')
	{
		if (!(rest = MatchingBracket(str + 1, '(', ')')))
		{
			if (!(rest = strchr(str, ')')))
			{
				yell("Unmatched paren in %s (%s)", 
						lval[lvalue], start);
				return empty_string;
			}
		}
		*call = 1;
		str = rest + 1;
	}

	/*
	 * If the entire thing looks to be invalid, perhaps its a 
	 * special built-in expando.  Check to see if it is, and if it
	 * is, then slurp up the first character as valid.
	 * Also note that $: by itself must be valid, which requires
	 * some shenanigans to handle correctly.  Ick.
	 */
	if (str == start || (str == start + 1 && *start == ':'))
	{
		int is_builtin = 0;

		built_in_alias(*start, &is_builtin);
		if (is_builtin && (str == start))
			str++;
	}


	/*
	 * All done!
	 */
	return str;
}

/* Used for statistics gathering */
static unsigned long 	alias_total_allocated = 0;
static unsigned long 	alias_total_bytes_allocated = 0;
static unsigned long 	var_cache_hits = 0;
static unsigned long 	var_cache_misses = 0;
static unsigned long 	var_cache_missed_by = 0;
static unsigned long 	var_cache_passes = 0;
static unsigned long 	cmd_cache_hits = 0;
static unsigned long 	cmd_cache_misses = 0;
static unsigned long 	cmd_cache_missed_by = 0;
static unsigned long 	cmd_cache_passes = 0;

/* Ugh. XXX Bag on the side */
struct ArgListT {
	char *	vars[32];
	char *	defaults[32];
	int	void_flag;
	int	dot_flag;
};
typedef struct ArgListT ArgList;
ArgList	*parse_arglist (char *arglist);
void	destroy_arglist (ArgList *);


/*
 * This is where we keep track of where the last pending function call.
 * This is used when you assign to the FUNCTION_RETURN value.  Since it
 * is neccesary to be able to access FUNCTION_RETURN in contexts where other
 * local variables would not be visible, we do this as a quasi-neccesary
 * hack.  When you reference FUNCTION_RETURN, it goes right to this stack.
 */
	int	last_function_call_level = -1;

/*
 * The following actions are supported:  add, delete, find, list
 * On the following types of data:	 var_alias, cmd_alias, local_alias
 * Except you cannot list or delete local_aliases.
 *
 * To fetch a variable, use ``get_variable''
 * To fetch an alias, use ``get_cmd_alias''
 * To fetch an ambiguous alias, use ``glob_cmd_alias''
 * To recurse an array structure, use ``get_subarray_elements''
 */

/*
 * These are general purpose interface functions.
 * However, the static ones should **always** be static!  If you are tempted
 * to use them outside of alias.c, please rethink what youre trying to do.
 * Using these functions violates the encapsulation of the interface.
 * Specifically, if you create a global variable and then want to delete it,
 * try using a local variable so it is reaped automatically.
 */
extern	void    add_var_alias      (char *name, char *stuff, int noisy);
extern  void    add_local_alias    (char *name, char *stuff, int noisy);
extern  void    add_cmd_alias      (char *name, ArgList *arglist, char *stuff);
extern  void    add_var_stub_alias (char *name, char *stuff);
extern  void    add_cmd_stub_alias (char *name, char *stuff);
static	void	delete_var_alias   (char *name, int noisy);
static	void	delete_cmd_alias   (char *name, int noisy);
/*	void	delete_local_alias (char *name); 		*/
static	void	unload_cmd_alias   (char *fn);
static	void	unload_var_alias   (char *fn);
static	void	list_cmd_alias     (char *name);
static	void	list_var_alias     (char *name);
static	void	list_local_alias   (char *name);
static	void 	destroy_aliases    (int type);

extern	char *  get_variable            (char *name);
extern	char ** glob_cmd_alias          (char *name, int *howmany);
extern	char ** glob_assign_alias	(char *name, int *howmany);
extern	char ** pmatch_cmd_alias        (char *name, int *howmany);
extern	char ** pmatch_assign_alias	(char *name, int *howmany);
extern	char *  get_cmd_alias           (char *name, int *howmany, 
					 char **complete_name, void **args);
extern	char ** get_subarray_elements   (char *root, int *howmany, int type);


static	char *	get_variable_with_args (const char *str, const char *args, int *args_flag);

/************************** HIGH LEVEL INTERFACE ***********************/

/*
 * User front end to the ALIAS command
 * Syntax to debug alias:  ALIAS /S
 * Syntax to add alias:    ALIAS name [{] irc command(s) [}]
 * Syntax to delete alias: ALIAS -name
 */
BUILT_IN_COMMAND(aliascmd)
{
	char *name;
	char *real_name;
	char *ptr;
	void show_alias_caches(void);

	/*
	 * If no name present, list all aliases
	 */
	if (!(name = next_arg(args, &args)))
	{
		list_cmd_alias(NULL);
		return;
	}

	/*
	 * Alias can take an /s arg, which shows some data we've collected
	 */
	if (!my_strnicmp(name, "/S", 2))
	{
extern u_32int_t       bin_ints;
extern u_32int_t       lin_ints;
extern u_32int_t       bin_chars;
extern u_32int_t       lin_chars;
extern u_32int_t       alist_searches;
extern u_32int_t       char_searches;


	    say("Total aliases handled: %ld", 
			alias_total_allocated);
	    say("Total bytes allocated to aliases: %ld", 
			alias_total_bytes_allocated);

	    say("Var command hits/misses/passes/missed-by [%ld/%ld/%ld/%3.1f]",
			var_cache_hits, 
			var_cache_misses, 
			var_cache_passes, 
			( var_cache_misses ? (double) 
			  (var_cache_missed_by / var_cache_misses) : 0.0));
	    say("Cmd command hits/misses/passes/missed-by [%ld/%ld/%ld/%3.1f]",
			cmd_cache_hits, 
			cmd_cache_misses, 
			cmd_cache_passes,
			( cmd_cache_misses ? (double)
			  (cmd_cache_missed_by / cmd_cache_misses) : 0.0));
	    say("Ints(bin/lin)/Chars(bin/lin)/Lookups: [(%d/%d)/(%d/%d)] (%d/%d)",
			 bin_ints, lin_ints, bin_chars, lin_chars,
			 alist_searches, char_searches);
	    show_alias_caches();
	    return;
	}

	/*
	 * Canonicalize the alias name
	 */
	real_name = remove_brackets(name, NULL, NULL);

	/*
	 * Find the argument body
	 */
	while (my_isspace(*args))
		args++;

	/*
	 * If there is no argument body, we probably want to delete alias
	 */
	if (!args || !*args)
	{
		/*
		 * If the alias name starts with a hyphen, then we are
		 * going to delete it.
		 */
		if (real_name[0] == '-')
		{
			if (real_name[1])
				delete_cmd_alias(real_name + 1, 1);
			else
				say("You must specify an alias to be removed.");
		}

		/*
		 * Otherwise, the user wants us to list that alias
		 */
		else
			list_cmd_alias(real_name);
	}

	/*
	 * If there is an argument body, then we have to register it
	 */
	else
	{
		ArgList *arglist = NULL;

		/*
		 * Aliases may contain a parameter list, which is parsed
		 * at registration time.
		 */
		if (*args == LEFT_PAREN)
		{
			ptr = MatchingBracket(++args, LEFT_PAREN, RIGHT_PAREN);
			if (!ptr)
				say("Unmatched lparen in %s %s", 
					command, real_name);
			else
			{
				*ptr++ = 0;
				while (*ptr && my_isspace(*ptr))
					ptr++;
				if (!*ptr)
					say("Missing alias body in %s %s", 
						command, real_name);

				while (*args && my_isspace(*args))
					args++;

				arglist = parse_arglist(args);
				args = ptr;
			}
		}

		/*
		 * Aliases' bodies can be surrounded by a set of braces,
		 * which are stripped off.
		 */
		if (*args == LEFT_BRACE)
		{
			ptr = MatchingBracket(++args, LEFT_BRACE, RIGHT_BRACE);

			if (!ptr)
				say("Unmatched brace in %s %s", 
						command, real_name);
			else 
			{
				*ptr++ = 0;
				while (*ptr && my_isspace(*ptr))
					ptr++;

				if (*ptr)
					say("Junk [%s] after closing brace in %s %s", ptr, command, real_name);

				while (*args && my_isspace(*args))
					args++;

			}
		}

		/*
		 * Register the alias
		 */
		add_cmd_alias(real_name, arglist, args);
	}

	new_free(&real_name);
	return;
}

/*
 * User front end to the ASSIGN command
 * Syntax to add variable:    ASSIGN name text
 * Syntax to delete variable: ASSIGN -name
 */
BUILT_IN_COMMAND(assigncmd)
{
	char *real_name;
	char *name;

	/*
	 * If there are no arguments, list all the global variables
	 */
	if (!(name = next_arg(args, &args)))
	{
		list_var_alias(NULL);
		return;
	}

	/*
	 * Canonicalize the variable name
	 */
	real_name = remove_brackets(name, NULL, NULL);

	/*
	 * Find the stuff to assign to the variable
	 */
	while (my_isspace(*args))
		args++;

	/*
	 * If there is no body, then the user probably wants to delete
	 * the variable
	 */
	if (!args || !*args)
	{
		/*
	 	 * If the variable name starts with a hyphen, then we remove
		 * the variable
		 */
		if (real_name[0] == '-')
		{
			if (real_name[1])
				delete_var_alias(real_name + 1, 1);
			else
				say("You must specify an alias to be removed.");
		}

		/*
		 * Otherwise, the user wants us to list the variable
		 */
		else
			list_var_alias(real_name);
	}

	/*
	 * Register the variable
	 */
	else
		add_var_alias(real_name, args, 1);

	new_free(&real_name);
	return;
}

/*
 * User front end to the STUB command
 * Syntax to stub an alias to a file:	STUB ALIAS name[,name] filename(s)
 * Syntax to stub a variable to a file:	STUB ASSIGN name[,name] filename(s)
 */
BUILT_IN_COMMAND(stubcmd)
{
	int 	type;
	char 	*cmd;
	char 	*name;
const 	char 	*usage = "Usage: %s (alias|assign) <name> <file> [<file> ...]";

	/*
	 * The first argument is the type of stub to make
	 * (alias or assign)
	 */
	if (!(cmd = upper(next_arg(args, &args))))
	{
		error("Missing stub type");
		say(usage, command);
		return;
	}

	if (!strncmp(cmd, "ALIAS", strlen(cmd)))
		type = COMMAND_ALIAS;
	else if (!strncmp(cmd, "ASSIGN", strlen(cmd)))
		type = VAR_ALIAS;
	else
	{
		error("[%s] is an Unrecognized stub type", cmd);
		say(usage, command);
		return;
	}

	/*
	 * The next argument is the name of the item to be stubbed.
	 * This is not optional.
	 */
	if (!(name = next_arg(args, &args)))
	{
		error("Missing alias name");
		say(usage, command);
		return;
	}

	/*
	 * Find the filename argument
	 */
	while (my_isspace(*args))
		args++;

	/*
	 * The rest of the argument(s) are the files to load when the
	 * item is referenced.  This is not optional.
	 */
	if (!args || !*args)
	{
		error("Missing file name");
		say(usage, command);
		return;
	}

	/*
	 * Now we iterate over the item names we were given.  For each
	 * item name, seperated from the next by a comma, stub that item
	 * to the given filename(s) specified as the arguments.
	 */
	while (name && *name)
	{
		char 	*next_name;
		char	*real_name;

		if ((next_name = strchr(name, ',')))
			*next_name++ = 0;

		real_name = remove_brackets(name, NULL, NULL);
		if (type == COMMAND_ALIAS)
			add_cmd_stub_alias(real_name, args);
		else
			add_var_stub_alias(real_name, args);

		new_free(&real_name);
		name = next_name;
	}
}

BUILT_IN_COMMAND(localcmd)
{
	char *name;

	if (!(name = next_arg(args, &args)))
	{
		list_local_alias(NULL);
		return;
	}

	while (args && *args && my_isspace(*args))
		args++;

	if (!args)
		args = empty_string;

	if (!my_strnicmp(name, "-dump", 2))	/* Illegal name anyways */
	{
		destroy_aliases(VAR_ALIAS_LOCAL);
		return;
	}

	while (name && *name)
	{
		char 	*next_name;
		char	*real_name;

		if ((next_name = strchr(name, ',')))
			*next_name++ = 0;

		real_name = remove_brackets(name, NULL, NULL);
		add_local_alias(real_name, args, 1);
		new_free(&real_name);
		name = next_name;
	}
}

BUILT_IN_COMMAND(dumpcmd)
{
	char 	*blah = empty_string;
	int 	all = 0;
	int 	dumped = 0;

	upper(args);

	if (!args || !*args || !strncmp(args, "ALL", 3))
		all = 1;

	while (all || (blah = next_arg(args, &args)))
	{
		dumped = 0;

		if (!strncmp(blah, "VAR", strlen(blah)) || all)
		{
			say("Dumping your global variables");
			destroy_aliases(VAR_ALIAS);
			dumped++;
		}
		if (!strncmp(blah, "ALIAS", strlen(blah)) || all)
		{
			say("Dumping your global aliases");
			destroy_aliases(COMMAND_ALIAS);
			dumped++;
		}
		if (!strncmp(blah, "ON", strlen(blah)) || all)
		{
			say("Dumping your ONs");
			flush_on_hooks();
			dumped++;
		}

		if (!dumped)
			say("Dump what? ('%s' is unrecognized)", blah);

		if (all)
			break;
	}
}

BUILT_IN_COMMAND(unloadcmd)
{
	char *filename;

	if (!(filename = next_arg(args, &args)))
		error("You must supply a filename for /UNLOAD.");
	else
	{
		do_hook(UNLOAD_LIST, "%s", filename);
		say("Removing aliases from %s ...", filename);
		unload_cmd_alias(filename);
		say("Removing assigns from %s ...", filename);
		unload_var_alias(filename);
		say("Removing hooks from %s ...", filename);
		unload_on_hooks(filename);
		say("Removing keybinds from %s ...", filename);
		unload_bindings(filename);
		say("Done.");
	}
}


/*
 * Argument lists look like this:
 *
 * LIST   := LPAREN TERM [COMMA TERM] RPAREN)
 * LPAREN := '('
 * RPAREN := ')'
 * COMMA  := ','
 * TERM   := LVAL QUAL | "..." | "void"
 * LVAL   := <An alias name>
 * QUAL   := NUM "words"
 *
 * In English:
 *   An argument list is a comma seperated list of variable descriptors (TERM)
 *   enclosed in a parenthesis set.  Each variable descriptor may contain any
 *   valid alias name followed by an action qualifier, or may be the "..."
 *   literal string, or may be the "void" literal string.  If a variable
 *   descriptor is an alias name, then that positional argument will be removed
 *   from the argument list and assigned to that variable.  If the qualifier
 *   specifies how many words are to be taken, then that many are taken.
 *   If the variable descriptor is the literal string "...", then all argument
 *   list parsing stops there and all remaining alias arguments are placed 
 *   into the $* special variable.  If the variable descriptor is the literal
 *   string "void", then the balance of the remaining alias arguments are 
 *   discarded and $* will expand to the false value.  If neither "..." nor
 *   "void" are provided in the argument list, then that last variable in the
 *   list will recieve all of the remaining arguments left at its position.
 * 
 * Examples:
 *
 *   # This example puts $0 in $channel, $1 in $mag, and $2- in $nicklist.
 *   /alias opalot (channel, mag, nicklist) {
 *	fe ($nicklist) xx yy zz {
 *	    mode $channel ${mag}ooo $xx $yy $zz
 *	}
 *   }
 *
 *   # This example puts $0 in $channel, $1 in $mag, and the new $* is old $2-
 *   /alias opalot (channel, mag, ...) {
 *	fe ($*) xx yy zz {
 *	    mode $channel ${mag}ooo $xx $yy $zz
 *	}
 *   }
 *
 *   # This example throws away any arguments passed to this alias
 *   /alias booya (void) { echo Booya! }
 */
ArgList	*parse_arglist (char *arglist)
{
	char *	this_term;
	char *	next_term;
	char *	varname;
	char *	modifier, *value;
	int	arg_count = 0;
	ArgList *args = new_malloc(sizeof(ArgList));

	args->void_flag = args->dot_flag = 0;
	for (this_term = arglist; *this_term; this_term = next_term,arg_count++)
	{
		while (isspace(*this_term))
			this_term++;
		next_in_comma_list(this_term, &next_term);
		if (!(varname = next_arg(this_term, &this_term)))
			continue;
		if (!my_stricmp(varname, "void")) {
			args->void_flag = 1;
			break;
		} else if (!my_stricmp(varname, "...")) {
			args->dot_flag = 1;
			break;
		} else {
			args->vars[arg_count] = m_strdup(varname);
			args->defaults[arg_count] = NULL;

			if (!(modifier = next_arg(this_term, &this_term)))
				continue;
			if (!my_stricmp(modifier, "default"))
			{
			    if (!(value = new_next_arg(this_term, &this_term)))
				continue;
			    args->defaults[arg_count] = m_strdup(value);
			}
		}
	}
	args->vars[arg_count] = NULL;
	return args;
}

void	destroy_arglist (ArgList *arglist)
{
	int	i = 0;

	if (!arglist)
		return;

	for (i = 0; ; i++)
	{
		if (!arglist->vars[i])
			break;
		new_free(&arglist->vars[i]);
		new_free(&arglist->defaults[i]);
	}
	new_free((char **)&arglist);
}

void	prepare_alias_call (void *al, char **stuff)
{
	ArgList *args = (ArgList *)al;
	int	i;

	if (!args)
		return;

	for (i = 0; args->vars[i]; i++)
	{
		char	*next_val;
		char	*expanded = NULL;
		int	af;

		/* Last argument on the list and no ... argument? */
		if (!args->vars[i + 1] && !args->dot_flag && !args->void_flag)
		{
			next_val = *stuff;
			*stuff = empty_string;
		}

		/* Yank the next word from the arglist */
		else
			next_val = next_arg(*stuff, stuff);

		if (!next_val || !*next_val)
		{
			if ((next_val = args->defaults[i]))
				next_val = expanded = expand_alias(next_val, *stuff, &af, NULL);
			else
				next_val = empty_string;
		}

		/* Add the local variable */
		add_local_alias(args->vars[i], next_val, 0);
		if (expanded)
			new_free(&expanded);
	}

	/* Throw away rest of args if wanted */
	if (args->void_flag)
		*stuff = empty_string;
}

/**************************** INTERMEDIATE INTERFACE *********************/
/* We define Alias here to keep it encapsulated */
/*
 * This is the description of an alias entry
 * This is an ``array_item'' structure
 */
typedef	struct	AliasItemStru
{
	char	*name;			/* name of alias */
	u_32int_t hash;			/* Hash of the name */
	char	*stuff;			/* what the alias is */
	char	*stub;			/* the file its stubbed to */
	char	*filename;		/* file it was loaded from */
	int	line;			/* line it was loaded from */
	int	global;			/* set if loaded from `global' */
	int	cache_revoked;		/* Cache revocation index */
	ArgList *arglist;		/* List of arguments to alias */
}	Alias;

/*
 * This is the description for a list of aliases
 * This is an ``array_set'' structure
 */
#define ALIAS_CACHE_SIZE 4

typedef struct	AliasStru
{
	Alias **	list;
	int		max;
	int		max_alloc;
	alist_func 	func;
	hash_type	hash;
	Alias  **	cache;
	int		cache_size;
	int		revoke_index;
}	AliasSet;

static AliasSet var_alias = 	{ NULL, 0, 0, strncmp, 
					HASH_INSENSITIVE, NULL, 0, 0 };
static AliasSet cmd_alias = 	{ NULL, 0, 0, strncmp, 
					HASH_INSENSITIVE, NULL, 0, 0 };

static	Alias *	find_var_alias     (char *name);
static	Alias *	find_cmd_alias     (char *name, int *cnt);
static	Alias *	find_local_alias   (char *name, AliasSet **list);

/*
 * This is the ``stack frame''.  Each frame has a ``name'' which is
 * the name of the alias or on of the frame, or is NULL if the frame
 * is not an ``enclosing'' frame.  Each frame also has a ``current command''
 * that is being executed, which is used to help us when the client crashes.
 * Each stack also contains a list of local variables.
 */
typedef struct RuntimeStackStru
{
	const char *name;	/* Name of the stack */
	char 	*current;	/* Current cmd being executed */
	AliasSet alias;		/* Local variables */
	int	locked;		/* Are we locked in a wait? */
	int	parent;		/* Our parent stack frame */
}	RuntimeStack;

/*
 * This is the master stack frame.  Its size is saved in ``max_wind''
 * and the current frame being used is stored in ``wind_index''.
 */
static 	RuntimeStack *call_stack = NULL;
	int 	max_wind = -1;
	int 	wind_index = -1;


void show_alias_caches(void)
{
	int i;
	for (i = 0; i < var_alias.cache_size; i++)
	{
		if (var_alias.cache[i])
			yell("VAR cache [%d]: [%s] [%s]", i, var_alias.cache[i]->name, var_alias.cache[i]->stuff);
		else
			yell("VAR cache [%d]: empty", i);
	}

	for (i = 0; i < cmd_alias.cache_size; i++)
	{
		if (cmd_alias.cache[i])
			yell("CMD cache [%d]: [%s] [%s]", i, cmd_alias.cache[i]->name, cmd_alias.cache[i]->stuff);
		else
			yell("CMD cache [%d]: empty", i);
	}
}




Alias *make_new_Alias (char *name)
{
	Alias *tmp = (Alias *) new_malloc(sizeof(Alias));
	tmp->name = m_strdup(name);
	tmp->stuff = tmp->stub = NULL;
	tmp->line = current_line();
	tmp->cache_revoked = 0;
	tmp->filename = m_strdup(current_package());
	tmp->arglist = NULL;
	alias_total_bytes_allocated += sizeof(Alias) + strlen(name) +
				strlen(tmp->filename);
	return tmp;
}


/*
 * add_var_alias: Add a global variable
 *
 * name -- name of the alias to create (must be canonicalized)
 * stuff -- what to have ``name'' expand to.
 *
 * If ``name'' is FUNCTION_RETURN, then it will create the local
 * return value (die die die)
 *
 * If ``name'' refers to an already created local variable, that
 * local variable is used (invisibly)
 */
void	add_var_alias	(char *name, char *stuff, int noisy)
{
	char 	*ptr;
	Alias 	*tmp = NULL;
	int 	af;
	int	local = 0;
	char	*save;

	save = name = remove_brackets(name, NULL, &af);
	if (*name == ':')
	{
		name++, local = 1;
		if (*name == ':')
			name++, local = -1;
	}

	/*
	 * Weed out invalid variable names
	 */
	ptr = after_expando(name, 1, NULL);
	if (*ptr)
		error("ASSIGN names may not contain '%c' (You asked for [%s])", *ptr, name);

	/*
	 * Weed out FUNCTION_RETURN (die die die)
	 */
	else if (!strcmp(name, "FUNCTION_RETURN"))
		add_local_alias(name, stuff, noisy);

	/*
	 * Pass the buck on local variables
	 */
	else if ((local == 1) || (local == 0 && find_local_alias(name, NULL)))
		add_local_alias(name, stuff, noisy);

	else if (stuff && *stuff)
	{
		int cnt, loc;

		/*
		 * Look to see if the given alias already exists.
		 * If it does, and the ``stuff'' to assign to it is
		 * empty, then we should remove the variable outright
		 */
		tmp = (Alias *) find_array_item ((array *)&var_alias, name, &cnt, &loc);
		if (!tmp || cnt >= 0)
		{
			tmp = make_new_Alias(name);
			add_to_array ((array *)&var_alias, (array_item *)tmp);
		}

		/*
		 * Then we fill in the interesting details
		 */
		if (strcmp(tmp->filename, current_package()))
			malloc_strcpy(&(tmp->filename), current_package());
		malloc_strcpy(&(tmp->stuff), stuff);
		new_free(&tmp->stub);
		tmp->global = loading_global;

		alias_total_allocated++;
		alias_total_bytes_allocated += strlen(tmp->name) + strlen(tmp->stuff) + strlen(tmp->filename);

		/*
		 * And tell the user.
		 */
		if (noisy)
			say("Assign %s added [%s]", name, stuff);
	}
	else
		delete_var_alias(name, noisy);

	new_free(&save);
	return;
}

void	add_local_alias	(char *name, char *stuff, int noisy)
{
	char 	*ptr;
	Alias 	*tmp = NULL;
	AliasSet *list = NULL;
	int 	af;

	name = remove_brackets(name, NULL, &af);

	/*
	 * Weed out invalid variable names
	 */
	ptr = after_expando(name, 1, NULL);
	if (*ptr)
	{
		error("LOCAL names may not contain '%c' (You asked for [%s])", 
						*ptr, name);
		new_free(&name);
		return;
	}
	
	/*
	 * Now we see if this local variable exists anywhere
	 * within our view.  If it is, we dont care where.
	 * If it doesnt, then we add it to the current frame,
	 * where it will be reaped later.
	 */
	if (!(tmp = find_local_alias (name, &list)))
	{
		tmp = make_new_Alias(name);
		add_to_array ((array *)list, (array_item *)tmp);
	}

	/* Fill in the interesting stuff */
	if (strcmp(tmp->filename, current_package()))
		malloc_strcpy(&(tmp->filename), current_package());
	malloc_strcpy(&(tmp->stuff), stuff);
	alias_total_allocated++;
	if (tmp->stuff)		/* Oh blah. */
	{
		alias_total_bytes_allocated += strlen(tmp->stuff);
		if (x_debug & DEBUG_LOCAL_VARS && noisy)
		    yell("Assign %s (local) added [%s]", name, stuff);
		else if (noisy)
		    say("Assign %s (local) added [%s]", name, stuff);
	}

	new_free(&name);
	return;
}

void	add_cmd_alias	(char *name, ArgList *arglist, char *stuff)
{
	Alias *tmp = NULL;
	int cnt, af, loc;

	name = remove_brackets(name, NULL, &af);

	tmp = (Alias *) find_array_item ((array *)&cmd_alias, name, &cnt, &loc);
	if (!tmp || cnt >= 0)
	{
		tmp = make_new_Alias(name);
		add_to_array ((array *)&cmd_alias, (array_item *)tmp);
	}

	if (strcmp(tmp->filename, current_package()))
		malloc_strcpy(&(tmp->filename), current_package());
	malloc_strcpy(&(tmp->stuff), stuff);
	new_free(&tmp->stub);
	tmp->global = loading_global;
	tmp->arglist = arglist;

	alias_total_allocated++;
	alias_total_bytes_allocated += strlen(tmp->stuff);
	say("Alias	%s added [%s]", name, stuff);

	new_free(&name);
	return;
}

void	add_var_stub_alias  (char *name, char *stuff)
{
	Alias *tmp = NULL;
	char *ptr;
	int af;

	name = remove_brackets(name, NULL, &af);

	ptr = after_expando(name, 1, NULL);
	if (*ptr)
		error("Assign names may not contain '%c' (You asked for [%s])", *ptr, name);

	else if (!strcmp(name, "FUNCTION_RETURN"))
		error("You may not stub the FUNCTION_RETURN variable.");

	else 
	{
		int cnt, loc;

		tmp = (Alias *) find_array_item ((array *)&var_alias, name, &cnt, &loc);
		if (!tmp || cnt >= 0)
		{
			tmp = make_new_Alias(name);
			add_to_array ((array *)&var_alias, (array_item *)tmp);
		}

		if (strcmp(tmp->filename, current_package()))
			malloc_strcpy(&(tmp->filename), current_package());
		malloc_strcpy(&(tmp->stub), stuff);
		new_free(&tmp->stuff);
		tmp->global = loading_global;

		alias_total_allocated++;
		alias_total_bytes_allocated += strlen(tmp->stub);
		say("Assign %s stubbed to file %s", name, stuff);
	}

	new_free(&name);
	return;
}


void	add_cmd_stub_alias  (char *name, char *stuff)
{
	Alias *tmp = NULL;
	int cnt, af;

	name = remove_brackets(name, NULL, &af);
	if (!(tmp = find_cmd_alias (name, &cnt)) || cnt >= 0)
	{
		tmp = make_new_Alias(name);
		add_to_array ((array *)&cmd_alias, (array_item *)tmp);
	}

	if (strcmp(tmp->filename, current_package()))
		malloc_strcpy(&(tmp->filename), current_package());
	malloc_strcpy(&(tmp->stub), stuff);
	new_free(&tmp->stuff);
	tmp->global = loading_global;

	alias_total_allocated++;
	alias_total_bytes_allocated += strlen(tmp->stub);
	say("Alias %s stubbed to file %s", name, stuff);

	new_free(&name);
	return;
}


/************************ LOW LEVEL INTERFACE *************************/
static void resize_cache (AliasSet *set, int newsize)
{
	int 	c, d;
	int 	oldsize = set->cache_size;

	set->cache_size = newsize;
	if (newsize < oldsize)
	{
		for (d = oldsize; d < newsize; d++)
			set->cache[d]->cache_revoked = ++set->revoke_index;
	}

	RESIZE(set->cache, Alias *, set->cache_size);
	for (c = oldsize; c < set->cache_size; c++)
		set->cache[c] = NULL;
}

static	int	unstub_in_progress = 0;

/*
 * 'name' is expected to already be in canonical form (uppercase, dot notation)
 */
static Alias *	find_var_alias (char *name)
{
	Alias *	item = NULL;
	int 	cache;
	int 	loc;
	int 	cnt = 0;
	int 	i;
	u_32int_t	mask;
	u_32int_t	hash = cs_alist_hash(name, &mask);

	if (!strncmp(name, "::", 2))
		name += 2;		/* Accept ::global */

	if (var_alias.cache_size == 0)
		resize_cache(&var_alias, ALIAS_CACHE_SIZE);

	for (cache = 0; cache < var_alias.cache_size; cache++)
	{
		if (var_alias.cache[cache] && var_alias.cache[cache]->name &&
			(var_alias.cache[cache]->hash == hash) &&
			!strcmp(name, var_alias.cache[cache]->name))
		{
			item = var_alias.cache[cache];
			cnt = -1;
			var_cache_hits++;
			break;
		}
	}

	if (!item)
	{
		cache = var_alias.cache_size - 1;
		if ((item = (Alias *) find_array_item ((array *)&var_alias, name, &cnt, &loc)))
			var_cache_misses++;
		else
			var_cache_passes++;
	}

	if (cnt < 0)
	{
		if (var_alias.cache[cache])
			var_alias.cache[cache]->cache_revoked = 
					++var_alias.revoke_index;

		for (i = cache; i > 0; i--)
			var_alias.cache[i] = var_alias.cache[i - 1];
		var_alias.cache[0] = item;

		if (item->cache_revoked)
		{
			var_cache_missed_by += var_alias.revoke_index - 
						item->cache_revoked;
		}

		if (item->stub)
		{
			char *file;

			file = LOCAL_COPY(item->stub);
			delete_var_alias(item->name, 0);

			if (!unstub_in_progress)
			{
				unstub_in_progress = 1;
				load("LOAD", file, empty_string);
				unstub_in_progress = 0;
			}

			/* 
			 * At this point, we have to see if 'item' was
			 * redefined by the /load.  We call find_var_alias 
			 * recursively to pick up the new value
			 */
			if (!(item = find_var_alias(name)))
				return NULL;
		}

		return item;
	}

	return NULL;
}

static Alias *	find_cmd_alias (char *name, int *cnt)
{
	Alias *		item = NULL;
	int 		loc;
	int 		i;
	int 		cache;
	u_32int_t	mask;
	u_32int_t	hash = cs_alist_hash(name, &mask);

	if (cmd_alias.cache_size == 0)
		resize_cache(&cmd_alias, ALIAS_CACHE_SIZE);

	for (cache = 0; cache < cmd_alias.cache_size; cache++)
	{
		if (cmd_alias.cache[cache] && cmd_alias.cache[cache]->name &&
			(cmd_alias.cache[cache]->hash == hash) &&
			!strcmp(name, cmd_alias.cache[cache]->name))
		{
			item = cmd_alias.cache[cache];
			*cnt = -1;
			cmd_cache_hits++;
			break;
		}

		/* XXXX this is bad cause this is free()d! */
		if (cmd_alias.cache[cache] && !cmd_alias.cache[cache]->name)
			cmd_alias.cache[cache] = NULL;
	}

	if (!item)
	{
		cache = cmd_alias.cache_size - 1;
		if ((item = (Alias *) find_array_item ((array *)&cmd_alias, name, cnt, &loc)))
			cmd_cache_misses++;
		else
			cmd_cache_passes++;
	}

	if (*cnt < 0 || *cnt == 1)
	{
		if (cmd_alias.cache[cache])
			cmd_alias.cache[cache]->cache_revoked = 
					++cmd_alias.revoke_index;

		for (i = cache; i > 0; i--)
			cmd_alias.cache[i] = cmd_alias.cache[i - 1];
		cmd_alias.cache[0] = item;

		if (item->cache_revoked)
		{
			cmd_cache_missed_by += cmd_alias.revoke_index - 
						item->cache_revoked;
		}

		if (item->stub)
		{
			char *file;

			file = LOCAL_COPY(item->stub);
			delete_cmd_alias(item->name, 0);

			if (!unstub_in_progress)
			{
				unstub_in_progress = 1;
				load("LOAD", file, empty_string);
				unstub_in_progress = 0;
			}

			/* 
			 * At this point, we have to see if 'item' was
			 * redefined by the /load.  We call find_cmd_alias 
			 * recursively to pick up the new value
			 */
			item = find_cmd_alias(name, cnt);

			if (!item)
				return NULL;
		}

		if (!item->stuff)
			panic("item->stuff is NULL and it shouldn't be.");

		return item;
	}

	return NULL;
}


/*
 * An example will best describe the semantics:
 *
 * A local variable will be returned if and only if there is already a
 * variable that is exactly ``name'', or if there is a variable that
 * is an exact leading subset of ``name'' and that variable ends in a
 * period (a dot).
 */
static Alias *	find_local_alias (char *name, AliasSet **list)
{
	Alias 	*alias = NULL;
	int 	c = wind_index;
	char 	*ptr;
	int 	implicit = -1;
	int	function_return = 0;

	/* No name is an error */
	if (!name)
		return NULL;

	ptr = after_expando(name, 1, NULL);
	if (*ptr)
		return NULL;

	if (!my_stricmp(name, "FUNCTION_RETURN"))
		function_return = 1;

	/*
	 * Search our current local variable stack, and wind our way
	 * backwards until we find a NAMED stack -- that is the enclosing
	 * alias or ON call.  If we find a variable in one of those enclosing
	 * stacks, then we use it.  If we dont, we progress.
	 *
	 * This needs to be optimized for the degenerate case, when there
	 * is no local variable available...  It will be true 99.999% of
	 * the time.
	 */
	for (c = wind_index; c >= 0; c = call_stack[c].parent)
	{
		/* XXXXX */
		if (function_return && last_function_call_level != -1)
			c = last_function_call_level;

		if (x_debug & DEBUG_LOCAL_VARS)
			yell("Looking for [%s] in level [%d]", name, c);

		if (call_stack[c].alias.list)
		{
			int x;

			/* XXXX - This is bletcherous */
			for (x = 0; x < call_stack[c].alias.max; x++)
			{
				size_t len = strlen(call_stack[c].alias.list[x]->name);

				if (streq(call_stack[c].alias.list[x]->name, name) == len)
				{
					if (call_stack[c].alias.list[x]->name[len-1] == '.')
						implicit = c;
					else if (strlen(name) == len)
					{
						alias = call_stack[c].alias.list[x];
						break;
					}
				}
				else
				{
					if (my_stricmp(call_stack[c].alias.list[x]->name, name) > 0)
						continue;
				}
			}

			if (!alias && implicit >= 0)
			{
				alias = make_new_Alias(name);
				add_to_array ((array *)&call_stack[implicit].alias, (array_item *)alias);
			}
		}

		if (alias)
		{
			if (x_debug & DEBUG_LOCAL_VARS) 
				yell("I found [%s] in level [%d] (%s)", name, c, alias->stuff);
			break;
		}

		if (*call_stack[c].name || call_stack[c].parent == -1)
		{
			if (x_debug & DEBUG_LOCAL_VARS) 
				yell("I didnt find [%s], stopped at level [%d]", name, c);
			break;
		}
	}

	if (alias)
	{
		if (list)
			*list = &call_stack[c].alias;
		return alias;
	}
	else if (list)
		*list = &call_stack[wind_index].alias;

	return NULL;
}





static
void	delete_var_alias (char *name, int noisy)
{
	Alias *item;
	int i;

	upper(name);
	if ((item = (Alias *)remove_from_array ((array *)&var_alias, name)))
	{
		for (i = 0; i < var_alias.cache_size; i++)
		{
			if (var_alias.cache[i] == item)
				var_alias.cache[i] = NULL;
		}

		new_free(&(item->name));
		new_free(&(item->stuff));
		new_free(&(item->stub));
		new_free(&(item->filename));
		new_free((char **)&item);
		if (noisy)
			say("Assign %s removed", name);
	}
	else if (noisy)
		say("No such assign: %s", name);
}

static
void	delete_cmd_alias (char *name, int noisy)
{
	Alias *item;
	int i;

	upper(name);
	if ((item = (Alias *)remove_from_array ((array *)&cmd_alias, name)))
	{
		for (i = 0; i < cmd_alias.cache_size; i++)
		{
			if (cmd_alias.cache[i] == item)
				cmd_alias.cache[i] = NULL;
		}

		new_free(&(item->name));
		new_free(&(item->stuff));
		new_free(&(item->stub));
		new_free(&(item->filename));
		destroy_arglist(item->arglist);
		new_free((char **)&item);
		if (noisy)
			say("Alias	%s removed", name);
	}
	else if (noisy)
		say("No such alias: %s", name);
}





static void	list_local_alias (char *name)
{
	int len = 0, cnt;
	int DotLoc, LastDotLoc = 0;
	char *LastStructName = NULL;
	char *s;

	say("Visible Local Assigns:");
	if (name)
		len = strlen(upper(name));

	for (cnt = wind_index; cnt >= 0; cnt = call_stack[cnt].parent)
	{
		int x;
		if (!call_stack[cnt].alias.list)
			continue;
		for (x = 0; x < call_stack[cnt].alias.max; x++)
		{
			if (!name || !strncmp(call_stack[cnt].alias.list[x]->name, name, len))
			{
				if ((s = strchr(call_stack[cnt].alias.list[x]->name + len, '.')))
				{
					DotLoc = s - call_stack[cnt].alias.list[x]->name;
					if (!LastStructName || (DotLoc != LastDotLoc) || strncmp(call_stack[cnt].alias.list[x]->name, LastStructName, DotLoc))
					{
						put_it("\t%*.*s\t<Structure>", DotLoc, DotLoc, call_stack[cnt].alias.list[x]->name);
						LastStructName = call_stack[cnt].alias.list[x]->name;
						LastDotLoc = DotLoc;
					}
				}
				else
					put_it("\t%s\t%s", call_stack[cnt].alias.list[x]->name, call_stack[cnt].alias.list[x]->stuff);
			}
		}
	}
}

/*
 * This function is strictly O(N).  Its possible to address this.
 */
static
void	list_var_alias (char *name)
{
	int	len;
	int	DotLoc,
		LastDotLoc = 0;
	char	*LastStructName = NULL;
	int	cnt;
	char	*s, *script;

	say("Assigns:");

	if (name)
	{
		upper(name);
		len = strlen(name);
	}
	else
		len = 0;

	for (cnt = 0; cnt < var_alias.max; cnt++)
	{
		if (!name || !strncmp(var_alias.list[cnt]->name, name, len))
		{
			script = var_alias.list[cnt]->filename[0]
				? var_alias.list[cnt]->filename
				: "*";

			if ((s = strchr(var_alias.list[cnt]->name + len, '.')))
			{
				DotLoc = s - var_alias.list[cnt]->name;
				if (!LastStructName || (DotLoc != LastDotLoc) || strncmp(var_alias.list[cnt]->name, LastStructName, DotLoc))
				{
					say("[%s]\t%*.*s\t<Structure>", script, DotLoc, DotLoc, var_alias.list[cnt]->name);
					LastStructName = var_alias.list[cnt]->name;
					LastDotLoc = DotLoc;
				}
			}
			else
			{
				if (var_alias.list[cnt]->stub)
					say("[%s]\t%s STUBBED TO %s", script, var_alias.list[cnt]->name, var_alias.list[cnt]->stub);
				else
					say("[%s]\t%s\t%s", script, var_alias.list[cnt]->name, var_alias.list[cnt]->stuff);
			}
		}
	}
}

/*
 * This function is strictly O(N).  Its possible to address this.
 */
static
void	list_cmd_alias (char *name)
{
	int	len;
	int	DotLoc,
		LastDotLoc = 0;
	char	*LastStructName = NULL;
	int	cnt;
	char	*s, *script;

	say("Aliases:");

	if (name)
	{
		upper(name);
		len = strlen(name);
	}
	else
		len = 0;

	for (cnt = 0; cnt < cmd_alias.max; cnt++)
	{
		if (!name || !strncmp(cmd_alias.list[cnt]->name, name, len))
		{
			script = cmd_alias.list[cnt]->filename[0]
				? m_strdup(cmd_alias.list[cnt]->filename)
				: "*";

			if ((s = strchr(cmd_alias.list[cnt]->name + len, '.')))
			{
				DotLoc = s - cmd_alias.list[cnt]->name;
				if (!LastStructName || (DotLoc != LastDotLoc) || strncmp(cmd_alias.list[cnt]->name, LastStructName, DotLoc))
				{
					say("[%s]\t%*.*s\t<Structure>", script, DotLoc, DotLoc, cmd_alias.list[cnt]->name);
					LastStructName = cmd_alias.list[cnt]->name;
					LastDotLoc = DotLoc;
				}
			}
			else
			{
				if (cmd_alias.list[cnt]->stub)
					say("[%s]\t%s STUBBED TO %s", script, cmd_alias.list[cnt]->name, cmd_alias.list[cnt]->stub);
				else
					say("[%s]\t%s\t%s", script, cmd_alias.list[cnt]->name, cmd_alias.list[cnt]->stuff);
			}
		}
	}
}

/************************* UNLOADING SCRIPTS ************************/
static void	unload_cmd_alias (char *filename)
{
	int 	cnt;

	for (cnt = 0; cnt < cmd_alias.max; )
	{
		if (!strcmp(cmd_alias.list[cnt]->filename, filename))
			delete_cmd_alias(cmd_alias.list[cnt]->name, 0);
		else
			cnt++;
	}
}

static void	unload_var_alias (char *filename)
{
	int 	cnt;

	for (cnt = 0; cnt < var_alias.max;)
	{
		if (!strcmp(var_alias.list[cnt]->filename, filename))
			delete_var_alias(var_alias.list[cnt]->name, 0);
		else
			cnt++;
	}
}


/************************* DIRECT VARIABLE EXPANSION ************************/
/*
 * get_variable: This simply looks up the given str.  It first checks to see
 * if its a user variable and returns it if so.  If not, it checks to see if
 * it's an IRC variable and returns it if so.  If not, it checks to see if
 * its and environment variable and returns it if so.  If not, it returns
 * null.  It mallocs the returned string 
 */
char 	*get_variable 	(char *str)
{
	int af;
	return get_variable_with_args(str, NULL, &af);
}


static 
char	*get_variable_with_args (const char *str, const char *args, int *args_flag)
{
	Alias	*alias = NULL;
	char	*ret = NULL;
	char	*name = NULL;
	char	*freep = NULL;
	int	copy = 0;
	int	local = 0;

	freep = name = remove_brackets(str, args, args_flag);

	/*
	 * Support $:var to mean local variable ONLY (no globals)
	 * Support $::var to mean global variable ONLY (no locals)
	 * Support $: with nothing after it as a built in expando.  Ick.
	 */
	if (*name == ':' && name[1])
	{
		name++, local = 1;
		if (*name == ':')
			name++, local = -1;
	}

	/*
	 * local == -1  means "local variables not allowed"
	 * local == 0   means "locals first, then globals"
	 * local == 1   means "global variables not allowed"
	 */
	if ((local != -1) && (alias = find_local_alias(name, NULL)))
		copy = 1, ret = alias->stuff;
	else if (local == 1)
		;
	else if ((alias = find_var_alias(name)) != NULL)
		copy = 1, ret = alias->stuff;
	else if ((strlen(str) == 1) && (ret = built_in_alias(*str, NULL)))
		copy = 0;
	else if ((ret = make_string_var(str)))
		copy = 0;
	else
		copy = 1, ret = getenv(str);

	if (x_debug & DEBUG_UNKNOWN && ret == NULL)
		yell("Variable lookup to non-existant assign [%s]", name);

	new_free(&freep);
	return (copy ? m_strdup(ret) : ret);
}

char *	get_cmd_alias (char *name, int *howmany, char **complete_name, void **args)
{
	Alias *item;

	if ((item = find_cmd_alias(name, howmany)))
	{
		if (complete_name)
			malloc_strcpy(complete_name, item->name);
		if (args)
			*args = (void *)item->arglist;
		return item->stuff;
	}
	return NULL;
}

/*
 * This function is strictly O(N).  This should probably be addressed.
 *
 * Updated as per get_subarray_elements.
 */
char **	glob_cmd_alias (char *name, int *howmany)
{
	int	pos, max;
	int    	cnt;
	int     len;
	char    **matches = NULL;
	int     matches_size = 0;

	len = strlen(name);
	*howmany = 0;
	if (len) {
		find_array_item((array*)&cmd_alias, name, &max, &pos);
	} else {
		pos = 0;
		max = cmd_alias.max;
	}
	if (0 > max) max = -max;

	for (cnt = 0; cnt < max; cnt++, pos++)
	{
		if (strchr(cmd_alias.list[pos]->name + len, '.'))
			continue;

		if (*howmany >= matches_size)
		{
			matches_size += 5;
			RESIZE(matches, char *, matches_size + 1);
		}
		matches[*howmany] = m_strdup(cmd_alias.list[pos]->name);
		*howmany += 1;
	}

	if (*howmany)
		matches[*howmany] = NULL;
	else
		new_free((char **)&matches);

	return matches;
}

/*
 * This function is strictly O(N).  This should probably be addressed.
 *
 * Updated as per get_subarray_elements.
 */
char **	glob_assign_alias (char *name, int *howmany)
{
	int	pos, max;
	int    	cnt;
	int     len;
	char    **matches = NULL;
	int     matches_size = 0;

	len = strlen(name);
	*howmany = 0;
	if (len) {
		find_array_item((array*)&var_alias, name, &max, &pos);
	} else {
		pos = 0;
		max = var_alias.max;
	}
	if (0 > max) max = -max;

	for (cnt = 0; cnt < max; cnt++, pos++)
	{
		if (strchr(var_alias.list[pos]->name + len, '.'))
			continue;

		if (*howmany >= matches_size)
		{
			matches_size += 5;
			RESIZE(matches, char *, matches_size + 1);
		}
		matches[*howmany] = m_strdup(var_alias.list[pos]->name);
		*howmany += 1;
	}

	if (*howmany)
		matches[*howmany] = NULL;
	else
		new_free((char **)&matches);

	return matches;
}

/*
 * This function is strictly O(N).  This should probably be addressed.
 */
char **	pmatch_cmd_alias (char *name, int *howmany)
{
	int	cnt;
	int 	len;
	char 	**matches = NULL;
	int 	matches_size = 5;

	len = strlen(name);
	*howmany = 0;
	matches = RESIZE(matches, char *, matches_size);

	for (cnt = 0; cnt < cmd_alias.max; cnt++)
	{
		if (wild_match(name, cmd_alias.list[cnt]->name))
		{
			matches[*howmany] = m_strdup(cmd_alias.list[cnt]->name);
			*howmany += 1;
			if (*howmany == matches_size)
			{
				matches_size += 5;
				RESIZE(matches, char *, matches_size);
			}
		}
	}

	if (*howmany)
		matches[*howmany] = NULL;
	else
		new_free((char **)&matches);

	return matches;
}

/*
 * This function is strictly O(N).  This should probably be addressed.
 */
char **	pmatch_assign_alias (char *name, int *howmany)
{
	int    	cnt;
	int     len;
	char    **matches = NULL;
	int     matches_size = 5;

	len = strlen(name);
	*howmany = 0;
	matches = RESIZE(matches, char *, matches_size);

	for (cnt = 0; cnt < var_alias.max; cnt++)
	{
		if (wild_match(name, var_alias.list[cnt]->name))
		{
			matches[*howmany] = m_strdup(var_alias.list[cnt]->name);
			*howmany += 1;
			if (*howmany == matches_size)
			{
				matches_size += 5;
				RESIZE(matches, char *, matches_size);
			}
		}
	}

	if (*howmany)
		matches[*howmany] = NULL;
	else
		new_free((char **)&matches);

	return matches;
}

/*
 * This function is strictly O(N).  This should probably be addressed.
 *
 * OK, so it isn't _strictly_ O(N) anymore, however, it is still O(N)
 * for N being all elements below the subarray being requested.  This
 * makes recursive /foreach's such as purge run much faster, however,
 * since each variable will be tested no more than its current depth
 * times, rather than every single time a /foreach is performed.
 *
 * In the worst case scenario where the entire variable space consists
 * of a single flat subarray, the new algorithm would perform no worse
 * than the old.
 */
char **	get_subarray_elements (char *root, int *howmany, int type)
{
	AliasSet *as;		/* XXXX */
	int pos, cnt, max;
	int cmp = 0;
	char **matches = NULL;
	int matches_size = 0;
	size_t end;
	char *last = NULL;

	if (type == COMMAND_ALIAS)
		as = &cmd_alias;
	else
		as = &var_alias;
	root = m_2dup(root, ".");
	find_array_item((array*)as, root, &max, &pos);

	if (0 > max) max = -max;
	*howmany = 0;
	cmp = strlen(root);
	new_free(&root);

	for (cnt = 0; cnt < max; cnt++, pos++)
	{
		end = strcspn(ARRAY_ITEM(as, pos)->name + cmp, ".");
		if (last && !my_strnicmp(ARRAY_ITEM(as, pos)->name, last, cmp + end))
			continue;
		if (*howmany >= matches_size)
		{
			matches_size = *howmany + 5;
			RESIZE(matches, char *, matches_size + 1);
		}
		matches[*howmany] = m_strndup(ARRAY_ITEM(as, pos)->name, cmp + end);
		last = matches[*howmany];
		*howmany += 1;
	}

	if (*howmany)
		matches[*howmany] = NULL;
	else
		new_free((char **)&matches);

	return matches;
}


/* XXX What is this doing here? */
static char *	parse_line_alias_special (char *name, char *what, char *args, int d1, int d2, void *arglist, int function)
{
	int	old_window_display = window_display;
	int	old_last_function_call_level = last_function_call_level;
	char	*result = NULL;

	window_display = 0;
	make_local_stack(name);
	prepare_alias_call(arglist, &args);
	if (function)
	{
		last_function_call_level = wind_index;
		add_local_alias("FUNCTION_RETURN", empty_string, 0);
	}
	window_display = old_window_display;

	will_catch_return_exceptions++;
	parse_line(NULL, what, args, d1, d2);
	will_catch_return_exceptions--;
	return_exception = 0;

	if (function)
	{
		result = get_variable("FUNCTION_RETURN");
		last_function_call_level = old_last_function_call_level;
	}
	destroy_local_stack();
	return result;
}

char *	parse_line_with_return (char *name, char *what, char *args, int d1, int d2)
{
	return parse_line_alias_special(name, what, args, d1, d2, NULL, 1);
}

/************************************************************************/
/*
 * call_user_function: Executes a user alias (by way of parse_command.
 * The actual function ends up being routed through execute_alias (below)
 * and we just keep track of the retval and stuff.  I dont know that anyone
 * depends on command completion with functions, so we can save a lot of
 * CPU time by just calling execute_alias() directly.
 */
char 	*call_user_function	(char *alias_name, char *args)
{
	char 	*result = NULL;
	char 	*sub_buffer;
	int 	cnt;
	void	*arglist = NULL;

	sub_buffer = get_cmd_alias(alias_name, &cnt, NULL, &arglist);
	if (cnt < 0)
		result = parse_line_alias_special(alias_name, sub_buffer, 
						args, 0, 1, arglist, 1);
	else if (x_debug & DEBUG_UNKNOWN)
		yell("Function call to non-existant alias [%s]", alias_name);

	if (!result)
		result = m_strdup(empty_string);

	return result;
}

/* XXX Ugh. */
void	call_user_alias	(char *alias_name, char *alias_stuff, char *args, void *arglist)
{
	parse_line_alias_special(alias_name, alias_stuff, args, 
					0, 1, arglist, 0);
}


/*
 * save_aliases: This will write all of the aliases to the FILE pointer fp in
 * such a way that they can be read back in using LOAD or the -l switch 
 */
void 	save_assigns	(FILE *fp, int do_all)
{
	int cnt = 0;

	for (cnt = 0; cnt < var_alias.max; cnt++)
	{
		if (!var_alias.list[cnt]->global || do_all)
		{
			if (var_alias.list[cnt]->stub)
				fprintf(fp, "STUB ");
			fprintf(fp, "ASSIGN %s %s\n", var_alias.list[cnt]->name, var_alias.list[cnt]->stuff);
		}
	}
}

void 	save_aliases (FILE *fp, int do_all)
{
	int	cnt = 0;

	for (cnt = 0; cnt < cmd_alias.max; cnt++)
	{
		if (!cmd_alias.list[cnt]->global || do_all)
		{
			if (cmd_alias.list[cnt]->stub)
				fprintf(fp, "STUB ");
			fprintf(fp, "ALIAS %s {%s}\n", cmd_alias.list[cnt]->name, cmd_alias.list[cnt]->stuff);
		}
	}	
}

static
void 	destroy_aliases (int type)
{
	int cnt = 0;
	AliasSet *my_array = NULL;

	if (type == COMMAND_ALIAS)
		my_array = &cmd_alias;
	else if (type == VAR_ALIAS)
		my_array = &var_alias;
	else if (type == VAR_ALIAS_LOCAL)
		my_array = &call_stack[wind_index].alias;

	for (cnt = 0; cnt < my_array->max; cnt++)
	{
		new_free((void **)&(my_array->list[cnt]->stuff));
		new_free((void **)&(my_array->list[cnt]->name));
		new_free((void **)&(my_array->list[cnt]->stub));
		new_free((void **)&(my_array->list[cnt]->filename));
		new_free((void **)&(my_array->list[cnt]));	/* XXX Hrm. */
	}
	for (cnt = 0; cnt < my_array->cache_size; cnt++)
		my_array->cache[cnt] = NULL;

	new_free((void **)&(my_array->list));
	my_array->max = my_array->max_alloc = 0;
}

/******************* RUNTIME STACK SUPPORT **********************************/

void 	make_local_stack 	(const char *name)
{
	wind_index++;

	if (wind_index >= max_wind)
	{
		int tmp_wind = wind_index;

		if (max_wind == -1)
			max_wind = 8;
		else
			max_wind <<= 1;

		RESIZE(call_stack, RuntimeStack, max_wind);
		for (; wind_index < max_wind; wind_index++)
		{
			call_stack[wind_index].alias.max = 0;
			call_stack[wind_index].alias.max_alloc = 0;
			call_stack[wind_index].alias.list = NULL;
			call_stack[wind_index].alias.func = strncmp;
			call_stack[wind_index].alias.cache = NULL;
			call_stack[wind_index].alias.cache_size = -1;
			call_stack[wind_index].current = NULL;
			call_stack[wind_index].name = NULL;
			call_stack[wind_index].parent = -1;
		}
		wind_index = tmp_wind;
	}

	/* Just in case... */
	destroy_local_stack();
	wind_index++;		/* XXXX - chicanery */

	if (name)
	{
		call_stack[wind_index].name = name;
		call_stack[wind_index].parent = -1;
	}
	else
	{
		call_stack[wind_index].name = empty_string;
		call_stack[wind_index].parent = wind_index - 1;
	}
	call_stack[wind_index].locked = 0;
}

int	find_locked_stack_frame	(void)
{
	int i;
	for (i = 0; i < wind_index; i++)
		if (call_stack[i].locked)
			return i;

	return -1;
}

void	bless_local_stack	(void)
{
	call_stack[wind_index].name = empty_string;
	call_stack[wind_index].parent = find_locked_stack_frame();
}

void 	destroy_local_stack 	(void)
{
	/*
	 * We clean up as best we can here...
	 */
	if (call_stack[wind_index].alias.list)
		destroy_aliases(VAR_ALIAS_LOCAL);
	if (call_stack[wind_index].current)
		call_stack[wind_index].current = 0;
	if (call_stack[wind_index].name)
		call_stack[wind_index].name = 0;

	wind_index--;
}

void 	set_current_command 	(char *line)
{
	call_stack[wind_index].current = line;
}

void 	unset_current_command 	(void)
{
	call_stack[wind_index].current = NULL;
}

void	lock_stack_frame 	(void)
{
	call_stack[wind_index].locked = 1;
}

void	unlock_stack_frame	(void)
{
	int lock = find_locked_stack_frame();
	if (lock >= 0)
		call_stack[lock].locked = 0;
}

void 	dump_call_stack 	(void)
{
	int my_wind_index = wind_index;
	if (wind_index >= 0)
	{
		say("Call stack");
		while (my_wind_index--)
			say("[%3d] %s", my_wind_index, call_stack[my_wind_index].current);
		say("End of call stack");
	}
}

void 	panic_dump_call_stack 	(void)
{
	int my_wind_index = wind_index;
	fprintf(stderr, "Call stack\n");
	if (wind_index >= 0)
	{
		while (my_wind_index--)
			fprintf(stderr, "[%3d] %s\n", my_wind_index, call_stack[my_wind_index].current);
	}
	else
		fprintf(stderr, "Stack is corrupted [wind_index is %d], sorry.\n",
			wind_index);
	fprintf(stderr, "End of call stack\n");
}


/*
 * You may NOT call this unless youre about to exit.
 * If you do (call this when youre not about to exit), and you do it more 
 * than a few times, max_wind will get absurdly large.  So dont do it.
 *
 * XXXX - this doesnt clean up everything -- but do i care?
 */
void 	destroy_call_stack 	(void)
{
	wind_index = -1;
	new_free((char **)&call_stack);
}

/******************* expression and text parsers ***************************/
/* XXXX - bogus for now */
#include "expr2.c"
#include "expr.c"


/****************************** ALIASCTL ************************************/
#define EMPTY empty_string
#define RETURN_EMPTY return m_strdup(EMPTY)
#define RETURN_IF_EMPTY(x) if (empty( x )) RETURN_EMPTY
#define GET_INT_ARG(x, y) {RETURN_IF_EMPTY(y); x = my_atol(safe_new_next_arg(y, &y));}
#define GET_FLOAT_ARG(x, y) {RETURN_IF_EMPTY(y); x = atof(safe_new_next_arg(y, &y));}
#define GET_STR_ARG(x, y) {RETURN_IF_EMPTY(y); x = new_next_arg(y, &y);RETURN_IF_EMPTY(x);}
#define RETURN_STR(x) return m_strdup((x) ? (x) : EMPTY)
#define RETURN_INT(x) return m_strdup(ltoa((x)))

/* Used by function_aliasctl */
/* MUST BE FIXED */
char 	*aliasctl 	(char *input)
{
	int list = -1;
	char *listc;
	enum { EXISTS, GET, SET, MATCH, PMATCH, GETPACKAGE, SETPACKAGE } op;

	GET_STR_ARG(listc, input);
	if (!my_strnicmp(listc, "AS", 2))
		list = VAR_ALIAS;
	else if (!my_strnicmp(listc, "AL", 2))
		list = COMMAND_ALIAS;
	else if (!my_strnicmp(listc, "LO", 2))
		list = VAR_ALIAS_LOCAL;
	else
		RETURN_EMPTY;

	GET_STR_ARG(listc, input);
	if (!my_strnicmp(listc, "GETP", 4))
		op = GETPACKAGE;
	else if (!my_strnicmp(listc, "G", 1))
		op = GET;
	else if (!my_strnicmp(listc, "SETP", 4))
		op = SETPACKAGE;
	else if (!my_strnicmp(listc, "S", 1))
		op = SET;
	else if (!my_strnicmp(listc, "M", 1))
		op = MATCH;
	else if (!my_strnicmp(listc, "P", 1))
		op = PMATCH;
	else if (!my_strnicmp(listc, "E", 1))
		op = EXISTS;
	else
		RETURN_EMPTY;

	GET_STR_ARG(listc, input);
	switch (op)
	{
		case (GET) :
		case (EXISTS) :
		case (GETPACKAGE) :
		case (SETPACKAGE) :
		{
			Alias *alias = NULL;
			AliasSet *a_list;
			int dummy;

			upper(listc);
			if (list == VAR_ALIAS_LOCAL)
				alias = find_local_alias(listc, &a_list);
			else if (list == VAR_ALIAS)
				alias = find_var_alias(listc);
			else
				alias = find_cmd_alias(listc, &dummy);

			if (alias)
			{
				if (op == GET)
					RETURN_STR(alias->stuff);
				else if (op == EXISTS)
					RETURN_INT(1);
				else if (op == GETPACKAGE)
					RETURN_STR(alias->filename);
				else /* op == SETPACKAGE */
				{
					malloc_strcpy(&alias->filename, input);
					RETURN_INT(1);
				}
			}
			else
			{
				if (op == GET || op == GETPACKAGE)
					RETURN_EMPTY;
				else	/* EXISTS or SETPACKAGE */
					RETURN_INT(0);
			}
		}
		case (SET) :
		{
			upper(listc);
			if (list == VAR_ALIAS_LOCAL)
				add_local_alias(listc, input, 0);
			else if (list == VAR_ALIAS)
				add_var_alias(listc, input, 0);
			else
				add_cmd_alias(listc, NULL, input);

			RETURN_INT(1);
		}
		case (MATCH) :
		{
			char **mlist = NULL;
			char *mylist = NULL;
			size_t	mylistclue = 0;
			int num = 0, ctr;

			if (!my_stricmp(listc, "*"))
				listc = empty_string;

			upper(listc);

			if (list == COMMAND_ALIAS)
				mlist = glob_cmd_alias(listc, &num);
			else
				mlist = glob_assign_alias(listc, &num);

			for (ctr = 0; ctr < num; ctr++)
			{
				m_sc3cat(&mylist, space, mlist[ctr], &mylistclue);
				new_free((char **)&mlist[ctr]);
			}
			new_free((char **)&mlist);
			if (mylist)
				return mylist;
			RETURN_EMPTY;
		}
		case (PMATCH) : 
		{
			char **	mlist = NULL;
			char *	mylist = NULL;
			size_t	mylistclue = 0;
			int	num = 0,
				ctr;

			if (list == COMMAND_ALIAS)
				mlist = pmatch_cmd_alias(listc, &num);
			else
				mlist = pmatch_assign_alias(listc, &num);

			for (ctr = 0; ctr < num; ctr++)
			{
				m_sc3cat(&mylist, space, mlist[ctr], &mylistclue);
				new_free((char **)&mlist[ctr]);
			}
			new_free((char **)&mlist);
			if (mylist)
				return mylist;
			RETURN_EMPTY;
		}
		default :
			error("aliasctl: Error");
			RETURN_EMPTY;
	}
	RETURN_EMPTY;
}

/*************************** stacks **************************************/
typedef	struct	aliasstacklist
{
	int	which;
	char	*name;
	Alias	*list;
	struct aliasstacklist *next;
}	AliasStack;

static  AliasStack *	alias_stack = NULL;
static	AliasStack *	assign_stack = NULL;

void	do_stack_alias (int type, char *args, int which)
{
	char		*name;
	AliasStack	*aptr, **aptrptr;
	Alias		*alptr;
	int		cnt;
	
	if (args)
		upper(args);

	if (which == STACK_DO_ALIAS)
	{
		name = "ALIAS";
		aptrptr = &alias_stack;
	}
	else
	{
		name = "ASSIGN";
		aptrptr = &assign_stack;
	}

	if (!*aptrptr && (type == STACK_POP || type == STACK_LIST))
	{
		say("%s stack is empty!", name);
		return;
	}

	if (type == STACK_PUSH)
	{
		if (which == STACK_DO_ALIAS)
		{
			int	i;

			/* Um. Can't delete what we want to keep! :P */
			if ((alptr = find_cmd_alias(args, &cnt)))
				remove_from_array((array *)&cmd_alias, 
					alptr->name);

			/* XXX This shouldn't need be done here... */
			for (i = 0; i < cmd_alias.cache_size; i++)
			{
				if (cmd_alias.cache[i] == alptr)
					cmd_alias.cache[i] = NULL;
			}
		}
		else
		{
			int	i;

			if ((alptr = find_var_alias(args)))
				remove_from_array((array *)&var_alias, 
					alptr->name);

			/* XXX This shouldn't need be done here... */
			for (i = 0; i < var_alias.cache_size; i++)
			{
				if (var_alias.cache[i] == alptr)
					var_alias.cache[i] = NULL;
			}
		}

		aptr = (AliasStack *)new_malloc(sizeof(AliasStack));
		aptr->list = alptr;
		aptr->name = m_strdup(args);
		aptr->next = aptrptr ? *aptrptr : NULL;
		*aptrptr = aptr;
		return;
	}

	if (type == STACK_POP)
	{
		AliasStack *prev = NULL;

		for (aptr = *aptrptr; aptr; prev = aptr, aptr = aptr->next)
		{
			/* have we found it on the stack? */
			if (!my_stricmp(args, aptr->name))
			{
				/* remove it from the list */
				if (prev == NULL)
					*aptrptr = aptr->next;
				else
					prev->next = aptr->next;

				/* throw away anything we already have */
				delete_cmd_alias(args, 0);

				/* put the new one in. */
				if (aptr->list)
				{
					if (which == STACK_DO_ALIAS)
						add_to_array((array *)&cmd_alias, (array_item *)(aptr->list));
					else
						add_to_array((array *)&var_alias, (array_item *)(aptr->list));
				}

				/* free it */
				new_free((char **)&aptr->name);
				new_free((char **)&aptr);
				return;
			}
		}
		say("%s is not on the %s stack!", args, name);
		return;
	}
	if (type == STACK_LIST)
	{
		AliasStack	*tmp;

		say("%s STACK LIST", name);
		for (tmp = *aptrptr; tmp; tmp = tmp->next)
		{
			if (!tmp->list)
				say("\t%s\t<Placeholder>", tmp->name);

			else if (tmp->list->stub)
				say("\t%s STUBBED TO %s", tmp->name, tmp->list->stub);

			else
				say("\t%s\t%s", tmp->name, tmp->list->stuff);
		}
		return;
	}
	say("Unknown STACK type ??");
}

