/* 
 * functions.h -- header file for functions.c
 *
 * Copyright 1995, 2002 EPIC Software Labs
 * See the COPYRIGHT file for copyright information
 */

#ifndef __functions_h__
#define __functions_h__

/* 
 * These are defined to make the construction of the built-in functions
 * easier and less prone to bugs and unexpected behaviors.  As long as
 * you consistently use these macros to do the dirty work for you, you
 * will never have to do bounds checking as the macros do that for you. >;-) 
 *
 * Yes, i realize it makes the code slightly less efficient, but i feel that 
 * the cost is minimal compared to how much time i have spent over the last 
 * year debugging these functions and the fact i wont have to again. ;-)
 */
#define EMPTY empty_string
#define EMPTY_STRING malloc_strdup(EMPTY)
#define RETURN_EMPTY return EMPTY_STRING
#define RETURN_IF_EMPTY(x) if (empty( (x) )) RETURN_EMPTY
#define GET_INT_ARG(x, y) {		\
	const char *xxx; 		\
	RETURN_IF_EMPTY((y)); 		\
	xxx = next_func_arg((y), &(y)); \
	if (!xxx) 			\
		xxx = empty_string; 	\
	x = strtoimax(xxx, NULL, 0);	\
}
#define GET_FLOAT_ARG(x, y) {		\
	const char *xxx; 		\
	RETURN_IF_EMPTY((y)); 		\
	xxx = next_func_arg((y), &(y)); \
	if (!xxx) 			\
		xxx = empty_string; 	\
	x = atof(xxx);			\
}
#define GET_FUNC_ARG(x, y) {		\
	RETURN_IF_EMPTY((y)); 		\
	x = next_func_arg((y), &(y));	\
}
#define GET_DWORD_ARG(x, y) {		\
	RETURN_IF_EMPTY((y)); 		\
	x = new_next_arg((y), &(y));	\
}
#define GET_UWORD_ARG(x, y) {		\
	RETURN_IF_EMPTY((y)); 		\
	x = next_arg((y), &(y));	\
}
#define RETURN_MSTR(x) return ((x) ? (x) : EMPTY_STRING)
#define RETURN_STR(x) return malloc_strdup((x) ? (x) : EMPTY)
#define RETURN_FSTR(x) return malloc_strdup( x )	/* Only fixed char arrays! */
#define RETURN_INT(x) return malloc_strdup(ltoa((x)))
#define RETURN_FLOAT(x) return malloc_sprintf(NULL, "%.50g", (double) (x))
#define RETURN_FLOAT2(x) return malloc_sprintf(NULL, "%.2f", (double) (x))

/*
 * XXXX REALLY REALLY REALLY REALLY REALLY REALLY REALLY IMPORTANT! XXXX
 *
 * Don't ever Ever EVER pass a function call to the RETURN_* macros.
 * They _WILL_ evaluate the term twice, and for some function calls, 
 * that can result in a memory leak, or worse.
 */
#ifdef need_static_functions
#define BUILT_IN_FUNCTION(x, y) static char * x (char * y)
#else
#define BUILT_IN_FUNCTION(x, y) char * x (char * y)
#endif

struct kwargs {
        const char *    kwarg;
        int             type;
        void *          data;
	int		required;
};
#define KWARG_TYPE_SENTINAL 0
#define KWARG_TYPE_STRING 1
#define KWARG_TYPE_INTEGER 2
#define KWARG_TYPE_NUMBER 3
#define KWARG_TYPE_BOOL 4

int     parse_kwargs (struct kwargs *kwargs, const char *input);
 
/* 
 * Examples for using the above:
 *       char *name1 = NULL, *flag = NULL;
 *       struct kwargs kwargs[] = {
 *               { "name1", KWARG_TYPE_STRING, &name1, 1 },
 *               { "flag", KWARG_TYPE_STRING, &flag, 1 },
 *               { NULL, KWARG_TYPE_SENTINAL, NULL, 0 }
 *       };
 *
 *       if (input && *input == '{')
 *            parse_kwargs(kwargs, input);
 *
 * and
 *      @func({"name1": "value1", "flag": "booya"})
 */

#endif
