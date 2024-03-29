#include "config.h"
#ifdef NEED_GLOB

/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by 
 * Guido van Rossum.
 *
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
 */

/* from: static char sccsid[] = "@(#)glob.c	8.3 (Berkeley) 10/13/93"; */

/*
 * glob(3) -- a superset of the one defined in POSIX 1003.2.
 *
 * The [!...] convention to negate a range is supported (SysV, Posix, ksh).
 *
 * Optional extra services, controlled by flags not defined by POSIX:
 *
 * GLOB_QUOTE:
 *	Escaping convention: \ inhibits any special meaning the following
 *	character might have (except \ at end of string is retained).
 * GLOB_MAGCHAR:
 *	Set in gl_flags if pattern contained a globbing character.
 * GLOB_NOMAGIC:
 *	Same as GLOB_NOCHECK, but it will only append pattern if it did
 *	not contain any magic characters.  [Used in csh style globbing]
 * GLOB_ALTDIRFUNC:
 *	Use alternately specified directory access functions.
 * GLOB_TILDE: (not available)
 *	expand ~user/foo to the /home/dir/of/user/foo
 * GLOB_BRACE:
 *	expand {1,2}{a,b} to 1a 1b 2a 2b 
 * GLOB_INSENSITIVE:
 *	Globbing occurs without regard to case of the letters.
 * gl_matchc:
 *	Number of matches in the current invocation of glob.
 */

#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#include <sys/stat.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include "glob.h"
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "irc.h"
#include "compat.h"
#ifndef PATH_MAX
# ifndef MAXPATHLEN
#  ifndef PATHSIZE
#   define PATH_MAX 1024
#  else
#   define PATH_MAX PATHSIZE
#  endif
# else
#  define PATH_MAX MAXPATHLEN
# endif
#endif

#undef EOS
#define	DOLLAR		'$'
#define	DOT		'.'
#define	EOS		'\0'
#define	LBRACKET	'['
#define	NOT		'!'
#define	QUESTION	'?'
#define	QUOTE		'\\'
#define	RANGE		'-'
#define	RBRACKET	']'
#define	SEP		'/'
#define	STAR		'*'
#if 0
#define	TILDE		'~'
#endif
#define	UNDERSCORE	'_'
#define	LBRACE		'{'
#define	RBRACE		'}'
#define	SLASH		'/'
#define	COMMA		','

#define	M_QUOTE		0x8000
#define	M_PROTECT	0x4000
#define M_ANYCASE	0x2000		/* EPIC ADD */
#define	M_MASK		0xffff
#define	M_ASCII		0x00ff

#ifdef Char
#undef Char
#endif
typedef unsigned short Char;

#define	CHAR(c)		((Char)((c)&M_ASCII))
#define	META(c)		((Char)((c)|M_QUOTE))
#define	M_ALL		META('*')
#define	M_END		META(']')
#define	M_NOT		META('!')
#define	M_ONE		META('?')
#define	M_RNG		META('-')
#define	M_SET		META('[')
#define	ismeta(c)	(((c)&M_QUOTE) != 0)


static int	 	compare (const void *, const void *);
static void	 	g_Ctoc (const Char *, char *);
static int	 	g_lstat (Char *, Stat *, glob_t *);
static DIR	*	g_opendir (Char *, glob_t *);
static ssize_t		g_strchr (const Char *, int);
static int	 	g_stat (Char *, Stat *, glob_t *);
static int	 	glob0 (const Char *, glob_t *);
static int	 	glob1 (Char *, glob_t *);
static int	 	glob2 (Char *, Char *, Char *, glob_t *);
static int	 	glob3 (Char *, Char *, Char *, Char *, glob_t *);
static int	 	globextend (const Char *, glob_t *);
static const Char *	globtilde (const Char *, Char *, glob_t *);
static int	 	globexp1 (const Char *, glob_t *);
static int	 	globexp2 (const Char *, const Char *, glob_t *, int *);
static int	 	globmatch (Char *, Char *, Char *, int);

int bsd_glob		(	const char *pattern,
				int flags, 
				int (*errfunc) (const char *, int),
				glob_t *pglob				)
{
	const unsigned char *patnext;
	int c;
	Char *bufnext, *bufend, patbuf[PATH_MAX+1];

	patnext = (const unsigned char *) pattern;
	if (!(flags & GLOB_APPEND)) 
	{
		pglob->gl_pathc = 0;
		pglob->gl_pathv = NULL;
		if (!(flags & GLOB_DOOFFS))
			pglob->gl_offs = 0;
	}
	pglob->gl_flags = flags & ~GLOB_MAGCHAR;
	pglob->gl_errfunc = errfunc;
	pglob->gl_matchc = 0;

	bufnext = patbuf;
	bufend = bufnext + PATH_MAX;
	if (flags & GLOB_QUOTE) 
	{
		/* Protect the quoted characters. */
		while (bufnext < bufend && (c = *patnext++) != EOS) 
		{
			if (c == QUOTE) 
			{
				if ((c = *patnext++) == EOS) 
				{
					c = QUOTE;
					--patnext;
				}
				*bufnext++ = c | M_PROTECT;
			}
			else
				*bufnext++ = c;
		}
	}
	else
		while (bufnext < bufend && (c = *patnext++) != EOS) 
			*bufnext++ = c;

	*bufnext = EOS;

	if (flags & GLOB_BRACE)
	    return globexp1(patbuf, pglob);
	else
	    return glob0(patbuf, pglob);
}

/*
 * Expand recursively a glob {} pattern. When there is no more expansion
 * invoke the standard globbing routine to glob the rest of the magic
 * characters
 */
static int globexp1	(	const Char *pattern,
				glob_t *pglob			)
{
	const Char* ptr = pattern;
	int rv;
	ssize_t	span;

	/* Protect a single {}, for find(1), like csh */
	if (pattern[0] == LBRACE && pattern[1] == RBRACE && pattern[2] == EOS)
		return glob0(pattern, pglob);

	while ((span = g_strchr(ptr, LBRACE)) >= 0)
	{
		ptr += span;
		if (!globexp2(ptr, pattern, pglob, &rv))
			return rv;
	}

	return glob0(pattern, pglob);
}


/*
 * Recursive brace globbing helper. Tries to expand a single brace.
 * If it succeeds then it invokes globexp1 with the new pattern.
 * If it fails then it tries to glob the rest of the pattern and returns.
 */
static int globexp2	(	const Char *ptr,
				const Char *pattern,
				glob_t *pglob,
				int *rv				)
{
	int     i;
	Char   *lm, *ls;
	const Char *pe, *pm, *pl;
	Char    patbuf[PATH_MAX + 1];

	/* There are bugs in here that I can't get to the bottom of */
	/* This just papers over a string overrun */
	memset(patbuf, 0, PATH_MAX + 1 * sizeof(Char));

	/* copy part up to the brace */
	for (lm = patbuf, pm = pattern; pm != ptr; *lm++ = *pm++)
		continue;
	*lm = EOS;
	ls = lm;

	/* Find the balanced brace */
	for (i = 0, pe = ++ptr; *pe != EOS; pe++)
	{
		if (*pe == LBRACKET) 
		{
			/* Ignore everything between [] */
			for (pm = pe++; *pe != RBRACKET && *pe != EOS; pe++)
				continue;
			if (*pe == EOS) 
			{
				/* 
				 * We could not find a matching RBRACKET.
				 * Ignore and just look for RBRACE
				 */
				pe = pm;
			}
		}
		else if (*pe == LBRACE)
			i++;
		else if (*pe == RBRACE) 
		{
			if (i == 0)
				break;
			i--;
		}
	}

	/* Non matching braces; just glob the pattern */
	if (i != 0 || *pe == EOS) 
	{
		*rv = glob0(patbuf, pglob);
		return 0;
	}

	for (i = 0, pl = pm = ptr; pm <= pe; pm++)
	{
		switch (*pm) 
		{
		case LBRACKET:
			/* Ignore everything between [] */
			for (pl = pm++; *pm != RBRACKET && *pm != EOS; pm++)
				continue;
			if (*pm == EOS) 
			{
				/* 
				 * We could not find a matching RBRACKET.
				 * Ignore and just look for RBRACE
				 */
				pm = pl;
			}
			break;

		case LBRACE:
			i++;
			break;

		case RBRACE:
			if (i) 
			{
			    i--;
			    break;
			}
			/* FALLTHROUGH */
		case COMMA:
			if (i && *pm == COMMA)
				break;
			else 
			{
				/* Append the current string */
				for (lm = ls; (pl < pm); *lm++ = *pl++)
					continue;
				/* 
				 * Append the rest of the pattern after the
				 * closing brace
				 */
				for (pl = pe + 1; (*lm++ = *pl++) != EOS;)
					continue;

				/* Expand the current pattern */
				*rv = globexp1(patbuf, pglob);

				/* move after the comma, to the next string */
				pl = pm + 1;
			}
			break;

		default:
			break;
		}
	}
	*rv = 0;
	return 0;
}



/*
 * expand tilde from the passwd file.
 */
static const Char *globtilde	(	const Char *pattern,
					Char *patbuf,
					glob_t *pglob		)
{
#if 1
	return pattern;
#else
	struct passwd *pwd;
	char *h;
	const Char *p;
	Char *b;

	if (*pattern != TILDE || !(pglob->gl_flags & GLOB_TILDE))
		return pattern;

	/* Copy up to the end of the string or / */
	for (p = pattern + 1, h = (char *) patbuf; *p && *p != SLASH; 
	     *h++ = *p++)
		continue;

	*h = EOS;

	if (((char *) patbuf)[0] == EOS) 
	{
		/* 
		 * handle a plain ~ or ~/ by expanding $HOME 
		 * first and then trying the password file
		 */
		if ((h = getenv("HOME")) == NULL) {
			if ((pwd = getpwuid(getuid())) == NULL)
				return pattern;
			else
				h = pwd->pw_dir;
		}
	}
	else {
		/*
		 * Expand a ~user
		 */
		if ((pwd = getpwnam((char *)patbuf)) == NULL)
			return pattern;
		else
			h = pwd->pw_dir;
	}

	/* Copy the home directory */
	for (b = patbuf; *h; *b++ = *h++)
		continue;
	
	/* Append the rest of the pattern */
	while ((*b++ = *p++) != EOS)
		continue;

	return patbuf;
#endif
}
	

/*
 * The main glob() routine: compiles the pattern (optionally processing
 * quotes), calls glob1() to do the real pattern matching, and finally
 * sorts the list (unless unsorted operation is requested).  Returns 0
 * if things went well, nonzero if errors occurred.  It is not an error
 * to find no matches.
 */
static int glob0		(	const Char *pattern,
					glob_t *pglob		)
{
	const Char *	qpatnext;
	int 		c;
	int		err;
	int		oldpathc;
	Char *		bufnext;
	Char		patbuf[PATH_MAX+1];

	qpatnext = globtilde(pattern, patbuf, pglob);
	if (qpatnext == NULL) 
	{
		errno = E2BIG;
		return (GLOB_NOSPACE);
	}
	oldpathc = pglob->gl_pathc;
	bufnext = patbuf;

	/* We don't need to check for buffer overflow any more. */
	while ((c = *qpatnext++) != EOS) 
	{
		switch (c) 
		{
		case LBRACKET:
			c = *qpatnext;
			if (c == NOT)
				++qpatnext;
			if (*qpatnext == EOS || 
			    g_strchr(qpatnext+1, RBRACKET) < 0)
			{
				*bufnext++ = LBRACKET;
				if (c == NOT)
					--qpatnext;
				break;
			}
			*bufnext++ = M_SET;
			if (c == NOT)
				*bufnext++ = M_NOT;
			c = *qpatnext++;
			do 
			{
				*bufnext++ = CHAR(c);
				if (*qpatnext == RANGE && 
				    (c = qpatnext[1]) != RBRACKET) 
				{
					*bufnext++ = M_RNG;
					*bufnext++ = CHAR(c);
					qpatnext += 2;
				}
			} 
			while ((c = *qpatnext++), (c && c != RBRACKET));

			pglob->gl_flags |= GLOB_MAGCHAR;
			*bufnext++ = M_END;
			break;
		case QUESTION:
			pglob->gl_flags |= GLOB_MAGCHAR;
			*bufnext++ = M_ONE;
			break;
		case STAR:
			pglob->gl_flags |= GLOB_MAGCHAR;
			/* 
			 * collapse adjacent stars to one, 
			 * to avoid exponential behavior
			 */
			if (bufnext == patbuf || bufnext[-1] != M_ALL)
			    *bufnext++ = M_ALL;
			break;
		default:
			*bufnext++ = CHAR(c);
			break;
		}
	}
	*bufnext = EOS;

	if ((err = glob1(patbuf, pglob)) != 0)
		return(err);

	/*
	 * If there was no match we are going to append the pattern 
	 * if GLOB_NOCHECK was specified or if GLOB_NOMAGIC was specified
	 * and the pattern did not contain any magic characters
	 * GLOB_NOMAGIC is there just for compatibility with csh.
	 */
	if (pglob->gl_pathc == oldpathc && 
	    ((pglob->gl_flags & GLOB_NOCHECK) || 
	      ((pglob->gl_flags & GLOB_NOMAGIC) &&
	       !(pglob->gl_flags & GLOB_MAGCHAR))))
		return(globextend(pattern, pglob));
	else if (!(pglob->gl_flags & GLOB_NOSORT)) 
	{
	    if (pglob->gl_pathv)
		qsort(pglob->gl_pathv + pglob->gl_offs + oldpathc,
		    pglob->gl_pathc - oldpathc, sizeof(char *), compare);
	}
	return(0);
}

static int compare		(	const void *p,
					const void *q		)
{
	return (strcmp(*(const char * const *)p, *(const char * const *)q));
}

static int glob1		(	Char *pattern,
					glob_t *pglob		)
{
	Char pathbuf[PATH_MAX+1];

	/* A null pathname is invalid -- POSIX 1003.1 sect. 2.4. */
	if (*pattern == EOS)
		return(0);
	return(glob2(pathbuf, pathbuf, pattern, pglob));
}

/*
 * The functions glob2 and glob3 are mutually recursive; there is one level
 * of recursion for each segment in the pattern that contains one or more
 * meta characters.
 */
static int glob2		(	Char *pathbuf,
					Char *pathend,
					Char *pattern,
					glob_t *pglob		)
{
	Stat sb;
	Char *p, *q;
	int anymeta;

	/*
	 * Loop over pattern segments until end of pattern or until
	 * segment with meta character found.
	 */
	for (anymeta = 0;;) 
	{
		if (*pattern == EOS)		/* End of pattern? */
		{
			*pathend = EOS;
			if (g_lstat(pathbuf, &sb, pglob))
				return(0);
		
			if (((pglob->gl_flags & GLOB_MARK) &&
			    pathend[-1] != SEP) && (S_ISDIR(sb.st_mode)
#ifdef S_ISLNK /* bummer for unixware */
			    || (S_ISLNK(sb.st_mode) &&
			    (g_stat(pathbuf, &sb, pglob) == 0) &&
			    S_ISDIR(sb.st_mode))
#endif
						))
			{
				*pathend++ = SEP;
				*pathend = EOS;
			}
			++pglob->gl_matchc;
			return(globextend(pathbuf, pglob));
		}

		/* Find end of next segment, copy tentatively to pathend. */
		q = pathend;
		p = pattern;
		while (*p != EOS && *p != SEP) 
		{
			if (ismeta(*p))
				anymeta = 1;
			*q++ = *p++;
		}

		if (!anymeta)		/* No expansion, do next segment. */
		{
			pathend = q;
			pattern = p;
			while (*pattern == SEP)
				*pathend++ = *pattern++;
		} else			/* Need expansion, recurse. */
			return(glob3(pathbuf, pathend, pattern, p, pglob));
	}
	/* NOTREACHED */
}

static int glob3		(	Char *pathbuf,
					Char *pathend,
					Char *pattern,
					Char *restpattern,
					glob_t *pglob			)
{
	register struct dirent *dp;
	DIR *dirp;
	int err;
	char buf[PATH_MAX];

	/*
	 * The readdirfunc declaration can't be prototyped, because it is
	 * assigned, below, to two functions which are prototyped in glob.h
	 * and dirent.h as taking pointers to differently typed opaque
	 * structures.
	 */
	struct dirent *(*readdirfunc)(DIR *);

	*pathend = EOS;
	errno = 0;

	if ((dirp = g_opendir(pathbuf, pglob)) == NULL)
	{
		/* TODO: don't call for ENOENT or ENOTDIR? */
		if (pglob->gl_errfunc) 
		{
			g_Ctoc(pathbuf, buf);
			if (pglob->gl_errfunc(buf, errno) ||
			    pglob->gl_flags & GLOB_ERR)
				return (GLOB_ABEND);
		}
		return(0);
	}

	err = 0;

	/* Search directory for matching names. */
	if (pglob->gl_flags & GLOB_ALTDIRFUNC)
		readdirfunc = (struct dirent *(*)(DIR *))pglob->gl_readdir;
	else
		readdirfunc = readdir;

	while ((dp = (*readdirfunc)(dirp))) 
	{
		register unsigned char *sc;
		register Char *dc;
		int	nocase = 0;

		/* Initial DOT must be matched literally. */
		if (dp->d_name[0] == DOT && *pattern != DOT)
			continue;
		for (sc = (unsigned char *) dp->d_name, dc = pathend; 
		     (*dc++ = *sc++) != EOS;)
			continue;


		if (pglob->gl_flags & GLOB_INSENSITIVE)
			nocase = 1;

		if (!globmatch(pathend, pattern, restpattern, nocase)) 
		{
			*pathend = EOS;
			continue;
		}
		err = glob2(pathbuf, --dc, restpattern, pglob);
		if (err)
			break;
	}

	if (pglob->gl_flags & GLOB_ALTDIRFUNC)
		(*pglob->gl_closedir)(dirp);
	else
		closedir(dirp);
	return(err);
}


/*
 * Extend the gl_pathv member of a glob_t structure to accomodate a new item,
 * add the new item, and update gl_pathc.
 *
 * This assumes the BSD realloc, which only copies the block when its size
 * crosses a power-of-two boundary; for v7 realloc, this would cause quadratic
 * behavior.
 *
 * Return 0 if new item added, error code if memory couldn't be allocated.
 *
 * Invariant of the glob_t structure:
 *	Either gl_pathc is zero and gl_pathv is NULL; or gl_pathc > 0 and
 *	gl_pathv points to (gl_offs + gl_pathc + 1) items.
 */
static int globextend		(	const Char *path,
					glob_t *pglob			)
{
	register char **pathv;
	register int i;
	unsigned newsize;
	char *copy;
	const Char *p;

	newsize = sizeof(*pathv) * (2 + pglob->gl_pathc + pglob->gl_offs);
	pathv = pglob->gl_pathv ? 
		    (char **)realloc((char *)pglob->gl_pathv, newsize) :
		    (char **)malloc(newsize);
	if (pathv == NULL)
		return(GLOB_NOSPACE);

	if (pglob->gl_pathv == NULL && pglob->gl_offs > 0) 
	{
		/* first time around -- clear initial gl_offs items */
		pathv += pglob->gl_offs;
		for (i = pglob->gl_offs; --i >= 0; )
			*--pathv = NULL;
	}
	pglob->gl_pathv = pathv;

	for (p = path; *p++;)
		continue;
	if ((copy = malloc(p - path)) != NULL) 
	{
		g_Ctoc(path, copy);
		pathv[pglob->gl_offs + pglob->gl_pathc++] = copy;
	}
	pathv[pglob->gl_offs + pglob->gl_pathc] = NULL;
	return(copy == NULL ? GLOB_NOSPACE : 0);
}


/*
 * pattern matching function for filenames.  Each occurrence of the *
 * pattern causes a recursion level.
 */
static int globmatch		(	register Char *name,
					register Char *pat,
					register Char *patend, int nocase )
{
	int ok, negate_range;
	Char c, k;

	while (pat < patend) 
	{
		c = *pat++;
		switch (c & M_MASK) 
		{
		case M_ALL:
			if (pat == patend)
				return(1);
			do 
			    if (globmatch(name, pat, patend, nocase))
				    return(1);
			while (*name++ != EOS);
			return(0);

		case M_ONE:
			if (*name++ == EOS)
				return(0);
			break;
		case M_SET:
			ok = 0;
			if ((k = *name++) == EOS)
				return(0);
			if ((negate_range = ((*pat & M_MASK) == M_NOT)) != EOS)
				++pat;
			while (((c = *pat++) & M_MASK) != M_END)
			{
				if ((*pat & M_MASK) == M_RNG) 
				{
					if (c <= k && k <= pat[1])
						ok = 1;
					pat += 2;
				} else if (c == k)
					ok = 1;
			}
			if (ok == negate_range)
				return(0);
			break;
		default:
		{
			if (nocase)
			{
				if (toupper((int)CHAR(*name)) != 
				    toupper((int)CHAR(c)))
					return 0;
			}
			else
			{
				if (*name != c)
					return 0;
			}
			name++;
			break;
		}
		}
	}

	if (*name == EOS)
		return 1;

	return 0;
}

/* Free allocated data belonging to a glob_t structure. */
void bsd_globfree 		(	glob_t *pglob			)
{
	register int i;
	register char **pp;

	if (pglob->gl_pathv != NULL) {
		pp = pglob->gl_pathv + pglob->gl_offs;
		for (i = pglob->gl_pathc; i--; ++pp)
			if (*pp)
				free(*pp);
		free(pglob->gl_pathv);
	}
}

static DIR *g_opendir		(	register Char *str,
					glob_t *pglob			)
{
	char buf[PATH_MAX];

	if (!*str)
		strlcpy(buf, ".", sizeof buf);
	else
		g_Ctoc(str, buf);

	if (pglob->gl_flags & GLOB_ALTDIRFUNC)
		return((*pglob->gl_opendir)(buf));

	return(opendir(buf));
}

static int g_lstat		(	register Char *fn,
					Stat *sb,
					glob_t *pglob			)
{
	char buf[PATH_MAX];

	g_Ctoc(fn, buf);
	if (pglob->gl_flags & GLOB_ALTDIRFUNC)
		return((*pglob->gl_lstat)(buf, sb));
	return(lstat(buf, sb));
}

static int g_stat		(	register Char *fn,
					Stat *sb,
					glob_t *pglob			)
{
	char buf[PATH_MAX];

	g_Ctoc(fn, buf);
	if (pglob->gl_flags & GLOB_ALTDIRFUNC)
		return((*pglob->gl_stat)(buf, sb));
	return(stat(buf, sb));
}

static ssize_t	g_strchr	(	const Char *str, 
					int ch				)
{
	const Char *start = str;
	do {
		if (*str == ch)
			return str - start;
	} while (*str++);
	return -1;
}

static void g_Ctoc		(	register const Char *str,
					char *buf			)
{
	register char *dc;

	for (dc = buf; (*dc++ = *str++) != EOS;)
		continue;
}
#endif
