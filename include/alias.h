/*
 * alias.h: header for alias.c 
 *
 * Written by Jeremy Nelson
 * Copyright 1997 EPIC Software Labs
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 */

#ifndef __alias_h__
#define __alias_h__

/*
 * XXXX - These need to go away
 */
#define COMMAND_ALIAS 		0
#define VAR_ALIAS 		1
#define VAR_ALIAS_LOCAL 	2

	extern	int	wind_index;
	extern	int	last_function_call_level;

/*
 * These are the user commands.  Dont call these directly.
 */
	BUILT_IN_COMMAND(aliascmd);
	BUILT_IN_COMMAND(assigncmd);
	BUILT_IN_COMMAND(localcmd);
	BUILT_IN_COMMAND(stubcmd);
	BUILT_IN_COMMAND(dumpcmd);
	BUILT_IN_COMMAND(unloadcmd);


/*
 * An "alias" is a symbol entry which can contain the following data types:
 *
 * 	A user macro command (/ALIAS items)
 *	A user macro expando (/ASSIGN and /LOCAL items)
 *	A built in command (ie, /MSG)
 *	A built in function (ie, $leftw())
 *	A built in expando (ie, $C or $N)
 *	A built in variable (/SET items)
 *
 * Not every alias will use every type.  Null values shall be used to indicate
 * an unused type in an alias.
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
 * Aliases broadly apply to one of three "namespace domains" -- 
 *
 *	Commands	/COMMAND
 *		Macro commands
 *		Built in commands
 *	Functions	$FUNCTION()
 *		Macro commands
 *		Built in functions
 *	Variables	$VARIABLE
 *		Macro expandos
 *		Built in expandos
 *		Built in variables
 */

	void 	add_var_alias      	(Char *name, Char *stuff, int noisy);
	void 	add_local_alias    	(Char *name, Char *stuff, int noisy);
#if 0	/* Internal now */
	void 	add_cmd_alias 	   	(void);
#endif
	void	add_builtin_cmd_alias	(Char *name, void (*func) (Char *, char *, Char *));
	void    add_builtin_func_alias  (Char *name, char *(*func) (char *));
	void    add_builtin_expando     (Char *name, char *(*func) (void));

	void 	add_var_stub_alias 	(Char *name, Char *stuff);
	void 	add_cmd_stub_alias 	(Char *name, Char *stuff);

	void	delete_builtin_command	(Char *);
	void	delete_builtin_function	(Char *);
	void	delete_builtin_expando	(Char *);

	char *	get_variable		(Char *name);
	char **	glob_cmd_alias		(Char *name, int *howmany, 
					 int maxret, int start, int rev);

	char *	get_cmd_alias   	(Char *name, void **args,
                                         void (**func) (const char *, char *, 
                                                        const char *));
	char *  get_func_alias		(const char *name, void **args, 
					 char * (**func) (char *));
	char *  get_var_alias		(const char *name, 
					 char *(**efunc)(void), 
					 void (**sfunc)(const void *));

	char **	get_subarray_elements 	(Char *root, int *howmany, int type);


/* These are in expr.c */
/*
 * This function is a general purpose interface to alias expansion.
 * The second argument is the text to be expanded.
 * The third argument are the command line expandoes $0, $1, etc.
 * The fourth argument is a flag whether $0, $1, etc are used
 * The fifth argument, if set, controls whether only the first "command"
 *   will be expanded.  If set, this argument will be set to the "rest"
 *   of the commands (after the first semicolon, or the null).  If NULL,
 *   then the entire text will be expanded.
 */
	char *	expand_alias 		(Char *, Char *, ssize_t *);

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
	void	destroy_call_stack	(void);
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


/*
 * Truly bogus. =)
 */
	char 	*canon_number (char *input);

	char	*aliasctl (char *);

	char *	after_expando (char *, int, int *);
#endif /* _ALIAS_H_ */
