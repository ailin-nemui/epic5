/*
 * Everything that im not directly responsible for I put in here.  Almost
 * all of this stuff is either borrowed from somewhere else (for you poor
 * saps that dont have something epic needs), or I wrote (and put into the 
 * public domain) in order to get epic to compile on some of the more painful
 * systems.  None of this is part of EPIC-proper, so dont feel that you're 
 * going to hurt my feelings if you "steal" this.
 */

/*
 * There are two different licenses in use by this file.  The first one
 * is the classic 3-clause BSD license: (The old clause 3 has been removed,
 * pursant to ftp://ftp.cs.berkeley.edu/ucb/4bsd/README.Impt.License.Change
 *
 * * * *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE. 
 * * * *
 *
 * And then everthing is either explicitly placed in the public domain
 * by someone else, or placed into the public domain by me.
 */


#include "defs.h"
#include "irc_std.h"

/*****************************************************************************/
/* ------------------------------- start of tparm.c ------------------------ */
#ifndef HAVE_TPARM
/*
 * tparm.c
 *
 * By Ross Ridge
 * Public Domain
 * 92/02/01 07:30:36
 *
 */
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifndef MAX_PUSHED
#define MAX_PUSHED	32
#endif

#define ARG	1
#define NUM	2

#define INTEGER	1
#define STRING	2

#define MAX_LINE 640

typedef void* anyptr;

typedef struct stack_str {
	int	type;
	int	argnum;
	int	value;
} stack;

static stack S[MAX_PUSHED];
static stack vars['z'-'a'+1];
static int pos = 0;

static struct arg_str {
	int type;
	int	integer;
	char	*string;
} arg_list[10];

static int argcnt;

static va_list tparm_args;

static int pusharg(int arg)
{
	if (pos == MAX_PUSHED)
		return 1;
	S[pos].type = ARG;
	S[pos++].argnum = arg;
	return 0;
}

static int pushnum(int num)
{
	if (pos == MAX_PUSHED)
		return 1;
	S[pos].type = NUM;
	S[pos++].value = num;
	return 0;
}

/* VARARGS2 */
static int getarg(int argnum, int type, anyptr p)
{
	while (argcnt < argnum) {
		arg_list[argcnt].type = INTEGER;
		arg_list[argcnt++].integer = (int) va_arg(tparm_args, int);
	}
	if (argcnt > argnum) {
		if (arg_list[argnum].type != type)
			return 1;
		else if (type == STRING)
			*(char **)p = arg_list[argnum].string;
		else
			*(int *)p = arg_list[argnum].integer;
	} else {
		arg_list[argcnt].type = type;
		if (type == STRING)
			*(char **)p = arg_list[argcnt++].string
				= (char *) va_arg(tparm_args, char *);
		else
			*(int *)p = arg_list[argcnt++].integer = (int) va_arg(tparm_args, int);
	}
	return 0;
}


static int popstring(char **str)
{
	if (pos-- == 0)
		return 1;
	if (S[pos].type != ARG)
		return 1;
	return(getarg(S[pos].argnum, STRING, (anyptr) str));
}

static int popnum(int *num)
{
	if (pos-- == 0)
		return 1;
	switch (S[pos].type) {
	case ARG:
		return (getarg(S[pos].argnum, INTEGER, (anyptr) num));
	case NUM:
		*num = S[pos].value;
		return 0;
	}
	return 1;
}

static int cvtchar(char *sp, char *c)
{
	switch(*sp) {
	case '\\':
		switch(*++sp) {
		case '\'':
		case '$':
		case '\\':
		case '%':
			*c = *sp;
			return 2;
		case '\0':
			*c = '\\';
			return 1;
		case '0':
			if (sp[1] == '0' && sp[2] == '0') {
				*c = '\0';
				return 4;
			}
			*c = '\200'; /* '\0' ???? */
			return 2;
		default:
			*c = *sp;
			return 2;
		}
	default:
		*c = *sp;
		return 1;
	}
}

static int termcap;

/* sigh... this has got to be the ugliest code I've ever written.
   Trying to handle everything has its cost, I guess.

   It actually isn't to hard to figure out if a given % code is supposed
   to be interpeted with its termcap or terminfo meaning since almost
   all terminfo codes are invalid unless something has been pushed on
   the stack and termcap strings will never push things on the stack
   (%p isn't used by termcap). So where we have a choice we make the
   decision by wether or not somthing has been pushed on the stack.
   The static variable termcap keeps track of this; it starts out set
   to 1 and is incremented as each argument processed by a termcap % code,
   however if something is pushed on the stack it's set to 0 and the
   rest of the % codes are interpeted as terminfo % codes. Another way
   of putting it is that if termcap equals one we haven't decided either
   way yet, if it equals zero we're looking for terminfo codes, and if
   its greater than 1 we're looking for termcap codes.

   Terminfo % codes:

	%%	output a '%'
	%[[:][-+# ][width][.precision]][doxXs]
		output pop according to the printf format
	%c	output pop as a char
	%'c'	push character constant c.
	%{n}	push decimal constant n.
	%p[1-9] push paramter [1-9]
	%g[a-z] push variable [a-z]
	%P[a-z] put pop in variable [a-z]
	%l	push the length of pop (a string)
	%+	add pop to pop and push the result
	%-	subtract pop from pop and push the result
	%*	multiply pop and pop and push the result
	%&	bitwise and pop and pop and push the result
	%|	bitwise or pop and pop and push the result
	%^	bitwise xor pop and pop and push the result
	%~	push the bitwise not of pop
	%=	compare if pop and pop are equal and push the result
	%>	compare if pop is less than pop and push the result
	%<	compare if pop is greater than pop and push the result
	%A	logical and pop and pop and push the result
	%O	logical or pop and pop and push the result
	%!	push the logical not of pop
	%? condition %t if_true [%e if_false] %;
		if condtion evaulates as true then evaluate if_true,
		else evaluate if_false. elseif's can be done:
%? cond %t true [%e cond2 %t true2] ... [%e condN %t trueN] [%e false] %;
	%i	add one to parameters 1 and 2. (ANSI)

  Termcap Codes:

	%%	output a %
	%.	output parameter as a character
	%d	output parameter as a decimal number
	%2	output parameter in printf format %02d
	%3	output parameter in printf format %03d
	%+x	add the character x to parameter and output it as a character
(UW)	%-x	subtract parameter FROM the character x and output it as a char
(UW)	%ax	add the character x to parameter
(GNU)	%a[+*-/=][cp]x
		GNU arithmetic.
(UW)	%sx	subtract parameter FROM the character x
	%>xy	if parameter > character x then add character y to parameter
	%B	convert to BCD (parameter = (parameter/10)*16 + parameter%16)
	%D	Delta Data encode (parameter = parameter - 2*(paramter%16))
	%i	increment the first two parameters by one
	%n	xor the first two parameters by 0140
(GNU)	%m	xor the first two parameters by 0177
	%r	swap the first two parameters
(GNU)	%b	backup to previous parameter
(GNU)	%f	skip this parameter

  Note the two definitions of %a, the GNU defintion is used if the characters
  after the 'a' are valid, otherwise the UW definition is used.

  (GNU) used by GNU Emacs termcap libraries
  (UW) used by the University of Waterloo (MFCF) termcap libraries

*/

char *tparm(const char *str, ...) {
	static char OOPS[] = "OOPS";
	static char buf[MAX_LINE];
	register const char *sp;
	register char *dp;
	register char *fmt;
	char conv_char;
	char scan_for;
	int scan_depth = 0, if_depth;
	static int i, j;
	static char *s, c;
	char fmt_buf[MAX_LINE];
	char sbuf[MAX_LINE];

	va_start(tparm_args, str);

	sp = str;
	dp = buf;
	scan_for = 0;
	if_depth = 0;
	argcnt = 0;
	pos = 0;
	termcap = 1;
	while (*sp != '\0') {
		switch(*sp) {
		case '\\':
			if (scan_for) {
				if (*++sp != '\0')
					sp++;
				break;
			}
			*dp++ = *sp++;
			if (*sp != '\0')
				*dp++ = *sp++;
			break;
		case '%':
			sp++;
			if (scan_for) {
				if (*sp == scan_for && if_depth == scan_depth) {
					if (scan_for == ';')
						if_depth--;
					scan_for = 0;
				} else if (*sp == '?')
					if_depth++;
				else if (*sp == ';') {
					if (if_depth == 0)
						return OOPS;
					else
						if_depth--;
				}
				sp++;
				break;
			}
			fmt = NULL;
			switch(*sp) {
			case '%':
				*dp++ = *sp++;
				break;
			case '+':
				if (!termcap) {
					if (popnum(&j) || popnum(&i))
						return OOPS;
					i += j;
					if (pushnum(i))
						return OOPS;
					sp++;
					break;
				}
				;/* FALLTHROUGH */
			case 'C':
				if (*sp == 'C') {
					if (getarg(termcap - 1, INTEGER, &i))
						return OOPS;
					if (i >= 96) {
						i /= 96;
						if (i == '$')
							*dp++ = '\\';
						*dp++ = i;
					}
				}
				fmt = "%c";
				/* FALLTHROUGH */
			case 'a':
				if (!termcap)
					return OOPS;
				if (getarg(termcap - 1, INTEGER, (anyptr) &i))
					return OOPS;
				if (*++sp == '\0')
					return OOPS;
				if ((sp[1] == 'p' || sp[1] == 'c')
			            && sp[2] != '\0' && fmt == NULL) {
					/* GNU aritmitic parameter, what they
					   realy need is terminfo.	      */
					int val, lc;
					if (sp[1] == 'p'
					    && getarg(termcap - 1 + sp[2] - '@',
						      INTEGER, (anyptr) &val))
						return OOPS;
					if (sp[1] == 'c') {
						lc = cvtchar(sp + 2, &c) + 2;
					/* Mask out 8th bit so \200 can be
					   used for \0 as per GNU doc's    */
						val = c & 0177;
					} else
						lc = 2;
					switch(sp[0]) {
					case '=':
						break;
					case '+':
						val = i + val;
						break;
					case '-':
						val = i - val;
						break;
					case '*':
						val = i * val;
						break;
					case '/':
						val = i / val;
						break;
					default:
					/* Not really GNU's %a after all... */
						lc = cvtchar(sp, &c);
						val = c + i;
						break;
					}
					arg_list[termcap - 1].integer = val;
					sp += lc;
					break;
				}
				sp += cvtchar(sp, &c);
				arg_list[termcap - 1].integer = c + i;
				if (fmt == NULL)
					break;
				sp--;
				/* FALLTHROUGH */
			case '-':
				if (!termcap) {
					if (popnum(&j) || popnum(&i))
						return OOPS;
					i -= j;
					if (pushnum(i))
						return OOPS;
					sp++;
					break;
				}
				fmt = "%c";
				/* FALLTHROUGH */
			case 's':
				if (termcap && (fmt == NULL || *sp == '-')) {
					if (getarg(termcap - 1, INTEGER, &i))
						return OOPS;
					if (*++sp == '\0')
						return OOPS;
					sp += cvtchar(sp, &c);
					arg_list[termcap - 1].integer = c - i;
					if (fmt == NULL)
						break;
					sp--;
				}
				if (!termcap)
					return OOPS;
				;/* FALLTHROUGH */
			case '.':
				if (termcap && fmt == NULL)
					fmt = "%c";
				;/* FALLTHROUGH */
			case 'd':
				if (termcap && fmt == NULL)
					fmt = "%d";
				;/* FALLTHROUGH */
			case '2':
				if (termcap && fmt == NULL)
					fmt = "%02d";
				;/* FALLTHROUGH */
			case '3':
				if (termcap && fmt == NULL)
					fmt = "%03d";
				;/* FALLTHROUGH */
			case ':': case ' ': case '#': case 'u':
			case 'x': case 'X': case 'o': case 'c':
			case '0': case '1': case '4': case '5':
			case '6': case '7': case '8': case '9':
				if (fmt == NULL) {
					if (termcap)
						return OOPS;
					if (*sp == ':')
						sp++;
					fmt = fmt_buf;
					*fmt++ = '%';
					while(*sp != 's' && *sp != 'x' && *sp != 'X' && *sp != 'd' && *sp != 'o' && *sp != 'c' && *sp != 'u') {
						if (*sp == '\0')
							return OOPS;
						*fmt++ = *sp++;
					}
					*fmt++ = *sp;
					*fmt = '\0';
					fmt = fmt_buf;
				}
				conv_char = fmt[strlen(fmt) - 1];
				if (conv_char == 's') {
					if (popstring(&s))
						return OOPS;
					sprintf(sbuf, fmt, s);
				} else {
					if (termcap) {
						if (getarg(termcap++ - 1,
							   INTEGER, &i))
							return OOPS;
					} else
						if (popnum(&i))
							return OOPS;
					if (i == 0 && conv_char == 'c')
						*sbuf = 0;
					else
						sprintf(sbuf, fmt, i);
				}
				sp++;
				fmt = sbuf;
				while(*fmt != '\0') {
					if (*fmt == '$')
						*dp++ = '\\';
					*dp++ = *fmt++;
				}
				break;
			case 'r':
				if (!termcap || getarg(1, INTEGER, &i))
					return OOPS;
				arg_list[1].integer = arg_list[0].integer;
				arg_list[0].integer = i;
				sp++;
				break;
			case 'i':
				if (getarg(1, INTEGER, &i)
				    || arg_list[0].type != INTEGER)
					return OOPS;
				arg_list[1].integer++;
				arg_list[0].integer++;
				sp++;
				break;
			case 'n':
				if (!termcap || getarg(1, INTEGER, &i))
					return OOPS;
				arg_list[0].integer ^= 0140;
				arg_list[1].integer ^= 0140;
				sp++;
				break;
			case '>':
				if (!termcap) {
					if (popnum(&j) || popnum(&i))
						return OOPS;
					i = (i > j);
					if (pushnum(i))
						return OOPS;
					sp++;
					break;
				}
				if (getarg(termcap-1, INTEGER, &i))
					return OOPS;
				sp += cvtchar(sp, &c);
				if (i > c) {
					sp += cvtchar(sp, &c);
					arg_list[termcap-1].integer += c;
				} else
					sp += cvtchar(sp, &c);
				sp++;
				break;
			case 'B':
				if (!termcap || getarg(termcap-1, INTEGER, &i))
					return OOPS;
				arg_list[termcap-1].integer = 16*(i/10)+i%10;
				sp++;
				break;
			case 'D':
				if (!termcap || getarg(termcap-1, INTEGER, &i))
					return OOPS;
				arg_list[termcap-1].integer = i - 2 * (i % 16);
				sp++;
				break;
			case 'p':
				if (termcap > 1)
					return OOPS;
				if (*++sp == '\0')
					return OOPS;
				if (*sp == '0')
					i = 9;
				else
					i = *sp - '1';
				if (i < 0 || i > 9)
					return OOPS;
				if (pusharg(i))
					return OOPS;
				termcap = 0;
				sp++;
				break;
			case 'P':
				if (termcap || *++sp == '\0')
					return OOPS;
				i = *sp++ - 'a';
				if (i < 0 || i > 25)
					return OOPS;
				if (pos-- == 0)
					return OOPS;
				switch(vars[i].type = S[pos].type) {
				case ARG:
					vars[i].argnum = S[pos].argnum;
					break;
				case NUM:
					vars[i].value = S[pos].value;
					break;
				}
				break;
			case 'g':
				if (termcap || *++sp == '\0')
					return OOPS;
				i = *sp++ - 'a';
				if (i < 0 || i > 25)
					return OOPS;
				switch(vars[i].type) {
				case ARG:
					if (pusharg(vars[i].argnum))
						return OOPS;
					break;
				case NUM:
					if (pushnum(vars[i].value))
						return OOPS;
					break;
				}
				break;
			case '\'':
				if (termcap > 1)
					return OOPS;
				if (*++sp == '\0')
					return OOPS;
				sp += cvtchar(sp, &c);
				if (pushnum(c) || *sp++ != '\'')
					return OOPS;
				termcap = 0;
				break;
			case '{':
				if (termcap > 1)
					return OOPS;
				i = 0;
				sp++;
				while(isdigit(*sp))
					i = 10 * i + *sp++ - '0';
				if (*sp++ != '}' || pushnum(i))
					return OOPS;
				termcap = 0;
				break;
			case 'l':
				if (termcap || popstring(&s))
					return OOPS;
				i = strlen(s);
				if (pushnum(i))
					return OOPS;
				sp++;
				break;
			case '*':
				if (termcap || popnum(&j) || popnum(&i))
					return OOPS;
				i *= j;
				if (pushnum(i))
					return OOPS;
				sp++;
				break;
			case '/':
				if (termcap || popnum(&j) || popnum(&i))
					return OOPS;
				i /= j;
				if (pushnum(i))
					return OOPS;
				sp++;
				break;
			case 'm':
				if (termcap) {
					if (getarg(1, INTEGER, &i))
						return OOPS;
					arg_list[0].integer ^= 0177;
					arg_list[1].integer ^= 0177;
					sp++;
					break;
				}
				if (popnum(&j) || popnum(&i))
					return OOPS;
				i %= j;
				if (pushnum(i))
					return OOPS;
				sp++;
				break;
			case '&':
				if (popnum(&j) || popnum(&i))
					return OOPS;
				i &= j;
				if (pushnum(i))
					return OOPS;
				sp++;
				break;
			case '|':
				if (popnum(&j) || popnum(&i))
					return OOPS;
				i |= j;
				if (pushnum(i))
					return OOPS;
				sp++;
				break;
			case '^':
				if (popnum(&j) || popnum(&i))
					return OOPS;
				i ^= j;
				if (pushnum(i))
					return OOPS;
				sp++;
				break;
			case '=':
				if (popnum(&j) || popnum(&i))
					return OOPS;
				i = (i == j);
				if (pushnum(i))
					return OOPS;
				sp++;
				break;
			case '<':
				if (popnum(&j) || popnum(&i))
					return OOPS;
				i = (i < j);
				if (pushnum(i))
					return OOPS;
				sp++;
				break;
			case 'A':
				if (popnum(&j) || popnum(&i))
					return OOPS;
				i = (i && j);
				if (pushnum(i))
					return OOPS;
				sp++;
				break;
			case 'O':
				if (popnum(&j) || popnum(&i))
					return OOPS;
				i = (i || j);
				if (pushnum(i))
					return OOPS;
				sp++;
				break;
			case '!':
				if (popnum(&i))
					return OOPS;
				i = !i;
				if (pushnum(i))
					return OOPS;
				sp++;
				break;
			case '~':
				if (popnum(&i))
					return OOPS;
				i = ~i;
				if (pushnum(i))
					return OOPS;
				sp++;
				break;
			case '?':
				if (termcap > 1)
					return OOPS;
				termcap = 0;
				if_depth++;
				sp++;
				break;
			case 't':
				if (popnum(&i) || if_depth == 0)
					return OOPS;
				if (!i) {
					scan_for = 'e';
					scan_depth = if_depth;
				}
				sp++;
				break;
			case 'e':
				if (if_depth == 0)
					return OOPS;
				scan_for = ';';
				scan_depth = if_depth;
				sp++;
				break;
			case ';':
				if (if_depth-- == 0)
					return OOPS;
				sp++;
				break;
			case 'b':
				if (--termcap < 1)
					return OOPS;
				sp++;
				break;
			case 'f':
				if (!termcap++)
					return OOPS;
				sp++;
				break;
			}
			break;
		default:
			if (scan_for)
				sp++;
			else
				*dp++ = *sp++;
			break;
		}
	}
	va_end(tparm_args);
	*dp = '\0';
	return buf;
}
#endif
/* ----------------------- end of tparm.c ------------------------------- */
/**************************************************************************/
/* ---------------------- start of strtoul.c ---------------------------- */
#ifndef HAVE_STRTOUL
/*
 * Copyright (c) 1990 Regents of the University of California.
 * All rights reserved.
 *
 * Licensed under the 3-clause BSD license, see above for text.
 */

/*
 * Convert a string to an unsigned long integer.
 *
 * Ignores `locale' stuff.  Assumes that the upper and lower case
 * alphabets and digits are each contiguous.
 */
unsigned long strtoul (const char *nptr, char **endptr, int base)
{
	const char *s;
	unsigned long acc, cutoff;
	int c;
	int neg, any, cutlim;

	s = nptr;
	do
		c = *s++;
	while (isspace(c));

	if (c == '-') 
	{
		neg = 1;
		c = *s++;
	} 
	else 
	{
		neg = 0;
		if (c == '+')
			c = *s++;
	}

	if ((base == 0 || base == 16) && c == '0' && (*s == 'x' || *s == 'X')) 
	{
		c = s[1];
		s += 2;
		base = 16;
	}

	if (base == 0)
		base = c == '0' ? 8 : 10;

#ifndef ULONG_MAX
#define ULONG_MAX (unsigned long) -1
#endif

	cutoff = ULONG_MAX / (unsigned long)base;
	cutlim = ULONG_MAX % (unsigned long)base;

	for (acc = 0, any = 0;; c = *s++) 
	{
		if (isdigit(c))
			c -= '0';
		else if (isalpha(c))
			c -= isupper(c) ? 'A' - 10 : 'a' - 10;
		else
			break;

		if (c >= base)
			break;

		if (any < 0)
			continue;

		if (acc > cutoff || acc == cutoff && c > cutlim) 
		{
			any = -1;
			acc = ULONG_MAX;
			errno = ERANGE;
		}
		else 
		{
			any = 1;
			acc *= (unsigned long)base;
			acc += c;
		}
	}
	if (neg && any > 0)
		acc = -acc;
	if (endptr != 0)
		*endptr = (char *) (any ? s - 1 : nptr);
	return (acc);
}
#endif /* DO NOT HAVE STRTOUL */
/* ----------------------- end of strtoul.c ----------------------------- */
/**************************************************************************/
/* --------------------- start of inet_aton.c --------------------------- */
#ifndef HAVE_INET_ATON
/*
 * Copyright (c) 1983, 1990, 1993
 *    The Regents of the University of California.  All rights reserved.
 * Portions Copyright (c) 1993 by Digital Equipment Corporation.
 * Licensed under the 3-clause BSD license, see above for text.
 */

/* 
 * Check whether "cp" is a valid ascii representation
 * of an Internet address and convert to a binary address.
 * Returns 1 if the address is valid, 0 if not.
 * This replaces inet_addr, the return value from which
 * cannot distinguish between failure and a local broadcast address.
 */
int inet_aton (const char *cp, IA *addr)
{
	unsigned long	val;
	int		base, n;
	char		c;
	unsigned	parts[4];
	unsigned	*pp = parts;

	c = *cp;
	for (;;) {
		/*
		 * Collect number up to ``.''.
		 * Values are specified as for C:
		 * 0x=hex, 0=octal, isdigit=decimal.
		 */
		if (!isdigit(c))
			return (0);
		val = 0; base = 10;
		if (c == '0') {
			c = *++cp;
			if (c == 'x' || c == 'X')
				base = 16, c = *++cp;
			else
				base = 8;
		}
		for (;;) {
			if (isascii(c) && isdigit(c)) {
				val = (val * base) + (c - '0');
				c = *++cp;
			} else if (base == 16 && isascii(c) && isxdigit(c)) {
				val = (val << 4) |
					(c + 10 - (islower(c) ? 'a' : 'A'));
				c = *++cp;
			} else
				break;
		}
		if (c == '.') {
			/*
			 * Internet format:
			 *	a.b.c.d
			 *	a.b.c	(with c treated as 16 bits)
			 *	a.b	(with b treated as 24 bits)
			 */
			if (pp >= parts + 3)
				return (0);
			*pp++ = val;
			c = *++cp;
		} else
			break;
	}
	/*
	 * Check for trailing characters.
	 */
	if (c != '\0' && (!isascii(c) || !isspace(c)))
		return (0);
	/*
	 * Concoct the address according to
	 * the number of parts specified.
	 */
	n = pp - parts + 1;
	switch (n) {

	case 0:
		return (0);		/* initial nondigit */

	case 1:				/* a -- 32 bits */
		break;

	case 2:				/* a.b -- 8.24 bits */
		if (val > 0xffffff)
			return (0);
		val |= parts[0] << 24;
		break;

	case 3:				/* a.b.c -- 8.8.16 bits */
		if (val > 0xffff)
			return (0);
		val |= (parts[0] << 24) | (parts[1] << 16);
		break;

	case 4:				/* a.b.c.d -- 8.8.8.8 bits */
		if (val > 0xff)
			return (0);
		val |= (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8);
		break;
	}
	if (addr)
		addr->s_addr = htonl(val);
	return (1);
}
#endif
/* --------------------- end of inet_aton.c ---------------------------- */
/**************************************************************************/
/* ---------------------- strlcpy, strlcat ----------------------------- */
/*
 * Copyright (c) 1998 Todd C. Miller <Todd.Miller@courtesan.com>
 * All rights reserved.
 * Licensed under the 3-clase BSD license
 */

#ifndef HAVE_STRLCPY
/*
 * Copy src to string dst of size siz.  At most siz-1 characters
 * will be copied.  Always NUL terminates (unless siz == 0).
 * Returns strlen(src); if retval >= siz, truncation occurred.
 */
size_t strlcpy (char *dst, const char *src, size_t siz)
{
        char *d = dst;
        const char *s = src;
        size_t n = siz;

	if (n > 0)
		n--;		/* Save space for the NUL */

        /* Copy as many bytes as will fit */
	while (n > 0)
	{
		if (!(*d = *s))
			break;
		d++;
		s++;
		n--;
        }

        /* Not enough room in dst, add NUL and traverse rest of src */
        if (n == 0 && siz)
                *d = 0;              /* NUL-terminate dst */

	return strlen(src);

#ifdef notanymore
        /* Copy as many bytes as will fit */
        if (n != 0 && --n != 0) {
                do {
                        if ((*d++ = *s++) == 0)
                                break;
                } while (--n != 0);
        }

        /* Not enough room in dst, add NUL and traverse rest of src */
        if (n == 0) {
                if (siz != 0)
                        *d = '\0';              /* NUL-terminate dst */
                while (*s++)
                        ;
        }

        return(s - src - 1);    /* count does not include NUL */
#endif
}
#endif

#ifndef HAVE_STRLCAT
/*
 * Appends src to string dst of size siz (unlike strncat, siz is the
 * full size of dst, not space left).  At most siz-1 characters
 * will be copied.  Always NUL terminates (unless siz == 0).
 * Returns strlen(src); if retval >= siz, truncation occurred.
 */
size_t strlcat(char *dst, const char *src, size_t siz)
{
        char *d = dst;
        const char *s = src;
        size_t n = siz;
        size_t dlen;

        /* Find the end of dst and adjust bytes left but don't go past end */
        while (n-- != 0 && *d != '\0')
                d++;
        dlen = d - dst;
        n = siz - dlen;

        if (n == 0)
                return(dlen + strlen(s));
        while (*s != '\0') {
                if (n != 1) {
                        *d++ = *s;
                        n--;
                }
                s++;
        }
        *d = '\0';

        return(dlen + (s - src));       /* count does not include NUL */
}
#endif

/* --------------------------- start of misc stuff -------------------- */
/* This is all written by Jeremy Nelson and is public domain */
#ifndef HAVE_VSNPRINTF
int vsnprintf (char *str, size_t size, const char *format, va_list ap)
{
	int ret = vsprintf(str, format, ap);

	/* If the string ended up overflowing, just give up. */
	if (ret == (int)str && strlen(str) > size)
		panic("Buffer overflow in vsnprintf");
	if (ret != (int)str && ret > size)
		panic("Buffer overflow in vsnprintf");

	return ret;
}
#endif

#ifndef HAVE_SNPRINTF
int snprintf (char *str, size_t size, const char *format, ...)
{
	int ret;
	va_list args;

	va_start(args, format);
	ret = vsnprintf(str, size, format, args);
	va_end(args);
	return ret;
}
#endif

#ifndef HAVE_SETSID
int	setsid	(void)
{
	setpgrp(getpid(), getpid());
}
#endif

#ifndef HAVE_STPCPY
char *	stpcpy (char *to, const char *from)
{
	while ((*to = *from))
		to++, from++;
	return to;
}
#endif

#ifndef HAVE_SETENV
int	setenv (const char *name, const char *value, int overwrite)
{
	static int warning = 0;
	char *envvalue;

	if (warning == 0) {
		yell("Warning: Your system does not have setenv(3).  Setting the same environment variable multiple times will result in memory leakage.  This is unavoidable and does not represent a bug in EPIC.");
		warning = 1;
	}

	envvalue = m_sprintf("%s=%s", name, value);
	putenv(envvalue);
	return 0;
}
#endif

#ifndef HAVE_UNSETENV
int	unsetenv (const char *name)
{
	yell("Warning: Your system does not have unsetenv(3) and so it is not possible to unset the [%s] environment variable.", name);
	return -1;
}
#endif


/* --- end of misc stuff --- */
/**************************************************************************/
/* ---------------------- inet_ntop, inet_pton ---------------------------*/
#ifndef HAVE_INET_NTOP
/* Copyright (c) 1996 by Internet Software Consortium.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND INTERNET SOFTWARE CONSORTIUM DISCLAIMS
 * ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL INTERNET SOFTWARE
 * CONSORTIUM BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
 * ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 */
#include <sys/param.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <arpa/nameser.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#define SPRINTF(x) ((size_t)sprintf x)

/*
 * WARNING: Don't even consider trying to compile this on a system where
 * sizeof(int) < 4.  sizeof(int) > 4 is fine; all the world's not a VAX.
 */

static const char *inet_ntop4 (const u_char *src, char *dst, size_t size);
static const char *inet_ntop6 (const u_char *src, char *dst, size_t size);

/* char *
 * inet_ntop(af, src, dst, size)
 *	convert a network format address to presentation format.
 * return:
 *	pointer to presentation format address (`dst'), or NULL (see errno).
 * author:
 *	Paul Vixie, 1996.
 */
const char *
inet_ntop(af, src, dst, size)
	int af;
	const void *src;
	char *dst;
	size_t size;
{
	switch (af) {
	case AF_INET:
		return (inet_ntop4(src, dst, size));
	case AF_INET6:
		return (inet_ntop6(src, dst, size));
	default:
		errno = EAFNOSUPPORT;
		return (NULL);
	}
	/* NOTREACHED */
}

/* const char *
 * inet_ntop4(src, dst, size)
 *	format an IPv4 address, more or less like inet_ntoa()
 * return:
 *	`dst' (as a const)
 * notes:
 *	(1) uses no statics
 *	(2) takes a u_char* not an in_addr as input
 * author:
 *	Paul Vixie, 1996.
 */
static const char *
inet_ntop4(src, dst, size)
	const u_char *src;
	char *dst;
	size_t size;
{
	static const char fmt[] = "%u.%u.%u.%u";
	char tmp[sizeof "255.255.255.255"];

	if (SPRINTF((tmp, fmt, src[0], src[1], src[2], src[3])) > size) {
		errno = ENOSPC;
		return (NULL);
	}
	strcpy(dst, tmp);
	return (dst);
}

/* const char *
 * inet_ntop6(src, dst, size)
 *	convert IPv6 binary address into presentation (printable) format
 * author:
 *	Paul Vixie, 1996.
 */
static const char *
inet_ntop6(src, dst, size)
	const u_char *src;
	char *dst;
	size_t size;
{
	/*
	 * Note that int32_t and int16_t need only be "at least" large enough
	 * to contain a value of the specified size.  On some systems, like
	 * Crays, there is no such thing as an integer variable with 16 bits.
	 * Keep this in mind if you think this function should have been coded
	 * to use pointer overlays.  All the world's not a VAX.
	 */
	char tmp[sizeof "ffff:ffff:ffff:ffff:ffff:ffff:255.255.255.255"], *tp;
	struct { int base, len; } best, cur;
	u_int words[NS_IN6ADDRSZ / NS_INT16SZ];
	int i;

	/*
	 * Preprocess:
	 *	Copy the input (bytewise) array into a wordwise array.
	 *	Find the longest run of 0x00's in src[] for :: shorthanding.
	 */
	memset(words, '\0', sizeof words);
	for (i = 0; i < NS_IN6ADDRSZ; i++)
		words[i / 2] |= (src[i] << ((1 - (i % 2)) << 3));
	best.base = -1;
	cur.base = -1;
	for (i = 0; i < (NS_IN6ADDRSZ / NS_INT16SZ); i++) {
		if (words[i] == 0) {
			if (cur.base == -1)
				cur.base = i, cur.len = 1;
			else
				cur.len++;
		} else {
			if (cur.base != -1) {
				if (best.base == -1 || cur.len > best.len)
					best = cur;
				cur.base = -1;
			}
		}
	}
	if (cur.base != -1) {
		if (best.base == -1 || cur.len > best.len)
			best = cur;
	}
	if (best.base != -1 && best.len < 2)
		best.base = -1;

	/*
	 * Format the result.
	 */
	tp = tmp;
	for (i = 0; i < (NS_IN6ADDRSZ / NS_INT16SZ); i++) {
		/* Are we inside the best run of 0x00's? */
		if (best.base != -1 && i >= best.base &&
		    i < (best.base + best.len)) {
			if (i == best.base)
				*tp++ = ':';
			continue;
		}
		/* Are we following an initial run of 0x00s or any real hex? */
		if (i != 0)
			*tp++ = ':';
		/* Is this address an encapsulated IPv4? */
		if (i == 6 && best.base == 0 &&
		    (best.len == 6 || (best.len == 5 && words[5] == 0xffff))) {
			if (!inet_ntop4(src+12, tp, sizeof tmp - (tp - tmp)))
				return (NULL);
			tp += strlen(tp);
			break;
		}
		tp += SPRINTF((tp, "%x", words[i]));
	}
	/* Was it a trailing run of 0x00's? */
	if (best.base != -1 && (best.base + best.len) ==
	    (NS_IN6ADDRSZ / NS_INT16SZ))
		*tp++ = ':';
	*tp++ = '\0';

	/*
	 * Check for overflow, copy, and we're done.
	 */
	if ((size_t)(tp - tmp) > size) {
		errno = ENOSPC;
		return (NULL);
	}
	strcpy(dst, tmp);
	return (dst);
}
#endif

#ifndef HAVE_INET_PTON
/*
 * WARNING: Don't even consider trying to compile this on a system where
 * sizeof(int) < 4.  sizeof(int) > 4 is fine; all the world's not a VAX.
 */

static int	inet_pton4 (const char *src, u_char *dst);
static int	inet_pton6 (const char *src, u_char *dst);

/* int
 * inet_pton(af, src, dst)
 *	convert from presentation format (which usually means ASCII printable)
 *	to network format (which is usually some kind of binary format).
 * return:
 *	1 if the address was valid for the specified address family
 *	0 if the address wasn't valid (`dst' is untouched in this case)
 *	-1 if some other error occurred (`dst' is untouched in this case, too)
 * author:
 *	Paul Vixie, 1996.
 */
int
inet_pton(af, src, dst)
	int af;
	const char *src;
	void *dst;
{
	switch (af) {
	case AF_INET:
		return (inet_pton4(src, dst));
	case AF_INET6:
		return (inet_pton6(src, dst));
	default:
		errno = EAFNOSUPPORT;
		return (-1);
	}
	/* NOTREACHED */
}

/* int
 * inet_pton4(src, dst)
 *	like inet_aton() but without all the hexadecimal and shorthand.
 * return:
 *	1 if `src' is a valid dotted quad, else 0.
 * notice:
 *	does not touch `dst' unless it's returning 1.
 * author:
 *	Paul Vixie, 1996.
 */
static int
inet_pton4(src, dst)
	const char *src;
	u_char *dst;
{
	static const char digits[] = "0123456789";
	int saw_digit, octets, ch;
	u_char tmp[NS_INADDRSZ], *tp;

	saw_digit = 0;
	octets = 0;
	*(tp = tmp) = 0;
	while ((ch = *src++) != '\0') {
		const char *pch;

		if ((pch = strchr(digits, ch)) != NULL) {
			u_int new = *tp * 10 + (pch - digits);

			if (new > 255)
				return (0);
			*tp = new;
			if (! saw_digit) {
				if (++octets > 4)
					return (0);
				saw_digit = 1;
			}
		} else if (ch == '.' && saw_digit) {
			if (octets == 4)
				return (0);
			*++tp = 0;
			saw_digit = 0;
		} else
			return (0);
	}
	if (octets < 4)
		return (0);

	memcpy(dst, tmp, NS_INADDRSZ);
	return (1);
}

/* int
 * inet_pton6(src, dst)
 *	convert presentation level address to network order binary form.
 * return:
 *	1 if `src' is a valid [RFC1884 2.2] address, else 0.
 * notice:
 *	(1) does not touch `dst' unless it's returning 1.
 *	(2) :: in a full address is silently ignored.
 * credit:
 *	inspired by Mark Andrews.
 * author:
 *	Paul Vixie, 1996.
 */
static int
inet_pton6(src, dst)
	const char *src;
	u_char *dst;
{
	static const char xdigits_l[] = "0123456789abcdef",
			  xdigits_u[] = "0123456789ABCDEF";
	u_char tmp[NS_IN6ADDRSZ], *tp, *endp, *colonp;
	const char *xdigits, *curtok;
	int ch, saw_xdigit;
	u_int val;

	memset((tp = tmp), '\0', NS_IN6ADDRSZ);
	endp = tp + NS_IN6ADDRSZ;
	colonp = NULL;
	/* Leading :: requires some special handling. */
	if (*src == ':')
		if (*++src != ':')
			return (0);
	curtok = src;
	saw_xdigit = 0;
	val = 0;
	while ((ch = *src++) != '\0') {
		const char *pch;

		if ((pch = strchr((xdigits = xdigits_l), ch)) == NULL)
			pch = strchr((xdigits = xdigits_u), ch);
		if (pch != NULL) {
			val <<= 4;
			val |= (pch - xdigits);
			if (val > 0xffff)
				return (0);
			saw_xdigit = 1;
			continue;
		}
		if (ch == ':') {
			curtok = src;
			if (!saw_xdigit) {
				if (colonp)
					return (0);
				colonp = tp;
				continue;
			}
			if (tp + NS_INT16SZ > endp)
				return (0);
			*tp++ = (u_char) (val >> 8) & 0xff;
			*tp++ = (u_char) val & 0xff;
			saw_xdigit = 0;
			val = 0;
			continue;
		}
		if (ch == '.' && ((tp + NS_INADDRSZ) <= endp) &&
		    inet_pton4(curtok, tp) > 0) {
			tp += NS_INADDRSZ;
			saw_xdigit = 0;
			break;	/* '\0' was seen by inet_pton4(). */
		}
		return (0);
	}
	if (saw_xdigit) {
		if (tp + NS_INT16SZ > endp)
			return (0);
		*tp++ = (u_char) (val >> 8) & 0xff;
		*tp++ = (u_char) val & 0xff;
	}
	if (colonp != NULL) {
		/*
		 * Since some memmove()'s erroneously fail to handle
		 * overlapping regions, we'll do the shift by hand.
		 */
		const int n = tp - colonp;
		int i;

		for (i = 1; i <= n; i++) {
			endp[- i] = colonp[n - i];
			colonp[n - i] = 0;
		}
		tp = endp;
	}
	if (tp != endp)
		return (0);
	memcpy(dst, tmp, NS_IN6ADDRSZ);
	return (1);
}
#endif
/**************************************************************************/
