/*
 * ircaux.c: some extra routines... not specific to irc... that I needed 
 *
 * Copyright 1990, 1991 Michael Sandrof
 * Copyright 1993, 1994 Matthew Green
 * Copyright 1993, 1999 EPIC Software labs
 * See the COPYRIGHT file for license information
 */

#if 0
static	char	rcsid[] = "@(#)$Id: ircaux.c,v 1.12 2001/11/12 21:46:45 jnelson Exp $";
#endif

#include "irc.h"
#include "screen.h"
#include <pwd.h>
#include <sys/stat.h>
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
} MO;

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
		panic("Malloc() failed, giving up!");

	/* Store the size of the allocation in the buffer. */
	ptr += sizeof(MO);
	magic(ptr) = ALLOC_MAGIC;
	alloc_size(ptr) = size;
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
	if (!size) 
		*ptr = really_new_free(ptr, fn, line);
	else if (!*ptr)
		*ptr = really_new_malloc(size, fn, line);
	else 
	{
		/* Make sure this is safe for realloc. */
		fatal_malloc_check(*ptr, NULL, fn, line);

		/* Copy everything, including the MO buffer */
		if (!(*ptr = (char *)realloc(mo_ptr(*ptr), size + sizeof(MO))))
			panic("realloc() failed, giving up!");

		/* Re-initalize the MO buffer; magic(*ptr) is already set. */
		*ptr += sizeof(MO);
		alloc_size(*ptr) = size;
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
char *	malloc_strcat (char **ptr, const char *src)
{
	size_t  msize;
	size_t  psize;

	if (*ptr)
	{
		if (alloc_size(*ptr) == FREED_VAL)
			panic("free()d pointer passed to malloc_strcat");

		if (!src)
			return *ptr;

		msize = (psize=strlen(*ptr)) + strlen(src) + 1;
		RESIZE(*ptr, char, msize);
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
		return m_c3cat(one, maybe, ifthere, clue);

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


char *	next_arg (char *str, char **new_ptr)
{
	char	*ptr;

	/* added by Sheik (kilau@prairie.nodak.edu) -- sanity */
	if (!str || !*str)
		return NULL;

	while (isspace(*str))
		str++;

	ptr = str;
	while (*ptr && !isspace(*ptr))
		ptr++;

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

char *	new_next_arg (char *str, char **new_ptr)
{
	char	*ptr,
		*start;

	if (!str || !*str)
		return NULL;

	ptr = str;
	while (*ptr && risspace(*ptr))
		ptr++;

	if (*ptr == '"')
	{
		start = ++ptr;
		for (str = start; *str; str++)
		{
			if (*str == '\\' && str[1])
				str++;
			else if (*str == '"')
			{
				*str++ = 0;
				if (risspace(*str))
					str++;
				break;
			}
		}
	}
	else
	{
		str = ptr;
		while (*str && !risspace(*str))
			str++;
		if (*str)
			*str++ = 0;
	}

	if (!*str || !*ptr)
		str = empty_string;

	if (new_ptr)
		*new_ptr = str;

	return ptr;
}


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
	*after = str;

	while (*after && **after && **after != ',')
		(*after)++;

	if (*after && **after == ',')
	{
		**after = 0;
		(*after)++;
	}

	return str;
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
char *	m_strcat_ues (char **dest, char *src, int unescape)
{
	int 	total_length;
	char *	ptr;
	char *	ptr2;
	int	z;

	if (!unescape)
	{
		malloc_strcat(dest, src);
		return *dest;
	}

	z = total_length = (*dest) ? strlen(*dest) : 0;
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
	return *dest;
}


/*
 * scanstr: looks for an occurrence of str in source.  If not found, returns
 * 0.  If it is found, returns the position in source (1 being the first
 * position).  Not the best way to handle this, but what the hell 
 */
int	scanstr (char *str, char *source)
{
	int	i,
		max,
		len;

	len = strlen(str);
	max = strlen(source) - len;
	for (i = 0; i <= max; i++, source++)
	{
		if (!my_strnicmp(source, str, len))
			return (i + 1);
	}
	return (0);
}

/* expand_twiddle: expands ~ in pathnames. */
char *	expand_twiddle (char *str)
{
	char	buffer[BIG_BUFFER_SIZE + 1];
	*buffer = 0;

	if (*str == '~')
	{
		str++;
		if (*str == '/' || *str == '\0')
		{
			strmcpy(buffer, my_path, BIG_BUFFER_SIZE);
			strmcat(buffer, str, BIG_BUFFER_SIZE);
		}
		else
		{
			char	*rest;
			struct	passwd *entry;

			if ((rest = strchr(str, '/')) != NULL)
				*rest++ = '\0';
			if ((entry = getpwnam(str)) != NULL)
			{
				strmcpy(buffer, entry->pw_dir, BIG_BUFFER_SIZE);
				if (rest)
				{
					strmcat(buffer, "/", BIG_BUFFER_SIZE);
					strmcat(buffer, rest, BIG_BUFFER_SIZE);
				}
			}
			else
				return (char *) NULL;
		}
	}
	else
		strmcpy(buffer, str, BIG_BUFFER_SIZE);

	return m_strdup(buffer);
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
char *	path_search (char *name, char *path)
{
	static	char	buffer[BIG_BUFFER_SIZE + 1];
	char	*ptr,
		*free_path = (char *) 0;

	/* No Path -> Error */
	if (!path)
		return NULL;		/* Take THAT! */

	/* A "relative" path is valid if the file exists */
	/* A "relative" path is searched in the path if the
	   filename doesnt really exist from where we are */
	if (strchr(name, '/') != NULL)
		if (!access(name, F_OK))
			return name;

	/* an absolute path is always checked, never searched */
	if (name[0] == '/')
		return (access(name, F_OK) ? (char *) 0 : name);

	/* This is cheating. >;-) */
	free_path = LOCAL_COPY(path);
	path = free_path;

	while (path)
	{
		if ((ptr = strchr(path, ':')) != NULL)
			*ptr++ = '\0';
		*buffer = 0;
		if (path[0] == '~')
		{
			strmcat(buffer, my_path, BIG_BUFFER_SIZE);
			path++;
		}
		strmcat(buffer, path, BIG_BUFFER_SIZE);
		strmcat(buffer, "/", BIG_BUFFER_SIZE);
		strmcat(buffer, name, BIG_BUFFER_SIZE);

		if (access(buffer, F_OK) == 0)
			break;
		path = ptr;
	}

	return (path != NULL) ? buffer : (char *) 0;
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
	fprintf(stderr, "Panic: [%s:%s]\n", irc_version, buffer);
	panic_dump_call_stack();

	if (x_debug & DEBUG_CRASH)
		irc_exit(0, "EPIC Panic: %s:%s", irc_version, buffer);
	else
		irc_exit(1, "EPIC Panic: %s:%s", irc_version, buffer);
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
 */
FILE *	uzfopen (char **filename, char *path, int do_error)
{
	static int	setup				= 0;
	int 		ok_to_decompress 		= 0;
	char *		filename_path;
	char *		filename_expanded;
	char 		filename_trying			[MAXPATHLEN + 2];
	char *		filename_blah;
	static char  *	path_to_gunzip = NULL;
	static char  *	path_to_uncompress = NULL;
	static char  *	path_to_bunzip = NULL;
	FILE *		doh;

	if (!setup)
	{
		char *gzip, *compress, *bzip;

		if (!(gzip = path_search("gunzip", getenv("PATH"))))
			gzip = empty_string;
		path_to_gunzip = m_strdup(gzip);

		if (!(compress = path_search("uncompress", getenv("PATH"))))
			compress = empty_string;
		path_to_uncompress = m_strdup(compress);

		if (!(bzip = path_search("bunzip", getenv("PATH"))))
			bzip = empty_string;
		path_to_bunzip = m_strdup(bzip);

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
	 * kev asked me to call expand_twiddle here 
	 */
	if (**filename == '~')
	{
		if ((filename_blah = expand_twiddle(*filename)))
		{
			filename_expanded = LOCAL_COPY(filename_blah);
			new_free(&filename_blah);
		}
		else
			filename_expanded = LOCAL_COPY(*filename);
	}
	else
		filename_expanded = LOCAL_COPY(*filename);


	/* 
	 * Look to see if the passed filename is a full compressed filename 
	 */
	if ((! end_strcmp(filename_expanded, ".gz", 3)) ||
	    (! end_strcmp(filename_expanded, ".z", 2))) 
	{
		if (*path_to_gunzip)
		{	
			ok_to_decompress = 2;
			filename_path = path_search(filename_expanded, path);
		}
		else
		{
			if (do_error)
				yell("Cannot open file %s because gunzip "
					"was not found", filename_trying);
			goto cleanup;
		}
	}
	else if (! end_strcmp(filename_expanded, ".Z", 2))
	{
		if (*path_to_gunzip || *path_to_uncompress)
		{
			ok_to_decompress = 1;
			filename_path = path_search(filename_expanded, path);
		}
		else
		{
			if (do_error)
				yell("Cannot open file %s becuase uncompress "
					"was not found", filename_trying);
			goto cleanup;
		}
	}
	else if (!end_strcmp(filename_expanded, ".bz2", 4))
	{
		if (*path_to_bunzip)
		{
			ok_to_decompress = 3;
			filename_path = path_search(filename_expanded, path);
		}
		else
		{
			if (do_error)
				yell("Cannot open file %s because bunzip "
					"was not found", filename_trying);
			goto cleanup;
		}
	}

	/* Right now it doesnt look like the file is a full compressed fn */
	else
	{
	    do
	    {
		/* Trivially, see if the file we were passed exists */
		if ((filename_path = path_search(filename_expanded, path)))
		{
			ok_to_decompress = 0;
			break;
		}

		/* Is there a "filename.gz"? */
		snprintf(filename_trying, MAXPATHLEN, "%s.gz", 
				filename_expanded);
		if ((filename_path = path_search(filename_trying, path)))
		{
			ok_to_decompress = 2;
			break;
		}

		/* Is there a "filename.Z"? */
		snprintf(filename_trying, MAXPATHLEN, "%s.Z", 
				filename_expanded);
		if ((filename_path = path_search(filename_trying, path)))
		{
			ok_to_decompress = 1;
			break;
		}

		/* Is there a "filename.z"? */
		snprintf(filename_trying, MAXPATHLEN, "%s.z", 
				filename_expanded);
		if ((filename_path = path_search(filename_trying, path)))
		{
			ok_to_decompress = 2;
			break;
		}

		/* Is there a "filename.bz2"? */
		snprintf(filename_trying, MAXPATHLEN, "%s.bz2", 
				filename_expanded);
		if ((filename_path = path_search(filename_trying, path)))
		{
			ok_to_decompress = 3;
			break;
		}
	    }
	    while (0);

	    if (filename_path)
	    {
		struct stat file_info;

		stat(filename_path, &file_info);
		if (file_info.st_mode & S_IFDIR)
		{
		    if (do_error)
			yell("%s is a directory", filename_path);
		    goto cleanup;
		}
		if (file_info.st_mode & 0111)
		{
		    if (do_error)
			yell("Cannot open %s -- executable file", 
				filename_path);
		    goto cleanup;
		}
	    }
	}

	if (!filename_path)
	{
		if (do_error)
			yell("File not found: %s", *filename);
		goto cleanup;
	}


	/* 
	 * At this point, we should have a filename in the variable
	 * *filename, and it should exist.  If ok_to_decompress is one, then
	 * we can gunzip the file if gunzip is available.  else we 
	 * uncompress the file.
	 */
	malloc_strcpy(filename, filename_path);
	if (ok_to_decompress)
	{
		     if ((ok_to_decompress <= 2) && *path_to_gunzip)
			return open_compression(path_to_gunzip, *filename);
		else if ((ok_to_decompress == 1) && *path_to_uncompress)
			return open_compression(path_to_uncompress, *filename);
		else if ((ok_to_decompress == 3) && *path_to_bunzip)
			return open_compression(path_to_bunzip, *filename);

		if (do_error)
			yell("Cannot open compressed file %s becuase no "
				"uncompressor was found", *filename);
		goto cleanup;
	}

	/* Its not a compressed file... Try to open it regular-like. */
	if ((doh = fopen(filename_path, "r")))
		return doh;

	/* nope.. we just cant seem to open this file... */
	if (do_error)
		yell("Cannot open file %s: %s", filename_path, strerror(errno));

cleanup:
	new_free(filename);
	return NULL;
}


/*
 * slurp_file opens up a file and puts the contents into 'buffer'.
 * The size of 'buffer' is returned.
 */ 
int	slurp_file (char **buffer, char *filename)
{
	char *		local_buffer;
	size_t		offset;
	off_t		local_buffer_size;
	off_t		file_size;
	struct stat	s;
	FILE *		file;
	size_t		count;

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
	struct stat statbuf;

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

/* Gets the time in second/usecond if you can,  second/0 if you cant. */
struct timeval 	get_time (struct timeval *timer)
{
	static struct timeval timer2;

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
		timer.tv_sec = time2;
		timer.tv_usec = 0;
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
double 	time_diff (struct timeval one, struct timeval two)
{
	struct timeval td;

	td.tv_sec = two.tv_sec - one.tv_sec;
	td.tv_usec = two.tv_usec - one.tv_usec;

	return (double)td.tv_sec + ((double)td.tv_usec / 1000000.0);
}

struct timeval double_to_timeval (double x)
{
	struct timeval td;
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
struct timeval 	time_subtract (struct timeval one, struct timeval two)
{
	struct timeval td;

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
struct timeval 	time_add (struct timeval one, struct timeval two)
{
	struct timeval td;

	td.tv_usec = one.tv_usec + two.tv_usec;
	td.tv_sec = one.tv_sec + two.tv_sec;
	if (td.tv_usec > 1000000)
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

	sprintf(buffer, "%f", foo);
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
			(*to)[counter] = new_next_arg(str, &str);
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
	sval = strtod(sub, &endptr);

	/* Its OK if:
	 *  1) the f-val is not zero.
	 *  2) the first invalid character was not a null.
	 *  3) there were no valid f-chars.
	 */
	if (sval || *endptr || (sub == endptr))
		return 1;

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
struct timeval 	time_to_next_minute (void)
{
	static	int 	which = 0;
	struct timeval	now, then;

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
	return then;
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


char *	encode(const char *str, int len)
{
	char *retval;
	char *ptr;

	if (len == -1)
		len = strlen(str);

	ptr = retval = new_malloc(len * 2 + 1);

	while (len)
	{
		*ptr++ = (*str >> 4) + 0x41;
		*ptr++ = (*str & 0x0f) + 0x41;
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
int	figure_out_address (char *nuh, char **nick, char **user, char **host, char **domain, int *ip)
{
static 	char 	*mystuff = NULL;
	char 	*firstback, 
		*secondback, 
		*thirdback, 
		*fourthback;
	char 	*bang, 
		*at, 
		*dot = NULL,
		*myhost = star, 
		*endstring;
	int	number;

	/* Dont bother with channels, theyre ok. */
	if (*nuh == '#' || *nuh == '&')
		return -1;

	malloc_strcpy(&mystuff, nuh);

	*host = *domain = star;
	*ip = 0;


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
			myhost = at + 1;
		}
		else
		{
			if (dot)		/* nick!host.domain */
			{
				*nick = mystuff;
				*user = star;
				myhost = at + 1;
			}
			else			/* nick!user */
			{
				*nick = mystuff;
				*user = star;
				myhost = star;
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
			myhost = at + 1;
		}
		else
		{
			if (dot)		/* host.domain */
			{
				*nick = star;
				*user = star;
				myhost = mystuff;
			}
			else			/* nick */
			{
				*nick = mystuff;
				*user = star;
				myhost = star;
			}
		}
	}


/*
 * STAGE TWO -- EXTRACT THE HOST AND DOMAIN FROM MYHOST
 */

	/*
	 * At this point, 'myhost' points what what we think the hostname
	 * is.  We chop it up into discrete parts and see what we end up with.
	 */
	endstring = myhost + strlen(myhost);
	firstback = strnrchr(myhost, '.', 1);
	secondback = strnrchr(myhost, '.', 2);
	thirdback = strnrchr(myhost, '.', 3);
	fourthback = strnrchr(myhost, '.', 4);

	/* Track foo@bar or some such thing. */
	if (!firstback)
	{
		*host = myhost;
		return 0;
	}

	/*
	 * IP address (A.B.C.D)
	 */
	if (my_atol(firstback + 1))
	{
		*ip = 1;
		*domain = myhost;

		number = my_atol(myhost);
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
		*host = myhost;
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
		*host = myhost;
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
		*domain = myhost;
	}
	/*
	 *	(*).(*.???.??)
	 *			Handles host.domain.com.au
	 */
	else if (thirdback && 
			(endstring - firstback == 3) &&
			(firstback - secondback == 4))
	{
		*host = myhost;
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
		*domain = myhost;
	}
	/*
	 *	(*).(*.??.??)
	 *			Handles host.domain.co.uk
	 */
	else if (thirdback && 
			(endstring - firstback == 3) &&
			(firstback - secondback == 3))
	{
		*host = myhost;
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
		*domain = myhost;
	}
	/*
	 *	(*).(*.??)
	 *			Handles domain.de
	 */
	else if (secondback && (endstring - firstback == 3))
	{
		*host = myhost;
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
		*domain = myhost;
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
void	mask_digits (char **hostname)
{
	char	*src_ptr;
	char 	*retval, *retval_ptr;

	retval = retval_ptr = alloca(strlen(*hostname) + 1);
	src_ptr = *hostname;

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
	strcpy(*hostname, retval);
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
 * 'incoming' needs to be at _least_ thrice as big as neccesary 
 * (ie, sizeof(incoming) >= strlen(incoming) * 3 + 1)
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
		/* strip_ansi can expand up to three times */
		char *output;

		strip_ansi_never_xlate = 1;	/* XXXXX */
		output = strip_ansi(incoming);
		strip_ansi_never_xlate = 0;	/* XXXXX */
		if (strlcpy(incoming, output, how_much) > how_much)
			say("Mangle_line truncating results. #1 -- "
				"Email jnelson@acronet.net [%d] [%d]",
				strlen(buffer), how_much);
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
			default:
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
static	const	long	RAND_A = 16807L;
static	const	long	RAND_M = 2147483647L;
static	const	long	RAND_Q = 127773L;
static	const	long	RAND_R = 2836L;
static		u_long	z = 0;
		long	t;

	if (z == 0)
		z = (u_long) getuid();

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
			z = (u_long) getuid();
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
	struct timeval 	tp1;
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


unsigned long	random_number (unsigned long l)
{
	switch (get_int_var(RANDOM_SOURCE_VAR))
	{
		case 0:
		default:
			return randd(l);
		case 1:
			return randm(l);
		case 2:
			return randt(l);
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

char *	urldecode (char *s)
{
	const char *p1;
	char *	p2;
	size_t	len;
	char *	retval;
	int	val1;
	int	val2;

	if (!s || !*s)
		return NULL;

	len = strlen(s);
	retval = alloca(len + 1);

	for (p1 = s, p2 = retval; *p1; p1++, p2++)
	{
		if (*p1 == '%' &&
		    ((p1[1] && ((val1 = XTOI(p1[1])) != -1)) &&
		     (p1[2] && ((val2 = XTOI(p1[2])) != -1))))
		{
			p1++, p1++;
			*p2 = (val1 << 4) | val2;
		}
		else
			*p2 = *p1;
	}

	*p2 = 0;
	return strcpy(s, retval);
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

const char *	my_strerror (int number)
{
	if (number < 0)
	{
#ifdef HAVE_HSTRERROR
		return hstrerror(h_errno);
#else
		return "Hostname lookup failure";
#endif
	}
	return strerror(errno);
}

