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

#ifndef HAVE_ARC4RANDOM
u_32int_t	bsd_arc4random (void);
#define arc4random bsd_arc4random
#endif

int	my_base64_encode (const void *, int, char **);
int	my_base64_decode (const char *, void *);

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

#ifndef HAVE_STRTOIMAX
long	strtoimax (const char *, char **, int);
#endif

#if 0
/* humanize_number(3) */ 
#define HN_DECIMAL              0x01
#define HN_NOSPACE              0x02
#define HN_B                    0x04
#define HN_DIVISOR_1000         0x08

#define HN_GETSCALE             0x10
#define HN_AUTOSCALE            0x20
int	humanize_number (char *, size_t, long, const char *, int, int);
#endif

#endif
