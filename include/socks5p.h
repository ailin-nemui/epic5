/*
 * socks5p.h -- Compatability shim for <socks.h>, public domain
 * 
 * If you #define INCLUDE_PROTOTYPES, then socks5 will try to
 * #include "socks5p.h" which isn't installed normally.  But on amd64,
 * we *must* have prototypes to get functions like localtime() correct.
 * So this file provides the limited set of things necessary to make
 * #define INCLUDE_PROTOTYPES work with <socks.h>
 */
#ifndef __socks5p_h__
#define __socks5p_h__

#include <setjmp.h>

#ifndef P
#define P(x) x
#endif

#ifndef LIBPREFIX
#ifdef USE_SOCKS4_PREFIX
#define LIBPREFIX(x)  R ## x
#else
#define LIBPREFIX(x)  SOCKS ## x
#endif
#endif

#ifndef IORETTYPE 
#define IORETTYPE int
#endif

#ifndef IOPTRTYPE 
#define IOPTRTYPE void *
#endif

#ifndef IOLENTYPE 
#define IOLENTYPE size_t
#endif

#endif /* __socks5p_h__ */
