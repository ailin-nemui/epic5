/* $EPIC: ircaux.c,v 1.70 2003/03/29 08:10:22 jnelson Exp $ */
/*
 * ircaux.c: some extra routines... not specific to irc... that I needed 
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright © 1994 Jake Khuon.
 * Copyright © 1993, 2002 EPIC Software Labs.
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
#include "ircaux.h"
#include "output.h"
#include "term.h"
#include "vars.h"
#include "alias.h"
#include "if.h"

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

void	fatal_malloc_check (void *ptr, const char *special, char *fn, int line)
{
	switch (malloc_check(ptr))
	{
	    case ALLOC_MAGIC_FAILED:
	    {
		yell("IMPORTANT! MAKE SURE TO INCLUDE ALL OF THIS INFORMATION" 
			" IN YOUR BUG REPORT!");
		yell("MAGIC CHECK OF MALLOCED MEMORY FAILED!");
		if (special)
			yell("Because of: [%s]", special);

		if (ptr)
		{
			yell("Address: [%p]  Size: [%d]  Magic: [%x] "
				"(should be [%lx])",
				ptr, alloc_size(ptr), magic(ptr), ALLOC_MAGIC);
			yell("Dump: [%s]", prntdump(ptr, alloc_size(ptr)));
		}
		else
			yell("Address: [NULL]");

		yell("IMPORTANT! MAKE SURE TO INCLUDE ALL OF THIS INFORMATION" 
			" IN YOUR BUG REPORT!");
		panic("BE SURE TO INCLUDE THE ABOVE IMPORTANT INFORMATION! "
			"-- new_free()'s magic check failed from [%s/%d].", 
			fn, line);
	    }

	    case ALREADY_FREED:
	    {
		panic("free()d the same address twice from [%s/%d].", 
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
void *	really_new_malloc (size_t size, char *fn, int line)
{
	char	*ptr;

	if (!(ptr = (char *)malloc(size + sizeof(MO))))
		panic("Malloc() failed from [%s/%d], giving up!", fn, line);

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
void *	really_new_free(void **ptr, char *fn, int line)
{
	if (*ptr)
	{
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
void *	really_new_realloc (void **ptr, size_t size, char *fn, int line)
{
	void *newptr = NULL;

	if (!size) 
		*ptr = really_new_free(ptr, fn, line);
	else if (!*ptr)
		*ptr = really_new_malloc(size, fn, line);
	else 
	{
		/* Make sure this is safe for realloc. */
		fatal_malloc_check(*ptr, NULL, fn, line);

		/* Copy everything, including the MO buffer */
		if ((newptr = (char *)realloc(mo_ptr(*ptr), size + sizeof(MO))))
		{
			*ptr = newptr;
		} else {
			new_free(ptr);
			panic("realloc() failed from [%s/%d], giving up!", fn, line);
		}

		/* Re-initalize the MO buffer; magic(*ptr) is already set. */
		*ptr = (void *)((char *)*ptr + sizeof(MO));
		alloc_size(*ptr) = size;
#ifdef ALLOC_DEBUG
		alloc_table.entries[mo_ptr(*ptr)->entry] = *ptr;
#endif
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
				return *ptr;

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

void malloc_dump (char* file) {
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
 * malloc_strcpy:  Mallocs enough space for src to be copied in to where
 * ptr points to.
 *
 * Never call this with ptr pointinng to an uninitialised string, as the
 * call to new_free() might crash the client... - phone, jan, 1993.
 */
char *	malloc_strcpy (char **ptr, const char *src)
{
	if (!src)
		return new_free(ptr);	/* shrug */

	if (*ptr)
	{
		if (alloc_size(*ptr) == FREED_VAL)
			panic("free()d pointer passed to malloc_strcpy");

		/* No copy neccesary! */
		if (*ptr == src)
			return *ptr;

		if (alloc_size(*ptr) > strlen(src))
			return strcpy(*ptr, src);

		new_free(ptr);
	}

	*ptr = new_malloc(strlen(src) + 1);
	return strcpy(*ptr, src);
}

/* malloc_strcat: Yeah, right */
char *	malloc_strcat_c (char **ptr, const char *src, size_t *cluep)
{
	size_t  msize;
	size_t  psize;
	size_t  ssize;
	size_t	clue = cluep ? *cluep : 0;

	if (*ptr)
	{
		if (alloc_size(*ptr) == FREED_VAL)
			panic("free()d pointer passed to malloc_strcat");

		if (!src)
			return *ptr;

		msize = (psize = clue + strlen(clue + *ptr)) + (ssize = strlen(src)) + 1;
		RESIZE(*ptr, char, msize);
		if (cluep) *cluep = psize + ssize;
		return strcat(psize+*ptr, src)-psize;
	}

	return (*ptr = m_strdup(src));
}

char *	malloc_str2cpy(char **ptr, const char *src1, const char *src2)
{
	if (!src1 && !src2)
		return new_free(ptr);

	if (*ptr)
	{
		if (alloc_size(*ptr) == FREED_VAL)
			panic("free()d pointer passed to malloc_str2cpy");

		if (alloc_size(*ptr) > strlen(src1) + strlen(src2))
		{
			stpcpy(stpcpy(*ptr, src1), src2);
			return *ptr;
		}

		new_free(ptr);
	}

	*ptr = new_malloc(strlen(src1) + strlen(src2) + 1);
	stpcpy(stpcpy(*ptr, src1), src2);
	return *ptr;
			/* * */
}


char *	m_2dup (const char *str1, const char *str2)
{
	size_t msize = strlen(str1) + strlen(str2) + 1;
	char * buffer = (char *)new_malloc(msize);

	stpcpy(stpcpy(buffer, str1), str2);
	return buffer;
}

char *	m_3dup (const char *str1, const char *str2, const char *str3)
{
	size_t msize = strlen(str1) + strlen(str2) + strlen(str3) + 1;
	char *buffer = (char *)new_malloc(msize);

	stpcpy(stpcpy(stpcpy(buffer, str1), str2), str3);
	return buffer;
}

char *	m_opendup (const char *str1, ...)
{
	va_list args;
	int size;
	char *this_arg = NULL;
	char *retval = NULL;
	char *p;

	size = strlen(str1);
	va_start(args, str1);
	while ((this_arg = va_arg(args, char *)))
		size += strlen(this_arg);

	retval = (char *)new_malloc(size + 1);

	p = stpcpy(retval, str1);
	va_start(args, str1);
	while ((this_arg = va_arg(args, char *)))
		p = stpcpy(p, this_arg);

	va_end(args);
	return retval;
}


char *	m_strdup (const char *str)
{
	char *ptr;

	if (!str)
		str = empty_string;

	ptr = (char *)new_malloc(strlen(str) + 1);
	return strcpy(ptr, str);
}

char *	m_ec3cat (char **one, const char *yes1, const char *yes2, size_t *clue)
{
	if (*one && **one)
		return m_c3cat(one, yes1, yes2, clue);

	return (*one = m_2dup(yes1, yes2));
}


char *	m_sc3cat (char **one, const char *maybe, const char *definitely, size_t *clue)
{
	if (*one && **one)
		return m_c3cat(one, maybe, definitely, clue);

	return malloc_strcpy(one, definitely);
}

char *	m_sc3cat_s (char **one, const char *maybe, const char *ifthere, size_t *clue)
{
	if (ifthere && *ifthere)
		return m_sc3cat(one, maybe, ifthere, clue);

	return *one;
}

char *	m_c3cat(char **one, const char *two, const char *three, size_t *clue)
{
	size_t	csize = clue?*clue:0;
	int 	msize = csize;

	if (*one)
	{
		if (alloc_size(*one) == FREED_VAL)
			panic("free()d pointer passed to m_3cat");
		msize += strlen(csize+*one);
	}
	if (two)
		msize += strlen(two);
	if (three)
		msize += strlen(three);

	if (!*one)
	{
		*one = new_malloc(msize + 1);
		**one = 0;
	}
	else
		RESIZE(*one, char, msize + 1);

	if (two)
		strcat(csize + *one, two);
	if (three)
		strcat(csize + *one, three);
	if (clue) 
		*clue = msize;

	return *one;
}

char *	upper (char *str)
{
	char	*ptr = (char *) 0;

	if (str)
	{
		ptr = str;
		for (; *str; str++)
		{
			if (islower(*str))
				*str = toupper(*str);
		}
	}
	return (ptr);
}

char *	lower (char *str)
{
	char *ptr = NULL;

	if (str)
	{
		ptr = str;
		for (; *str; str++)
		{
			if (isupper(*str))
				*str = tolower(*str);
		}
	}
	return ptr;
}


char *	malloc_sprintf (char **to, const char *pattern, ...)
{
	char booya[BIG_BUFFER_SIZE * 10 + 1];
	*booya = 0;

	if (pattern)
	{
		va_list args;
		va_start(args, pattern);
		vsnprintf(booya, BIG_BUFFER_SIZE * 10, pattern, args);
		va_end(args);
	}

	malloc_strcpy(to, booya);
	return *to;
}

/* same thing, different variation */
char *	m_sprintf (const char *pattern, ...)
{
	char booya[BIG_BUFFER_SIZE * 10 + 1];
	*booya = 0;

	if (pattern)
	{
		va_list args;
		va_start(args, pattern);
		vsnprintf(booya, BIG_BUFFER_SIZE * 10, pattern, args);
		va_end(args);
	}

	return m_strdup(booya);
}


/* case insensitive string searching */
char *	stristr (const char *source, const char *search)
{
        int     x = 0;

        if (!source || !*source || !search || !*search || strlen(source) < strlen(search))
		return NULL;

        while (*source)
        {
                if (source[x] && toupper(source[x]) == toupper(search[x]))
			x++;
                else if (search[x])
			source++, x = 0;
		else
			return (char *)source;
        }
	return NULL;
}

/* case insensitive string searching from the end */
char *	rstristr (const char *source, const char *search)
{
	const char *ptr;
	int x = 0;

        if (!source || !*source || !search || !*search || strlen(source) < strlen(search))
		return NULL;

	ptr = source + strlen(source) - strlen(search);

	while (ptr >= source)
        {
		if (!search[x])
			return (char *)ptr;

		if (toupper(ptr[x]) == toupper(search[x]))
			x++;
		else
			ptr--, x = 0;
	}
	return NULL;
}


char *	next_arg_count (char *str, char **new_ptr, int count)
{
	char	*ptr;

	/* added by Sheik (kilau@prairie.nodak.edu) -- sanity */
	if (!str || !*str)
		return NULL;

	while (isspace(*str))
		str++;

	for (ptr = str; count > 0 && *ptr; count--)
	{
		while (isspace(*ptr))
			ptr++;
		while (*ptr && !isspace(*ptr))
			ptr++;
	}

	if (!*ptr)
		ptr = empty_string;		/* XXX sigh */
	else
		*ptr++ = 0;

	if (new_ptr)
		*new_ptr = ptr;

	return str;
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

/*
 * yanks off the last word from 'src'
 * kinda the opposite of next_arg
 */
char *	last_arg (char **src, size_t *cluep)
{
	char *ptr;

	if (!src || !*src)
		return NULL;

	remove_trailing_spaces(*src, cluep);
	ptr = *src + (cluep ? *cluep : 0);
	ptr += strlen(ptr);
	ptr -= 1;

	if (*ptr == '"')
	{
		for (ptr--; ; ptr--)
		{
			if (*ptr == '"')
			{
				if (ptr == *src)
					break;
				if (ptr[-1] == ' ')
				{
					ptr--;
					break;
				}
			}
			if (ptr == *src)
				break;
		}
	}
	else
	{
		for (; ; ptr--)
		{
			if (*ptr == ' ')
				break;
			if (ptr == *src)
				break;
		}
	}

	if (cluep) 
		*cluep = ptr - *src;

	if (ptr == *src)
		*src = empty_string;
	else
		*ptr++ = 0;

	return ptr;
}

#define risspace(c) (c == ' ')

/*
 * We seriously need to merge the complexity in this function with
 * that of word_count().
 */
char *	new_next_arg (char *str, char **new_ptr)
{
	char	*ptr;

	if (!str || !*str)
		return NULL;

	ptr = str;
	while (*ptr && risspace(*ptr))
		ptr++;

	if (*ptr == '"')
	{
		for (str = ++ptr; *str; str++)
		{
			if (*str == '\\' && str[1])
				str++;
			else if (str[1] && !risspace(str[1]))
			{}
			else if (*str == '"')
			{
				*str++ = 0;
				if (risspace(*str))
					str++;
				break;
			}

#if 0
			if (!str[1])
			{
				ptr--;
				goto noquotedword;
			}
#endif
		}
	}
	else
	{
#if 0
noquotedword:
#endif
		str = ptr;
		while (*str && !risspace(*str))
			str++;
		if (*str)
			*str++ = 0;
	}

	if (!*str)
		str = empty_string;

	if (new_ptr)
		*new_ptr = str;

	return ptr;
}

/*
 * Return multiple arguments using new_next_arg().
 *
 * This is just a little bit of a crock.  The basic problem here is the
 * dequoting which will dispose of the distinction between the words,
 * but presumably that's what we want.  Also, note that stpcpy isn't
 * supposed to be used with overlapping blocks, but since new_next_arg
 * always returns an equal or shorter string, it should work.  (since
 * str1 <= str).
 */
char *	new_next_arg_count (char *str, char **new_ptr, int count)
{
	char *ret = str;
	char *str1 = str;
	*new_ptr = str;

	while (count-- > 0 && *new_ptr && **new_ptr)
	{
		str = new_next_arg(*new_ptr, new_ptr);
		if (str)
		{
			str = LOCAL_COPY(str);
			if (str1 != ret)
				str1 = stpcpy(str1, space);
			str1 = stpcpy(str1, str);
		}
	}

	return ret;
}

char * next_quoted_args (char *str, char **new_ptr, int count)
{
	return NULL;
}

#if 0
/*
 * This function is "safe" because it doesnt ever return NULL.
 * XXX - this is an ugly kludge that needs to go away
 */
char *	safe_new_next_arg (char *str, char **new_ptr)
{
	char	*ptr,
		*start;

	if (!str || !*str)
		return empty_string;

	if ((ptr = sindex(str, "^ \t")) != NULL)
	{
		if (*ptr == '"')
		{
			start = ++ptr;
			while ((str = sindex(ptr, "\"\\")) != NULL)
			{
				switch (*str)
				{
					case '"':
						*str++ = '\0';
						if (*str == ' ')
							str++;
						if (new_ptr)
							*new_ptr = str;
						return (start);
					case '\\':
						if (str[1] == '"')
							ov_strcpy(str, str + 1);
						ptr = str + 1;
				}
			}
			str = empty_string;
		}
		else
		{
			if ((str = sindex(ptr, " \t")) != NULL)
				*str++ = '\0';
			else
				str = empty_string;
		}
	}
	else
		str = empty_string;

	if (new_ptr)
		*new_ptr = str;

	if (!ptr)
		return empty_string;

	return ptr;
}

#else

/*
 * Note that the old version is now out of sync with epics word philosophy.
 */
char *	safe_new_next_arg (char *str, char **new_ptr)
{
	char * ret = new_next_arg(str, new_ptr);

	return ret ? ret : empty_string;
}

#endif

char *	new_new_next_arg (char *str, char **new_ptr, char *type)
{
	char	*ptr,
		*start;

	if (!str || !*str)
		return NULL;

	if ((ptr = sindex(str, "^ \t")) != NULL)
	{
		if ((*ptr == '"') || (*ptr == '\''))
		{
			char blah[3];
			blah[0] = *ptr;
			blah[1] = '\\';
			blah[2] = '\0';

			*type = *ptr;
			start = ++ptr;
			while ((str = sindex(ptr, blah)) != NULL)
			{
				switch (*str)
				{
				case '\'':
				case '"':
					*str++ = '\0';
					if (*str == ' ')
						str++;
					if (new_ptr)
						*new_ptr = str;
					return (start);
				case '\\':
					if (str[1] == *type)
						ov_strcpy(str, str + 1);
					ptr = str + 1;
				}
			}
			str = empty_string;
		}
		else
		{
			*type = '\"';
			if ((str = sindex(ptr, " \t")) != NULL)
				*str++ = '\0';
			else
				str = empty_string;
		}
	}
	else
		str = empty_string;
	if (new_ptr)
		*new_ptr = str;
	return ptr;
}

char *	s_next_arg (char **from)
{
	char *next = strchr(*from, ' ');
	char *keep = *from;
	*from = next;
	return keep;
}

char *	next_in_comma_list (char *str, char **after)
{
	return next_in_div_list(str, after, ',');
}

char *	next_in_div_list (char *str, char **after, char div)
{
	*after = str;

	while (*after && **after && **after != div)
		(*after)++;

	if (*after && **after == div)
	{
		**after = 0;
		(*after)++;
	}

	return str;
}

/* Find the next instance of 'what' that isn't backslashed. */
char *	findchar (char *str, int what)
{
	char *p;

	for (p = str; *p; p++)
	{
		if (p[0] == '\\' && p[1])
			p++;
		else if (p[0] == what)
			break;
	}

	if (*p == what)
		return p;
	else
		return NULL;
}

unsigned char stricmp_table [] = 
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
	88,	89,	90,	91,	92,	93,	126,	127,

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

/* my_stricmp: case insensitive version of strcmp */
int	my_stricmp (const unsigned char *str1, const unsigned char *str2)
{
	while (*str1 && *str2 && (stricmp_table[(unsigned short)*str1] == stricmp_table[(unsigned short)*str2]))
		str1++, str2++;

	return (stricmp_table[(unsigned short)*str1] -
		stricmp_table[(unsigned short)*str2]);
}

/* my_strnicmp: case insensitive version of strncmp */
int	my_strnicmp (const unsigned char *str1, const unsigned char *str2, size_t n)
{
	while (n && *str1 && *str2 && (stricmp_table[(unsigned short)*str1] == stricmp_table[(unsigned short)*str2]))
		str1++, str2++, n--;

	return (n ? 
		(stricmp_table[(unsigned short)*str1] -
		stricmp_table[(unsigned short)*str2]) : 0);
}

/* chop -- chops off the last character. capiche? */
char *	chop (char *stuff, size_t nchar)
{
	size_t sl = strlen(stuff);

	if (nchar > 0 && sl > 0 &&  nchar <= sl)
		stuff[sl - nchar] = 0;
	else if (nchar > sl)
		stuff[0] = 0;

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


/*
 * strmcpy:  Used to call strncpy(), but that was bogus.  Now we call
 * strlcpy() and just return the same old thing.  Anyone who needs strlcpy()'s
 * return code can just call it directly.
 */
char *	strmcpy (char *dest, const char *src, int maxlen)
{
	strlcpy(dest, src, maxlen + 1);		/* XXX kludge */
	return dest;
}

/*
 * strmcat: Used to call strcat(), but that was bogus and a lot of work
 * to boot.  Now we call strlcat() and just return the same old thing.
 * Anyone who needs strlcat()'s return code can just call it directly.
 */
char *	strmcat (char *dest, const char *src, int maxlen)
{
	strlcat(dest, src, maxlen + 1);		/* XXX kludge */
	return dest;
}

char *	strmopencat (char *dest, int maxlen, ...)
{
	va_list args;
	int 	size;
	char *	this_arg = NULL;
	int 	this_len;
	char *	endp;
	char *	p;
	size_t	worklen;

	endp = dest + maxlen;		/* This better not be an error */
	size = strlen(dest);		/* Find the end of the string */
	p = dest + size;		/* We will start catting there */

	va_start(args, maxlen);		/* Begin grabbing args */
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
		p = stpcpy(p, this_arg);
	}

	va_end(args);
	return dest;
}


/*
 * strmcat_ue: like strcat, but truncs the dest string to maxlen (thus the dest
 * should be able to handle maxlen + 1 (for the null)). Also unescapes
 * backslashes.
 */
char *	strmcat_ue (char *dest, const char *src, int maxlen)
{
	int	dstlen;

	dstlen = strlen(dest);
	dest += dstlen;
	maxlen -= dstlen;
	while (*src && maxlen > 0)
	{
		if (*src == '\\')
		{
			if (strchr("npr0", src[1]))
				*dest++ = '\020';
			else if (*(src + 1))
				*dest++ = *++src;
			else
				*dest++ = '\\';
		}
		else
			*dest++ = *src;
		src++;
	}
	*dest = 0;
	return dest;
}

/*
 * m_strcat_ues: Given two strings, concatenate the 2nd string to
 * the end of the first one, but if the "unescape" argument is 1, do
 * unescaping (like in strmcat_ue).
 * (Malloc_STRCAT_UnEscape Special, in case you were wondering. ;-))
 *
 * This uses a cheating, "not-as-efficient-as-possible" algorithm,
 * And boy do we pay for it.
 */
char *	m_strcat_ues_c (char **dest, const char *src, int unescape, size_t *cluep)
{
	int 	total_length;
	const char *	ptr;
	char *	ptr2;
	int	z;
	size_t	clue = cluep ? *cluep : 0;

	if (!unescape)
	{
		malloc_strcat_c(dest, src, cluep);
		return *dest;
	}

	z = total_length = (*dest) ? clue + strlen(clue + *dest) : 0;
	total_length += strlen(src);

	RESIZE(*dest, char, total_length + 1);
	if (z == 0)
		**dest = 0;

	ptr2 = *dest + z;
	for (ptr = src; ; ptr++)
	{
		if (*ptr == '\\')
		{
			switch (*++ptr)
			{
				case 'n': case 'p': case 'r': case '0':
					*ptr2++ = '\020';
					break;
				case (char) 0:
					*ptr2++ = '\\';
					*ptr2 = 0;	/* Ahem */
					break;
				default:
					*ptr2++ = *ptr;
			}
		}
		else
			*ptr2++ = *ptr;

		if (!*ptr)
			break;
	}
	if (cluep) *cluep = ptr2 - *dest - 1;
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
			    snprintf(result, MAXPATHLEN + 1, "~%s", pathname);
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
			strlcpy(result, str, MAXPATHLEN + 1);
		return 0;
	}

	/* Handle filenames that are just ~ or ~/... */
	str++;
	if (!*str || *str == '/')
	{
		strlcpy(buffer, my_path, sizeof(buffer));
		strlcat(buffer, str, sizeof(buffer));

		strlcpy(result, buffer, MAXPATHLEN + 1);
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

		strlcpy(result, buffer, MAXPATHLEN + 1);
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
char *	check_nickname (char *nick, int truncate)
{
	char	*s;

	if (!nick || *nick == '-' || isdigit(*nick))
		return NULL;

	for (s = nick; *s && (s - nick) < NICKNAME_LEN; s++)
		if (!islegal(*s) || my_isspace(*s))
			break;
	*s = '\0';

	return *nick ? nick : NULL;
}

/*
 * sindex: much like index(), but it looks for a match of any character in
 * the group, and returns that position.  If the first character is a ^, then
 * this will match the first occurence not in that group.
 *
 * XXXX - sindex is a lot like strpbrk(), which is standard
 */
char *	sindex (char *string, char *group)
{
	char	*ptr;

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

/* is_number: returns true if the given string is a number, false otherwise */
int	is_number (const char *str)
{
	if (!str || !*str)
		return 0;

	while (*str == ' ')
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
	int	dot = 0;

	if (!str || !*str)
		return 0;

	while (*str == ' ')
		str++;

	if (*str == '-')
		str++;

	if (!*str)
		return 0;

	for (; *str; str++)
	{
		if (isdigit((*str)))
			continue;

		if (*str == '.' && dot == 0)
		{
			dot = 1;
			continue;
		}

		return 0;
	}

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
			strlcpy(result, candidate, MAXPATHLEN + 1);
			return 0;
		}
	    }
	}

	/* 
	 * The previous check already took care of absolute paths
	 * that exist, so we need to check for absolute paths here
	 * that DON'T exist. (is this cheating?).  Also, ~user/foo
	 * is considered an "absolute path".
	 */
	if (*name == '/' || *name == '~')
		return -1;

	*result = 0;
	for (path = LOCAL_COPY(xpath); path; path = ptr)
	{
		if ((ptr = strchr(path, ':')))
			*ptr++ = 0;

		snprintf(buffer, sizeof(buffer), "%s/%s", path, name);
		if (normalize_filename(buffer, candidate))
			continue;

		if (file_exists(candidate)) {
			strlcpy(result, candidate, MAXPATHLEN + 1);
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

	*buffer = 0;		/* Whatever */

	if (!stuff)
		return buffer;	/* Whatever */

	for (pos = 0; (c = *str); str++)
	{
		if (strchr(stuff, c))
		{
			if (c == '$')
				buffer[pos++] = '$';
			else
				buffer[pos++] = '\\';
		}
		buffer[pos++] = c;
	}
	buffer[pos] = '\0';
	return buffer;
}

void	panic (char *format, ...)
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
	fprintf(stderr, "An unrecoverable logic error has occured.\n");
	fprintf(stderr, "Please fill out the BUG_FORM file, and include the following message:\n");
	fprintf(stderr, "Panic: [%s (%lu):%s]\n", irc_version, commit_id, buffer);
	panic_dump_call_stack();

	if (x_debug & DEBUG_CRASH)
		irc_exit(0, "EPIC Panic: %s (%lu):%s", irc_version, commit_id, buffer);
	else
		irc_exit(1, "EPIC Panic: %s (%lu):%s", irc_version, commit_id, buffer);
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
int 	end_strcmp (const char *one, const char *two, int bytes)
{
	if (bytes < strlen(one))
		return (strcmp(one + strlen (one) - (size_t) bytes, two));
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
char*	exec_pipe (char *executable, char *input, size_t *len, char**args)
{
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
		setuid(getuid());
		setgid(getgid());
		execvp(executable, args);
#if 0
		/*
		 * OK, well the problem with this is that the message
		 * is going to go out on stdout right, and where does
		 * that end up?
		 */
		yell("Cannot exec %s: %s", 
			executable, strerror(errno));
#endif
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
}

/*
 * exec() something and return three FILE's.
 *
 * On failure, close everything and return NULL.
 */
FILE **	open_exec (char *executable, char **args)
{
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
			setuid(getuid());
			setgid(getgid());
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
}

static FILE *	open_compression (char *executable, char *filename)
{
	FILE *	file_pointer;
	int 	pipes[2] = {-1, -1};

	if (pipe(pipes) == -1)
	{
		yell("Cannot start decompression: %s\n", strerror(errno));
		if (pipes[0] != -1)
		{
			close(pipes[0]);
			close(pipes[1]);
		}
		return NULL;
	}

	switch (fork())
	{
		case -1:
		{
			yell("Cannot start decompression: %s\n", 
					strerror(errno));
			return NULL;
		}
		case 0:
		{
			dup2(pipes[1], 1);
			close(pipes[0]);
			close(2);	/* we dont want to see errors */
			setuid(getuid());
			setgid(getgid());
			execl(executable, executable, "-c", filename, NULL);
			_exit(0);
		}
		default :
		{
			close(pipes[1]);
			if (!(file_pointer = fdopen(pipes[0], "r")))
			{
				yell("Cannot start decompression: %s\n", 
						strerror(errno));
				return NULL;
			}
			break;
		}
	}
	return file_pointer;
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
FILE *	uzfopen (char **filename, const char *path, int do_error)
{
static int		setup				= 0;
static 	Filename 	path_to_gunzip;
static	Filename 	path_to_uncompress;
static 	Filename 	path_to_bunzip2;
	int 		ok_to_decompress 		= 0;
	Filename	fullname;
	Filename	candidate;
	Stat 		file_info;
	FILE *		doh;

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
		if (!*path_to_gunzip)
		{
			if (do_error)
				yell("Cannot open file %s because gunzip "
					"was not found", *filename);
			goto error_cleanup;
		}

		ok_to_decompress = 2;
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

		stat(fullname, &file_info);
		if (S_ISDIR(file_info.st_mode))
		{
		    if (do_error)
			yell("%s is a directory", fullname);
		    goto error_cleanup;
		}
		if (file_info.st_mode & 0111)
		{
		    if (do_error)
			yell("Cannot open %s -- executable file", fullname);
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
	}

	/* Its not a compressed file... Try to open it regular-like. */
	else if ((doh = fopen(*filename, "r")))
		return doh;

	/* nope.. we just cant seem to open this file... */
	else if (do_error)
		yell("Cannot open file %s: %s", *filename, strerror(errno));

	goto error_cleanup;

file_not_found:
	if (do_error)
		yell("File not found: %s", *filename);

error_cleanup:
	new_free(filename);
	return NULL;
}


/*
 * slurp_file opens up a file and puts the contents into 'buffer'.
 * The size of 'buffer' is returned.
 */ 
int	slurp_file (char **buffer, char *filename)
{
	char *	local_buffer;
	size_t	offset;
	off_t	local_buffer_size;
	off_t	file_size;
	Stat	s;
	FILE *	file;
	size_t	count;

	file = uzfopen(&filename, get_string_var(LOAD_PATH_VAR), 1);
	if (stat(filename, &s) < 0)
	{
		fclose(file);
		new_free(&filename);
		return -1;		/* Whatever. */
	}

	file_size = s.st_size;
	if (!end_strcmp(filename, ".gz", 3))
		file_size *= 7;
	else if (!end_strcmp(filename, ".bz2", 4))
		file_size *= 10;
	else if (!end_strcmp(filename, ".Z", 2))
		file_size *= 5;

	local_buffer = new_malloc(file_size);
	local_buffer_size = file_size;
	offset = 0;

	do
	{
		count = fread(local_buffer + offset, 
			      local_buffer_size - offset, 1, file);
		offset += count;

		if (!feof(file))
		{
			local_buffer_size += (file_size * 3);
			new_realloc((void **)&local_buffer, local_buffer_size);
			continue;
		}
	}
	while (0);

	*buffer = local_buffer;
	return offset;
}



int 	fw_strcmp(comp_len_func *compar, char *one, char *two)
{
	int len = 0;
	char *pos = one;

	while (!my_isspace(*pos))
		pos++, len++;

	return compar(one, two, len);
}



/* 
 * Compares the last word in 'one' to the string 'two'.  You must provide
 * the compar function.  my_stricmp is a good default.
 */
int 	lw_strcmp(comp_func *compar, char *one, char *two)
{
	char *pos = one + strlen(one) - 1;

	if (pos > one)			/* cant do pos[-1] if pos == one */
		while (!my_isspace(pos[-1]) && (pos > one))
			pos--;
	else
		pos = one;

	if (compar)
		return compar(pos, two);
	else
		return my_stricmp(pos, two);
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

	if (!stat(filename, &statbuf))
		return (off_t)(statbuf.st_size);
	else
		return -1;
}

int	file_exists (const char *filename)
{
	if (file_size(filename) == -1)
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

/* Gets the time in second/usecond if you can,  second/0 if you cant. */
Timeval get_time (Timeval *timer)
{
	static Timeval timer2;

#ifdef HAVE_GETTIMEOFDAY
	if (timer)
	{
		gettimeofday(timer, NULL);
		return *timer;
	}
	gettimeofday(&timer2, NULL);
	return timer2;
#else
	time_t time2 = time(NULL);

	if (timer)
	{
		timer->tv_sec = time2;
		timer->tv_usec = 0;
		return *timer;
	}
	timer2.tv_sec = time2;
	timer2.tv_usec = 0;
	return timer2;
#endif
}

/* 
 * calculates the time elapsed between 'one' and 'two' where they were
 * gotten probably with a call to get_time.  'one' should be the older
 * timer and 'two' should be the most recent timer.
 */
double 	time_diff (Timeval one, Timeval two)
{
	Timeval td;

	td.tv_sec = two.tv_sec - one.tv_sec;
	td.tv_usec = two.tv_usec - one.tv_usec;

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
Timeval time_subtract (Timeval one, Timeval two)
{
	Timeval td;

	td.tv_sec = two.tv_sec - one.tv_sec;
	td.tv_usec = two.tv_usec - one.tv_usec;
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
Timeval time_add (Timeval one, Timeval two)
{
	Timeval td;

	td.tv_usec = one.tv_usec + two.tv_usec;
	td.tv_sec = one.tv_sec + two.tv_sec;
	if (td.tv_usec >= 1000000)
	{
		td.tv_usec -= 1000000;
		td.tv_sec++;
	}
	return td;
}


char *	plural (int number)
{
	return (number != 1) ? "s" : empty_string;
}

char *	my_ctime (time_t when)
{
	return chop(ctime(&when), 1);
}


char *	ltoa (long foo)
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

char *	ftoa (double foo)
{
	static char buffer [BIG_BUFFER_SIZE + 1];
	extern double fmod (double, double);

	if (get_int_var(FLOATING_POINT_MATH_VAR)) {
		sprintf(buffer, "%.*g", get_int_var(FLOATING_POINT_PRECISION_VAR), foo);
	} else {
		foo -= fmod(foo, 1);
		sprintf(buffer, "%.0f", foo);
	}
	return buffer;
}

/*
 * Formats "src" into "dest" using the given length.  If "length" is
 * negative, then the string is right-justified.  If "length" is
 * zero, nothing happens.  Sure, i cheat, but its cheaper then doing
 * two sprintf's.
 *
 * Changed to use the PAD_CHAR variable, which allows the user to specify
 * what character should be used to "fill out" the padding.
 */
char *	strformat (char *dest, const char *src, int length, int pad)
{
	char *		ptr1 = dest;
	const char *	ptr2 = src;
	int 		tmplen = length;
	int 		abslen;
	char 		padc;

	abslen = (length >= 0 ? length : -length);
	if ((padc = (char)pad) == 0)
		padc = ' ';

	/* Cheat by spacing out 'dest' */
	for (tmplen = abslen - 1; tmplen >= 0; tmplen--)
		dest[tmplen] = padc;
	dest[abslen] = 0;

	/* Then cheat further by deciding where the string should go. */
	if (length > 0)		/* left justified */
	{
		while ((length-- > 0) && *ptr2)
			*ptr1++ = *ptr2++;
	}
	else if (length < 0)	/* right justified */
	{
		length = -length;
		ptr1 = dest;
		ptr2 = src;
		if (strlen(src) < length)
			ptr1 += length - strlen(src);
		while ((length-- > 0) && *ptr2)
			*ptr1++ = *ptr2++;
	}
	return dest;
}


/* 
 * MatchingBracket returns the next unescaped bracket of the given type 
 * This used to be real simple (see the final else clause), but very
 * expensive.  Since its called a lot, i unrolled the two most common cases
 * (parens and brackets) and parsed them out with switches, which should 
 * really help the cpu usage.  I hope.  Everything else just falls through
 * and uses the old tried and true method.
 */
char *	MatchingBracket (char *string, char left, char right)
{
	int	bracket_count = 1;

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
						return string;
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
						return string;
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
					return string;
			}
			string++;
		}
	}

	return NULL;
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
	int	counter;
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

int 	splitw (char *str, char ***to)
{
	int numwords = word_count(str);
	int counter;

	if (numwords)
	{
		*to = (char **)new_malloc(sizeof(char *) * numwords);
		for (counter = 0; counter < numwords; counter++)
			(*to)[counter] = safe_new_next_arg(str, &str);
	}
	else
		*to = NULL;

	return numwords;
}

char *	unsplitw (char ***container, int howmany)
{
	char *retval = NULL;
	char **str = *container;
	size_t clue = 0;

	if (!str || !*str)
		return NULL;

	while (howmany)
	{
		if (*str && **str) m_sc3cat(&retval, " ", *str, &clue);
		str++, howmany--;
	}

	new_free((char **)container);
	return retval;
}

double strtod();	/* sunos must die. */
int 	check_val (char *sub)
{
	double sval;
	char *endptr;

	if (!*sub)
		return 0;

	/* get the numeric value (if any). */
	errno = 0;
	sval = strtod(sub, &endptr);

	/* Numbers that cause exceptional conditions in strtod() are true */
	if (errno == ERANGE || isinf(sval) || isnan(sval))
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

/*
 * Appends the given character to the string
 */
char 	*strmccat (char *str, char c, int howmany)
{
	int x = strlen(str);

	if (x < howmany)
		str[x] = c;
	str[x+1] = 0;

	return str;
}

/*
 * Pull a substring out of a larger string
 * If the ending delimiter doesnt occur, then we dont pass
 * anything (by definition).  This is because we dont want
 * to introduce a back door into CTCP handlers.
 */
char 	*pullstr (char *source_string, char *dest_string)
{
	char delim = *source_string;
	char *end;

	end = strchr(source_string + 1, delim);

	/* If there is no closing delim, then we punt. */
	if (!end)
		return NULL;

	*end = 0;
	end++;

	strcpy(dest_string, source_string + 1);
	strcpy(source_string, end);
	return dest_string;
}


int 	empty (const char *str)
{
#if 0
	while (str && *str && *str == ' ')
		str++;
#endif

	if (str && *str)
		return 0;

	return 1;
}


/* makes foo[one][two] look like tmp.one.two -- got it? */
char *	remove_brackets (const char *name, const char *args, int *arg_flag)
{
	char 	*ptr, 
		*right, 
		*result1, 
		*rptr, 
		*retval = NULL;

	/* XXXX - ugh. */
	rptr = m_strdup(name);

	while ((ptr = strchr(rptr, '[')))
	{
		*ptr++ = 0;
		right = ptr;
		if ((ptr = MatchingBracket(right, '[', ']')))
			*ptr++ = 0;

		if (args)
			result1 = expand_alias(right, args, arg_flag, NULL);
		else
			result1 = right;

		retval = m_3dup(rptr, ".", result1);
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

long	my_atol (const char *str)
{
	if (str)
		return (long) strtol(str, NULL, 0);
	else
		return 0L;
}

char *	m_dupchar (int i)
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
size_t 	streq (const char *one, const char *two)
{
	size_t cnt = 0;

	while (*one && *two && *one == *two)
		cnt++, one++, two++;

	return cnt;
}

char *	m_strndup (const char *str, size_t len)
{
	char *retval = (char *)new_malloc(len + 1);
	return strmcpy(retval, str, len);
}

char *	spanstr (const char *str, const char *tar)
{
	int cnt = 1;
	const char *p;

	for ( ; *str; str++, cnt++)
	{
		for (p = tar; *p; p++)
		{
			if (*p == *str)
				return (char *)p;
		}
	}

	return 0;
}

char *	prntdump(const char *ptr, size_t size)
{
	int i;
static char dump[65];

	strmcat(dump, ptr, 64);

	for (i = 0; i < size && i < 64; i++)
	{
		if (!isgraph(dump[i]) && !isspace(dump[i]))
			dump[i] = '.';
	}
	if (i == 64)
		dump[63] = '>';
	dump[i] = 0;
	return dump;
}

/* XXXX this doesnt belong here. im not sure where it goes, though. */
char *	get_userhost (void)
{
	strmcpy(userhost, username, NAME_LEN);
	strmcat(userhost, "@", NAME_LEN);
	strmcat(userhost, hostname, NAME_LEN);
	return userhost;
}


/* Fancy attempt to compensate for broken time_t's */
double	time_to_next_minute (void)
{
static	int 	which = 0;
	Timeval	now, then;

	get_time(&now);

	/* 
	 * The first time called, try to determine if the system clock
	 * is an exact multiple of 60 at the top of every minute.  If it
	 * is, then we will use the "60 trick" to optimize calculations.
	 * If it is not, then we will do it the hard time every time.
	 */
	if (which == 0)
	{
		struct tm *now_tm = gmtime(&now.tv_sec);

		if (!which)
		{
			if (now_tm->tm_sec == now.tv_sec % 60)
				which = 1;
			else
				which = 2;
		}
	}

	then.tv_usec = 1000000 - now.tv_usec;
	if (which == 1)
		then.tv_sec = 60 - (now.tv_sec + 1) % 60;
	else 	/* which == 2 */
	{
		struct tm *now_tm = gmtime(&now.tv_sec);
		then.tv_sec = 60 - (now_tm->tm_sec + 1) % 60;
	}

	return (double)then.tv_sec + (double)then.tv_usec / 1000000;
}

/*
 * An strcpy that is guaranteed to be safe for overlaps.
 * Warning: This may _only_ be called when one and two overlap!
 */
char *	ov_strcpy (char *one, const char *two)
{
	if (two > one)
	{
		while (two && *two)
			*one++ = *two++;
		*one = 0;
	}
	return one;
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
	while (string && string[0] && string[1])
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

/* Dest should be big enough to hold "src" */
void	strip_control (const char *src, char *dest)
{
	for (; *src; src++)
	{
		if (isgraph(*src) || isspace(*src))
			*dest++ = *src;
	}

	*dest++ = 0;
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


char *	encode(const char *str, size_t len)
{
	char *retval;
	char *ptr;

	if (len == -1)
		len = strlen(str);

	ptr = retval = new_malloc(len * 2 + 1);
	while (len)
	{
		*ptr++ = ((unsigned char)*str >> 4) + 0x41;
		*ptr++ = ((unsigned char)*str & 0x0f) + 0x41;
		str++;
		len--;
 	}
	*ptr = 0;
	return retval;
}

char *	decode(const char *str)
{
	char *retval;
	char *ptr;
	int len = strlen(str);

	ptr = retval = new_malloc(len / 2 + 1);
	while (len >= 2)
	{
		*ptr++ = ((str[0] - 0x41) << 4) | (str[1] - 0x41);
		str += 2;
		len -= 2;
	}
	*ptr = 0;
	return retval;
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
 */
int	figure_out_address (char *nuh, char **nick, char **user, char **host)
{
static 	char 	*mystuff = NULL;
	char 	*bang, 
		*at, 
		*dot = NULL;

	/* Dont bother with channels, theyre ok. */
	if (*nuh == '#' || *nuh == '&')
		return -1;

	malloc_strcpy(&mystuff, nuh);

	*host = star;


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
			dot = strchr(at + 1, '.');
		}
	}
	else if ((at = strchr(mystuff, '@')))
	{
		*at = 0;
		dot = strchr(at + 1, '.');
	}
	else 
		dot = strchr(mystuff, '.');

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
			if (dot)		/* nick!host.domain */
			{
				*nick = mystuff;
				*user = star;
				*host = at + 1;
			}
			else			/* nick!user */
			{
				*nick = mystuff;
				*user = star;
				*host = star;
			}
		}
	}
	else
	{
		if (at)
		{
			/* user@host.domain */
			*nick = star;
			*user = mystuff;
			*host = at + 1;
		}
		else
		{
			if (dot)		/* host.domain */
			{
				*nick = star;
				*user = star;
				*host = mystuff;
			}
			else			/* nick */
			{
				*nick = mystuff;
				*user = star;
				*host = star;
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
	char	*dot;
	int	number;

	/* determine if we have an IP, use dot to hold this */
	/* is_number is better than my_atol since floating point
	 * base 36 numbers are pretty much invalid as IPs.
	 */
	if ((dot = strrchr(fqdn, '.')) && is_number(dot + 1))
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
		*host = empty_string;
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
		*host = empty_string;
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
		*host = empty_string;
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
		*host = empty_string;
		*domain = fqdn;
	}

	return 0;
}

int 	count_char (const unsigned char *src, const unsigned char look)
{
	const unsigned char *t;
	int	cnt = 0;

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
 * We know that the final strcpy() is safe, since we never make a string that
 * is longer than the source string, always less than or equal in size.
 */
void	mask_digits (char **host)
{
	char	*src_ptr;
	char 	*retval, *retval_ptr;

	retval = retval_ptr = alloca(strlen(*host) + 1);
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
	strcpy(*host, retval);
	return;
}

char *	strpcat (char *source, const char *format, ...)
{
	va_list args;
	char	buffer[BIG_BUFFER_SIZE + 1];

	va_start(args, format);
	vsnprintf(buffer, BIG_BUFFER_SIZE, format, args);
	va_end(args);

	strcat(source, buffer);
	return source;
}

char *	strmpcat (char *source, size_t siz, const char *format, ...)
{
	va_list args;
	char	buffer[BIG_BUFFER_SIZE + 1];

	va_start(args, format);
	vsnprintf(buffer, BIG_BUFFER_SIZE, format, args);
	va_end(args);

	strmcat(source, buffer, siz);
	return source;
}


u_char *strcpy_nocolorcodes (u_char *dest, const u_char *source)
{
	u_char	*save = dest;

	do
	{
		while (*source == 3)
			source = skip_ctl_c_seq(source, NULL, NULL);
		*dest++ = *source;
	}
	while (*source++);

	return save;
}

/*
 * This mangles up 'incoming' corresponding to the current values of
 * /set mangle_inbound or /set mangle_outbound.
 * 'incoming' needs to be at least _ELEVEN_ as big as neccesary
 * (ie, sizeof(incoming) >= strlen(incoming) * 11 + 1)
 */
size_t	mangle_line	(char *incoming, int how, size_t how_much)
{
	int	stuff;
	char	*buffer;
	int	i;
	char	*s;

	stuff = how;
	buffer = alloca(how_much + 1);	/* Absurdly large */

#if notyet
	if (stuff & STRIP_CTCP2)
	{
		char *output;

		output = strip_ctcp2(incoming);
		strlcpy(incoming, output, how_much);
		new_free(&output);
	}
	else if (stuff & MANGLE_INBOUND_CTCP2)
	{
		char *output;

		output = ctcp2_to_ircII(incoming);
		strlcpy(incoming, output, how_much);
		new_free(&output);
	}
	else if (stuff & MANGLE_OUTBOUND_CTCP2)
	{
		char *output;

		output = ircII_to_ctcp2(incoming);
		strlcpy(incoming, output, how_much);
		new_free(&output);
	}
#endif

	if (stuff & MANGLE_ESCAPES)
	{
		for (i = 0; incoming[i]; i++)
		{
			if (incoming[i] == 0x1b)
				incoming[i] = 0x5b;
		}
	}

	if (stuff & MANGLE_ANSI_CODES)
	{
		/* normalize_string can expand up to three times */
		char *output;

		normalize_never_xlate = 1;	/* XXXXX */
		output = normalize_string(incoming, 1);	/* Should be ok */
		normalize_never_xlate = 0;	/* XXXXX */
		if (strlcpy(incoming, output, how_much) > how_much)
			say("Mangle_line truncating results. #1 -- "
				"Email jnelson@acronet.net [%d] [%d]",
				strlen(output), how_much);
		new_free(&output);
	}

	/*
	 * Now we mangle the individual codes
	 */
	for (i = 0, s = incoming; *s; s++)
	{
		switch (*s)
		{
			case 003:		/* color codes */
			{
				int 		lhs = 0, 
						rhs = 0;
				char 		*end;

				end = (char *)skip_ctl_c_seq(s, &lhs, &rhs);
				if (!(stuff & STRIP_COLOR))
				{
					while (s < end)
						buffer[i++] = *s++;
				}
				s = end - 1;
				break;
			}
			case REV_TOG:		/* Reverse */
			{
				if (!(stuff & STRIP_REVERSE))
					buffer[i++] = REV_TOG;
				break;
			}
			case UND_TOG:		/* Underline */
			{
				if (!(stuff & STRIP_UNDERLINE))
					buffer[i++] = UND_TOG;
				break;
			}
			case BOLD_TOG:		/* Bold */
			{
				if (!(stuff & STRIP_BOLD))
					buffer[i++] = BOLD_TOG;
				break;
			}
			case BLINK_TOG: 	/* Flashing */
			{
				if (!(stuff & STRIP_BLINK))
					buffer[i++] = BLINK_TOG;
				break;
			}
			case ROM_CHAR:		/* Special rom-chars */
			{
				if (!(stuff & STRIP_ROM_CHAR))
					buffer[i++] = ROM_CHAR;
				break;
			}
			case ND_SPACE:		/* Nondestructive spaces */
			{
				if (!(stuff & STRIP_ND_SPACE))
					buffer[i++] = ND_SPACE;
				break;
			}
			case ALT_TOG:		/* Alternate character set */
			{
				if (!(stuff & STRIP_ALT_CHAR))
					buffer[i++] = ALT_TOG;
				break;
			}
			case ALL_OFF:		/* ALL OFF attribute */
			{
				if (!(stuff & STRIP_ALL_OFF))
					buffer[i++] = ALL_OFF;
				break;
			}
			default:		/* Everything else */
				if (!(stuff & STRIP_OTHER))
					buffer[i++] = *s;
		}
	}

	buffer[i] = 0;
	return strlcpy(incoming, buffer, how_much);
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

	read(random_fd, (void *)&value, sizeof(value));
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

	return (unsigned long)bsd_arc4random();
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
 * urlencode: converts non-alphanumeric characters to hexidecimal codes
 * Contributed by SrFrog
 */
char *	urlencode (const char *s)
{
	static const char unsafe[] = "`'!@#$%^&*(){}<>~|\\\";? ,/";
	static const char hexnum[] = "0123456789ABCDEF";
	const char *p1;
	char *	p2;
	size_t	len;
	char *	retval;

	if (!s || !*s)
		return NULL;

	len = strlen(s);
	retval = new_malloc(len * 3 + 1);

	for (p1 = s, p2 = retval; *p1; p1++)
	{
		if (strchr(unsafe, *p1))
		{
			unsigned c = (unsigned) *p1;

			*p2++ = '%';
			*p2++ = hexnum[c >> 4];
			*p2++ = hexnum[c & 0x0f];
		}
		else
			*p2++ = *p1;
	}
	*p2 = 0;

	return retval;
}

#define XTOI(x) 					\
( 							\
	((x) >= '0' && (x) <= '9') 			\
		? ((x) - '0') 				\
		: ( ((x) >= 'A' && (x) <= 'F')		\
		    ? (((x) - 'A') + 10) 		\
		    : ( ((x) >= 'a' && (x) <= 'f')	\
			?  (((x) - 'a') + 10)		\
			: -1				\
		      )					\
		  )					\
)

char *	urldecode (char *s, size_t *length)
{
	const char *p1;
	char *	p2;
	size_t	len;
	char *	retval;
	int	val1;
	int	val2;

	if (!s || !*s)
		return NULL;

	len = length ? *length : strlen(s);
	retval = alloca(len + 1);

	for (p1 = s, p2 = retval; len--; p1++, p2++)
	{
		if (*p1 == '%' && len >= 2 &&
		    (((val1 = XTOI(p1[1])) != -1) &&
		     ((val2 = XTOI(p1[2])) != -1)))
		{
			p1++, p1++;
			len--, len--;
			*p2 = (val1 << 4) | val2;
		}
		else
			*p2 = *p1;
	}

	*p2 = 0;
	if (length)
		*length = p2 - retval;
	return memcpy(s, retval, p2 - retval + 1);
}

/*
 * quote_it: This quotes the given string making it sendable via irc.  A
 * pointer to the length of the data is required and the data need not be
 * null terminated (it can contain nulls).  Returned is a malloced, null
 * terminated string.
 */
char	*enquote_it (char *str, size_t len)
{
	char	*buffer = new_malloc(len + 5);
	char	*ptr = buffer;
	int	i;
	int	size = len;

	for (i = 0; i < len; i++)
	{
		if (ptr-buffer >= size)
		{
			int j = ptr-buffer;
			size += 256;
			RESIZE(buffer, char, size + 5);
			ptr = buffer + j;
		}

		switch (str[i])
		{
			case CTCP_DELIM_CHAR:	*ptr++ = CTCP_QUOTE_CHAR;
						*ptr++ = 'a';
						break;
			case '\n':		*ptr++ = CTCP_QUOTE_CHAR;
						*ptr++ = 'n';
						break;
			case '\r':		*ptr++ = CTCP_QUOTE_CHAR;
						*ptr++ = 'r';
						break;
			case CTCP_QUOTE_CHAR:	*ptr++ = CTCP_QUOTE_CHAR;
						*ptr++ = CTCP_QUOTE_CHAR;
						break;
			case '\0':		*ptr++ = CTCP_QUOTE_CHAR;
						*ptr++ = '0';
						break;
			default:		*ptr++ = str[i];
						break;
		}
	}
	*ptr = '\0';
	return buffer;
}

/*
 * ctcp_unquote_it: This takes a null terminated string that had previously
 * been quoted using ctcp_quote_it and unquotes it.  Returned is a malloced
 * space pointing to the unquoted string.  NOTE: a trailing null is added for
 * convenied, but the returned data may contain nulls!.  The len is modified
 * to contain the size of the data returned. 
 */
char	*dequote_it (char *str, size_t *len)
{
	char	*buffer;
	char	*ptr;
	char	c;
	int	i,
		new_size = 0;

	buffer = (char *) new_malloc(sizeof(char) * *len + 1);
	ptr = buffer;
	i = 0;
	while (i < *len)
	{
		if ((c = str[i++]) == CTCP_QUOTE_CHAR)
		{
			switch (c = str[i++])
			{
				case CTCP_QUOTE_CHAR:
					*ptr++ = CTCP_QUOTE_CHAR;
					break;
				case 'a':
					*ptr++ = CTCP_DELIM_CHAR;
					break;
				case 'n':
					*ptr++ = '\n';
					break;
				case 'r':
					*ptr++ = '\r';
					break;
				case '0':
					*ptr++ = '\0';
					break;
				default:
					*ptr++ = c;
					break;
			}
		}
		else
			*ptr++ = c;
		new_size++;
	}
	*ptr = '\0';
	*len = new_size;
	return (buffer);
}

unsigned char isspace_table [256] = 
{
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	1,	1,	1,	1,	1,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	1,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,

	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
};

#if 0
/* 
 * word_count:  Efficient way to find out how many words are in
 * a given string.  Relies on my_isspace() not being broken.
 */
int     word_count (const char *str)
{
	const char *	p;
	int		count = 0;

        if (!str || !*str)
                return 0;

	if (*str == '"')
	{
		while (*str && *++str != '"')
			str++;
		if (!*str)
			return 1;
		str++;
	}
	else while (my_isspace(*str))
		str++;

	for (count = 1, p = str; *p; p++)
        {
		if ((my_isspace(*p) && p[1] == '"'))
		{
			if (*p != '"')
				p++;
			while (p[1] && *++p != '"')
				;
			count++;
			continue;
		}

		if (my_isspace(*p) && !isspace(p[1]))
			count++;
	}

	return count;
}
#else

/* 
 * XXX XXX XXX -- This is expensive, and ugly, but it counts words
 * in the same way as new_next_word, and that is all that counts for now.
 */
int	word_count (const char *ptr)
{
	int	count = 0;

	if (!ptr || !*ptr)
		return 0;

	/* Skip any leading whitespace */
	while (*ptr && risspace(*ptr))
		ptr++;

	while (ptr && *ptr)
	{
		/* Always pre-count words */
		count++;

		/* 
		 * If this is an extended word, then skip everything
		 * up to the first un-backslashed double quote.
		 */
		if (*ptr == '"')
		{
			for (ptr++; *ptr; ptr++)
			{
				if (*ptr == '\\' && ptr[1])
					ptr++;
				else if (ptr[1] && !risspace(ptr[1]))
				{}
				else if (*ptr == '"')
				{
					ptr++;
					break;
				}
			}
		}

		/* 
		 * This is a regular word, skip all of the non-whitespace
		 * characters.
		 */
		else
		{
			while (*ptr && !risspace(*ptr))
				ptr++;
		}

		/* Skip any leading whitespace before the next word */
		while (*ptr && risspace(*ptr))
			ptr++;
	}

	return count;
}
#endif

/*
 * A "forward quote" is the first double quote we find that has a space
 * or the end of string after it; or the end of the string.
 */
const char *	find_forward_quote (const char *input, const char *start)
{
	/*
	 * An "extended word" is defined as:
	 *	<SPACE> <QUOTE> <ANY>* <QUOTE> <SPACE>
	 * <SPACE> := <WORD START> | <WORD END> | <" "> | <"\t"> | <"\n">
	 * <QUOTE> :- <'"'>
	 * <ANY>   := ANY ASCII CHAR 0 .. 256
	 */
	/* Make sure we are actually looking at a double-quote */
	if (*input != '"')
		return NULL;

	/* 
	 * Make sure that the character before is the start of the
	 * string or that it is a space.
	 */
	if (input > start && !risspace(input[-1]))
		return NULL;

	/*
	 * Walk the string looking for a double quote.  Yes, we do 
	 * really have to check for \'s, still, because the following
	 * still is one word:
	 *			"one\" two"
	 * Once we find a double quote, then it must be followed by 
	 * either the end of string (chr 0) or a space.  If we find 
	 * that, return the position of the double-quote.
	 */
	for (input++; *input; input++)
	{
		if (input[0] == '\\' && input[1])
			input++;
		else if (input[0] == '"')
		{
			if (input[1] == 0)
				return input;
			else if (risspace(input[1]))
				return input;
		}
	}

	/*
	 * If we get all the way to the end of the string w/o finding 
	 * a matching double-quote, return the position of the (chr 0), 
	 * which is a special meaning to the caller. 
	 */
	return input;		/* Bumped the end of string. doh! */
}

/*
 * A "backward quote" is the first double quote we find, going backward,
 * that has a space or the start of string after it; or the start of 
 * the string.
 */
const char *	find_backward_quote (const char *input, const char *start)
{
	const char *	saved_input = input;

	/*
	 * An "extended word" is defined as:
	 *	<SPACE> <QUOTE> <ANY>* <QUOTE> <SPACE>
	 * <SPACE> := <WORD START> | <WORD END> | <" "> | <"\t"> | <"\n">
	 * <QUOTE> :- <'"'>
	 * <ANY>   := ANY ASCII CHAR 0 .. 256
	 */
	/* Make sure we are actually looking at a double-quote */
	if (input[0] != '"')
		return NULL;

	/* 
	 * Make sure that the character after is either the end of the
	 * string, or that it is a space.
	 */
	if (input[1] && !risspace(input[1]))
		return NULL;

	/*
	 * Walk the string looking for a double quote.  Yes, we do 
	 * really have to check for \'s, still, because the following
	 * still is one word:
	 *			"one\" two"
	 * Once we find a double quote, then it must be followed by 
	 * either the end of string (chr 0) or a space.  If we find 
	 * that, return the position of the double-quote.
	 */
	for (input--; input > start; input--)
	{
		if (input[0] == '\\' && input[1])
			input++;
		else if (input[0] == '"')
		{
			if (input[1] == 0)
				return input;
			else if (risspace(input[1]))
				return input;
		}
	}

	/*
	 * If we get all the way to the start of the string w/o finding 
	 * a matching double-quote, then THIS IS NOT AN EXTENDED WORD!
	 * We need to re-do this word entirely by starting over and looking
	 * for a normal word.
	 */
	input = saved_input;
	while (input > start && !risspace(input[0]))
		input--;

	if (risspace(input[0]))
		input++;		/* Just in case we've gone too far */

	return input;		/* Wherever we are is fine. */
}

const char *	my_strerror (int err1, int err2)
{
static	char	buffer[1024];

	if (err1 == -1)
	{
	    if (err2 < 0)
	    {
#ifdef HAVE_HSTRERROR
		return hstrerror(h_errno);
#else
		return "Hostname lookup failure";
#endif
	    }
	    else
		return strerror(errno);
	}
	else if (err1 == -2)
	    return "The operation is not supported for the protocol family";
	else if (err1 == -3)
	    return "The hostname has no address in the protocol family";
	else if (err1 == -4)
	    return "The presentation internet address was invalid";
	else if (err1 == -5)
	    return "The hostname does not resolve";
	else if (err1 == -6)
	    return "There is no virtual host for the protocol family";
	else if (err1 == -7)
	    return "The remote peer to connect to was not provided";
	else if (err1 == -8)
	    return "The local and remote address are in different protocol families.";
	else if (err1 == -9)
	    return "Connection was not successful (may have timed out)";
	else if (err1 == -10)
	    return "Requested local port is not available.";
	else if (err1 == -11)
	{
	    snprintf(buffer, 1024, "Connect failed: %s", strerror(err2));
	    return buffer;
	}
	else if (err1 == -12)
	    return "Connection was not successful (may have been reset)";
	else if (err1 == -13)
	    return "The local address to bind to was not provided";
	else if (err1 == -14)
	    return "The protocol family does not make sense";
	else
	{
	    snprintf(buffer, 1024, "EPIC Network Error %d", err1);
	    return buffer;
	}
}

/* 
 * Should I switch over to using getaddrinfo() directly or is using
 * inet_strton() sufficient?
 */
const char *	switch_hostname (const char *new_hostname)
{
	char *	retval;
	ISA 	new_4;
	char 	v4_name[1024];
	int	accept4 = 0;
#ifdef INET6
	ISA6 	new_6;
#endif
	char	v6_name[1024];
	int	accept6 = 0;


	if (new_hostname == NULL)
	{
		new_free(&LocalIPv4Addr);
#ifdef INET6
		new_free(&LocalIPv6Addr);
#endif

		if (LocalHostName)
			retval = m_sprintf("Virtual Hostname [%s] will no longer be used", LocalHostName);
		else
			retval = m_sprintf("Virtual Hostname support is not activated");

		new_free(&LocalHostName);
		return retval;
	}

	strcpy(v4_name, "<none>");
	new_4.sin_family = AF_INET;
	if (!inet_strton(new_hostname, zero, (SA *)&new_4, 0)) {
		inet_ntostr((SA *)&new_4, v4_name, 1024, NULL, 0, NI_NUMERICHOST);
		accept4 = 1;
	}

	strcpy(v6_name, "<none>");
#ifdef INET6
	new_6.sin6_family = AF_INET6;
	if (!inet_strton(new_hostname, zero, (SA *)&new_6, 0)) {
		inet_ntostr((SA *)&new_6, v6_name, 1024, NULL, 0, NI_NUMERICHOST);
		accept6 = 1;
	}
#endif

	if (accept4 || accept6)
	{
		new_free(&LocalIPv4Addr);
#ifdef INET6
		new_free(&LocalIPv6Addr);
#endif

		if (accept4)
		{
		    LocalIPv4Addr = (ISA *)new_malloc(sizeof(*LocalIPv4Addr));
		    *LocalIPv4Addr = new_4;
		}

#ifdef INET6
		if (accept6) 
		{
		    LocalIPv6Addr = (ISA6 *)new_malloc(sizeof(*LocalIPv6Addr));
		    *LocalIPv6Addr = new_6;
		}
#endif

		malloc_strcpy(&LocalHostName, new_hostname);

		retval = m_sprintf("Local address changed to [%s] (%s) (%s)",
				LocalHostName, v4_name, v6_name);
	}
	else
		retval = m_sprintf("I cannot configure [%s] -- local address not changed.", new_hostname);

	return retval;
}

