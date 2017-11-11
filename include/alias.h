/*
 * alias.h: header for alias.c 
 * Copyright 1997 EPIC Software Labs
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 */

#ifndef __alias_h__
#define __alias_h__

#include "alist.h"
#include "ircaux.h"
#include "vars.h"


/* 
 * For historical reasons, commands and variables were saved in 
 * different linked lists, and these #define's were used to specify
 * what kind of thing you were looking up.  For external consumers,
 * this is the 'type' parameter, as in get_subarray_elements().
 */
#define COMMAND_ALIAS 		0
#define VAR_ALIAS 		1
#define VAR_ALIAS_LOCAL 	2

/* * * * * */
/*
 * Argument lists 
 *
 * Command aliases can be defined as taking an argument list.
 * The argument list is syntactic sugar for auto-assignments,
 * and has one advantage of modifying $* for you.
 *
 * For example:
 *    alias myalias (foo, bar) {
 *       echo $foo and $bar
 *    }
 * 
 * Is exactly the same as:
 *
 *    alias myalias {
 *      @ foo = [$0]
 *      @ bar = [$1-]
 *      echo $foo and $bar
 *    }
 */

/*
 * Each variable in an arglist can pull off a "word",
 * but the client has 4 types of words.  The definitions 
 * are somewhere else.
 */
enum ARG_TYPES {
	WORD,
	UWORD,
	DWORD,
	QWORD
};

/*
 * An arglist itself is a compiled list of variable
 * names and their metadata.
 *
 * For each x:
 *	vars[x]     -> the name of the variable
 *	defaults[x] -> the default value (if $* is too short)
 *	words[x]    -> How many words to pull off of $*
 *      types[x]    -> The kind of word this variable takes
 * Additionally:
 *	void_flag   -> If there's stuff left in $* when all
 *                     the variables are processed, discard it.
 *      dot_flag    -> If there's stuff left in $* when all
 *                     the variables are processed, put it
 *                     back into $*, instead of the last var
 */
struct ArgListT {
	char *	vars[32];
	char *	defaults[32];
	int	words[32];
	enum	ARG_TYPES types[32];
	int	void_flag;
	int	dot_flag;
};
typedef struct ArgListT ArgList;

/* Deserialize an ArgList object */
/* IE, convert a string to an internal object */
/* This malloc()s memory! */
	ArgList *parse_arglist 		(char *arglist);

/* Destroy an ArgList when you're done with it */
	void	destroy_arglist 	(ArgList **);

/* Serialize an ArgList object */
/* IE, convert an internal object to a string */
	char *	print_arglist 		(ArgList *);

/* Create a deep copy of an ArgList Object */
/* Now you must destroy _both_ of them! */
	ArgList *clone_arglist 		(ArgList *);


/* * * * * */
/*
 * Symbols
 *
 * When you use commands and aliases in epic, these things have names.
 * But how does the client translate a name to the underlying thing that 
 * does the task?
 * 
 * A Symbol is a data structure that contains all of the things that a
 * name can point to
 *
 *	1. A user variable (/ASSIGN or @)
 *	2. A builtin variable (/SETs)
 *	3. An inline expando function (ie, $Z)
 *	4. A user command (/ALIAS)
 *	5. A builtin command (/MSG)
 *	6. A builtin function ($leftw())
 *
 * Whenever the client comes across a name, it lookups up the symbol,
 * and then the type of thing it needs.
 *
 * Not every symbol uses every type.  Unused types get NULL pointers to 
 * indicate the lack of use.
 * 
 * The following operations are generally supported for each type:
 *
 *	Adding (setting) the value
 *	Stubbing the value to a file loaded on demand
 *	Getting the value
 *	Deleting the value
 *	Globbing names of types with values
 *	Subarray expansion of types with values
 *	Deleting all items
 *	Deleting items by package name
 *
 * Not every operation will be implemented for every type, but they will be
 * implemented if they are needed.
 *
 * Although there are 6 types, there are three logical namespace domains 
 * in which names are looked up for: (in priority)
 *
 *	Commands	/COMMAND
 *		User command
 *		Built in command
 *	Functions	$FUNCTION()
 *		User command
 *		Built in functions
 *	Variables	$VARIABLE
 *		User variable
 *		Inline expando
 *		Built in variables
 */

/*
 * This is the description of an alias entry
 * This is an ``array_item'' structure
 * (that is to say, it implements a struct that may be stored in an alist) 
 */
typedef struct  SymbolStru
{
        char    *name;                  /* name of alias */
        u_32int_t hash;                 /* Hash of the name */

        char *  user_variable;
        int     user_variable_stub;
        char *  user_variable_package;

        char *  user_command;
        int     user_command_stub;
        char *  user_command_package;
        ArgList *arglist;               /* List of arguments to alias */

        void    (*builtin_command) (const char *, char *, const char *);
        char *  (*builtin_function) (char *);
        char *  (*builtin_expando) (void);
        IrcVariable *   builtin_variable;

	struct SymbolStru *     saved;          /* For stacks */
        int     saved_hint;
}       Symbol;

/* These are the values for "saved_hint" */
#define SAVED_VAR                1
#define SAVED_CMD                2
#define SAVED_BUILTIN_CMD        4
#define SAVED_BUILTIN_FUNCTION   8
#define SAVED_BUILTIN_EXPANDO   16
#define SAVED_BUILTIN_VAR       32

/*
 * The symbol_types list allows $symbolctl() to inform the user
 * what kinds of things a symbol can store.  IE, any of these 
 * values must be accepted back by $symbolctl().
 */
extern const char *symbol_types[];
/*
const char *symbol_types[] = {
        "ASSIGN",               "ALIAS",                "BUILTIN_COMMAND",
        "BUILTIN_FUNCTION",     "BUILTIN_EXPANDO",      "BUILTIN_VARIABLE",
        NULL
};
*/

/*
 * The SymbolSet is an alist structure that contains Symbols.
 * (that is to say, it can be passed to add_to_array() et al)
 */
typedef struct  SymbolSetStru
{
        Symbol **       list;
        int             max;
        int             max_alloc;
        alist_func      func;
        hash_type       hash;
}       SymbolSet;

/* This is it, right here; the global symbol table. */
extern	SymbolSet globals;
/* SymbolSet globals =      { NULL, 0, 0, strncmp, HASH_INSENSITIVE }; */

/* Use this to look up a global symbol. */
	Symbol *lookup_symbol	   	(const char *name);

/* Use this to look up a local variable (see more below) */
	Symbol *find_local_alias   	(const char *name, SymbolSet **list);

/* At exit() time, this is called to help us detect memory leaks */
	void	flush_all_symbols	(void);


/* * * * */
/*
 * Local variables and The Stack
 *
 * So local variables are stored the same way as global variables -- they're 
 * stored in a SymbolSet -- but instead of being put in the single global
 * namespace, we create a new SymbolSet for every atomic scope.
 */ 

/*
 * This is the ``stack frame''.  Each frame has a ``name'' which is
 * the name of the alias or on of the frame, or is NULL if the frame
 * is not an ``enclosing'' frame.  Each frame also has a ``current command''
 * that is being executed, which is used to help us when the client crashes.
 * Each stack also contains a Symbol table (for local variables)
 */
typedef struct RuntimeStackStru
{
        const char *name;       /* Name of the stack */
        char    *current;       /* Current cmd being executed */
        SymbolSet alias;        /* Local variables */
        int     locked;         /* Are we locked in a wait? */
        int     parent;         /* Our parent stack frame */
}       RuntimeStack;

/* This is it, right here; the stack frames where all local vars are stored */
extern	RuntimeStack *	call_stack;

/* This is the _physical size_ of call_stack -- ie, what to avoid to do buffer overrun */
extern	int		max_wind;

/* 
 * This is the _local size_ of call_stack -- ie, how many frames are in use.
 * You can peek at call_stack[wind_index] through call_stack[0]
 */
extern	int		wind_index;

/*
 * Historically, the return value of an ircII alias was whatever you stashed 
 * in the FUNCTION_RETURN variable.  This was a magic variable (because ircII
 * does not have local variables) that ircII keeps in an array.  EPIC now 
 * treats FUNCTION_RETURN as a plain old local variable; but in order to make
 * sure old codes does the right thing, we need to keep track of where the 
 * last function call was, so @ FUNCTION_RETURN = x  sets the correct one. :)
 *
 *  E.G.,
 *   s = find_local_alias("FUNCTION_RETURN", call_stack[last_function_call_level])
 */
extern	int		last_function_call_level;


/*
 * These are commands for the user to use.
 * You probably shouldn't use them.
 */
	BUILT_IN_COMMAND(aliascmd);
	BUILT_IN_COMMAND(assigncmd);
	BUILT_IN_COMMAND(localcmd);
	BUILT_IN_COMMAND(stubcmd);
	BUILT_IN_COMMAND(dumpcmd);
	BUILT_IN_COMMAND(unloadcmd);


/*
 * These intermediate level functions are more your speed.
 *
 * In these functions:
 *	'name' 	  is the name of the symbol (of course)
 *	'stuff'   is the payload of the symbol 
 *	'noisy'   is 1 if you want the user to be told of the change, and 0 if you don't
 *		  (this simulates the difference between /ASSIGN and /^ASSSIGN)
 *      'arglist' is an ArgList object that you deserialized.  It can be NULL.
 *      'stub'    means you want to defer the intialization of this thing until later.
 *                The first time someone looks up this symbol, the client will do /load 'stuff'
 *                This used to be cool back in the day when /load's were slow and 
 *                memory was precious and you didn't want to load stuff you didn't use.
 *      'Char'    is is just a (const char *)
 *      'unload'  means to delete everything that is tagged with a filename.
 *      'destroy' means irreversibly destroy.  Don't call those unless you own a SymbolSet.
 *      'glob'    means wildcard pattern matching (ie,  "AL*S" -> "ALIAS"
 *      'pmatch'  means the same as 'glob' (pattern matching)
 *      "subarray" means dot-extension:  If there is $one.two.three and $one.two.four,
 *                then "one.two" would return "three" and "four"
 *      "args"    means the value of "args" (aka $*) that you have at hand.
 *                  Every command and alias and builtin function has an "args"!
 *                  You need this for things like $var[$0]  to work.
 *      "buckets" are tuples (a container of named stuff). Check out ircaux.c for more info
 *		    Buckets are used for disambiguation, and return actual things
 *                  rather than the names of things
 *                  (ie,  "AL" -> returns the objects for "ALIAS" and "ALLOCDUMP")
 */

	void    add_var_alias      		(Char *name, Char *stuff, int noisy);
	void    add_local_alias    		(Char *name, Char *stuff, int noisy);
	void    add_cmd_alias      		(Char *name, ArgList *arglist, Char *stuff);
	void    add_var_stub_alias 		(Char *name, Char *stuff);
	void    add_cmd_stub_alias 		(Char *name, Char *stuff);
	void    add_builtin_cmd_alias  		(Char *, void (*)(Char *, char *, Char *));
	void    add_builtin_func_alias 		(Char *, char *(*)(char *));
	void    add_builtin_expando    		(Char *, char *(*)(void));
	void    add_builtin_variable_alias	(Char *name, IrcVariable *var);

	void    delete_var_alias   		(Char *name, int noisy);
	void    delete_cmd_alias   		(Char *name, int noisy);
/*      void    delete_local_alias 		(Char *name);                */

	void    unload_cmd_alias   		(Char *fn);
	void    unload_var_alias   		(Char *fn);
	void    list_cmd_alias     		(Char *name);
	void    list_var_alias     		(Char *name);
	void    list_local_alias   		(Char *name);
	void    destroy_cmd_aliases    		(SymbolSet *);
	void    destroy_var_aliases    		(SymbolSet *);
	void    destroy_builtin_commands    	(SymbolSet *);
	void    destroy_builtin_functions    	(SymbolSet *);
	void    destroy_builtin_variables    	(SymbolSet *);
	void    destroy_builtin_expandos    	(SymbolSet *);

	char * 	get_variable       		(Char *name);
	char **	glob_cmd_alias          	(Char *name, int *howmany, int maxret, int start, int rev);
	char **	glob_assign_alias       	(Char *name, int *howmany, int maxret, int start, int rev);
	Char * 	get_cmd_alias     		(Char *name, void **args, void (**func) (const char *, char *, const char *));
	char **	get_subarray_elements   	(Char *root, int *howmany, int type);
	char * 	get_variable_with_args 		(Char *str, Char *args);
	Char * 	get_func_alias			(Char *name, void **args, char * (**func) (char *));
	Char * 	get_var_alias			(Char *name, char *(**efunc)(void), IrcVariable **var);

	char ** pmatch_cmd_alias        	(Char *name, int *howmany, int maxret, int start, int rev);
	char ** pmatch_assign_alias     	(Char *name, int *howmany, int maxret, int start, int rev);
	char ** pmatch_builtin_commands        	(Char *name, int *howmany, int maxret, int start, int rev);
	char ** pmatch_builtin_functions       	(Char *name, int *howmany, int maxret, int start, int rev);
	char ** pmatch_builtin_expandos       	(Char *name, int *howmany, int maxret, int start, int rev);
	char ** pmatch_builtin_variables      	(Char *name, int *howmany, int maxret, int start, int rev);

	void	bucket_var_alias 		(Bucket *, const char *);
	void	bucket_cmd_alias 		(Bucket *, const char *);
	void	bucket_builtin_commands 	(Bucket *, const char *);
	void	bucket_builtin_functions 	(Bucket *, const char *);
	void	bucket_builtin_expandos 	(Bucket *, const char *); 
	void	bucket_builtin_variables 	(Bucket *, const char *);

/* * * * I stopped here * * * */
/* These are in expr.c */
	ssize_t	next_statement 			(Char *string);

/*
 * This function is a general purpose interface to alias expansion.
 * The first argument is the text to be expanded.
 * The third argument are the command line expandoes $0, $1, etc.
 */
	char *	expand_alias 		(Char *, Char *);

/*
 * This is the interface to the "expression parser"
 * The first argument is the expression to be parsed
 * The second argument is the command line expandoes ($0, $1, etc)
 * The third argument will be set if the command line expandoes are used.
 */
	char *	parse_inline 		(char *, Char *);

/*
 * This function is used to save all the current aliases to a global
 * file.  This is used by /SAVE and /ABORT.
 */
	void	save_assigns		(FILE *, int);
	void	save_aliases 		(FILE *, int);


/* BOGUS */

/*
 * This function is used to prepare the $* string before calling a user
 * alias or function.  You should pass in the last argument from get_cmd_alias
 * to this function, and also the $* value.  The second value may very well
 * be modified.
 */
	void	prepare_alias_call	(void *, char **);
	void	destroy_alias_call	(void *);

/*
 * This is in functions.c
 * This is only used by next_unit and expand_alias to call built in functions.
 * Noone should call this function directly.
 */
	char *	call_function		(char *, Char *);
	void	init_functions		(void);
	void	init_expandos		(void);

/*
 * These are the two primitives for runtime stacks.
 */
	int	make_local_stack 	(Char *);
	void	destroy_local_stack 	(void);
	void	set_current_command 	(char *);
	void	bless_local_stack 	(void);
	void	unset_current_command 	(void);
	void	lock_stack_frame	(void);
	void	unlock_stack_frame	(void);
	void	dump_call_stack		(void);
	void	panic_dump_call_stack 	(void);

/*
 * This is the alias interface to the /STACK command.
 */
	int	stack_push_var_alias (const char *name);
	int     stack_pop_var_alias (const char *name);
	int     stack_list_var_alias (const char *name);
	int	stack_push_cmd_alias (char *name);
	int     stack_pop_cmd_alias (const char *name);
	int     stack_list_cmd_alias (const char *name);
	int	stack_push_builtin_cmd_alias (const char *name);
	int     stack_pop_builtin_cmd_alias (const char *name);
	int     stack_list_builtin_cmd_alias (const char *name);
	int	stack_push_builtin_func_alias (const char *name);
	int     stack_pop_builtin_function_alias (const char *name);
	int     stack_list_builtin_function_alias (const char *name);
	int	stack_push_builtin_expando_alias (const char *name);
	int     stack_pop_builtin_expando_alias (const char *name);
	int     stack_list_builtin_expando_alias (const char *name);
	int	stack_push_builtin_var_alias (const char *name);
	int     stack_pop_builtin_var_alias (const char *name);
	int     stack_list_builtin_var_alias (const char *name);


/*
 * Truly bogus. =)
 */
	char 	*canon_number (char *input);

	char	*aliasctl (char *);
	char	*symbolctl (char *);

	char *	after_expando (char *, int, int *);



#endif /* _ALIAS_H_ */
