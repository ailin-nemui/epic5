/* $EPIC: alias.c,v 1.61 2004/09/15 10:54:08 crazyed Exp $ */
/*
 * alias.c -- Handles the whole kit and caboodle for aliases.
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright © 1997, 2003 EPIC Software Labs
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
#include "functions.h"
#include "words.h"
#include "reg.h"
#include "vars.h"

#define LEFT_BRACE '{'
#define RIGHT_BRACE '}'
#define LEFT_BRACKET '['
#define RIGHT_BRACKET ']'
#define LEFT_PAREN '('
#define RIGHT_PAREN ')'
#define DOUBLE_QUOTE '"'

/**************************** INTERMEDIATE INTERFACE *********************/
enum ARG_TYPES {
	WORD,
	UWORD,
	DWORD,
	QWORD
};

/* Ugh. XXX Bag on the side */
struct ArgListT {
	char *	vars[32];
	char *	defaults[32];
	int	words[32];
	enum	ARG_TYPES types[32];
	int	void_flag;
	int	dot_flag;
};
typedef struct ArgListT ArgList;
ArgList	*parse_arglist (char *arglist);
void	destroy_arglist (ArgList **);


/*
 * This is the description of an alias entry
 * This is an ``array_item'' structure
 */
typedef	struct	SymbolStru
{
	char	*name;			/* name of alias */
	u_32int_t hash;			/* Hash of the name */

	char *	user_variable;
	int	user_variable_stub;
	char *	user_variable_package;

	char *	user_command;
	int	user_command_stub;
	char *	user_command_package;
	ArgList *arglist;		/* List of arguments to alias */

        void    (*builtin_command) (const char *, char *, const char *);
	char *	(*builtin_function) (char *);
	char *	(*builtin_expando) (void);
	IrcVariable *	builtin_variable;

struct SymbolStru *	saved;		/* For stacks */
	int	saved_hint;
}	Symbol;

#define SAVED_VAR		 1
#define SAVED_CMD		 2
#define SAVED_BUILTIN_CMD	 4
#define SAVED_BUILTIN_FUNCTION	 8
#define SAVED_BUILTIN_EXPANDO	16
#define SAVED_BUILTIN_VAR	32

/*
 * This is the description for a list of aliases
 * This is an ``array_set'' structure
 */
#define ALIAS_CACHE_SIZE 4

typedef struct	SymbolSetStru
{
	Symbol **	list;
	int		max;
	int		max_alloc;
	alist_func 	func;
	hash_type	hash;
}	SymbolSet;

static SymbolSet globals = 	{ NULL, 0, 0, strncmp, HASH_INSENSITIVE };

static	Symbol *lookup_symbol (const char *name);
static	Symbol *find_local_alias   (const char *name, SymbolSet **list);

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
	SymbolSet alias;	/* Local variables */
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
extern	void    add_var_alias      (Char *name, Char *stuff, int noisy);
extern  void    add_local_alias    (Char *name, Char *stuff, int noisy);
extern  void    add_cmd_alias      (Char *name, ArgList *arglist, Char *stuff);
extern  void    add_var_stub_alias (Char *name, Char *stuff);
extern  void    add_cmd_stub_alias (Char *name, Char *stuff);
extern	void	add_builtin_cmd_alias  (Char *, void (*)(Char *, char *, Char *));
extern	void	add_builtin_func_alias (Char *, char *(*)(char *));
extern	void	add_builtin_expando    (Char *, char *(*)(void));

static	void	delete_var_alias   (Char *name, int noisy);
static	void	delete_cmd_alias   (Char *name, int noisy);
/*	void	delete_local_alias (Char *name); 		*/

static	void	unload_cmd_alias   (Char *fn);
static	void	unload_var_alias   (Char *fn);
static	void	list_cmd_alias     (Char *name);
static	void	list_var_alias     (Char *name);
static	void	list_local_alias   (Char *name);
static	void 	destroy_cmd_aliases    (SymbolSet *);
static	void 	destroy_var_aliases    (SymbolSet *);
static	void 	destroy_builtin_commands    (SymbolSet *);
static	void 	destroy_builtin_functions    (SymbolSet *);
static	void 	destroy_builtin_variables    (SymbolSet *);
static	void 	destroy_builtin_expandos    (SymbolSet *);

extern	char *  get_variable       (Char *name);
extern	char ** glob_cmd_alias          (Char *name, int *howmany, int maxret, int start, int rev);
extern	char ** glob_assign_alias	(Char *name, int *howmany, int maxret, int start, int rev);
extern	char *  get_cmd_alias           (Char *name, void **args, 
					 void (**func) (const char *, char *, 
					                const char *));
extern	char ** get_subarray_elements   (Char *root, int *howmany, int type);


static	char *	get_variable_with_args (Char *str, Char *args);

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
		say("Sorry, this EPIC does not have caches");
		return;
	}

	/*
	 * Canonicalize the alias name
	 */
	real_name = remove_brackets(name, NULL);

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
		    ssize_t	span;

		    args++;
		    if ((span = MatchingBracket(args, '(', ')')) < 0)
			say("Unmatched lparen in ALIAS %s", real_name);
		    else
		    {
			ptr = args + span;
			*ptr++ = 0;
			while (*ptr && my_isspace(*ptr))
				ptr++;
			if (!*ptr)
				say("Missing alias body in ALIAS %s", 
					real_name);

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
		    ssize_t	span;

		    args++;
		    if ((span = MatchingBracket(args, '{', '}')) < 0)
			say("Unmatched brace in ALIAS %s", real_name);
		    else
		    {
			ptr = args + span;
			*ptr++ = 0;
			while (*ptr && my_isspace(*ptr))
				ptr++;

			if (*ptr)
				say("Junk [%s] after closing brace in ALIAS %s", 
					ptr, real_name);

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
	real_name = remove_brackets(name, NULL);

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
const 	char 	*usage = "Usage: STUB (alias|assign) <name> <file> [<file> ...]";

	/*
	 * The first argument is the type of stub to make
	 * (alias or assign)
	 */
	if (!(cmd = upper(next_arg(args, &args))))
	{
		error("Missing stub type");
		say("%s", usage);
		return;
	}

	if (!strncmp(cmd, "ALIAS", strlen(cmd)))
		type = COMMAND_ALIAS;
	else if (!strncmp(cmd, "ASSIGN", strlen(cmd)))
		type = VAR_ALIAS;
	else
	{
		error("[%s] is an Unrecognized stub type", cmd);
		say("%s", usage);
		return;
	}

	/*
	 * The next argument is the name of the item to be stubbed.
	 * This is not optional.
	 */
	if (!(name = next_arg(args, &args)))
	{
		error("Missing alias name");
		say("%s", usage);
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
		say("%s", usage);
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

		real_name = remove_brackets(name, NULL);
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
		args = LOCAL_COPY(empty_string);

	if (!my_strnicmp(name, "-dump", 2))	/* Illegal name anyways */
	{
		destroy_var_aliases(&call_stack[wind_index].alias);
		return;
	}

	while (name && *name)
	{
		char 	*next_name;
		char	*real_name;

		if ((next_name = strchr(name, ',')))
			*next_name++ = 0;

		real_name = remove_brackets(name, NULL);
		add_local_alias(real_name, args, 1);
		new_free(&real_name);
		name = next_name;
	}
}

BUILT_IN_COMMAND(dumpcmd)
{
	const char 	*blah = empty_string;
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
			destroy_var_aliases(&globals);
			dumped++;
		}
		if (!strncmp(blah, "ALIAS", strlen(blah)) || all)
		{
			say("Dumping your global aliases");
			destroy_cmd_aliases(&globals);
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
	for (this_term = arglist; *this_term; this_term = next_term)
	{
		while (isspace(*this_term))
			this_term++;
		next_in_comma_list(this_term, &next_term);
		if (!(varname = next_arg(this_term, &this_term)))
			continue;

		args->types[arg_count] = WORD;
		if (!my_stricmp(varname, "void")) {
			args->void_flag = 1;
			break;
		} else if (!my_stricmp(varname, "...")) {
			args->dot_flag = 1;
			break;
		} else {
			args->vars[arg_count] = malloc_strdup(varname);
			args->defaults[arg_count] = NULL;
			args->words[arg_count] = 1;

			while ((modifier = next_arg(this_term, &this_term)))
			{
				if (!(value = new_next_arg(this_term, &this_term)))
						break;
				if (!my_stricmp(modifier, "default"))
				{
					args->defaults[arg_count] = malloc_strdup(value);
				}
				else if (!my_stricmp(modifier, "words"))
				{
					args->types[arg_count] = WORD;
					args->words[arg_count] = atol(value);
				}
				else if (!my_stricmp(modifier, "uwords"))
				{
					args->types[arg_count] = UWORD;
					args->words[arg_count] = atol(value);
				}
				else if (!my_stricmp(modifier, "qwords"))
				{
					args->types[arg_count] = QWORD;
					args->words[arg_count] = atol(value);
				}
				else if (!my_stricmp(modifier, "dwords"))
				{
					args->types[arg_count] = DWORD;
					args->words[arg_count] = atol(value);
				}
				else
				{
					yell("Bad modifier %s", modifier);
				}
			}
			arg_count++;
		}
	}
	args->vars[arg_count] = NULL;
	return args;
}

void	destroy_arglist (ArgList **arglist)
{
	int	i = 0;

	if (!arglist || !*arglist)
		return;

	for (i = 0; ; i++)
	{
		if (!(*arglist)->vars[i])
			break;
		new_free(&(*arglist)->vars[i]);
		new_free(&(*arglist)->defaults[i]);
	}
	new_free((char **)arglist);
}

static ArgList *clone_arglist (ArgList *orig)
{
	ArgList *args;
	int	i = 0;

	if (!orig)
		return NULL;

	args = new_malloc(sizeof(ArgList));
	for (i = 0; i < 32; i++)
	{
		args->vars[i] = NULL;
		args->defaults[i] = NULL;
		args->words[i] = 0;
		args->types[i] = WORD;
	}
	for (i = 0; ; i++)
	{
		if (!orig->vars[i])
			break;
		malloc_strcpy(&args->vars[i], orig->vars[i]);
		malloc_strcpy(&args->defaults[i], orig->defaults[i]);
		args->words[i] = orig->words[i];
		args->types[i] = orig->types[i];
	}
	args->void_flag = orig->void_flag;
	args->dot_flag = orig->dot_flag;

	return args;
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
		int	type = 0, do_dequote_it;

		switch (args->types[i])
		{
			case WORD:
				if (!(x_debug & DEBUG_EXTRACTW))
				{
					type = DWORD_NEVER;
					do_dequote_it = 0;
				}
				else if (args->words[i] <= 1)
				{
					type = DWORD_YES;
					do_dequote_it = 1;
				}
				else
				{
					type = DWORD_ALWAYS;
					do_dequote_it = 0;
				}
				break;
			case UWORD:
				type = DWORD_NEVER;
				do_dequote_it = 0;
				break;
			case DWORD:
				type = DWORD_ALWAYS;
				do_dequote_it = 1;
				break;
			case QWORD:
				type = DWORD_ALWAYS;
				do_dequote_it = 0;
				break;
			default:
				panic("Alias list argument [%d] has unsupported typed [%d]", i, args->types[i]);
				/* NOTREACHED */
				return;
		}

		/* Last argument on the list and no ... argument? */
		if (!args->vars[i + 1] && !args->dot_flag && !args->void_flag)
		{
			next_val = *stuff;
			*stuff = endstr(*stuff);
		}

		/* Yank the next word from the arglist */
		else
		{
			next_val = universal_next_arg_count(*stuff, stuff, args->words[i], type, 0, "\"");
		}

		if (!next_val || !*next_val)
		{
			if ((next_val = args->defaults[i]))
				next_val = expanded = expand_alias(next_val, *stuff, NULL);
			else
				next_val = LOCAL_COPY(empty_string);
		}

		/* Do dequoting last so it's useful for ``defaults'' */
		if (next_val && *next_val && do_dequote_it == 1)
		{
			size_t clue;
			clue = strlen(next_val);
			dequoter(&next_val, &clue, 1, type, "\"");
		}

		/* Add the local variable */
		add_local_alias(args->vars[i], next_val, 0);
		if (expanded)
			new_free(&expanded);
	}

	/* Throw away rest of args if wanted */
	if (args->void_flag)
		*stuff = endstr(*stuff);
}

static char *	print_arglist (ArgList *args)
{
	char *	retval = NULL;
	size_t	cluep = 0;
	int	i;

	if (!args)
		return NULL;

	for (i = 0; args->vars[i]; i++)
	{
	    if (i > 0)
		malloc_strcat_c(&retval, ", ", &cluep);

	    malloc_strcat_c(&retval, args->vars[i], &cluep);

	    switch (args->types[i])
	    {
		case WORD:
		    malloc_strcat_c(&retval, " words ", &cluep);
		    break;
		case UWORD:
		    malloc_strcat_c(&retval, " uwords ", &cluep);
		    break;
		case QWORD:
		    malloc_strcat_c(&retval, " qwords ", &cluep);
		    break;
		case DWORD:
		    malloc_strcat_c(&retval, " dwords ", &cluep);
		    break;
	    }
	    malloc_strcat_c(&retval, ltoa(args->words[i]), &cluep);

	    if (args->defaults[i])
	    {
		malloc_strcat_c(&retval, " default ", &cluep);
		malloc_strcat_c(&retval, args->defaults[i], &cluep);
	    }
	}

	if (args->void_flag)
	    malloc_strcat_c(&retval, ", void", &cluep);
	else if (args->dot_flag)
	    malloc_strcat_c(&retval, ", ...", &cluep);

	return retval;
}


#undef ew_next_arg
/***************************************************************************/

static Symbol *make_new_Symbol (const char *name)
{
	Symbol *tmp = (Symbol *) new_malloc(sizeof(Symbol));
	tmp->name = malloc_strdup(name);

	tmp->user_variable = NULL;
	tmp->user_variable_stub = 0;
	tmp->user_variable_package = NULL;

	tmp->user_command = NULL;
	tmp->user_command_stub = 0;
	tmp->user_command_package = NULL;
	tmp->arglist = NULL;

	tmp->builtin_command = NULL;
	tmp->builtin_function = NULL;
	tmp->builtin_expando = NULL;
	tmp->builtin_variable = NULL;

	tmp->saved = NULL;
	tmp->saved_hint = 0;
	return tmp;
}

static int	GC_symbol (Symbol *item, array *list, int loc)
{
	if (item->user_variable)
		return 0;
	if (item->user_command)
		return 0;
	if (item->builtin_command)
		return 0;
	if (item->builtin_function)
		return 0;
	if (item->builtin_expando)
		return 0;
	if (item->builtin_variable)
		return 0;
	if (item->saved)
		return 0;

	if (list && loc >= 0)
		array_pop(list, loc);

	new_free(&item->user_variable_package);
	new_free(&item->user_command_package);
	destroy_arglist(&item->arglist);
	new_free(&(item->name));
	new_free((char **)&item);
	return 1;
}

/* * * */
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
void	add_var_alias	(const char *orig_name, const char *stuff, int noisy)
{
	const char 	*ptr;
	Symbol 	*tmp = NULL;
	int	local = 0;
	char	*save, *name;

	save = name = remove_brackets(orig_name, NULL);
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
		tmp = (Symbol *) find_array_item ((array *)&globals, name, &cnt, &loc);
		if (!tmp || cnt >= 0)
		{
			tmp = make_new_Symbol(name);
			add_to_array ((array *)&globals, (array_item *)tmp);
		}
		if (!tmp->user_variable_package)
			tmp->user_variable_package = malloc_strdup(current_package());
		else if (strcmp(tmp->user_variable_package, current_package()))
			malloc_strcpy(&(tmp->user_variable_package), current_package());

		malloc_strcpy(&(tmp->user_variable), stuff);
		tmp->user_variable_stub = 0;


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

void	add_var_stub_alias  (const char *orig_name, const char *stuff)
{
	Symbol *tmp = NULL;
	const char *ptr;
	char *name;

	name = remove_brackets(orig_name, NULL);

	ptr = after_expando(name, 1, NULL);
	if (*ptr)
	{
		error("Assign names may not contain '%c' (You asked for [%s])", *ptr, name);
		new_free(&name);
		return;
	}
	else if (!strcmp(name, "FUNCTION_RETURN"))
	{
		error("You may not stub the FUNCTION_RETURN variable.");
		new_free(&name);
		return;
	}


	if (!(tmp = lookup_symbol(name)))
	{
		tmp = make_new_Symbol(name);
		tmp->user_variable_package = malloc_strdup(current_package());
		add_to_array ((array *)&globals, (array_item *)tmp);
	}
	else if (tmp->user_variable_package)
	    if (strcmp(tmp->user_variable_package, current_package()))
		malloc_strcpy(&tmp->user_variable_package, current_package());

	malloc_strcpy(&(tmp->user_variable), stuff);
	tmp->user_variable_stub = 1;

	say("Assign %s stubbed to file %s", name, stuff);
	new_free(&name);
	return;
}

void	add_local_alias	(const char *orig_name, const char *stuff, int noisy)
{
	const char 	*ptr;
	Symbol 	*tmp = NULL;
	SymbolSet *list = NULL;
	char *	name;

	name = remove_brackets(orig_name, NULL);

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
		tmp = make_new_Symbol(name);
		add_to_array ((array *)list, (array_item *)tmp);
	}

	/* Fill in the interesting stuff */
	malloc_strcpy(&(tmp->user_variable), stuff);
	if (tmp->user_variable)		/* Oh blah. */
	{
		if (x_debug & DEBUG_LOCAL_VARS && noisy)
		    yell("Assign %s (local) added [%s]", name, stuff);
		else if (noisy)
		    say("Assign %s (local) added [%s]", name, stuff);
	}

	new_free(&name);
	return;
}

/* * * */
void	add_cmd_alias	(const char *orig_name, ArgList *arglist, const char *stuff)
{
	Symbol *tmp = NULL;
	char *name;
	char *argstr;

	name = remove_brackets(orig_name, NULL);
	if (!(tmp = lookup_symbol(name)))
	{
		tmp = make_new_Symbol(name);
		tmp->user_command_package = malloc_strdup(current_package());
		add_to_array ((array *)&globals, (array_item *)tmp);
	}
	else if (tmp->user_command_package)
	    if (strcmp(tmp->user_command_package, current_package()))
		malloc_strcpy(&(tmp->user_command_package), current_package());

	malloc_strcpy(&(tmp->user_command), stuff);
	tmp->user_command_stub = 0;
	destroy_arglist(&tmp->arglist);
	tmp->arglist = arglist;

	argstr = print_arglist(arglist);
	if (argstr)
	{
	    say("Alias	%s (%s) added [%s]", name, argstr, stuff);
	    new_free(&argstr);
	}
	else
	    say("Alias	%s added [%s]", name, stuff);

	new_free(&name);
	return;
}


void	add_cmd_stub_alias  (const char *orig_name, const char *stuff)
{
	Symbol *tmp = NULL;
	char *name;

	name = remove_brackets(orig_name, NULL);
	if (!(tmp = lookup_symbol(name)))
	{
		tmp = make_new_Symbol(name);
		tmp->user_command_package = malloc_strdup(current_package());
		add_to_array ((array *)&globals, (array_item *)tmp);
	}
	else if (tmp->user_command_package)
	    if (strcmp(tmp->user_command_package, current_package()))
		malloc_strcpy(&(tmp->user_command_package), current_package());

	malloc_strcpy(&(tmp->user_command), stuff);
	tmp->user_command_stub = 1;

	say("Alias %s stubbed to file %s", name, stuff);

	new_free(&name);
	return;
}

void	add_builtin_cmd_alias	(const char *name, void (*func) (const char *, char *, const char *))
{
	Symbol *tmp = NULL;
	int cnt, loc;

	tmp = (Symbol *) find_array_item ((array *)&globals, name, &cnt, &loc);
	if (!tmp || cnt >= 0)
	{
		tmp = make_new_Symbol(name);
		add_to_array ((array *)&globals, (array_item *)tmp);
	}

	tmp->builtin_command = func;
	return;
}

void	add_builtin_func_alias	(const char *name, char * (*func) (char *))
{
	Symbol *tmp = NULL;
	int cnt, loc;

	tmp = (Symbol *) find_array_item ((array *)&globals, name, &cnt, &loc);
	if (!tmp || cnt >= 0)
	{
		tmp = make_new_Symbol(name);
		add_to_array ((array *)&globals, (array_item *)tmp);
	}

	tmp->builtin_function = func;
	return;
}

void	add_builtin_expando	(const char *name, char *(*func) (void))
{
	Symbol *tmp = NULL;
	int cnt, loc;

	tmp = (Symbol *) find_array_item ((array *)&globals, name, &cnt, &loc);
	if (!tmp || cnt >= 0)
	{
		tmp = make_new_Symbol(name);
		add_to_array ((array *)&globals, (array_item *)tmp);
	}

	tmp->builtin_expando = func;
	return;
}

void	add_builtin_variable_alias (const char *name, IrcVariable *var)
{
	Symbol *tmp = NULL;
	int cnt, loc;

	tmp = (Symbol *) find_array_item ((array *)&globals, name, &cnt, &loc);
	if (!tmp || cnt >= 0)
	{
		tmp = make_new_Symbol(name);
		add_to_array((array *)&globals, (array_item *)tmp);
	}

	tmp->builtin_variable = var;
	return;
}



/************************ LOW LEVEL INTERFACE *************************/

static	int	unstub_in_progress = 0;

static Symbol *unstub_variable (Symbol *item)
{
	char *name;
	char *file;

	if (!item)
		return NULL;

	name = LOCAL_COPY(item->name);
	if (item->user_variable_stub)
	{
		file = LOCAL_COPY(item->user_variable);
		delete_var_alias(item->name, 0);

		if (!unstub_in_progress)
		{
			unstub_in_progress = 1;
			load("LOAD", file, empty_string);
			unstub_in_progress = 0;
		}

		/* Look the name up again */
		item = lookup_symbol(name);
	}
	return item;
}

static Symbol *unstub_command (Symbol *item)
{
	char *name;
	char *file;

	if (!item)
		return NULL;

	name = LOCAL_COPY(item->name);
	if (item->user_command_stub)
	{
		file = LOCAL_COPY(item->user_command);
		delete_cmd_alias(item->name, 0);

		if (!unstub_in_progress)
		{
			unstub_in_progress = 1;
			load("LOAD", file, empty_string);
			unstub_in_progress = 0;
		}

		/* Look the name up again */
		item = lookup_symbol(name);
	}
	return item;
}


/*
 * 'name' is expected to already be in canonical form (uppercase, dot notation)
 */
static Symbol *	lookup_symbol (const char *name)
{
	Symbol *	item = NULL;
	int 	loc;
	int 	cnt = 0;

	name += strspn(name, ":");		/* Accept ::global */

	item = (Symbol *)find_array_item((array *)&globals, name, &cnt, &loc);
	if (cnt >= 0)
		item = NULL;
	if (item && item->user_variable_stub)
		item = unstub_variable(item);
	if (item && item->user_command_stub)
		item = unstub_command(item);
	return item;
}

/*
 * An example will best describe the semantics:
 *
 * A local variable will be returned if and only if there is already a
 * variable that is exactly ``name'', or if there is a variable that
 * is an exact leading subset of ``name'' and that variable ends in a
 * period (a dot).
 */
static Symbol *	find_local_alias (const char *orig_name, SymbolSet **list)
{
	Symbol 	*alias = NULL;
	int 	c = wind_index;
	const char 	*ptr;
	int 	implicit = -1;
	int	function_return = 0;
	char *	name;

	/* No name is an error */
	if (!orig_name)
		return NULL;

	name = remove_brackets(orig_name, NULL);
	upper(name);

	ptr = after_expando(name, 1, NULL);
	if (*ptr) {
		new_free(&name);
		return NULL;
	}

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
			int x, cnt, loc;

			/* We can always hope that the variable exists */
			find_array_item((array*)&call_stack[c].alias, name, &cnt, &loc);
			if (cnt < 0)
				alias = call_stack[c].alias.list[loc];

			/* XXXX - This is bletcherous */
			/*
			 * I agree, however, there doesn't seem to be any other
			 * reasonable way to do it.  I guess launching multiple
			 * binary searches on relevant portions of name would
			 * do it, but the overhead could(?) do damage.
			 *
			 * Actualy, thinking about it again, seperating the
			 * implicit variables from the normal ones would
			 * probably work.
			 */
			else if (strchr(name, '.'))
			    for (x = 0; x < loc; x++)
			    {
				Symbol *item;
				size_t len;

				item = call_stack[c].alias.list[x];
				len =  strlen(item->name);

				if (streq(item->name, name) == len) {
					if (item->name[len-1] == '.') {
						implicit = c;
						break;
					}
				}
			}

			if (!alias && implicit >= 0)
			{
				alias = make_new_Symbol(name);
				add_to_array ((array *)&call_stack[implicit].alias, (array_item *)alias);
			}
		}

		if (alias)
		{
			if (x_debug & DEBUG_LOCAL_VARS) 
				yell("I found [%s] in level [%d] (%s)", name, c, alias->user_variable);
			break;
		}

		if (*call_stack[c].name || call_stack[c].parent == -1 ||
		    (function_return && last_function_call_level != -1))
		{
			if (x_debug & DEBUG_LOCAL_VARS) 
				yell("I didnt find [%s], stopped at level [%d]", name, c);
			break;
		}
	}

	new_free(&name);

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


/* * */
static void	delete_var_alias (const char *orig_name, int noisy)
{
	Symbol *item;
	char *	name;
	int	cnt, loc;

	name = remove_brackets(orig_name, NULL);
	upper(name);
	item = (Symbol *)find_array_item((array *)&globals, name, &cnt, &loc);
	if (item && cnt < 0)
	{
		new_free(&item->user_variable);
		item->user_variable_stub = 0;
		new_free(&(item->user_variable_package));
		GC_symbol(item, (array *)&globals, loc);
		if (noisy)
			say("Assign %s removed", name);
	}
	else if (noisy)
		say("No such assign: %s", name);
	new_free(&name);
}

static void	delete_cmd_alias (const char *orig_name, int noisy)
{
	Symbol *item;
	char *	name;
	int	cnt, loc;

	name = remove_brackets(orig_name, NULL);
	upper(name);
	item = (Symbol *)find_array_item((array *)&globals, name, &cnt, &loc);
	if (item && cnt < 0)
	{
		new_free(&item->user_command);
		item->user_command_stub = 0;
		new_free(&(item->user_command_package));
		destroy_arglist(&item->arglist);
		GC_symbol(item, (array *)&globals, loc);
		if (noisy)
			say("Alias %s removed", name);
	}
	else if (noisy)
		say("No such alias: %s", name);
	new_free(&name);
}

void	delete_builtin_command (const char *orig_name)
{
	Symbol *item;
	char *	name;
	int	cnt, loc;

	name = remove_brackets(orig_name, NULL);
	upper(name);
	item = (Symbol *)find_array_item((array *)&globals, name, &cnt, &loc);
	if (item && cnt < 0)
	{
		item->builtin_command = NULL;
		GC_symbol(item, (array *)&globals, loc);
	}
	new_free(&name);
}

void	delete_builtin_function (const char *orig_name)
{
	Symbol *item;
	char *	name;
	int	cnt, loc;

	name = remove_brackets(orig_name, NULL);
	upper(name);
	item = (Symbol *)find_array_item((array *)&globals, name, &cnt, &loc);
	if (item && cnt < 0)
	{
		item->builtin_function = NULL;
		GC_symbol(item, (array *)&globals, loc);
	}
	new_free(&name);
}

void	delete_builtin_expando (const char *orig_name)
{
	Symbol *item;
	char *	name;
	int	cnt, loc;

	name = remove_brackets(orig_name, NULL);
	upper(name);
	item = (Symbol *)find_array_item((array *)&globals, name, &cnt, &loc);
	if (item && cnt < 0)
	{
		item->builtin_expando = NULL;
		GC_symbol(item, (array *)&globals, loc);
	}
	new_free(&name);
}

void	delete_builtin_variable (const char *orig_name)
{
	Symbol *item;
	char *	name;
	int	cnt, loc;

	name = remove_brackets(orig_name, NULL);
	upper(name);
	item = (Symbol *)find_array_item((array *)&globals, name, &cnt, &loc);
	if (item && cnt < 0)
	{
		item->builtin_variable = NULL;
		GC_symbol(item, (array *)&globals, loc);
	}
	new_free(&name);
}

/* * * */
static void	bucket_local_alias (Bucket *b, const char *name)
{
}

#define BUCKET_FUNCTION(x, y) \
void	bucket_ ## x (Bucket *b, const char *name)	\
{								\
	int	i;						\
	size_t	len;						\
	Symbol *item;						\
								\
	len = strlen(name);					\
	for (i = 0; i < globals.max; i++)			\
	{							\
		item = globals.list[i];				\
		if (!my_strnicmp(name, item->name, len))	\
		{						\
		    if (item-> y )				\
			add_to_bucket(b, item->name, item-> y);	\
		}						\
	}							\
}

BUCKET_FUNCTION(var_alias, user_variable)
BUCKET_FUNCTION(cmd_alias, user_command)
BUCKET_FUNCTION(builtin_commands, builtin_command)
BUCKET_FUNCTION(builtin_functions, builtin_function)
BUCKET_FUNCTION(builtin_expandos, builtin_expando)
BUCKET_FUNCTION(builtin_variables, builtin_variable)

/* * * */
static void	list_local_alias (const char *orig_name)
{
	size_t len = 0;
	int cnt;
	int DotLoc, LastDotLoc = 0;
	char *LastStructName = NULL;
	char *s;
	char *name = NULL;
	Symbol *item;

	say("Visible Local Assigns:");
	if (orig_name)
	{
		name = remove_brackets(orig_name, NULL);
		len = strlen(upper(name));
	}

	for (cnt = wind_index; cnt >= 0; cnt = call_stack[cnt].parent)
	{
	    int x;
	    if (!call_stack[cnt].alias.list)
		continue;
	    for (x = 0; x < call_stack[cnt].alias.max; x++)
	    {
		item = call_stack[cnt].alias.list[x];
		if (!name || !strncmp(item->name, name, len))
		{
		    if ((s = strchr(item->name + len, '.')))
		    {
			DotLoc = s - item->name;
			if (!LastStructName || (DotLoc != LastDotLoc) || 
				strncmp(item->name, LastStructName, DotLoc))
			{
			    put_it("\t%*.*s\t<Structure>", DotLoc, DotLoc, 
						item->name);
			    LastStructName = item->name;
			    LastDotLoc = DotLoc;
			}
		    }
		    else
			put_it("\t%s\t%s", item->name, item->user_variable);
		}
	    }
	}
}

/*
 * This function is strictly O(N).  Its possible to address this.
 */
static void	list_var_alias (const char *orig_name)
{
	size_t	len = 0;
	int	DotLoc,
		LastDotLoc = 0;
	const char	*LastStructName = NULL;
	int	cnt;
	char	*s;
	const char *script;
	char *name = NULL;

	say("Assigns:");

	if (orig_name)
	{
		name = remove_brackets(orig_name, NULL);
		len = strlen(upper(name));
	}

	for (cnt = 0; cnt < globals.max; cnt++)
	{
	    Symbol *item = globals.list[cnt];

	    if (item == NULL)
			continue;

	    if (!item->user_variable)
		continue;

	    if (!name || !strncmp(item->name, name, len))
	    {
		script = empty(item->user_variable_package) ? "*" :
				  item->user_variable_package;

		if ((s = strchr(item->name + len, '.')))
		{
			DotLoc = s - globals.list[cnt]->name;
			if (!LastStructName || (DotLoc != LastDotLoc) || 
				strncmp(item->name, LastStructName, DotLoc))
			{
				say("[%s]\t%*.*s\t<Structure>", script, DotLoc,
					 DotLoc, item->name);
				LastStructName = item->name;
				LastDotLoc = DotLoc;
			}
		}
		else
		{
			if (item->user_variable_stub)
				say("[%s]\t%s STUBBED TO %s", script, 
					item->name, item->user_variable);
			else
				say("[%s]\t%s\t%s", script,
					item->name, item->user_variable);
		}
	    }
	}
}

/*
 * This function is strictly O(N).  Its possible to address this.
 */
static void	list_cmd_alias (const char *orig_name)
{
	size_t	len = 0;
	int	DotLoc,
		LastDotLoc = 0;
	char	*LastStructName = NULL;
	int	cnt;
	char	*s;
	const char *script;
	char *name = NULL;

	say("Aliases:");

	if (orig_name)
	{
		name = remove_brackets(orig_name, NULL);
		len = strlen(upper(name));
	}

	for (cnt = 0; cnt < globals.max; cnt++)
	{
	    Symbol *item = globals.list[cnt];

	    if (item == NULL)
		continue;

	    if (!item->user_command)
		continue;

	    if (!name || !strncmp(item->name, name, len))
	    {
		script = empty(item->user_command_package) ? "*" :
				  item->user_command_package;

		if ((s = strchr(item->name + len, '.')))
		{
			DotLoc = s - item->name;
			if (!LastStructName || (DotLoc != LastDotLoc) || 
				strncmp(item->name, LastStructName, DotLoc))
			{
				say("[%s]\t%*.*s\t<Structure>", script, DotLoc,
					 DotLoc, item->name);
				LastStructName = item->name;
				LastDotLoc = DotLoc;
			}
		}
		else
		{
			if (item->user_command_stub)
				say("[%s]\t%s STUBBED TO %s", script, 
					item->name, item->user_command);
			else
			{
				char *arglist = print_arglist(item->arglist);
				if (arglist)
				{
				    say("[%s]\t%s (%s)\t%s", script, 
					item->name, arglist, 
					item->user_command);
				    new_free(&arglist);
				}
				else
				    say("[%s]\t%s\t%s", script, 
					item->name, item->user_command);
			}
		}
	    }
	}
	new_free(&name);
}

static void	list_builtin_commands (const char *orig_name)
{
}

static void	list_builtin_functions (const char *orig_name)
{
}

static void	list_builtin_expandos (const char *orig_name)
{
}

static void	list_builtin_variables (const char *orig_name)
{
}

/************************* UNLOADING SCRIPTS ************************/
static void	unload_cmd_alias (const char *package)
{
	int 	cnt;
	Symbol *item;

	for (cnt = 0; cnt < globals.max; cnt++)
	{
	    item = globals.list[cnt];

	    if (!item->user_command_package)
		continue;
	    else if (!strcmp(item->user_command_package, package)) {
		delete_cmd_alias(item->name, 0);
		cnt = -1;
		continue;
	    }
	}
}

static void	unload_var_alias (const char *package)
{
	int 	cnt;
	Symbol	*item;

	for (cnt = 0; cnt < globals.max; cnt++)
	{
	    item = globals.list[cnt];

	    if (!item->user_variable_package)
		continue;
	    else if (!strcmp(item->user_variable_package, package)) {
		delete_var_alias(item->name, 0);
		cnt = -1;
		continue;
	    }
	}
}

static void	unload_builtin_commands (const char *filename)
{
}

static	void	unload_builtin_functions (const char *filename)
{
}

static	void	unload_builtin_expandos (const char *filename)
{
}

static	void	unload_builtin_variables (const char *filename)
{
}

/* * */
/*
 * This function is strictly O(N).  This should probably be addressed.
 *
 * Updated as per get_subarray_elements.
 */
char **	glob_cmd_alias (const char *name, int *howmany, int maxret, int start, int rev)
{
	int	pos, max;
	int    	cnt;
	int     len;
	char    **matches = NULL;
	int     matches_size = 0;

	len = strlen(name);
	*howmany = 0;
	if (len) {
		find_array_item((array*)&globals, name, &max, &pos);
	} else {
		pos = 0;
		max = globals.max;
	}
	if (0 > max) max = -max;

	for (cnt = 0; cnt < max; cnt++, pos++)
	{
		if (!globals.list[pos]->user_command)
			continue;
		if (strchr(globals.list[pos]->name + len, '.'))
			continue;

		if (*howmany >= matches_size)
		{
			matches_size += 5;
			RESIZE(matches, char *, matches_size + 1);
		}
		matches[*howmany] = malloc_strdup(globals.list[pos]->name);
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
char **	glob_assign_alias (const char *name, int *howmany, int maxret, int start, int rev)
{
	int	pos, max;
	int    	cnt;
	int     len;
	char    **matches = NULL;
	int     matches_size = 0;

	len = strlen(name);
	*howmany = 0;
	if (len) {
		find_array_item((array*)&globals, name, &max, &pos);
	} else {
		pos = 0;
		max = globals.max;
	}
	if (0 > max) max = -max;

	for (cnt = 0; cnt < max; cnt++, pos++)
	{
		if (!globals.list[pos]->user_variable)
			continue;
		if (strchr(globals.list[pos]->name + len, '.'))
			continue;

		if (*howmany >= matches_size)
		{
			matches_size += 5;
			RESIZE(matches, char *, matches_size + 1);
		}
		matches[*howmany] = malloc_strdup(globals.list[pos]->name);
		*howmany += 1;
	}

	if (*howmany)
		matches[*howmany] = NULL;
	else
		new_free((char **)&matches);

	return matches;
}

#if 0
char **glob_builtin_commands (const char *name, int *howmany, int maxret, int start, int rev)
{
	return NULL;
}

char **glob_builtin_functions (const char *name, int *howmany, int maxret, int start, int rev)
{
	return NULL;
}

char **glob_builtin_expandos (const char *name, int *howmany, int maxret, int start, int rev)
{
	return NULL;
}

char **glob_builtin_variables (const char *name, int *howmany, int maxret, int start, int rev)
{
	return NULL;
}
#endif

#define PMATCH_SYMBOL(x, y) \
char **	pmatch_ ## x (const char *name, int *howmany, int maxret, int start, int rev) \
{ \
	int    	cnt, cnt1; \
	int     len; \
	char **matches = NULL; \
	int     matches_size = 5; \
\
	len = strlen(name); \
	*howmany = 0; \
	matches = RESIZE(matches, char *, matches_size); \
\
	for (cnt1 = 0; cnt1 < globals.max; cnt1++) \
	{ \
		cnt = rev ? globals.max - cnt1 - 1 : cnt1; \
		if (!globals.list[cnt]-> y ) \
			continue; \
\
		if (wild_match(name, globals.list[cnt]->name)) \
		{ \
			if (start--) \
				continue; \
			else \
				start++; \
			matches[*howmany] = globals.list[cnt]->name; \
			*howmany += 1; \
			if (*howmany == matches_size) \
			{ \
				matches_size += 5; \
				RESIZE(matches, char *, matches_size); \
			} \
			if (maxret && --maxret <= 0) \
				break; \
		} \
	} \
\
	if (*howmany) \
		matches[*howmany] = NULL; \
	else \
		new_free((char **)&matches); \
\
	return matches; \
} 

PMATCH_SYMBOL(assign_alias, user_variable)
PMATCH_SYMBOL(cmd_alias, user_command)
PMATCH_SYMBOL(builtin_variables, builtin_variable)
PMATCH_SYMBOL(builtin_commands, builtin_command)
PMATCH_SYMBOL(builtin_functions, builtin_function)
PMATCH_SYMBOL(builtin_expandos, builtin_expando)


/*****************************************************************************/
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
char **	get_subarray_elements (const char *orig_root, int *howmany, int type)
{
	SymbolSet *as;		/* XXXX */
	int pos, cnt, max;
	int cmp = 0;
	char **matches = NULL;
	int matches_size = 0;
	size_t end;
	char *last = NULL;
	char *root = NULL;

	as = &globals;
	root = malloc_strdup2(orig_root, ".");
	find_array_item((array*)as, root, &max, &pos);

	if (0 > max) 
		max = -max;
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
		matches[*howmany] = malloc_strndup(ARRAY_ITEM(as, pos)->name, cmp + end);
		last = matches[*howmany];
		*howmany += 1;
	}

	if (*howmany)
		matches[*howmany] = NULL;
	else
		new_free((char **)&matches);

	return matches;
}


/***************************************************************************/
static	void	destroy_cmd_aliases (SymbolSet *my_array)
{
	int cnt = 0;
	Symbol *item;

	for (cnt = 0; cnt < my_array->max; cnt++)
	{
		item = my_array->list[cnt];
		new_free((void **)&item->user_command);
		item->user_command_stub = 0;
		destroy_arglist(&item->arglist);
		if (!GC_symbol(item, (array *)my_array, cnt))
			cnt++;
	}
}

static	void	destroy_var_aliases (SymbolSet *my_array)
{
	int cnt = 0;
	Symbol *item;

	for (cnt = 0; cnt < my_array->max; cnt++)
	{
		item = my_array->list[cnt];
		new_free((void **)&item->user_variable);
		item->user_variable_stub = 0;
		if (!GC_symbol(item, (array *)my_array, cnt))
			cnt++;
	}

}

static	void	destroy_builtin_commands (SymbolSet *my_array)
{
	int cnt = 0;
	Symbol *item;

	for (cnt = 0; cnt < my_array->max; cnt++)
	{
		item = my_array->list[cnt];
		item->builtin_command = NULL;
		if (!GC_symbol(item, (array *)my_array, cnt))
			cnt++;
	}
}

static	void	destroy_builtin_functions (SymbolSet *my_array)
{
	int cnt = 0;
	Symbol *item;

	for (cnt = 0; cnt < my_array->max; cnt++)
	{
		item = my_array->list[cnt];
		item->builtin_function = NULL;
		if (!GC_symbol(item, (array *)my_array, cnt))
			cnt++;
	}
}

static	void	destroy_builtin_expandos (SymbolSet *my_array)
{
	int cnt = 0;
	Symbol *item;

	for (cnt = 0; cnt < my_array->max; cnt++)
	{
		item = my_array->list[cnt];
		item->builtin_expando = NULL;
		if (!GC_symbol(item, (array *)my_array, cnt))
			cnt++;
	}
}

static	void	destroy_builtin_variables (SymbolSet *my_array)
{
	int cnt = 0;
	Symbol *item;

	for (cnt = 0; cnt < my_array->max; cnt++)
	{
		item = my_array->list[cnt];
		item->builtin_variable = NULL;

		if (!GC_symbol(item, (array *)my_array, cnt))
			cnt++;
	}
}

/******************* RUNTIME STACK SUPPORT **********************************/

int	make_local_stack 	(const char *name)
{
	wind_index++;

#ifdef MAX_STACK_FRAMES
	if (wind_index >= MAX_STACK_FRAMES)
	{
		system_exception++;
		return 0;
	}
#endif

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
			call_stack[wind_index].alias.hash = HASH_INSENSITIVE;
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
	return 1;
}

static int	find_locked_stack_frame	(void)
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
		destroy_var_aliases(&call_stack[wind_index].alias);
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
		yell("Call stack");
		while (my_wind_index--)
			yell("[%3d] %s", my_wind_index, call_stack[my_wind_index].current);
		yell("End of call stack");
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

/************************* DIRECT VARIABLE EXPANSION ************************/
/*
 * get_variable: This returns the rvalue of the symbol "str".  
 *
 * An rvalue is what an expando is substituted with if it is used on the 
 * right hand side of an assignment. 
 *
 *    1) local variable
 *    2) global variable
 *    3) environment variable
 *    4) The empty string
 */
char 	*get_variable 	(const char *str)
{
	return get_variable_with_args(str, NULL);
}


static char *	get_variable_with_args (const char *str, const char *args)
{
	Symbol	*alias = NULL;
	char	*ret = NULL;
	char	*name = NULL;
	char	*freep = NULL;
	int	copy = 0;
	int	local = 0;

	freep = name = remove_brackets(str, args);

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
		copy = 1, ret = alias->user_variable;
	else if (local == 1)
		;
	else if ((alias = lookup_symbol(name)) != NULL)
	{
		if (alias->user_variable)
			copy = 1, ret = alias->user_variable;
		else if (alias->builtin_expando)
			copy = 0, ret = alias->builtin_expando();
		else if (alias->builtin_variable)
			copy = 0, ret = make_string_var_bydata(alias->builtin_variable->type, alias->builtin_variable->data);
	}
/*
	else if ((ret = make_string_var(str)))
		copy = 0;
*/
	else
		copy = 1, ret = getenv(str);

	if (x_debug & DEBUG_UNKNOWN && ret == NULL)
		yell("Variable lookup to non-existant assign [%s]", name);

	new_free(&freep);
	return (copy ? malloc_strdup(ret) : ret);
}

/* * */
char *	get_cmd_alias (const char *name, void **args, void (**func) (const char *, char *, const char *))
{
	Symbol *item;

	if ((item = lookup_symbol(name)))
	{
		if (args)
			*args = (void *)item->arglist;
		*func = item->builtin_command;
		return item->user_command;
	}

	*func = NULL;
	return NULL;
}

/* * */
char *	get_func_alias (const char *name, void **args, char * (**func) (char *))
{
	Symbol *item;

	if ((item = lookup_symbol(name)))
	{
		if (args)
			*args = (void *)item->arglist;
		*func = item->builtin_function;
		return item->user_command;
	}

	*func = NULL;
	return NULL;
}

char *	get_var_alias (const char *name, char *(**efunc)(void), IrcVariable **var)
{
	Symbol *item;

	if ((item = lookup_symbol(name)))
	{
		*efunc = item->builtin_expando;
		*var = item->builtin_variable;
		return item->user_variable;
	}

	*efunc = NULL;
	*var = NULL;
	return NULL;
}


/******************* expression and text parsers ***************************/
/* XXXX - bogus for now */
#include "expr2.c"
#include "expr.c"


/****************************** ALIASCTL ************************************/
/* Used by function_aliasctl */
/* MUST BE FIXED */
char 	*aliasctl 	(char *input)
{
	int list = -1;
	char *listc;
	enum { EXISTS, GET, SET, NMATCH, PMATCH, GETPACKAGE, SETPACKAGE, MAXRET } op;
static	int maxret = 0;
	int start = 0;
	int reverse = 0;

	GET_STR_ARG(listc, input);
	if (!my_strnicmp(listc, "AS", 2))
		list = VAR_ALIAS;
	else if (!my_strnicmp(listc, "AL", 2))
		list = COMMAND_ALIAS;
	else if (!my_strnicmp(listc, "LO", 2))
		list = VAR_ALIAS_LOCAL;
	else if (!my_strnicmp(listc, "MAXR", 4))
	{
		int old_maxret = maxret;
		if (input && *input)
			GET_INT_ARG(maxret, input);
		RETURN_INT(old_maxret);
	}
	else
		RETURN_EMPTY;

	GET_STR_ARG(listc, input);
	if ((start = my_atol(listc)))
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
		op = NMATCH;
	else if (!my_strnicmp(listc, "RM", 2))
		op = NMATCH, reverse = 1;
	else if (!my_strnicmp(listc, "P", 1))
		op = PMATCH;
	else if (!my_strnicmp(listc, "RP", 2))
		op = PMATCH, reverse = 1;
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
			Symbol *alias = NULL;
			SymbolSet *a_list;

			upper(listc);
			if (list == VAR_ALIAS_LOCAL)
				alias = find_local_alias(listc, &a_list);
			else 
				alias = lookup_symbol(listc);

			if (alias)
			{
				if (op == GET)
				{
				    if (list == COMMAND_ALIAS)
					RETURN_STR(alias->user_command);
                                    else
					RETURN_STR(alias->user_variable);
				}
				else if (op == EXISTS)
					RETURN_INT(1);
				else if (op == GETPACKAGE)
				{
				    if (list == VAR_ALIAS_LOCAL || list == VAR_ALIAS)
					RETURN_STR(alias->user_variable_package);
				    else
					RETURN_STR(alias->user_command_package);
				}
				else /* op == SETPACKAGE */
				{
				    if (list == VAR_ALIAS_LOCAL || list == VAR_ALIAS)
					malloc_strcpy(&alias->user_variable_package, input);
				    else
					malloc_strcpy(&alias->user_command_package, input);
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
		case (NMATCH) :
		{
			char **mlist = NULL;
			char *mylist = NULL;
			size_t	mylistclue = 0;
			int num = 0, ctr;

			if (!my_stricmp(listc, "*"))
				listc = LOCAL_COPY(empty_string);

			upper(listc);

			if (list == COMMAND_ALIAS)
				mlist = glob_cmd_alias(listc, &num, maxret, start, reverse);
			else if (list == VAR_ALIAS)
				mlist = glob_assign_alias(listc, &num, maxret, start, reverse);
			else
				RETURN_EMPTY;

			for (ctr = 0; ctr < num; ctr++)
			{
				malloc_strcat_word_c(&mylist, space, mlist[ctr], &mylistclue);
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
				mlist = pmatch_cmd_alias(listc, &num, maxret, start, reverse);
			else if (list == VAR_ALIAS)
				mlist = pmatch_assign_alias(listc, &num, maxret, start, reverse);
			else
				RETURN_EMPTY;

			for (ctr = 0; ctr < num; ctr++)
				malloc_strcat_word_c(&mylist, space, mlist[ctr], &mylistclue);
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
/* * * */
int	stack_push_var_alias (const char *name)
{
	Symbol *item, *sym;
	int	cnt = 0, loc = 0;

	item = (Symbol *)find_array_item((array *)&globals, name, &cnt, &loc);
	if (!item || cnt >= 0)
	{
	    item = make_new_Symbol(name);
	    add_to_array((array *)&globals, (array_item *)item);
	}

	sym = make_new_Symbol(name);
	malloc_strcpy(&sym->user_variable, item->user_variable);
	sym->user_variable_stub = item->user_variable_stub;
	malloc_strcpy(&sym->user_variable_package, item->user_variable_package);
	sym->saved = item->saved;
	sym->saved_hint = SAVED_VAR;
	item->saved = sym;
	return 0;
}

int	stack_pop_var_alias (const char *name)
{
	Symbol *item, *sym, *s, *ss;
	int	cnt = 0, loc = 0;

	item = (Symbol *)find_array_item((array *)&globals, name, &cnt, &loc);
	if (!item || cnt >= 0)
		return -1;

	for (sym = item; sym; sym = sym->saved)
		if (sym->saved && sym->saved->saved_hint == SAVED_VAR)
			break;
	if (!sym)
		return -1;

	s = sym->saved;
	ss = sym->saved->saved;
	malloc_strcpy(&item->user_variable, s->user_variable);
	item->user_variable_stub = s->user_variable_stub;
	malloc_strcpy(&item->user_variable_package, s->user_variable_package);
	new_free(&s->user_variable);
	new_free(&s->user_variable_package);

	if (GC_symbol(s, NULL, -1))
		sym->saved = ss;
	GC_symbol(item, (array *)&globals, loc);
	return 0;
}

int	stack_list_var_alias (const char *name)
{
	Symbol *item, *sym;
	int	counter = 0;

	if (!(item = lookup_symbol(name)))
		return -1;

	for (sym = item->saved; sym; sym = sym->saved)
	{
	    if (sym->saved_hint == SAVED_VAR)
	    {
		if (sym->user_variable == NULL)
		    say("\t%s\t<Placeholder>", sym->name);
		else if (sym->user_variable_stub)
		    say("\t%s STUBBED TO %s", sym->name, sym->user_variable);
		else
		    say("\t%s\t%s", sym->name, sym->user_variable);
		counter++;
	    }
	}

	if (counter)
		return 0;
	return -1;
}

/* * * */
int	stack_push_cmd_alias (char *name)
{
	Symbol *item, *sym;
	int	cnt = 0, loc = 0;

	item = (Symbol *)find_array_item((array *)&globals, name, &cnt, &loc);
	if (!item || cnt >= 0)
	{
	    item = make_new_Symbol(name);
	    add_to_array((array *)&globals, (array_item *)item);
	}

	sym = make_new_Symbol(name);
	malloc_strcpy(&sym->user_command, item->user_command);
	sym->user_command_stub = item->user_command_stub;
	malloc_strcpy(&sym->user_command_package, item->user_command_package);
	sym->arglist = clone_arglist(item->arglist);
	sym->saved = item->saved;
	sym->saved_hint = SAVED_CMD;
	item->saved = sym;
	return 0;
}

int	stack_pop_cmd_alias (const char *name)
{
	Symbol *item, *sym, *s, *ss;
	int	cnt = 0, loc = 0;

	item = (Symbol *)find_array_item((array *)&globals, name, &cnt, &loc);
	if (!item || cnt >= 0)
		return -1;

	for (sym = item; sym; sym = sym->saved)
	{
		if (sym->saved && sym->saved->saved_hint == SAVED_CMD)
			break;
	}
	if (!sym)
		return -1;

	s = sym->saved;
	ss = sym->saved->saved;
	malloc_strcpy(&item->user_command, s->user_command);
	item->user_command_stub = s->user_command_stub;
	malloc_strcpy(&item->user_command_package, s->user_command_package);
	new_free(&s->user_command);
	new_free(&s->user_command_package);

	if (GC_symbol(s, NULL, -1))
		sym->saved = ss;
	GC_symbol(item, (array *)&globals, loc);
	return 0;
}

int	stack_list_cmd_alias (const char *name)
{
	Symbol *item, *sym;
	int	counter = 0;

	if (!(item = lookup_symbol(name)))
		return -1;

	for (sym = item->saved; sym; sym = sym->saved)
	{
	    if (sym->saved_hint == SAVED_CMD)
	    {
		if (sym->user_command == NULL)
		    say("\t%s\t<Placeholder>", sym->name);
		else if (sym->user_command_stub)
		    say("\t%s STUBBED TO %s", sym->name, sym->user_command);
		else
		    say("\t%s\t%s", sym->name, sym->user_command);
		counter++;
	    }
	}

	if (counter)
		return 0;
	return -1;
}



/* * * */
#if 0
int	stack_push_builtin_cmd_alias (const char *name)
{
	Symbol *item, *sym;
	int	cnt = 0, loc = 0;

	item = (Symbol *)find_array_item((array *)&globals, name, &cnt, &loc);
	if (!item || cnt >= 0)
	{
	    item = make_new_Symbol(name);
	    add_to_array((array *)&globals, (array_item *)item);
	}

	sym = make_new_Symbol(name);
	sym->builtin_command = item->builtin_command;
	sym->saved = item->saved;
	sym->saved_hint = SAVED_BUILTIN_CMD;
	item->saved = sym;
	return 0;
}

int	stack_pop_builtin_cmd_alias (const char *name)
{
	Symbol *item, *sym, *s, *n;
	int	cnt = 0, loc = 0;

	item = (Symbol *)find_array_item((array *)&globals, name, &cnt, &loc);
	if (!item || cnt >= 0)
		return -1;

	for (sym = item; sym; sym = sym->saved)
	{
		if (sym->saved && sym->saved->saved_hint == SAVED_BUILTIN_CMD)
			break;
	}
	if (!sym)
		return -1;

	s = sym->saved;
	n = sym->saved->saved;
	item->builtin_command = s->builtin_command;
	s->builtin_command = NULL;

	if (GC_symbol(s, NULL, -1))
		sym->saved = n;
	GC_symbol(item, (array *)&globals, loc);
	return 0;
}

int	stack_list_builtin_cmd_alias (const char *name)
{
	Symbol *item, *sym;
	int	counter = 0;

	if (!(item = lookup_symbol(name)))
		return -1;

	for (sym = item->saved; sym; sym = sym->saved)
	{
	    if (sym->saved_hint == SAVED_BUILTIN_CMD)
	    {
		if (sym->builtin_command == NULL)
		    say("\t%s\t<Placeholder>", sym->name);
		else 
		    say("\t%s\t%p", sym->name, sym->builtin_command);
		counter++;
	    }
	}

	if (counter)
		return 0;
	return -1;
}
#endif

/* * * */
int	stack_push_builtin_func_alias (const char *name)
{
	Symbol *item, *sym;
	int	cnt = 0, loc = 0;

	item = (Symbol *)find_array_item((array *)&globals, name, &cnt, &loc);
	if (!item || cnt >= 0)
	{
	    item = make_new_Symbol(name);
	    add_to_array((array *)&globals, (array_item *)item);
	}

	sym = make_new_Symbol(name);
	sym->builtin_function = item->builtin_function;
	sym->saved = item->saved;
	sym->saved_hint = SAVED_BUILTIN_FUNCTION;
	item->saved = sym;
	return 0;
}

int	stack_pop_builtin_function_alias (const char *name)
{
	Symbol *item, *sym, *s, *n;
	int	cnt = 0, loc = 0;

	item = (Symbol *)find_array_item((array *)&globals, name, &cnt, &loc);
	if (!item || cnt >= 0)
		return -1;

	for (sym = item; sym; sym = sym->saved)
	{
		if (sym->saved && sym->saved->saved_hint == SAVED_BUILTIN_FUNCTION)
			break;
	}
	if (!sym)
		return -1;

	s = sym->saved;
	n = sym->saved->saved;
	item->builtin_function = s->builtin_function;
	s->builtin_function = NULL;

	if (GC_symbol(s, NULL, -1))
		sym->saved = n;
	GC_symbol(item, (array *)&globals, loc);
	return 0;
}

int	stack_list_builtin_function_alias (const char *name)
{
	Symbol *item, *sym;
	int	counter = 0;

	if (!(item = lookup_symbol(name)))
		return -1;

	for (sym = item->saved; sym; sym = sym->saved)
	{
	    if (sym->saved_hint == SAVED_BUILTIN_FUNCTION)
	    {
		if (sym->builtin_function == NULL)
		    say("\t%s\t<Placeholder>", sym->name);
		else 
		    say("\t%s\t%p", sym->name, sym->builtin_function);
		counter++;
	    }
	}

	if (counter)
		return 0;
	return -1;
}

/* * * */
int	stack_push_builtin_expando_alias (const char *name)
{
	Symbol *item, *sym;
	int	cnt = 0, loc = 0;

	item = (Symbol *)find_array_item((array *)&globals, name, &cnt, &loc);
	if (!item || cnt >= 0)
	{
	    item = make_new_Symbol(name);
	    add_to_array((array *)&globals, (array_item *)item);
	}

	sym = make_new_Symbol(name);
	sym->builtin_expando = item->builtin_expando;
	sym->saved = item->saved;
	sym->saved_hint = SAVED_BUILTIN_EXPANDO;
	item->saved = sym;
	return 0;
}

int	stack_pop_builtin_expando_alias (const char *name)
{
	Symbol *item, *sym, *s, *n;
	int	cnt = 0, loc = 0;

	item = (Symbol *)find_array_item((array *)&globals, name, &cnt, &loc);
	if (!item || cnt >= 0)
		return -1;

	for (sym = item; sym; sym = sym->saved)
	{
		if (sym->saved && sym->saved->saved_hint == SAVED_BUILTIN_EXPANDO)
			break;
	}
	if (!sym)
		return -1;

	s = sym->saved;
	n = sym->saved->saved;
	item->builtin_expando = s->builtin_expando;
	s->builtin_expando = NULL;

	if (GC_symbol(s, NULL, -1))
		sym->saved = n;
	GC_symbol(item, (array *)&globals, loc);
	return 0;
}

int	stack_list_builtin_expando_alias (const char *name)
{
	Symbol *item, *sym;
	int	counter = 0;

	if (!(item = lookup_symbol(name)))
		return -1;

	for (sym = item->saved; sym; sym = sym->saved)
	{
	    if (sym->saved_hint == SAVED_BUILTIN_EXPANDO)
	    {
		if (sym->builtin_expando == NULL)
		    say("\t%s\t<Placeholder>", sym->name);
		else 
		    say("\t%s\t%p", sym->name, sym->builtin_expando);
		counter++;
	    }
	}

	if (counter)
		return 0;
	return -1;
}


/* * * */
static int	stack_push_builtin_var_alias (const char *name)
{
	Symbol *item, *sym;
	int	cnt = 0, loc = 0;

	item = (Symbol *)find_array_item((array *)&globals, name, &cnt, &loc);
	if (!item || cnt >= 0)
	{
	    item = make_new_Symbol(name);
	    add_to_array((array *)&globals, (array_item *)item);
	}

	sym = make_new_Symbol(name);
	sym->builtin_variable = item->builtin_variable;
	sym->saved = item->saved;
	sym->saved_hint = SAVED_BUILTIN_VAR;
	item->saved = sym;
	return 0;
}

static int	stack_pop_builtin_var_alias (const char *name)
{
	Symbol *item, *sym, *s, *n;
	int	cnt = 0, loc = 0;

	item = (Symbol *)find_array_item((array *)&globals, name, &cnt, &loc);
	if (!item || cnt >= 0)
		return -1;

	for (sym = item; sym; sym = sym->saved)
	{
		if (sym->saved && sym->saved->saved_hint == SAVED_BUILTIN_VAR)
			break;
	}
	if (!sym)
		return -1;

	s = sym->saved;
	n = sym->saved->saved;
	item->builtin_variable = s->builtin_variable;
	s->builtin_variable = NULL;

	if (GC_symbol(s, NULL, -1))
		sym->saved = n;
	GC_symbol(item, (array *)&globals, loc);
	return 0;
}

static int	stack_list_builtin_variable_alias (const char *name)
{
	Symbol *item, *sym;
	int	counter = 0;

	if (!(item = lookup_symbol(name)))
		return -1;

	for (sym = item->saved; sym; sym = sym->saved)
	{
	    if (sym->saved_hint == SAVED_BUILTIN_VAR)
	    {
		if (sym->builtin_variable == NULL)
		    say("\t%s\t<Placeholder>", sym->name);
		else 
		{
		    char *s;
		    s = make_string_var_bydata(sym->builtin_variable->type, 
					(void *)sym->builtin_variable->data);
		    if (!s)
			s = malloc_strdup("<EMPTY>");

		    say("\t%s\t%s", sym->name, s);
		    new_free(&s);
		}
		counter++;
	    }
	}

	if (counter)
		return 0;
	return -1;
}

