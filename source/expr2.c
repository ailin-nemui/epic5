/* $EPIC: expr2.c,v 1.21 2004/03/19 06:05:13 jnelson Exp $ */
/*
 * Zsh: math.c,v 3.1.2.1 1997/06/01 06:13:15 hzoli Exp 
 * math.c - mathematical expression evaluation
 * This file is based on 'math.c', which is part of zsh, the Z shell.
 *
 * Copyright (c) 1992-1997 Paul Falstad, All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright notice,
 *    this list of conditions and the two following paragraphs.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimers in the
 *    documentation and/or other materials provided with the distribution
 * 3. The names of the author(s) may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * In no event shall Paul Falstad or the Zsh Development Group be liable
 * to any party for direct, indirect, special, incidental, or consequential
 * damages arising out of the use of this software and its documentation,
 * even if Paul Falstad and the Zsh Development Group have been advised of
 * the possibility of such damage.
 *
 * Paul Falstad and the Zsh Development Group specifically disclaim any
 * warranties, including, but not limited to, the implied warranties of
 * merchantability and fitness for a particular purpose.  The software
 * provided hereunder is on an "as is" basis, and Paul Falstad and the
 * Zsh Development Group have no obligation to provide maintenance,
 * support, updates, enhancements, or modifications.
 *
 */
/*
 * Substantial modifications by Jeremy Nelson which are
 * Coypright 1998, 2003 EPIC Software Labs, All rights reserved.
 *
 * You may distribute this file under the same terms as the above, by 
 * including the parties "Jeremy Nelson" and "EPIC Software Labs" to the 
 * limitations of liability and the express disclaimer of all warranties.
 * This software is provided "AS IS".
 */
#include <math.h>

#define STACKSZ 	80
#define TOKENCOUNT	80
#define MAGIC_TOKEN	-14

/*
 * THIS IS THE "NEW NEW" MATH PARSER -- or shall I say, this is the
 * new math parser, second edition.
 */
/*
 * Question: Why is this math parser so much more hideous than the old one?
 *
 * Answer: The implementation looks very very complex, and to a certain 
 * extent it is.  However, do not be frightened by the malicious abuse of 
 * macros and the over-use of inline functions.  The design of this math 
 * parser is not nearly as complex as its implementation.  Maybe that is 
 * due to my not being the world's best C programmer, and I probably wrote
 * this code too quickly.  One of the most important design considerations 
 * was that it should be possible to prove correctness, while maintaining
 * the possibility for general optimization.  Those are conflicting goals
 * and this implementation tries to balance the two.
 */
/*
 * Question: Why should I use this math parser instead of the old one?
 * 
 * Answer: Like the old math parser, this new math parser evaluates infix
 * notation expressions and returns the resulting value of the entire
 * expression.  Unlike the old math parser, this new math parser correctly
 * obeys both operator precedence as well as associativity.  It still can
 * do short-circuiting of &&, ||, and ?: operators.  All operands start
 * off their life as text strings, but can be converted to and from other
 * data types (such as floating point, integer, and boolean).  Type conversion
 * is automatic and efficient, and the results of every conversion is cached,
 * so the conversion is only done once, and only when it is actually needed.
 * This new math parser has a slew of new operators, as well as correct
 * implementations of some of the old operators.  This new math parser also
 * handles both integer and floating point operations gracefully.
 */
/*
 * Question:  Why is everything stored in a struct?
 * 
 * Answer: All the information for each expression is stored in a struct.
 * This is done so that there are no global variables in use (they're all 
 * collected making them easier to handle), and makes re-entrancy possible
 * since you don't have to worry about whether or not all of the state
 * information is accounted for (it's all on the stack).
 */
/*
 * Question: Why do you keep a 'symbol table' for each expression?
 *
 * By keeping a record of every direct symbol or derived symbol during
 * the entire lifetime of the expression, we can be assured that we have
 * a clean way to clean up after an expression (no memory leaks), and
 * permit a reference to any operator to persist for the entire lifetime
 * of the expression, without having to worry about who might be holding
 * a reference to an operand (tokens never change over their lifetime).
 * By refering to each token through an integer, rather than a pointer, 
 * we can also prevent stale pointers, which can cause crashes.
 * 
 * This also solves several more problems.  There is never any concern over
 * whether or not a certain string is malloced(), or just who is responsible
 * for free()ing it.  If you need a value to stay around as a temporary value
 * you can always tokenize() it and get a handle which you then use further.
 * The value will not go away until the entire expression has been parsed.
 */
/*
 * Question:  Why don't you support pre-compiled expressions?
 *
 * Because this implementation does not create a compiled spanning tree
 * out of the expression before executing it, but rather tokenizes the
 * operands and reduces the operations based on prior operations.  Perhaps
 * this might change in the future, but don't count on it.  The lexer uses
 * the results of prior operations to support such things as short circuits
 * and changing that would be a big pain.
 */

typedef 	int		TOKEN;
typedef 	int		BooL;

/*
 * These flags tell us whether or not a given token has a useful value
 * of the given type.  This is used to tell us when we need to do automatic
 * conversion, or whether the conversion was done before and we can just
 * grab the cached value.  These flags are used by the "used" field, and
 * are cumulative.
 */
#define USED_NONE		0
#define USED_LVAL		1 << 0
#define	USED_RAW		1 << 1
#define	USED_EXPANDED		1 << 2
#define USED_INTEGER		1 << 3
#define USED_FLOAT		1 << 4
#define USED_BOOLEAN		1 << 5

/*
 * Theoretically, all these macros are independant because the return value of
 * INT*FUNC is typecasted back to INTTYPE.  One caveat is that INT2STR
 * must return a malloc'd string.
 */
#ifdef HAVE_LONG_LONG
typedef long long INTTYPE;
#define FORMAT "%lld"
#define STR2INT(x) ((INTTYPE)atoll(x))
#define INT2STR(x) (malloc_sprintf(NULL, FORMAT , (INTTYPE)(x)))
#else
typedef long INTTYPE;
#define FORMAT "%ld"
#define STR2INT(x) ((INTTYPE)atol(x))
#define INT2STR(x) (malloc_sprintf(NULL, FORMAT , (INTTYPE)(x)))
#endif

/*
 * This is a symbol table entry.
 */
typedef struct TOKEN_type 
{
	int	used;			/* Which fields contain useful info? */
	char *	lval;			/* Cached unexpanded variable name */
	char *	raw_value;		/* Cached unexpected string */
	char *	expanded_value;		/* Cached full expanded string */
	INTTYPE	integer_value;		/* Cached integer value */
	double	float_value;		/* Cached floating point value */
	short	boolean_value;		/* Cached boolean value */
} SYMBOL;

#define __inline 

/*
 * This is an expression context
 */
typedef struct
{
	/* POSITION AND STATE INFORMATION */
	/*
	 * This is the current position in the lexing.
	 */
	char	*ptr;

	/*
	 * When set, the expression is lexed, but nothing that may have a side
	 * effect (function calls, assignments, etc) are actually performed.
	 * Dummy values are instead substituted.
	 */
	int	noeval;

	/* 
	 * When set, this means the next token may either be a prefix operator
	 * or an operand.  When clear, it means the next operator must be a
	 * non-prefix operator.
	 */
	int	operand;


	/* TOKEN TABLE */
	/*
	 * Each registered 'token' is given a TOKEN id.  The idea is
	 * that we want TOKEN to be an opaque type to be used to refer
	 * to a token in a generic way, but in practice its just an integer
	 * offset into a char ** table.  We register all tokens sequentially,
	 * so this just gets incremented when we want to register a new token.
	 */
	TOKEN	token;

	/*
	 * This is the list of operand (string) tokens we have extracted
	 * so far from the expression.  Offsets into this array are stored
	 * into the parsing stack.
	 */
	SYMBOL	tokens[TOKENCOUNT + 1];


	/* OPERAND STACK */
	/*
	 * This is the operand shift stack.  These are the operands that
	 * are currently awaiting reduction.  Note that rather than keeping
	 * track of the lvals and rvals here, we simply keep track of offsets
	 * to the 'tokens' table that actually stores all the relevant data.
	 * Then we can just call the token-class functions to get that data.
	 * This is more efficient because it allows us to recycle tokens
	 * more reasonably without wasteful malloc-copies.
	 */
	TOKEN 	stack[STACKSZ + 1];

	/* Current index to the operand stack */
	int	sp;

	/* This is the last token that was lexed. */
	TOKEN	mtok;

	/* This is set when an error happens */
	int	errflag;

	TOKEN	last_token;

	const char	*args;
} expr_info;

/* 
 * Useful macro to get at a specific token.
 * 'c' is the expression context, 'v' is the token handle.
 */
#define TOK(c, v) 	c->tokens[v]

/* Forward function references */
__inline static	TOKEN	tokenize_raw (expr_info *c, const char *t);
	static	char *	after_expando_special (expr_info *c);
	static	char *	alias_special_char (char **buffer, char *ptr, 
					const char *args, char *quote_em);


/******************** EXPRESSION CONSTRUCTOR AND DESTRUCTOR ****************/
/*
 * Clean the expression context pointed to by 'c'.
 * This function must be called before you call mathparse().
 */
static void	setup_expr_info (expr_info *c)
{
	int	i;

	c->ptr = NULL;
	c->noeval = 0;
	c->operand = 1;
	c->token = 0;
	for (i = 0; i <= TOKENCOUNT; i++)
	{
		TOK(c, i).used = USED_NONE;
		TOK(c, i).lval = NULL;
		TOK(c, i).raw_value = NULL;
		TOK(c, i).expanded_value = NULL;
		TOK(c, i).integer_value = 0;
		TOK(c, i).float_value = 0;
		TOK(c, i).boolean_value = 0;
	}
	for (i = 0; i <= STACKSZ; i++)
		c->stack[i] = 0;
	c->sp = -1;
	c->mtok = 0;
	c->errflag = 0;
	c->last_token = 0;
	tokenize_raw(c, empty_string);	/* Always token 0 */
}

/*
 * Clean up the expression context pointed to by 'c'.
 * This function must be called after you call mathparse().
 */
static void 	destroy_expr_info (expr_info *c)
{
	int	i;

	c->ptr = NULL;
	c->noeval = -1;
	c->operand = -1;
	for (i = 0; i < c->token; i++)
	{
		TOK(c, i).used = USED_NONE;
		new_free(&TOK(c, i).lval);
		new_free(&TOK(c, i).raw_value);
		new_free(&TOK(c, i).expanded_value);
	}
	c->token = -1;
	for (i = 0; i <= STACKSZ; i++)
		c->stack[i] = -1;
	c->sp = -1;
	c->mtok = -1;
	c->errflag = -1;
	c->last_token = -1;
}


/**************** TOKEN, PRECEDENCE, and ASSOCITIVITY TABLES ****************/
/* 
 * LR = left-to-right associativity
 * RL = right-to-left associativity
 * BOOL = short-circuiting boolean
 */
#define LR 		0
#define RL 		1
#define BOOL 		2

/*
 * These are all the token-types.  Each of the operators is represented,
 * as is the generic operand type
 */

enum LEX {
	M_INPAR,
	NOT, 		COMP, 		PREMINUS,	PREPLUS,
			UPLUS,		UMINUS,		STRLEN,
			WORDC,		DEREF,
	POWER,
	MUL,		DIV,		MOD,
	PLUS,		MINUS,		STRCAT,
	SHLEFT,		SHRIGHT,
	LES,		LEQ,		GRE,		GEQ,
	MATCH,		NOMATCH,
	DEQ,		NEQ,
	AND,
	XOR,
	OR,
	DAND,
	DXOR,
	DOR,
	QUEST,		COLON,
	EQ,		PLUSEQ,		MINUSEQ,	MULEQ,		DIVEQ,
			MODEQ,		ANDEQ,		XOREQ,		OREQ,
			SHLEFTEQ,	SHRIGHTEQ,	DANDEQ,		DOREQ,
			DXOREQ,		POWEREQ,	STRCATEQ,    STRPREEQ,
			SWAP,
	COMMA,
	POSTMINUS,	POSTPLUS,
	ID,
	M_OUTPAR,
	ERROR,
	EOI,
	TOKCOUNT
};


/*
 * Precedence table:  Operators with a lower precedence VALUE have a higher
 * precedence.  The theory behind infix notation (algebraic notation) is that
 * you have a sequence of operands seperated by (typically binary) operators.
 * The problem of precedence is that each operand is surrounded by two
 * operators, so it is ambiguous which operator the operand "binds" to.  This
 * is resolved by "precedence rules" which state that given two operators,
 * which one is allowed to "reduce" (operate on) the operand.  For a simple
 * explanation, take the expression  (3+4*5).  Now the middle operand is a
 * '4', but we dont know if it should be reduced via the plus, or via the
 * multiply.  If we look up both operators in the prec table, we see that
 * multiply has the lower value -- therefore the 4 is reduced via the multiply
 * and then the result of the multiply is reduced by the addition.
 */
static	int	prec[TOKCOUNT] = 
{
	1,
	2,		2,		2,		2,
			2,		2,		2,
			2,		2,
	3,
	4,		4,		4,
	5,		5,		5,
	6,		6,
	7,		7,		7,		7,
	8,		8,
	9,		9,
	10,
	11,
	12,
	13,
	14,
	15,
	16,		16,
	17,		17,		17,		17,		17,
			17,		17,		17,		17,
			17,		17,		17,		17,
			17,		17,		17,		17,
			17,
	18,
	2,		2,
	0,
	137,
	156,
	200
};
#define TOPPREC 21


/*
 * Associativity table: But precedence isnt enough.  What happens when you
 * have two identical operations to determine between?  Well, the easy way
 * is to say that the first operation is always done first.  But some
 * operators dont work that way (like the assignment operator) and always
 * reduce the LAST (or rightmost) operation first.  For example:
 *	(3+4+5)	    ((4+3)+5)    (7+5)    (12)
 *      (v1=v2=3)   (v1=(v2=3))  (v1=3)   (3)
 * So for each operator we need to specify how to determine the precedence
 * of the same operator against itself.  This is called "associativity".
 * Left-to-right associativity means that the operations occur left-to-right,
 * or first-operator, first-reduced.  Right-to-left associativity means
 * that the operations occur right-to-left, or last-operator, first-reduced.
 * 
 * We have a special case of associativity called BOOL, which is just a 
 * special type of left-to-right associtivity whereby the right hand side
 * of the operand is not automatically parsed. (not really, but its the 
 * theory that counts.)
 */
static 	int 	assoc[TOKCOUNT] =
{
	LR,
	RL,		RL,		RL,		RL,
			RL,		RL,		RL,
			RL,		RL,
	RL,
	LR,		LR,		LR,
	LR,		LR,		LR,
	LR,		LR,
	LR,		LR,		LR,		LR,
	LR,		LR,
	LR,		LR,
	LR,
	LR,
	LR,
	BOOL,
	BOOL,
	BOOL,
	RL,		RL,
	RL,		RL,		RL,		RL,		RL,
			RL,		RL,		RL,		RL,
			RL,		RL,		RL,		RL,
			RL,		RL,		RL,		RL,
			RL,
	LR,
	RL,		RL,
	LR,
	LR,
	LR,
	LR
};


/* ********************* CREATE NEW SYMBOLS AND TOKENS ********************/
/* Lvalues are stored via this routine */
__inline static	TOKEN		tokenize_lval (expr_info *c, const char *t)
{
	if (c->token >= TOKENCOUNT)
	{
		error("Too many tokens for this expression");
		return -1;
	}
	TOK(c, c->token).used = USED_LVAL;
	malloc_strcpy(&TOK(c, c->token).lval, t);
	return c->token++;
}

/*
 * This is where we store our rvalues, kind of.  What we really store here
 * are all of the string operands from the original expression.  Whenever
 * one of the original operands is parsed, it is converted to a string and
 * put in here and the index into the 'token' table is returned.  Only the
 * operands of the original expression go here.  All derived operands (the
 * result of operators) directly create "expanded" tokens.
 */
/* THIS FUNCTION MAKES A NEW COPY OF 'T'.  YOU MUST DISPOSE OF 'T' YOURSELF */
__inline static	TOKEN		tokenize_raw (expr_info *c, const char *t)
{
	if (c->token >= TOKENCOUNT)
	{
		error("Too many tokens for this expression");
		return -1;
	}
	TOK(c, c->token).used = USED_RAW;
	malloc_strcpy(&TOK(c, c->token).raw_value, t);
	return c->token++;
}

/*
 * This is where any rvalues are stored.  The result of any operation that
 * yeilds a string creates an 'expanded' token.  Tokens that begin life as
 * 'expanded' tokens never can be passed through expand_alias() again.  This
 * protects against possible security holes in the client.
 */
__inline static	TOKEN		tokenize_expanded (expr_info *c, char *t)
{
	if (c->token >= TOKENCOUNT)
	{
		error("Too many tokens for this expression");
		return -1;
	}
	TOK(c, c->token).used = USED_EXPANDED;
	malloc_strcpy(&TOK(c, c->token).expanded_value, t);
	return c->token++;
}

/*
 * This creates an integer token.  This is useful for quick operation on
 * complex mathematical expressions, where conversions to and from strings
 * is both expensive, and unnecesary.  This type of token is created by
 * integer-only operations like &, |, ^, <<, >>.
 */
__inline static	TOKEN		tokenize_integer (expr_info *c, INTTYPE t)
{
	if (c->token >= TOKENCOUNT)
	{
		error("Too many tokens for this expression");
		return -1;
	}
	TOK(c, c->token).used = USED_INTEGER;
	TOK(c, c->token).integer_value = t;
	return c->token++;
}

/*
 * This creates a floating point token.  This is useful for the same things
 * that integer tokens are, and this is the token generated by floating 
 * point operations, such as +, -, *, /, %, **.
 */
__inline static	TOKEN		tokenize_float (expr_info *c, double t)
{
	if (c->token >= TOKENCOUNT)
	{
		error("Too many tokens for this expression");
		return -1;
	}
	TOK(c, c->token).used = USED_FLOAT;
	TOK(c, c->token).float_value = t;
	return c->token++;
}

/*
 * This creates a boolean token.  This is useful for the same things as
 * integer tokens are.  This token is generated by any sort of logic 
 * operation, such as ==, !=, >, <, &&, ||.
 */
__inline static	TOKEN		tokenize_bool (expr_info *c, BooL t)
{
	if (c->token >= TOKENCOUNT)
	{
		error("Too many tokens for this expression");
		return -1;
	}
	TOK(c, c->token).used = USED_BOOLEAN;
	TOK(c, c->token).boolean_value = t;
	return c->token++;
}

/******************** RETRIEVE SYMBOLS FROM TOKEN HANDLES *******************/
/*
 * These functions permit you to get at the tokens in various ways.
 * There is a definite concept of each token being stored in several
 * different representations: "raw" format (as the user specified it 
 * in the expression), "expanded" format (what the raw value looks like
 * after it is passed through expand_alias), "integer" format (what the
 * expanded value looks like after a call to atol()), "float" format,
 * (what the expanded value looks like after a call to atof()), and 
 * "boolean" format (what the expanded value looks like after a call to
 * check_val()).  Each of these types are created on demand; so that if
 * a token is never referenced in an integer context, the expensive 
 * conversion to an integer is not performed.  This is inteded to keep
 * the number of unnecesary conversions to an absolute minimum.
 *
 * YOU MUST NOT EVER -- EVER -- FREE THE RETURN VALUE FROM ANY OF THESE
 * FUNCTIONS!  TO DO SO WILL CRASH THE CLIENT!  YOU HAVE BEEN WARNED!
 */

/*
 * Get the "raw" value of the token.  For tokens that have been created
 * via "tokenize", nothing has to be done since the raw value is already
 * present.  However, for computed tokens (such as the result of a math 
 * operation), the raw token may not actually exist, and so it must be
 * infered from whatever data is available.  We use the value that has 
 * the most information, starting with expanded string, to the boolean
 * value. 
 */ 
__inline static	const char *	get_token_raw (expr_info *c, TOKEN v)
{
	if (v == MAGIC_TOKEN)	/* Magic token */
		return c->args;			/* XXX Probably wrong */

	if (v < 0 || v >= c->token)
	{
		error("Token index [%d] is out of range", v);
		return get_token_raw(c, 0);	/* The empty token */
	}

	if (v == 0)
		return empty_string;

	if (c->noeval)
		return get_token_raw(c, 0);

	if ((TOK(c, v).used & USED_RAW) == 0)
	{
		TOK(c, v).used |= USED_RAW;

		if (TOK(c, v).used & USED_EXPANDED)
			panic("Cannot convert EXPANDED token to RAW");
		else if (TOK(c, v).used & USED_FLOAT)
		{
			TOK(c, v).raw_value = 
				malloc_sprintf(NULL, "%f", TOK(c, v).float_value);
			canon_number(TOK(c, v).raw_value);
		}
		else if (TOK(c, v).used & USED_INTEGER)
			TOK(c, v).raw_value = 
				INT2STR(TOK(c, v).integer_value);
		else if (TOK(c, v).used & USED_BOOLEAN)
			TOK(c, v).raw_value = 
				malloc_sprintf(NULL, "%d", TOK(c, v).boolean_value);
		else if (TOK(c, v).used & USED_LVAL)
		{
			if (x_debug & DEBUG_NEW_MATH_DEBUG)
				yell(">>> Expanding var name [%d]: [%s]", 
					v, TOK(c, v).lval);

			TOK(c, v).raw_value = expand_alias(TOK(c, v).lval, 
					c->args, NULL);

			if (x_debug & DEBUG_NEW_MATH_DEBUG)
				yell(">>> Expanded var name [%d]: [%s] to [%s]",
					v, TOK(c, v).lval, TOK(c, v).raw_value);
		}
		else
			panic("Can't convert this token to raw format");
	}

	return TOK(c, v).raw_value;
}

/*
 * This is kind of funky.  This is used to get the "fully qualified"
 * lvalue for a given token.  What that means is that this is not the
 * raw value of TOK(c, v).lval, but is rather TOK(c, v).raw, but if
 * and only if (TOK(c, v).used & USED_LVAL)!  Otherwise, this returns
 * NULL and emits an error.
 */
__inline static const char *	get_token_lval (expr_info *c, TOKEN v)
{
	if (v == MAGIC_TOKEN)	/* Magic token */
		return c->args;			/* XXX Probably wrong */

	if (v < 0 || v >= c->token)
	{
		error("Token index [%d] is out of range", v);
		return NULL;			/* No lvalue here! */
	}

	if (v == 0)
		return NULL;		/* Suppress the operation entirely */
	else if (c->noeval)
		return NULL;		/* Suppress the operation entirely */
	else if (((TOK(c, v).used & USED_LVAL) == 0))
	{
		error("Token [%d] is not an lvalue", v);
		return NULL;			/* No lvalue here! */
	}
	else
		return get_token_raw(c, v);
}


/*
 * Get the "expanded" representation of the token.  The first time you
 * call this function, it gets the "raw" value, if there is one, and then
 * call expand_alias() to get the value.  Of course, there is always a
 * "raw" value, as get_token_raw() can convert from anything.
 */
__inline static	const char *	get_token_expanded (expr_info *c, TOKEN v)
{
	if (v == MAGIC_TOKEN)	/* Magic token */
		return c->args;

	if (v < 0 || v >= c->token)
	{
		error("Token index [%d] is out of range", v);
		return get_token_expanded(c, 0);	/* The empty token */
	}

	if (v == 0)
		return get_token_raw(c, 0);

	if (c->noeval)
		return get_token_raw(c, 0);

	if (x_debug & DEBUG_NEW_MATH_DEBUG)
		yell(">>> Getting token [%d] now.", v);

	if ((TOK(c, v).used & USED_EXPANDED) == 0)
	{
		char	*myval;

		myval = LOCAL_COPY(get_token_raw(c, v));
		TOK(c, v).used |= USED_EXPANDED;

		/*
		 * If this token started off life as an lval, then the
		 * "raw" value of this token will yeild the expanded form
		 * of the lvalue, suitable for passing to alias_special_char.
		 */
		if (TOK(c, v).used & USED_LVAL)
		{
			char *buffer = NULL;

			if (x_debug & DEBUG_NEW_MATH_DEBUG)
				yell(">>> Looking up variable [%d]: [%s]", 
					v, myval);

			alias_special_char(&buffer, myval, c->args, 
					NULL);
			if (!buffer)
				buffer = malloc_strdup(empty_string);
			TOK(c, v).expanded_value = buffer;

			if (x_debug & DEBUG_NEW_MATH_DEBUG)
				yell("<<< Expanded variable [%d] [%s] to: [%s]",
					v, myval, TOK(c, v).expanded_value);
		}

		/*
		 * Otherwise, this token started off life as an rval
		 * (such as a [...] string), and only needs to be expanded
		 * with alias_special_char to yeild a useful value.
		 */
		else if (TOK(c, v).used & USED_RAW)
		{
			if (x_debug & DEBUG_NEW_MATH_DEBUG)
				yell(">>> Expanding token [%d]: [%s]", 
					v, myval);

			TOK(c, v).used |= USED_EXPANDED;
			TOK(c, v).expanded_value = 
				expand_alias(myval, c->args, 
						NULL);

			if (x_debug & DEBUG_NEW_MATH_DEBUG)
				yell("<<< Expanded token [%d]: [%s] to: [%s]", 
					v, myval, TOK(c, v).expanded_value);
		}
		else
			panic("Cannot convert from this token to EXPANDED");
	}

	if (x_debug & DEBUG_NEW_MATH_DEBUG)
		yell("<<< Token [%d] value is [%s].", v, TOK(c, v).expanded_value);
	return TOK(c, v).expanded_value;
}

/*
 * Get the integer representation of the token.  The first time you call
 * this function, it calls atof() on the "raw" value, if there is one, 
 * to get the result.
 */
__inline static	INTTYPE	get_token_integer (expr_info *c, TOKEN v)
{
	if (v == MAGIC_TOKEN)	/* Magic token */
		return STR2INT(c->args);		/* XXX Probably wrong */

	if (v < 0 || v >= c->token)
	{
		error("Token index [%d] is out of range", v);
		return 0;		/* The empty token */
	}
	
	if ((TOK(c, v).used & USED_INTEGER) == 0)
	{
		const char *	myval = get_token_expanded(c, v);

		TOK(c, v).used |= USED_INTEGER;
		TOK(c, v).integer_value = STR2INT(myval);
	}
	return TOK(c, v).integer_value;
}

/*
 * Get the floating point value representation of the token.
 */
__inline static	double	get_token_float (expr_info *c, TOKEN v)
{
	if (v == MAGIC_TOKEN)	/* Magic token */
		return atof(c->args);		/* XXX Probably wrong */

	if (v < 0 || v >= c->token)
	{
		error("Token index [%d] is out of range", v);
		return 0.0;		/* The empty token */
	}
	
	if ((TOK(c, v).used & USED_FLOAT) == 0)
	{
		const char *	myval = get_token_expanded(c, v);

		TOK(c, v).used |= USED_FLOAT;
		TOK(c, v).float_value = atof(myval);
	}
	return TOK(c, v).float_value;
}

/*
 * Get the boolean value of the token
 */
__inline static	BooL	get_token_boolean (expr_info *c, TOKEN v)
{
	if (v == MAGIC_TOKEN)	/* Magic token */
		return check_val(c->args);	/* XXX Probably wrong */

	if (v < 0 || v >= c->token)
	{
		error("Token index [%d] is out of range", v);
		return 0;		/* The empty token */
	}
	
	if ((TOK(c, v).used & USED_BOOLEAN) == 0)
	{
		const char *	myval = get_token_expanded(c, v);

		TOK(c, v).used |= USED_BOOLEAN;
		TOK(c, v).boolean_value = check_val(myval);
	}
	return TOK(c, v).boolean_value;
}

/* *********************** ADD TO OPERAND STACK **************************** */
/*
 * Adding (shifting) and Removing (reducing) operands from the stack is a 
 * fairly straightforward process.  The general way to add an token to
 * the stack is to pass in its TOKEN index.  However, there are some times
 * when you want to shift a value that has not been tokenized.  So you call
 * one of the other functions that will do this for you.
 */
__inline static	TOKEN	push_token (expr_info *c, TOKEN t)
{
	if (c->sp == STACKSZ - 1)
	{
		error("Expressions may not have more than %d operands",
			STACKSZ);
		return -1;
	}
	else
		c->sp++;

	if (x_debug & DEBUG_NEW_MATH_DEBUG)
		yell("Pushing token [%d] [%s]", t, get_token_expanded(c, t));

	return ((c->stack[c->sp] = t));
}

__inline static	TOKEN	push_string (expr_info *c, char *val)
{
	return push_token(c, tokenize_expanded(c, val));
}

__inline static	TOKEN	push_float (expr_info *c, double val)
{ 
	return push_token(c, tokenize_float(c, val));
}

__inline static	TOKEN	push_integer (expr_info *c, INTTYPE val)
{ 
	return push_token(c, tokenize_integer(c, val));
}

__inline static TOKEN	push_boolean (expr_info *c, BooL val)
{
	return push_token(c, tokenize_bool(c, val));
}

__inline static TOKEN	push_lval (expr_info *c, const char *val)
{
	return push_token(c, tokenize_lval(c, val));
}


/*********************** REMOVE FROM OPERAND STACK **************************/
__inline static TOKEN	top (expr_info *c)
{
	if (c->sp < 0)
	{
		error("No operands.");
		return -1;
	}
	else
		return c->stack[c->sp];
}

__inline static	TOKEN	pop_token (expr_info *c)
{
	if (c->sp < 0)
	{
		/* 
		 * Attempting to pop more operands than are available
		 * Yeilds empty values.  Thats probably the most reasonable
		 * course of action.
		 */
		error("Cannot pop operand: no more operands");
		return 0;
	}
	else
		return c->stack[c->sp--];
}

__inline static	double		pop_integer (expr_info *c)
{
	return get_token_integer(c, pop_token(c));
}

__inline static	double		pop_float (expr_info *c)
{
	return get_token_float(c, pop_token(c));
}

__inline static	const char *	pop_expanded (expr_info *c)
{
	return get_token_expanded(c, pop_token(c));
}

__inline static	BooL		pop_boolean (expr_info *c)
{
	return get_token_boolean(c, pop_token(c));
}

__inline static void	pop_2_tokens (expr_info *c, TOKEN *t1, TOKEN *t2)
{
	*t2 = pop_token(c);
	*t1 = pop_token(c);
}

__inline static	void	pop_2_floats (expr_info *c, double *a, double *b)
{
	*b = pop_float(c);
	*a = pop_float(c);
}

__inline static	void	pop_2_integers (expr_info *c, INTTYPE *a, INTTYPE *b)
{
	*b = pop_integer(c);
	*a = pop_integer(c);
}

__inline static void	pop_2_strings (expr_info *c, const char **s, const char **t)
{
	*t = pop_expanded(c);
	*s = pop_expanded(c);
}

__inline static void	pop_2_booleans (expr_info *c, BooL *a, BooL *b)
{
	*b = pop_boolean(c);
	*a = pop_boolean(c);
}

__inline static	void	pop_3_tokens (expr_info *c, BooL *a, TOKEN *v, TOKEN *w)
{
	TOKEN	t1, t2, t3;

	t3 = pop_token(c);
	t2 = pop_token(c);
	t1 = pop_token(c);
	*a = get_token_boolean(c, t1);
	*v = t2;
	*w = t3;
}


/******************************* OPERATOR REDUCER **************************/
/*
 * This is the reducer.  It takes the relevant arguments off the argument
 * stack and then performs the neccesary operation on them.
 */
static void	reduce (expr_info *cx, int what)
{
	double	a, b;
	BooL	c, d;
	INTTYPE	i, j;
	const char *s, *t;
	TOKEN	v, w;

	if (x_debug & DEBUG_NEW_MATH_DEBUG)
		yell("Reducing last operation...");

	if (cx->sp < 0) 
	{
		error("An operator is missing a required operand");
		return;
	}

	if (cx->errflag)
		return;		/* Dont parse on an error */

/* Check to see if we are evaluating the expression at this point. */
#define CHECK_NOEVAL							\
	if (cx->noeval)							\
	{								\
		if (x_debug & DEBUG_NEW_MATH_DEBUG)			\
			yell("O: Operation short-circuited");		\
		push_token(cx, 0);					\
		break;							\
	}

/* Perform an ordinary garden variety floating point binary operation. */
#define BINARY_FLOAT(floatop)						\
	{ 								\
		pop_2_floats(cx, &a, &b); 				\
		CHECK_NOEVAL						\
		push_float(cx, (floatop));				\
		if (x_debug & DEBUG_NEW_MATH_DEBUG) 			\
			yell("O: %s (%f %f) -> %f", 			\
				#floatop, a, b, floatop); 		\
		break; 							\
	}

/* Perform an ordinary garden variety integer binary operation */
#define BINARY_INTEGER(intop)						\
	{ 								\
		pop_2_integers(cx, &i, &j); 				\
		CHECK_NOEVAL						\
		push_integer(cx, (intop)); 				\
		if (x_debug & DEBUG_NEW_MATH_DEBUG) 			\
			yell("O: %s (" FORMAT " " FORMAT ") -> " FORMAT, \
				#intop, i, j, intop); 			\
		break; 							\
	}

/* Perform an ordinary garden variety boolean binary operation */
#define BINARY_BOOLEAN(boolop) 						\
	{ 								\
		pop_2_booleans(cx, &c, &d); 				\
		CHECK_NOEVAL						\
		push_boolean(cx, (boolop)); 				\
		if (x_debug & DEBUG_NEW_MATH_DEBUG) 			\
			yell("O: %s (%d %d) -> %d", 			\
				#boolop, c, d, boolop); 		\
		break; 							\
	}

/* Perform a floating point binary operation where the rhs must not be 0. */
#define BINARY_FLOAT_NOZERO(floatop)					\
	{ 								\
		pop_2_floats(cx, &a, &b); 				\
		CHECK_NOEVAL						\
		if (b == 0.0) 						\
		{ 							\
			if (x_debug & DEBUG_NEW_MATH_DEBUG) 		\
				yell("O: %s (%f %f) -> []", 		\
					#floatop, a, b); 		\
			error("Division by zero"); 			\
			push_token(cx, 0);				\
		} 							\
		else 							\
		{ 							\
			if (x_debug & DEBUG_NEW_MATH_DEBUG) 		\
			    yell("O: %s (%f %f) -> %f", 		\
					#floatop, a, b, floatop); 	\
			push_float(cx, (floatop)); 			\
		} 							\
		break; 							\
	}

/* Perform a floating point binary operation where the rhs must not be 0. */
#define BINARY_INTEGER_NOZERO(intop)					\
	{ 								\
		pop_2_integers(cx, &i, &j); 				\
		CHECK_NOEVAL						\
		if (j == 0) 						\
		{ 							\
			if (x_debug & DEBUG_NEW_MATH_DEBUG) 		\
				yell("O: %s (" FORMAT " " FORMAT ") -> []", \
					#intop, i, j); 			\
			error("Division by zero"); 			\
			push_token(cx, 0);				\
		} 							\
		else 							\
		{ 							\
			if (x_debug & DEBUG_NEW_MATH_DEBUG) 		\
			    yell("O: %s (" FORMAT " " FORMAT ") -> " FORMAT, \
					#intop, i, j, intop); 		\
			push_integer(cx, (intop)); 			\
		} 							\
		break; 							\
	}

/***************** ASSIGNMENT MACROS *******************/
/* Prep the lvalue and the rvalue for a future assignment. */
#define	GET_LVAL_RVAL							\
		pop_2_tokens(cx, &v, &w);				\
		if (!(s = get_token_lval(cx, v)))			\
		{							\
			push_token(cx, 0);				\
			break;						\
		}

/* Perform an ordinary integer operation, assigning the result to the lvalue */
#define IMPLIED_INTEGER(intop) 						\
	{ 								\
		GET_LVAL_RVAL						\
		CHECK_NOEVAL						\
		i = get_token_integer(cx, v);				\
		j = get_token_integer(cx, w);				\
									\
		if (x_debug & DEBUG_NEW_MATH_DEBUG) 			\
			yell("O: %s = %s (" FORMAT " " FORMAT ") -> " FORMAT, \
				s, #intop, i, j, intop); 		\
									\
		w = tokenize_integer(cx, (intop));			\
		t = get_token_expanded(cx, w);				\
		add_var_alias(s, t, 0);					\
		push_token(cx, w);					\
		break; 							\
	}

/* Perform an ordinary float operation, assigning the result to the lvalue */
#define IMPLIED_FLOAT(floatop) 						\
	{ 								\
		GET_LVAL_RVAL						\
		CHECK_NOEVAL						\
		a = get_token_float(cx, v);				\
		b = get_token_float(cx, w);				\
									\
		if (x_debug & DEBUG_NEW_MATH_DEBUG) 			\
			yell("O: %s = %s (%f %f) -> %f",  		\
				s, #floatop, a, b, floatop);		\
									\
		w = tokenize_float(cx, (floatop));			\
		t = get_token_expanded(cx, w);				\
		add_var_alias(s, t, 0);					\
		push_token(cx, w);					\
		break; 							\
	}

/* Perform an ordinary boolean operation, assigning the result to the lvalue */
#define IMPLIED_BOOLEAN(boolop) 					\
	{ 								\
		GET_LVAL_RVAL						\
		CHECK_NOEVAL						\
		c = get_token_boolean(cx, v);				\
		d = get_token_boolean(cx, w);				\
									\
		if (x_debug & DEBUG_NEW_MATH_DEBUG) 			\
			yell("O: %s = %s (%d %d) -> %d",  		\
				s, #boolop, c, d, boolop); 		\
									\
		w = tokenize_bool(cx, (boolop));			\
		t = get_token_expanded(cx, w);				\
		add_var_alias(s, t, 0);					\
		push_token(cx, w);					\
		break; 							\
	}


/* 
 * Perform a float operation, rvalue must not be zero, assigning the result
 * to the lvalue. 
 */
#define IMPLIED_FLOAT_NOZERO(floatop) 					\
	{ 								\
		GET_LVAL_RVAL						\
		CHECK_NOEVAL						\
		a = get_token_float(cx, v);				\
		b = get_token_float(cx, w);				\
									\
		if (b == 0.0) 						\
		{ 							\
			if (x_debug & DEBUG_NEW_MATH_DEBUG) 		\
				yell("O: %s = %s (%f %f) -> 0",  	\
					s, #floatop, a, b); 		\
			error("Division by zero"); 			\
			add_var_alias(s, empty_string, 0);		\
			push_token(cx, 0);				\
			break;						\
		} 							\
									\
		if (x_debug & DEBUG_NEW_MATH_DEBUG) 			\
			yell("O: %s =  %s (%f %f) -> %f",  		\
				s, #floatop, a, b, floatop); 		\
									\
		w = tokenize_float(cx, (floatop));			\
		t = get_token_expanded(cx, w);				\
		add_var_alias(s, t, 0);					\
		push_token(cx, w);					\
		break; 							\
	}

/* 
 * Perform a float operation, rvalue must not be zero, assigning the result
 * to the lvalue. 
 */
#define IMPLIED_INTEGER_NOZERO(intop) 					\
	{ 								\
		GET_LVAL_RVAL						\
		CHECK_NOEVAL						\
		i = get_token_float(cx, v);				\
		j = get_token_float(cx, w);				\
									\
		if (j == 0) 						\
		{ 							\
			if (x_debug & DEBUG_NEW_MATH_DEBUG) 		\
				yell("O: %s = %s (" FORMAT " " FORMAT ") -> 0",\
					s, #intop, i, j); 		\
			error("Division by zero"); 			\
			add_var_alias(s, empty_string, 0);		\
			push_token(cx, 0);				\
			break;						\
		} 							\
									\
		if (x_debug & DEBUG_NEW_MATH_DEBUG) 			\
			yell("O: %s =  %s (" FORMAT " "  FORMAT ") -> " FORMAT,\
				s, #intop, i, j, intop); 		\
									\
		w = tokenize_float(cx, (intop));			\
		t = get_token_expanded(cx, w);				\
		add_var_alias(s, t, 0);					\
		push_token(cx, w);					\
		break; 							\
	}


/* 
 * Perform an auto(in|de)crement operation on the last operand, assigning
 * to it the value of 'x', but pushing the value of 'y' onto the stack.
 */
#define AUTO_UNARY(intop_assign, intop_result) 				\
	{ 								\
		v = pop_token(cx); 					\
		if (!(s = get_token_lval(cx, v)))			\
		{							\
			push_token(cx, 0);				\
			break;						\
		}							\
		CHECK_NOEVAL						\
									\
		j = get_token_integer(cx, v);				\
		if (x_debug & DEBUG_NEW_MATH_DEBUG) 			\
			yell("O: %s (%s " FORMAT ") -> " FORMAT, 	\
				#intop_result, s, j, (intop_result));	\
									\
		w = tokenize_integer(cx, (intop_assign));		\
		t = get_token_expanded(cx, w);				\
		add_var_alias(s, t, 0);					\
									\
		push_integer(cx, (intop_result));			\
		break; 							\
	}

/* ****************** START HERE *********************/
#define dpushn(x1,x2,y1) 						\
	{ 								\
		if (x_debug & DEBUG_NEW_MATH_DEBUG) 			\
		{ 							\
			yell("O: COMPARE"); 				\
			yell("O: %s -> %d", #x2, (x2)); 		\
		} 							\
		push_boolean( x1 , y1 ); 				\
	} 

#define COMPARE(x, y) 							\
	{ 								\
		pop_2_strings(cx, &s, &t);				\
		CHECK_NOEVAL						\
		if (is_real_number(s) && is_real_number(t))		\
		{							\
			a = atof(s), b = atof(t);			\
			if (x_debug & DEBUG_NEW_MATH_DEBUG) 		\
				yell("O: %s N(%f %f) -> %d", #x, a, b, (x)); \
			if ((x))		dpushn(cx, x, 1) 	\
			else			dpushn(cx, x, 0) 	\
		} 							\
		else 							\
		{ 							\
			if (x_debug & DEBUG_NEW_MATH_DEBUG) 		\
				yell("O: %s S(%s %s) -> %d", #x, s, t, (y)); \
			if ((y))		dpushn(cx, y, 1) 	\
			else			dpushn(cx, y, 0) 	\
		} 							\
		break; 							\
	}

	/************** THE OPERATORS THEMSELVES ********************/
	switch (what) 
	{
		/* Simple unary prefix operators */
		case NOT:
			c = pop_boolean(cx);
			CHECK_NOEVAL
			if (x_debug & DEBUG_NEW_MATH_DEBUG)
				yell("O: !%d -> %d", c, !c);
			push_boolean(cx, !c);
			break;
		case COMP:
			i = pop_integer(cx);
			CHECK_NOEVAL
			if (x_debug & DEBUG_NEW_MATH_DEBUG)
				yell(": ~" FORMAT " -> " FORMAT, i, ~i);
			push_integer(cx, ~i);
			break;
		case UPLUS:
			a = pop_float(cx);
			CHECK_NOEVAL
			if (x_debug & DEBUG_NEW_MATH_DEBUG)
				yell("O: +%f -> %f", a, a);
			push_float(cx, a);
			break;
		case UMINUS:
			a = pop_float(cx);
			CHECK_NOEVAL
			if (x_debug & DEBUG_NEW_MATH_DEBUG)
				yell("O: -%f -> %f", a, -a);
			push_float(cx, -a);
			break;
		case STRLEN:
			s = pop_expanded(cx);
			CHECK_NOEVAL
			i = strlen(s);
			if (x_debug & DEBUG_NEW_MATH_DEBUG)
				yell("O: @(%s) -> " FORMAT, s, i);
			push_integer(cx, i);
			break;
		case WORDC:
			s = pop_expanded(cx);
			CHECK_NOEVAL
			i = count_words(s, DWORD_YES, "\"");
			if (x_debug & DEBUG_NEW_MATH_DEBUG)
				yell("O: #(%s) -> " FORMAT, s, i);
			push_integer(cx, i);
			break;
		case DEREF:
		{
			if (top(cx) == MAGIC_TOKEN)
				break;		/* Dont do anything */

			/*
			 * We need to consume the operand, even if
			 * we don't intend to use it; plus we need
			 * to ensure this defeats auto-append.  Ick.
			 */
			s = pop_expanded(cx);

			CHECK_NOEVAL
			push_lval(cx, s);
			break;
		}

		/* (pre|post)(in|de)crement operators. */
		case PREPLUS:   AUTO_UNARY(j + 1, j + 1)
		case PREMINUS:  AUTO_UNARY(j - 1, j - 1)
		case POSTPLUS:	AUTO_UNARY(j + 1, j)
		case POSTMINUS: AUTO_UNARY(j - 1, j)

		/* Simple binary operators */
		case AND:	BINARY_INTEGER(i & j)
		case XOR:	BINARY_INTEGER(i ^ j)
		case OR:	BINARY_INTEGER(i | j)
		case PLUS:	BINARY_FLOAT(a + b)
		case MINUS:	BINARY_FLOAT(a - b)
		case MUL:	BINARY_FLOAT(a * b)
		case POWER:	BINARY_FLOAT(pow(a, b))
		case SHLEFT:	BINARY_INTEGER(i << j)
		case SHRIGHT:	BINARY_INTEGER(i >> j)
		case DIV:	BINARY_FLOAT_NOZERO(a / b)
		case MOD:	BINARY_INTEGER_NOZERO(i % j)
		case DAND:	BINARY_BOOLEAN(c && d)
		case DOR:	BINARY_BOOLEAN(c || d)
		case DXOR:	BINARY_BOOLEAN((c && !d) || (!c && d))
		case STRCAT:	
		{
			char *	myval;

			pop_2_strings(cx, &s, &t);
			CHECK_NOEVAL
			if (x_debug & DEBUG_NEW_MATH_DEBUG)
				yell("O: (%s) ## (%s) -> %s%s", s, t, s, t);

			myval = malloc_strdup2(s, t);
			push_string(cx, myval);
			new_free(&myval);
			break;
		}
		/* Assignment operators */
		case PLUSEQ:	IMPLIED_FLOAT(a + b)
		case MINUSEQ:	IMPLIED_FLOAT(a - b)
		case MULEQ:	IMPLIED_FLOAT(a * b)
		case POWEREQ:	IMPLIED_FLOAT(pow(a, b))
		case DIVEQ:	IMPLIED_FLOAT_NOZERO(a / b)
		case MODEQ:	IMPLIED_INTEGER_NOZERO(i % j)
		case ANDEQ:	IMPLIED_INTEGER(i & j)
		case XOREQ:	IMPLIED_INTEGER(i ^ j)
		case OREQ:	IMPLIED_INTEGER(i | j)
		case SHLEFTEQ:	IMPLIED_INTEGER(i << j)
		case SHRIGHTEQ: IMPLIED_INTEGER(i >> j)
		case DANDEQ:	IMPLIED_BOOLEAN(c && d)
		case DOREQ:	IMPLIED_BOOLEAN(c || d)
		case DXOREQ:	IMPLIED_BOOLEAN((c && !d) || (!c && d))
		case STRCATEQ:
		{
			char *	myval;

			GET_LVAL_RVAL
			CHECK_NOEVAL
			myval = malloc_strdup(get_token_expanded(cx, v));
			t = get_token_expanded(cx, w);

			if (x_debug & DEBUG_NEW_MATH_DEBUG) 
				yell("O: %s = (%s ## %s) -> %s%s", 
					s, myval, t, myval, t);

			malloc_strcat(&myval, t);
			push_string(cx, myval);
			add_var_alias(s, myval, 0);
			new_free(&myval);
			break;
		}
		case STRPREEQ:
		{
			char *	myval;

			GET_LVAL_RVAL
			CHECK_NOEVAL
			myval = malloc_strdup(get_token_expanded(cx, w));
			t = get_token_expanded(cx, v);

			if (x_debug & DEBUG_NEW_MATH_DEBUG) 
				yell("O: %s = (%s ## %s) -> %s%s", 
					s, t, myval, t, myval);

			malloc_strcat(&myval, t);
			push_string(cx, myval);
			add_var_alias(s, myval, 0);
			new_free(&myval);
			break;
		}
		case EQ:
		{
			GET_LVAL_RVAL
			CHECK_NOEVAL
			t = get_token_expanded(cx, w);

			if (x_debug & DEBUG_NEW_MATH_DEBUG)
				yell("O: %s = (%s)", s, t);

			push_token(cx, w);
			add_var_alias(s, t, 0);
			break;
		}
		case SWAP:
		{
			const char *sval, *tval;

			pop_2_tokens(cx, &v, &w);
			CHECK_NOEVAL
			if (!(s = get_token_lval(cx, v)))
			{
				push_token(cx, 0);
				break;		
			}
			if (!(t = get_token_lval(cx, w)))
			{
				push_token(cx, 0);
				break;		
			}
			sval = get_token_expanded(cx, v);
			tval = get_token_expanded(cx, w);

			if (x_debug & DEBUG_NEW_MATH_DEBUG)
				yell("O: %s <=> %s", s, t);

			add_var_alias(s, tval, 0);
			add_var_alias(t, sval, 0);
			push_token(cx, w);
			break;
		}

		/* Comparison operators */
		case DEQ:
		{
			pop_2_strings(cx, &s, &t);
			CHECK_NOEVAL
			c = my_stricmp(s, t) ? 0 : 1;

			if (x_debug & DEBUG_NEW_MATH_DEBUG)
				yell("O: %s == %s -> %d", s, t, c);

			push_boolean(cx, c);
			break;
		}
		case NEQ:
		{
			pop_2_strings(cx, &s, &t);
			CHECK_NOEVAL
			c = my_stricmp(s, t) ? 1 : 0;

			if (x_debug & DEBUG_NEW_MATH_DEBUG)
				yell("O: %s != %s -> %d", s, t, c);

			push_boolean(cx, c);
			break;
		}
		case MATCH:
		{
			pop_2_strings(cx, &s, &t);
			CHECK_NOEVAL
			c = wild_match(t, s) ? 1 : 0;
			
			if (x_debug & DEBUG_NEW_MATH_DEBUG)
				yell("O: %s =~ %s -> %d", s, t, c);

			push_boolean(cx, c);
			break;
		}
		case NOMATCH:
		{
			pop_2_strings(cx, &s, &t);
			CHECK_NOEVAL
			c = wild_match(t, s) ? 0 : 1;
			
			if (x_debug & DEBUG_NEW_MATH_DEBUG)
				yell("O: %s !~ %s -> %d", s, t, c);

			push_boolean(cx, c);
			break;
		}

		case LES:	COMPARE(a < b,  my_stricmp(s, t) < 0)
		case LEQ:	COMPARE(a <= b, my_stricmp(s, t) <= 0)
		case GRE:	COMPARE(a > b,  my_stricmp(s, t) > 0)
		case GEQ:	COMPARE(a >= b, my_stricmp(s, t) >= 0)

		/* Miscelaneous operators */
		case QUEST:
		{
			pop_3_tokens(cx, &c, &v, &w);
			CHECK_NOEVAL
			if (x_debug & DEBUG_NEW_MATH_DEBUG)
			{
				yell("O: %d ? %s : %s -> %s", c,
					get_token_expanded(cx, v),
					get_token_expanded(cx, w),
					(c ? get_token_expanded(cx, v) : 
					     get_token_expanded(cx, w)));
			}
			push_token(cx, c ? v : w);
			break;
		}
		case COLON:
			break;

		case COMMA:
		{
			pop_2_tokens(cx, &v, &w);
			CHECK_NOEVAL

			if (x_debug & DEBUG_NEW_MATH_DEBUG)
				yell("O: %s , %s -> %s", 
					get_token_expanded(cx, v),
					get_token_expanded(cx, w),
					get_token_expanded(cx, w));
			push_token(cx, w);
			break;
		}

		default:
			error("Unknown operator or out of operators");
			return;
	}
}


/**************************** EXPRESSION LEXER ******************************/
static	int	dummy = 1;

static int	lexerr (expr_info *c, const char *format, ...)
{
	char 	buffer[BIG_BUFFER_SIZE + 1];
	va_list	a;

	va_start(a, format);
	vsnprintf(buffer, BIG_BUFFER_SIZE, format, a);
	va_end(a);

	error("%s", buffer);
	c->errflag = 1;
	return EOI;
}

/*
 * 'operand' is state information that tells us about what the next token
 * is expected to be.  When a binary operator is lexed, then the next token
 * is expected to be either a unary operator or an operand.  So in this
 * case 'operand' is set to 1.  When an operand is lexed, then the next token
 * is expected to be a binary operator, so 'operand' is set to 0. 
 */
static __inline int	check_implied_arg (expr_info *c)
{
	if (c->operand == 2)
	{
		push_token(c, MAGIC_TOKEN);	/* XXXX Bleh */
		c->operand = 0;
		return 0;
	}

	return c->operand;
}

static __inline TOKEN 	operator (expr_info *c, const char *x, int y, TOKEN z)
{
	check_implied_arg(c);
	if (c->operand)
		return lexerr(c, "A binary operator (%s) was found "
				 "where an operand was expected", x);
	c->ptr += y;
	c->operand = 1;
	return z;
}

static __inline TOKEN 	unary (expr_info *c, const char *x, int y, TOKEN z)
{
	if (!c->operand)
		return lexerr(c, "A unary operator (%s) was found where "
				 "a binary operator was expected", x);
	c->ptr += y;
	c->operand = dummy;
	return z;
}


/*
 * This finds and extracts the next token in the expression
 */
static int	zzlex (expr_info *c)
{
	char	*start = c->ptr;

#define OPERATOR(x, y, z) return operator(c, x, y, z);
#define UNARY(x, y, z) return unary(c, x, y, z);

	dummy = 1;
	if (x_debug & DEBUG_NEW_MATH_DEBUG)
		yell("Parsing next token from: [%s]", c->ptr);

	for (;;)
	{
	    switch (*(c->ptr++)) 
	    {
		case '(':
			c->operand = 1;
			return M_INPAR;
		case ')':
			/*
			 * If we get a close paren and the lexer is expecting
			 * an operand, then obviously thats a syntax error.
			 * But we gently just insert the empty value as the
			 * rhs for the last operand and hope it all works out.
			 */
			if (check_implied_arg(c))
				push_token(c, 0);
			c->operand = 0;
			return M_OUTPAR;

		case '+':
		{
			/*
			 * Note:  In general, any operand that depends on 
			 * whether it is a unary or binary operator based
			 * upon the context is required to call the func
			 * 'check_implied_arg' to solidify the context.
			 * That is because some operators are ambiguous,
			 * And if you see   (# + 4), it can only be determined
			 * on the fly how to lex that.
			 */
			check_implied_arg(c);
			if (*c->ptr == '+' && (c->operand || !isalnum(*c->ptr))) 
			{
				c->ptr++;
				return c->operand ? PREPLUS : POSTPLUS;
			}
			else if (*c->ptr == '=') 
				OPERATOR("+=", 1, PLUSEQ)
			else if (c->operand)
				UNARY("+", 0, UPLUS)
			else
				OPERATOR("+", 0, PLUS)
		}
		case '-':
		{
			check_implied_arg(c);
			if (*c->ptr == '-' && (c->operand || !isalnum(*c->ptr)))
			{
				c->ptr++;
				return (c->operand) ? PREMINUS : POSTMINUS;
			}
			else if (*c->ptr == '=') 
				OPERATOR("-=", 1, MINUSEQ)
			else if (c->operand)
				UNARY("-", 0, UMINUS)
			else
				OPERATOR("-", 0, MINUS)
		}
		case '*':
		{
			if (*c->ptr == '*') 
			{
				c->ptr++;
				if (*c->ptr == '=') 
					OPERATOR("**=", 1, POWEREQ)
				else
					OPERATOR("**", 0, POWER)
			}
			else if (*c->ptr == '=') 
				OPERATOR("*=", 1, MULEQ)
			else if (c->operand)
			{
				dummy = 2;
				UNARY("*", 0, DEREF)
			}
			else
				OPERATOR("*", 0, MUL)
		}
		case '/':
		{
			if (*c->ptr == '=') 
				OPERATOR("/=", 1, DIVEQ)
			else
				OPERATOR("/", 0, DIV)
		}
		case '%':
		{
			if (*c->ptr == '=')
				OPERATOR("%=", 1, MODEQ)
			else
				OPERATOR("%", 0, MOD)
		}

		case '!':
		{
			if (*c->ptr == '=')
				OPERATOR("!=", 1, NEQ)
			else if (*c->ptr == '~')
				OPERATOR("!~", 1, NOMATCH)
			else
				UNARY("!", 0, NOT)
		}
		case '~':
			UNARY("~", 0, COMP)

		case '&':
		{
			if (*c->ptr == '&') 
			{
				c->ptr++;
				if (*c->ptr == '=')
					OPERATOR("&&=", 1, DANDEQ)
				else
					OPERATOR("&&", 0, DAND)
			} 
			else if (*c->ptr == '=') 
				OPERATOR("&=", 1, ANDEQ)
			else
				OPERATOR("&", 0, AND)
		}
		case '|':
		{
			if (*c->ptr == '|') 
			{
				c->ptr++;
				if (*c->ptr == '=') 
					OPERATOR("||=", 1, DOREQ)
				else
					OPERATOR("||", 0, DOR)
			} 
			else if (*c->ptr == '=')
				OPERATOR("|=", 1, OREQ)
			else
				OPERATOR("|", 0, OR)
		}
		case '^':
		{
			if (*c->ptr == '^')
			{
				c->ptr++;
				if (*c->ptr == '=')
					OPERATOR("^^=", 1, DXOREQ)
				else
					OPERATOR("^^", 0, DXOR)
			}
			else if (*c->ptr == '=')
				OPERATOR("^=", 1, XOREQ)
			else
				OPERATOR("^", 0, XOR)
		}
		case '#':
		{
			check_implied_arg(c);
			if (*c->ptr == '#') 
			{
				c->ptr++;
				if (*c->ptr == '=')
					OPERATOR("##=", 1, STRCATEQ)
				else
					OPERATOR("##", 0, STRCAT)
			}
			else if (*c->ptr == '=') 
				OPERATOR("#=", 1, STRCATEQ)
			else if (*c->ptr == '~')
				OPERATOR("#~", 1, STRPREEQ)
			else if (c->operand)
			{
				dummy = 2;
				UNARY("#", 0, WORDC)
			}
			else
				OPERATOR("#", 0, STRCAT)
		}

		case '@':
			dummy = 2;
			UNARY("@", 0, STRLEN)

		case '<':
		{
			if (*c->ptr == '<') 
			{
				c->ptr++;
				if (*c->ptr == '=')
					OPERATOR("<<=", 1, SHLEFTEQ)
				else
					OPERATOR("<<", 0, SHLEFT)
			}
			else if (*c->ptr == '=')
			{
				c->ptr++;
				if (*c->ptr == '>')
					OPERATOR("<=>", 1, SWAP)
				else
					OPERATOR("<=", 0, LEQ)
			}
			else
				OPERATOR("<", 0, LES)
		}
		case '>':
		{
			if (*c->ptr == '>') 
			{
				c->ptr++;
				if (*c->ptr == '=')
					OPERATOR(">>=", 1, SHRIGHTEQ)
				else
					OPERATOR(">>", 0, SHRIGHT)
			} 
			else if (*c->ptr == '=') 
				OPERATOR(">=", 1, GEQ)
			else
				OPERATOR(">", 0, GRE)
		}

		case '=':
			if (*c->ptr == '=') 
				OPERATOR("==", 1, DEQ)
			else if (*c->ptr == '~')
				OPERATOR("=~", 1, MATCH)
			else
				OPERATOR("=", 0, EQ)

		case '?':
			check_implied_arg(c);
			c->operand = 1;
			return QUEST;

		case ':':
			/*
			 * I dont want to hear anything from you anti-goto
			 * bigots out there. ;-)  If you can't figure out
			 * what this does, you ought to give up programming.
			 * And a big old :p to everyone who insisted that
			 * i support this horrid hack.
			 */
			if (c->operand)
				goto handle_expando;

			c->operand = 1;
			return COLON;

		case ',':
			/* Same song, second verse. */
			if (c->operand)
				goto handle_expando;

			c->operand = 1;
			return COMMA;

		case '\0':
			check_implied_arg(c);
			c->operand = 1;
			c->ptr--;
			return EOI;

		/*
		 * The {...} operator is really a hack that is left over
		 * from the old math parser.  The support for it here is
		 * a hack.  The entire thing is a hack.
		 */
		case '{':
		{
			char *p = c->ptr;
			char oc = 0;
			ssize_t	span;

			if (!c->operand)
				return lexerr(c, "Misplaced { token");

			if ((span = MatchingBracket(p, '{', '}')) >= 0)
			{
				c->ptr = p + span;
				oc = *c->ptr;
				*c->ptr = 0;
			}
			else
				c->ptr = endstr(c->ptr);

			c->last_token = 0;
			if (!c->noeval)
			{
				char *	result;

				result = call_lambda_function(NULL, p, c->args);
				c->last_token = tokenize_expanded(c, result);
				new_free(&result);
			}

			if (oc)
				*c->ptr++ = oc;
			c->operand = 0;
			return ID;
		}

		/******************** OPERAND TYPES **********************/
		/*
		 * This is an UNEXPANDED-STRING operand type.
		 * Extract everything inside the [...]'s, and then
		 * tokenize that as a "raw" token.  It will be expanded
		 * on an as-needed basis.
		 *
		 * If we are in the no-eval section of a short-circuit, 
		 * then we throw away this token entirely.
		 */
		case '[':
		{
			char *p = c->ptr;
			char oc = 0;
			ssize_t	span;

			if (!c->operand)
				return lexerr(c, "Misplaced [ token");

			if ((span = MatchingBracket(p, '[', ']')) >= 0)
			{
				c->ptr = p + span;
				oc = *c->ptr;
				*c->ptr = 0;
			}
			else
				c->ptr = endstr(c->ptr);

			if (c->noeval)
				c->last_token = 0;
			else
				c->last_token = tokenize_raw(c, p);

			if (oc)
				*c->ptr++ = oc;
			c->operand = 0;
			return ID;
		}
		case ' ':
		case '\t':
		case '\n':
			start++;
			break;

		/*
		 * This is a NUMBER operand type.  This may seem unusual,
		 * but we actually tokenize the string representation of
		 * the number as an "expanded" token type.  Why do we do
		 * this?  Because in the most common case, the user is
		 * doing something like:
		 *
		 *		@ var = 0
		 *
		 * Now we already have the "0" in string format (natch),
		 * and we will never actually reference the 0 as a number
		 * per se -- all assignments are done on strings, so we
		 * need the string anyhow.  So when we lex a number in the
		 * expression, we tokenize it as an 'expanded string' and
		 * then if we ever have to actually use the number in any 
		 * particular context, it will be converted as-needed.
		 */
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
		{
			char 	*end;
			char 	endc;

			c->operand = 0;
			c->ptr--;
			strtod(c->ptr, &end);
			endc = *end;
			*end = 0;

			if (c->noeval)
				c->last_token = 0;
			else
				c->last_token = tokenize_expanded(c, c->ptr);

			*end = endc;
			c->ptr = end;
			return ID;
		}

		/*
		 * Handle those weirdo $-values
		 */
		case '$':
			continue;

		/*
		 * This is an LVAL operand type.  This also may seem 
		 * unusual, but lval's may contain $'s, which need to
		 * be expanded.  They may contain function calls, which
		 * must only be expanded *once*.  When we lex out an lval
		 * (such as "var" in "@ var = 5"), we tokenize the lval
		 * as an unexpanded lval.  Then, if we ever actually need
		 * to reference the proper variable name, we will get the
		 * "raw" value, which will be the lval after passed through
		 * expand_alias().  If we want to get the *value of the
		 * variable $lval), then we will get the "expanded" value,
		 * which will use the "raw" value to do the variable name
		 * lookup.  See?  It's really pretty straightforward.  The
		 * reason we do all this is to make sure that the expansion
		 * of the variable name happens *at most once*, and that if
		 * the variable is not actually referenced, then the expansion
		 * isn't done at all.
		 */
		default:
handle_expando:
		{
			char 	*end;
			char	endc;

			c->operand = 0;
			c->ptr--;
			if ((end = after_expando_special(c)))
			{
				endc = *end;
				*end = 0;

				/*
				 * If we are in the short-circuit of a noeval,
				 * then we throw the token away.
				 */
				if (c->noeval)
					c->last_token = 0;
				else
					c->last_token = tokenize_lval(c, start);

				*end = endc;
				c->ptr = end;
			}
			else
			{
				c->last_token = 0; /* Empty token */
				c->ptr = endstr(c->ptr);
			}

			if (x_debug & DEBUG_NEW_MATH_DEBUG)
				yell("After token: [%s]", c->ptr);
			return ID;
		}
	    }
	}
}

/******************************* STATE MACHINE *****************************/
/*
 * mathparse -- this is the state machine that actually parses the
 * expression.   The parsing is done through a shift-reduce mechanism,
 * and all the precedence levels lower than 'pc' are evaluated.
 */
static void	mathparse (expr_info *c, int pc)
{
	int	otok, 
		onoeval;

	/*
	 * Drop out of parsing if an error has occured
	 */
	if (c->errflag)
		return;

	/*
	 * Get the next token in the expression
	 */
	c->mtok = zzlex(c);

	/*
	 * For as long as the next operator indicates a shift operation...
	 */
	while (prec[c->mtok] <= pc) 
	{
	    /* Drop out if an error has occured */
	    if (c->errflag)
		return;

	    /*
	     * Figure out what to do with this token that needs
	     * to be shifted.
	     */
	    switch (c->mtok) 
	    {
		/*
		 * This is any kind of an indentifier.  There are several
		 * that we handle:
		 *
		 *	VARIABLE REFERENCE	ie, "foo"   in "@ foo = 2"
		 *	NUMBER			ie, "2"     in "@ foo = 2"
		 *	UNEXPANDED STRING	ie, "[boo]" in "@ foo = [boo]"
		 *
		 * The actual determination of which type is done in the lexer.
		 * We just get a token id for the resulting identifier.
		 * Getting the value is done on an as-needed basis.
		 */
		case ID:
			if (x_debug & DEBUG_NEW_MATH_DEBUG)
				yell("Parsed identifier token [%s]", 
					get_token_expanded(c, c->last_token));

			/* 
			 * The lexer sets the last token to
			 * 0 if noeval is set.  This saves us
			 * from having to tokenize a string
			 * that we expressly will not use.
			 */
			push_token(c, c->last_token);
			break;

		/*
		 * An open-parenthesis indicates that we should
		 * recursively evaluate the inside of the paren-set.
		 */
		case M_INPAR:
		{
			if (x_debug & DEBUG_NEW_MATH_DEBUG)
				yell("Parsed open paren");
			mathparse(c, TOPPREC);

			/*
			 * Of course if the expression ends without
			 * a matching rparen, then we whine about it.
			 */
			if (c->mtok != M_OUTPAR) 
			{
				if (!c->errflag)
				    error("')' expected");
				return;
			}
			break;
		}

		/*
		 * A question mark requires that we check for short
		 * circuiting.  We check the lhs, and if it is true,
		 * then we evaluate the lhs of the colon.  If it is
		 * false then we just parse the lhs of the colon and
		 * evaluate the rhs of the colon.
		 */
		case QUEST:
		{
			BooL u = pop_boolean(c);

			push_boolean(c, u);
			if (!u)
				c->noeval++;
			mathparse(c, prec[QUEST] - 1);
			if (!u)
				c->noeval--;
			else
				c->noeval++;
			mathparse(c, prec[QUEST]);
			if (u)
				c->noeval--;
			reduce(c, QUEST);

			continue;
		}

		/*
		 * All other operators handle normally
		 */
		default:
		{
			/* Save state */
			otok = c->mtok;
			onoeval = c->noeval;

			/*
			 * Check for short circuiting.
			 */
			if (assoc[otok] == BOOL)
			{
			    if (x_debug & DEBUG_NEW_MATH_DEBUG)
				yell("Parsed short circuit operator");

			    switch (otok)
			    {
				case DAND:
				case DANDEQ:
				{
					BooL u = pop_boolean(c);
					push_boolean(c, u);
					if (!u)
						c->noeval++;
					break;
				}
				case DOR:
				case DOREQ:
				{
					BooL u = pop_boolean(c);
					push_boolean(c, u);
					if (u)
						c->noeval++;
					break;
				}
			    }
			}

		 	if (x_debug & DEBUG_NEW_MATH_DEBUG)
			    yell("Parsed operator of type [%d]", otok);

			/*
			 * Parse the right hand side through
			 * recursion if we're doing things R->L.
			 */
			mathparse(c, prec[otok] - (assoc[otok] != RL));

			/*
			 * Then reduce this operation.
			 */
			c->noeval = onoeval;
			reduce(c, otok);
			continue;
		}
	    }

	    /*
	     * Grab the next token
	     */
	    c->mtok = zzlex(c);
	}
}

/******************************** HARNASS **********************************/
/*
 * This is the new math parser.  It sets up an execution context, which
 * contains sundry information like all the extracted tokens, intermediate
 * tokens, shifted tokens, and the like.  The expression context is passed
 * around from function to function, each function is totaly independant
 * of state information stored in global variables.  Therefore, this math
 * parser is re-entrant safe.
 */
static char *	matheval (char *s, const char *args)
{
	expr_info	context;
	char *		ret = NULL;

	/* Sanity check */
	if (!s || !*s)
		return malloc_strdup(empty_string);

	/* Create new state */
	setup_expr_info(&context);
	context.ptr = s;
	context.args = args;

	/* Actually do the parsing */
	mathparse(&context, TOPPREC);

	/* Check for error */
	if (context.errflag)
	{
		ret = malloc_strdup(empty_string);
		goto cleanup;
	}

	/* Check for leftover operands */
	if (context.sp)
		error("The expression has too many operands");

	if (x_debug & DEBUG_NEW_MATH_DEBUG)
	{
		int i;
		yell("Terms left: %d", context.sp);
		for (i = 0; i <= context.sp; i++)
			yell("Term [%d]: [%s]", i, 
				get_token_expanded(&context, context.stack[i]));
	}

	/* Get the return value, if requested */
	ret = malloc_strdup(get_token_expanded(&context, pop_token(&context)));

cleanup:
	/* Clean up and restore order */
	destroy_expr_info(&context);

	if (x_debug & DEBUG_NEW_MATH_DEBUG)
		yell("Returning [%s]", ret);

	/* Return the result */
	return ret;
}


/******************************* SUPPORT *************************************/
/*
 * after_expando_special: This is a special version of after_expando that
 * can handle parsing out lvalues in expressions.  Due to the eclectic nature
 * of lvalues in expressions, this is quite a bit different than the normal
 * after_expando, requiring a different function. Ugh.
 *
 * This replaces some much more complicated logic strewn
 * here and there that attempted to figure out just how long an expando 
 * name was supposed to be.  Well, now this changes that.  This will slurp
 * up everything in 'start' that could possibly be put after a $ that could
 * result in a syntactically valid expando.  All you need to do is tell it
 * if the expando is an rvalue or an lvalue (it *does* make a difference)
 */
static  char *	after_expando_special (expr_info *c)
{
	char	*start;
	char	*rest;
	int	call;

	if (!(start = c->ptr))
		return c->ptr;

	for (;;)
	{
		rest = after_expando(start, 0, &call);
		if (*rest != '$')
			break;
		start = rest + 1;
	}

	if (c->ptr == rest)
	{
		yell("Erf.  I'm trying to find an lval at [%s] and I'm not "
			"having much luck finding one.  Punting the rest of "
			"this expression", c->ptr);
		return NULL;
	}

	/*
	 * All done!
	 */
	return rest;
}

