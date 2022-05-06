/*
 * irc_std.h: This is where we make up for operating system lossage
 * Originally written by Matthew Green, Copyright 1993
 * Various modifications by various people since then.
 *
 * See the copyright file, or do a help ircii copyright 
 */

#ifndef __irc_std_h
#define __irc_std_h

#include "defs.h"

/*
 * Everybody needs these ANSI headers...
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdarg.h>
#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif
#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#endif
#ifdef HAVE_STDDEF_H
#include <stddef.h>
#endif
#include <setjmp.h>

/*
 * Everybody needs these POSIX headers...
 */
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <limits.h>
#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#include <errno.h>
#include <sys/stat.h>
/* I used to #include <locale.h>, but centos 5.11 required special handling */

/*
 * Everybody needs these INET headers...
 */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#ifndef AI_ADDRCONFIG
# define AI_ADDRCONFIG 0
#endif
#ifdef USE_SOCKS5
# define INCLUDE_PROTOTYPES
# include <socks.h>
#endif
#ifdef HAVE_SYS_UN_H
#include <sys/un.h>
#endif

/*
 * Some systems define tputs, etc in this header
 */
#ifdef HAVE_TERMCAP_H
# include <termcap.h>
#else
# if defined(__need_term_h__)
#  if defined(HAVE_TERM_H) && !defined(DONT_USE_TERM_H)
#   if defined(TERM_H_REQUIRES_CURSES_H)
#    include <termio.h>
#    include <curses.h>
#   endif
#   include <term.h>
#  endif
# endif
#endif

#ifdef HAVE_ICONV
# include <iconv.h>
#endif

/*
 * Deal with brokenness in <time.h> and <sys/time.h>
 */
#ifdef TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# ifdef HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

/*
 * Deal with brokenness in <fcntl.h> and <sys/fcntl.h>
 */
#ifdef HAVE_SYS_FCNTL_H
# include <sys/fcntl.h>
#else
# ifdef HAVE_FCNTL_H
#  include <fcntl.h>
# endif
#endif

/*
 * Deal with brokenness figuring out struct direct
 */
#if HAVE_DIRENT_H
# include <dirent.h>
# define NAMLEN(dirent) strlen((dirent)->d_name)
#else
# define dirent direct
# define NAMLEN(dirent) (dirent)->d_namlen
# if HAVE_SYS_NDIR_H
#  include <sys/ndir.h>
# endif
# if HAVE_SYS_DIR_H
#  include <sys/dir.h>
# endif
# if HAVE_NDIR_H
#  include <ndir.h>
# endif
#endif




/*
 * First try to figure out if we can use GNU CC special features...
 */
#ifndef __GNUC__
# define __inline__		/* delete gcc keyword */
# define __inline
# define __A(x)
# define __N
#else
# if ((__GNUC__ > 2) || (__GNUC__ >= 2) && (__GNUC_MINOR__ >= 7))
#  define __A(x) __attribute__ ((format (printf, x, x + 1)))
#  define __N    __attribute__ ((noreturn))
# else
#  define __A(x)
#  define __N
# endif
#endif

#ifdef HAVE_ATTRIBUTE_MAY_ALIAS
#define MAY_ALIAS __attribute__((may_alias))
#else
#define MAY_ALIAS
#endif

/*
 * Figure out how to make alloca work
 * I took this from the autoconf documentation
 */
#if defined(__GNUC__) && !defined(HAVE_ALLOCA_H)
# ifndef alloca
#  define alloca __builtin_alloca
# endif
#else
# if HAVE_ALLOCA_H
#  include <alloca.h>
# else
#  ifdef _AIX
 #pragma alloca
#  else
#   ifndef alloca
char *alloca();
#   endif
#  endif
# endif
#endif

/*
 * Define the MIN and MAX macros if they don't already exist.
 */
#ifndef MIN
# define MIN(a,b) (((a)<(b))?(a):(b))
#endif
#ifndef MAX
# define MAX(a,b) (((a)>(b))?(a):(b))
#endif


/*
 * Deal with brokenness with sys_errlist.
 */
#ifndef HAVE_STRERROR
# ifndef SYS_ERRLIST_DECLARED
extern	char	*sys_errlist[];
# endif
#define strerror(x) sys_errlist[x]
#endif

/*
 * Deal with brokenness with realpath.
 */
#ifdef HAVE_BROKEN_REALPATH
# define realpath my_realpath
#endif

/*
 * Dont trust anyone else's NULLs.
 */
#ifdef NULL
#undef NULL
#endif
#define NULL (void *) 0

/*
 * Make sure there is TRUE and FALSE
 */
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

/*
 * Can you believe some systems done #define this?
 * I was told that hurd doesn't, so this helps us on hurd.
 */
#ifndef PATH_MAX
# ifndef MAXPATHLEN
#  ifndef PATHSIZE
#   define PATH_MAX 1024
#  else
#   define PATH_MAX PATHSIZE
#  endif
# else
#   define PATH_MAX MAXPATHLEN
# endif
#endif

/*
 * NSIG is a pain in my [censored]
 */
#ifndef NSIG
# ifdef _NSIG
#  define NSIG _NSIG
# else
#  define NSIG 32
# endif
#endif

/*
 * Define generic macros for signal handlers and built in commands.
 */
typedef void sigfunc (int);
int	block_signal (int);
int	unblock_signal (int);
sigfunc *my_signal (int, sigfunc *);
#define SIGNAL_HANDLER(x) void x (int unused)
sigfunc *init_signals (void);
void	init_signal_names (void);
const char *get_signal_name (int);
int	get_signal_by_name (const char *);	/* Returns -1 on error */
extern	volatile int    signals_caught[NSIG];

#define BUILT_IN_COMMAND(x) \
	void x (const char *command, char *args, const char *subargs)

#define BUILT_IN_KEYBINDING(x) void x (unsigned int key, unsigned char *string)

typedef char Filename[PATH_MAX + 1];

/*
 * It's really really important that you never use LOCAL_COPY in the actual
 * argument list of a function call, because bad things can happen.  Always
 * do your LOCAL_COPY as a separate step before you call a function.
 */
#define LOCAL_COPY(y) strcpy(alloca(strlen((y)) + 1), y)
#define SAFE(x) (((x) && *(x)) ? (x) : empty_string)

/*
 * Deal with our brokenness wrt ANSI.  Sigh.
 */
#ifndef HAVE_MEMMOVE
#define memmove(x, y, z) bcopy(y, x, z)
#endif

/*
 * Interix's getpgrp() does not take an argument, but the configure script
 * detects it wrongly.
 */
#ifdef __INTERIX
# define GETPGRP_VOID
#endif

/*
 * Interix provides intptr_t, but the shipped gcc 3.3 provides a broken
 * stddef.h that hides it from us.
 */
#ifdef __INTERIX
typedef int intptr_t;
#endif

/*
 * Figure out our intmax_t
 */
#ifdef PRIdMAX
# define INTMAX_FORMAT "%" PRIdMAX
# define UINTMAX_FORMAT "%" PRIuMAX
#else
# define INTMAX_FORMAT "%jd"
# define UINTMAX_FORMAT "%ju"
#endif

/*
 * DCC specification requires exactly a 32 bit checksum.
 * Kind of lame, actually.
 */
typedef		uint32_t		u_32int_t;

#ifdef Char
#undef Char
#endif
#define Char const char

#if defined(HAVE_SYS_SELECT_H) 
#include <sys/select.h>
#endif

/*
 * Now we deal with lame systems that dont have correct select()
 * support (like aix 3.2.5, and older linux systems.)
 */
#ifndef NBBY
# define NBBY	8		/* number of bits in a byte */
#endif /* NBBY */

#ifndef NFDBITS
# define NFDBITS	(sizeof(long) * NBBY)	/* bits per mask */
#endif /* NFDBITS */

#ifndef FD_SETSIZE
#define FD_SETSIZE      256
#endif

#ifndef howmany
#define howmany(x, y)   (((x) + ((y) - 1)) / (y))
#endif

#if defined(HAVE_SYS_SYSCTL_H)
#include <sys/sysctl.h>
#endif

/*
 * Define an RFC2553 compatable "struct sockaddr_storage" if we do not
 * already have one.
 */
#ifndef HAVE_STRUCT_SOCKADDR_STORAGE
struct sockaddr_storage {
#ifdef HAVE_SA_LEN
	unsigned char ss_len;
	unsigned char ss_family;
#else
	unsigned short ss_family;
#endif
	unsigned char padding[128 - 2];
};
#endif

#ifndef HAVE_SOCKLEN_T
typedef int socklen_t;
#endif

/*
 * C99 requires that type-punned aliases must be accessed through a union.
 * So, that's why we do this.  Always write to 'ss' and then read back
 * through whatever type you need.
 */
typedef union SSu {
	struct sockaddr 	sa;
	struct sockaddr_storage	ss;
	struct sockaddr_in	si;
	struct sockaddr_in6	si6;
#ifdef HAVE_SYS_UN_H
	struct sockaddr_un	su;
#endif
} SSu;

typedef struct addrinfo		AI;

#ifndef __no_timeval_stuff__
typedef struct timeval		Timeval;
#endif
typedef struct stat		Stat;

/*
 * Interix's getpgrp() does not take an argument, but the configure script
 * detects it wrongly.
 */
#ifdef __INTERIX
# define GETPGRP_VOID
#endif

/*
 * See if we are supposed to give valgrind a hand in memory leak checking
 */
#ifdef HAVE_VALGRIND_MEMCHECK_H
#include <valgrind/memcheck.h>
#else
#define VALGRIND_CREATE_MEMPOOL(x,y,z)
#define VALGRIND_MEMPOOL_ALLOC(x,y,z)
#define VALGRIND_MEMPOOL_TRIM(x,y,z)
#define VALGRIND_MEMPOOL_FREE(x,y)
#define VALGRIND_DESTROY_MEMPOOL(x)
#endif

/*
 * Ah, the joys of newer gcc's....
 */
#define CUC_PP (const unsigned char **)
#define CC_PP (const char **)

#ifdef NEWLOCALE_DOESNT_WORK
#undef HAVE_NEWLOCALE
#endif

#ifdef HAVE_ATTRIBUTE_FALLTHROUGH
#define FALLTHROUGH __attribute__((fallthrough));
#else
#define FALLTHROUGH 
#endif


#ifndef NO_SSL
/* Everybody needs these OpenSSL headers */
#include <openssl/crypto.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/opensslconf.h>
#endif

#endif /* __irc_std_h */
