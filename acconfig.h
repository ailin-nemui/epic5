/*
 * @(#)$Id: acconfig.h,v 1.46 2014/12/26 15:26:41 jnelson Exp $
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
#undef HAVE_PERL

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

/* Define this if you have getservbyport() */
#undef HAVE_GETSERVBYPORT

/* Define this if you have getaddrinfo */
#undef HAVE_GETADDRINFO

/* Define this if you have getnameinfo */
#undef HAVE_GETNAMEINFO

/* Define this if you have sys_siglist declared */
#undef SYS_SIGLIST_DECLARED

/* Define this if you have a broken realpath */
#undef HAVE_BROKEN_REALPATH

/* Define this if you have TCL */
#undef HAVE_TCL

/* Define this if you have RUBY */
#undef HAVE_RUBY

/* Define this if you have clock_gettime() */
#undef HAVE_CLOCK_GETTIME

/* Define this if you have <intypes.h> */
#undef HAVE_INTTYPES_H

/* Define this if you have intptr_t */
#undef HAVE_INTPTR_T

/* Define this if you have a (long long) */
#undef HAVE_LONG_LONG

/* Define this if you have atoll() */
#undef HAVE_ATOLL

/* Define this if you have strtoll() */
#undef HAVE_STRTOLL

/* Define this if you have atoq() */
#undef HAVE_ATOQ

/* Define this to use select() */
#undef USE_SELECT

/* Define this to use poll() */
#undef USE_POLL

/* Define this to use kqueue() */
#undef USE_FREEBSD_KQUEUE

/* Define this to use pthreads */
#undef USE_PTHREAD

/* Define this if you have arc4random() */
#undef HAVE_ARC4RANDOM

/* Define this if you have solaris ports */
#undef USE_SOLARIS_PORTS

/* Define this if your largest int is (long) */
#undef HAVE_INTMAX_LONG

/* Define this if your largest int is (quad_t) */
#undef HAVE_INTMAX_QUADT

/* Define this if your largest int is (long long) */
#undef HAVE_INTMAX_LONG_LONG

/* Define this if your largest int is (intmax_t) */
#undef HAVE_INTMAX_NATIVE

/* Define this if you have a strtoimax() */
#undef HAVE_STRTOIMAX

/* Define this if you have a strtoq() */
#undef HAVE_STRTOQ

/* Define this if you have <stddef.h> */
#undef HAVE_STDDEF_H

/* Define this if you have ruby and stuff */
#undef RUBYCFLAGS

/* Define this if you have ruby and stuff */
#undef RUBYLIBS

/* Define this if you have tcl and stuff */
#undef TCLCFLAGS

/* Define this if you have tcl and stuff */
#undef TCLLIBS

/* Define this if you have perl and stuff */
#undef PERLCFLAGS

/* Define this if you have perl and stuff */
#undef PERLLIBS

/* Define this if you have finite() */
#undef HAVE_FINITE

/* Define this if you have isfinite() */
#undef HAVE_ISFINITE

/* Define this if you have math.h */
#undef HAVE_MATH_H

/* Define this if you want to use threaded stdout */
#undef WITH_THREADED_STDOUT

/* Define this if you have nanosleep() */
#undef HAVE_NANOSLEEP

/* Define this if you have <iconv.h> */
#undef HAVE_ICONV_H

/* Define this if you have iconv_open() */
#undef HAVE_ICONV

/* Define this if you have iconv_open() */
#undef HAVE_LIBARCHIVE

/* Define this if you have strptime() */
#undef HAVE_STRPTIME

/* Define this if your <term.h> requires <curses.h> (x/open curses) */
#undef TERM_H_REQUIRES_CURSES_H

/* Define this if your <term.h> can't be used */
#undef DONT_USE_TERM_H

/* Define this if you have tcsetpgrp() */
#undef HAVE_TCSETPGRP

/* Define this if you have no posix-like job control */
#undef NO_JOB_CONTROL

/* Define this if you want wserv support */
#undef WSERV_SUPPORT

/* Define this if you have <ieeefp.h> */
#undef HAVE_IEEEFP_H

/* Define this if you need strtoll() */
#undef NEED_STRTOLL

/* Define this if you have <termios.h> */
#undef HAVE_TERMIOS_H

/* Define this if you have <sys/termios.h> */
#undef HAVE_SYS_TERMIOS_H

/* Define this if you have <xlocale.h> */
#undef HAVE_XLOCALE_H

/* Define this if you have newlocale() */
#undef HAVE_NEWLOCALE

/* Define this if you have embeddable python */
#undef HAVE_PYTHON

/* Define this if you have ldflags to link python */
#undef PYTHON_LDFLAGS

/* Define this if you have cflags to link python */
#undef PYTHON_CFLAGS

/* Define this if you have python */
#undef PYTHON_O

/* Define this if newlocale() requires #define _GNU_SOURCE to work */
#undef NEWLOCALE_REQUIRES__GNU_SOURCE

/* Define this if newlocale() doesn't appear to work properly */
#undef NEWLOCALE_DOESNT_WORK

/* Define this if your compiler supports __attribute__((fallthrough)) */
#undef HAVE_ATTRIBUTE_FALLTHROUGH

/* Define this if you have va_copy */
#undef HAVE_VA_COPY

/* Define this if you have __va_copy */
#undef HAVE___VA_COPY

/* Define this if va_list is assignable */
#undef VA_LIST_ASSIGNABLE
