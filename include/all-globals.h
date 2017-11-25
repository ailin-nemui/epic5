/*
 * This file is to be included at the end of #include "all.h"
 * All global variables whatsoever are to be declared here.
 * Eventually, we will move all global variables into a struct.
 * We will serialize and deserialize thie struct to save client state
 */

/* alias.h */

	/* This is it, right here; the global symbol table. */
	extern  SymbolSet globals;

	/* This is it, right here; the stack frames where all local vars are stored */
	extern  RuntimeStack *  call_stack;

	/* This is the _physical size_ of call_stack -- ie, what to avoid to do buffer overrun */
	extern  int             max_wind;

	/*
	 * This is the _local size_ of call_stack -- ie, how many frames are in use.
	 * You can peek at call_stack[wind_index] through call_stack[0]
	 */
	extern  int             wind_index;

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
	extern  int             last_function_call_level;


