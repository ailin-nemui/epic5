/*
 * Copyright © 1997, 2003 EPIC Software Labs.
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

#ifndef __WORDS_H__
#define __WORDS_H__

/* From words.c */
#define SOS 		-32767
#define EOS 		 32767

/*
 * These are the "extended" values, and determine whether move_word_rel
 * and real_move_to_abs_word are to honor "double quoted words" or not.
 */
#define DWORD_NO 	0		/* Never honor double quoted words */
#define DWORD_YES 	1		/* Always support */
#define DWORD_EXTRACTW 	2		/* Support only if /xdebug extractw */
#define DWORD_DWORDS	3		/* Support only if /xdebug dword */

char *		search_for		(char *, char **, char *, int);
ssize_t		move_word_rel		(Char *, Char **, int, int, Char *);
const char *	real_move_to_abs_word 	(Char *, Char **, int, int, Char *);
#define move_to_abs_word(a, b, c)	real_move_to_abs_word(a, b, c, DWORD_YES, "\"");

/*
 * real_extract is used for word extraction functions where if 'firstword'
 * is negative, it silently gets truncated up to 0.
 * 	start		The word list (value of $*)
 *	firstword	The first word to extract
 *	lastword	The final word to extract
 *	extended	The type of words in this list.
 */
char *		real_extract 		(char *, int, int, int);

/*
 * real_extract2 is used for word extraction where if 'firstword'
 * is negative, it is recalculated from the end of the string (ie -2 means
 * to start 2 words from the end of the string)
 *	start		The word list (value of $*)
 *	firstword	The first word to extract (negative counts from end)
 * 	secondword	The final word to extract (negative counts from end)
 *	extended	The type of words in this list
 */
char *		real_extract2 		(Char *, int, int, int);

/*
 * Macros to extract UWORDS (double quoted words never supported)
 */
#define extract(a, b, c)	real_extract(a, b, c, DWORD_NO)
#define extract2(a, b, c)	real_extract2(a, b, c, DWORD_NO)
#define next_arg(a, b)		universal_next_arg_count((a),(b),1,DWORD_NO,0,"\"")
#define next_arg_count(a, b, c)	universal_next_arg_count((a),(b),(c),DWORD_NO,0,"\"")

/*
 * Macros to extract DWORDS (double quoted words always supported)
 */
#define extractdw(a, b, c)	real_extract(a, b, c, DWORD_YES)
#define extractdw2(a, b, c)	real_extract2(a, b, c, DWORD_YES)
#define new_next_arg(a, b)	universal_next_arg_count((a),(b),1,DWORD_YES,1,"\"")
#define new_next_arg_count(a, b, c)	universal_next_arg_count((a),(b),(c),DWORD_YES,0,"\"")

/*
 * Macros to extract DWORDS FROM $* (depends on /xdebug extractw)
 */
#define extractew(a, b, c)	real_extract(a, b, c, DWORD_EXTRACTW)
#define extractew2(a, b, c)	real_extract2(a, b, c, DWORD_EXTRACTW)
#define next_earg(a, b)		universal_next_arg_count((a),(b),1,DWORD_EXTRACTW,1,"\"")

/*
 * Macros to extract DWORDS FROM FUNCTIONS (depends on /xdebug dword)
 */
#define extractfw(a, b, c)	real_extract(a, b, c, DWORD_DWORDS)
#define extractfw2(a, b, c)	real_extract2(a, b, c, DWORD_DWORDS)
#define next_func_arg(a, b)	universal_next_arg_count((a),(b),1,DWORD_DWORDS,1,"\"")

int		count_words		(Char *str, int extended, Char *quotes);

#endif
