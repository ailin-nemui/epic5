/*
 * @(#)$Id: acconfig.h,v 1.14 2002/07/26 17:10:07 jnelson Exp $
 */

/*
 * Functions
 */

/* Define this if you have fpathconf(2) */
#undef HAVE_FPATHCONF

/* define this if you have getpass(3) */
#undef HAVE_GETPASS

/* define this if you have getpgid(2) */
#undef HAVE_GETPGID

/* define this if you have gettimeofday(2) */
#undef HAVE_GETTIMEOFDAY

/* define this if you have glob(3) */
#undef HAVE_GLOB

/* Define this if you have killpg(2) */
#undef HAVE_KILLPG

/* define this if you have memmove(3) */
#undef HAVE_MEMMOVE

/* define this if you have scandir(3) */
#undef HAVE_SCANDIR

/* define this if you have setenv(3) */
#undef HAVE_SETENV

/* define this if you have unsetenv(3) */
#undef HAVE_UNSETENV

/* define this if you have setsid(2) */
#undef HAVE_SETSID

/* define this if you have snprintf(3) */
#undef HAVE_SNPRINTF

/* define this if you have strerror(3) */
#undef HAVE_STRERROR

/* define if you have strtoul(3) */
#undef HAVE_STRTOUL

/* define this if you have sysconf(3) */
#undef HAVE_SYSCONF

/* define this if you have uname(2) */
#undef HAVE_UNAME

/* define this if you have vsnprintf(3) */
#undef HAVE_VSNPRINTF


/*
 * Implicit Global variables
 */

/* define if you have sys_siglist */
#undef HAVE_SYS_SIGLIST


/*
 * Header files
 */

/* define this if you have fcntl.h */
#undef HAVE_FCNTL_H

/* define this if you have memory.h */
#undef HAVE_MEMORY_H

/* define this if you have netdb.h */
#undef HAVE_NETDB_H

/* define this if you have stdarg.h */
#undef HAVE_STDARG_H

/* define this if you have string.h */
#undef HAVE_STRING_H

/* define this if you have sys/fcntl.h */
#undef HAVE_SYS_FCNTL_H

/* define this if you have sys/file.h */
#undef HAVE_SYS_FILE_H

/* define this if you have sys/select.h */
#undef HAVE_SYS_SELECT_H

/* define this if you have sys/time.h */
#undef HAVE_SYS_TIME_H

/* define this if you have sys/wait.h */
#undef HAVE_SYS_WAIT_H

/*
 * Libraries
 */

/* define this if -lnls exists */
#undef HAVE_LIB_NLS

/* define this if -lnsl exists */
#undef HAVE_LIB_NSL

/* define his if -lPW exists */
#undef HAVE_LIB_PW

/* define this if you are using -ltermcap */
#undef USING_TERMCAP

/* define this if you are using -lxtermcap */
#undef USING_XTERMCAP

/* define this if you are using -ltermlib */
#undef USING_TERMLIB


/*
 * System, library, and header semantics
 */

/* Define this if you have SUN_LEN in <sys/un.h> */
#undef HAVE_SUN_LEN

/* define this if you don't have struct linger */
#undef NO_STRUCT_LINGER

/* define if allow sys/time.h with time.h */
#undef TIME_WITH_SYS_TIME

/* Define this if your getpgrp is broken posix */
#undef GETPGRP_VOID

/* define this if signal's return void */
#undef SIGVOID

/* define this if an unsigned long is 32 bits */
#undef UNSIGNED_LONG32

/* define this if an unsigned int is 32 bits */
#undef UNSIGNED_INT32

/* define this if you are unsure what is is 32 bits */
#undef UNKNOWN_32INT

/* define this if you are on a svr4 derivative */
#undef SVR4

/* define this to the location of normal unix mail */
#undef UNIX_MAIL

/* Define this if you have inet_aton(). */
#undef HAVE_INET_ATON

/* Define this if you need to include sys/select.h */
#undef NEED_SYS_SELECT_H

/*
 * SOCKS 4 && 5 support.
 */
#undef SOCKS
#undef USE_SOCKS
#undef USE_SOCKS5
#undef connect
#undef getsockname
#undef bind
#undef accept
#undef listen
#undef select
#undef dup
#undef dup2
#undef fclose
#undef gethostbyname
#undef read
#undef recv
#undef recvfrom
#undef rresvport
#undef send
#undef sendto
#undef shutdown
#undef write
#undef Rconnect
#undef Rgetsockname
#undef Rgetpeername
#undef Rbind
#undef Raccept
#undef Rlisten
#undef Rselect

/*
 *  Perl support.
 */
#undef PERL

/* Define this is DIRSIZ takes no argument */
#undef DIRSIZ_TAKES_NO_ARG

/* Define this if you have setsid() */
#undef HAVE_SETSID

/* Define this if you have a useful FIONREAD */
#undef HAVE_USEFUL_FIONREAD

/* Define this if you have tparm(2) */
#undef HAVE_TPARM

/* Define this if you have getlogin(3) */
#undef HAVE_GETLOGIN

/* Define this if you have terminfo support */
#undef HAVE_TERMINFO

/* Define this if you have fchdir() */
#undef HAVE_FCHDIR

/* Define this if you have realpath() */
#undef HAVE_REALPATH

/* Define this if you have strlcpy() */
#undef HAVE_STRLCPY

/* Define this if you have strlcat() */
#undef HAVE_STRLCAT

/* Define this if you have stpcpy() */
#undef HAVE_STPCPY

/* Define this if you have a function decl for stpcpy() */
#undef STPCPY_DECLARED

/* Define this if you have hstrerror, for h_errno use */
#undef HAVE_HSTRERROR

/* Define this if you want OPENSSL support */
#undef HAVE_SSL

/* Define this if you want OPENSSL2 support */
#undef WANT_SSL2

/* Define this if you have sysctlbyname() */
#undef HAVE_SYSCTLBYNAME

/* Define this if you have SO_SNDLOWAT */
#undef HAVE_SO_SNDLOWAT

/* Define this if you have struct sockaddr_storage */
#undef HAVE_STRUCT_SOCKADDR_STORAGE

/* Define this if your system has SA_LEN in its sockaddrs */
#undef HAVE_SA_LEN

/* Define this if you have inet_ntop() */
#undef HAVE_INET_NTOP

/* Define this if you have inet_pton() */
#undef HAVE_INET_PTON

/* Define this if you have gethostbyname2() */
#undef HAVE_GETHOSTBYNAME2

/* Define this if you don't have siglen_t */
#undef HAVE_SOCKLEN_T

/* Define this if you have a getaddrinfo() with missing functionality */
#undef GETADDRINFO_DOES_NOT_DO_AF_UNIX

/* Define this if you do not want INET6 support */
#undef DO_NOT_USE_IPV6

/* Define this if you have struct addrinfo */
#undef HAVE_STRUCT_ADDRINFO

/* Define this if you have struct sockaddr_in6 */
#undef HAVE_STRUCT_SOCKADDR_IN6

