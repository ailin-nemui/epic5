/*
 * expr.c -- The expression mode parser and the textual mode parser
 * #included by alias.c -- DO NOT DELETE
 *
 * Copyright 1990 Michael Sandrof
 * Copyright 1997 EPIC Software Labs
 * See the COPYRIGHT file for more info
 */

#include <math.h>

/* Function decls */
static	void	TruncateAndQuote(char **, const char *, int, const char *);
static	char	*alias_special_char(char **, char *, const char *, char *, int *);
static	void	do_alias_string (char *, char *);

char *alias_string = NULL;

/************************** EXPRESSION MODE PARSER ***********************/
/* canon_number: canonicalizes number to something relevant */
/* If FLOATING_POINT_MATH isnt set, it truncates it to an integer */
static char *canon_number (char *input)
{
	int end = strlen(input);

	if (end)
		end--;
	else
		return input;		/* nothing to do */

	if (get_int_var(FLOATING_POINT_MATH_VAR))
	{
		/* remove any trailing zeros */
		while (input[end] == '0')
			end--;

		/* If we removed all the zeros and all that is
		   left is the decimal point, remove it too */
		if (input[end] == '.')
			end--;

		input[end+1] = 0;
	}
	else
	{
		char *dot = strchr(input, '.');
		if (dot)
			*dot = 0;
	}

	return input;
}


/* Given a pointer to an operator, find the last operator in the string */
static char	*lastop (char *ptr)
{
	/* dont ask why i put the space in there. */
	while (ptr[1] && strchr("!=<>&^|#+/%,-* ", ptr[1]))
		ptr++;
	return ptr;
}




#define	NU_EXPR	0
#define	NU_ASSN	1
#define NU_TERT 2
#define NU_CONJ 3
#define NU_BITW 4 
#define NU_COMP 5 
#define NU_ADD  6 
#define NU_MULT 7 
#define NU_UNIT 8

/*
 * Cleaned up/documented by Jeremy Nelson, Feb 1996.
 *
 * What types of operators does ircII (EPIC) support?
 *
 * The client handles as 'special cases" the () and []
 * operators which have ircII-specific meanings.
 *
 * The general-purpose operators are:
 *
 * additive class:  		+, -, ++, --, +=, -=
 * multiplicative class: 	*, /, %, *=, /=, %=
 * string class: 		##, #=
 * and-class:			&, &&, &=
 * or-class:			|, ||, |=
 * xor-class:			^, ^^, ^=
 * equality-class:		==, !=
 * relative-class:		<, <=, >, >=
 * negation-class:		~ (1s comp), ! (2s comp)
 * selector-class:		?: (tertiary operator)
 * list-class:			, (comma)
 *
 * Backslash quotes any character not within [] or (),
 * which have their own quoting rules (*sigh*)
 */ 
static	char	*next_unit (char *str, const char *args, int *arg_flag, int stage)
#if 0
	char	*str,			/* start of token */
		*args;			/* the values of $0, $1, etc */
	int	*arg_flag,		/* whether we use $0, $1, etc */
		stage;			/* What parsing level were at */
#endif
{
	char	*ptr,			/* pointer to the current op */
		*ptr2,			/* used to point matching brackets */
		*right,			/* used to denote end of bracket-set */
		*lastc,			/* used to denote end of token-set */
		*tmp = NULL,		/* place to sprintf() into */
		op;			/* the op were working on */
	int	got_sloshed = 0,	/* If the last char was a slash */
		display;

	char	*result1 = (char *) 0,	/* raw lefthand-side of operator */
		*result2 = (char *) 0,	/* raw righthand-side of operator */
		*varname = (char *) 0;	/* Where we store varnames */
	long	value1 = 0,		/* integer value of lhs */
		value2 = 0,		/* integer value of rhs */
		value3 = 0;		/* integer value of operation */
	double	dvalue1 = 0.0,		/* floating value of lhs */
		dvalue2 = 0.0,		/* floating value of rhs */
		dvalue3 = 0.0;		/* floating value of operation */


/*
 * These macros make my life so much easier, its not funny.
 */

/*
 * An implied operation is one where the left-hand argument is
 * "implied" because it is both an lvalue and an rvalue.  We use
 * the rvalue of the left argument when we do the actual operation
 * then we do an assignment to the lvalue of the left argument.
 */
#define SETUP_IMPLIED(var1, var2, func)					\
{									\
	/* Save the type of op, skip over the X=. */			\
	op = *ptr;							\
	*ptr++ = '\0';							\
	ptr++;								\
									\
	/* Figure out the variable name were working on */		\
	varname = expand_alias(str, args, arg_flag, NULL);		\
	lastc = varname + strlen(varname) - 1;				\
	while (lastc > varname && *lastc == ' ')			\
		*lastc-- = '\0';					\
	while (my_isspace(*varname))					\
		 varname++;						\
									\
	/* Get the value of the implied argument */			\
	result1 = get_variable(varname);				\
	var1 = (result1 && *result1) ? func (result1) : 0;		\
	new_free(&result1);						\
									\
	/* Get the value of the explicit argument */			\
	result2 = next_unit(ptr, args, arg_flag, stage);		\
	var2 = func (result2);						\
	new_free(&result2);						\
}

/*
 * This make sure the calculated value gets back into the lvalue of
 * the left operator, and turns the display back on.
 */
#define CLEANUP_IMPLIED()						\
{									\
	/* Make sure were really an implied case */			\
	if (ptr[-1] == '=' && stage == NU_ASSN)				\
	{								\
		/* Make sure theres an lvalue */			\
		if (*varname)						\
			add_var_alias(varname, tmp, 0);			\
		else							\
			yell("Invalid assignment: No lvalue");		\
	}								\
	new_free(&varname);						\
}


/*
 * This sets up an ordinary explicit binary operation, where both
 * arguments are used as rvalues.  We just recurse and get their
 * values and then do the operation on them.
 */
#define SETUP_BINARY(var1, var2, func)					\
{									\
	/* Save the op were working on cause we clobber it. */		\
	op = *ptr;							\
	*ptr++ = '\0';							\
									\
	/* Get the two explicit operands */				\
	result1 = next_unit(str, args, arg_flag, stage);		\
	result2 = next_unit(ptr, args, arg_flag, stage);		\
									\
	/* Convert them with the specified function */			\
	var1 = func (result1);						\
	var2 = func (result2);						\
									\
	/* Clean up the mess we created. */				\
	new_free(&result1);						\
	new_free(&result2);						\
}


/*
 * This sets up a type-independant section of code for doing an
 * operation when the X and X= forms are both valid.
 */
#define SETUP(var1, var2, func, STAGE)					\
{									\
	/* If its an X= op, do an implied operation */			\
	if (ptr[1] == '=' && stage == NU_ASSN)				\
		SETUP_IMPLIED(var1, var2, func)				\
									\
	/* Else if its just an X op, do a binary operation */		\
	else if (ptr[1] != '=' && stage == STAGE)			\
		SETUP_BINARY(var1, var2, func)				\
									\
	/* Its not our turn to do this operation, just punt. */		\
	else								\
	{								\
		ptr = lastop(ptr);					\
		break;							\
	}								\
}

/* This does a setup for a floating-point operation. */
#define SETUP_FLOAT_OPERATION(STAGE)					\
	SETUP(dvalue1, dvalue2, atof, STAGE)

/* This does a setup for an integer operation. */
#define SETUP_INTEGER_OPERATION(STAGE)					\
	SETUP(value1, value2, my_atol, STAGE)

	/* remove leading spaces */
	while (my_isspace(*str))
		++str;

	/* If there's nothing there, return it */
	if (!*str)
		return m_strdup(empty_string);


	/* find the end of the rest of the expression */
	if ((lastc = str+strlen(str)) > str)
		lastc--;

	/* and remove trailing spaces */
	while (my_isspace(*lastc))
		*lastc-- = '\0';

	/* Must have arguments.  Fill in if not supplied */
	if (!args)
		args = empty_string;

	/* 
	 * If we're in the last parsing level, and this token is in parens,
	 * strip the parens and parse the insides immediately.
	 */
	if (stage == NU_UNIT && *lastc == ')' && *str == '(')
	{
		str++, *lastc-- = '\0';
		return next_unit(str, args, arg_flag, NU_EXPR);
	}


	/* 
	 * Ok. now lets get some work done.
	 * 
	 * Starting at the beginning of the string, look for something
	 * resembling an operator.  This divides the expression into two
	 * parts, a lhs and an rhs.  The rhs starts at "str", and the
	 * operator is at "ptr".  So if you do ptr = lastop(ptr), youll
	 * end up at the beginning of the rhs, which is the rest of the
	 * expression.  You can then parse it at the same level as the
	 * current level and it will recursively trickle back an rvalue
	 * which you can then apply to the lvalue give the operator.
	 *
	 * PROS: This is a simplistic setup and not (terribly) confusing.
	 * CONS: Every operator is evaluated right-to-left which is *WRONG*.
	 */

	for (ptr = str; *ptr; ptr++)
	{
		if (got_sloshed)
		{
			got_sloshed = 0;
			continue;
		}

		switch(*ptr)
		{
		case '\\':
		{
			got_sloshed = 1;
			continue;
		}

		/*
		 * Parentheses have two contexts:  
		 * 1) (text) is a unary precedence operator.  It is nonassoc,
		 *	and simply parses the insides immediately.
		 * 2) text(text) is the function operator.  It calls the
		 *	specified function/alias passing it the given args.
		 */
		case '(':
		{
			int	savec = 0;

			/*
			 * If we're not in NU_UNIT, then we have a paren-set
			 * that (probably) is still an left-operand for some
			 * binary op.  Anyhow, we just immediately parse the
			 * paren-set, as thats the general idea of parens.
			 */
			if (stage != NU_UNIT || ptr == str)
			{
				/* 
				 * If there is no matching ), gobble up the
				 * entire expression.
				 */
				if (!(ptr2 = MatchingBracket(ptr+1, '(', ')')))
					ptr = ptr + strlen(ptr) - 1;
				else
					ptr = ptr2;
				break;
			}

			/*
			 * and if that token is a left-paren, then we have a
			 * function call.  We gobble up the arguments and
			 * ship it all off to call_function.
			 */
			if ((ptr2 = MatchingBracket(ptr + 1, '(', ')')))
			{
				ptr2++;
				savec = *ptr2;
				*ptr2 = 0;
			}

			result1 = call_function(str, args, arg_flag);

			if (savec)
				*ptr2 = savec;

			/*
			 * and what do we do with this value?  Why we prepend
			 * it to the next token!  This is actually a hack
			 * that if you have a NON-operator as the next token,
			 * it has an interesting side effect:
			 * ie:
			 * 	/eval echo ${foobar()4 + 3}
			 * where
			 *	alias foobar {@ function_return = 2}
			 *
			 * you get '27' as a return value, "as-if" you had done
			 *
			 *	/eval echo ${foobar() ## 4 + 3}
			 *
			 * Dont depend on this behavior.
			 */
			if (ptr && *ptr)
			{
				malloc_strcat(&result1, ptr);
				result2 = next_unit(result1, args, arg_flag, stage);
				new_free(&result1);
				result1 = result2;
			}

			return result1;
		}

		/*
		 * Braces are used for anonymous functions:
		 * @ condition : {some code} : {some more code}
		 *
		 * Dont yell at me if you dont think its useful.  Im just
		 * supporting it because it makes sense.  And it saves you
		 * from having to declare aliases to do the parts.
		 */
		case '{':
		{
			display = window_display;

			ptr2 = MatchingBracket(ptr + 1, LEFT_BRACE, RIGHT_BRACE);
			if (!ptr2)
				ptr2 = ptr + strlen(ptr) - 1;

			if (stage != NU_UNIT)
			{
				ptr = ptr2;
				break;
			}

			*ptr2++ = 0;
			*ptr++ = 0;

			/* Taken more or less from call_user_function */
			make_local_stack(NULL);
			window_display = 0;
			add_local_alias("FUNCTION_RETURN", empty_string, 0);
			window_display = display;

			will_catch_return_exceptions++;
			parse_line(NULL, ptr, args, 0, 1);
			will_catch_return_exceptions--;
			return_exception = 0;

			result1 = get_variable("FUNCTION_RETURN");
			destroy_local_stack();
			if (!result1)
				result1 = m_strdup(empty_string);

			return result1;
		}

		/*
		 * Brackets have two contexts:
		 * [text] is the literal-text operator.  The contents are
		 * 	not parsed as an lvalue, but as literal text.
		 *      This also covers the case of the array operator,
		 *      since it just appends whats in the [] set with what
		 *      came before it.
		 *
		 * The literal text operator applies not only to entire
		 * tokens, but also to the insides of array qualifiers.
		 */
		case '[':
		{
			if (stage != NU_UNIT)
			{
				if (!(ptr2 = MatchingBracket(ptr+1, LEFT_BRACKET, RIGHT_BRACKET)))
					ptr = ptr+strlen(ptr)-1;
				else
					ptr = ptr2;
				break;
			}

			/* ptr points right after the [] set */
			*ptr++ = 0;
			right = ptr;
	
			/*
			 * At this point, we check to see if it really is a
			 * '[', and if it is, we skip over it.
			 */
			if ((ptr = MatchingBracket(right, 
					LEFT_BRACKET, RIGHT_BRACKET)))
				*ptr++ = '\0';

#ifndef NO_CHEATING
			/*
			 * Simple heuristic... If its $<num> or
			 * $-<num>, handle it here.  Otherwise, if its
			 * got no $'s, its a literal, otherwise, do the
			 * normal thing.  Optimize $*, too.
			 */
			if (*right == '$')
			{
			    if (right[1] == '*' && right[2] == 0)
			    {
				result1 = m_strdup(args);
				*arg_flag = 1;
			    }
			    else
			    {
				char *end = NULL;
				long j;

				j = strtol(right + 1, &end, 10);
				if (end && !*end)
				{
					if (j < 0)
						result1 = extractw2(args, SOS, -j);
					else
						result1 = extractw2(args, j, j);

					/* 
					 * Oops. this would have been set by
					 * expand alias, so we have to do it
					 * explicitly here!
					 */
					*arg_flag = 1;
				}

				/*
				 * Gracefully handle $X- here, too.
				 */
				else if (*end == '-' && !end[1])
				{
					result1 = extractw2(args, j, EOS);
					*arg_flag = 1;
				}
				else
					result1 = expand_alias(right, args, arg_flag, NULL);
			    }
			}
			else if (!strchr(right, '$') && !strchr(right, '\\'))
				result1 = m_strdup(right);
			else
#endif
				result1 = expand_alias(right, args, arg_flag, NULL);

			/*
			 * You need to look closely at this, as this test 
			 * is actually testing to see if (ptr != str) at the
			 * top of this case, which would imply that the []
			 * set was an array qualifier to some other variable.
			 *
			 * Before you ask "how do you know that?"  Remember
			 * that if (ptr == str) at the beginning of the case,
			 * then when we  *ptr++ = 0, we would then be setting
			 * *str to 0; so testing to see if *str is not zero
			 * tells us if (ptr == str) was true or not...
			 */
			if (*str)
			{
				/* array qualifier */
				size_t	size = strlen(str) + 
						strlen(result1) + 
						(ptr ? strlen(ptr) : 0) + 2;

				/* Obviously, these are safe. */
				result2 = alloca(size);
				strcpy(result2, str);
				strcat(result2, ".");
				strcat(result2, result1);
				new_free(&result1);

				/*
				 * Notice of unexpected behavior:
				 *
				 * If $foobar.onetwo is "999"
				 * then ${foobar[one]two + 3} is "1002"
				 * Dont depend on this behavior.
				 */
				if (ptr && *ptr)
				{
					/* Obviously, this is safe. */
					strcat(result2, ptr);
					result1 = next_unit(result2, args, arg_flag, stage);
				}
				else
				{
					if (!(result1 = get_variable(result2)))
						malloc_strcpy(&result1, empty_string);
				}
			}

			/*
			 * Notice of unexpected behavior:
			 *
			 * If $onetwo is "testing",
			 * /eval echo ${[one]two} returns "testing".
			 * Dont depend on this behavior.
			 */ 
			else if (ptr && *ptr)
			{
				malloc_strcat(&result1, ptr);
				result2 = next_unit(result1, args, arg_flag, stage);
				new_free(&result1);
				result1 = result2;
			}

			/*
			 * result1 shouldnt ever be pointing at an empty
			 * string here, but if it is, we just malloc_strcpy
			 * a new empty_string into it.  This fixes an icky
			 * memory hog bug my making sure that a (long) string
			 * with a leading null gets replaced by a (small) 
			 * string of size one.  Capish?
			 */
			if (!*result1)
				malloc_strcpy(&result1, empty_string);

			return result1;
		}

		/*
		 * The addition and subtraction operators have four contexts:
		 * 1) + is a binary additive operator if there is an rvalue
		 *	as the token to the left (ignored)
		 * 2) + is a unary magnitudinal operator if there is no 
		 *	rvalue to the left.
		 * 3) ++text or text++ is a unary pre/post in/decrement
		 *	operator.
		 * 4) += is the binary implied additive operator.
		 */
		case '-':
		case '+':
		{
			if (ptr[1] == ptr[0])
			{
				int prefix;
				long r;

				if (stage != NU_UNIT)
				{
					/* 
					 * only one 'ptr++' because a 2nd
					 * one is done at the top of the
					 * loop after the 'break'.
					 */
					ptr++;
					break;
				}

				if (ptr == str)		/* prefix */
					prefix = 1, ptr2 = ptr + 2;
				else			/* postfix */
					prefix = 0, ptr2 = str, *ptr++ = 0;

				varname = expand_alias(ptr2, args, arg_flag, NULL);
				upper(varname);

				if (!(result1 = get_variable(varname)))
					malloc_strcpy(&result1, zero);

				r = my_atol(result1);
				if (*ptr == '+')
					r++;
				else
					r--;

				add_var_alias(varname, ltoa(r), 0);

				if (!prefix)
					r--;

				new_free(&result1);
				new_free(&varname);
				return m_strdup(ltoa(r));
			}

			/* Unary op is ignored */
			else if (ptr == str)
				break;


#ifdef FLOATING_POINT_SUPPORT
			SETUP_FLOAT_OPERATION(NU_ADD)

			if (op == '-')
				dvalue3 = dvalue1 - dvalue2;
			else
				dvalue3 = dvalue1 + dvalue2;

			tmp = m_sprintf("%.999g", dvalue3);
			canon_number(tmp);
#else
			SETUP_INTEGER_OPERATION(NU_ADD)

			if (op == '-')
				value3 = value1 - value2;
			else
				value3 = value1 + value2;

			tmp = m_strdup(ltoa(value3));
#endif
			CLEANUP_IMPLIED()
			return tmp;
		}


		/*
		 * The Multiplication operators have two contexts:
		 * 1) * is a binary multiplicative op
		 * 2) *= is the implied binary multiplicative op
		 */
		case '/':
		case '*':
		case '%':
		{
			/* Unary op is ignored */
			if (ptr == str)
				break;

			/* default value on error */
			dvalue3 = 0.0;

			/*
			 * XXXX This is an awful hack to support
			 * the ** (pow) operator.  Sorry.
			 */
			if (ptr[0] == '*' && ptr[1] == '*' && stage == NU_MULT)
			{
				*ptr++ = '\0';
				SETUP_BINARY(dvalue1, dvalue2, atof)
				return m_sprintf("%.999g", pow(dvalue1, dvalue2));
			}

			SETUP_FLOAT_OPERATION(NU_MULT)

			if (op == '*')
				dvalue3 = dvalue1 * dvalue2;
			else 
			{
				if (dvalue2 == 0.0)
					yell("Division by zero!");

				else if (op == '/')
					dvalue3 = dvalue1 / dvalue2;
				else
					dvalue3 = (int)dvalue1 % (int)dvalue2;
			}

			tmp = m_sprintf("%.999g", dvalue3);
			canon_number(tmp);
			CLEANUP_IMPLIED()
			return tmp;
		}


		/*
		 * The # operator has three contexts:
		 * 1) # or ## is a binary string catenation operator
		 * 2) #= is an implied string catenation operator
		 * 3) #~ is an implied string prepend operator
		 */
		case '#':
		{
			if (ptr[1] == '#' && stage == NU_ADD)
			{
				*ptr++ = '\0';
				ptr++;
				result1 = next_unit(str, args, arg_flag, stage);
				result2 = next_unit(ptr, args, arg_flag, stage);
				malloc_strcat(&result1, result2);
				new_free(&result2);
				return result1;
			}

			else if (ptr[1] == '=' && stage == NU_ASSN)
			{
				char *sval1, *sval2;

				SETUP_IMPLIED(sval1, sval2, m_strdup)
				malloc_strcat(&sval1, sval2);
				new_free(&sval2);
				tmp = sval1;
				CLEANUP_IMPLIED()
				return sval1;
			}

			else if (ptr[1] == '~' && stage == NU_ASSN)
			{
				char *sval1, *sval2;

				ptr[1] = '=';	/* XXXX */
				SETUP_IMPLIED(sval1, sval2, m_strdup)
				malloc_strcat(&sval2, sval1);
				new_free(&sval1);
				tmp = sval2;
				CLEANUP_IMPLIED()
				return sval2;
			}

			else
			{
				ptr = lastop(ptr);
				break;
			}
		}


	/* 
	 * Reworked - Jeremy Nelson, Feb 1994
	 * Reworked again, Feb 1996 (jfn)
	 * 
	 * X, XX, and X= are all supported, where X is one of "&" (and), 
	 * "|" (or) and "^" (xor).  The XX forms short-circuit, as they
	 * do in C and perl.  X and X= forms are bitwise, XX is logical.
	 */
		case '&':
		{
			/* && is binary short-circuit logical and */
			if (ptr[0] == ptr[1] && stage == NU_CONJ)
			{
				*ptr++ = '\0';
				ptr++;

				result1 = next_unit(str, args, arg_flag, stage);
				if (check_val(result1))
				{
					result2 = next_unit(ptr, args, arg_flag, stage);
					value3 = check_val(result2);
				}
				else
					value3 = 0;

				new_free(&result1);
				new_free(&result2);
				return m_strdup(value3 ? one : zero);
			}

			/* &= is implied binary bitwise and */
			else if (ptr[1] == '=' && stage == NU_ASSN)
			{
				SETUP_IMPLIED(value1, value2, my_atol)
				value1 &= value2;
				tmp = m_strdup(ltoa(value1));
				CLEANUP_IMPLIED();
				return tmp;
			}

			/* & is binary bitwise and */
			else if (ptr[1] != ptr[0] && ptr[1] != '=' && stage == NU_BITW)
			{
				SETUP_BINARY(value1, value2, my_atol)
				return m_strdup(ltoa(value1 & value2));
			}

			else
			{
				ptr = lastop(ptr);
				break;
			}
		}


		case '|':
		{
			/* || is binary short-circuiting logical or */
			if (ptr[0] == ptr[1] && stage == NU_CONJ)
			{
				*ptr++ = '\0';
				ptr++;

				result1 = next_unit(str, args, arg_flag, stage);
				if (!check_val(result1))
				{
					result2 = next_unit(ptr, args, arg_flag, stage);
					value3 = check_val(result2);
				}
				else
					value3 = 1;

				new_free(&result1);
				new_free(&result2);
				return m_strdup(value3 ? one : zero);
			}

			/* |= is implied binary bitwise or */
			else if (ptr[1] == '=' && stage == NU_ASSN)
			{
				SETUP_IMPLIED(value1, value2, my_atol)
				value1 |= value2;
				tmp = m_strdup(ltoa(value1));
				CLEANUP_IMPLIED();
				return tmp;
			}

			/* | is binary bitwise or */
			else if (ptr[1] != ptr[0] && ptr[1] != '=' && stage != NU_BITW)
			{
				SETUP_BINARY(value1, value2, my_atol)
				return m_strdup(ltoa(value1 | value2));
			}

			else
			{
				ptr = lastop(ptr);
				break;
			}
		}

		case '^':
		{
			/* ^^ is binary logical xor */
			if (ptr[0] == ptr[1] && stage == NU_CONJ)
			{
				*ptr++ = '\0';
				ptr++;

				value1 = check_val((result1 = next_unit(str, args, arg_flag, stage)));
				value2 = check_val((result2 = next_unit(ptr, args, arg_flag, stage)));
				new_free(&result1);
				new_free(&result2);

				return m_strdup(value1 ^ value2 ? one : zero);
			}

			/* ^= is implied binary bitwise xor */
			else if (ptr[1] == '=' && stage == NU_ASSN)  /* ^= op */
			{

				SETUP_IMPLIED(value1, value2, my_atol)
				value1 ^= value2;
				tmp = m_strdup(ltoa(value1));
				CLEANUP_IMPLIED();
				return tmp;
			}

			/* ^ is binary bitwise xor */
			else if (ptr[1] != ptr[0] && ptr[1] != '=' && stage == NU_BITW)
			{
				SETUP_BINARY(value1, value2, my_atol)
				return m_strdup(ltoa(value1 ^ value2));
			}

			else
			{
				ptr = lastop(ptr);
				break;
			}
		}

		/*
		 * ?: is the tertiary operator.  Confusing.
		 */
		case '?':
		{
			if (stage == NU_TERT)
			{
				*ptr++ = '\0';
				result1 = next_unit(str, args, arg_flag, stage);
				ptr2 = MatchingBracket(ptr, '?', ':');

				/* Unbalanced :, or possibly missing */
				if (!ptr2)  /* ? but no :, ignore */
				{
					ptr = lastop(ptr);
					break;
				}
				*ptr2++ = '\0';
				if ( check_val(result1) )
					result2 = next_unit(ptr, args, arg_flag, stage);
				else
					result2 = next_unit(ptr2, args, arg_flag, stage);

				/* XXXX - needed? */
				ptr2[-1] = ':';
				new_free(&result1);
				return result2;
			}

			else
			{
				ptr = lastop(ptr);
				break;
			}
		}

		/*
		 * = is the binary assignment operator
		 * == is the binary equality operator
		 * =~ is the binary matching operator
		 */
		case '=':
		{
			if (ptr[1] == '~' && stage == NU_COMP)
			{
				*ptr++ = 0;
				ptr++;
				result1 = next_unit(str, args, arg_flag, stage);
				result2 = next_unit(ptr, args, arg_flag, stage);
				if (wild_match(result2, result1))
					malloc_strcpy(&result1, one);
				else
					malloc_strcpy(&result1, zero);
				new_free(&result2);
				return result1;
			}

			if (ptr[1] != '=' && ptr[1] != '~' && stage == NU_ASSN)
			{
				*ptr++ = '\0';
				upper(str);
				result1 = expand_alias(str, args, arg_flag, NULL);
				result2 = next_unit(ptr, args, arg_flag, stage);

				lastc = result1 + strlen(result1) - 1;
				while (lastc > result1 && *lastc == ' ')
					*lastc-- = '\0';
				for (varname = result1; my_isspace(*varname);)
					varname++;

				upper(varname);

				if (*varname)
					add_var_alias(varname, result2, 0);
				else
					yell("Invalid assignment: no lvalue");

				new_free(&result1);
				return result2;
			}

			else if (ptr[1] == '=' && stage == NU_COMP)
			{
				*ptr++ = '\0';
				ptr++;
				result1 = next_unit(str, args, arg_flag, stage);
				result2 = next_unit(ptr, args, arg_flag, stage);
				if (!my_stricmp(result1, result2))
					malloc_strcpy(&result1, one);
				else
					malloc_strcpy(&result1, zero);
				new_free(&result2);
				return result1;
			}

			else
			{
				ptr = lastop(ptr);
				break;
			}
		}

		/*
		 * < is the binary relative operator
		 * << is the binary bitwise shift operator
		 */
		case '>':
		case '<':
		{
			if (ptr[1] == ptr[0] && stage == NU_BITW)  /* << or >> op */
			{
				op = *ptr;
				*ptr++ = 0;
				ptr++;

				result1 = next_unit(str, args, arg_flag, stage);
				result2 = next_unit(ptr, args, arg_flag, stage);
				value1 = my_atol(result1);
				value2 = my_atol(result2);
				if (op == '<')
					value3 = value1 << value2;
				else
					value3 = value1 >> value2;

				new_free(&result1);
				new_free(&result2);
				return m_strdup(ltoa(value3));
			}

			else if (ptr[1] != ptr[0] && stage == NU_COMP)
			{
				op = *ptr;
				if (ptr[1] == '=')
					value3 = 1, *ptr++ = '\0';
				else
					value3 = 0;

				*ptr++ = '\0';
				result1 = next_unit(str, args, arg_flag, stage);
				result2 = next_unit(ptr, args, arg_flag, stage);

				if ((my_isdigit(result1)) && (my_isdigit(result2)))
				{
					dvalue1 = atof(result1);
					dvalue2 = atof(result2);
					value1 = (dvalue1 == dvalue2) ? 0 : ((dvalue1 < dvalue2) ? -1 : 1);
				}
				else
					value1 = my_stricmp(result1, result2);

				if (value1)
				{
					value2 = (value1 > 0) ? 1 : 0;
					if (op == '<')
						value2 = 1 - value2;
				}
				else
					value2 = value3;

				new_free(&result1);
				new_free(&result2);
				return m_strdup(ltoa(value2));
			}

			else
			{
				ptr = lastop(ptr);
				break;
			}
		}

		/*
		 * ~ is the 1s complement (bitwise negation) operator
		 */
		case '~':
		{
			if (ptr == str && stage == NU_UNIT)
			{
				result1 = next_unit(str+1, args, arg_flag, stage);
				if (isdigit(*result1))
					value1 = ~my_atol(result1);
				else
					value1 = 0;

				return m_strdup(ltoa(value1));
			}

			else
			{
				ptr = lastop(ptr);
				break;
			}
		}

		/*
		 * ! is the 2s complement (logical negation) operator
		 * != is the inequality operator
		 */
		case '!':
		{
			if (ptr == str && stage == NU_UNIT)
			{
				result1 = next_unit(str+1, args, arg_flag, stage);

				if (my_isdigit(result1))
				{
					value1 = my_atol(result1);
					value2 = value1 ? 0 : 1;
				}
				else
					value2 = ((*result1)?0:1);

				new_free(&result1);
				return m_strdup(ltoa(value2));
			}

			else if (ptr != str && ptr[1] == '~' && stage == NU_COMP)
			{
				*ptr++ = 0;
				ptr++;

				result1 = next_unit(str, args, arg_flag, stage);
				result2 = next_unit(ptr, args, arg_flag, stage);

				if (!wild_match(result2, result1))
					malloc_strcpy(&result1, one);
				else
					malloc_strcpy(&result1, zero);

				new_free(&result2);
				return result1;
			}

			else if (ptr != str && ptr[1] == '=' && stage == NU_COMP)
			{
				*ptr++ = '\0';
				ptr++;

				result1 = next_unit(str, args, arg_flag, stage);
				result2 = next_unit(ptr, args, arg_flag, stage);

				if (!my_stricmp(result1, result2))
					malloc_strcpy(&result1, zero);
				else
					malloc_strcpy(&result1, one);

				new_free(&result2);
				return result1;
			}

			else
			{
				ptr = lastop(ptr);
				break;
			}
		}


		/*
		 * , is the binary right-hand operator
		 */
		case ',': 
		{
			if (stage == NU_EXPR)
			{
				*ptr++ = '\0';
				result1 = next_unit(str, args, arg_flag, stage);
				result2 = next_unit(ptr, args, arg_flag, stage);
				new_free(&result1);
				return result2;
			}

			else
			{
				ptr = lastop(ptr);
				break;
			}
		}

		} /* end of switch */
	}

	/*
	 * If were not done parsing, parse it again.
	 */
	if (stage != NU_UNIT)
		return next_unit(str, args, arg_flag, stage + 1);

	/*
	 * If the result is a number, return it.
	 */
	if (my_isdigit(str))
		return m_strdup(str);


	/*
	 * If the result starts with a #, or a @, its a special op
	 */
	if (*str == '#' || *str == '@')
		op = *str++;
	else
		op = '\0';

	/*
	 * Its not a number, so its a variable, look it up.
	 */
	if (!*str)
		result1 = m_strdup(args);
	else if (!(result1 = get_variable(str)))
		return m_strdup(empty_string);

	/*
	 * See if we have to take strlen or word_count on the variable.
	 */
	if (op)
	{
		if (op == '#')
			value1 = word_count(result1);
		else if (op == '@')
			value1 = strlen(result1);
		new_free(&result1);
		return m_strdup(ltoa(value1));
	}

	/*
	 * Nope.  Just return the variable.
	 */
	return result1;
}

/*
 * parse_inline:  This evaluates user-variable expression.  I'll talk more
 * about this at some future date. The ^ function and some fixes by
 * troy@cbme.unsw.EDU.AU (Troy Rollo) 
 */
char	*parse_inline (char *str, const char *args, int *args_flag)
{
	if (x_debug & DEBUG_NEW_MATH)
		return matheval(str, args, args_flag);
	else
		return next_unit(str, args, args_flag, NU_EXPR);
}



/**************************** TEXT MODE PARSER *****************************/
/*
 * expand_alias: Expands inline variables in the given string and returns the
 * expanded string in a new string which is malloced by expand_alias(). 
 *
 * Also unescapes anything that was quoted with a backslash
 *
 * Behaviour is modified by the following:
 *	Anything between brackets (...) {...} is left unmodified.
 *	If more_text is supplied, the text is broken up at
 *		semi-colons and returned one at a time. The unprocessed
 *		portion is written back into more_text.
 *	Backslash escapes are unescaped.
 */
char	*expand_alias	(const char *string, const char *args, int *args_flag, char **more_text)
{
	char	*buffer = NULL,
		*ptr,
		*stuff = (char *) 0,
		*free_stuff,
		*quote_str = (char *) 0;
	char	quote_temp[2];
	char	ch;
	int	is_quote = 0;
	int	unescape = 1;

	if (!string || !*string)
		return m_strdup(empty_string);

	if (*string == '@' && more_text)
	{
		unescape = 0;
		*args_flag = 1; /* Stop the @ command from auto appending */
	}
	quote_temp[1] = 0;

	stuff = LOCAL_COPY(string);
	free_stuff = stuff;

	ptr = stuff;
	if (more_text)
		*more_text = NULL;

	while (ptr && *ptr)
	{
		if (is_quote)
		{
			is_quote = 0;
			++ptr;
			continue;
		}

		switch (*ptr)
		{
		    case '$':
		    {
			/*
			 * The test here ensures that if we are in the 
			 * expression evaluation command, we don't expand $. 
			 * In this case we are only coming here to do command 
			 * separation at ';'s.  If more_text is not defined, 
			 * and the first character is '@', we have come here 
			 * from [] in an expression.
			 */
			if (more_text && *string == '@')
			{
				ptr++;
				break;
			}
			*ptr++ = 0;
			if (!*ptr)
				break;		/* Hrm. */

			m_strcat_ues(&buffer, stuff, unescape);

			for (; *ptr == '^'; ptr++)
			{
				ptr++;
				if (!*ptr)	/* Blah */
					break;
				quote_temp[0] = *ptr;
				malloc_strcat(&quote_str, quote_temp);
			}
			stuff = alias_special_char(&buffer, ptr, args, quote_str, args_flag);
			if (quote_str)		/* Why ``stuff''? */
				new_free(&quote_str);
			ptr = stuff;
			break;
		    }

		    case ';':
		    {
			if (!more_text)
			{
				ptr++;
				break;
			}
			*more_text = (char *)(string + (ptr - free_stuff) + 1);
			*ptr = '\0'; /* To terminate the loop */
			break;
		    }

		    case LEFT_PAREN:
		    case LEFT_BRACE:
		    {
			ch = *ptr;
			*ptr = '\0';
			m_strcat_ues(&buffer, stuff, unescape);
			stuff = ptr;
			*args_flag = 1;
			if (!(ptr = MatchingBracket(stuff + 1, ch,
					(ch == LEFT_PAREN) ?
					RIGHT_PAREN : RIGHT_BRACE)))
			{
				put_it("Unmatched %c", ch);
				ptr = stuff + strlen(stuff+1)+1;
			}
			else
				ptr++;

			*stuff = ch;
			ch = *ptr;
			*ptr = '\0';
			malloc_strcat(&buffer, stuff);
			stuff = ptr;
			*ptr = ch;
			break;
		    }

		    case '\\':
		    {
			is_quote = 1;
			ptr++;
			break;
		    }

		    default:
			ptr++;
			break;
		}
	}

	if (stuff)
		m_strcat_ues(&buffer, stuff, unescape);

	if (get_int_var(DEBUG_VAR) & DEBUG_EXPANSIONS)
		yell("Expanded [%s] to [%s]", string, buffer);

	return buffer;
}

/*
 * alias_special_char: Here we determine what to do with the character after
 * the $ in a line of text. The special characters are described more fully
 * in the help/ALIAS file.  But they are all handled here. Parameters are the
 * return char ** pointer to which things are placed,
 * a ptr to the string (the first character of which is the special
 * character), the args to the alias, and a character indication what
 * characters in the string should be quoted with a backslash.  It returns a
 * pointer to the character right after the converted alias.
 * 
 * The args_flag is set to 1 if any of the $n, $n-, $n-m, $-m, $*, or $() 
 * is used in the alias.  Otherwise it is left unchanged.
 */
static	char	*alias_special_char(char **buffer, char *ptr, const char *args, char *quote_em, int *args_flag)
{
	char	*tmp,
		*tmp2,
		c;
	int	upper,
		lower,
		length;

	length = 0;
	if ((c = *ptr) == LEFT_BRACKET)
	{
		ptr++;
		if ((tmp = MatchingBracket(ptr, '[', ']')))
		{
			*tmp++ = 0;
			if (*ptr == '$')
			{
				char *	str;
				str = expand_alias(ptr, args, args_flag, NULL);
				length = my_atol(str);
				new_free(&str);
			}
			else
				length = my_atol(ptr);

			ptr = tmp;
			c = *ptr;
		}
		else
		{
			say("Missing %c", RIGHT_BRACKET);
			return (ptr);
		}
	}

	/*
	 * Dont ever _ever_ _EVER_ break out of this switch.
	 * Doing so will catch the panic() at the end of this function. 
	 * If you need to break, then youre doing something wrong.
	 */
	tmp = ptr+1;
	switch (c)
	{
		/*
		 * Nothing to expand?  Hrm...
		 */
		case 0:
		{
			tmp = m_strdup(empty_string);
			TruncateAndQuote(buffer, tmp, length, quote_em);
			new_free(&tmp);
			return ptr;
		}

		/*
		 * $(...) is the "dereference" case.  It allows you to 
		 * expand whats inside of the parens and use *that* as a
		 * variable name.  The idea can be used for pointers of
		 * sorts.  Specifically, if $foo == [bar], then $($foo) is
		 * actually the same as $(bar), which is actually $bar.
		 * Got it?
		 *
		 * epic4pre1.049 -- I changed this somewhat.  I dont know if
		 * itll get me in trouble.  It will continue to expand the
		 * inside of the parens until the first character isnt a $.
		 * since by all accounts the result of the expansion is
		 * SUPPOSED to be an lvalue, obviously a leading $ precludes
		 * this.  However, there are definitely some cases where you
		 * might want two or even three levels of indirection.  I'm
		 * not sure I have any immediate ideas why, but Kanan probably
		 * does since he's the one that needed this. (esl)
		 */
		case LEFT_PAREN:
		{
			char 	*sub_buffer = NULL,
				*tmp2 = NULL, 
				*tmpsav = NULL,
				*ph = ptr + 1;

			if ((ptr = MatchingBracket(ph, '(', ')')) || 
	/* ( */		    (ptr = strchr(ph, ')')))
				*ptr++ = 0;
			else
				yell("Unmatched ( (continuing anyways)");

			/*
			 * Keep expanding as long as neccesary.
			 */
			do
			{
				tmp2 = expand_alias(tmp, args, args_flag, NULL);
				if (tmpsav)
					new_free(&tmpsav);
				tmpsav = tmp = tmp2;
			}
			while (*tmp == '$');

			alias_special_char(&sub_buffer, tmp, args, 
						quote_em, args_flag);

			/* Some kind of bogus expando */
			if (sub_buffer == NULL)
				sub_buffer = m_strdup(empty_string);

			if (!(x_debug & DEBUG_SLASH_HACK))
				TruncateAndQuote(buffer, sub_buffer, 
						length, quote_em);

			new_free(&sub_buffer);
			new_free(&tmpsav);
			*args_flag = 1;
			return (ptr);
		}

		/*
		 * $!...! is the history mechanism.  Given a string, it will
		 * return the most previously executed command that contains
		 * that string.  Or something like that.
		 */
		case '!':
		{
			if ((ptr = strchr(tmp, '!')))
				*ptr++ = 0;

			if ((tmp = do_history(tmp, empty_string)) != NULL)
			{
				TruncateAndQuote(buffer, tmp, length, quote_em);
				new_free(&tmp);
			}
			return (ptr);
		}

		/*
		 * ${...} is the expression parser.  Given a mathematical
		 * expression, it will evaluate it and then return the result
		 * of the expression as the expando value.
		 */
		case LEFT_BRACE:
		{
			char	*ph = ptr + 1;

			/* 
			 * BLAH. This didnt allow for nesting before.  
			 * How lame.
			 */
			if ((ptr = MatchingBracket(ph, '{', '}')) ||
			    (ptr = strchr(ph, '}')))
				*ptr++ = 0;
			else
				yell("Unmatched { (continuing anyways)");

			if ((tmp = parse_inline(tmp, args, args_flag)) != NULL)
			{
				TruncateAndQuote(buffer, tmp, length, quote_em);
				new_free(&tmp);
			}
			return (ptr);
		}

		/*
		 * $"..." is the syncrhonous input mechanism.  Given a prompt,
		 * This waits until the user enters a full line of text and
		 * then returns that text as the expando value.  Note that 
		 * this requires a recursive call to io(), so you have all of
		 * the synchronous problems when this is mixed with /wait and
		 * /redirect and all that jazz waiting for io() to unwind.
		 *
		 * This has been changed to use add_wait_prompt instead of
		 * get_line, and hopefully get_line will go away.  I dont 
		 * expect any problems, but watch out for this anyways.
		 *
		 * Also note that i added ' to this, so now you can also
		 * do $'...' that acts just like $"..." except it only
		 * waits for the next key rather than the next return.
		 */
		case DOUBLE_QUOTE:
		case '\'':
		{
			if ((ptr = strchr(tmp, c)))
				*ptr++ = 0;

			alias_string = NULL;
			add_wait_prompt(tmp, do_alias_string, empty_string,
				(c == DOUBLE_QUOTE) ? WAIT_PROMPT_LINE
						    : WAIT_PROMPT_KEY, 1);
			while (!alias_string)
				io("Input Prompt");

			TruncateAndQuote(buffer, alias_string, length,quote_em);
			new_free(&alias_string);
			return (ptr);
		}

		/*
		 * $* is the very special "all args" expando.  You really
		 * shouldnt ever use $0-, because its a lot more involved
		 * (see below).  $* is handled here; very quickly, quietly,
		 * and without any fuss.
		 */
		case '*':
		{
			TruncateAndQuote(buffer, args, length, quote_em);
			*args_flag = 1;
			return (ptr + 1);
		}

		/*
		 * $# and $@ are the "word count" and "length" operators.
		 * Given any normal rvalue following the # or @, this will
		 * return the number of words/characters in that result.
		 * As a special case, if no rvalue is specified, then the
		 * current args list is the default.  So, $# all by its
		 * lonesome is the number of arguments passed to the alias.
		 */
		case '#':
		case '@':
		{
			char 	c2 = 0;
			char 	*sub_buffer = NULL;
			char 	*rest, *val;
			int	dummy;

			rest = after_expando(ptr + 1, 0, &dummy);
			if (rest == ptr + 1)
			{
			    sub_buffer = m_strdup(args);
			    *args_flag = 1;
			}
			else
			{
			    c2 = *rest;
			    *rest = 0;
			    alias_special_char(&sub_buffer, ptr + 1, 
						args, quote_em, args_flag);
			    *rest = c2;
			}

			if (c == '#')
			    val = m_strdup(ltoa(word_count(sub_buffer)));
			else
			    val = m_strdup(ltoa(strlen(sub_buffer)));

#if 0	/* Don't need to do this here */
			if (!(x_debug & DEBUG_SLASH_HACK))
#endif
				TruncateAndQuote(buffer, val, length, quote_em);
			new_free(&val);
			new_free(&sub_buffer);

			if (c2)
			    *rest = c2;

			return rest;
		}

		/*
		 * Do some magic for $$.
		 */
		case '$':
		{
			TruncateAndQuote(buffer, "$", length, quote_em);
			return ptr + 1;
		}

		/*
		 * Ok.  So its some other kind of expando.  Generally,
		 * its either a numeric expando or its a normal rvalue.
		 */
		default:
		{
			/*
			 * Is it a numeric expando?  This includes the
			 * "special" expando $~.
			 */
			if (isdigit(c) || (c == '-') || c == '~')
			{
			    *args_flag = 1;

			    /*
			     * Handle $~.  EOS especially handles this
			     * condition.
			     */
			    if (c == '~')
			    {
				lower = upper = EOS;
				ptr++;
			    }

			    /*
			     * Handle $-X where X is some number.  Note that
			     * any leading spaces in the args list are
			     * retained in the result, even if X is 0.  The
			     * stock client stripped spaces out on $-0, but
			     * not for any other case, which was judged to be
			     * in error.  We always retain the spaces.
			     *
			     * If there is no number after the -, then the
			     * hyphen is slurped and expanded to nothing.
			     */
			    else if (c == '-')
			    {
				lower = SOS;
				ptr++;
				upper = parse_number(&ptr);
				if (upper == -1)
				    return empty_string; /* error */
			    }

			    /*
			     * Handle $N, $N-, and $N-M, where N and M are
			     * numbers.
			     */
			    else
			    {
				lower = parse_number(&ptr);
				if (*ptr == '-')
				{
				    ptr++;
				    upper = parse_number(&ptr);
				    if (upper == -1)
					upper = EOS;
				}
				else
				    upper = lower;
			    }

			    /*
			     * Protect against a crash.  There
			     * are some gross syntactic errors
			     * that can be made that will result
			     * in ''args'' being NULL here.  That
			     * will crash the client, so we have
			     * to protect against that by simply
			     * chewing the expando.
			     */
			    if (!args)
				tmp2 = m_strdup(empty_string);
			    else
				tmp2 = extractw2(args, lower, upper);

			    TruncateAndQuote(buffer, tmp2, length, quote_em);
			    new_free(&tmp2);
			    return (ptr ? ptr : empty_string);
			}

			/*
			 * Ok.  So we know we're doing a normal rvalue
			 * expando.  Slurp it up.
			 */
			else
			{
			    char  *rest, d = 0;
			    int	  function_call = 0;

			    rest = after_expando(ptr, 0, &function_call);
			    if (*rest)
			    {
				d = *rest;
				*rest = 0;
			    }

			    if (function_call)
				tmp = call_function(ptr, args, args_flag);
			    else
				tmp = get_variable_with_args(ptr, args, args_flag);

			    if (!tmp)
				tmp = m_strdup(empty_string);

			    TruncateAndQuote(buffer, tmp, length, quote_em);

			    if (function_call)
				MUST_BE_MALLOCED(tmp, "BOGUS RETVAL FROM FUNCTION CALL");
			    else
				MUST_BE_MALLOCED(tmp, "BOGUS RETVAL FROM VARIABLE EXPANSION");

			    new_free(&tmp);	/* Crash here */

			    if (d)
				*rest = d;

			    return(rest);
			}
		}
	}
	panic("Returning NULL from alias_special_char");
	return NULL;
}

/*
 * TruncateAndQuote: This handles string width formatting and quoting for irc 
 * variables when [] or ^x is specified.  
 */
static	void	TruncateAndQuote(char **buff, const char *add, int length, const char *quote_em)
{
	if (!add)
		panic("add is NULL");

	/* 
	 * Semantics:
	 *	If length is nonzero, then "add" will be truncated
	 *	   to "length" characters
	 * 	If length is zero, nothing is done to "add"
	 *	If quote_em is not NULL, then the resulting string
	 *	   will be quoted and appended to "buff"
	 *	If quote_em is NULL, then the value of "add" is
	 *	   appended to "buff"
	 */

	if (length)
	{
		/*
		 * No, the "alloca" space doesnt go away until the end
		 * of the function, so yes, we can reference "add" after
		 * the end of this block.
		 */
		char *buffer = alloca(abs(length) + 1);
		add = strformat(buffer, add, length, get_int_var(PAD_CHAR_VAR));
	}
	if (quote_em && add)	/* add shouldnt ever be NULL, though! */
	{
		/* Don't pass retval from "alloca" as func arg, use local. */
		char *ptr = alloca(strlen(add) * 2 + 2);
		add = double_quote(add, quote_em, ptr);
	}

	if (buff)
		malloc_strcat(buff, add);

	return;
}

static void	do_alias_string (char *unused, char *input)
{
	malloc_strcpy(&alias_string, input);
}

