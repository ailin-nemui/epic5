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
#define EMPTY_STRING m_strdup(EMPTY)
#define RETURN_EMPTY return EMPTY_STRING
#define RETURN_IF_EMPTY(x) if (empty( (x) )) RETURN_EMPTY
#define GET_INT_ARG(x, y) {RETURN_IF_EMPTY((y)); x = my_atol(safe_new_next_arg((y), &(y)));}
#define GET_FLOAT_ARG(x, y) {RETURN_IF_EMPTY((y)); x = atof(safe_new_next_arg((y), &(y)));}
#define GET_STR_ARG(x, y) {RETURN_IF_EMPTY((y)); x = new_next_arg((y), &(y));RETURN_IF_EMPTY((x));}
#define RETURN_MSTR(x) return ((x) ? (x) : EMPTY_STRING)
#define RETURN_STR(x) return m_strdup((x) ? (x) : EMPTY)
#define RETURN_INT(x) return m_strdup(ltoa((x)))
#define RETURN_FLOAT(x) return m_sprintf("%.50g", (double) (x))
#define RETURN_FLOAT2(x) return m_sprintf("%.2f", (double) (x))

/*
 * XXXX REALLY REALLY REALLY REALLY REALLY REALLY REALLY IMPORTANT! XXXX
 *
 * Don't ever Ever EVER pass a function call to the RETURN_* macros.
 * They _WILL_ evaluate the term twice, and for some function calls, 
 * that can result in a memory leak, or worse.
 */

#define BUILT_IN_FUNCTION(x, y) static char * x (char * y)

#endif
