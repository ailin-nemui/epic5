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
 * The second license is OpenBSD's ISC-like license, which is used for 
 * strlcpy() and strlcat().  See the license later on in the file.
 * 
 * Everthing else has had its copyright explicitly disclaimed by the author.
 */


#include "defs.h"
#include "irc_std.h"
#include "ircaux.h"
#include "output.h"

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

static int cvtchar (const char *sp, char *c)
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

char *my_tparm(const char *str, ...) {
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
					snprintf(sbuf, MAX_LINE, fmt, s);
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
						snprintf(sbuf, MAX_LINE, fmt, i);
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
/* ---------------------- strlcpy, strlcat ----------------------------- */
/*
 * Copyright (c) 1998 Todd C. Miller <Todd.Miller@courtesan.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND TODD C. MILLER DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL TODD C. MILLER BE LIABLE
 * FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef HAVE_STRLCPY
/*	OpenBSD's strlcpy.c version 1.7 2003/04/12 21:56:39 millert */
/*
 * Copy src to string dst of size siz.  At most siz-1 characters
 * will be copied.  Always NUL terminates (unless siz == 0).
 * Returns strlen(src); if retval >= siz, truncation occurred.
 */
size_t	strlcpy (char *dst, const char *src, size_t siz)
{
	char *d = dst;
	const char *s = src;
	size_t n = siz;

	/* Copy as many bytes as will fit */
	if (n != 0 && --n != 0) {
		do {
			if ((*d = *s) == 0)
				break;
			d++;
			s++;
		} while (--n != 0);
	}

	/* Not enough room in dst, add NUL and traverse rest of src */
	if (n == 0) {
		if (siz != 0)
			*d = '\0';		/* NUL-terminate dst */
		while (*s)
			s++;
	}

	return(s - src);	/* count does not include NUL */
}
#endif

#ifndef HAVE_STRLCAT
/*      OpenBSD's strlcat.c version 1.10 2003/04/12 21:56:39 millert */
/*
 * Appends src to string dst of size siz (unlike strncat, siz is the
 * full size of dst, not space left).  At most siz-1 characters
 * will be copied.  Always NUL terminates (unless siz <= strlen(dst)).
 * Returns strlen(src) + MIN(siz, strlen(initial dst)).
 * If retval >= siz, truncation occurred.
 */
size_t	strlcat (char *dst, const char *src, size_t siz)
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

        return(dlen + (s - src));        /* count does not include NUL */
}
#endif

/* --------------------------- start of arc4 stuff -------------------- */
#ifndef HAVE_ARC4RANDOM
/*
 * Arc4 random number generator for OpenBSD.
 * Copyright 1996 David Mazieres <dm@lcs.mit.edu>.
 *
 * Modification and redistribution in source and binary forms is
 * permitted provided that due credit is given to the author and the
 * OpenBSD project (for instance by leaving this copyright notice
 * intact).
 */

/*
 * This code is derived from section 17.1 of Applied Cryptography,
 * second edition, which describes a stream cipher allegedly
 * compatible with RSA Labs "RC4" cipher (the actual description of
 * which is a trade secret).  The same algorithm is used as a stream
 * cipher called "arcfour" in Tatu Ylonen's ssh package.
 *
 * Here the stream cipher has been modified always to include the time
 * when initializing the state.  That makes it impossible to
 * regenerate the same random sequence twice, so this can't be used
 * for encryption, but will generate good random numbers.
 *
 * RC4 is a registered trademark of RSA Laboratories.
 */
struct bsd_arc4_stream {
	unsigned char	i;
	unsigned char	j;
	unsigned char	s[256];
};
typedef struct bsd_arc4_stream 	ARC4;

static int	rs_initialized = 0;
static ARC4	rs;

__inline__
static void	bsd_arc4_init (ARC4 *as)
{
	int     n;

	for (n = 0; n < 256; n++)
		as->s[n] = n;
	as->i = 0;
	as->j = 0;
}

__inline__
static void	bsd_arc4_addrandom (ARC4 *as, unsigned char *dat, int datlen)
{
	int     n;
	unsigned char	si;

	as->i--;
	for (n = 0; n < 256; n++) {
		as->i = (as->i + 1);
		si = as->s[as->i];
		as->j = (as->j + si + dat[n % datlen]);
		as->s[as->i] = as->s[as->j];
		as->s[as->j] = si;
	}
}

static void	bsd_arc4_stir (ARC4 *as)
{
	int     fd;
	struct {
		Timeval tv;
		pid_t 	pid;
		unsigned char	rnd[128 - sizeof(Timeval) - sizeof(pid_t)];
	}       rdat;

	gettimeofday(&rdat.tv, NULL);
	rdat.pid = getpid();
	if ((fd = open("/dev/urandom", O_RDONLY, 0)) >= 0) 
	{
		if (read(fd, rdat.rnd, sizeof(rdat.rnd)) <= 0)
			yell("Read from /dev/urandom failed.  Bummer.");
		close(fd);
	}
	/* 
	 * fd < 0?  Ah, what the heck. We'll just take whatever was on the
	 * stack... 
	 */
	bsd_arc4_addrandom(as, (void *) &rdat, sizeof(rdat));
}

__inline__
static unsigned char		bsd_arc4_getbyte (ARC4 *as)
{
	unsigned char si, sj;

	as->i = (as->i + 1);
	si = as->s[as->i];
	as->j = (as->j + si);
	sj = as->s[as->j];
	as->s[as->i] = sj;
	as->s[as->j] = si;
	return (as->s[(si + sj) & 0xff]);
}

__inline__
static uint32_t	bsd_arc4_getword (ARC4 *as)
{
	uint32_t val;

	val = bsd_arc4_getbyte(as) << 24;
	val |= bsd_arc4_getbyte(as) << 16;
	val |= bsd_arc4_getbyte(as) << 8;
	val |= bsd_arc4_getbyte(as);
	return val;
}

void	bsd_arc4random_stir (void)
{
	if (!rs_initialized) {
		bsd_arc4_init(&rs);
		rs_initialized = 1;
	}
	bsd_arc4_stir(&rs);
}

void	bsd_arc4random_addrandom (unsigned char *dat, int datlen)
{
	if (!rs_initialized)
		bsd_arc4random_stir();
	bsd_arc4_addrandom(&rs, dat, datlen);
}

uint32_t	bsd_arc4random (void)
{
	if (!rs_initialized)
		bsd_arc4random_stir();
	return bsd_arc4_getword(&rs);
}
#endif

/* --------------------------- start of misc stuff -------------------- */
/* This is written by EPIC Software Labs contributers and is public domain */
#ifndef HAVE_VSNPRINTF
int vsnprintf (char *str, size_t size, const char *format, va_list ap)
{
	int ret = vsprintf(str, format, ap);

	/* If the string ended up overflowing, just give up. */
	/* Pre-ansi vsprintf()s return (char *) */
	if (ret == (int)str && strlen(str) > size)
		panic(1, "Buffer overflow in vsnprintf");
	/* ANSI vsprintf()s return (int) */
	if (ret != (int)str && ret > size)
		panic(1, "Buffer overflow in vsnprintf");

	/* We always return (int). */
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

#ifndef HAVE_SETENV
int	setenv (const char *name, const char *value, int overwrite)
{
	static int warning = 0;
	char *envvalue;
	size_t	len;

	if (warning == 0) {
		yell("Warning: Your system does not have setenv(3).  Setting the same environment variable multiple times will result in memory leakage.  This is unavoidable and does not represent a bug in EPIC.");
		warning = 1;
	}

	len = strlen(name) + strlen(value) + 2;
	envvalue = (char *)malloc(len);
	snprintf(envvalue, len, "%s=%s", name, value);
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

#ifdef HAVE_BROKEN_REALPATH
# if defined(realpath)
#  undef realpath
# endif
char *	my_realpath (const char *pathname, char resolved_path[PATH_MAX])
{
	char *mypath;
	char *rest;
	Stat unused;
	size_t	size;

	/* If the file exists, just run realpath on it. */
	if (stat(pathname, &unused) == 0)
		return realpath(pathname, resolved_path);

	/* Otherwise, run realpath() on the dirname only */
	size = strlen(pathname) + 1;
	mypath = alloca(size);
	strlcpy(mypath, pathname, size);
	if ((rest = strrchr(mypath, '/')))
		*rest++ = 0;
	else
	{
		rest = LOCAL_COPY(mypath);
		strlcpy(mypath, ".", size);
	}

	if (realpath(mypath, resolved_path) == NULL)
		return NULL;

	/* And put the basename back on the result. */
	strlcat(resolved_path, "/", PATH_MAX);
	strlcat(resolved_path, rest, PATH_MAX);
	return resolved_path;
}
#endif

/*
#ifdef NEED_STRTOLL
long long	strtoll (const char *nptr, char **endptr, int base)
{
}
#endif
*/

/*
#ifndef HAVE_STRTOIMAX
long		strtoimax (const char *nptr, char **endptr, int base)
{
	return (long)strtol(nptr, endptr, base);
}
#endif
*/

/**** END MISC PUBLIC DOMAIN STUFF ****/

