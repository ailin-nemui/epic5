/*
 * Collected by EPIC Software Labs
 * Public Domain
 */

#ifndef __COMPAT_H__
#define __COMPAT_H__

#ifndef HAVE_TPARM
char *tparm (const char *, ...);
#endif

#ifndef HAVE_STRTOUL
unsigned long strtoul (const char *, char **, int);
#endif

#ifndef HAVE_STRLCPY
size_t	strlcpy (char *, const char *, size_t);
#endif

#ifndef HAVE_STRLCAT
size_t	strlcat (char *, const char *, size_t);
#endif

void	bsd_arc4random_stir (void);
void	bsd_arc4random_addrandom (unsigned char *, int);
u_32int_t	bsd_arc4random (void);

#ifndef HAVE_VSNPRINTF
int	vsnprintf (char *, size_t, const char *, va_list);
#endif

#ifndef HAVE_SNPRINTF
int	snprintf (char *, size_t, const char *, ...);
#endif

#ifndef HAVE_SETSID
int	setsid (void);
#endif

#ifndef HAVE_SETENV
int	setenv (const char *, const char *, int);
#endif

#ifndef HAVE_UNSETENV
int	unsetenv (const char *);
#endif

#ifdef HAVE_BROKEN_REALPATH
char *	my_realpath (const char *, char x[MAXPATHLEN]);
#endif

#endif
