/*
 * ircaux.c: some extra routines... not specific to irc... that I needed 
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright 1994 Jake Khuon.
 * Copyright 1993, 2016 EPIC Software Labs.
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

#include "irc.h"
#include "screen.h"
#include <pwd.h>
#include <sys/wait.h>
#include <math.h>
#include <stddef.h>
#include <fcntl.h>
#ifdef HAVE_IEEEFP_H
#include <ieeefp.h>
#endif
#include "ircaux.h"
#include "output.h"
#include "termx.h"
#include "vars.h"
#include "alias.h"
#include "ifcmd.h"
#include "words.h"
#include "ctcp.h"
#include "server.h"
#include "sedcrypt.h"
#include "elf.h"

/*
 * This is the basic overhead for every malloc allocation (8 bytes).
 * The size of the allocation and a "magic" sequence are retained here.
 * This is in addition to any overhead the underlying malloc package
 * may impose -- this may mean that malloc()s may be actually up to
 * 20 to 30 bytes larger than you request.
 */
typedef struct _mo_money
{
	unsigned magic;
	int size;
#ifdef ALLOC_DEBUG
	unsigned entry;
	char* fn;
	int line;
#endif
} MO;

#ifdef ALLOC_DEBUG
struct {
	int size;
	void** entries;
} alloc_table = { 0, NULL };
#endif

#define mo_ptr(ptr) ((MO *)( (char *)(ptr) - sizeof(MO) ))
#define alloc_size(ptr) ((mo_ptr(ptr))->size)
#define magic(ptr) ((mo_ptr(ptr))->magic)

#define FREED_VAL -3
#define ALLOC_MAGIC (unsigned long)0x7fbdce70

/* 
 * This was all imported by pre3 for no reason other than to track down
 * that blasted bug that splitfire is tickling.
 */
#define NO_ERROR 0
#define ALLOC_MAGIC_FAILED 1
#define ALREADY_FREED 2

static int	malloc_check (void *ptr)
{
	if (!ptr)
		return ALLOC_MAGIC_FAILED;
	if (magic(ptr) != ALLOC_MAGIC)
		return ALLOC_MAGIC_FAILED;
	if (alloc_size(ptr) == FREED_VAL)
		return ALREADY_FREED;
#ifdef ALLOC_DEBUG
	if (ptr != alloc_table.entries[mo_ptr(ptr)->entry])
		return ALREADY_FREED;
#endif
	return NO_ERROR;
}

void	fatal_malloc_check (void *ptr, const char *special, const char *fn, int line)
{
	static	int	recursion = 0;

	switch (malloc_check(ptr))
	{
	    case ALLOC_MAGIC_FAILED:
	    {
		if (recursion)
			abort();

		recursion++;
		privileged_yell("IMPORTANT! MAKE SURE TO INCLUDE ALL OF THIS INFORMATION" 
			" IN YOUR BUG REPORT!");
		privileged_yell("MAGIC CHECK OF MALLOCED MEMORY FAILED!");
		if (special)
			privileged_yell("Because of: [%s]", special);

		if (ptr)
		{
			privileged_yell("Address: [%p]  Size: [%d]  Magic: [%x] "
				"(should be [%lx])",
				ptr, alloc_size(ptr), magic(ptr), ALLOC_MAGIC);
			privileged_yell("Dump: [%s]", prntdump(ptr, alloc_size(ptr)));
		}
		else
			privileged_yell("Address: [NULL]");

		privileged_yell("IMPORTANT! MAKE SURE TO INCLUDE ALL OF THIS INFORMATION" 
			" IN YOUR BUG REPORT!");
		panic(1, "BE SURE TO INCLUDE THE ABOVE IMPORTANT INFORMATION! "
			"-- new_free()'s magic check failed from [%s/%d].", 
			fn, line);
		/* NOTREACHED */
		exit(1);
	    }

	    case ALREADY_FREED:
	    {
		if (recursion)
			abort();

		recursion++;
		panic(1, "free()d the same address twice from [%s/%d].", 
				fn, line);
	    }

	    case NO_ERROR:
		return;
	}
}

/*
 * really_new_malloc is the general interface to the malloc(3) call.
 * It is only called by way of the ``new_malloc'' #define.
 * It wont ever return NULL.
 */
void *	really_new_malloc (size_t size, const char *fn, int line)
{
	char	*ptr;

	if (!(ptr = (char *)malloc(size + sizeof(MO))))
		panic(1, "Malloc(%ld) failed from [%s/%d], giving up!", 
				(long)size, fn, line);

	/* Store the size of the allocation in the buffer. */
	ptr += sizeof(MO);
	magic(ptr) = ALLOC_MAGIC;
	alloc_size(ptr) = size;
#ifdef ALLOC_DEBUG
	mo_ptr(ptr)->entry = alloc_table.size;
	mo_ptr(ptr)->fn = fn;
	mo_ptr(ptr)->line = line;
	alloc_table.size++;
	alloc_table.entries = realloc(alloc_table.entries, (alloc_table.size) * sizeof(void**));
	alloc_table.entries[alloc_table.size-1] = ptr;
#endif
	VALGRIND_CREATE_MEMPOOL(mo_ptr(ptr), 0, 1);
	VALGRIND_MEMPOOL_ALLOC(mo_ptr(ptr), ptr, size);
	return ptr;
}

#ifdef DELAYED_FREES
/*
 * Instead of calling free() directly in really_new_free(), we instead 
 * delay that until the stack has been unwound completely.  This masks the
 * many bugs in epic where we hold a pointer to some object (such as a DCC
 * item) and then invoke a sequence point.  When that sequence point returns
 * we cannot assume that pointer is still valid.  But regretably we assume
 * that so often that we can't just sweep this problem away.  The rules
 * regarding double and invalid frees stays because that is all done at a 
 * higher level.  The only thing this changes is that we do not release
 * memory until it is impossible that we could be holding a pointer to it.
 * This does not fix those bugs, only mitigates their damaging effect.
 */
	int	need_delayed_free = 0;
static	void **	delayed_free_table;
static	int	delayed_free_table_size = 0;
static	int	delayed_frees = 0;

void	do_delayed_frees (void)
{
	int	i;

	for (i = 0; i < delayed_frees; i++)
	{
		free((void *)delayed_free_table[i]);
		delayed_free_table[i] = NULL;
	}
	delayed_frees = 0;
	need_delayed_free = 0;
}

static void	delay_free (void *ptr)
{
	need_delayed_free = 1;
	if (delayed_frees >= delayed_free_table_size - 2)
	{
		int	i = delayed_free_table_size;

		if (delayed_free_table_size)
			delayed_free_table_size *= 2;
		else
			delayed_free_table_size = 128;
		RESIZE(delayed_free_table, void *, delayed_free_table_size);
		for (; i < delayed_free_table_size; i++)
			delayed_free_table[i] = NULL;
	}
	delayed_free_table[delayed_frees++] = ptr;
}
#endif

/*
 * really_new_free is the general interface to the free(3) call.
 * It is only called by way of the ``new_free'' #define.
 * You must always use new_free to free anything youve allocated
 * with new_malloc, or all heck will break loose.
 */
void *	really_new_free (void **ptr, const char *fn, int line)
{
	if (*ptr)
	{
		VALGRIND_MEMPOOL_FREE(mo_ptr(*ptr), *ptr);
		VALGRIND_DESTROY_MEMPOOL(mo_ptr(*ptr));
		fatal_malloc_check(*ptr, NULL, fn, line);
		alloc_size(*ptr) = FREED_VAL;
#ifdef ALLOC_DEBUG
		alloc_table.entries[mo_ptr(*ptr)->entry]
			= alloc_table.entries[--alloc_table.size];
		mo_ptr(alloc_table.entries[mo_ptr(*ptr)->entry])->entry = mo_ptr(*ptr)->entry;
#endif
#ifdef DELAYED_FREES
		delay_free((void *)(mo_ptr(*ptr)));
#else
		free((void *)(mo_ptr(*ptr)));
#endif
	}
	return ((*ptr = NULL));
}

#if 1

/* really_new_malloc in disguise */
void *	really_new_realloc (void **ptr, size_t size, const char *fn, int line)
{
	void *newptr = NULL;

	if (!size) 
		return *ptr;	/* Don't change anything */
		/* *ptr = really_new_free(ptr, fn, line); */
	else if (!*ptr)
		*ptr = really_new_malloc(size, fn, line);
	else 
	{
		/* Make sure this is safe for realloc. */
		fatal_malloc_check(*ptr, NULL, fn, line);

		/* If it's already big enough, keep it. */
		if ((ssize_t)alloc_size(*ptr) >= (ssize_t)size)
		{
			VALGRIND_MEMPOOL_FREE(mo_ptr(*ptr), *ptr);
			VALGRIND_MEMPOOL_ALLOC(mo_ptr(*ptr), *ptr, size);
			return (*ptr);
		}

		/* Copy everything, including the MO buffer */
		VALGRIND_MEMPOOL_FREE(mo_ptr(*ptr), *ptr);
		VALGRIND_DESTROY_MEMPOOL(mo_ptr(*ptr));
		if ((newptr = (char *)realloc(mo_ptr(*ptr), size + sizeof(MO))))
			*ptr = newptr;
		else {
			panic(1, "realloc() failed from [%s/%d], giving up!", fn, line);
		}

		/* Re-initalize the MO buffer; magic(*ptr) is already set. */
		*ptr = (void *)((char *)*ptr + sizeof(MO));
		alloc_size(*ptr) = size;
#ifdef ALLOC_DEBUG
		alloc_table.entries[mo_ptr(*ptr)->entry] = *ptr;
#endif
		VALGRIND_CREATE_MEMPOOL(mo_ptr(*ptr), 0, 1);
		VALGRIND_MEMPOOL_ALLOC(mo_ptr(*ptr), *ptr, size);
	}
	return *ptr;
}

#else

void *	new_realloc (void **ptr, size_t size)
{
	char *ptr2 = NULL;
	size_t  foo,bar;

	/* Yes the function is ifdefed out, but this serves as a proof of concept. */
	for (foo=1, bar=size+sizeof(MO); bar; foo<<=1, bar>>=1) /* Nothing */ ;
	if (foo) {size=foo;}

	if (*ptr)
	{
		if (size)
		{
			size_t msize = alloc_size(*ptr);

			if (msize >= size)
			{
				VALGRIND_MEMPOOL_TRIM(mo_ptr(*ptr), *ptr, size);
				return *ptr;
			}

			ptr2 = new_malloc(size);
			memmove(ptr2, *ptr, msize);
		}
		new_free(ptr);
	}
	else if (size)
		ptr2 = new_malloc(size);

	return ((*ptr = ptr2));
}

#endif

void malloc_dump (const char *file) {
#ifdef ALLOC_DEBUG
	int	foo, bar;
	FILE	*fd;

	if (!(file && *file && (fd=fopen(file, "a"))))
		fd=stdout;

	for (foo = alloc_table.size; foo--;) {
		fprintf(fd, "%s/%d\t%d\t", mo_ptr(alloc_table.entries[foo])->fn, mo_ptr(alloc_table.entries[foo])->line, mo_ptr(alloc_table.entries[foo])->size);
		for (bar = 0; bar<mo_ptr(alloc_table.entries[foo])->size; bar++)
			fprintf(fd, " %x", (unsigned char)(((char*)(alloc_table.entries[foo]))[bar]));
		fprintf(fd, "\n");
	}
	fclose(fd);
#endif
}

/*
 * Now *technically* there could be a problem if uppercase of a code point
 * took more bytes than the original one.  So we pay attention to that.
 */
char *	upper (char *str)
{
	char 	*s, *p;
	int	c, d;
	char	*x, *y;

	s = str;
	while (x = s, (c = next_code_point((const unsigned char **)&s, 1)))
	{
		d = mkupper_l(c);
		if (c != d)
		{
			char c_utf8str[16];
			char d_utf8str[16];

			ucs_to_utf8(c, c_utf8str, sizeof(c_utf8str));
			ucs_to_utf8(d, d_utf8str, sizeof(d_utf8str));
			if ((ssize_t)strlen(d_utf8str) != (s - x))
			{
				yell("The string [%s] contains a character [%s] whose upper case version [%s] is not the same length.  I didn't convert it for your safety.", str, c_utf8str, d_utf8str);
				continue;
			}

			y = d_utf8str;
			while (*y)
				*x++ = *y++;
		}
	}

	return str;
}

char *	lower (char *str)
{
	char 	*s, *p;
	int	c, d;
	char	*x, *y;

	s = str;
	while (x = s, (c = next_code_point((const unsigned char **)&s, 1)))
	{
		d = mklower_l(c);
		if (c != d)
		{
			char c_utf8str[16];
			char d_utf8str[16];

			ucs_to_utf8(c, c_utf8str, sizeof(c_utf8str));
			ucs_to_utf8(d, d_utf8str, sizeof(d_utf8str));
			if ((ssize_t)strlen(d_utf8str) != (s - x))
			{
				yell("The string [%s] contains a character [%s] whose upper case version [%s] is not the same length.  I didn't convert it for your safety.", str, c_utf8str, d_utf8str);
				continue;
			}

			y = d_utf8str;
			while (*y)
				*x++ = *y++;
		}
	}

	return str;
}

/* case insensitive string searching */
ssize_t	stristr (const char *start, const char *srch)
{
	const char *p;
	int	srchlen;
	int	d;

	/* There must be both a source string and a search string */
	if (!start || !*start || !srch || !*srch)
		return -1;

	srchlen = quick_code_point_count(srch);

	/* The search string must be shorter than the source string */
	if (srchlen > quick_code_point_count(start))
		return -1;

	/*
	 * For each byte in 'start' (-> p)
 	 *  - If 'srch' exists here
	 *	Return this position as code points into 'start'.
	 *	(Counting code points instead of bytes makes this utf-8 aware)
 	 *  - Grab the next code point from "here"
	 *  - If there is no code point here, skip this byte
	 *	[see note below]
	 *  - If this code point is nul, then we didn't find 'srch'.
	 */
	p = start;
	for (;;)
	{
		if (!my_strnicmp(p, srch, srchlen))
			return quick_code_point_index(start, p);

		/*
		 * Although we support 'start' and 'srch' being UTF-8,
		 * why do we walk 'start' byte-by-byte instead of 
		 * codepoint-by-codepoint?
		 *
		 * 1. We know both strings are in the same encoding, so
		 *    my_strnicmp() is valid.
		 * 2. No valid UTF-8 string would match a string that begins 
		 *    with an invalid/partial UTF-8 sequence, so if 'p' points 
		 *    in the middle of a sequence, it will fail quickly.
		 * 
		 * Technically, this fails to maximize the awesome, but it
		 * works, so I don't care.
		 *
		 * One possible way to increase the awesome would be to create
		 * a new next_code_point() type function that would resync
		 * 'p' to the next code point, instead of just returning -1
		 * to indicate "lack of sync".
		 */
		while ((d = next_code_point((const unsigned char **)&p, 0)) == -1)
			p++;

		/* Not found */
		if (d == 0)
			return -1;
	}

	return -1;
}

/* case insensitive string searching from the end */
/* All the comments from stristr() apply here */
ssize_t	rstristr (const char *start, const char *srch)
{
	const char *p;
	int	srchlen;
	int	d, i;

	/* There must be both a source string and a search string */
	if (!start || !*start || !srch || !*srch)
		return -1;

	srchlen = quick_code_point_count(srch);

	/* The search string must be shorter than the source string */
	if (srchlen > quick_code_point_count(start))
		return -1;

	/* Start looking at <srclen> code points from the end of <start>. */
	p = start + strlen(start);
	for (i = 1; i < srchlen; i++)
		previous_code_point(start, (const unsigned char **)&p);

	for (;;)
	{
		if (!my_strnicmp(p, srch, srchlen))
			return quick_code_point_index(start, p);

		if (previous_code_point(start, (const unsigned char **)&p) == 0)
			return -1;		/* Not found */

	}

	return -1;
}


char *	remove_trailing_spaces (char *foo, size_t *cluep)
{
	char *end;
	size_t clue = cluep?*cluep:0;
	if (!*foo)
		return foo;

	end = clue + foo + strlen(clue + foo) - 1;
	while (end > foo && my_isspace(*end))
		end--;
	/* If this is a \, then it was a \ before a space.  Go forward */
	if (end[0] == '\\' && my_isspace(end[1]))
		end++;
	end[1] = 0;
	if (cluep) 
		*cluep = end - foo;
	return foo;
}

char *	forcibly_remove_trailing_spaces (char *foo, size_t *cluep)
{
	char *end;
	size_t clue = cluep?*cluep:0;
	if (!*foo)
		return foo;

	end = clue + foo + strlen(clue + foo) - 1;
	while (end > foo && my_isspace(*end))
		end--;
	/* Do not save spaces after \ at end of words! */
	end[1] = 0;
	if (cluep)
		*cluep = end - foo;
	return foo;
}

char *	next_in_comma_list (char *str, char **after)
{
	return next_in_div_list(str, after, ',');
}

/* This is only used by WHO so i don't care if it's expensive. */
int	remove_from_comma_list (char *str, const char *what)
{
	char *result = NULL;
	size_t	clue = 0;
	size_t	bufsiz;
	char *s, *p;
	int	removed = 0;

	bufsiz = strlen(str) + 1;
	p = str;

	while (p && *p)
	{
		s = next_in_comma_list(p, &p);
		if (!my_stricmp(s, what))
		{
			removed = 1;
			continue;
		}
		malloc_strcat_wordlist_c(&result, ",", s, &clue);
	}

	if (result)
		strlcpy(str, result, bufsiz);
	else
		*str = 0;

	new_free(&result);
	return removed;
}

char *	next_in_div_list (char *str, char **after, int delim)
{
	unsigned char *s, *p;
	int	c;

	s = str;
	while (p = s, (c = next_code_point((const unsigned char **)&s, 0)))
	{
		if (c == -1)
		{
			s++;
			continue;
		}

		if (c == delim)
		{
			*p++ = 0;	/* Terminate the old string */
			break;
		}
	}

	*after = s;	/* Pointing at the new string */
	return str;
}

unsigned char rfc1459_stricmp_table [] = 
{
	0,	1,	2,	3,	4,	5,	6,	7,
	8,	9,	10,	11,	12,	13,	14,	15,
	16,	17,	18,	19,	20,	21,	22,	23,
	24,	25,	26,	27,	28,	29,	30,	31,
	32,	33,	34,	35,	36,	37,	38,	39,
	40,	41,	42,	43,	44,	45,	46,	47,
	48,	49,	50,	51,	52,	53,	54,	55,
	56,	57,	58,	59,	60,	61,	62,	63,
	64,	65,	66,	67,	68,	69,	70,	71,
	72,	73,	74,	75,	76,	77,	78,	79,
	80,	81,	82,	83,	84,	85,	86,	87,
	88,	89,	90,	91,	92,	93,	94,	95,
	96,	65,	66,	67,	68,	69,	70,	71,
	72,	73,	74,	75,	76,	77,	78,	79,
	80,	81,	82,	83,	84,	85,	86,	87,
	88,	89,	90,	91,	92,	93,	94,	127,

	128,	129,	130,	131,	132,	133,	134,	135,
	136,	137,	138,	139,	140,	141,	142,	143,
	144,	145,	146,	147,	148,	149,	150,	151,
	152,	153,	154,	155,	156,	157,	158,	159,
	160,	161,	162,	163,	164,	165,	166,	167,
	168,	169,	170,	171,	172,	173,	174,	175,
	176,	177,	178,	179,	180,	181,	182,	183,
	184,	185,	186,	187,	188,	189,	190,	191,
	192,	193,	194,	195,	196,	197,	198,	199,
	200,	201,	202,	203,	204,	205,	206,	207,
	208,	209,	210,	211,	212,	213,	214,	215,
	216,	217,	218,	219,	220,	221,	222,	223,
	224,	225,	226,	227,	228,	229,	230,	231,
	232,	233,	234,	235,	236,	237,	238,	239,
	240,	241,	242,	243,	244,	245,	246,	247,
	248,	249,	250,	251,	252,	253,	254,	255
};

unsigned char ascii_stricmp_table [] = 
{
	0,	1,	2,	3,	4,	5,	6,	7,
	8,	9,	10,	11,	12,	13,	14,	15,
	16,	17,	18,	19,	20,	21,	22,	23,
	24,	25,	26,	27,	28,	29,	30,	31,
	32,	33,	34,	35,	36,	37,	38,	39,
	40,	41,	42,	43,	44,	45,	46,	47,
	48,	49,	50,	51,	52,	53,	54,	55,
	56,	57,	58,	59,	60,	61,	62,	63,
	64,	65,	66,	67,	68,	69,	70,	71,
	72,	73,	74,	75,	76,	77,	78,	79,
	80,	81,	82,	83,	84,	85,	86,	87,
	88,	89,	90,	91,	92,	93,	94,	95,
	96,	65,	66,	67,	68,	69,	70,	71,
	72,	73,	74,	75,	76,	77,	78,	79,
	80,	81,	82,	83,	84,	85,	86,	87,
	88,	89,	90,	123,	124,	125,	126,	127,

	128,	129,	130,	131,	132,	133,	134,	135,
	136,	137,	138,	139,	140,	141,	142,	143,
	144,	145,	146,	147,	148,	149,	150,	151,
	152,	153,	154,	155,	156,	157,	158,	159,
	160,	161,	162,	163,	164,	165,	166,	167,
	168,	169,	170,	171,	172,	173,	174,	175,
	176,	177,	178,	179,	180,	181,	182,	183,
	184,	185,	186,	187,	188,	189,	190,	191,
	192,	193,	194,	195,	196,	197,	198,	199,
	200,	201,	202,	203,	204,	205,	206,	207,
	208,	209,	210,	211,	212,	213,	214,	215,
	216,	217,	218,	219,	220,	221,	222,	223,
	224,	225,	226,	227,	228,	229,	230,	231,
	232,	233,	234,	235,	236,	237,	238,	239,
	240,	241,	242,	243,	244,	245,	246,	247,
	248,	249,	250,	251,	252,	253,	254,	255
};

unsigned char *stricmp_tables[2] = {
	ascii_stricmp_table,
	rfc1459_stricmp_table
};

/* XXX These functions should mean "must be equal, at least to 'n' chars */
/* my_table_strnicmp: case insensitive version of strncmp */
#if 0
int	my_table_strnicmp (const unsigned char *short_string, const unsigned char *full_string, size_t min_match, int table)
{
	int	actual_match = 0;

	while (*short_string && *full_string &&
		(stricmp_tables[table][(unsigned short)*short_string] == 
		 stricmp_tables[table][(unsigned short)*full_string]))
	    short_string++, full_string++, actual_match++;

	/* 
	 * Success IFF the the two strings match for the LONGER of
	 * 		(strlen(short_string), min_match)
	 * That is to say, the short string MUST be exhausted
	 * and its length must be >= min_match.
	 */
	if (*short_string == 0 && actual_match >= min_match)
		return 0;

	/* Otherwise, return the ordinary strcmp() retval. */
	return (stricmp_tables[table][(unsigned short)*short_string] -
		stricmp_tables[table][(unsigned short)*full_string]);
}
#endif

/* my_strnicmp: case insensitive version of strncmp */
int     my_table_strnicmp (const unsigned char *str1, const unsigned char *str2, size_t n, int table)
{
        while (n && *str1 && *str2 && 
		(stricmp_tables[table][(unsigned short)*str1] == 
		 stricmp_tables[table][(unsigned short)*str2]))
                str1++, str2++, n--;
                        
        return (n ?
                (stricmp_tables[table][(unsigned short)*str1] -
                 stricmp_tables[table][(unsigned short)*str2]) : 0);
} 

static int	utf8_strnicmp (const unsigned char *str1, const unsigned char *str2, size_t n)
{
	const unsigned char 	*s1, *s2;
	int	c1, c2;
	int	u1, u2;

	s1 = str1;
	s2 = str2;

	while (n-- > 0)
	{
		c1 = next_code_point(&s1, 1);
		c2 = next_code_point(&s2, 1);

		if (c1 == -1 || c2 == -1)
			return *s1 - *s2;	/* What to do here? */

		u1 = mkupper_l(c1);
		u2 = mkupper_l(c2);

		if (u1 != u2)
			return u1 - u2;
		if (u1 == 0)
			return 0;
	}

	return 0;
}


/* XXX Never turn these functions into macros, we create fn ptrs to them! */
int	my_strnicmp (const unsigned char *str1, const unsigned char *str2, size_t n)
{
	return utf8_strnicmp(str1, str2, n);
	/* return my_table_strnicmp(str1, str2, n, 0); */
}

int	my_stricmp (const unsigned char *str1, const unsigned char *str2)
{
	return utf8_strnicmp(str1, str2, UINT_MAX);
	/* return my_table_strnicmp(str1, str2, UINT_MAX, 0); */
}

int	ascii_strnicmp (const unsigned char *str1, const unsigned char *str2, size_t n)
{
	return utf8_strnicmp(str1, str2, n);
	/* return my_table_strnicmp(str1, str2, n, 0); */
}

int	ascii_stricmp (const unsigned char *str1, const unsigned char *str2)
{
	return utf8_strnicmp(str1, str2, UINT_MAX);
	/* return my_table_strnicmp(str1, str2, UINT_MAX, 0); */
}

int	rfc1459_strnicmp (const unsigned char *str1, const unsigned char *str2, size_t n)
{
	return my_table_strnicmp(str1, str2, n, 1);
}

int	rfc1459_stricmp (const unsigned char *str1, const unsigned char *str2)
{
	return my_table_strnicmp(str1, str2, UINT_MAX, 1);
}

int	server_strnicmp (const unsigned char *str1, const unsigned char *str2, size_t n, int servref)
{
	int	table;

	table = get_server_stricmp_table(servref);
	if (table == 1)
		return my_table_strnicmp(str1, str2, n, table);
	else
		return utf8_strnicmp(str1, str2, n);
}

/* chop -- chops off the last 'nchar' code points. */
char *	chop	(char *stuff, size_t nchar)
{
	size_t	sl;

	sl = quick_code_point_count(stuff);	

	if (nchar > 0 && sl > 0 &&  nchar <= sl)
	{
		char *s;
		size_t	i;

		s = stuff + strlen(stuff);
		for (i = 0; i < nchar; i++)
			previous_code_point(stuff, (const unsigned char **)&s);
		*s++ = 0;
	}
	else if (nchar > sl)
		*stuff = 0;

	return stuff;
}

/*
 * strext: Makes a copy of the string delmited by two char pointers and
 * returns it in malloced memory.  Useful when you dont want to munge up
 * the original string with a null.  end must be one place beyond where
 * you want to copy, ie, its the first character you dont want to copy.
 */
char *	strext (const char *start, const char *end)
{
	char *ptr, *retval;

	ptr = retval = (char *)new_malloc(end-start+1);
	while (start < end)
		*ptr++ = *start++;
	*ptr = 0;
	return retval;
}


char *	strlopencat (char *dest, size_t maxlen, ...)
{
	va_list	args;
	int 	size;
	char *	this_arg = NULL;
	size_t 	this_len;
	char *	endp;
	char *	p;
	size_t	worklen;

	endp = dest + maxlen;		/* This better not be an error */
	size = strlen(dest);		/* Find the end of the string */
	p = dest + size;		/* We will start catting there */
	va_start(args, maxlen);

	for (;;)
	{
		/* Grab the next string, stop if no more */
		if (!(this_arg = va_arg(args, char *)))
			break;

		this_len = strlen(this_arg);	/* How much do we need? */
		worklen = endp - p;		/* How much do we have? */

		/* If not enough space, copy what we can and stop */
		if (this_len > worklen)
		{
			strlcpy(p, this_arg, worklen);
			break;			/* No point in continuing */
		}

		/* Otherwise, we have enough space, copy it */
		p += strlcpy(p, this_arg, endp - p);
	}

	va_end(args);
	return dest;
}

/*
 * malloc_strcat_ues_c:  Just as with malloc_strcat, append 'src' to the end
 * of '*dest', optionally dequoting (not copying backslashes from) 'src' 
 * pursuant to the following rules:
 *
 * special == empty_string	De-quote all characters (remove all \'s)
 * special == NULL		De-quote nothing ('src' is literal text)
 * special is anything else	De-quote only \X where X is one of the
 *				characters in 'special'.
 *
 * Examples where: dest == "one" and src == "t\w\o"
 *	special == empty_string		result is "one two"
 *					(remove all \'s)
 *	special == NULL			result is "one t\w\o"
 *					(remove no \'s)
 *	special == "lmnop"		result is "one t\wo"
 *					(remove the \ before o, but not w,
 *					 because "o" is in "lmnop")
 *
 * "ues" stands for "UnEscape Special" and was written to replace the old
 * ``strmcat_ue'' which had string length limit problems.  "_c" of course
 * means this function takes a string length clue.  The previous name,
 * 'm_strcat_ues_c' was changed becuase ISO C does not allow user symbols
 * to start with ``m_''.
 *
 * Just as with 'malloc_strcat', 'src' may be NULL and this function will
 * no-op (as opposed to crashing)
 *
 * The technique we use here is a hack, and it's expensive, but it works.
 * 1) Copy 'src' into a temporary buffer, removing any \'s as proscribed
 * 2) Append the temporary buffer to '*dest'
 *
 * NOTES: This is the "dequoter", also known as "Quoting Hell".  Everything
 * that removes \'s uses this function to do it.
 */
char *	malloc_strcat_ues_c (char **dest, const char *src, const char *special, size_t *cluep)
{
	char *workbuf, *p;
	const char *s;

	/*
	 * The callers expect (*dest) to be an empty string if
	 * 'src' is null or empty.
	 */
	if (!src || !*src)
	{
		malloc_strcat_c(dest, empty_string, cluep);
		return *dest;
	}

	/* If we're not dequoting, cut it short and return. */
	if (special == NULL)
	{
		malloc_strcat_c(dest, src, cluep);
		return *dest;
	}

	/* 
	 * Set up a working buffer for our copy.
	 * Reserve two extra spaces because the algorithm below
	 * may copy two nuls to 'workbuf', and we need the space
	 * for the second nul.
	 */
	workbuf = alloca(strlen(src) + 2);

	/* Walk 'src' looking for characters to dequote */
	for (s = src, p = workbuf; ; s++, p++)
	{
	    /* 
	     * If we see a backslash, it is not at the end of the
	     * string, and the character after it is contained in 
	     * 'special', then skip the backslash.
	     */
	    if (*s == '\\')
	    {
		/*
		 * If we are doing special dequote handling,
		 * and the \ is not at the end of the string, 
		 * and the character after it is contained
		 * within ``special'', skip the \.
		 */
		if (special != empty_string)
		{
		    /*
		     * If this character is handled by 'special', then
		     * copy the next character and either continue to
		     * the next character or stop if we're done.
		     */
		    if (s[1] && strchr(special, s[1]))
		    {
			if ((*p = *++s) == 0)
			    break;
			else
			    continue;
			/* NOTREACHED */
		    }
		}

		/*
		 * BACKWARDS COMPATABILITY:
		 * In any case where \n, \p, \r, and \0 are not 
		 * explicitly caught by 'special', we have to 
		 * convert them to \020 (dle) to maintain backwards
		 * compatability.
		 */
		if (s[1] == 'n' || s[1] == 'p' || 
		    s[1] == 'r' || s[1] == '0')
		{
			s++;			/* Skip the \ */
			*p = '\020';		/* Copy a \n */
			continue;
		}

		/*
		 * So it is not handled by 'special' and it is not
		 * a legacy escape.  So we either need to copy or ignore
		 * this \ based on the value of special.  If "special"
		 * is empty_string, we remove it.  Otherwise, we keep it.
		 */
		if (special == empty_string)
			s++;

		/*
		 * Copy this character (or in the above case, the character
		 * after the \). If we copy a nul, then immediately stop the 
		 * process here!
		 */
		if ((*p = *s) == 0)
			break;
	    }

	    /* 
	     * Always copy any non-slash character.
	     * Stop when we reach the nul.
	     */
	    else
		if ((*p = *s) == 0)
			break;
	}

	/*
	 * We're done!  Append 'workbuf' to 'dest'.
	 */
	malloc_strcat_c(dest, workbuf, cluep);
	return *dest;
}

/*
 * normalize_filename: replacement for expand_twiddle
 *
 * This is a front end to realpath(), has the same signature, except
 * it expands twiddles for me, and it returns 0 or -1 instead of (char *).
 */
int	normalize_filename (const char *str, Filename result)
{
	Filename workpath;
	char *	pathname;
	char *	rest;
	struct	passwd *entry;

	if (*str == '~')
	{
		/* make a copy of the path we can make changes to */
		pathname = LOCAL_COPY(str + 1);

		/* Stop the tilde-expansion at the first / (or nul) */
		if ((rest = strchr(pathname, '/')))
			*rest++ = 0;

		/* Expand ~ to our homedir, ~user to user's homedir */
		if (*pathname) {
			if ((entry = getpwnam(pathname)) == NULL) {
			    snprintf(result, PATH_MAX + 1, "~%s", pathname);
			    return -1;
			}
			strlcpy(workpath, entry->pw_dir, sizeof(workpath));
		} else
			strlcpy(workpath, my_path, sizeof(workpath));

		/* And tack on whatever is after the first / */
		if (rest)
		{
			strlcat(workpath, "/", sizeof(workpath));
			strlcat(workpath, rest, sizeof(workpath));
		}

		str = workpath;
	}

/* I don't understand the point of this code. */
#if 0
        /* yes, these are redundant, but what if people */
        /* have directories with .zip in them? */
        if (((stristr(str, ".zip"))!=-1) || ((stristr(str, ".tar"))!=-1)) {
            if (file_exists(str)) {
                snprintf(result, PATH_MAX + 1, "%s", str);
                return 0;
            }
        }
#endif

	if (realpath(str, result) == NULL)
		return -1;

	return 0;
}

/* 
 * expand_twiddle: expands ~ in pathnames.
 *
 * XXX WARNING XXX 
 *
 * It is perfectly valid for (str == *result)!  You must NOT change
 * '*result' until you have first copied 'str' to 'buffer'!  If you
 * do not do this, you will corrupt the result!  You have been warned!
 */
int	expand_twiddle (const char *str, Filename result)
{
	Filename buffer;
	char	*rest;
	struct	passwd *entry;

	/* Handle filenames without twiddles to expand */
	if (*str != '~')
	{
		/* Only do the copy if the destination is not the source */
		if (str != result)
			strlcpy(result, str, PATH_MAX + 1);
		return 0;
	}

	/* Handle filenames that are just ~ or ~/... */
	str++;
	if (!*str || *str == '/')
	{
		strlcpy(buffer, my_path, sizeof(buffer));
		strlcat(buffer, str, sizeof(buffer));

		strlcpy(result, buffer, PATH_MAX + 1);
		return 0;
	}

	/* Handle filenames that are ~user or ~user/... */
	if ((rest = strchr(str, '/')))
		*rest++ = 0;
	if ((entry = getpwnam(str)))
	{
		strlcpy(buffer, entry->pw_dir, sizeof(buffer));
		if (rest)
		{
			strlcat(buffer, "/", sizeof(buffer));
			strlcat(buffer, rest, sizeof(buffer));
		}

		strlcpy(result, buffer, PATH_MAX + 1);
		return 0;
	}

	return -1;
}

/* islegal: true if c is a legal nickname char anywhere but first char */
#define islegal(c) ((((c) >= 'A') && ((c) <= '}')) || \
		    (((c) >= '0') && ((c) <= '9')) || \
		     ((c) == '-') || ((c) == '_'))

/*
 * check_nickname: checks is a nickname is legal.  If the first character is
 * bad, null is returned.  If the first character is not bad, the string is
 * truncated down to only legal characters and returned 
 *
 * rewritten, with help from do_nick_name() from the server code (2.8.5),
 * phone, april 1993.
 */
char *	check_nickname (char *nick, int unused)
{
	char	*s;

	/* Generally we should always accept "0" as a nickname */
	if (nick && nick[0] == '0' && nick[1] == 0)
		return nick;

	/* IRCNet nicknames can start with numbers now. (*gulp*) */
	if (!nick || *nick == '-' || isdigit(*nick))
		return NULL;

	for (s = nick; *s && (s - nick) < NICKNAME_LEN; s++)
		if (!islegal(*s) || my_isspace(*s))
			break;
	*s = 0;

	return *nick ? nick : NULL;
}

#if 0
/*
 * sindex: much like index(), but it looks for a match of any character in
 * the group, and returns that position.  If the first character is a ^, then
 * this will match the first occurence not in that group.
 *
 * XXXX - sindex is a lot like strpbrk(), which is standard
 */
char *	sindex (char *string, const char *group)
{
	const char	*ptr;

	if (!string || !group)
		return (char *) NULL;
	if (*group == '^')
	{
		group++;
		for (; *string; string++)
		{
			for (ptr = group; *ptr; ptr++)
			{
				if (*ptr == *string)
					break;
			}
			if (*ptr == '\0')
				return string;
		}
	}
	else
	{
		for (; *string; string++)
		{
			for (ptr = group; *ptr; ptr++)
			{
				if (*ptr == *string)
					return string;
			}
		}
	}
	return (char *) NULL;
}

/*
 * rsindex: much like rindex(), but it looks for a match of any character in
 * the group, and returns that position.  If the first character is a ^, then
 * this will match the first occurence not in that group.
 */
char *	rsindex (char *string, char *start, char *group, int howmany)
{
	char	*ptr;

	if (howmany && string && start && group && start <= string)
	{
		if (*group == '^')
		{
			group++;
			for (ptr = string; (ptr >= start) && howmany; ptr--)
			{
				if (!strchr(group, *ptr))
				{
					if (--howmany == 0)
						return ptr;
				}
			}
		}
		else
		{
			for (ptr = string; (ptr >= start) && howmany; ptr--)
			{
				if (strchr(group, *ptr))
				{
					if (--howmany == 0)
						return ptr;
				}
			}
		}
	}
	return NULL;
}
#endif

/*
 * cpindex - Find the 'howmany'th instance of any of the codepoints in 'search'
 *	     and return a pointer to that place; and calculate the character
 *	     offset from the front of the string (for use with $mid())
 *
 * Arguments:
 *	string	- A UTF8 string to be searched
 *	search	- One or more code points to look for in 'string'
 *		  if search[0] is ^, then the string is inverted.
 *	howmany	- Return the 'howmany'th match (usually 1)
 *	cpoffset - Returns which codepoint in 'string' has the found character.
 *
 * Return Value:
 *	The location of the characters. if found.
 *	NULL if the character(s) are not found.
 *	'cpoffset' is unchanged if not found.
 */
const unsigned char *	cpindex (const unsigned char *string, const unsigned char *search, int howmany, size_t *cpoffset)
{
	const unsigned char *s, *p;
	int	c, d;
	int	found = 0;
	int	inverted = 0;

	if (*search == '^')
	{
		inverted = 1;
		search++;
	}

	p = string;
	while ((c = next_code_point(&p, 1)))
	{
		/* 
		 * This code point 'c' is "found" IF AND ONLY IF:
		 *	inverted == 1:	for every d in search, x != c
		 *	inverted == 0:	for any d in search, x = c
		 */
		if (inverted)
		{
			s = search;
			while ((d = next_code_point(&s, 1)))
			{
				/* If any d == c, this 'c' is NOT FOUND */
				if (c == d)
					goto cpindex_not_found;
			}
			/* If we reach this point, 'c' was FOUND -- FALLTHROUGH */
			goto cpindex_found;
		}
		else
		{
			s = search;
			while ((d = next_code_point(&s, 1)))
			{
				/* If any d == c, this 'c' is FOUND */
				if (c == d)
					goto cpindex_found;
			}
			/* If we reach this point, 'c' was NOT FOUND */
			continue;
		}

cpindex_found:
		/* If we reach this point, 'c' was FOUND */
		if (++found >= howmany)
		{
			previous_code_point(string, &p);
			*cpoffset = quick_code_point_index(string, p);
			return p;
		}
		else
			continue;

cpindex_not_found:
		/* If we reach this point 'c' was NOT FOUND */;
	}

	return NULL;
}

const unsigned char *	rcpindex (const unsigned char *where, const unsigned char *string, const unsigned char *search, int howmany, size_t *cpoffset)
{
	const unsigned char *s, *p;
	int	c, d;
	int	found = 0;
	int	inverted = 0;

	if (*search == '^')
	{
		inverted = 1;
		search++;
	}

	p = where;
	while ((c = previous_code_point(string, &p)))
	{
		/* 
		 * This code point 'c' is "found" IF AND ONLY IF:
		 *	inverted == 1:	for every d in search, x != c
		 *	inverted == 0:	for any d in search, x = c
		 */
		if (inverted)
		{
			s = search;
			while ((d = next_code_point(&s, 1)))
			{
				/* If any d == c, this 'c' is NOT FOUND */
				if (c == d)
					goto rcpindex_not_found;
			}
			/* If we reach this point, 'c' was FOUND -- FALLTHROUGH */
			goto rcpindex_found;
		}
		else
		{
			s = search;
			while ((d = next_code_point(&s, 1)))
			{
				/* If any d == c, this 'c' is FOUND */
				if (c == d)
					goto rcpindex_found;
			}
			/* If we reach this point, 'c' was NOT FOUND */
			continue;
		}

rcpindex_found:
		/* If we reach this point, 'c' was FOUND */
		if (++found >= howmany)
		{
			*cpoffset = quick_code_point_index(string, p);
			return p;
		}
		else
			continue;

rcpindex_not_found:
		/* If we reach this point 'c' was NOT FOUND */;
	}

	return NULL;
}


/* is_number: returns true if the given string is a number, false otherwise */
int	is_number (const char *str)
{
	if (!str || !*str)
		return 0;

	while (*str && isspace(*str))
		str++;

	if (*str == '-')
		str++;

	if (*str)
	{
		for (; *str; str++)
		{
			if (!isdigit((*str)))
				return (0);
		}
		return 1;
	}
	else
		return 0;
}

/* is_number: returns 1 if the given string is a real number, 0 otherwise */
int	is_real_number (const char *str)
{
	int	period = 0;

	if (!str || !*str)
		return 0;

	while (*str && isspace(*str))
		str++;

	if (*str == '-')
		str++;

	if (!*str)
		return 0;

	for (; *str; str++)
	{
		if (isdigit((*str)))
			continue;

		if (*str == '.' && period == 0)
		{
			period = 1;
			continue;
		}

		break;
	}

	/* Check for trailing spaces */
	while (*str && isspace(*str))
		str++;

	/* If we don't end up at the end of the string, it's not a number. */
	if (*str)
		return 0;

	return 1;
}

/*
 * path_search: given a file called name, this will search each element of
 * the given path to locate the file.  If found in an element of path, the
 * full path name of the file is returned in a static string.  If not, null
 * is returned.  Path is a colon separated list of directories 
 */
int	path_search (const char *name, const char *xpath, Filename result)
{
	Filename buffer;
	Filename candidate;
	char	*path;
	char	*ptr;

	/* No Path -> Error */
	if (!xpath)
		return -1;		/* Take THAT! */

	/*
	 * A "relative" path is valid if the file exists
	 * based on the current directory.  If it does not
	 * exist from the current directory, then we will
	 * search the path for it.
	 *
	 * PLEASE NOTE this catches things like ~foo/bar too!
	 */
	if (strchr(name, '/'))
	{
	    if (!normalize_filename(name, candidate))
	    {
		if (file_exists(candidate))
		{
			strlcpy(result, candidate, PATH_MAX + 1);
			return 0;
		}
	    }
	}

/* This test looks really bogus.  The above test catches all these cases. */
#if 0
	/* 
	 * There are three exceptions to path searching:
	 * 1) absolute pathnames: /path/to/file, which start with "/"
         * 2) Homedir pathnames: ~user/path/to/file, which start with "~"
	 * 3) ``absolute'' relatives: ./path/to/file or ../path/to, which
	 *	start with "."
	 * In any of these three cases, we do not do path searching!
	 */
	if (*name == '.' || *name == '/' || *name == '~')
		return -1;
#endif

	*result = 0;
	for (path = LOCAL_COPY(xpath); path; path = ptr)
	{
		if ((ptr = strchr(path, ':')))
			*ptr++ = 0;

		snprintf(buffer, sizeof(buffer), "%s/%s", path, name);
		if (normalize_filename(buffer, candidate))
			continue;

		if (file_exists(candidate)) {
			strlcpy(result, candidate, PATH_MAX + 1);
			return 0;
		}
	}

	return -1;
}

/*
 * double_quote: Given a str of text, this will quote any character in the
 * set stuff with the QUOTE_CHAR.  You have to pass in a buffer thats at 
 * least twice the size of 'str' (in case every character is quoted.)
 * "output" is returned for your convenience.
 */
char *	double_quote (const char *str, const char *stuff, char *buffer)
{
	char	c;
	int	pos;
	int	everything = 0;

	*buffer = 0;		/* Whatever */

	if (!stuff)
		return buffer;	/* Whatever */
	if (*stuff == '*')		/* QUOTE ALL THE THINGS! */
		everything = 1;

	/* For every character... */
	for (pos = 0; (c = *str); str++)
	{
		/* Only 7 bit characters need quoting */
		if ((c & 0x80) == 0x00)
		{
			/* 
			 * If it's not a letter or digit, and 'everything',
			 * or if it's explicitly included in the list...
			 * quote it.
			 */
			if ((everything && !isalpha(c) && !isdigit(c))
				||
			    (strchr(stuff, c)))
			{
				if (c == '$')
					buffer[pos++] = '$';
				else
					buffer[pos++] = '\\';
			}
		}
		buffer[pos++] = c;
	}
	buffer[pos] = '\0';
	return buffer;
}

void	panic (int quitmsg, const char *format, ...)
{
	char buffer[BIG_BUFFER_SIZE * 10 + 1];
static	int recursion = 0;		/* Recursion is bad */

	if (recursion)
		abort();

	recursion = 1;
	if (format)
	{
		va_list arglist;
		va_start(arglist, format);
		vsnprintf(buffer, BIG_BUFFER_SIZE * 10, format, arglist);
		va_end(arglist);
	}

	term_reset();
	fprintf(stderr, "An unrecoverable logic error has occurred.\n");
	fprintf(stderr, "Please fill out the BUG_FORM file, and include the following message:\n");
	fprintf(stderr, "Panic: [%s (%lu):%s]\n", irc_version, commit_id, buffer);
	panic_dump_call_stack();

	if (quitmsg == 0)
		strlcpy(buffer, "Ask user for panic message.", sizeof(buffer));

	if (x_debug & DEBUG_CRASH)
		irc_exit(0, "Panic: epic5-%lu:%s", commit_id, buffer);
	else
		irc_exit(1, "Panic: epic5-%lu:%s", commit_id, buffer);
}

/* beep_em: Not hard to figure this one out */
void	beep_em (int beeps)
{
	int	cnt,
		i;

	for (cnt = beeps, i = 0; i < cnt; i++)
		term_beep();
}

/* Not really complicated, but a handy function to have */
int 	end_strcmp (const char *val1, const char *val2, size_t bytes)
{
	if (bytes < strlen(val1))
		return (strcmp(val1 + strlen(val1) - (size_t) bytes, val2));
	else
		return -1;
}

/*
 * exec a program, given its arguments and input, return its entire output.
 * on call, *len is the length of input, and on return, it is set to the
 * length of the data returned.
 *
 * Reading is done more agressively than writing to keep the buffers
 * clean, and the data flowing.
 *
 * Potential Bugs:
 *   - If the program in question locks up for any reason, so will epic.
 *     This can be fixed with an appropriate timeout in the select call.
 *   - If the program in question outputs enough data, epic will run out
 *     of memory and dump core.
 *   - Although input and the return values are char*'s, they are only
 *     treated as blocks of data, the size of *len.
 *
 * Special Note: stdin and stdout are not expected to be textual.
 */
char *	exec_pipe (const char *executable, char *input, size_t *len, char * const *args)
{
#ifdef NO_JOB_CONTROL
	yell("Your system does not support job control");
	return NULL;
#else
	int 	pipe0[2] = {-1, -1};
	int 	pipe1[2] = {-1, -1};
	pid_t	pid;
	char *	ret = NULL;
	size_t	retlen = 0, rdpos = 0, wrpos = 0;
	fd_set	rdfds, wrfds;
	int	fdmax;

	if (pipe(pipe0) || pipe(pipe1))
	{
		yell("Cannot open pipes for %s: %s",
				executable, strerror(errno));
		close(pipe0[0]);
		close(pipe0[1]);
		close(pipe1[0]);
		close(pipe1[1]);
		return ret;
	}

	switch (pid = fork())
	{
	case -1:
		yell("Cannot fork for %s: %s", 
				executable, strerror(errno));
		close(pipe0[0]);
		close(pipe0[1]);
		close(pipe1[0]);
		close(pipe1[1]);
		return ret;
	case 0:
		dup2(pipe0[0], 0);
		dup2(pipe1[1], 1);
		close(pipe0[1]);
		close(pipe1[0]);
		close(2);	/* we dont want to see errors yet */
		if (setgid(getgid()))
			exit(0);
		if (setuid(getuid()))
			exit(0);
		execvp(executable, args);
		_exit(0);
	default :
		close(pipe0[0]);
		close(pipe1[1]);
		FD_ZERO(&rdfds);
		FD_ZERO(&wrfds);
		FD_SET(pipe1[0], &rdfds);
		FD_SET(pipe0[1], &wrfds);
		fdmax = 1 + MAX(pipe1[0], pipe0[1]);
		for (;;) {
			fd_set RDFDS = rdfds;
			fd_set WRFDS = wrfds;
			int foo;
			foo = select(fdmax, &RDFDS, &WRFDS, NULL, NULL);
			if (-1 == foo) {
				yell("Broken select call: %s", strerror(errno));
				if (EINTR == errno)
					continue;
				break;
			} else if (0 == foo) {
				break;
			}
			if (FD_ISSET(pipe1[0], &RDFDS)) {
				retlen = rdpos + 4096;
				new_realloc((void**)&ret, retlen);
				foo = read(pipe1[0], ret+rdpos, retlen-rdpos);
				if (0 == foo)
					break;
				else if (0 < foo)
					rdpos += foo;
			} else if (FD_ISSET(pipe0[1], &WRFDS)) {
				if (input && wrpos < *len)
					foo = write(pipe0[1], input+wrpos, MIN(512, *len-wrpos));
				else {
					FD_CLR(pipe0[1], &wrfds);
					close(pipe0[1]);
				}
				if (0 < foo)
					wrpos += foo;
			}
		}
		close(pipe0[1]);
		close(pipe1[0]);
		waitpid(pid, NULL, WNOHANG);
		new_realloc((void**)&ret, rdpos);
		break;
	}
	*len = rdpos;
	return ret;
#endif
}

/*
 * exec() something and return three FILE's.
 *
 * On failure, close everything and return NULL.
 */
FILE **	open_exec (const char *executable, char * const *args)
{
#ifdef NO_JOB_CONTROL
	yell("Your system does not support job control");
	return NULL;
#else
static	FILE *	file_pointers[3];
	int 	pipe0[2] = {-1, -1};
	int 	pipe1[2] = {-1, -1};
	int 	pipe2[2] = {-1, -1};

	if (pipe(pipe0) == -1 || pipe(pipe1) == -1 || pipe(pipe2) == -1)
	{
		yell("Cannot open exec pipes: %s\n", strerror(errno));
		close(pipe0[0]);
		close(pipe0[1]);
		close(pipe1[0]);
		close(pipe1[1]);
		close(pipe2[0]);
		close(pipe2[1]);
		return NULL;
	}

	switch (fork())
	{
		case -1:
		{
			yell("Cannot fork for exec: %s\n", 
					strerror(errno));
			close(pipe0[0]);
			close(pipe0[1]);
			close(pipe1[0]);
			close(pipe1[1]);
			close(pipe2[0]);
			close(pipe2[1]);
			return NULL;
		}
		case 0:
		{
			dup2(pipe0[0], 0);
			dup2(pipe1[1], 1);
			dup2(pipe2[1], 2);
			close(pipe0[1]);
			close(pipe1[0]);
			close(pipe2[0]);
			if (setgid(getgid()))
				exit(0);
			if (setuid(getuid()))
				exit(0);
			execvp(executable, args);
			_exit(0);
		}
		default :
		{
			close(pipe0[0]);
			close(pipe1[1]);
			close(pipe2[1]);
			if (!(file_pointers[0] = fdopen(pipe0[1], "w")))
			{
				yell("Cannot open exec STDIN: %s\n", 
						strerror(errno));
				close(pipe0[1]);
				close(pipe1[0]);
				close(pipe2[0]);
				return NULL;
			}
			if (!(file_pointers[1] = fdopen(pipe1[0], "r")))
			{
				yell("Cannot open exec STDOUT: %s\n", 
						strerror(errno));
				fclose(file_pointers[0]);
				close(pipe1[0]);
				close(pipe2[0]);
				return NULL;
			}
			if (!(file_pointers[2] = fdopen(pipe2[0], "r")))
			{
				yell("Cannot open exec STDERR: %s\n", 
						strerror(errno));
				fclose(file_pointers[0]);
				fclose(file_pointers[1]);
				close(pipe2[0]);
				return NULL;
			}
			break;
		}
	}
	return file_pointers;
#endif
}

static struct epic_loadfile *	open_compression (char *executable, char *filename)
{
#ifdef NO_JOB_CONTROL
	yell("Your system does not support job control");
	return NULL;
#else
	struct epic_loadfile *	elf;
	int 	pipes[2] = {-1, -1};

	elf = (struct epic_loadfile *) new_malloc(sizeof(struct epic_loadfile));
        if (pipe(pipes) == -1)
	{
		yell("Cannot start decompression: %s\n", strerror(errno));
		if (pipes[0] != -1)
		{
			close(pipes[0]);
			close(pipes[1]);
		}
		new_free(&elf);
		return NULL;
	}

	switch (fork())
	{
		case -1:
		{
			yell("Cannot start decompression: %s\n", 
					strerror(errno));
			new_free(&elf);
			return NULL;
		}
		case 0:
		{
			dup2(pipes[1], 1);
			close(pipes[0]);
			close(2);	/* we dont want to see errors */
			if (setgid(getgid()))
				exit(0);
			if (setuid(getuid()))
				exit(0);

			/* 
			 * 'compress', 'uncompress, 'gzip', 'gunzip',
			 * 'bzip2' and 'bunzip'2 on my system all support
			 * the -d option reasonably.  I hope they do
			 * elsewhere. :d
			 */
			execl(executable, executable, "-d", "-c", filename, NULL);
			_exit(0);
		}
		default :
		{
			close(pipes[1]);
			if (!(elf->fp = fdopen(pipes[0], "r")))
			{
				yell("Cannot start decompression: %s\n", 
						strerror(errno));
				new_free(&elf);
				close(pipes[0]);
				return NULL;
			}
			break;
		}
	}
	return elf;
#endif
}

/*
 * Front end to fopen() that will open ANY file, compressed or not, and
 * is relatively smart about looking for the possibilities, and even
 * searches a path for you! ;-)
 *
 * NOTICE -- 'filename' is an input/output parameter.  On input, it must
 * be a malloc()ed string containing a file to open.  On output, it will
 * be changed to point to the actual filename that was opened.  THE ORIGINAL
 * INPUT VALUE IS ALWAYS FREE()D IN EVERY CIRCUMSTANCE.  IT WILL BE REPLACED
 * WITH A NEW VALUE (ie, the variable will be changed) UPON RETURN.  You must
 * not save the original value of '*filename' and use it after calling uzfopen.
 */
struct epic_loadfile *	uzfopen (char **filename, const char *path, int do_error, struct stat *sb)
{
static int		setup				= 0;
static 	Filename 	path_to_gunzip;
static	Filename 	path_to_uncompress;
static 	Filename 	path_to_bunzip2;
	int 		ok_to_decompress 		= 0;
	Filename	fullname;
	Filename	candidate;

        struct epic_loadfile * elf;

        if (!setup)
	{
		*path_to_gunzip = 0;
		path_search("gunzip", getenv("PATH"), path_to_gunzip);

		*path_to_uncompress = 0;
		path_search("uncompress", getenv("PATH"), path_to_uncompress);

		*path_to_bunzip2 = 0;
		if (path_search("bunzip2", getenv("PATH"), path_to_bunzip2))
		    path_search("bunzip", getenv("PATH"), path_to_bunzip2);

		setup = 1;
	}

	/* I should probably emit an error here */
	if (!filename || !*filename)
		return NULL;

	/* 
	 * It is allowed to pass to this function either a true filename
	 * with the compression extention, or to pass it the base name of
	 * the filename, and this will look to see if there is a compressed
	 * file that matches the base name 
	 */

	/* 
	 * Start with what we were given as an initial guess 
	 * kev asked me to call expand_twiddle here once,
	 * Now that path_search() does ~'s, we don't need to do
	 * so here any more.
	 */

	/* 
	 * Look to see if the passed filename is a full compressed filename 
	 */
	if ((!end_strcmp(*filename, ".gz", 3)) ||
	    (!end_strcmp(*filename, ".z", 2))) 
        {
            /* these are handled differently */
            if (end_strcmp(*filename, ".tar.gz", 7)) {
                if (!*path_to_gunzip)
                {
                    if (do_error)
                            yell("Cannot open file %s because gunzip "
                                     "was not found", *filename);
                        goto error_cleanup;
                }
                ok_to_decompress = 2;
            }
            if (path_search(*filename, path, fullname))
                goto file_not_found;

	}
	else if (!end_strcmp(*filename, ".Z", 2))
	{
		if (!*path_to_gunzip && !*path_to_uncompress)
		{
			if (do_error)
				yell("Cannot open file %s becuase uncompress "
					"was not found", *filename);
			goto error_cleanup;
		}

		ok_to_decompress = 1;
		if (path_search(*filename, path, fullname))
			goto file_not_found;
	}
	else if (!end_strcmp(*filename, ".bz2", 4))
	{
		if (!*path_to_bunzip2)
		{
			if (do_error)
				yell("Cannot open file %s because bunzip "
					"was not found", *filename);
			goto error_cleanup;
		}

		ok_to_decompress = 3;
		if (path_search(*filename, path, fullname))
			goto file_not_found;
	}

	/* Right now it doesnt look like the file is a full compressed fn */
	else
	{
	    do
	    {
		/* Trivially, see if the file we were passed exists */
		if (!path_search(*filename, path, fullname)) {
			ok_to_decompress = 0;
			break;
		}

		/* Is there a "filename.gz"? */
		snprintf(candidate, sizeof(candidate), "%s.gz", *filename);
		if (!path_search(candidate, path, fullname)) {
			ok_to_decompress = 2;
			break;
		}

		/* Is there a "filename.Z"? */
		snprintf(candidate, sizeof(candidate), "%s.Z", *filename);
		if (!path_search(candidate, path, fullname)) {
			ok_to_decompress = 1;
			break;
		}

		/* Is there a "filename.z"? */
		snprintf(candidate, sizeof(candidate), "%s.z", *filename);
		if (!path_search(candidate, path, fullname)) {
			ok_to_decompress = 2;
			break;
		}

		/* Is there a "filename.bz2"? */
		snprintf(candidate, sizeof(candidate), "%s.bz2", *filename);
		if (!path_search(candidate, path, fullname)) {
			ok_to_decompress = 3;
			break;
		}

		goto file_not_found;
	    }
	    while (0);

            if (epic_stat(fullname, sb) < 0)
            {
                if (do_error)
                    yell("%s could not be accessed", fullname);
                goto error_cleanup;
            }

            if (S_ISDIR(sb->st_mode))
            {
                if (do_error)
                    yell("%s is a directory", fullname);
                goto error_cleanup;
            }
        }

        /*
         * At this point, we should have a filename in the variable
         * *filename, and it should exist.  If ok_to_decompress is one, then
         * we can gunzip the file if gunzip is available.  else we
         * uncompress the file.
	 */
	malloc_strcpy(filename, fullname);
	if (ok_to_decompress)
	{
		     if ((ok_to_decompress <= 2) && *path_to_gunzip)
			return open_compression(path_to_gunzip, *filename);
		else if ((ok_to_decompress == 1) && *path_to_uncompress)
			return open_compression(path_to_uncompress, *filename);
		else if ((ok_to_decompress == 3) && *path_to_bunzip2)
			return open_compression(path_to_bunzip2, *filename);

		if (do_error)
			yell("Cannot open compressed file %s becuase no "
				"uncompressor was found", *filename);
		goto error_cleanup;
	} else {
            if ((elf=epic_fopen(*filename, "r", do_error))) {
                return elf;
            }
        }

	goto error_cleanup;

file_not_found:
	if (do_error)
		yell("File not found: %s", *filename);

error_cleanup:
	new_free(filename);
	return NULL;
}


int 	fw_strcmp (comp_len_func *compar, char *v1, char *v2)
{
	int len = 0;
	const char *pos = one;

	while (!my_isspace(*pos))
		pos++, len++;

	return compar(v1, v2, len);
}



/* 
 * Compares the last word in 'one' to the string 'two'.  You must provide
 * the compar function.  my_stricmp is a good default.
 */
int 	lw_strcmp(comp_func *compar, char *val1, char *val2)
{
	char *pos = val1 + strlen(val1) - 1;

	if (pos > val1)			/* cant do pos[-1] if pos == val1 */
		while (!my_isspace(pos[-1]) && (pos > val1))
			pos--;
	else
		pos = val1;

	if (compar)
		return compar(pos, val2);
	else
		return my_stricmp(pos, val2);
}

/* 
 * you give it a filename, some flags, and a position, and it gives you an
 * fd with the file pointed at the 'position'th byte.
 */
int 	opento(const char *filename, int flags, off_t position)
{
	int file;

	file = open(filename, flags, 777);
	lseek(file, position, SEEK_SET);
	return file;
}


/* swift and easy -- returns the size of the file */
off_t 	file_size (const char *filename)
{
	Stat statbuf;

	if (!epic_stat(filename, &statbuf))
		return (off_t)(statbuf.st_size);
	else
		return -1;
}

/*
 * file_exists		Returns 1 if "filename" exists
 *	In order to support ~-expansion, we call normalize_filename.
 *
 *  Input parameter:
 *	filename	A filename, either a fully qualified pathname,
 *			or a relative pathname.
 *  Returns:
 *	1 if 'filename' is something that can be elf_open()ed.
 *	0 if 'filename' is not openable.
 */
int	file_exists (const char *filename)
{
	Filename	expanded;

        if (normalize_filename(filename, expanded))
		return 0;

	if (file_size(expanded) == -1)
		return 0;
	else
		return 1;
}

int	isdir (const char *filename)
{
	Stat statbuf;

	if (!stat(filename, &statbuf))
	{
	    if (S_ISDIR(statbuf.st_mode))
			return 1;
	}
	return 0;
}

int	isdir2 (const char *directory, const void * const ent)
{
	Filename	f;
	const struct dirent * const d = (struct dirent *)ent;

#if defined(DT_DIR)
	/* if dirent.h supports d_type and it is a directory or regular file, return */
	if (d->d_type == DT_DIR)
		return 1;
	if (d->d_type == DT_REG) 
		return 0;
#endif

	snprintf(f, sizeof f, "%s/%s", directory, d->d_name);
	return isdir(f);
}


struct metric_time	timeval_to_metric (const Timeval *tv)
{
	struct metric_time retval;
	double	my_timer;
	long	sec;

	retval.mt_days = tv->tv_sec / 86400;
	sec = tv->tv_sec % 86400;		/* Seconds after midnight */
	sec = sec * 1000;			/* Convert to ms */
	sec += (tv->tv_usec / 1000);		/* Add ms fraction */
	my_timer = (double)sec / 86400.0;	/* Convert to millidays */
	retval.mt_mdays = my_timer;
	return retval;
}

struct metric_time	get_metric_time (double *timer)
{
	Timeval	tv;
	struct metric_time mt;

	get_time(&tv);
	mt = timeval_to_metric(&tv);
	if (timer)
		*timer = mt.mt_mdays;
	return mt;
}


/* Gets the time in second/usecond if you can,  second/0 if you cant. */
Timeval get_time (Timeval *timer)
{
	static Timeval retval;

	/* Substitute a dummy timeval if we need one. */
	if (!timer)
		timer = &retval;

	{
#ifdef HAVE_CLOCK_GETTIME
		struct timespec ts;
		clock_gettime(CLOCK_REALTIME, &ts);
		timer->tv_sec = ts.tv_sec;
		timer->tv_usec = ts.tv_nsec / 1000;
#else
# ifdef HAVE_GETTIMEOFDAY
		gettimeofday(timer, NULL);
# else
		timer->tv_sec = time(NULL);
		timer->tv_usec = 0;
# endif
#endif
	}

	return *timer;
}

/* 
 * calculates the time elapsed between 't1' and 't2' where they were
 * gotten probably with a call to get_time.  't1' should be the older
 * timer and 't2' should be the most recent timer.
 */
double 	time_diff (const Timeval t1, const Timeval t2)
{
	Timeval td;

	td.tv_sec = t2.tv_sec - t1.tv_sec;
	td.tv_usec = t2.tv_usec - t1.tv_usec;

	return (double)td.tv_sec + ((double)td.tv_usec / 1000000.0);
}

Timeval double_to_timeval (double x)
{
	Timeval td;
	time_t	s;

	s = (time_t) x;
	x = x - s;
	x = x * 1000000;

	td.tv_sec = s;
	td.tv_usec = (long) x;
	return td;
}

/* 
 * calculates the time elapsed between 'one' and 'two' where they were
 * gotten probably with a call to get_time.  'one' should be the older
 * timer and 'two' should be the most recent timer.
 */
Timeval time_subtract (const Timeval t1, const Timeval t2)
{
	Timeval td;

	td.tv_sec = t2.tv_sec - t1.tv_sec;
	td.tv_usec = t2.tv_usec - t1.tv_usec;
	if (td.tv_usec < 0)
	{
		td.tv_usec += 1000000;
		td.tv_sec--;
	}
	return td;
}

/* 
 * Adds the interval "two" to the base time "one" and returns it.
 */
Timeval time_add (const Timeval t1, const Timeval t2)
{
	Timeval td;

	td.tv_usec = t1.tv_usec + t2.tv_usec;
	td.tv_sec = t1.tv_sec + t2.tv_sec;
	if (td.tv_usec >= 1000000)
	{
		td.tv_usec -= 1000000;
		td.tv_sec++;
	}
	return td;
}


const char *	plural (int number)
{
	return (number != 1) ? "s" : empty_string;
}

char ctime_failure[] = "ctime failed";
const char *	my_ctime (time_t when)
{
	char *x;

	if ((x = ctime(&when)))
		chomp(x);
	else
		x = ctime_failure;

	return x;
}


const char *	my_ltoa (long foo)
{
	static char buffer[BIG_BUFFER_SIZE + 1];
	char *pos = buffer + BIG_BUFFER_SIZE - 1;
	unsigned long absv;
	int negative;

	absv = (foo < 0) ? (unsigned long)-foo : (unsigned long)foo;
	negative = (foo < 0) ? 1 : 0;

	buffer[BIG_BUFFER_SIZE] = 0;
	for (; absv > 9; absv /= 10)
		*pos-- = (absv % 10) + '0';
	*pos = (absv) + '0';

	if (negative)
		*--pos = '-';

	return pos;
}

const char *	intmaxtoa (intmax_t foo)
{
	static char buffer[BIG_BUFFER_SIZE + 1];
	char *	    pos = buffer + BIG_BUFFER_SIZE - 1;
	uintmax_t   absv;
	int	    negative;

	absv = (foo < 0) ? (uintmax_t)-foo : (uintmax_t)foo;
	negative = (foo < 0) ? 1 : 0;

	buffer[BIG_BUFFER_SIZE] = 0;
	for (; absv > 9; absv /= 10)
		*pos-- = (absv % 10) + '0';
	*pos = (absv) + '0';

	if (negative)
		*--pos = '-';

	return pos;
}

const char *	ftoa (double foo)
{
	static char buffer [BIG_BUFFER_SIZE + 1];
	extern double fmod (double, double);

	if (get_int_var(FLOATING_POINT_MATH_VAR)) {
		snprintf(buffer, sizeof buffer, "%.*g", 
			get_int_var(FLOATING_POINT_PRECISION_VAR), foo);
	} else {
		foo -= fmod(foo, 1);
		snprintf(buffer, sizeof buffer, "%.0f", foo);
	}
	return buffer;
}

/*  
 * This function is bad -- it does not correctly count columns,
 * You should use:
 *	dest = fix_string_width(src, "left", pad, length);
 */
#if 0
/*
 * strformat - Return 'src' so it is 'length' columns wide.
 *
 * Arguments:
 *	dest	- Where to return results. should be 6 times size of 'src'.
 *	src	- A UTF8 string
 *	length	- How many columns 'src' should take up
 *		  If > 0, src is left justified
 *		  If 0, error (dest is unchanged)
 *		  If < 0, src is right justified
 *	pad	- If 'src' is not 'length' columns wide, use this code
 *		  point to pad it out.
 *
 * Note:
 *	If 'src' is greater than 'length', then it is TRUNCATED.
 *	If 'src' is less than 'length' then it will be left or right
 *	padded with 'pad', (which should be a 1 column code point)
 */
char *	strformat (char *dest, size_t destlen, const unsigned char *src, ssize_t length, int pad)
{
	int	srclen;		/* How many columns 'src' is */
	int	srcuse;		/* How many columns filled by 'src' */
	int	paduse;		/* How many columns filled by 'pad' */
	char	padutf8[8];	/* 'pad' as a utf8 string */
	int	padlen;		/* How many columns padutf8 takes */
	int	padreps;	/* How many padutf8's we need in result */
	int	i;

	/* How many columns does the source string take up? */
	srclen = quick_display_column_count(src);

	/*
	 * Figure out how many columns to use from 'src'
	 * and how many columns to use from 'pad'
	 */
	/* Right Justify */
	if (length < 0)	
	{
		if (-length < srclen)
		{
			srcuse = -length;		/* Truncate! */
			paduse = 0;
		}
		else
		{
			srcuse = srclen;
			paduse = length + srclen;	/* Negative! */
		}
	}

	/* This is nonsense - just cut it short */
	else if (length == 0)
	{
		*dest = 0;
		return dest;
	}

	/* Left Justify */
	else
	{
		if (length < srclen)
		{
			srcuse = length;		/* Truncate! */
			paduse = 0;
		}
		else
		{
			srcuse = srclen;
			paduse = length - srclen;	/* Positive! */
		}
	}

	ucs_to_utf8(pad, padutf8, sizeof(padutf8));
	if ((padlen = quick_display_column_count(padutf8)) == 0)
	{
		strlcpy(padutf8, " ", sizeof(padutf8));
		padlen = 1;
	}
	padreps = paduse / padlen;


	/* 
	 * Create the result.
	 * 'padreps' has the sign of 'length' (< 0 Left Justify, > 0 Right)
	 * and it is the difference of 'length' and columns of 'src'.
	 * So if 'length' is shorter than 'src', padreps is 0.
	 */
	*dest = 0;
	for (i = padreps; i < 0; i++)
		strlcat(dest, padutf8, destlen);
	while (srcuse > 0)
	{
		int	codepoint;
		int	cols;
		char	utf8str[16];
		char	utf8strlen;

		codepoint = next_code_point(&src, 1);
		cols = codepoint_numcolumns(codepoint);
		if (cols == -1)
			cols = 1;	/* XXX is this right? */

		ucs_to_utf8(codepoint, utf8str, sizeof(utf8str));
		strlcat(dest, utf8str, destlen);
		srcuse -= cols;
	}
	for (i = 0; i < padreps; i++)
		strlcat(dest, padutf8, destlen);

	return dest;
}
#endif


/* 
 * MatchingBracket returns the next unescaped bracket of the given type 
 * This used to be real simple (see the final else clause), but very
 * expensive.  Since its called a lot, i unrolled the two most common cases
 * (parens and brackets) and parsed them out with switches, which should 
 * really help the cpu usage.  I hope.  Everything else just falls through
 * and uses the old tried and true method.
 */
ssize_t	MatchingBracket (const char *start, char left, char right)
{
	int	bracket_count = 1;
	const char *string = start;

	if (left == '(')
	{
	    for (; *string; string++)
	    {
		switch (*string)
		{
		    case '(':
			bracket_count++;
			break;
		    case ')':
			bracket_count--;
			if (bracket_count == 0)
				return string - start;
			break;
		    case '\\':
			if (string[1])
				string++;
			break;
		}
	    }
	}
	else if (left == '[')
	{
	    for (; *string; string++)
	    {
		switch (*string)
	    	{
		    case '[':
			bracket_count++;
			break;
		    case ']':
			bracket_count--;
			if (bracket_count == 0)
				return string - start;
			break;
		    case '\\':
			if (string[1])
				string++;
			break;
		}
	    }
	}
	else		/* Fallback for everyone else */
	{
	    while (*string && bracket_count)
	    {
		if (*string == '\\' && string[1])
			string++;
		else if (*string == left)
			bracket_count++;
		else if (*string == right)
		{
			if (--bracket_count == 0)
				return string - start;
		}
		string++;
	    }
	}

	return -1;
}

/*
 * parse_number: returns the next number found in a string and moves the
 * string pointer beyond that point	in the string.  Here's some examples: 
 *
 * "123harhar"  returns 123 and str as "harhar" 
 *
 * while: 
 *
 * "hoohar"     returns -1  and str as "hoohar" 
 *
 * XXX Why doesn't this use (intmax_t)?
 */
int	parse_number (char **str)
{
	long ret;
	char *ptr = *str;	/* sigh */

	ret = strtol(ptr, str, 10);
	if (*str == ptr)
		ret = -1;

	return (int)ret;
}

char *	chop_word (char *str)
{
	char *end = str + strlen(str) - 1;

	while (my_isspace(*end) && (end > str))
		end--;
	while (!my_isspace(*end) && (end > str))
		end--;

	if (end >= str)
		*end = 0;

	return str;
}

char *	skip_spaces (char *str)
{
	while (str && *str && isspace(*str))
		str++;
	return str;
}

int	split_args (char *str, char **to, size_t maxargs)
{
	size_t	counter;
	char *	ptr;

	ptr = str;
	for (counter = 0; counter < maxargs; counter++)
	{
		if (!ptr || !*ptr)
			break;

		ptr = skip_spaces(ptr);
		if (*ptr == '{' || *ptr == '(')
		{
			if (counter > 0)
				ptr[-1] = 0;
			to[counter] = next_expr_with_type(&ptr, *ptr);
		}
		else
			to[counter] = new_next_arg(ptr, &ptr);

		/* Syntax error? abort immediately. */
		if (to[counter] == NULL)
			break;
	}
	to[counter] = NULL;
	return counter;
}

/*
 * Break down a string containing a word list (words defined by 'extended')
 * into an array of those words.  Double quotes, if any, that surround the
 * double quoted words are removed.
 */
int 	splitw (char *str, char ***to, int extended)
{
	int numwords = count_words(str, extended, "\"");
	int counter;

	if (numwords)
	{
		*to = (char **)new_malloc(sizeof(char *) * numwords);
		for (counter = 0; counter < numwords; counter++)
		{
			char *x = universal_next_arg_count(str, &str, 
							1, extended, 1, "\"");
			if (!x)
				x = endstr(str);
			(*to)[counter] = x;
		}
	}
	else
		*to = NULL;

	return numwords;
}

/*
 * unsplitw implicitly depends on /xdebug dword!
 */
char *	unsplitw (char ***container, int howmany, int extended)
{
	char *retval = NULL;
	char **str;
	size_t clue = 0;

	if (!container || !*container || !**container)
		return NULL;
	str = *container;
	while (howmany)
	{
		if (*str && **str)
			malloc_strcat_word_c(&retval, space, *str, extended, &clue);
		str++, howmany--;
	}

	new_free((char **)container);
	return retval;
}

/*
 * Break down a delimited string and return it in a form suitable for passing
 * back to unsplitw().  This function works on delimiters and not on words.
 * The entire point is for stuff like PATH.
 */
int 	split_string (char *str, char ***to, char delimiter)
{
	int	parts;
	int	counter;
	char *	x;

	parts = count_char(str, delimiter) + 1;
	*to = (char **)new_malloc(sizeof(char *) * parts);
	for (counter = 0; counter < parts; counter++)
	{
		if ((x = strchr(str, delimiter)))
		{
			/* 
			 * IF AND ONLY IF 'delimiter' is not null,
			 * advance past it.  If it is null, then 
			 * there is already a null here, and we
			 * definitely do not want to walk off 
			 * the end! 
			 */
			if (delimiter != 0) 
				*x++ = 0;
		}
		(*to)[counter] = str;
		str = x;
	}

	return parts;
}

/*
 * new_split_string - Splitting delimited UTF-8 string into parts.
 * Arguments:
 *	str	- A string containing delimiters (like PATH -- "/bin:/usr/bin")
 *	to	- A pointer to a (unsigned char **) we can put the results.
 *		  YOU MUST FREE THE RESULT OF (*to).  YOU MUST _NOT_ FREE
 *		  THE STRINGS WITHIN (*to), BECAUSE THOSE POINT TO 'str'.
 *	delimiter - A Unicode Code Point that may be present in 'str' and
 *		    separates the segments within 'str'.  For example, ":".
 *		    You can get a Unicode Code Point with next_code_point()
 *
 * Return Value:
 *	The number of array elements in (*to).
 *	You must new_free() the "to" pointer.
 */
int	new_split_string (unsigned char *str, unsigned char ***to, int delimiter)
{
	int	segments = 0;
	unsigned char *s;
	int	code_point;
	int	i;

	/* First, count the number of segments in 'str' */
	s = str;
	while ((code_point = next_code_point((const unsigned char **)&s, 0)))
		if (code_point == delimiter)
			segments++;
	segments++;

	/* Create an array for (*to) */
	*to = (unsigned char **)new_malloc(sizeof(unsigned char *) * segments);
	for (i = 0; i < segments; i++)
		(*to)[i] = NULL;		/* Just in case */

	/* Split up 'str' nulling out each instance of 'delimiter' */
	i = 0;
	s = str;
	while ((code_point = next_code_point((const unsigned char **)&s, 0)))
	{
		/* If we've found a delimiter.... */
		if (code_point == delimiter)
		{
			/* 
			 * Back up one code point so that 'p' points
			 * at the delimiter again.
			 */
			unsigned char *p = s;
			previous_code_point(str, (const unsigned char **)&p);

			/* 
			 * Terminate the string there and put the result
			 * into the result array
			 */
			*p = 0;
			(*to)[i++] = str;
			if (i >= segments)
			{
				privileged_yell("Warning: new_split_string() thought that string had %d parts, but there appear to be more.", segments);
				return 0;
			}

			/*
			 * Move the "start of string" to the code point
			 * after the delimiter, so we may continue.
			 */
			str = s;
		}
	}

	/* The final segment is the one terminated by a nul... */
	if (str && *str)
		(*to)[i++] = str;

#if 0
	/* This is just for helping me debug it. */
	/* 
	 * This can false-positive if the string ends with the delimiter,
	 * which is not an error and should not be called out to the user.
	 */
	if (i != segments)
	{
		privileged_yell("Warning: new_split_string() thought that string had %d parts, but i only found %d parts", segments, i);
	}
#endif

	/* Return the number of segments in 'str' */
	return segments;
}


double strtod();	/* sunos must die. */
int 	check_val (const char *sub)
{
	double sval;
	char *endptr;

	if (!*sub)
		return 0;

	/* get the numeric value (if any). */
	errno = 0;
	sval = strtod(sub, &endptr);

	/* Numbers that cause exceptional conditions in strtod() are true */
        if (errno == ERANGE
#if defined(HAVE_ISFINITE)
                                || isfinite(sval) == 0
#elif defined(HAVE_FINITE)
                                || finite(sval) == 0
#endif
                                                        )
		return 1;

	/* 
	 * - Any string with no leading number
	 * - Any string containing anything after a leading number
	 * - Any string wholly containing a non-zero number
	 * are all true.
	 */
	if (sub == endptr || *endptr || sval != 0.0)
		return 1;

	/* Basically that leaves empty strings and the number 0 as false. */
	return 0;
}

/*
 * Appends 'num' copies of 'app' to the end of 'str'.
 */
char 	*strextend (char *str, char app, int num)
{
	char *ptr = str + strlen(str);

	for (;num;num--)
		*ptr++ = app;

	*ptr = (char) 0;
	return str;
}

int 	empty (const char *str)
{
	if (str && *str)
		return 0;

	return 1;
}


/* makes foo[one][two] look like tmp.one.two -- got it? */
char *	remove_brackets (const char *name, const char *args)
{
	char 	*ptr, 
		*right, 
		*result1, 
		*rptr, 
		*retval = NULL;
	ssize_t	span;

	/* XXXX - ugh. */
	rptr = malloc_strdup(name);

	while ((ptr = strchr(rptr, '[')))
	{
		*ptr++ = 0;
		right = ptr;
		if ((span = MatchingBracket(right, '[', ']')) >= 0)
		{
			ptr = right + span;
			*ptr++ = 0;
		}
		else
			ptr = NULL;

		if (args)
			result1 = expand_alias(right, args);
		else
			result1 = right;

		retval = malloc_strdup3(rptr, ".", result1);
		if (ptr)
			malloc_strcat(&retval, ptr);

		if (args)
			new_free(&result1);
		if (rptr)
			new_free(&rptr);
		rptr = retval;
	}
	return upper(rptr);
}

/* This should use (intmax_t) */
long	my_atol (const char *str)
{
	if (str)
		return (long) strtol(str, NULL, 0);
	else
		return 0L;
}

char *	malloc_dupchar (int i)
{
	char 	c = (char) i;	/* blah */
	char *	ret = (char *)new_malloc(2);

	ret[0] = c;
	ret[1] = 0;
	return ret;
}

/*
 * This checks to see if ``root'' is a proper subname for ``var''.
 */
int 	is_root (const char *root, const char *var, int descend)
{
	int rootl, varl;

	/* ``root'' must end in a dot */
	rootl = strlen(root);
	if (rootl == 0)
		return 0;
	if (root[rootl - 1] != '.')
		return 0;

	/* ``root'' must be shorter than ``var'' */
	varl = strlen(var);
	if (varl <= rootl)
		return 0;

	/* ``var'' must contain ``root'' as a leading subset */
	if (my_strnicmp(root, var, rootl))
		return 0;

	/* 
	 * ``var'' must not contain any additional dots
	 * if we are checking for the current level only
	 */
	if (!descend && strchr(var + rootl, '.'))
		return 0;

	/* Looks like its ok */
	return 1;
}


/* Returns the number of characters they are equal at. */
size_t 	streq (const char *str1, const char *str2)
{
	size_t cnt = 0;

	while (*str1 && *str2 && *str1 == *str2)
		cnt++, str1++, str2++;

	return cnt;
}

char *	malloc_strndup (const char *str, size_t len)
{
	char *retval = (char *)new_malloc(len + 1);
	strlcpy(retval, str, len + 1);
	return retval;
}

char *	prntdump(const char *ptr, size_t size)
{
	size_t ptridx, dumpidx = 0;
static char dump[640];

	memset(dump, 0, 640);

	for (ptridx = 0; ptridx < size && ptridx < 64; ptridx++)
	{
	    if (!isgraph(ptr[ptridx]) && !isspace(ptr[ptridx]))
	    {
		dump[dumpidx++] = '<';
		snprintf(dump + dumpidx, 3, "%02hhX", (unsigned char)(ptr[ptridx]));
		dumpidx += 2;
		dump[dumpidx++] = '>';
	    }
	    else
		dump[dumpidx++] = ptr[ptridx];
	}

	if (ptridx == 64)
		dump[dumpidx++] = '>';
	dump[dumpidx] = 0;
	return dump;
}

/* XXXX this doesnt belong here. im not sure where it goes, though. */
char *	get_userhost (void)
{
	static char uh[BIG_BUFFER_SIZE];

	const char *x = get_string_var(DEFAULT_USERNAME_VAR);

	if (x && *x)
		strlcpy(uh, x, sizeof uh);
	else
		strlcpy(uh, "Unknown", sizeof uh);

	strlcat(uh, "@", sizeof uh);
	strlcat(uh, hostname, sizeof uh);
	return uh;
}


double	time_to_next_interval (int interval)
{
	Timeval	right_now, then;

	get_time(&right_now);

	then.tv_usec = 1000000 - right_now.tv_usec;
	if (interval == 1)
		then.tv_sec = 0;
	else
		then.tv_sec = interval - (right_now.tv_sec + 1) % interval;
	return (double)then.tv_sec + (double)then.tv_usec / 1000000;
}


/* Fancy attempt to compensate for broken time_t's */
double	time_to_next_minute (void)
{
static	int 	which = 0;
	Timeval	right_now, then;

	get_time(&right_now);

	/* 
	 * The first time called, try to determine if the system clock
	 * is an exact multiple of 60 at the top of every minute.  If it
	 * is, then we will use the "60 trick" to optimize calculations.
	 * If it is not, then we will do it the hard time every time.
	 */
	if (which == 0)
	{
		time_t	blargh;
		struct tm *now_tm;

		blargh = right_now.tv_sec;
		now_tm = gmtime(&blargh);

		if (!which)
		{
			if (now_tm->tm_sec == right_now.tv_sec % 60)
				which = 1;
			else
				which = 2;
		}
	}

	then.tv_usec = 1000000 - right_now.tv_usec;
	if (which == 1)
		then.tv_sec = 60 - (right_now.tv_sec + 1) % 60;
	else 	/* which == 2 */
	{
		time_t	blargh;
		struct tm *now_tm;

		blargh = right_now.tv_sec;
		now_tm = gmtime(&blargh);

		then.tv_sec = 60 - (now_tm->tm_sec + 1) % 60;
	}

	return (double)then.tv_sec + (double)then.tv_usec / 1000000;
}

/*
 * An strcpy that is guaranteed to be safe for overlaps.
 * Warning: This may _only_ be called when one and two overlap!
 */
char *	ov_strcpy (char *str1, const char *str2)
{
	if (str2 > str1)
	{
		while (str2 && *str2)
			*str1++ = *str2++;
		*str1 = 0;
	}
	return str1;
}


/*
 * Its like strcspn, except the second arg is NOT a string.
 */
size_t 	ccspan (const char *string, int s)
{
	size_t count = 0;
	char c = (char) s;

	while (string && *string && *string != c)
		string++, count++;

	return count;
}


int 	last_char (const char *string)
{
	if (!string)
		return 0;

	while (string[0] && string[1])
		string++;

	return (int)*string;
}

int	charcount (const char *string, char what)
{
	int x = 0;
	const char *place = string;

	while (*place)
		if (*place++ == what)
			x++;

	return x;
}

const char *	strfill (char c, int num)
{
	static char buffer[BIG_BUFFER_SIZE / 4 + 1];
	int i;

	if (num > BIG_BUFFER_SIZE / 4)
		num = BIG_BUFFER_SIZE / 4;

	for (i = 0; i < num; i++)
		buffer[i] = c;
	buffer[i] = 0;
	return buffer;
}


char *	chomp (char *s)
{
	char *e = s + strlen(s);

	if (e == s)
		return s;

	while (*--e == '\n')
	{
		*e = 0;
		if (e == s)
			break;
	}

	return s;
}

/*
 * figure_out_address -- lets try this one more time.
 *
 * Arguments:
 *   nuh  - A raw nick!user@host string or a server or channel name
 *   nick - A pointer to a place we can put the "nick"
 *   user - A pointer to a place we can put the "user"
 *   host - A pointer to a place we can put the "host"
 *
 * Returns:
 *   -1     'nuh' is a server or channel -- use it as is.
 *    0     'nuh' is a nick!user@host and has been split up/normalized
 */
int	figure_out_address (const char *nuh, char **nick, char **user, char **host)
{
static 	char 	*mystuff = NULL;
	char 	*bang, 
		*at, 
		*adot = NULL;

	/* Dont bother with channels, they're ok. */
	if (*nuh == '#' || *nuh == '&')
		return -1;

	/* Don't bother with servers, they're ok */
	if (strchr(nuh, '.') && !strchr(nuh, '!') && !strchr(nuh, '@'))
		return -1;

	malloc_strcpy(&mystuff, nuh);

	*host = endstr(mystuff);


	/*
	 * Find and identify each of the three context clues
	 * (A bang, an at, and a dot).
	 */
	if ((bang = strchr(mystuff, '!')))
	{
		*bang = 0;
		if ((at = strchr(bang + 1, '@')))
		{
			*at = 0;
			adot = strchr(at + 1, '.');
		}
	}
	else if ((at = strchr(mystuff, '@')))
	{
		*at = 0;
		adot = strchr(at + 1, '.');
	}
	else 
		adot = strchr(mystuff, '.');

	/*
	 * Hrm.  How many cases are there?  There are three context clues
	 * (A bang, an at, and a dot.)  So that makes 8 different cases.
	 * Let us enumerate them:
	 *
	 * 	nick				(no !, no @, no .)
	 *	nick!user			(a !,  no @, no .)
	 *	nick!user@host			(a !,  a @,  no .)
	 *	nick!user@host.domain		(a !,  a @,  a .)
	 *	nick!host.domain		(a !,  no @, a .)
	 *	user@host			(no !, a @,  no .)
	 *	user@host.domain		(no !, a @,  a .)
	 *	host.domain			(no !, no @, yes .)
	 */

/*
 * STAGE ONE -- EXTRACT THE NICK, USER, AND HOST PORTIONS.
 *
 * stage two is now in 'figure_out_domain', so functions which want it
 * that way can have it that way.
 */

	/*
	 * Now let us handle each of these eight cases in a reasonable way.
	 */
	if (bang)
	{
		if (at)
		{
			/* nick!user@host */
			*nick = mystuff;
			*user = bang + 1;
			*host = at + 1;
		}
		else
		{
			if (adot)		/* nick!host.domain */
			{
				*nick = mystuff;
				*user = endstr(mystuff);
				*host = at + 1;
			}
			else			/* nick!user */
			{
				*nick = mystuff;
				*user = endstr(mystuff);
				*host = *user;
			}
		}
	}
	else
	{
		if (at)
		{
			/* user@host.domain */
			*nick = endstr(mystuff);
			*user = mystuff;
			*host = at + 1;
		}
		else
		{
			if (adot)		/* host.domain */
			{
				*nick = endstr(mystuff);
				*user = *nick;
				*host = mystuff;
			}
			else			/* nick */
			{
				*nick = mystuff;
				*user = endstr(mystuff);
				*host = *user;
			}
		}
	}

	return 0;
}


int	figure_out_domain (char *fqdn, char **host, char **domain, int *ip)
{
	char 	*firstback, 
		*secondback, 
		*thirdback, 
		*fourthback;
	char	*endstring;
	char	*adot;
	int	number;

	/* determine if we have an IP, use dot to hold this */
	/* is_number is better than my_atol since floating point
	 * base 36 numbers are pretty much invalid as IPs.
	 */
	if ((adot = strrchr(fqdn, '.')) && is_number(adot + 1))
		*ip = 1;
	else
		*ip = 0;

/*
 * STAGE TWO -- EXTRACT THE HOST AND DOMAIN FROM FQDN
 */

	/*
	 * At this point, 'fqdn' points what what we think the hostname
	 * is.  We chop it up into discrete parts and see what we end up with.
	 */
	endstring = fqdn + strlen(fqdn);
	firstback = strnrchr(fqdn, '.', 1);
	secondback = strnrchr(fqdn, '.', 2);
	thirdback = strnrchr(fqdn, '.', 3);
	fourthback = strnrchr(fqdn, '.', 4);

	/* Track foo@bar or some such thing. */
	if (!firstback)
	{
		*host = fqdn;
		return 0;
	}

	/*
	 * IP address (A.B.C.D)
	 */
	if (my_atol(firstback + 1))
	{
		*domain = fqdn;

		number = my_atol(fqdn);
		if (number < 128)
			*host = thirdback;
		else if (number < 192)
			*host = secondback;
		else
			*host = firstback;

		if (!*host)
			return -1;		/* Invalid hostname */

		**host = 0;
		(*host)++;
	}
	/*
	 *	(*).(*.???) 
	 *			Handles *.com, *.net, *.edu, etc
	 */
	else if (secondback && (endstring - firstback == 4))
	{
		*host = fqdn;
		*domain = secondback;
		**domain = 0;
		(*domain)++;
	}
	/*
	 *	(*).(*.k12.??.us)
	 *			Handles host.school.k12.state.us
	 */
	else if (fourthback && 
			(firstback - secondback == 3) &&
			!strncmp(thirdback, ".k12.", 5) &&
			!strncmp(firstback, ".us", 3))
	{
		*host = fqdn;
		*domain = fourthback;
		**domain = 0;
		(*domain)++;
	}
	/*
	 *	()(*.k12.??.us)
	 *			Handles school.k12.state.us
	 */
	else if (thirdback && !fourthback && 
			(firstback - secondback == 3) &&
			!strncmp(thirdback, ".k12.", 5) &&
			!strncmp(firstback, ".us", 3))
	{
		*host = endstr(fqdn);
		*domain = fqdn;
	}
	/*
	 *	(*).(*.???.??)
	 *			Handles host.domain.com.au
	 */
	else if (thirdback && 
			(endstring - firstback == 3) &&
			(firstback - secondback == 4))
	{
		*host = fqdn;
		*domain = thirdback;
		**domain = 0;
		(*domain)++;
	}
	/*
	 *	()(*.???.??)
	 *			Handles domain.com.au
	 */
	else if (secondback && !thirdback && 
			(endstring - firstback == 3) &&
		 	(firstback - secondback == 4))
	{
		*host = endstr(fqdn);
		*domain = fqdn;
	}
	/*
	 *	(*).(*.??.??)
	 *			Handles host.domain.co.uk
	 */
	else if (thirdback && 
			(endstring - firstback == 3) &&
			(firstback - secondback == 3))
	{
		*host = fqdn;
		*domain = thirdback;
		**domain = 0;
		(*domain)++;
	}
	/*
	 *	()(*.??.??)
	 *			Handles domain.co.uk
	 */
	else if (secondback && !thirdback &&
			(endstring - firstback == 3) &&
			(firstback - secondback == 3))
	{
		*host = endstr(fqdn);
		*domain = fqdn;
	}
	/*
	 *	(*).(*.??)
	 *			Handles domain.de
	 */
	else if (secondback && (endstring - firstback == 3))
	{
		*host = fqdn;
		*domain = secondback;
		**domain = 0;
		(*domain)++;
	}
	/*
	 *	Everything else...
	 */
	else
	{
		*host = endstr(fqdn);
		*domain = fqdn;
	}

	return 0;
}

/*
 * count_char - count the number of instances of a byte in a string.
 *
 * Arguments: 
 *	src 	- A (non-utf8 - sigh) string
 *	look	- A byte to look for in 'src'
 *		  MUST NOT BE NUL (0)
 *
 * XXX This should be utf8-aware
 */
int 	count_char (const unsigned char *src, const unsigned char look)
{
	const unsigned char *t;
	int	cnt = 0;

	/* 'look' must not be 0 -- there is only 1 nul in a string */
	if (look == 0)
		return 1;

	/* XXX This should be utf8 aware */
	while ((t = strchr(src, look)))
		cnt++, src = t + 1;

	return cnt;
}

char *	strnrchr(char *start, char which, int howmany)
{
	char *ends = start + strlen(start);

	while (ends > start && howmany)
	{
		if (*--ends == which)
			howmany--;
	}
	if (ends == start)
		return NULL;
	else
		return ends;
}

/*
 * This replaces some number of numbers (1 or more) with a single asterisk.
 */
void	mask_digits (char **host)
{
	char	*src_ptr;
	char 	*retval, *retval_ptr;
	size_t	size;

	size = strlen(*host) + 1;
	retval = retval_ptr = alloca(size);
	src_ptr = *host;

	while (*src_ptr)
	{
		if (isdigit(*src_ptr))
		{
			while (*src_ptr && isdigit(*src_ptr))
				src_ptr++;

			*retval_ptr++ = '*';
		}
		else
			*retval_ptr++ = *src_ptr++;
	}

	*retval_ptr = 0;
	strlcpy(*host, retval, size);
}

char *	strlpcat (char *source, size_t size, const char *format, ...)
{
	va_list args;
	char	buffer[BIG_BUFFER_SIZE + 1];

	va_start(args, format);
	vsnprintf(buffer, sizeof buffer, format, args);
	va_end(args);

	strlcat(source, buffer, size);
	return source;
}

/* RANDOM NUMBERS */
/*
 * Random number generator #1 -- psuedo-random sequence
 * If you do not have /dev/random and do not want to use gettimeofday(), then
 * you can use the psuedo-random number generator.  Its performance varies
 * from weak to moderate.  It is a predictable mathematical sequence that
 * varies depending on the seed, and it provides very little repetition,
 * but with 4 or 5 samples, it should be trivial for an outside person to
 * find the next numbers in your sequence.
 *
 * If 'l' is not zero, then it is considered a "seed" value.  You want 
 * to call it once to set the seed.  Subsequent calls should use 'l' 
 * as 0, and it will return a value.
 */
static	unsigned long	randm (unsigned long l)
{
	/* patch from Sarayan to make $rand() better */
static	const	long		RAND_A = 16807L;
static	const	long		RAND_M = 2147483647L;
static	const	long		RAND_Q = 127773L;
static	const	long		RAND_R = 2836L;
static		unsigned long	z = 0;
		long		t;

	if (z == 0)
		z = (unsigned long) getuid();

	if (l == 0)
	{
		t = RAND_A * (z % RAND_Q) - RAND_R * (z / RAND_Q);
		if (t > 0)
			z = t;
		else
			z = t + RAND_M;
		return (z >> 8) | ((z & 255) << 23);
	}
	else
	{
		if ((long) l < 0)
			z = (unsigned long) getuid();
		else
			z = l;
		return 0;
	}
}

/*
 * Random number generator #2 -- gettimeofday().
 * If you have gettimeofday(), then we could use it.  Its performance varies
 * from weak to moderate.  At best, it is a source of modest entropy, with 
 * distinct linear qualities. At worst, it is a linear sequence.  If you do
 * not have gettimeofday(), then it uses randm() instead.
 */
static unsigned long randt_2 (void)
{
	Timeval tp1;

	get_time(&tp1);
	return (unsigned long) tp1.tv_usec;
}

static	unsigned long randt (unsigned long l)
{
#ifdef HAVE_GETTIMEOFDAY
	unsigned long t1, t2, t;

	if (l != 0)
		return 0;

	t1 = randt_2();
	t2 = randt_2();
	t = (t1 & 65535) * 65536 + (t2 & 65535);
	return t;
#else
	return randm(l);
#endif
}


/*
 * Random number generator #3 -- /dev/urandom.
 * If you have the /dev/urandom device, then we will use it.  Its performance
 * varies from moderate to very strong.  At best, it is a source of pretty
 * substantial unpredictable numbers.  At worst, it is mathematical psuedo-
 * random sequence (which randm() is).
 */
static unsigned long randd (unsigned long l)
{
	unsigned long	value;
static	int		random_fd = -1;

	if (l != 0)
		return 0;	/* No seeding appropriate */

	if (random_fd == -2)
		return randm(l);

	else if (random_fd == -1)
	{
		if ((random_fd = open("/dev/urandom", O_RDONLY)) == -1)
		{
			random_fd = -2;
			return randm(l);	/* Fall back to randm */
		}
	}

	if (read(random_fd, (void *)&value, sizeof(value)) <= 0)
	{
		random_fd = -2;
		return randm(l);	/* Fall back to randm */
	}

	return value;
}

/*
 * Random number generator #4 -- Arc4random.
 * If you have the /dev/urandom device, this this may very well be the best
 * random number generator for you.  It spits out relatively good entropic
 * numbers, while not severely depleting the entropy pool (as reading directly
 * from /dev/random does).   If you do not have the /dev/urandom device, then
 * this function uses the stack for its entropy, which may or may not be 
 * suitable, but what the heck.  This generator is always available.
 */
static unsigned long	randa (unsigned long l)
{
	if (l != 0)
		return 0;	/* No seeding appropriate */

	return (unsigned long)arc4random();
}

unsigned long	random_number (unsigned long l)
{
	switch (get_int_var(RANDOM_SOURCE_VAR))
	{
		case 0:
			return randd(l);
		case 1:
			return randm(l);
		case 2:
			return randt(l);
		case 3:
		default:
			return randa(l);
	}
}

/* 
 * Should I switch over to using getaddrinfo() directly or is using
 * inet_strton() sufficient?
 */
char *	switch_hostname (const char *new_hostname)
{
	char 	*workstr = NULL, *v4 = NULL, *v6 = NULL;
	char 	*retval4 = NULL, *retval6 = NULL, *retval = NULL;
	char 	*v4_error = NULL, *v6_error = NULL;
	ISA 	new_4;
#ifdef INET6
	ISA6 	new_6;
#endif
	int	accept4 = 0, accept6 = 0;
	int	fd;

	if (new_hostname == NULL)
	{
		new_free(&LocalIPv4HostName);
#ifdef INET6
		new_free(&LocalIPv6HostName);
#endif
		goto summary;
	}

	accept4 = accept6 = 0;

	workstr = LOCAL_COPY(new_hostname);
	v4 = workstr;
	if ((v6 = strchr(workstr, '/')))
		*v6++ = 0;
	else
		v6 = workstr;

	if (v4 && *v4)
	{
	    new_4.sin_family = AF_INET;
	    if (!inet_strton(v4, zero, (SA *)&new_4, AI_ADDRCONFIG))
	    {
		if ((fd = client_bind((SA *)&new_4, sizeof(ISA))) >= 0)
		{
		    close(fd);
		    accept4 = 1;
		    malloc_strcpy(&LocalIPv4HostName, v4);
		}
		else
		    malloc_strcpy(&v4_error, "see above");
	    }
	    else
		malloc_strcpy(&v4_error, "see above");
	}
	else
		malloc_strcpy(&v4_error, "not specified");

#ifdef INET6
	if (v6 && *v6)
	{
	    new_6.sin6_family = AF_INET6;
	    if (!inet_strton(v6, zero, (SA *)&new_6, AI_ADDRCONFIG)) 
	    {
		if ((fd = client_bind((SA *)&new_6, sizeof(ISA6))) >= 0)
		{
		    close(fd);
		    accept6 = 1;
		    malloc_strcpy(&LocalIPv6HostName, v6);
		}
		else
		    malloc_strcpy(&v6_error, "see above");
	    }
	    else
		malloc_strcpy(&v6_error, "see above");
	}
	else
		malloc_strcpy(&v6_error, "not specified");
#endif

summary:
	if (v4_error)
		malloc_sprintf(&retval4, "IPv4 vhost not changed because (%s)",
						v4_error);
	else if (LocalIPv4HostName)
	{
	    if (accept4)
		malloc_sprintf(&retval4, "IPv4 vhost changed to [%s]",
						LocalIPv4HostName);
	    else
		malloc_sprintf(&retval4, "IPv4 vhost unchanged from [%s]",
						LocalIPv4HostName);
	}
	else
		malloc_sprintf(&retval4, "IPv4 vhost unset");

#ifdef INET6
	if (v6_error)
		malloc_sprintf(&retval6, "IPv6 vhost not changed because (%s)",
						v6_error);
	else if (LocalIPv6HostName)
	{
	    if (accept6)
		malloc_sprintf(&retval6, "IPv6 vhost changed to [%s]",
						LocalIPv6HostName);
	    else
		malloc_sprintf(&retval6, "IPv6 vhost unchanged from [%s]",
						LocalIPv6HostName);
	}
	else
		malloc_sprintf(&retval6, "IPv6 vhost unset");
#endif

	if (retval6)
		malloc_sprintf(&retval, "%s, %s", retval4, retval6);
	else
		retval = retval4, retval4 = NULL;

	new_free(&v4_error);
	new_free(&v6_error);
	new_free(&retval4);
	new_free(&retval6);

	return retval;
}


size_t	strlcpy_c (char *dst, const char *src, size_t size, size_t *cluep)
{
	size_t	retval;

	retval = strlcpy(dst, src, size);
	if (retval > size - 1)
		*cluep = size - 1;
	else
		*cluep = retval;
	return retval;
}

size_t	strlcat_c (char *dst, const char *src, size_t size, size_t *cluep)
{
	size_t	retval;
	size_t	real_size;
	char *	real_dst;

	real_dst = dst + *cluep;
	real_size = size - *cluep;

	retval = strlcat(real_dst, src, real_size);
	if (retval + *cluep > size - 1)
		*cluep = size - 1;
	else
		*cluep = retval + *cluep;
	return retval;
}

char *	strlopencat_c (char *dest, size_t maxlen, size_t *cluep, ...)
{
	va_list	args;
	char *	this_arg = NULL;
	size_t	spare_clue;

	if (cluep == NULL)
	{
		spare_clue = strlen(dest);
		cluep = &spare_clue;
	}

	va_start(args, cluep);
	for (;;)
	{
		/* Grab the next string, stop if no more */
		if (!(this_arg = va_arg(args, char *)))
			break;

		/* Add this string to the end, adjusting the clue */
		strlcat_c(dest, this_arg, maxlen, cluep);

		/* If we reached the end of our space, stop here. */
		if (*cluep >= maxlen - 1)
			break;
	}

	va_end(args);
	return dest;
}

int     is_string_empty (const char *str) 
{
        while (str && *str && isspace(*str))
                str++;
 
        if (str && *str)
                return 0;
 
        return 1;
}

/*
 * malloc_strcpy_c: Make a copy of a string into heap space, which may
 *	optionally be provided, with an optional clue.
 *
 * Arguments:
 *  'ptr' - A pointer to a variable pointer that is either:
 *	    1) The value NULL or a valid heap pointer to space which is not
 *		large enough to hold 'src', in which case heap space will be
 *		allocated, and the original value of (*ptr) will be invalidated.
 *	    2) A valid heap pointer to space which is large enough to hold 'src'
 *		in which case 'src' will be copied to the heap space.
 *  'src' - The string to be copied.  If NULL, (*ptr) is invalidated (freed).
 *
 * Return value:
 *  If 'src' is NULL, an invalid heap pointer.
 *  If 'src' is not NULL, a valid heap pointer that contains a copy of 'src'.
 *  (*ptr) is set to the return value.
 *  This function will not return (panic) if (*ptr) is not NULL and is 
 *	not a valid heap pointer.
 *
 * Notes:
 *  If (*ptr) is not big enough to hold 'src' then the original value (*ptr) 
 * 	will be invalidated and must not be used after this function returns.
 *  You must deallocate the space later by passing (ptr) to the new_free() 
 *	function.
 */
char *	malloc_strcpy_c (char **ptr, const char *src, size_t *clue)
{
	size_t	size, size_src;

	if (!src)
	{
		if (clue)
			*clue = 0;
		return new_free(ptr);	/* shrug */
	}

	if (*ptr)
	{
		size = alloc_size(*ptr);
		if (size == (size_t) FREED_VAL)
			panic(1, "free()d pointer passed to malloc_strcpy");

		/* No copy neccesary! */
		if (*ptr == src)
			return *ptr;

		size_src = strlen(src);
		if (size > size_src)
		{
			strlcpy(*ptr, src, size);
			if (clue)
				*clue = size_src;
			return *ptr;
		}

		new_free(ptr);
	}

	size = strlen(src);
	*ptr = new_malloc(size + 1);
	strlcpy(*ptr, src, size + 1);
	if (clue)
		*clue = size;
	return *ptr;
}

/*
 * malloc_strcat_c: Append a copy of 'src' to the end of '*ptr', with an 
 *	optional "clue" (length of (*ptr))
 *
 * Arguments:
 *  'ptr' - A pointer to a variable pointer that is either:
 *	    1) The value NULL or a valid heap pointer to space which is not
 *		large enough to hold 'src', in which case heap space will be
 *		allocated, and the original value of (*ptr) will be invalidated.
 *	    2) A valid heap pointer to space which shall contain a valid
 *		nul-terminated C string.
 *  'src' - The string to be copied.  If NULL, this function is a no-op.
 *  'cluep' - A pointer to an integer holding the string length of (*ptr).
 *		may be NULL.
 *
 * Return value:
 *  If 'src' is NULL, the original value of (*ptr) is returned.
 *  If 'src' is not NULL, a valid heap pointer that contains the catenation 
 *	of the string originally contained in (*ptr) and 'src'.
 *  (*ptr) is set to the return value.
 *  This function will not return (panic) if (*ptr) is not NULL and is 
 *	not a valid heap pointer.
 *  If 'cluep' is not NULL, it will be set to the string length of the 
 *	new value of (*ptr).
 *
 * Notes:
 *  If (*ptr) is not big enough to take on the catenated string, then the
 *	original value (*ptr) will be invalidated and must not be used after
 *	this function returns.
 *  This function needs to determine how long (*ptr) is, and unless you 
 *	provide 'cluep' it will do a strlen(*ptr).  If (*ptr) is quite
 *	large, this could be an expensive operation.  The use of a clue
 *	is an optimization option.
 *  If you don't want to bother with the 'clue', use the malloc_strcat macro.
 *  You must deallocate the space later by passing (ptr) to the new_free() 
 *	function.
 */
char *	malloc_strcat_c (char **ptr, const char *src, size_t *cluep)
{
	size_t  msize;
	size_t  psize;
	size_t  ssize;
	size_t	clue = cluep ? *cluep : 0;

	if (*ptr)
	{
		if (alloc_size(*ptr) == FREED_VAL)
			panic(1, "free()d pointer passed to malloc_strcat");

		if (!src)
			return *ptr;

		psize = clue + strlen(clue + *ptr);
		ssize = strlen(src);
		msize = psize + ssize + 1;

		RESIZE(*ptr, char, msize);
		if (cluep) 
			*cluep = psize + ssize;
		strlcat(psize + *ptr, src, msize - psize);
		return (*ptr);
	}

	return (*ptr = malloc_strdup(src));
}

/*
 * malloc_strdup: Allocate and return a pointer to valid heap space into
 *	which a copy of 'str' has been placed.
 *
 * Arguments:
 *  'str' - The string to be copied.  If NULL, a zero length string will
 *		be copied in its place.
 *
 * Return value:
 *  If 'str' is not NULL, then a valid heap pointer containing a copy of 'str'.
 *  If 'str' is NULL, then a valid heap pointer containing a 0-length string.
 *
 * Notes:
 *  You must deallocate the space later by passing a pointer to the return
 *	value to the new_free() function.
 */
char *	malloc_strdup (const char *str)
{
	char *ptr;
	size_t size;

	if (!str)
		str = empty_string;

	size = strlen(str) + 1;
	ptr = (char *)new_malloc(size);
	strlcpy(ptr, str, size);
	return ptr;
}

/*
 * malloc_strdup2: Allocate and return a pointer to valid heap space into
 *	which a catenation of 'str1' and 'str2' have been placed.
 *
 * Arguments:
 *  'str1' - The first string to be copied.  If NULL, a zero-length string
 *		will be used in its place.
 *  'str2' - The second string to be copied.  If NULL, a zero-length string
 *		will be used in its place.
 *
 * Return value:
 *  A valid heap pointer containing a copy of 'str1' and 'str2' catenated 
 *	together.  'str1' and 'str2' may be substituted as indicated above.
 *
 * Notes:
 *  You must deallocate the space later by passing a pointer to the return
 *	value to the new_free() function.
 */
char *	malloc_strdup2 (const char *str1, const char *str2)
{
	size_t msize;
	char * buffer;

	/* Prevent a crash. */
	if (str1 == NULL)
		str1 = empty_string;
	if (str2 == NULL)
		str2 = empty_string;

	msize = strlen(str1) + strlen(str2) + 1;
	buffer = (char *)new_malloc(msize);
	*buffer = 0;
	strlopencat_c(buffer, msize, NULL, str1, str2, NULL);
	return buffer;
}

/*
 * malloc_strdup3: Allocate and return a pointer to valid heap space into
 *	which a catenation of 'str1', 'str2', and 'str3' have been placed.
 *
 * Arguments:
 *  'str1' - The first string to be copied.  If NULL, a zero-length string
 *		will be used in its place.
 *  'str2' - The second string to be copied.  If NULL, a zero-length string
 *		will be used in its place.
 *  'str3' - The third string to be copied.  If NULL, a zero-length string
 *		will be used in its place.
 *
 * Return value:
 *  A valid heap pointer containing a copy of 'str1', 'str2', and 'str3' 
 *	catenated together.  'str1', 'str2', and 'str3' may be substituted 
 *	as indicated above.
 *
 * Notes:
 *  You must deallocate the space later by passing a pointer to the return
 *	value to the new_free() function.
 */
char *	malloc_strdup3 (const char *str1, const char *str2, const char *str3)
{
	size_t msize;
	char *buffer;

	if (!str1)
		str1 = empty_string;
	if (!str2)
		str2 = empty_string;
	if (!str3)
		str3 = empty_string;

	msize = strlen(str1) + strlen(str2) + strlen(str3) + 1;
	buffer = (char *)new_malloc(msize);
	*buffer = 0;
	strlopencat_c(buffer, msize, NULL, str1, str2, str3, NULL);
	return buffer;
}

/*
 * malloc_strcat2_c: Append a copy of 'str1' and 'str2'' to the end of 
 *	'*ptr', with an optional "clue" (length of (*ptr))
 *
 * Arguments:
 *  'ptr' - A pointer to a variable pointer that is either NULL or a valid
 *		heap pointer which shall contain a valid C string.
 *  'str1' - The first of two strings to be appended to '*ptr'.
 *		May be NULL.
 *  'str2' - The second of two strings to be appended to '*ptr'.
 *		May be NULL.
 *  'cluep' - A pointer to an integer holding the string length of (*ptr).
 *		May be NULL.
 *
 * Return value:
 *  The catenation of the three strings '*ptr', 'str1', and 'str2', except
 *	if either 'str1' or 'str2' are NULL, those values are ignored.
 *  (*ptr) is set to the return value.
 *  This function will not return (panic) if (*ptr) is not NULL and is 
 *	not a valid heap pointer.
 *  If 'cluep' is not NULL, it will be set to the string length of the 
 *	new value of (*ptr).
 *
 * Notes:
 *  The original value of (*ptr) is always invalidated by this function and
 *	may not be used after this function returns.
 *  This function needs to determine how long (*ptr) is, and unless you 
 *	provide 'cluep' it will do a strlen(*ptr).  If (*ptr) is quite
 *	large, this could be an expensive operation.  The use of a clue
 *	is an optimization option.
 *  If you don't want to bother with the 'clue', use the malloc_strcat2 macro.
 *  You must deallocate the space later by passing (ptr) to the new_free() 
 *	function.
 */
char *	malloc_strcat2_c (char **ptr, const char *str1, const char *str2, size_t *clue)
{
	size_t	csize;
	int 	msize;

	csize = clue ? *clue : 0;
	msize = csize;

	if (*ptr)
	{
		if (alloc_size(*ptr) == FREED_VAL)
			panic(1, "free()d pointer passed to malloc_strcat2");
		msize += strlen(csize + *ptr);
	}
	if (str1)
		msize += strlen(str1);
	if (str2)
		msize += strlen(str2);

	if (!*ptr)
	{
		*ptr = new_malloc(msize + 1);
		**ptr = 0;
	}
	else
		RESIZE(*ptr, char, msize + 1);

	if (str1)
		strlcat(csize + *ptr, str1, msize + 1 - csize);
	if (str2)
		strlcat(csize + *ptr, str2, msize + 1 - csize);
	if (clue) 
		*clue = msize;

	return *ptr;
}

char *	malloc_strcat3_c (char **ptr, const char *str1, const char *str2, const char *str3, size_t *clue)
{
	size_t	csize;
	size_t	msize;

	csize = clue ? *clue : 0;
	msize = csize;

	if (*ptr)
	{
		if (alloc_size(*ptr) == FREED_VAL)
			panic(1, "free()d pointer passed to malloc_strcat2");
		msize += strlen(csize + *ptr);
	}
	if (str1)
		msize += strlen(str1);
	if (str2)
		msize += strlen(str2);
	if (str3)
		msize += strlen(str3);

	if (!*ptr)
	{
		*ptr = new_malloc(msize + 1);
		**ptr = 0;
	}
	else if (msize >= 0)
		RESIZE(*ptr, char, msize + 1);
	else
	{
		msize = 10;
		*ptr = new_malloc(msize + 1);
		**ptr = 0;
	}

	if (str1)
		strlcat(csize + *ptr, str1, msize + 1 - csize);
	if (str2)
		strlcat(csize + *ptr, str2, msize + 1 - csize);
	if (str3)
		strlcat(csize + *ptr, str3, msize + 1 - csize);
	if (clue) 
		*clue = msize;

	return *ptr;
}
/*
 * malloc_strcat_wordlist_c: Append a word list to another word list using
 *	a delimiter, with an optional "clue" (length of (*ptr))
 *
 * Arguments:
 *  'ptr' - A pointer to a variable pointer that is either NULL or a valid
 *		heap pointer which shall contain a valid C string which 
 *		represents a word list (words separated by delimiters)
 *  'word_delim' - The delimiter to use to separate (*ptr) from 'word_list'.
 *		May be NULL if no delimiter is desired.
 *  'word_list' - The word list to append to (*ptr).
 *		May be NULL.
 *  'cluep' - A pointer to an integer holding the string length of (*ptr).
 *		May be NULL.
 *
 * Return value:
 *  If "wordlist" is either NULL or a zero-length string, this function
 *	does nothing, and returns the original value of (*ptr).
 *  If "wordlist" is not NULL and not a zero-length string, and (*ptr) is
 *	either NULL or a zero-length string, (*ptr) is set to "wordlist",
 *	and the new value of (*ptr) is returned.
 *  If "wordlist" is not NULL and not a zero-length string, and (*ptr) is
 *	not NULL and not a zero-length string, (*ptr) is set to the 
 *	catenation of (*ptr), 'word_delim', and 'wordlist' and is the
 *	return value.  
 *  This function will not return (panic) if (*ptr) is not NULL and is 
 *	not a valid heap pointer.
 *  If 'cluep' is not NULL, it will be set to the string length of the 
 *	new value of (*ptr).
 *
 * Notes:
 *  The idea of this function is given two word lists, either of which 
 *	may contain zero or more words, paste them together using a
 *	delimiter, which for word lists, is usually a space, but could
 *	be any character.
 *  Unless "wordlist" is NULL or a zero-length string, the original value
 *	of (*ptr) is invalidated and may not be used after this function
 *	returns.
 *  This function needs to determine how long (*ptr) is, and unless you 
 *	provide 'cluep' it will do a strlen(*ptr).  If (*ptr) is quite
 *	large, this could be an expensive operation.  The use of a clue
 *	is an optimization option.
 *  If you don't want to bother with the 'clue', use the 
 *	malloc_strcat_wordlist macro.
 *  You must deallocate the space later by passing (ptr) to the new_free() 
 *	function.
 *  A WORD LIST IS CONSIDERED TO HAVE ONE ELEMENT IF IT HAS ANY CHARACTERS
 *	EVEN IF THAT CHARACTER IS A DELIMITER (ie, a space).
 */
char *	malloc_strcat_wordlist_c (char **ptr, const char *word_delim, const char *wordlist, size_t *clue)
{
	if (wordlist && *wordlist)
	{
	    if (*ptr && **ptr)
		return malloc_strcat2_c(ptr, word_delim, wordlist, clue);
	    else
		return malloc_strcpy_c(ptr, wordlist, clue);
	}
	else
	    return *ptr;
}

char *	malloc_strcat_word_c (char **ptr, const char *word_delim, const char *word, int extended, size_t *clue)
{
	/* You MUST turn on /xdebug dword to get double quoted words */
	if (extended == DWORD_DWORDS && !(x_debug & DEBUG_DWORD))
		return malloc_strcat_wordlist_c(ptr, word_delim, word, clue);
	if (extended == DWORD_EXTRACTW && !(x_debug & DEBUG_EXTRACTW))
		return malloc_strcat_wordlist_c(ptr, word_delim, word, clue);
	if (extended == DWORD_NO)
		return malloc_strcat_wordlist_c(ptr, word_delim, word, clue);

	if (word && *word)
	{
		if (*ptr && **ptr)
			malloc_strcat_c(ptr, word_delim, clue);

		/* Remember, any double quotes therein need to be quoted! */
		if (strpbrk(word, word_delim))
		{
			char *oofda = NULL;
			oofda = alloca(strlen(word) * 2 + 1);
			double_quote(word, "\"", oofda);

			malloc_strcat3_c(ptr, "\"", oofda, "\"", clue);
		}
		else
			malloc_strcat_c(ptr, word, clue);
	}

	return *ptr;
}

/*
 * malloc_sprintf: write a formatted string to heap memory
 *
 * Arguments:
 *  'ptr' - A NULL pointer, or a pointer to a variable pointer that is 
 *		either NULL or a valid heap pointer which shall contain 
 *		a valid C string which represents a word list (words 
 *		separated by delimiters)
 *  'format' - A *printf() format string
 *  ... - The rest of the arguments map to 'format' in the normal way for
 *		*printf() functions.
 *
 * Return value:
 *  If 'format' is NULL, The return value will be set to a valid heap pointer 
 *	to a zero-length C string.
 *  If 'format' is not NULL, The return value will be set to a valid heap 
 *	pointer sufficiently large to contain a C string of the form 'format',
 *	filled in with the rest of the arguments in accordance with sprintf().
 *  In either case, if ptr is not NULL, (*ptr) is set to the return value.
 *  This function will not return (panic) if ptr is not NULL, *ptr is not NULL,
 *	and is (*ptr) is not a valid heap pointer.
 *
 * Notes:
 *  If the arguments passed do not match up with 'format', chaos may result.
 *  If ptr is not NULL then the original value of (*ptr) is invalidated and
 *	may not be used after this function returns.
 *  You must deallocate the space later by passing a pointer to the return
 *	value to the new_free() function.
 */
char *	malloc_sprintf (char **ptr, const char *format, ...)
{
	char *retval;
	va_list	args;

	va_start(args, format);
	retval = malloc_vsprintf(ptr, format, args);
	va_end(args);
	return retval;
}

char *	malloc_vsprintf (char **ptr, const char *format, va_list args)
{
	char *	buffer = NULL;
	size_t	buffer_size;
	int	vsn_retval;
	va_list	orig_args;

	if (format)
	{
		/* Our initial hint assumes the result is twice the 
		 * length of format */
		buffer_size = strlen(format) * 2;
		buffer = new_malloc(buffer_size + 1);

	        va_copy(orig_args, args);
		do {
		    vsn_retval = vsnprintf(buffer, buffer_size, format, args);

		    if (vsn_retval < 0)		/* DIE DIE DIE */
			buffer_size += 16;
		    else if ((size_t)vsn_retval < buffer_size)
			break;
		    else
			buffer_size = vsn_retval + 1;
		    RESIZE(buffer, char, buffer_size);

	            va_copy(args, orig_args);
		} while (1);

	}

	if (ptr)
	{
		new_free(ptr);
		*ptr = buffer;
	}
	return buffer;
}

/*
 * universal_next_arg_count:  Remove the first "count" words from "str", 
 *	where ``word'' is defined by all this scary text below here...
 *
 * Arguments:
 *  'str' - A standard word list (words are separated by spaces only)
 *		This string will be modified!
 *  'new_ptr' - A pointer to a variable pointer into which shall be placed
 *		the new start of 'str' after the words have been removed.
 *		It's customary to pass a pointer to "str" for this param.
 *  'count' - The number of words to remove.  If you want to remove one
 *		word, use the next_arg() macro.
 *  'extended' - One of the three "DWORD" macros:
 *		DWORD_NO     - Do not honor double-quoted words in 'str'
 *		DWORD_EXTRACTW - Honor them if /xdebug extractw is on
 *		DWORD_DWORDS - Honor them if /xdebug dword is on
 *		DWORD_YES    - Always honor double-quoted words in 'str'.
 *  'dequote' - The double quotes that surround double-quoted words
 *		should be stripped from the return value.
 *
 * Definition:
 *  A "word" is either a "standard word" or a "double quoted word".
 *  A "standard word" is one or more characters that are not spaces, as 
 *      defined by your locale's "isspace(3)" rules.  A "standard word" 
 *      is separated from other "standard words" by spaces, as defined 
 *      by "isspace(3)".  "Standard words" do not contain any spaces.
 *  A "double quoted word" is one or more characters that are surrounded
 *	by double quotes (").  A word is considered "double quoted" if it
 *	begins with a double quote that occurs at the start of the string,
 *	or immediately after one or more spaces as defined by isspace(3);
 *	and if it ends with a double quote that occurs at the end of the
 *	string or immediately before one or more spaces.  Every word that
 *	is not a "double quoted word" as defined here is a Standard Word.
 *
 *  If "dequote" is 0, then any Double Quoted Words shall be modified to
 *	remove the double quotes that surround them.  Dequoting multiple
 *	words is expensive.
 *
 * Return value:
 *  The first position of the first word in 'str'; the start of a string
 *	that includes the first 'count' words in 'str', perhaps modified
 *	by the above rules.  Spaces between words are retained, but words
 *	before the first word and after the last word will be trimmed.
 *  Furthermore, because the whitespace character after the last word in
 *	the return value shall be filled in with a NUL character, the 
 *	'*new_ptr' value will be set to the position after this NUL.
 *  Subject to the following conditions:
 *	If "str" is NULL or a zero-length string, NULL is returned.
 *	If "str" contains only spaces the return value shall be a
 *		zero-length string.
 *	If "str" does not contain more words than requested, The return
 *		value shall be as normal, but '*new_ptr' shall be set to
 *		a zero-length string.
 *
 * Notes:
 *  "Contain more words than requested" means if you request 1 word
 *	and there is only one word in 'str', then the return value 
 *	will point to that word, but '*new_ptr' will be set to a zero
 *	length string.
 *  You can loop over a string, pulling each word off doing something
 *	like the following:
 *		while ((ptr = universal_next_arg_count(str, &str, 1, 1, 0))) {
 *			... operate on ptr ...
 *		}
 *  'str' will be modified, so if you need to remove words from a
 *	(const char *), make a LOCAL_COPY() of it first.
 *
 * There are some shorthand macros available:
 *	next_arg(char *str, char **new_ptr);
 *	next_arg_count(char *str, char **new_ptr, int count);
 *	new_next_arg(char *str, char **new_ptr);
 *	new_next_arg_count(char *str, char **new_ptr, int count);
 */
char *	universal_next_arg_count (char *str, char **new_ptr, int count, int extended, int dequote, const char *delims)
{
	size_t clue;

	if (!str || !*str)
		return NULL;

	while (str && *str && my_isspace(*str))
		str++;

	if (x_debug & DEBUG_EXTRACTW_DEBUG)
		yell(">>>> universal_next_arg_count: Start: [%s], count [%d], extended [%d], dequote [%d], delims [%s]", str, count, extended, dequote, delims);

	real_move_to_abs_word(str, (const char **)new_ptr, count, extended, delims);
	if (**new_ptr && *new_ptr > str)
		(*new_ptr)[-1] = 0;

	clue = (*new_ptr) - str - 1;

	/* XXX Is this really correct? This seems wrong. */
	remove_trailing_spaces(str, &clue);

	/* Arf! */
	if (dequote == -1)
	{
		if (extended == DWORD_EXTRACTW)
			if (x_debug & DEBUG_EXTRACTW)
				dequote = 1;
			else
				dequote = 0;
		else if (extended == DWORD_DWORDS)
			if (x_debug & DEBUG_DWORD)
				dequote = 1;
			else
				dequote = 0;
		else if (extended == DWORD_YES)
			dequote = 1;
		else
			dequote = 0;
	}

	if (dequote)
		dequoter(&str, &clue, count == 1 ? 0 : 1, extended, delims);

	if (x_debug & DEBUG_EXTRACTW_DEBUG)
		yell("<<<< universal_next_arg_count: End:   [%s] [%s]", 
						str, *new_ptr);
	return str;
}

/*
 * dequoter: Remove double quotes around Double Quoted Words
 *
 * Arguments:
 *  'str' - A pointer to a string that contains Double Quoted Words.
 *		Double quotes around Double Quoted Words in (*str) will be
 *		removed.
 *  'clue' - A pointer to an integer holding the size of '*str'.  You must
 *		provide this correct value.  If '*str' is shortened, this
 *		value will be changed to reflect the new length of '*str'.
 *  'full' - Assume '*str' contains more than one word and an exhaustive
 *		dequoting is neccessary.  THIS IS VERY EXPENSIVE.  If '*str'
 *		contains one word, this should be 0.
 *  'extended' - The extended word policy for this string.  This should 
 *		usually be DWORD_YES unless you're doing something fancy.
 *
 * Return value:
 *	There is no return value, but '*str' and '*clue' may be modified as
 *	described in the above notes.
 *
 * Notes:
 *	None.
 */
void	dequoter (char **str, size_t *clue, int full, int extended, const char *delims)
{
	int	simple;
	char	what;
	size_t	orig_size;
	orig_size = *clue + 1;

	/*
	 * Solve the problem of a string with one word...
	 */
	if (full == 0)
	{
	    if (delims && delims[0] && delims[1] == 0)
	    {
		simple = 1;
		what = delims[0];
		if (x_debug & DEBUG_EXTRACTW_DEBUG)
			yell("#### dequoter: Dequoting [%s] simply with delim [%c]", *str, what);
	    }
	    else
	    {
		simple = 0;
		what = -1;
		if (x_debug & DEBUG_EXTRACTW_DEBUG)
			yell("#### dequoter: Dequoting [%s] fully with delims [%s]", *str, delims);
	    }

	    if (str && *str && ((simple == 1 && **str == what) || 
				(simple == 0 && strchr(delims, **str))))
	    {
		if (x_debug & DEBUG_EXTRACTW_DEBUG)
			yell("#### dequoter: simple string starts with delim...");

		if (*clue > 0 && ((simple == 1 && (*str)[*clue] == what) ||
				  (simple == 0 && strchr(delims, (*str)[*clue]))))
		{
			if (x_debug & DEBUG_EXTRACTW_DEBUG)
				yell("#### dequoter: simple string ends with delim...");

			/* Kill the closing quote. */
			(*str)[*clue] = 0;
			(*clue)--;

			/* Kill the opening quote. */
			(*str)++;
			(*clue)--;
		}
	    }
	    return;
	}

	/*
	 * I'm going to perdition for writing this, aren't I...
	 */
	else
	{
		char *orig_str;		/* Where to start the dest string */
		char *retval;		/* our temp working buffer */
		size_t rclue;		/* A working clue for 'retval' */
		char *this_word;	/* The start of each word */
		size_t this_len;	/* How long the word is */

		orig_str = *str;	/* Keep this for later use */
		retval = alloca(orig_size);	/* Reserve space for a full copy */
		*retval = 0;		/* Prep retval for use... */
		rclue = 0;

		/*
		 * Solve the problem of dequoting N words iteratively by 
		 * solving the problem for the first word and repeating
		 * until we've worked through the entire list.  Then copy
		 * the results back to the original string.
		 */
		while ((this_word = universal_next_arg_count(*str, str, 1, extended, 0, delims)))
		{
			this_len = strlen(this_word) - 1;
			dequoter(&this_word, &this_len, 0, extended, delims);
			if (rclue > 0)
				strlcat_c(retval, space, orig_size, &rclue);
			strlcat_c(retval, this_word, orig_size, &rclue);
		}

		*orig_str = 0;
		*clue = 0;
		strlcpy_c(orig_str, retval, orig_size, clue);
		*str = orig_str;
	}
}


char *	new_new_next_arg_count (char *str, char **new_ptr, char *type, int count)
{
	char kludge[2];

	/* Skip leading spaces, blah blah blah */
        while (str && *str && my_isspace(*str))
                str++;

	if (!str || !*str)
		return NULL;

	if (*str == '\'')
		*type = '\'';
	else
		*type = '"';

	kludge[0] = *type;
	kludge[1] = 0;
	return universal_next_arg_count(str, new_ptr, 1, DWORD_YES, 1, kludge);
}

/*
 * Note that the old version is now out of sync with epics word philosophy.
 */
char *	safe_new_next_arg (char *str, char **new_ptr)
{
	char * ret;

	if (!(ret = new_next_arg(str, new_ptr)))
		ret = endstr(str);

	return ret;
}

char *	safe_next_arg (char *str, char **new_ptr)
{
	char * ret;

	if (!(ret = next_arg(str, new_ptr)))
		ret = endstr(str);

	return ret;
}

/*
 * yanks off the last word from 'src'
 * kinda the opposite of next_arg
 */
char *	last_arg (char **src, size_t *cluep, int extended)
{
	char *mark, *start, *end;
	size_t	clue2;

	start = *src;
	end = start + *cluep;
	mark = end + strlen(end);
	/* Always support double-quoted words. */
	move_word_rel(start, (const char **)&mark, -1, extended, "\"");
	*cluep = (mark - *src - 1);

	if (mark > start)
		mark[-1] = 0;
	else
		*src = NULL;		/* We're done, natch! */

	clue2 = strlen(mark) - 1;
	dequoter(&mark, &clue2, 0, extended, "\"");
	return mark;
}

char *	endstr (char *src)
{
	if (!src)
		return NULL;
	while (*src)
		src++;
	return src;
}


/* USER MODES */
void	add_mode_to_str (char *modes, size_t len, int mode)
{
	char c, *p, *o;
	char new_modes[1024];		/* Too huge for words */
	int	i;

	/* 
	 * 'c' is the mode that is being added
	 * 'o' is the umodes that are already set
	 * 'p' is the string that we are building that adds 'c' to 'o'.
	 */
	c = (char)mode;
	o = modes;
	p = new_modes;

	/* Copy the modes in 'o' that are alphabetically less than 'c' */
	for (i = 0; o && o[i]; i++)
	{
		if (o[i] >= c)
			break;
		*p++ = o[i];
	}

	/* If 'c' is already set, copy it, otherwise add it. */
	if (o && o[i] == c)
		*p++ = o[i++];
	else
		*p++ = c;

	/* Copy all the rest of the modes */
	for (; o && o[i]; i++)
		*p++ = o[i];

	/* Nul terminate the new string and reset the server's info */
	*p++ = 0;
	strlcpy(modes, new_modes, len);
}

void	remove_mode_from_str (char *modes, size_t len, int mode)
{
	char c, *o, *p;
	char new_modes[1024];		/* Too huge for words */
	int	i;

	/* 
	 * 'c' is the mode that is being deleted
	 * 'o' is the umodes that are already set
	 * 'p' is the string that we are building that adds 'c' to 'o'.
	 */
	c = (char)mode;
	o = modes;
	p = new_modes;

	/*
	 * Copy the whole of 'o' to 'p', except for any instances of 'c'.
	 */
	for (i = 0; o && o[i]; i++)
	{
		if (o[i] != c)
			*p++ = o[i];
	}

	/* Nul terminate the new string and reset the server's info */
	*p++ = 0;
	strlcpy(modes, new_modes, len);
}

void 	clear_modes (char *modes)
{
	*modes = 0;
}

void	update_mode_str (char *modes, size_t len, const char *changes)
{
	int		onoff = 1;

	if (!modes)
		return;

	for (; *changes; changes++)
	{
		if (*changes == '-')
			onoff = 0;
		else if (*changes == '+')
			onoff = 1;
		else if (onoff == 1)
			add_mode_to_str(modes, len, (int)*changes);
		else if (onoff == 0)
			remove_mode_from_str(modes, len, (int)*modes);
	}
	update_all_status();
}

/*
 * This function is 8 bit clean (it ignores nuls) so please do not just
 * whimsically throw it away!
 */
ssize_t	searchbuf (const unsigned char *str, size_t start, size_t end, int find)
{
	size_t	counter;

	for (counter = 0;;)
	{
		if (start + counter >= end)
			return -1;
		if (str[start + counter] == (char)find)
			return counter;
		counter++;
	}

	return -1;		/* Eh, whatever */
}

/*
 * after_expando: This replaces some much more complicated logic strewn
 * here and there that attempted to figure out just how long an expando 
 * name was supposed to be.  Well, now this changes that.  This will slurp
 * up everything in 'start' that could possibly be put after a $ that could
 * result in a syntactically valid expando.  All you need to do is tell it
 * if the expando is an rvalue or an lvalue (it *does* make a difference)
 */
static 	const char *lval[] = { "rvalue", "lvalue" };
char *	after_expando (char *start, int lvalue, int *call)
{
	char	*rest;
	char	*str;

	if (!*start)
		return start;

	/*
	 * One or two leading colons are allowed
	 */
	str = start;
	if (*str == ':')
		if (*++str == ':')
			++str;

	/*
	 * This handles 99.99% of the cases
	 */
	while (*str && (isalpha(*str) || isdigit(*str) || 
				*str == '_' || *str == '.'))
		str++;

	/*
	 * This handles any places where a var[var] could sneak in on
	 * us.  Supposedly this should never happen, but who can tell?
	 */
	while (*str == '[')
	{
		ssize_t span;

		if ((span = MatchingBracket(str + 1, '[', ']')) < 0)
		{
			if (!(rest = strchr(str, ']')))
			{
				yell("Unmatched bracket in %s (%s)", 
						lval[lvalue], start);
				return endstr(str);
			}
		}
		else
			rest = str + 1 + span;

		str = rest + 1;
	}

	/*
	 * Rvalues may include a function call, slurp up the argument list.
	 */
	if (!lvalue && *str == '(')
	{
		ssize_t span;

		if ((span = MatchingBracket(str + 1, '(', ')')) < 0)
		{
			if (!(rest = strchr(str, ')')))
			{
				yell("Unmatched paren in %s (%s)", 
						lval[lvalue], start);
				return endstr(str);
			}
		}
		else
			rest = str + 1 + span;

		*call = 1;
		str = rest + 1;
	}

	/*
	 * If the entire thing looks to be invalid, perhaps its a 
	 * special built-in expando.  Check to see if it is, and if it
	 * is, then slurp up the first character as valid.
	 * Also note that $: by itself must be valid, which requires
	 * some shenanigans to handle correctly.  Ick.
	 */
	if (str == start || (str == start + 1 && *start == ':'))
	{
	    int	is_builtin = 0;

	    if (!lvalue)
	    {
		/* XXX Hardcoding these is a hack. XXX */
		if (*start == '.' || *start == ',' || *start == ':' ||
		    *start == ';' || *start == '$')
			is_builtin = 1;

		if (is_builtin && (str == start))
			str++;
	    }
	}


	/*
	 * All done!
	 */
	return str;
}


/****************************************************************************/
/*
 * XXX - CE is gonna kill me. :/
 */
/*
 * Here's the plan.  A "Bucket" is a container of named stuff.  You start
 * by creating a Bucket object, and pass that into a bucket collector.  
 * What you should get back is an array of structs that contain a name 
 * and a pointer.  You never own anything in the bucket, it belogns to the
 * collector.  If the collector malloc()s memory, it must provide a free
 * function when you're done with the bucket.
 *
 * struct BucketItem {
 *	const char *name;
 *	void *stuff;
 * };
 *
 * struct Bucket {
 *	int numitems;
 *	int max;
 *	BucketItem *list;
 * };
 *
 */

Bucket *new_bucket (void)
{
	Bucket *b;
	int	i;

	b = (Bucket *)new_malloc(sizeof(Bucket));
	b->numitems = 0;
	b->max = 16;
	b->list = NULL;
	RESIZE(b->list, BucketItem, b->max);
	for (i = 0; i < b->max; i++)
	{
		b->list[i].name = NULL;
		b->list[i].stuff = NULL;
	}
	return b;
}

void	free_bucket (Bucket **b)
{
	if ((*b)->list)
		new_free((char **)&((*b)->list));
	new_free(b);
}

void	add_to_bucket (Bucket *b, const char *name, void *stuff)
{
	int i, newsize;

	if (!b)
	{
		privileged_yell("add_to_bucket: A null bucket was passed in");
		return;
	}

	/*
	 * Determine whether b->list[b->numitems] is an overrun,
	 * and if it is, extend b->list so it's big enough.
	 *
	 * So at this point, b->numitems points at the "next" place
	 * ie, b->max = 10, so b->list[0..9]
	 * So we have to resize if b->numitems == b->max
	 */
	if (b->numitems == b->max)
	{
		/*
		 * so if b->max is 10, then newsize is 20.
		 *, ie, 0..9 -> 0..19
		 */
		newsize = b->max * 2;
		RESIZE(b->list, BucketItem, newsize);
		if (!b->list)
			panic(1, "add_to_bucket: RESIZE(b->list) failed.");

		/* 
		 * Then we go from b->max (10) to newsize - 1 (19)
		 * and then initialize each one.
		 */
		for (i = b->max; i < newsize; i++)
		{
			b->list[i].name = NULL;
			b->list[i].stuff = NULL;
		}

		/*
		 * Then we reset 'max' (oldval: 10) to newsize (20)
		 * and we allow use of b->list[0..19]
		 */
		b->max = newsize;
	}

	b->list[b->numitems].name = name;
	b->list[b->numitems].stuff = stuff;
	b->numitems++;
}

/*
 * vmy_strnicmp() compares given string against a set of strings, 
 * and returns the indexnumber of the first string which matches
 */
int	vmy_strnicmp (size_t len, char *str, ...)
{
	va_list ap;
	int ret = 1;
	char *cmp;

	va_start(ap, str);
	while ((cmp = va_arg(ap, char *)) != NULL)
	{
		if (my_strnicmp(cmp,str,len))
			ret++;
		else
			break;
	}
	va_end(ap);
	return (cmp == NULL) ? 0 : ret;
}

/*
 * XXX I don't like that this iterates over the string byte by byte,
 * especially when 'oldstr' or 'newstr' might be multi-bytes.
 * This runs a non-zero risk (i guess?) of the subsequent bytes of a 
 * utf8 sequence matching up previous bytes of a utf8 sequence.
 * But I don't believe that's valid -- I should just rewrite it to be safer.
 */
char *	substitute_string (const char *string, const char *oldstr, const char *newstr, int case_sensitive, int global)
{
	char *	retval;
	int	i;
	size_t	retvalsize;
	size_t	oldlen, oldcplen;
	size_t	newlen;
	size_t	stringlen;
	const char *	p;
	int	max_matches;

	/* 
	 * For string sensitive cases, we use regular old strncmp,
	 * which doesn't know about utf8 and doesn't care.
	 * For string INSENSITIVE cases, we use our strnicmp,
	 * which DOES know about utf8, and works on code points, not bytes.
	 */
	if (!(oldlen = strlen(oldstr)))
		return malloc_strdup(string);
	oldcplen = quick_code_point_count(oldstr);

	/* XXX ok. so it's lame. */
	if (global)
		max_matches = 99999;
	else
		max_matches = 1;

	newlen = strlen(newstr);
	stringlen = strlen(string);
	retvalsize = (((stringlen / oldlen) + 1) * newlen) + (stringlen * 2);

	/* 
	 * Oh, what the hey...
	 * Remember, malloc_strcat_c() is cheap if the buffer already 
	 * holds enough space for the result!
	 */
	retval = new_malloc(retvalsize);
	i = 0;

	/*
	 * XXX At first glance this seems horrible, but really, it's not!
	 * So strn[i]cmp() is O(N), that's true, but it returns immediately
	 * upon the first failure -- which means that for each non-matching
	 * position, strnicmp is O(1); so doing this for each byte is not
	 * fully O(N^2).
	 */
	for (p = string; *p; p++)
	{
	    /* If we are still willing to make substitutions... */
	    if (max_matches > 0)
	    {
		int	found = 0;

		/* For case sensitive searches, we use strncmp */
		if (case_sensitive == 1)
		{
			if (!strncmp(p, oldstr, oldlen))
				found = 1;
		}
		/* For case insensitive searches, we use my_strnicmp */
		else
		{
			if (!my_strnicmp(p, oldstr, oldcplen))
				found = 1;
		}

		/* If we found it... */
		if (found)
		{
			/* We copy in the replacement string */
			const char *s;

			for (s = newstr; *s; s++)
				retval[i++] = *s;
			if (global == 0)
				global = -1;
			p += oldlen - 1;

			/* We decrease the number of permitted substitutions */
			max_matches--;

			/* And then skip to the next character in src */
			continue;
		}
	    }

	    /* 
	     * If this char doesn't begin the string to be removed,
	     * then we just copy this char and continue on.
	     */
	    retval[i++] = *p;
	}

	retval[i] = 0;

	/* Bail if we accidentally caused a buffer overflow. */
	if (i > (int)retvalsize)
	    panic(1, "substitute [%s] with [%s] in [%s] overflows [%ld] chars", 
			oldstr, newstr, string, (long)retvalsize);

	return retval;
}

/*
 * This function implements the guts of $fix_width(), and is also used by 
 * /set indent.  It returns a copy of 'orig_str' that is guaranteed to be
 * exactly 'newlen' columns when displayed on the screen, either by truncating
 * it or filling it out with 'fillchar'.
 * Justify is -1 for left, 0 for center, or 1 for right; only -1 is supported
 * for now.
 */


/* 
 * fix_string_width - Format "orig_str" so it takes up 'newlen' columns
 *
 * Arguments:
 *	orig_str - The string to be formatted
 *	justify	 - -1 : Left Justify; 0 : Center; 1 : Right Justify
 *	fillchar - A Code Point to use to fill (usually a space)
 *	newlen	 - How many columns the result should be.
 *	chop	 - Whether too-long strings should be chopped off (or not)
 *
 * For this function, "length" means "display columns", as opposed to 
 * a count of "bytes" or "code points" like other columns use.
 *
 * If "orig_str" is longer than "newlen", then it is truncated per "justify"
 *	-1	Left Justify: The right part is chopped off
 *	 0	Center: Approximately the same amount is taken off both sides
 *	 1 	Right Justify: The left part is chopped off.
 *
 * If "orig_str" is shorter than "newlen" then extra chars are added 
 * per "justify"
 *	-1	Left Justify: "fillchar" is added to the end of the string
 *	0	Center: Approximately the same is added to both sides
 *	1	Right Justify: "fillchar" is added to the start of string
 *
 * In any case, the result should be 'newlen' columns.
 * Now technically, 'fillchar' could be a multi-byte character, which may
 * result in a string that can't be exactly 'newlen' columns.  That would
 * be terrible, wouldn't it?  Don't do that.
 *
 */
char *	fix_string_width (const char *orig_str, int justify, int fillchar, size_t newlen, int chop)
{
	/*
	 * Here's the plan....
	 * 
	 * let 'input' be normalized 'new_str'
	 * let 'input_col' = column_count(input).
	 * let 'fill_col' = quick_column_count(fillchar).
	 * we want a string taking up 'newlen' columns,
	 * where 'input_str' is 'justify'd in it.
	 *
	 * We want to calculate:
	 * 1) How many columns to chop off the left of input_str
	 * 2) How many columns to chop off the right of input_str
	 * 3) How many instances of 'pad' on the left
	 * 4) How many instances of 'pad' on the right
	 *
	 *	col_adjustments = newlen - str_col
	 *	if (col_adjustments < 0)	// orig_str too big 
	 *		if (justify = left)
	 *			right_chop = (-col_adjustments)
	 *		else if (justify = center)
	 * 			left_chop = (-col_adjustments) / 2;
	 *			right_chop = (-col_adjustments) - left_chop;
	 *		else (justify = right)
	 *			left_chop = (-col_adjustments)
	 *	else if (col_adjustments == 0)	// orig_str is exactly right
	 *		left_chop = 0
	 *		right_chop = 0
	 *	else 				// orig_str too short
	 *		if (justify = left)
	 *			right_add = (-col_adjustments)
	 *		else if (justify = center)
	 *			left_add = (-col_adjustments) / 2
	 *			right_add = (-col_adjustments) - left_add
	 *		else (justify = right)
	 *			left_add = (-col_adjustments)
	 *
	 *	new_str = orig_str
	 *	for (x = 0, x < left_chop; x++)
	 *		chop_column(&new_str);
	 *	for (x = 0, x < right_chop; x++)
	 *		chop_column_end(&new_str);
	 *	for (x = 0; x < left_add; x++)
	 *		left_padstr += fill_col
	 *	for (x = 0; x < right_add; x++)
	 *		right_padstr += fill_col
	 *	result = left_padstr + new_str + right_padstr
	 */

	char *	orig_str_copy;
	char *	input;
	int	input_cols;
	char	fillstr[16];
	int	adjust_columns;
	int	left_chop = 0, right_chop = 0;
	int	left_add = 0, right_add = 0;
	char *	new_str;
	char *	retval = NULL;
	char *	result;
	size_t	cluep = 0;
	int	x;

	/* Normalize and count the input string */
	orig_str_copy = LOCAL_COPY(orig_str);
	input = new_normalize_string(orig_str_copy, 0, display_line_mangler);
	input_cols = output_with_count(input, 0, 0);

	/* Normalize and count the fill char */
	if (codepoint_numcolumns(fillchar) != 1)
		fillchar = ' ';		/* No time for this nonsense. */

	ucs_to_utf8(fillchar, fillstr, sizeof(fillstr));

	/* How many columns do we need to adjust? */
	adjust_columns = newlen - input_cols;

	/* Now figure out left/right chop, left/right pad */
	if (adjust_columns < 0) {		/* string is too long */
		if (justify == -1)
			right_chop = -adjust_columns;
		else if (justify == 0) {
			left_chop = (-adjust_columns) / 2;
			right_chop = (-adjust_columns) - left_chop;
		} else	/* justify == 1 */ 
			left_chop = -adjust_columns;
	} else if (adjust_columns == 0) {
		left_chop = 0;
		right_chop = 0;
	} else	/* adjust_columns > 0 */ {	/* string is too short */
		if (justify == -1)
			right_add = adjust_columns;
		else if (justify == 0) {
			left_add = adjust_columns / 2;
			right_add = adjust_columns - left_chop;
		} else
			left_add = adjust_columns;
	}

	/* Do the chops */
	new_str = input;
	if (chop)
	{
		chop_columns((unsigned char **)&new_str, left_chop);
		chop_final_columns((unsigned char **)&new_str, right_chop);
	}

	/* Assemble the final string */
	for (x = 0; x < left_add; x++)
		malloc_strcat_c(&retval, fillstr, &cluep);
	malloc_strcat_c(&retval, new_str, &cluep);
	for (x = 0; x < right_add; x++)
		malloc_strcat_c(&retval, fillstr, &cluep);

	result = denormalize_string(retval);

	new_free(&input);
	new_free(&retval);
	return result;
}

/****************************************************************************/
static ssize_t	len_encoder (const char *orig, size_t orig_len, const void *meta, size_t meta_len, char *dest, size_t dest_len)
{
	snprintf(dest, dest_len, "%ld", (long)orig_len);
	return strlen(dest);
}

/* Based in whole or in part on code contributed by SrFrOg in 1999 */
static ssize_t	url_encoder (const char *orig, size_t orig_len, const void *meta, size_t meta_len, char *dest, size_t dest_len)
{
	static const char safe[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
				   "abcdefghijklmnopqrstuvwxyz"
				   "0123456789-._~";
#if 0
        static const char unsafe[] = "`'!@#$%^&*(){}<>~|\\\";? ,/+";
#endif
        static const char hexnum[] = "0123456789ABCDEF";
	size_t	orig_i, dest_i;
	ssize_t	count = 0;

        if (!orig || !dest || dest_len <= 0)
                return -1;

	if (orig_len == 0)
		orig_len = strlen(orig);
	dest_i = 0;

	for (orig_i = 0; orig_i < orig_len; orig_i++)
	{
	    if (orig[orig_i] == 0 || !strchr(safe, orig[orig_i]))
            {
		unsigned c = (unsigned)(unsigned char)orig[orig_i];

		if (dest_i < dest_len)	
			dest[dest_i++] = '%';
		if (dest_i < dest_len)	
			dest[dest_i++] = hexnum[c >> 4];
		if (dest_i < dest_len)	
			dest[dest_i++] = hexnum[c & 0x0f];
		count += 3;
	    }
	    else
	    {
		if (dest_i < dest_len)
			dest[dest_i++] = orig[orig_i];
		count++;
	    }
        }

	if (dest_i >= dest_len)
		dest_i = dest_len - 1;
	dest[dest_i] = 0;
        return dest_i;
}

#define XTOI(x)                                         \
(                                                       \
        ((x) >= '0' && (x) <= '9')                      \
                ? ((x) - '0')                           \
                : ( ((x) >= 'A' && (x) <= 'F')          \
                    ? (((x) - 'A') + 10)                \
                    : ( ((x) >= 'a' && (x) <= 'f')      \
                        ?  (((x) - 'a') + 10)           \
                        : -1                            \
                      )                                 \
                  )                                     \
)

/* Based in whole or in part on code contributed by SrFrOg in 1999 */
static ssize_t	url_decoder (const char *orig, size_t orig_len, const void *meta, size_t meta_len, char *dest, size_t dest_len)
{
	size_t	orig_i, dest_i;
	int	val1, val2;
	ssize_t	count = 0;

        if (!orig || !dest || dest_len <= 0)
                return -1;

	if (!*orig)
	{
		*dest = 0;
		return 0;
	}

	if (orig_len == 0)
		orig_len = strlen(orig);
	dest_i = 0;
	for (orig_i = 0; orig_i < orig_len; orig_i++)
	{
	    if (orig[orig_i] == '%' && orig_len - orig_i >= 2 &&
		(((val1 = XTOI(orig[orig_i+1])) != -1) &&
		 ((val2 = XTOI(orig[orig_i+2])) != -1)))
	    {
		orig_i += 2;
		if (dest_i < dest_len) 
			dest[dest_i++] = (val1 << 4) | val2;
	    }
	    else
		if (dest_i < dest_len) 
			dest[dest_i++] = orig[orig_i];

	    count++;
	}

	if (dest_i >= dest_len)
		dest_i = dest_len - 1;
	dest[dest_i] = 0;
        return dest_i;
}

static ssize_t	enc_encoder (const char *orig, size_t orig_len, const void *meta, size_t meta_len, char *dest, size_t dest_len)
{
	size_t	orig_i, dest_i;
	ssize_t	count = 0;

        if (!orig || !dest || dest_len <= 0)
                return -1;

	if (orig_len == 0)
		orig_len = strlen(orig);
	dest_i = 0;
	for (orig_i = 0; orig_i < orig_len; orig_i++)
	{
		if (dest_i < dest_len)
		   dest[dest_i++] = ((unsigned char)orig[orig_i] >> 4) + 0x41;
		if (dest_i < dest_len)
		   dest[dest_i++] = ((unsigned char)orig[orig_i] & 0x0f) + 0x41;
		count += 2;
	}
	if (dest_i >= dest_len)
		dest_i = dest_len - 1;
	dest[dest_i] = 0;
        return dest_i;
}

static ssize_t	enc_decoder (const char *orig, size_t orig_len, const void *meta, size_t meta_len, char *dest, size_t dest_len)
{
	size_t	orig_i, dest_i;
	ssize_t	count = 0;

        if (!orig || !dest || dest_len <= 0)
                return -1;

	if (!*orig)
	{
		*dest = 0;
		return 0;
	}

	if (orig_len == 0)
		orig_len = strlen(orig);
	dest_i = 0;
	for (orig_i = 0; orig_i + 1 < orig_len; orig_i += 2)
	{
	    if (dest_i < dest_len)
		dest[dest_i++] = ((orig[orig_i] - 0x41) << 4) | 
				  (orig[orig_i+1] - 0x41);
	    count++;
	}

	if (dest_i >= dest_len)
		dest_i = dest_len - 1;
	dest[dest_i] = 0;
        return dest_i;
}


/**********************************************************************/
static char fish64_chars[] =
    "./0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

/*
 * Convert 4 bytes (32 bits) into 6 RADIX64 (printable) characters.
 *
 * Taking 4 bytes, convert them into an (unsigned int) the same way that
 * base64 does.  Then convert that (unsigned int) into 6 characters grabbing
 * 6 bits at a time, working right to left.  
 *
 * The final output byte only contains 2 bits of information, and so it is
 * padded on the left with 4 zero bits.
 *
 * If the input string is short (doesn't contain 4 bytes) then we will just
 * fill in the missing bytes with zeros (which happens during our multiply)
 *
 * This function is (semantically) the same as l64a()
 */
static int	four_bytes_to_fish64 (const char *input, size_t inputlen, unsigned char *output, size_t outputlen)
{
	uint32_t	packet = 0;

        if (outputlen < 6)
		return -1;	/* Sorry Dave, I can't let you do that. */

	/* 
	 * Any bytes beyond the end of the string are treated as nuls.
	 * This is the way FiSH does it, so we must do it too.
	 */
	if (inputlen > 0)	packet = (uint32_t)(unsigned char)input[0];
        packet *= 256;
	if (inputlen > 1)	packet += (uint32_t)(unsigned char)input[1];
        packet *= 256;
	if (inputlen > 2)	packet += (uint32_t)(unsigned char)input[2];
        packet *= 256;
	if (inputlen > 3)	packet += (uint32_t)(unsigned char)input[3];

        output[0] = fish64_chars[ packet & 0x0000003fL          ];
        output[1] = fish64_chars[(packet & 0x00000fc0UL) >> 6   ];
        output[2] = fish64_chars[(packet & 0x0003f000UL) >> 12  ];
        output[3] = fish64_chars[(packet & 0x00fc0000UL) >> 18  ];
        output[4] = fish64_chars[(packet & 0x3f000000UL) >> 24  ];
        output[5] = fish64_chars[(packet & 0xc0000000UL) >> 30  ];

#if 0
yell("The input packet is %c %c %c %c %#x", input[0], input[1], 
					    input[2], input[3], packet);
yell("Byte 0: %#x",  packet & 0x0000003fL          );
yell("Byte 1: %#x", (packet & 0x00000fc0UL) >> 6   );
yell("Byte 2: %#x", (packet & 0x0003f000UL) >> 12  );
yell("Byte 3: %#x", (packet & 0x00fc0000UL) >> 18  );
yell("Byte 4: %#x", (packet & 0x3f000000UL) >> 24  );
yell("Byte 5: %#x", (packet & 0xc0000000UL) >> 30  );
#endif

	return 0;
}

/*
 * Convert 8 bytes (64 bits) into 12 RADIX64 (printable) characters.
 *
 * Chop the 64 bits into two 32-bit blocks, and encode them RIGHT TO LEFT,
 * so the first character is the low 6 bits of the 8th byte, and the sixth
 * character is the high 2 bits of the 5th byte; and the seventh character
 * is the low 6 bits of the 4th byte, and the twelfth character is the high
 * 2 bits of the 1st byte.  Got it?
 */
static int	eight_bytes_to_fish64 (const unsigned char *input, size_t inputlen, unsigned char *output, size_t outputlen)
{
	if (outputlen < 12)
		return -1;	/* Sorry Dave, I can't let you do that. */

	/* We can't do 'input + 4' blindly -- that might be an illegal ptr! */
	if (inputlen >= 4)
	    four_bytes_to_fish64(input + 4, inputlen - 4, output, outputlen);
	else
	    four_bytes_to_fish64(NULL, 0, output, outputlen);

	four_bytes_to_fish64(input, inputlen, output + 6, outputlen - 6);

	return 0;
}

/*
 * Convert binary data into printable text compatable with FiSH ("FiSH64").
 *
 * Convert the input buffer into 8 byte (64 bit) chunks and encode them into
 * 12 output bytes using char8_to_fish64.
 */
static ssize_t	fish64_encoder (const char *orig, size_t orig_len, const void *meta, size_t meta_len, char *dest, size_t dest_len)
{
	size_t	ib	= 0;	/* Input Bytes Consumed */
	size_t	ob	= 0;	/* Output Bytes Generated */

        if (!orig || !dest || dest_len <= 0)
                return -1;

	/*
	 * Convert each 8 byte packet into 12 radix64 chars, and then
	 * advance to the next packet.  
	 */
	for (ib = 0; ib < orig_len && dest_len > ob; )
	{
		if (eight_bytes_to_fish64(orig + ib, orig_len - ib, 
					   dest + ob, dest_len - ob) < 0)
			break;		/* Something went wrong */
		ib += 8;
		ob += 12;
	}

	/* 
	 * Remember, this is the number of output bytes, which is
	 * always a multiple of 12.  FiSH demands no less.
	 */
	if (ob >= dest_len)
		ob = dest_len - 1;
	dest[ob] = 0;
	return ob;
}

/* * * * */
static	int	fishbyte (int	character)
{
	char *	p;

	p = strchr(fish64_chars, character);
	if (p == NULL)
		return -1;
	else
		return p - fish64_chars;
}

/*
 * Convert 6 RADIX64 (printable) characters into 4 binary bytes (32 bits)
 *
 * For each RADIX64 character, take the low 6 bits, and assign them RIGHT
 * TO LEFT to the 32 bits.  
 *
 * The final radix64 character contains only 2 bits of information, so we
 * ignore the high 6 bits for it.
 *
 * If the input string is short (doesn't contain 6 radix chars) then we will 
 * just pretend they contained zero bits.
 *
 * This function is (semantically) the same as a64l() 
 */
static int	fish64_to_four_bytes (const char *input, size_t inputlen, char *output, size_t outputlen)
{
	int	decoded[6] = {0};

	if (outputlen < 4)
		return -1;	/* Sorry Dave, I can't let you do that */

	/*
	 * Why do I do it this way when FiSH just packs it into an int and
	 * peels off 6 bits at a time?  Because this way does not depend on 
	 * undefined considerations for how bits are stored in ints.
	 */

	if (inputlen >= 6)	decoded[5] = fishbyte(input[5]);
	if (inputlen >= 5)	decoded[4] = fishbyte(input[4]);
	if (inputlen >= 4)	decoded[3] = fishbyte(input[3]);
	if (inputlen >= 3)	decoded[2] = fishbyte(input[2]);
	if (inputlen >= 2)	decoded[1] = fishbyte(input[1]);
	if (inputlen >= 1)	decoded[0] = fishbyte(input[0]);


	/* Char 5 Char 4 Char 3 Char 2 Char 1 Char 0  <- input */
	/* 543210 543210 543210 543210 543210 543210  <- input bit */
	/* ....00 000000 111111 112222 222233 333333  <- output byte */
	/* ....76 543210 765432 107654 321076 543210  <- output bit */

	/*  0x03   0x3F   0x3F   0x30   0x3C   0x3F
				 0x0F	0x03	     */

	output[0] =  ((decoded[5] & 0x03) << 6);	/* 2 high bits */
	output[0] += (decoded[4] & 0x3F);		/* 6 low bits */

	output[1] =  ((decoded[3] & 0x3F) << 2);	/* 6 high bits */
	output[1] += ((decoded[2] & 0x30) >> 4);	/* 2 low bits */

	output[2] =  ((decoded[2] & 0x0F) << 4);	/* 4 high bits */
	output[2] += ((decoded[1] & 0x3C) >> 2);	/* 4 low bits */

	output[3] =  ((decoded[1] & 0x03) << 6);	/* 2 high bits */
	output[3] += (decoded[0] & 0x3F);		/* 6 low bits */

#if 0
yell("Input Byte 0: %#x",  decoded[0]);
yell("Input Byte 1: %#x",  decoded[1]);
yell("Input Byte 2: %#x",  decoded[2]);
yell("Input Byte 3: %#x",  decoded[3]);
yell("Input Byte 4: %#x",  decoded[4]);
yell("Input Byte 5: %#x",  decoded[5]);
yell("The output packet is %c %c %c %c %#x", output[0], output[1], 
					    output[2], output[3], decoded);
#endif

	return 0;
}

/*
 * Convert 12 RADIX64 (printable) characters into 8 bytes (64 bits)
 *
 * Chop the 12 RADIX64 characters into two 6 character blocks, and then
 * decode them RIGHT TO LEFT, so the first character goes into the low 6
 * bits of the 8th byte, and the sixth character goes into the high 2 bits
 * of the 5th byte; and the seventh character goes into the low 6 bits of 
 * the 4th byte; and the twelfth character goes into the high 2 bits of the
 * 1st byte.  Got it?
 */
static int	fish64_to_eight_bytes (const char *input, size_t inputlen, char *output, size_t outputlen)
{
	if (outputlen < 8)
		return -1;	/* Sorry Dave, I can't let you do that. */

	/* We can't do 'input + 6' blindly -- that might be an illegal ptr! */
	if (inputlen >= 6)
	    fish64_to_four_bytes(input + 6, inputlen - 6, output, outputlen);
	else
	    fish64_to_four_bytes(NULL, 0, output, outputlen);

	fish64_to_four_bytes(input, inputlen, output + 4, outputlen - 4);
	return 0;
}

/*
 * Convert FiSH compatable data ("FiSH64") into binary data
 *
 * Convert the input buffer into 12 byte chunks and decode them into 8 bytes
 * of binary data using fish64_to_eight_bytes.
 */
static ssize_t	fish64_decoder (const char *orig, size_t orig_len, const void *meta, size_t meta_len, char *dest, size_t dest_len)
{
	size_t	ib	= 0;	/* Input Bytes Consumed */
	size_t	ob	= 0;	/* Output Bytes Generated */

        if (!orig || !dest || dest_len <= 0)
                return -1;

	/*
	 * A sanity check -- our input buffer must contain only FiSH64 chars
	 * XXX Yea. yea yea, this is lame.  I'll take it out later.
	 */
	if (strspn(orig, fish64_chars) != orig_len)
		yell("The FiSH64 string contains non-fish64 chars: [%s]", orig);

	/*
	 * Convert each 12 byte packet into 8 output bytes, and then
	 * advance to the next packet.  
	 */
	for (ib = 0; ib < orig_len && dest_len > ob; )
	{
		if (fish64_to_eight_bytes(orig + ib, orig_len - ib, 
					   dest + ob, dest_len - ob) < 0)
			break;		/* Something went wrong */
		ib += 12;
		ob += 8;
	}

	/* 
	 * Remember, this is the number of output bytes, which is
	 * always a multiple of 8.  FiSH demands no less.
	 */
	return ob;
}

/*****************************************************************************/
/* Begin BSD licensed code */
/*
 * Copyright (c) 1995-2001 Kungliga Tekniska Ho"gskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * This is licensed under the 3-clause BSD license, which is found in compat.c
 */
static char base64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int      posfunc (const char *base, char c)
{
    const char *p;
    for (p = base; *p; p++)
        if (*p == c)
            return p - base;
    return -1;
}

static ssize_t	b64_general_encoder (const char *orig, size_t orig_len, const void *meta, size_t meta_len, char *dest, size_t dest_len, const char *base)
{
	size_t	orig_i, dest_i;

        if (!orig || !dest || dest_len <= 0)
                return -1;

	if (orig_len == 0)
		orig_len = strlen(orig);
	dest_i = 0;
	for (orig_i = 0; orig_i < orig_len; )
	{
	    unsigned	c = (unsigned)(unsigned char)orig[orig_i];
	    c *= 256;
	    if (orig_i+1 < orig_len)
		c += (unsigned)(unsigned char)orig[orig_i+1];
	    c *= 256;
	    if (orig_i+2 < orig_len)
		c += (unsigned)(unsigned char)orig[orig_i+2];

	    if (dest_i < dest_len)
		dest[dest_i]   = base[(c & 0x00fc0000) >> 18];
	    if (dest_i+1 < dest_len)
		dest[dest_i+1] = base[(c & 0x0003f000) >> 12];
	    if (dest_i+2 < dest_len)
		dest[dest_i+2] = base[(c & 0x00000fc0) >> 6];
	    if (dest_i+3 < dest_len)
		dest[dest_i+3] = base[(c & 0x0000003f) >> 0];

	    if (orig_i+2 >= orig_len && dest_i + 3 < dest_len)
		dest[dest_i+3] = '=';
	    if (orig_i+1 >= orig_len && dest_i + 2 < dest_len)
		dest[dest_i+2] = '=';

	    dest_i += 4;
	    orig_i += 3;
	}
	if (dest_i >= dest_len)
		dest_i = dest_len - 1;
	dest[dest_i] = 0;
        return dest_i;
}

static ssize_t	b64_encoder (const char *orig, size_t orig_len, const void *meta, size_t meta_len, char *dest, size_t dest_len)
{
	return b64_general_encoder(orig, orig_len, meta, meta_len, dest, dest_len, base64_chars);
}


#define DECODE_ERROR 0xFFFFFFFF
static unsigned int     token_decode (const char *base, const char *token)
{
    int i;
    unsigned int val = 0;
    int marker = 0;

    if (strlen(token) < 4)
        return DECODE_ERROR;
    for (i = 0; i < 4; i++) {
        val *= 64;
        if (token[i] == '=')
            marker++;
        else if (marker > 0)
            return DECODE_ERROR;
        else
            val += posfunc(base, token[i]);
    }
    if (marker > 2)
        return DECODE_ERROR;
    return (marker << 24) | val;
}

static ssize_t	b64_general_decoder (const char *orig, size_t orig_len, const void *meta, size_t meta_len, char *dest, size_t dest_len, const char *base)
{
	size_t	orig_i, dest_i;
	ssize_t	count = 0;

        if (!orig || !dest || dest_len <= 0)
                return -1;

	if (!*orig)
	{
		*dest = 0;
		return 0;
	}

	if (orig_len == 0)
		orig_len = strlen(orig);
	dest_i = 0;
	for (orig_i = 0; orig_i < orig_len; orig_i += 4)
	{
	    unsigned val = token_decode(base, orig + orig_i);
	    unsigned marker = (val >> 24) & 0xff;

	    if (val == DECODE_ERROR)
		break;

	    if (dest_i < dest_len)
	        dest[dest_i++] = (val >> 16) & 0xff;
	    if (marker < 2)
		if (dest_i < dest_len)
		    dest[dest_i++] = (val >> 8) & 0xff;
	    if (marker < 1)
		if (dest_i < dest_len)
		    dest[dest_i++] = val & 0xff;

	    count += 3;
	}
	if (dest_i >= dest_len)
		dest_i = dest_len - 1;
	dest[dest_i] = 0;
        return dest_i;
}

static ssize_t	b64_decoder (const char *orig, size_t orig_len, const void *meta, size_t meta_len, char *dest, size_t dest_len)
{
	return b64_general_decoder(orig, orig_len, meta, meta_len, dest, dest_len, base64_chars);
}

/* End BSD licensed stuff (see compat.c!) */

static ssize_t	sed_encoder (const char *orig, size_t orig_len, const void *meta, size_t meta_len, char *dest, size_t dest_len)
{
	size_t	len;

        if (!orig || !dest || dest_len <= 0)
                return -1;

	if (dest_len < orig_len)
		len = dest_len;
	else
		len = orig_len;

	memmove(dest, orig, len);
	encrypt_sed(dest, len, meta, meta_len);
	return len;
}

static ssize_t	sed_decoder (const char *orig, size_t orig_len, const void *meta, size_t meta_len, char *dest, size_t dest_len)
{
	size_t	len;

        if (!orig || !dest || dest_len <= 0)
                return -1;

	if (dest_len < orig_len)
		len = dest_len;
	else
		len = orig_len;

	memmove(dest, orig, len);
	decrypt_sed(dest, len, meta, meta_len);
	return len;
}

static ssize_t	ctcp_encoder (const char *orig, size_t orig_len, const void *meta, size_t meta_len, char *dest, size_t dest_len)
{
	size_t	orig_i, dest_i;

        if (!orig || !dest || dest_len <= 0)
	{
		if (!orig)
			yell("ctcp_encoder: orig is NULL");
		if (!dest)
			yell("ctcp_encoder: dest is NULL");
		if (dest_len <= 0)
			yell("ctcp_encoder: dest_len <= 0");
                return -1;
	}

	if (orig_len == 0)
		orig_len = strlen(orig);
	dest_i = 0;
	for (orig_i = 0; orig_i < orig_len; orig_i++)
	{
	    switch (orig[orig_i])
	    {
		case CTCP_DELIM_CHAR:
		    if (dest_i < dest_len) dest[dest_i++] = CTCP_QUOTE_CHAR;
		    if (dest_i < dest_len) dest[dest_i++] = 'a';
		    break;
		case '\n':
		    if (dest_i < dest_len) dest[dest_i++] = CTCP_QUOTE_CHAR;
		    if (dest_i < dest_len) dest[dest_i++] = 'n';
		    break;
		case '\r':
		    if (dest_i < dest_len) dest[dest_i++] = CTCP_QUOTE_CHAR;
		    if (dest_i < dest_len) dest[dest_i++] = 'r';
		    break;
		case CTCP_QUOTE_CHAR:
		    if (dest_i < dest_len) dest[dest_i++] = CTCP_QUOTE_CHAR;
		    if (dest_i < dest_len) dest[dest_i++] = CTCP_QUOTE_CHAR;
		    break;
		case '\0':
		    if (dest_i < dest_len) dest[dest_i++] = CTCP_QUOTE_CHAR;
		    if (dest_i < dest_len) dest[dest_i++] = '0';
		    break;
		default:
		    if (dest_i < dest_len) dest[dest_i++] = orig[orig_i];
		    break;
	    }
	}

	if (dest_i >= dest_len)
		dest_i = dest_len - 1;
	dest[dest_i] = 0;
        return dest_i;
}

static ssize_t	ctcp_decoder (const char *orig, size_t orig_len, const void *meta, size_t meta_len, char *dest, size_t dest_len)
{
	size_t	orig_i, dest_i;

        if (!orig || !dest || dest_len <= 0)
                return -1;

	if (!*orig)
	{
		*dest = 0;
		return 0;
	}

	if (orig_len == 0)
		orig_len = strlen(orig);
	dest_i = 0;
	for (orig_i = 0; orig_i < orig_len; orig_i++)
	{
	    if (orig[orig_i] == CTCP_QUOTE_CHAR)
	    {
		orig_i++;
		if (orig[orig_i] == CTCP_QUOTE_CHAR) {
		    if (dest_i < dest_len) dest[dest_i++] = CTCP_QUOTE_CHAR;
		} else if (orig[orig_i] == 'a') {
		    if (dest_i < dest_len) dest[dest_i++] = CTCP_DELIM_CHAR;
		} else if (orig[orig_i] == 'n') {
		    if (dest_i < dest_len) dest[dest_i++] = '\n';
		} else if (orig[orig_i] == 'r') {
		    if (dest_i < dest_len) dest[dest_i++] = '\r';
		} else if (orig[orig_i] == '0') {
		    if (dest_i < dest_len) dest[dest_i++] = '\0';
		} else {
		    if (dest_i < dest_len) dest[dest_i++] = orig[orig_i];
		}
	    }
	    else
	        if (dest_i < dest_len) dest[dest_i++] = orig[orig_i];
	}

	if (dest_i >= dest_len)
		dest_i = dest_len - 1;
	dest[dest_i] = 0;
        return dest_i;
}

static ssize_t	null_encoder (const char *orig, size_t orig_len, const void *meta, size_t meta_len, char *dest, size_t dest_len)
{
	size_t	orig_i, dest_i;
	ssize_t	count = 0;

        if (!orig || !dest || dest_len <= 0)
                return -1;

	if (!*orig)
	{
		*dest = 0;
		return 0;
	}

	if (orig_len == 0)
		orig_len = strlen(orig);
	dest_i = 0;
	for (orig_i = 0; orig_i < orig_len; orig_i++)
	{
	    if (dest_i < dest_len)
		dest[dest_i++] = orig[orig_i];
	    count++;
	}
	if (dest_i >= dest_len)
		dest_i = dest_len - 1;
	dest[dest_i] = 0;
        return dest_i;
}

static ssize_t	crypt_encoder (const char *orig, size_t orig_len, const void *meta, size_t meta_len, char *dest, size_t dest_len)
{
	return 0;
}

static ssize_t	crypt_decoder (const char *orig, size_t orig_len, const void *meta, size_t meta_len, char *dest, size_t dest_len)
{
	return 0;
}

static ssize_t	all_encoder (const char *orig, size_t orig_len, const void *meta, size_t meta_len, char *dest, size_t dest_len)
{
	char	*all_xforms;

        if (!dest || dest_len <= 0)
                return -1;

	all_xforms = valid_transforms();
	strlcpy(dest, all_xforms, dest_len);
	new_free(&all_xforms);
	return strlen(dest);
}

static ssize_t	sha256_encoder (const char *orig, size_t orig_len, const void *meta, size_t meta_len, char *dest, size_t dest_len)
{
        if (!orig || !dest || dest_len <= 0)
                return -1;

	sha256str(orig, orig_len, dest);
	return strlen(dest);
}

#ifdef HAVE_ICONV
ssize_t iconv_list_size = 0;
struct Iconv_stuff **iconv_list = NULL;

int my_iconv_open (iconv_t *forward, iconv_t *reverse, const char *stuff2)
{
	size_t pos, len;
	char *stuff, *fromcode, *tocode, *option, *tmp;
	int	size;

	stuff = LOCAL_COPY(stuff2);
	size = strlen(stuff) + 1;
	tmp = alloca(size);
	len = strlen(stuff);
	for (pos = 0; pos < len && stuff[pos] != '/'; pos++);
	if (stuff[pos] != '/')
	{
		if (x_debug & DEBUG_UNICODE)
			yell ("Unicode debug: Incomplete encoding information: %s", stuff);
		return 1;
	}
	stuff[pos] = '\0';
	fromcode = stuff;
	tocode = stuff + (pos + 1);

	for (;pos<len && stuff[pos] != '/'; pos++);

	if (stuff[pos] == '/')
	{
		stuff[pos] = '\0';
		option = stuff + (pos + 1);
	}
	else
		option = NULL;
	
	/* forward: tocode (+option), fromcode*/
	if (forward)
	{
		strlcpy(tmp, tocode, size);
		if (option)
		{
			len = strlen(tocode);
			tmp[len] = '/';
			tmp[len + 1] = '\0';
			strlcpy(tmp + len + 1, option, size - len - 1);
		}
		if ((*forward = iconv_open(tmp, fromcode)) == (iconv_t) (-1))
		{
			if (x_debug & DEBUG_UNICODE)
				yell ("Unicode debug: my_iconv_open() fwd: iconv_open(%s, %s) failed.",
					tmp, fromcode);
			return 1;
		}

	}
	/* reverse: fromcode (+option), tocode*/
	if (reverse)
	{
		strlcpy(tmp, fromcode, size);
		if (option)
		{
			len = strlen(fromcode);
			tmp[len] = '/';
			tmp[len +1] = '\0';
			strlcpy(tmp + len + 1, option, size - len - 1);
		}
		if ((*reverse = iconv_open(tmp, tocode)) == (iconv_t) (-1))
		{
			if (x_debug & DEBUG_UNICODE)
				yell ("Unicode debug: my_iconv_open() rev: iconv_open(%s, %s) failed.",
					tmp, tocode);
			return 1;
		}
	}
	return 0;
}

static ssize_t	iconv_recoder (const char *orig, size_t orig_len, const void *meta, size_t meta_len, char *dest, size_t dest_len)
{
	size_t	orig_left = orig_len, 
		dest_left = dest_len, 
		n, 
		close_it = 1;
	int	id;
	char 	*dest_ptr;
	char 	*orig_ptr;
	iconv_t encodingx;

        if (!orig || !dest || dest_len <= 0)
                return -1;

	dest_ptr = (char *) dest;

	/* 
	 * Old libiconv(), esp as used by FreeBSD expects the 2nd argument
	 * to iconv() to be (const char **), but POSIX iconv() requires
	 * it to be (char **), and these cannot be reconciled through a 
	 * cast [see the comp.lang.c faq for a good explanation], so I chose
	 * to have non-standard iconv() result in the warning.
	 *
	 * The xform subsystem requires all inputs to be treated
	 * as const, so as a special case, I cast away the cast for
	 * iconv because i trust it to behave itself.
	 */
	orig_ptr = (char *)(intptr_t)orig;

	if ((*(const char *)meta) == '+' || (*(const char*)meta) == '-')
	{
		if (strlen((const char *)meta) <= 1)
			return 0;
		id = (size_t) strtol((const char *)meta +1, NULL, 10);
		if (id < iconv_list_size && iconv_list[id] != NULL)
		{
			if ((*(const char *)meta) == '+')
			{
				if (iconv_list[id]->forward == NULL)
				{
					if (x_debug & DEBUG_UNICODE)
						yell ("Unicode debug: iconv_recoder(): iconv identifier: %i has no forward", id);
					return 0;
				}
				encodingx = iconv_list[id]->forward;
			}
			else
			{
				if (iconv_list[id]->reverse == NULL)
				{
					if (x_debug & DEBUG_UNICODE)
						yell ("Unicode debug: iconv_recoder(): iconv identifier: %i has no reverse", id);
					return 0;
				}
				encodingx = iconv_list[id]->reverse;
			}
			close_it = 0;
		}
		else
		{
			if (x_debug & DEBUG_UNICODE)
			{
				yell ("Unicode debug: iconv_recoder(): no such iconv identifier: %i", id);
			}
			return 0;
		}
	}
	else
	{
		if (my_iconv_open(&encodingx, NULL, (const char *) meta))
			return 0;
	}

	/* Stuff seems to be working... */
	while (iconv(encodingx, &orig_ptr, &orig_left, &dest_ptr, &dest_left) != 0)
	{
		/* I *THINK* this is a hack. */
		if (errno == EINVAL || errno == EILSEQ)
		{
			orig_ptr++;
			orig_left--;
			continue;
		}
		break;
	}
	if (close_it)
		iconv_close(encodingx);
	return dest_len - dest_left;
}

#endif


struct Transformer
{
	int	refnum;
	const char *	name;
	int	takes_meta;
	int	recommended_size;
	int	recommended_overhead;
	ssize_t	(*encoder) (const char *, size_t, const void *, size_t, char *, size_t);
	ssize_t	(*decoder) (const char *, size_t, const void *, size_t, char *, size_t);
};

struct Transformer default_transformers[] = {
{	0,	"URL",		0, 3, 8,  url_encoder,	  url_decoder	 },
{	0,	"ENC",		0, 2, 8,  enc_encoder,	  enc_decoder	 },
{	0,	"LEN",		0, 1, 0,  len_encoder,	  len_encoder	 },
{	0,	"B64",		0, 2, 8,  b64_encoder,	  b64_decoder	 },
{	0,	"FISH64",	0, 2, 16, fish64_encoder, fish64_decoder },
{	0,	"SED",		1, 2, 8,  sed_encoder,	  sed_decoder	 },
{	0,	"CTCP",		0, 2, 8,  ctcp_encoder,	  ctcp_decoder	 },
{	0,	"NONE",		0, 1, 8,  null_encoder,	  null_encoder	 },
{	0,	"DEF",		0, 1, 16, crypt_encoder,  crypt_decoder	 },
{	0,	"SHA256",	0, 0, 65, sha256_encoder, sha256_encoder },
#ifdef HAVE_SSL
{	0,	"BF",		1, 1, 8,  blowfish_encoder, blowfish_decoder },
{	0,	"CAST",		1, 1, 8,  cast5_encoder,    cast5_decoder    },
{	0,	"AES",		1, 1, 8,  aes_encoder,	    aes_decoder	     },
{	0,	"AESSHA",	1, 1, 8,  aessha_encoder,   aessha_decoder   },
{	0,	"FISH",		1, 1, 16, fish_encoder,     fish_decoder     },
#endif
#ifdef HAVE_ICONV
{	0,	"ICONV",	1, 4, 16, iconv_recoder,  iconv_recoder },
#endif
{	0,	"ALL",		0, 0, 256, all_encoder,	  all_encoder	},
{	-1,	NULL,		0, 0, 0,   NULL,	  NULL		}
};

int	max_transform;
int	max_number_of_transforms = 256;
struct Transformer transformers[256];		/* XXX */
int	NONE_xform, URL_xform, ENC_xform, B64_xform, FISH64_xform;
int	CTCP_xform, SHA256_xform;

/*
 * transform_string -- Transform some bytes from one form into another form
 *
 * Args:
 *  'type'	- The type of transformation.  You must fetch this value by
 *		  calling lookup_transform().
 *  'encoding'	- 0 for "decoding" : The input is encoded in the 'type' and
 *			you need to recover the original data
 *		  1 for "encoding" : The input is raw data and you need it
 *			to be encoded in 'type'.
 *  'meta'	- A word separated list of meta argument(s) required by the
 *		  transform.  May be NULL if the transform doesn't require 
 *		  an argument.
 *		  Example:  Encryption require a password argument, so you 
 *			    would pass it here.  
 *		  Note: No transforms currently require multiple arguments
 *		        so there are no examples I can give for that, but it
 *		        is definitely supported for future use.
 *  'orig_str'	- The data to be transformed
 *  'orig_str_len' - The number of bytes in 'orig_str' to transform.
 *		     Note: This function does not work on C strings, so if you
 *		        pass in a C string you have to tell it how long it is!
 *  'dest_str'	   - Where to put the transformed string (output)
 *  'dest_str_len' - How many bytes 'dest_str' has.
 *		     Note: If 'dest_str' isn't big enough to hold all of the
 *		           transformed output, it will be truncated. 
 *
 * Returns:
 *	Returns 0 if 'type' is not a valid transform.
 *	Returns the return value of the transform, which should be the
 *		number of bytes written to 'dest_str'.
 */
size_t	transform_string (int type, int encodingx, const char *meta, const char *orig_str, size_t orig_str_len, char *dest_str, size_t dest_str_len)
{
	int	x;
	int	meta_len;

	meta_len = meta ? strlen(meta) : 0;

	*dest_str = 0;
	for (x = 0; transformers[x].name; x++)
	{
	    if (transformers[x].refnum == type)
	    {
		if (encodingx == XFORM_ENCODE)
			return transformers[x].encoder(orig_str, orig_str_len, meta, meta_len, dest_str, dest_str_len);
		else if (encodingx == XFORM_DECODE)
			return transformers[x].decoder(orig_str, orig_str_len, meta, meta_len, dest_str, dest_str_len);
		else
		{
			syserr(FROMSERV, "transform_string: type [%d], encodingx [%d], is not %d or %d",
				type, encodingx, XFORM_ENCODE, XFORM_DECODE);
			return 0;
		}
	    }
	}
	return 0;
}

/*
 * transform_string_dyn -- a more pleasant front end to transform_string
 *
 * Args:
 *  'type'	   - The string transformation you want to use.
 *		     For now we only support non-encryption transforms.
 *		     You should prefix with + to encode, - to decode.
 *  'orig_str'	   - The string you want to transform
 *  'orig_str_len' - The number of bytes of 'orig_str' to transform
 *		     If this value is 0, then will do a strlen(orig_str)
 *  'dest_str_len' - If non-NULL, the number of bytes written to return value.
 *			THIS IS NOT THE SAME AS THE MALLOC() SIZE!
 *			We don't return that.  We could, though...
 *
 * Return value:
 *  Returns the transformed buffer.  
 *  Return value may or may not be nul terminated depending on the transform!
 *  You *MUST* new_free() the return value later.
 */
char *	transform_string_dyn (const char *type, const char *orig_str, size_t orig_str_len, size_t *my_dest_str_len)
{
	char *	dest_str;
	size_t	dest_str_len;
	int	transform, numargs;
	int	expansion_size, expansion_overhead;
	int	direction;
	int	retval;

	if (*type == '-')
	{
		direction = XFORM_DECODE;
		type++;
	}
	else if (*type == '+')
	{
		direction = XFORM_ENCODE;
		type++;
	}
	else
		direction = XFORM_ENCODE;

	if (orig_str_len <= 0)
		orig_str_len = strlen(orig_str);

	if ((transform = lookup_transform(type, &numargs, 
				&expansion_size, &expansion_overhead)) == -1)
	{
		yell("Invalid transform: [%s] to transform_string_dyn", type);
		return NULL;
	}

	dest_str_len = orig_str_len * expansion_size + expansion_overhead;
	if (dest_str_len <= 0)
	{
		yell("Transform [%s] on a string of size [%ld] resulted in "
		     "an invalid destination size of [%ld].  Refusing to "
		     " transform.  Sorry!", 
			type, (long)orig_str_len, (long)dest_str_len);
		if (my_dest_str_len)
			*my_dest_str_len = orig_str_len;
		return malloc_strdup(orig_str);
	}

	dest_str = (char *)new_malloc(dest_str_len);
	retval = transform_string(transform, direction, NULL, 
				  orig_str, orig_str_len, 
				  dest_str, dest_str_len);

	if (retval <= 0)	/* It failed */
	{
		yell("Transform [%s] failed to transform string [%s] (%ld bytes) (returned %d)", 
			type, orig_str, (long)orig_str_len, retval);
		new_free(&dest_str);
		if (my_dest_str_len)
			*my_dest_str_len = 0;
		return NULL;
	}

	if (my_dest_str_len)
		*my_dest_str_len = retval;
		/* *my_dest_str_len = dest_str_len; */	/* This was the old value */
	return dest_str;
}

int	lookup_transform (const char *str, int *numargs, int *expansion_size, int *expansion_overhead)
{
	int	x = 0;

	for (x = 0; transformers[x].name; x++)
	{
		if (!my_stricmp(transformers[x].name, str))
		{
			*numargs = transformers[x].takes_meta;
			*expansion_size = transformers[x].recommended_size;
			*expansion_overhead = transformers[x].recommended_overhead;
			return transformers[x].refnum;
		}
	}
	return -1;
}

char *	valid_transforms (void)
{
	int	x = 0;
	char *	retval = NULL;
	size_t	cluep;

	for (x = 0; transformers[x].name; x++)
	{
		malloc_strcat_word_c(&retval, space, 
			transformers[x].name, DWORD_NO, &cluep);
	}
	return retval;
}

static int	register_transform (const char *name, int takes_meta, int expansion_size, int expansion_overhead, ssize_t (*encoder)(const char *, size_t, const void *, size_t, char *, size_t), ssize_t (*decoder)(const char *, size_t, const void *, size_t, char *, size_t))
{
	int	i;

	for (i = 0; i < max_number_of_transforms; i++)
	{
		if (transformers[i].refnum == -1)
		{
			transformers[i].refnum = i;
			transformers[i].name = malloc_strdup(name);
			transformers[i].takes_meta = takes_meta;
			transformers[i].encoder = encoder;
			transformers[i].decoder = decoder;
			transformers[i].recommended_size = expansion_size;
			transformers[i].recommended_overhead = expansion_overhead;
			return i;
		}
	} 
	return -1;
}

static int	unregister_transform (int i)
{
	/* We don't change 'refnum' so this entry doesn't get re-used! */
	/* The warning generated here is intentional */
	new_free((char **) &transformers[i].name);
	transformers[i].takes_meta = 0;
	transformers[i].encoder = NULL;
	transformers[i].decoder = NULL;
	return i;
}

void	init_transforms (void)
{
	int	i = 0;
	int	numargs;
	int	d1, d2;

	for (i = 0; i < max_number_of_transforms; i++)
	{
		transformers[i].refnum = -1;
		transformers[i].name = NULL;
		transformers[i].takes_meta = 0;
		transformers[i].recommended_size = 0;
		transformers[i].recommended_overhead = 0;
		transformers[i].encoder = NULL;
		transformers[i].decoder = NULL;
	}

	for (i = 0; default_transformers[i].name; i++)
	{
		register_transform(default_transformers[i].name,
				   default_transformers[i].takes_meta,
				   default_transformers[i].recommended_size,
				   default_transformers[i].recommended_overhead,
				   default_transformers[i].encoder,
				   default_transformers[i].decoder);
	}

	NONE_xform = lookup_transform("NONE", &numargs, &d1, &d2);
	URL_xform = lookup_transform("URL", &numargs, &d1, &d2);
	ENC_xform = lookup_transform("ENC", &numargs, &d1, &d2);
	B64_xform = lookup_transform("B64", &numargs, &d1, &d2);
	FISH64_xform = lookup_transform("FISH64", &numargs, &d1, &d2);
	CTCP_xform = lookup_transform("CTCP", &numargs, &d1, &d2);
	SHA256_xform = lookup_transform("SHA256", &numargs, &d1, &d2);
}


/* 
 * num_code_points(s) returns the number of code points in string s.
 * However, if it "chokes" on one (1) sequence it mistakes for a code point,
 * it returns this code point's index number negatively. 
 * If the string doesn't end up "matching" (code points pluss
 * trailing bytes should count up to the length of the string), nil (0)
 * is returned.
 */
int	num_code_points(const char *i)
{
	/*
	 * I apologise the extensive use of one letter variables.
	 * 'n' indexes current character of 'input'.
	 * 'l' is the length of 'input'.
	 * 't' is a temporary variable used to indicate number of
	 * 	trailing bytes in a sequence.
	 * 'v' is the number of valid first bytes..
	 * 'd' is the total of trailing bytes.
	 * 's' is the total of seven bit bytes.
	 */

	int n = 0, l, t = 0, v = 0, d = 0, s = 0;
	l = strlen (i);
	
	for (n = 0; n < l; n++)
	{
		if (t)
		{
			if (((i[n] & (1 << 7)) && !(i[n] & (1 << 6))))
			{
				t--;
				continue;
			}
			return (s + v) * -1;
		}
		/* This is not an eight bit character. */
		if (!(i[n] & (1 << 7)))
		{
			s++;
			continue;
		}
		
		/* This is most presumably the first byte of a sequence. */
		v++;
	
		/* 
		 * I was thinking of doing this in a for (...;...;...) manner,
		 * but as a matter of fact, this is *probably* somewhat
		 * better.
		 */
		if (!(i[n] & (1 << 6))) t = 0;
		else if (!(i[n] & (1 << 5))) t = 1;
		else if (!(i[n] & (1 << 4))) t = 2;
		else if (!(i[n] & (1 << 3))) t = 3;
		else if (!(i[n] & (1 << 2))) t = 4;
		else if (!(i[n] & (1 << 1))) t = 5;
		else
		{
			/* Invalid. 1111 1111 is not a good first byte. */
			return 0;
		}
		d += t;
	}

	/* At this juncture, d + v should be l. */
	if (s + d + v != l)
		return 0;
	return v + s;
}

ssize_t	findchar_quoted (const char *source, int delim)
{
	const char *p;

	for (p = source; *p; p++)
	{
		if (*p == '\\' && p[1])
			p++;
		else if (*p == delim)
			break;
	}

	if (*p)
		return p - source;
	else
		return -1;
}

/*
 * recode_with_iconv -- copy and iconv convert a string
 * Arguments:
 *	from 	- The encoding of (*data)
 *		  IF NULL: error (returns 0)
 *	to	- The encoding you wish (*data) would be
 *		  IF NULL: "UTF-8" will be used.
 *	data	- A pointer to *MALLOCED SPACE*
 *		  The input value of (*data) _may be free'd_.
 *		  The return value of (*data) _must be free'd_.
 *		  IF NULL: error (returns 0)
 *	numbytes - A pointer to an integer of how big (*data) is. 
 *		   Only (*numbytes) of (*data) will be converted!
 *		   The value of (*numbytes) will change if (*data) is changed.
 *		   IF NULL: error (returns 0)
 * Return Value:
 *	0	- No changes were made (because of error)
 *	>0	- The number of bytes in the output string
 *
 * Inspired liberally by howl's iconv support above.
 */
int	recode_with_iconv (const char *from, const char *to, char **data, size_t *numbytes)
{
	iconv_t	iref;
	char *	dest_ptr = NULL;
	size_t	dest_left = 0;
	size_t	dest_size = 0;
	int	n;
	char *	work_data;
	char *	retstr = NULL;

	/*
	 * Some sanity checks!
	 */
	if (!from)
		return 0;	/* Don't change anything */
	if (!numbytes || !*numbytes)
		return 0;
	if (!data || !*data)
		return 0;

	if (!my_stricmp(from, "CP437_BUILTIN"))
	{
		size_t 	dd_len = 0;
		retstr = cp437_to_utf8(*data, *numbytes, &dd_len);
		*numbytes = dd_len;
	}
	else
	{
		if (!to)
			to = "UTF-8";	/* For now, convert to UTF8 is the default */

		/*
		 * So iconv(3) says I need to create an iconv_t with iconv_open
		 */
		if ((iref = iconv_open(to, from)) == (iconv_t)-1)
		{
			yell("Iconv_open %s/%s failed; %s",
				to, from, strerror(errno));
			return 0;	/* Don't change anything */
		}

		dest_size = *numbytes;
		dest_left = *numbytes;
		RESIZE(retstr, char, dest_size);

		work_data = *data;
		dest_ptr = retstr;

		while (iconv(iref, &work_data, numbytes, &dest_ptr, &dest_left) != 0)
		{
			/* I *THINK* this is a hack. */ 
			if (errno == EINVAL || errno == EILSEQ)
			{
				work_data++;
				(*numbytes)--;
				continue;
			}

			if (dest_left < 8)
			{
				size_t	offset;
				offset = dest_ptr - retstr;

				dest_size += 64;
				dest_left += 64;
				RESIZE(retstr, char, dest_size);
				dest_ptr = retstr + offset;
				continue;
			}

			break;
		}
		iconv_close (iref);
		*numbytes = (dest_size - dest_left);
	}

	new_free(data);
	*data = retstr;
	return (*numbytes);
}

/*
 * recode_with_iconv_t -- copy and iconv convert a string.
 * Arguments:
 *	iref	- An (iconv_t) for your conversion.
 *	data	- A pointer to *MALLOCED SPACE*
 *		  The input value of (*data) _may be free'd_.
 *		  The return value of (*data) _must be free'd_.
 *		  IF NULL: error (returns 0)
 *	numbytes - A pointer to an integer of how big (*data) is. 
 *		   Only (*numbytes) of (*data) will be converted!
 *		   The value of (*numbytes) will change if (*data) is changed.
 *		   IF NULL: error (returns 0)
 * Return Value:
 *	0	- No changes were made (because of error)
 *	>0	- The number of bytes in the output string
 *
 * Inspired liberally by howl's iconv support above.
 */
int	recode_with_iconv_t (iconv_t iref, char **data, size_t *numbytes)
{
	char *	dest_ptr = NULL;
	size_t	dest_left = 0;
	size_t	dest_size = 0;
	int	n;
	char *	work_data;
	char *	retstr = NULL;

	/*
	 * Some sanity checks!
	 */
	if (!numbytes || !*numbytes)
		return 0;
	if (!data || !*data)
		return 0;

	dest_size = *numbytes;
	dest_left = *numbytes;
	RESIZE(retstr, char, dest_size);

	work_data = *data;
	dest_ptr = retstr;

        while (iconv(iref, &work_data, numbytes, &dest_ptr, &dest_left) != 0)
        {
                /* I *THINK* this is a hack. */ 
                if (errno == EINVAL || errno == EILSEQ)
                {
                        work_data++;
                        (*numbytes)--;
                        continue;
                }

		if (dest_left < 8)
		{
			size_t	offset;
			offset = dest_ptr - retstr;

			dest_size += 64;
			dest_left += 64;
			RESIZE(retstr, char, dest_size);
			dest_ptr = retstr + offset;
			continue;
		}

                break;
        }

	new_free(data);
	*data = retstr;
	*numbytes = (dest_size - dest_left);
	return (*numbytes);
}

/*
 * ucs_to_utf8 -- Convert a single unicode code point into a utf8 string.
 *
 * Arguments:
 *	key	- A single unicode code point.  Ideally something returned
 *		  by next_code_point().
 *	utf8str	- A buffer to write the string into.  At least 8 bytes big.
 *	utf8strsiz - The size of utf8str (ignored for now)
 *
 * Returns:
 *	The number of bytes in the resulting utf8 sequence, that is, the 
 *	number of bytes written to utf8str, not counting trailing nul.
 */
int	ucs_to_utf8 (u_32int_t key, unsigned char *utf8str, size_t utf8strsiz)
{
	if (key <= 0x007F)
	{
		utf8str[0] = key;
		utf8str[1] = 0;
		return 1;
	}

	else if (key <= 0x07FF)
	{
		utf8str[0] = key / 64 + 192;
		utf8str[1] = key % 64 + 128;
		utf8str[2] = 0;
		return 2;
	}

	else if (key <= 0xD7FF || (key >= 0xE000 && key <= 0xFFFF))
	{
		utf8str[0] = key / 4096 + 224;
		utf8str[1] = (key % 4096) / 64 + 128;
		utf8str[2] = key % 64 + 128;
		utf8str[3] = 0;
		return 3;
	}

	else
	{
		utf8str[0] = key / 262144 + 240;
		utf8str[1] = (key % 262144) / 4096 + 128;
		utf8str[2] = (key % 4096) / 64 + 128;
		utf8str[3] = key % 64 + 128;
		utf8str[4] = 0;
		return 4;
	}
}

/*
 * strext2 - Remove (and return) the middle part of a buffer
 *
 * Arguments:
 *	cut	- A pointer to new_malloc()ed memory, or a pointer to NULL.
 *		  THIS POINTER WILL BE NEW_FREE()D!
 *		  THE POINTED-TO VALUE WILL BE REPLACED!
 *	buffer	- A C string to be sliced and diced
 *		  XXX I'd prefer if this didn't have to be a C str.
 *	part2	- The first byte in buffer to be removed
 *	part3	- The first byte in buffer NOT to be removed
 *
 * This function will remove the (part2)th through (part3-1)th bytes in 
 * (buffer).  This substring will be malloc_strcpy()d into (*cut).
 * (Buffer) will be changed so that the byte after (part2-1) will be (part3).
 * 
 * Example:
 *   Input:
 *	Buffer:		zero one two three four
 *	part2: (5)           ^
 *	part3: (13)                  ^
 *
 *   Output:
 *	Buffer:		zero three four
 *	(*cut):		one two 
 */
int	strext2 (unsigned char **cut, unsigned char *buffer, size_t part2, size_t part3)
{
	unsigned char *part2str, *part3str, *p, *s;
	size_t	buflen;
	size_t	cutlen;
	ssize_t	newlen;

	if (part3 <= part2)
		return 0;		/* Nothing to extract */

	buflen = strlen(buffer);	/* XXX Should be passed in as param */
	if (part2 > buflen)
		return 0;		/* Nothing to extract */
	if (part3 > buflen)
		part3 = buflen;		/* Stop at end of string */

	/* Copy the cut part */
	newlen = (ssize_t)(part3 - part2 + 1);
	if (newlen <= 0)
		return 0;		/* Uh, what? */

	RESIZE(*cut, char, newlen);
	p = *cut;
	s = buffer + part2;
	while (s < buffer + part3)
		*p++ = *s++;
	*p++ = 0;

	/* Eliminate the cut part */
	p = buffer + part2;
	s = buffer + part3;
	while (*s)
		*p++ = *s++;
	*p++ = 0;

	return 0;
}


/*
 * invalid_utf8str - Test whether a string is valid utf8 string (or not)
 *
 * Arguments:
 *	utf8str	- A string to be tested for utf8-ness.  Must be nul terminated
 *		  IF THIS STRING ENDS WITH AN INCOMPLETE PARTIAL UTF8 SEQUENCE
 *		  THE INCOMPLETE PARTIAL SEQUENCE WILL BE TRIMMED.  This 
 *		  trimming is not considered a 'defect'.  This is to handle
 *		  strings that were truncated blindly by ircd or whatnot.
 *	utf8strsiz - The number of bytes to be tested.
 *
 * Return Value:
 *	0	(Success) The utf8str is well formed -- no defects found
 *	>0	(Failure) The number of bytes that were not part of a 
 *			  valid utf8 string.
 */
int	invalid_utf8str (unsigned char *utf8str)
{
	unsigned char *s;
	int	code_point;
	int	errors = 0;
	int	count = 0;

	s = utf8str;
	while ((code_point = next_code_point((const unsigned char **)&s, 0)))
	{
		/* The next byte did not start a utf8 code point */
		if (code_point < 0)
		{
			/* Did it start an incomplete utf8 code point? */
			int	x = partial_code_point(s);

			/* 
			 * If this is a partial/truncated code point:
			 *  - And we have previously seen valid utf8
			 *  - And we have not previously seen invalid utf8
			 * Then we assume this is a partial/truncated code
			 * point at the end of an otherwise valid string,
			 * and chop it off.
			 */
			if (x == 1 && count > 0 && errors == 0)
			{
				*s = 0;
				break;
			}

			else
			{
				errors++;
				s++;
			}
		}

		/* 
		 * We count the number of non-ascii utf8 code points
		 * we've seen
		 */
		else if (code_point > 127)
			count++;
	}

	return errors;
}

/*
 * is_iso2022_jp - Test whether a string is encoded in iso2022_jp.
 *
 * Arguments:
 *	buffer - A string to be tested for iso2022-ness
 *
 * Return Value:
 *	1 	At least one ISO2022-JP escape sequence was found
 *	0	No ISO2022-JP escape sequences were found.
 *
 * Notes:
 *	Ideally we could do this test by running it through iconv().
 * 	I would like to support other ISO2022's as well, but I don't
 *		have anyone to test them.
 * 	I would like to support other non-ISO2022's, but again, I don't
 *		have anyone to test them.
 */
int	is_iso2022_jp (const unsigned char *buffer)
{
	static	iconv_t	iso2022_jp = (iconv_t) -2;
	const unsigned char *x;
	int	found_one = 0;

	/* ISO-2022-JP has no 8 bit chars. */
	for (x = buffer; *x; x++)
		if (*x & 0x80)
			return 0;

	/* ISO-2022-JP has to have a 2022 <escape> sequence somewhere */
	for (x = buffer; *x; x++)
	{
	    if (*x == 0x1B)	/* Escape */
	    {
		if (x[1] == '$')
		{
			if (x[2] == '@')
				found_one++;
			else if (x[2] == 'B')
				found_one++;
			else if (x[2] == '(')
			{
				if (x[3] == 'D')
					found_one++;
			}
		}
		else if (x[1] == '(')
		{
			if (x[2] == 'B')
				found_one++;
			else if (x[2] == 'I')
				found_one++;
			else if (x[2] == 'J')
				found_one++;
		}
	    }
	}

	/* I could run the string through iconv() to see if it converts.. */
	if (found_one)
		return 1;

	return 0;
}

/*
 * check_xdigit - Test whether a character is a valid hexadecimal digit.
 *
 * Arguments:
 *	digit - A single byte that might contain a hexadecimal digit.
 *		0 - 9	- Itself
 *		A - F	- 10 through 15
 *		a - f	- Also 10 through 15
 *
 * Return value:
 *	-1 	(failure) - "digit" is not a hexadecimal digit
 *	>= 0	(success) - "digit" represents the return value
 */
int	check_xdigit (unsigned char digit)
{
	if (digit >= '0' && digit <= '9')
		return digit - '0';
	else if (digit >= 'A' && digit <= 'F')
		return digit - 'A' + 10;
	else if (digit >= 'a' && digit <= 'f')
		return digit - 'a' + 10;
	else
		return -1;
}


/*
 * Returns the number of seconds between 't' and startup.
 */
double	time_since_startup (Timeval t)
{
	return time_diff(start_time, t);
}

static  char *          signal_name[NSIG + 1];

/*
 * This is required because musl linux does not implement sys_siglist[],
 * and stock ircII does not require it, so we should not either.
 */
void	init_signal_names (void)
{
	int	i;

	for (i = 1; i < NSIG; i++)
	{
		signal_name[i] = NULL;

/* XXX because clang objects to #x + 3 */
#define SIGH(x) if (i == x) malloc_strcpy(&signal_name[i], & #x [3]);

#ifdef SIGABRT
		SIGH(SIGABRT)
#endif
#ifdef SIGALRM
		else SIGH(SIGALRM)
#endif
#ifdef SIGBUS
		else SIGH(SIGBUS)
#endif
#ifdef SIGCHLD
		else SIGH(SIGCHLD)
#endif
#ifdef SIGCONT
		else SIGH(SIGCONT)
#endif
#ifdef SIGFPE
		else SIGH(SIGFPE)
#endif
#ifdef SIGHUP
		else SIGH(SIGHUP)
#endif
#ifdef SIGINFO
		else SIGH(SIGINFO)
#endif
#ifdef SIGILL
		else SIGH(SIGILL)
#endif
#ifdef SIGINT
		else SIGH(SIGINT)
#endif
#ifdef SIGKILL
		else SIGH(SIGKILL)
#endif
#ifdef SIGPIPE
		else SIGH(SIGPIPE)
#endif
#ifdef SIGPOLL
		else SIGH(SIGPOLL)
#endif
#ifdef SIGRTMIN
		else SIGH(SIGRTMIN)
#endif 
#ifdef SIGRTMAX
		else SIGH(SIGRTMAX)
#endif
#ifdef SIGQUIT
		else SIGH(SIGQUIT)
#endif
#ifdef SIGSEGV
		else SIGH(SIGSEGV)
#endif
#ifdef SIGSTOP
		else SIGH(SIGSTOP)
#endif
#ifdef SIGSYS
		else SIGH(SIGSYS)
#endif
#ifdef SIGTERM
		else SIGH(SIGTERM)
#endif
#ifdef SIGTSTP
		else SIGH(SIGTSTP)
#endif
#ifdef SIGTTIN
		else SIGH(SIGTTIN)
#endif
#ifdef SIGTTOU
		else SIGH(SIGTTOU)
#endif
#ifdef SIGTRAP
		else SIGH(SIGTRAP)
#endif
#ifdef SIGURG
		else SIGH(SIGURG)
#endif
#ifdef SIGUSR1
		else SIGH(SIGUSR1)
#endif
#ifdef SIGUSR2
		else SIGH(SIGUSR2)
#endif
#ifdef SIGXCPU
		else SIGH(SIGXCPU)
#endif
#ifdef SIGXFSZ
		else SIGH(SIGXFSZ)
#endif
#ifdef SIGWINCH
		else SIGH(SIGWINCH)
#endif
		else
			malloc_sprintf(&signal_name[i], "SIG%d", i);
	}
}

const char *	get_signal_name (int signo)
{
	static	char	static_signal_name[128];

	if (signo <= 0 || signo > NSIG || signal_name[signo] == NULL)
	{
		snprintf(static_signal_name, 127, "SIG%d", signo);
		return static_signal_name;
	}
	else
		return signal_name[signo];
}

int	get_signal_by_name (const char *signame)
{
	int	i;
	size_t	len;

	len = strlen(signame);

	for (i = 1; i < NSIG; i++)
	{
		if (!get_signal_name(i))
			continue;
		if (!my_strnicmp(get_signal_name(i), signame, len))
			return i;
	}
	return -1;
}

/*
 * This is from https://github.com/rxi/uuid4/blob/master/src/uuid4.c
 */
/**
 * Copyright (c) 2018 rxi
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 */

static char *	uuid4_generate_internal (int dashes)
{
static const char *template_with_dashes = "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx";
static const char *template_without_dashes = "xxxxxxxxxxxx4xxxyxxxxxxxxxxxxxxx";
static const char *chars = "0123456789abcdef";
	union { unsigned char b[16]; uint32_t word[4]; } s;
	const char *	p;
	int 	i, n;
	char	*dst, *retval;

	s.word[0] = (uint32_t)random_number(0);
	s.word[1] = (uint32_t)random_number(0);
	s.word[2] = (uint32_t)random_number(0);
	s.word[3] = (uint32_t)random_number(0);

	dst = retval = new_malloc(64);
	memset(retval, 0, 64);

	/* build string */
	if (dashes)
		p = template_with_dashes;
	else
		p = template_without_dashes;

	for (i = 0; *p; p++, dst++)
	{
		n = s.b[i >> 1];
		n = (i & 1) ? (n >> 4) : (n & 0xf);

		switch (*p) 
		{
			case 'x': 
				*dst = chars[n];
				i++;  
				break;
			case 'y': 
				*dst = chars[(n & 0x3) + 8];  
				i++;  
				break;
			default: 
				*dst = *p;
		}
	}
	*dst = 0;
	return retval;
}

char *	uuid4_generate (void)
{
	return uuid4_generate_internal(1);
}

char *	uuid4_generate_no_dashes (void)
{
	return uuid4_generate_internal(0);
}

/* End of stuff from from https://github.com/rxi/uuid4/blob/master/src/uuid4.c */

/*
 * I sourced this from https://en.wikipedia.org/wiki/Code_page_437
 */
static  u_32int_t       cp437map[256] = {
#if 0
/* 00-07 */     0x0000, 0x263a, 0x263b, 0x2665, 0x2666, 0x2663, 0x2660, 0x2022,
/* 08-0F */     0x25D8, 0x25CB, 0x25D9, 0x2642, 0x2640, 0x266a, 0x266b, 0x263c,
/* 10-17 */     0x25ba, 0x25c4, 0x2195, 0x203c, 0x00b6, 0x0017, 0x25ac, 0x21a8,
/* 18-1F */     0x2191, 0x2193, 0x2192, 0x2190, 0x221f, 0x2194, 0x25b2, 0x25bc,
#else
/* 00-07 */     0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007,
/* 08-0F */     0x0008, 0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x000E, 0x000F,
/* 10-17 */     0x0010, 0x0011, 0x0012, 0x0013, 0x0014, 0x0015, 0x0016, 0x0017,
/* 18-1F */     0x0018, 0x0019, 0x001A, 0x001B, 0x001C, 0x001D, 0x001E, 0x001F,
#endif
/* 20-27 */     0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027,
/* 28-2F */     0x0028, 0x0029, 0x002a, 0x002b, 0x002c, 0x002d, 0x002e, 0x002f,
/* 30-37 */     0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,
/* 38-3F */     0x0038, 0x0039, 0x003a, 0x003b, 0x003c, 0x003d, 0x003e, 0x003f,
/* 40-47 */     0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047,
/* 48-4F */     0x0048, 0x0049, 0x004a, 0x004b, 0x004c, 0x004d, 0x004e, 0x004f,
/* 50-57 */     0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057,
/* 58-5F */     0x0058, 0x0059, 0x005a, 0x005b, 0x005c, 0x005d, 0x005e, 0x005f,
/* 60-67 */     0x0060, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067,
/* 68-6F */     0x0068, 0x0069, 0x006a, 0x006b, 0x006c, 0x006d, 0x006e, 0x006f,
/* 70-77 */     0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077,
/* 78-7F */     0x0078, 0x0079, 0x007a, 0x007b, 0x007c, 0x007d, 0x007e, 0x2302,
/* 80-87 */     0x00c7, 0x00fc, 0x00e9, 0x00e2, 0x00e4, 0x00e0, 0x00e5, 0x00e7,
/* 88-8F */     0x00ea, 0x00eb, 0x00e8, 0x00ef, 0x00ee, 0x00ec, 0x00c4, 0x00c5,
/* 90-97 */     0x00c9, 0x00e6, 0x00c6, 0x00f4, 0x00f6, 0x00f2, 0x00fb, 0x00f9,
/* 98-9F */     0x00ff, 0x00d6, 0x00dc, 0x00a2, 0x00a3, 0x00a5, 0x20a7, 0x0192,
/* A0-A7 */     0x00e1, 0x00ed, 0x00f3, 0x00fa, 0x00f1, 0x00d1, 0x00aa, 0x00ba,
/* A8-AF */     0x00bf, 0x2310, 0x00ac, 0x00bd, 0x00bc, 0x00a1, 0x00ab, 0x00bb,
/* B0-B7 */     0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
/* B8-BF */     0x2555, 0x2563, 0x2551, 0x2557, 0x255d, 0x255c, 0x255b, 0x2510,
/* C0-C7 */     0x2514, 0x2534, 0x252c, 0x251c, 0x2500, 0x253c, 0x255e, 0x255f,
/* C8-CF */     0x255a, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256c, 0x2567,
/* D0-D7 */     0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256b,
/* D8-DF */     0x256a, 0x2518, 0x250c, 0x2588, 0x2584, 0x258c, 0x2590, 0x2580,
/* E0-E7 */     0x03b1, 0x00df, 0x0393, 0x03c0, 0x03a3, 0x03c3, 0x00b5, 0x03c4,
/* E8-EF */     0x03a6, 0x0398, 0x03a9, 0x03b4, 0x221e, 0x03c6, 0x03b5, 0x2229,
/* F0-F7 */     0x2261, 0x00b1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00f7, 0x2248,
/* F8-FF */     0x00b0, 0x2219, 0x00b7, 0x221a, 0x207f, 0x00b2, 0x25a0, 0x00a0
};

/*
 * cp437_to_ucs - Convert a CP437 byte to a unicode code point.
 *
 * Arguments:
 *      key     - A unicode code point
 *      utf8str - Where to put the code point in the user's encoding
 *      utf8strsiz - How big utf8str is.
 */
static u_32int_t       cp437_to_ucs (unsigned char cp437_byte)
{
	return cp437map[cp437_byte];
}


/*
 * cp437_to_utf8 - Convert a CP437 string to UTF8 when the locale is missing
 *
 * Arguments:
 *	input	 - A CP437-encoded string
 *	inputlen - A pointer to how many bytes are in 'input'
 *	destlen	 - A pointer where to store the number of output bytes
 *
 * Return value:
 *	A nul-terminated, malloced UTF8 string whose length is stored in *destlen
 *
 * Notes:
 *	Some systems don't have the CP437 locale (*cough*mac*cough*).  
 *	But we really need to support CP437 for ascii art scripts.  
 *	So this hack is the result
 */
unsigned char *  cp437_to_utf8 (const unsigned char *input, size_t inputlen, size_t *destlen)
{
	unsigned char *	dest;
	size_t		dest_len;
	size_t		s, d;
	unsigned char *	y;
	u_32int_t	codepoint;
	char 		utf8str[16];

	/*
	 * We are going to convert 'src' to 'dest'.
	 * No code points results in more than 6 bytes.
	 * So 6 * numbytes plus one for nul is big enough
	 */
	dest_len = inputlen * 6 + 1;
	dest = new_malloc(dest_len);

	/*
	 * Now let's walk each byte in the (cp437-encoded) source.
	 * We convert the cp437 byte into a unicode code point.
	 * Then we convert the code point to utf8.
	 * Then we append that utf8 to the destination string.
	 */
	for (s = d = 0; s < inputlen; s++)
	{
		codepoint = cp437_to_ucs(input[s]);
		ucs_to_utf8(codepoint, utf8str, sizeof(utf8str));
		y = utf8str;
		while (*y)
		{
			if (d >= dest_len)
				break;
			dest[d++] = *y++;
		}
	}

	/* now nul-terminate the string, and return its length */
	if (d < dest_len)
	{
		dest[d] = 0;
		dest_len = d + 1;
	}
	else
		dest[dest_len - 1] = 0;

	*destlen = dest_len;
	return dest;
}


